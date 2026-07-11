#ifndef WIFI_MODE_H
#define WIFI_MODE_H

#include "wifi_nvs.h"
#include <stdbool.h>

#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"  //FreeRTOS 内核原生事件标志组
#include "esp_event.h" //ESP-IDF 封装的系统事件总线
#include "web_server_bridge.h"


//宏定义
/* AP 模式默认热点名称 */
#define AP_SSID "ESP32_WIFI_CONFIG"
/* AP 模式默认密码（WPA2 要求至少 8 位） */
#define AP_PASS "12345678"

/* 事件组标志位：Wi-Fi 连接成功并获取到 IP */
#define WIFI_CONNECTED_BIT BIT0



// 启动AP配网模式（开启热点+网页服务）
void wifi_start_ap_mode(void);
// 启动STA站点模式（连接路由器）
bool wifi_start_sta_mode(const char *ssid, const char *pass);
// 关闭WiFi、释放资源
void wifi_stop_all(void);

// WiFi联网状态回调
typedef void (*wifi_sta_connected_cb)(void);
void wifi_set_sta_connect_cb(wifi_sta_connected_cb cb);

#endif