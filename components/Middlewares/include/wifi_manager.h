/**
* WiFi 连接管理 & AP 配网 & WebSocket 服务接口
*/
#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 *  通用类型定义
 * ================================================================ */

 /** WiFi 连接状态 */
typedef enum {
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
} WIFI_STATE;

/** WiFi 状态变化回调函数 */
typedef void (*p_wifi_state_callback)(WIFI_STATE state);

/** WiFi 扫描完成回调
 * @param num        扫描到的 AP 数量
 * @param ap_record  AP 信息数组
 */
typedef void (*p_wifi_scan_callback)(int num, wifi_ap_record_t *ap_record);


/** WebSocket 接收数据回调 */
typedef void (*web_receive_cb)(uint8_t *payload, int len);

/** WebSocket 服务配置 */
typedef struct {
    const char    *html_code;   /* HTTP GET / 时返回的 HTML 页面内容 */
    web_receive_cb receive_fn;  /* WebSocket 收到数据时的回调 */
} ws_cfg_t;

/* ================================================================
 *  WiFi 驱动管理
 * ================================================================ */

 /**
 * @brief  初始化 WiFi（TCP/IP + 事件循环 + STA 默认模式）
 * @param  f  WiFi 状态变化回调
 */
void wifi_manager_init(p_wifi_state_callback f);

/**
 * @brief  STA 模式连接指定路由器
 * @param  ssid     WiFi 名称
 * @param  password WiFi 密码
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief  切换到 AP+STA 混杂模式（开启热点）
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_ap(void);

/**
 * @brief  启动 WiFi 扫描
 * @param  f  扫描完成回调
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_scan(p_wifi_scan_callback f);

/* ================================================================
 *  AP 智能配网
 * ================================================================ */

 /**
 * @brief  AP 配网初始化（WiFi 初始化 + 加载 HTML 网页）
 * @param  f  WiFi 状态变化回调
 */
void wifi_apcfg_init(p_wifi_state_callback f);

/**
 * @brief  进入 AP 配网模式（开启热点 + WebSocket 服务）
 */
void wifi_apcfg_start(void);

/* ================================================================
 *  HTTP + WebSocket 服务器
 * ================================================================ */

/**
 * @brief  启动 HTTP + WebSocket 服务器
 * @param  cfg  配置（HTML 页面 + 接收回调）
 * @return ESP_OK 成功
 */
esp_err_t ws_server_start(ws_cfg_t *cfg);

/**
 * @brief  停止 HTTP + WebSocket 服务器
 * @return ESP_OK 成功
 */
esp_err_t ws_server_stop(void);

/**
 * @brief  通过 WebSocket 向客户端发送文本数据
 * @param  data  数据内容
 * @param  len   数据长度
 * @return ESP_OK 成功
 */
esp_err_t ws_server_send(uint8_t *data, int len);

#endif
