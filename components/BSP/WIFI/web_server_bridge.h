#ifndef WEB_SERVER_BRIDGE_H
#define WEB_SERVER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi配网信息提交回调
 * @param ssid  用户输入的WiFi名称
 * @param pass  用户输入的WiFi密码
 */
typedef void (*wifi_config_submit_cb)(const char *ssid, const char *pass);

/**
 * @brief 注册配网页面提交回调（收到手机发来的SSID/密码时触发）
 * @param cb 回调函数指针
 */
void web_server_reg_wifi_config_cb(wifi_config_submit_cb cb);

/**
 * @brief 启动配网HTTP服务器
 * @param port 监听端口，通常为80
 */
void web_server_start(uint16_t port);

/**
 * @brief 停止配网HTTP服务器（未启动时调用也安全）
 */
void web_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif
