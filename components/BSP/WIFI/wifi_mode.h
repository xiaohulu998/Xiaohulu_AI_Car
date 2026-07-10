#ifndef WIFI_MODE_H
#define WIFI_MODE_H

#include "wifi_nvs.h"
#include <stdbool.h>

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