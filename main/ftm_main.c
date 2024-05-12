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
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rom/ets_sys.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_console.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "trilateration.h"
#include "event_source.h"

#define CSI_QUEUE_SIZE 32
#define CONFIG_LESS_INTERFERENCE_CHANNEL 11
#define ESP_WIFI_SSID "ftm_tag"
#define ESP_WIFI_PASS "ftmftmftmftm"

// 设定为大于最大可能的AP数
#define MAX_APS 5
// 本设备名称
static const char *TAG_STA = "ftm_tag";

// 设定STA（本设备）的MAC地址
static const uint8_t CONFIG_CSI_SEND_MAC[] = {0x1a, 0x00, 0x00, 0x00, 0x00, 0x00};
// 设定AP的MAC地址
uint8_t mac_add[MAX_APS][6] = {{0x70, 0x04, 0x1D, 0x4C, 0xCF, 0x8C},
                               {0x70, 0x04, 0x1D, 0x4C, 0xD0, 0x78},
                               {0x70, 0x04, 0x1D, 0x4C, 0xD0, 0x4C}};
// AP位置坐标，二维，一维存储，预输入
float ap_pos[6] = {54.12, 65.6, 33.87, 25.25, 87.16, 58.25};

// 设定好的FTMAP设备全集的部分信息
typedef struct
{
    uint8_t mac_address[6];
    float pos_x;
    float pos_y;
    wifi_csi_info_t csi_info;
    bool is_csi_empty;
    double volatile dist_est;
} AP_Info;
AP_Info AP_Infomation[MAX_APS];

// 实时扫描到的支持FTM的AP设备信息
typedef struct
{
    wifi_ap_record_t ap_record;
    bool volatile is_valid;
    //实时记录当前设备是否有效
} ftm_APs_record;
// 记录AP设备信息表
ftm_APs_record ftm_APs_record_list[MAX_APS];


// 当前AP设备数目（包括已断开的），
// 当前ftm请求的设备序号，
// 目前仍有通讯的ap数目
uint8_t num_aps, current_ap, num_valid;
// 是否启用多线程
bool is_multi_task = false;

TaskHandle_t ftm_task_handle;
TaskHandle_t csi_task_handle;

static bool first_scan = true;
static bool s_reconnect = true;

uint8_t loop_time = 0; // 标示ftm循环次数

//扫描时用到的缓冲数组
uint16_t scanned_ap_num;
wifi_ap_record_t *ap_list_buffer;
wifi_ftm_report_entry_t *ftm_report;
uint8_t ftm_report_num_entries;


//任务组用到的标识bit，用于状态控制
static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t ftm_event_group;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
const int FTM_REPORT_BIT = BIT0;
const int FTM_FAILURE_BIT = BIT1;

static void initialize_ap_infomation()
{
    ESP_LOGI(TAG_STA, "constructing AP_Pos...");
    for (uint8_t i = 0; i < MAX_APS; i++)
    {
        memcpy(AP_Infomation[i].mac_address, mac_add[i], 6 * sizeof(uint8_t));
        AP_Infomation[i].pos_x = ap_pos[2 * i];
        AP_Infomation[i].pos_y = ap_pos[2 * i + 1];
        AP_Infomation[i].is_csi_empty = true;
    }
    ESP_LOGI(TAG_STA, "initialise csi info...");
}
static uint8_t get_ap_id_by_mac_from_list(uint8_t *peer_mac)
{
    for (uint8_t i = 0; i < MAX_APS; i++)
    {
        if (memcmp(peer_mac, ftm_APs_record_list[i].ap_record.bssid, 6 * sizeof(uint8_t)) == 0)
        {
            return i;
        }
    }
    return 255;
}
static uint8_t get_ap_pos_by_mac(uint8_t *peer_mac)
{
    peer_mac[5]--; // 因为STA/AP混合模式，这里的peermac（AP接口）会比原始值（STA接口值）大1
    for (uint8_t i = 0; i < MAX_APS; i++)
    {
        // debug用的：
        //  printf("get pos by MAC address: ");
        //  for (uint8_t j = 0; j < 6; j++)
        //  {
        //      printf("%02X", AP_Infomation[i].mac_address[j]);

        // }
        // printf("\n");
        // for (uint8_t j = 0; j < 6; j++)
        // {
        //     printf("%02X", peer_mac[j]);
        // }

        if (memcmp(peer_mac, AP_Infomation[i].mac_address, 6 * sizeof(uint8_t)) == 0)
        {
            return i;
        }
    }
    return 255;
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

    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
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
    first_scan = false;
    return true;
}

