#include "wifi_mode.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "event_groups.h"
#include "esp_event.h"
#include "web_server_bridge.h"

#define AP_SSID "ESP32_WIFI_CONFIG"
#define AP_PASS "12345678"
#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group;
static wifi_sta_connected_cb sta_conn_cb = NULL;
static esp_event_handler_instance_t s_wifi_evt_handle = NULL;
static esp_event_handler_instance_t s_ip_evt_handle   = NULL;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if(event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if(sta_conn_cb) sta_conn_cb();
    }
}

void wifi_set_sta_connect_cb(wifi_sta_connected_cb cb)
{
    sta_conn_cb = cb;
}

void wifi_start_ap_mode(void)
{
    wifi_stop_all();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = AP_SSID,
            .password = AP_PASS,
            .ssid_len = strlen(AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        }
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();
    // 启动网页配网服务（桥接C++ WebServer）
    web_server_start(80);
}

bool wifi_start_sta_mode(const char *ssid, const char *pass)
{
    wifi_stop_all();
    wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_evt_handle);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_ip_evt_handle);

    wifi_config_t sta_cfg = {0};
    strcpy((char*)sta_cfg.sta.ssid, ssid);
    strcpy((char*)sta_cfg.sta.password, pass);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();
    esp_wifi_connect();

    // 等待10秒联网
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    if(bits & WIFI_CONNECTED_BIT)
        return true;
    return false;
}

void wifi_stop_all(void)
{
    web_server_stop(); // 关闭配网网页服务（未启动时安全）

    // 注销STA事件处理器（避免模式切换时重复注册）
    if (s_wifi_evt_handle) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_evt_handle);
        s_wifi_evt_handle = NULL;
    }
    if (s_ip_evt_handle) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_evt_handle);
        s_ip_evt_handle = NULL;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
}