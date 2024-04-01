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

#define MAX_APS 8

struct ftm_APs_record
{
    wifi_ap_record_t ap_record;
    bool is_valid;
    double dist_est;
};
// AP位置坐标，二维，一维存储，预输入
double ap_pos[6] = {54, 65, 33, 25, 87, 58};

ESP_EVENT_DEFINE_BASE(END_SCAN_OR_FTM_EVENT);

static bool first_scan = true;
static bool s_reconnect = true;
static const char *TAG_STA = "ftm_tag";
uint8_t num_aps, current_ap;
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

    if (!first_scan)
    {
        scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
    }

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
    } // 清空一下ap记录

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
            for (i = 0; i < scanned_ap_num; i++)
            {
                if (num_aps < MAX_APS && ap_list_buffer[i].ftm_responder)
                {
                    aps[num_aps] = ap_list_buffer[i];
                    num_aps += 1;
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
        printf("=====>>>  FTM raw result, dist: %dmm, RTT: %d", event->dist_est, event->rtt_raw);

        ftm_dist_est_buffer[current_ap] = (double)event->dist_est; // 存储一个循环内的ftm距离信息

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

static int execute_ftm(wifi_ap_record_t *ap_record)
{
    ESP_LOGI(TAG_STA, "Executing FTM...");

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
        return 0;
    }

    // ESP_LOGI(TAG_STA, "Requesting FTM session with Frm Count - %d, Burst Period - %dmSec (0: No Preference)",ftm_cfg.frm_count, ftm_cfg.burst_period * 100);

    if (ESP_OK != esp_wifi_ftm_initiate_session(&ftm_cfg)) // 启动ftm！
    {
        ESP_LOGE(TAG_STA, "Failed to start FTM session.");
        // ESP_ERROR_CHECK(esp_event_post(END_SCAN_OR_FTM_EVENT, 0, NULL, 0, pdMS_TO_TICKS(100)));
        return 0;
    }

    return 0;
}

static void execute_localization()
{
    double res[2] = {-1, -1};
    int nodeList_size[2] = {2, 2};
    int disList_size[2] = {1, 2};
    // num_aps = 3;
    // double dist_test[8] = {1.155,1.155,1.115,1.155,1.155,1.115,1.155,1.155};
    trilateration(num_aps, ap_pos, nodeList_size, ftm_dist_est_buffer, disList_size, res);
    printf("\n------>localization result: [%d,%d]", (int)res[0], (int)res[1]);
    vTaskDelay(500);
}

static int proccess_next_ap()
{

    current_ap += 1;
    // int res = execute_ftm(&aps[current_ap]);
    if (current_ap >= num_aps)
    {
        current_ap = 0;
        // execute_localization();//调试用
        if (num_aps == 3)
        {
            execute_localization(); // 一次循环结束，执行定位
        }
    }

    ESP_LOGI(TAG_STA, "Proccess_next_ap %d", current_ap);
    return execute_ftm(&aps[current_ap]);
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
    wifi_perform_scan(NULL, false); // 开始扫AP
    // 维持一个序列
    // ftm并行
    // 被动扫描
    while (1)
    {
        proccess_next_ap();
        vTaskDelay(500);
    }
}