static void wifi_ftm_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG_STA, "FTM handler");
    // esp_wifi_ftm_end_session();
    // wifi_csi_info_t *csi = (wifi_csi_info_t *)event_data;
    wifi_event_ftm_report_t *event = (wifi_event_ftm_report_t *)event_data;
    // 获取到的ftm信息

    if (event->status == FTM_STATUS_SUCCESS)
    {
        ESP_LOGI(TAG_STA, "FTM reported successfully.");

        ftm_report = event->ftm_report_data;
        ftm_report_num_entries = event->ftm_report_num_entries;
        printf("=====>>>  FTM raw result, dist: %dmm, RTT: %df\n", (int)event->dist_est, (int)event->rtt_raw);

        uint8_t ap_id = get_ap_id_by_mac_from_list(event->peer_mac);
        uint8_t ap_local_storage_id = get_ap_pos_by_mac(event->peer_mac);
        if (ap_local_storage_id == 255)
        {
            ESP_LOGW(TAG_STA, "can't find mac.");
        }


        printf("data for predicting:  RSSI: %d, RTT: %d, APid: %d, AP_X: %.2f, AP_Y: %.2f",
               (int)event->ftm_report_data->rssi,
               (int)event->rtt_raw,
               ap_id,
               AP_Infomation[ap_local_storage_id].pos_x,
               AP_Infomation[ap_local_storage_id].pos_y);
        if (!AP_Infomation[ap_local_storage_id].is_csi_empty)
        {
            wifi_csi_info_t *info = &AP_Infomation[ap_local_storage_id].csi_info;
            const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;
            ets_printf("CSI_DATA,%d," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                       0, MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
                       rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
                       rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
                       rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
                       rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state);
            ets_printf(",%d,%d,\"[%d", info->len, info->first_word_invalid, info->buf[0]);
            for (int i = 1; i < info->len; i++)
            {
                ets_printf(",%d", info->buf[i]);
            }
            ets_printf("]\"\n");
        }

        AP_Infomation[ap_local_storage_id].dist_est= (double)event->dist_est;


        // if (is_multi_task)
        // {
        //     if (ap_id != 255)
        //     {
        //         AP_Infomation[ap_local_storage_id].dist_est= (double)event->dist_est;
        //     }
        //     else
        //     {
        //         // 理论上不可能出现，仅发生在AP记录列表丢失时
        //         ESP_LOGW(TAG_STA, "Unexpected FTM Report received, wait for next scan.\n");
        //     }
        // }
        // else
        // {
        //     ftm_APs_record_list[current_ap].dist_est = (double)event->dist_est; // 存储一个循环内的ftm距离信息
        // }

        xEventGroupSetBits(ftm_event_group, FTM_REPORT_BIT);
        // ESP_ERROR_CHECK(esp_event_post(CSI_EVENT,CSI_LISTEN_START_EVENT,NULL,0,portMAX_DELAY));
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
            ESP_LOGE(TAG_STA, "FTM procedure with Peer( " MACSTR " ) failed! (Status - FTM_STATUS_CONF_REJECTED)",
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

static void wifi_csi_handler(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf)
    {
        ESP_LOGW(TAG_STA, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    // if (memcmp(info->mac, CONFIG_CSI_SEND_MAC, 6))
    // {
    //     return;
    // }
    uint8_t ap_csi_id = get_ap_pos_by_mac(info->mac);
    if (ap_csi_id == 255)
    {
        // csi源不在记录中
        return;
    }

    static int s_count = 0;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

    // ESP_LOGI(TAG_STA,"csi info detected and collected.");
    //ESP_LOGI(TAG_STA, "csi from mac: " MACSTR "has been stored.", MAC2STR(info->mac));
    AP_Infomation[ap_csi_id].csi_info = *info;
    AP_Infomation[ap_csi_id].is_csi_empty = false;

    // if (!s_count)
    // {
    //     ESP_LOGI(TAG_STA, "================ CSI RECV ================");
    //     ets_printf("type,id,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
    // }

    // ets_printf("CSI_DATA,%d," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
    //            s_count++, MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
    //            rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
    //            rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
    //            rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
    //            rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state);
    // ets_printf(",%d,%d,\"[%d", info->len, info->first_word_invalid, info->buf[0]);
    // for (int i = 1; i < info->len; i++)
    // {
    //     ets_printf(",%d", info->buf[i]);
    // }
    // ets_printf("]\"\n");

    // ESP_ERROR_CHECK(esp_event_post(CSI_EVENT,CSI_LISTEN_START_EVENT,NULL,0,portMAX_DELAY));
}

static int execute_ftm(wifi_ap_record_t *ap_record, uint8_t ap_id)
{
    ESP_LOGI(TAG_STA, "Executing FTM...");

    if (!is_multi_task)
    {
        ap_id = num_aps;
        //这是简略方式，多线程下需要逐一对比mac地址
    }

    wifi_ftm_initiator_cfg_t ftm_cfg = {
        .frm_count = 16,
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

    ESP_LOGI(TAG_STA, "Requesting FTM session with Frm Count - %d, Burst Period - %dmSec (0: No Preference)", ftm_cfg.frm_count, ftm_cfg.burst_period * 100);

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

static void initialise_csi(void *parameter)
{
    // vTaskSuspend(ftm_task_handle);
    //  csi_info_queue = xQueueCreate(CSI_QUEUE_SIZE, sizeof(wifi_csi_info_t));
    //  if (csi_info_queue == NULL) {
    //      ESP_LOGE(TAG_STA, "Create queue fail");
    //      return;
    //  }
    while (1)
    {
        ESP_LOGI(TAG_STA, "csi task called.");
        ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, WIFI_PHY_RATE_MCS0_SGI));
        // ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
        ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, CONFIG_CSI_SEND_MAC));
        ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
        // 致谢：吴老师牛批
        //  ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(g_wifi_radar_config->wifi_sniffer_cb));

        /**< default config */
        wifi_csi_config_t csi_config = {
            .lltf_en = true,
            .htltf_en = true,
            .stbc_htltf2_en = true,
            .ltf_merge_en = true,
            .channel_filter_en = true,
            .manu_scale = false,
            .shift = false,
        };
        ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
        ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_handler, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_csi(true));
        ESP_LOGI(TAG_STA, "csi listener configured done.");
        vTaskDelay(500);
    }
    // ESP_LOGI(TAG_STA, "csi task called.");
    // ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    // // ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(g_wifi_radar_config->wifi_sniffer_cb));

    // /**< default config */
    // wifi_csi_config_t csi_config = {
    //     .lltf_en = true,
    //     .htltf_en = true,
    //     .stbc_htltf2_en = true,
    //     .ltf_merge_en = true,
    //     .channel_filter_en = true,
    //     .manu_scale = false,
    //     .shift = false,
    // };
    // ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    // ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_handler, NULL));
    // ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    // ESP_LOGI(TAG_STA, "csi listener configured done.");

    // uint8_t delay = 0;
    //  while(1){
    //      vTaskDelay(1);
    //      delay++;
    //      if(delay>=1000){
    //          delay = 0;
    //          ESP_LOGI(TAG_STA,"yes, csi task is still runing.");
    //          //vTaskResume(ftm_task_handle);
    //      }
    //  };
    //  ESP_ERROR_CHECK(esp_event_post(CSI_EVENT,))
    // ESP_ERROR_CHECK(esp_event_post_to(csi_task_loop_handle,CSI_EVENT,CSI_LISTEN_START_EVENT,NULL,0,portMAX_DELAY));
}

