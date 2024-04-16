/* Wi-Fi FTM Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_console.h"
#include "trilateration.h"

// 设定为大于最大可能的AP数
#define MAX_APS 5

// 支持FTM的AP设备信息
typedef struct
{
    wifi_ap_record_t ap_record;
    bool volatile is_valid;
    double volatile dist_est;
} ftm_APs_record;

// AP位置坐标，二维，一维存储，预输入
double ap_pos[6] = {54, 65, 33, 25, 87, 58};
// 记录AP设备信息链表
ftm_APs_record ftm_APs_record_list[MAX_APS];
// 当前AP设备数目（包括已断开的），
// 当前ftm请求的设备序号，
// 目前仍有通讯的ap数目
uint8_t num_aps, current_ap, num_valid;
// 是否启用多线程
bool is_multi_task = false;

#define TASK_PRIO 1        // 任务优先级
#define TASK_STK_SIZE 4096 // 任务堆栈大小
TaskHandle_t ftm_task_handle;
TaskHandle_t localization_task_handle;
TaskHandle_t scan_task_handle;

ESP_EVENT_DEFINE_BASE(END_SCAN_OR_FTM_EVENT);
static bool first_scan = true;
static bool s_reconnect = true;
static const char *TAG_STA = "ftm_tag";
wifi_ap_record_t aps[MAX_APS];

uint16_t scanned_ap_num;
wifi_ap_record_t *ap_list_buffer;

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;

static EventGroupHandle_t ftm_event_group;
const int FTM_REPORT_BIT = BIT0;
const int FTM_FAILURE_BIT = BIT1;
wifi_ftm_report_entry_t *ftm_report;
uint8_t ftm_report_num_entries;
// 存储ftm测距信息
double ftm_dist_est_buffer[MAX_APS];

static uint8_t get_ap_id_by_mac_from_list(uint8_t *peer_mac)
{
    for (uint8_t i = 0; i < MAX_APS; i++)
    {
        if (memcmp(peer_mac, ftm_APs_record_list[i].ap_record.bssid, 6 * sizeof(uint8_t)) == 0)
        {
            return i;
        }
    }
    return -1;
}

static void wifi_connected_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    // wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;

    xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (s_reconnect)
    {
        // ESP_LOGI(TAG_STA, "sta disconnect, s_reconnect...");
        esp_wifi_connect();
    }
    else
    {
        // ESP_LOGI(TAG_STA, "sta disconnect");
    }
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
}

static bool wifi_perform_scan(const char *ssid, bool internal)
{
    wifi_scan_config_t scan_config = {0};
    // scan_config.ssid = (uint8_t *) ssid;
    // scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    uint8_t i;

    /*if (!first_scan)
    {
        scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
    }*/

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    esp_wifi_scan_get_ap_num(&scanned_ap_num);
    if (scanned_ap_num == 0)
    {
        ESP_LOGI(TAG_STA, "cannot found any matching APs.");
        return false;
    }

    if (ap_list_buffer)
    {
        free(ap_list_buffer);
    } // 清空ap记录

    ap_list_buffer = malloc(scanned_ap_num * sizeof(wifi_ap_record_t));
    if (ap_list_buffer == NULL)
    {
        ESP_LOGE(TAG_STA, "malloc falied.");
        return false;
    }

    if (esp_wifi_scan_get_ap_records(&scanned_ap_num, (wifi_ap_record_t *)ap_list_buffer) == ESP_OK)
    {
        if (!internal)
        {
            num_aps = 0;
            num_valid = 0;
            ftm_APs_record ftm_ap_record = {
                .dist_est = -1,
                .is_valid = false,
            };
            for (i = 0; i < scanned_ap_num; i++)
            {
                if (num_aps < MAX_APS && ap_list_buffer[i].ftm_responder)
                {
                    ftm_ap_record.ap_record = ap_list_buffer[i];
                    ftm_APs_record_list[num_aps] = ftm_ap_record;
                    ftm_APs_record_list[num_aps].is_valid = true;
                    num_aps += 1;
                    num_valid += 1;
                }
                // ESP_LOGI(TAG_STA, "[%s][rssi=%d]""%s", ap_list_buffer[i].ssid, ap_list_buffer[i].rssi, ap_list_buffer[i].ftm_responder ? "[FTM Responder]" : "");
            }
        }
    }
    ESP_LOGI(TAG_STA, "Scan done, %d aps found", num_aps);
    // 相较于原示例代码提取了符合条件的ap数量
    first_scan = false;
    return true;
}

