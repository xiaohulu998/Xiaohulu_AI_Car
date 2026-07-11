#ifndef WIFI_NVS_H
#define WIFI_NVS_H

#include <stdbool.h>
#include <stdint.h>
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>


#define WIFI_SSID_LEN 33
#define WIFI_PASS_LEN 65

/* NVS 命名空间：所有 Wi-Fi 相关键值对存放在 "wifi_info" 下 */
#define NVS_NAMESPACE "wifi_info"

/* 键名：SSID / 密码 */
#define KEY_SSID "ssid"
#define KEY_PASS "pass"

// 存储WiFi信息结构体
typedef struct {
    char ssid[WIFI_SSID_LEN];
    char password[WIFI_PASS_LEN];
    bool valid; // true=存有有效账号
} wifi_store_t;

// 读取NVS保存的WiFi
bool wifi_nvs_load(wifi_store_t *out_cfg);
// 写入WiFi到NVS
bool wifi_nvs_save(const wifi_store_t *cfg);
// 清空NVS保存的WiFi
void wifi_nvs_clear(void);

#endif