static void execute_localization()
{

    //this function has been transplantied to PC.

    // double res[2] = {-1, -1};
    // int nodeList_size[2] = {2, 2};
    // int disList_size[2] = {1, 2};
    // // num_aps = 3;
    // // double dist_test[8] = {1.155,1.155,1.115,1.155,1.155,1.115,1.155,1.155};
    // for (int i = 0; i < 3; i++)
    // {
    //     ftm_dist_est_buffer[i] = ftm_APs_record_list[i].dist_est;
    // }
    // trilateration(num_aps, ap_pos, nodeList_size, ftm_dist_est_buffer, disList_size, res);
    // printf("------>localization result: [%d,%d]\n", (int)res[0], (int)res[1]);
    // // vTaskDelay(500);


}

void request_ap_task()
{
    while (1)
    {
        for (uint8_t ap_id = 0; ap_id < MAX_APS; ap_id++)
        {
            char task_name[8];
            sprintf(task_name, "task%u", ap_id);
            if (ftm_APs_record_list[ap_id].is_valid)
            {
                execute_ftm(&ftm_APs_record_list[ap_id].ap_record, ap_id);
                // 正常情况下这里触发一次ftm_handler
                vTaskDelay(100);
            }
            else
            {
                ESP_LOGW(task_name, " connected an invalid AP.");
            }
            // 测试数据
            // printf("data for predicting:  RSSI: %.2f, RTT: %.2f\n",13.62, 9.13);
        }
        // uint8_t ap_id = (uint8_t *)pvParameters;
        if (num_valid >= 3)
        {
            ESP_LOGI(TAG_STA, "excuting localization...");
            // execute_localization();
        }
        else
        {
            ESP_LOGW(TAG_STA, "need more valid APs, scanning...");
            wifi_perform_scan(NULL, false);
        }

        if (loop_time >= 10)
        {
            ESP_LOGI(TAG_STA, "excuting gradual scanning...");
            wifi_perform_scan(NULL, false);
            loop_time = 0;
        }
        vTaskDelay(100);
    }
    // for (uint8_t ap_id = 0; ap_id < MAX_APS; ap_id++)
    // {
    //     char task_name[8];
    //     sprintf(task_name, "task%u", ap_id);
    //     if (ftm_APs_record_list[ap_id].is_valid)
    //     {
    //         execute_ftm(&ftm_APs_record_list[ap_id].ap_record, ap_id);
    //         // 正常情况下这里触发一次ftm_handler
    //         vTaskDelay(200);
    //     }
    //     else
    //     {
    //         ESP_LOGW(task_name, " connected an invalid AP.");
    //     }
    //     // 测试数据
    //     // printf("data for predicting:  RSSI: %.2f, RTT: %.2f\n",13.62, 9.13);
    // }
    // // uint8_t ap_id = (uint8_t *)pvParameters;
    // if (num_valid >= 3)
    // {
    //     ESP_LOGI(TAG_STA, "excuting localization...");
    //     // execute_localization();
    // }
    // else
    // {
    //     ESP_LOGW(TAG_STA, "need more valid APs, scanning...");
    //     wifi_perform_scan(NULL, false);
    // }
    // //ESP_ERROR_CHECK(esp_event_post(CSI_EVENT,CSI_LISTEN_START_EVENT,NULL,0,portMAX_DELAY));
    // vTaskDelay(150);
    // if (loop_time >= 10)
    // {
    //     ESP_LOGI(TAG_STA, "excuting gradual scanning...");
    //     wifi_perform_scan(NULL, false);
    //     loop_time = 0;
    // }
    // ESP_ERROR_CHECK(esp_event_post(FTM_EVENT,FTM_LOOP_START_EVENT,NULL,0,portMAX_DELAY));
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

void infinite_loop(void *p)
{
    for (int i = 0;; i++)
    {
        ESP_LOGI(TAG_STA, "I am infinite loop.");
        vTaskDelay(100);
        if (i >= 10)
        {
            i = 0;
        }
    }
}

void init_ftm_task()
{
    xTaskCreate(initialise_csi, "csi_handler_task", 1024, NULL, 4, csi_task_handle);
    xTaskCreate(request_ap_task, "request_ap_task", 4096, NULL, 1, ftm_task_handle);
    // xTaskCreate(infinite_loop,"infinite_loop", 1024, NULL, 1, NULL);
    // request_ap_task();
    //  is_multi_task = true;
    //  ESP_LOGI(TAG_STA, "preparing init ftm tasks...");
    //  xTaskCreate(request_ap_task, "taskThread", TASK_STK_SIZE, NULL, TASK_PRIO, ftm_task_handle);
    //  ESP_LOGI(TAG_STA, "preparing init support tasks...");
    //  xTaskCreate(localization_task, "localizationThread", TASK_STK_SIZE, NULL, TASK_PRIO, localization_task_handle);
    //  xTaskCreate(scan_ap_list_task,"scanThread",TASK_STK_SIZE,NULL, TASK_PRIO+1,scan_task_handle);
    // vTaskStartScheduler();
    //  ESP_LOGI(TAG_STA, "task init done.");
    // request_ap_task();
    // ESP_ERROR_CHECK(esp_event_post_to(csi_task_loop_handle,CSI_EVENT,CSI_LISTEN_START_EVENT,NULL,0,portMAX_DELAY));
    // ESP_ERROR_CHECK(esp_event_post(CSI_EVENT,CSI_LISTEN_START_EVENT,NULL,0,portMAX_DELAY));
    // ESP_ERROR_CHECK(esp_event_post(FTM_EVENT,FTM_LOOP_START_EVENT,NULL,0,portMAX_DELAY));
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
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_CONNECTED,
        &wifi_connected_handler,
        NULL,
        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_DISCONNECTED,
        &disconnect_handler,
        NULL,
        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_FTM_REPORT,
        &wifi_ftm_handler,
        NULL,
        NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT40));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, WIFI_PHY_RATE_MCS0_SGI));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, CONFIG_CSI_SEND_MAC));

    initialized = true;
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

    // 初始化csi配置
    // initialise_csi();
    // ESP_LOGI(TAG_STA, "wifi csi configured done.");

    initialize_ap_infomation();
    num_aps = 0;
    current_ap = -1;
    wifi_perform_scan(NULL, false); // 扫AP，除第一次外均为被动扫描
    init_ftm_task();

    // 维持一个序列OK
    // ftm并行OK
    // 被动扫描OK
}