static void ftm_report_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG_STA, "FTM handler");

    wifi_event_ftm_report_t *event = (wifi_event_ftm_report_t *)event_data;
    // 获取到的ftm信息

    if (event->status == FTM_STATUS_SUCCESS)
    {
        ESP_LOGI(TAG_STA, "FTM reported successfully.");

        ftm_report = event->ftm_report_data;
        ftm_report_num_entries = event->ftm_report_num_entries;
        printf("=====>>>  FTM raw result, dist: %dmm, RTT: %d\n", event->dist_est, event->rtt_raw);

        if (is_multi_task)
        {
            uint8_t ap_id = get_ap_id_by_mac_from_list(event->peer_mac);
            if (ap_id != -1)
            {
                ftm_APs_record_list[ap_id].dist_est = (double)event->dist_est;
            }
            else
            {
                // 理论上不可能出现，仅发生在AP记录列表丢失时
                ESP_LOGW(TAG_STA, "Unexpected FTM Report received, wait for next scan.\n");
            }
        }
        else
        {
            ftm_APs_record_list[current_ap].dist_est = (double)event->dist_est; // 存储一个循环内的ftm距离信息
        }

        xEventGroupSetBits(ftm_event_group, FTM_REPORT_BIT);
    }
    else
    {
        if (event->status == FTM_STATUS_UNSUPPORTED)
        {
            ESP_LOGE(TAG_STA, "FTM procedure with Peer(" MACSTR ") failed! (Status - FTM_STATUS_UNSUPPORTED)",
                     MAC2STR(event->peer_mac));
        }
        else if (event->status == FTM_STATUS_CONF_REJECTED)
        {
            ESP_LOGE(TAG_STA, "FTM procedure with Peer(" MACSTR ") failed! (Status - FTM_STATUS_CONF_REJECTED)",
                     MAC2STR(event->peer_mac));
        }
        else if (event->status == FTM_STATUS_NO_RESPONSE)
        {
            ESP_LOGE(TAG_STA, "FTM procedure with Peer(" MACSTR ") failed! (Status - FTM_STATUS_NO_RESPONSE)",
                     MAC2STR(event->peer_mac));
        }
        else if (event->status == FTM_STATUS_FAIL)
        {
            ESP_LOGE(TAG_STA, "FTM procedure with Peer(" MACSTR ") failed! (Status - FTM_STATUS_FAIL)",
                     MAC2STR(event->peer_mac));
        }
        xEventGroupSetBits(ftm_event_group, FTM_FAILURE_BIT);
    }
}

static int execute_ftm(wifi_ap_record_t *ap_record, uint8_t ap_id)
{
    ESP_LOGI(TAG_STA, "Executing FTM...");

    if (!is_multi_task)
    {
        ap_id = num_aps;
    }

    wifi_ftm_initiator_cfg_t ftm_cfg = {
        .frm_count = 32,
        .burst_period = 2,
    };

    if (ap_record)
    {
        ESP_LOGI(TAG_STA, "Starting FTM with " MACSTR " on channel %d\n", MAC2STR(ap_record->bssid), ap_record->primary);
        memcpy(ftm_cfg.resp_mac, ap_record->bssid, 6);
        ftm_cfg.channel = ap_record->primary;
        // 将读取到的ap信息写入到ftm配置中
    }
    else
    {
        ESP_LOGE(TAG_STA, "no AP record.");
        // ESP_ERROR_CHECK(esp_event_post(END_SCAN_OR_FTM_EVENT, 0, NULL, 0, pdMS_TO_TICKS(100)));
        ftm_APs_record_list[ap_id].is_valid = false;
        num_valid -= 1;
        return 0;
    }

    // ESP_LOGI(TAG_STA, "Requesting FTM session with Frm Count - %d, Burst Period - %dmSec (0: No Preference)",ftm_cfg.frm_count, ftm_cfg.burst_period * 100);

    if (ESP_OK != esp_wifi_ftm_initiate_session(&ftm_cfg)) // 启动ftm！
    {
        ESP_LOGE(TAG_STA, "Failed to start FTM session.");
        // ESP_ERROR_CHECK(esp_event_post(END_SCAN_OR_FTM_EVENT, 0, NULL, 0, pdMS_TO_TICKS(100)));
        ftm_APs_record_list[ap_id].is_valid = false;
        num_valid -= 1;
        return 0;
    }

    return 0;
}

void initialise_wifi(void)
{
    static bool initialized = false;

    if (initialized)
    {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ftm_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_CONNECTED,
                                                        &wifi_connected_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_DISCONNECTED,
                                                        &disconnect_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_FTM_REPORT,
                                                        &ftm_report_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    initialized = true;
}

static void execute_localization()
{
    double res[2] = {-1, -1};
    int nodeList_size[2] = {2, 2};
    int disList_size[2] = {1, 2};
    // num_aps = 3;
    // double dist_test[8] = {1.155,1.155,1.115,1.155,1.155,1.115,1.155,1.155};
    for (int i = 0; i < 3; i++)
    {
        ftm_dist_est_buffer[i] = ftm_APs_record_list[i].dist_est;
    }
    trilateration(num_aps, ap_pos, nodeList_size, ftm_dist_est_buffer, disList_size, res);
    printf("------>localization result: [%d,%d]\n", (int)res[0], (int)res[1]);
    // vTaskDelay(500);
}

void request_ap_task()
{
    uint8_t loop_time = 0;
    while (1)
    {
        for (uint8_t ap_id = 0; ap_id < MAX_APS; ap_id++)
        {
            char task_name[8];
            sprintf(task_name, "task%u", ap_id);
            if (ftm_APs_record_list[ap_id].is_valid)
            {
                execute_ftm(&ftm_APs_record_list[ap_id].ap_record, ap_id);
                vTaskDelay(200);
            }
            else
            {
                ESP_LOGW(task_name, " connected an invalid AP.");
            }
        }
        // uint8_t ap_id = (uint8_t *)pvParameters;
        if (num_valid >= 3)
        {
            ESP_LOGI(TAG_STA,"excuting localization...");
            execute_localization();
        }
        else
        {
            ESP_LOGW(TAG_STA,"need more valid APs, scanning...");
            wifi_perform_scan(NULL, false);
        }
        vTaskDelay(150);
        if(loop_time>=10){
            ESP_LOGI(TAG_STA,"excuting gradual scanning...");
            wifi_perform_scan(NULL, false);
            loop_time = 0;
        }
    }
}

void scan_ap_list_task()
{
    while (1)
    {
        vTaskDelay(10000);
        vTaskSuspend(ftm_task_handle);
        wifi_perform_scan(NULL, false);
        vTaskDelay(300);
        vTaskResume(ftm_task_handle);
    }
}
void init_ftm_task()
{
    is_multi_task = true;
    ESP_LOGI(TAG_STA, "preparing init ftm tasks...");
    // xTaskCreate(request_ap_task, "taskThread", TASK_STK_SIZE, NULL, TASK_PRIO, ftm_task_handle);
    ESP_LOGI(TAG_STA, "preparing init support tasks...");
    // xTaskCreate(localization_task, "localizationThread", TASK_STK_SIZE, NULL, TASK_PRIO, localization_task_handle);
    // xTaskCreate(scan_ap_list_task,"scanThread",TASK_STK_SIZE,NULL, TASK_PRIO+1,scan_task_handle);
    // vTaskStartScheduler();
    ESP_LOGI(TAG_STA, "task init done.");
    request_ap_task();
}

static int proccess_next_ap()
{

    current_ap += 1;
    // int res = execute_ftm(&aps[current_ap]);
    if (current_ap >= num_aps)
    {
        // 一次循环结束
        current_ap = 0;
        // execute_localization();//调试用
        if (num_valid >= 3)
        {
            execute_localization(); // 大于三个AP，执行定位
        }
        wifi_perform_scan(NULL, false); // 再次扫描，此次为被动模式
    }

    ESP_LOGI(TAG_STA, "Proccess_next_ap %d", current_ap);

    if (ftm_APs_record_list[current_ap].is_valid)
    {
        return execute_ftm(&ftm_APs_record_list[current_ap].ap_record, 0);
    }
    else
    {
        return 0;
    }
}

void app_main(void)
{
    // 初始化内存
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_STA, "flash initialized.");

    // 初始化wifi
    initialise_wifi();
    ESP_LOGI(TAG_STA, "wifi initialized.");

    num_aps = 0;
    current_ap = -1;
    wifi_perform_scan(NULL, false); // 扫AP，除第一次外均为被动扫描
    init_ftm_task();

    // 维持一个序列OK
    // ftm并行OK
    // 被动扫描OK
    // while (1)
    // {
    //     proccess_next_ap();
    //     vTaskDelay(300);
    // }
}
