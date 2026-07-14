#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_
#include "esp_err.h"
#include "esp_wifi.h"


typedef enum
{
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
}WIFI_STATE;



//wifi状态变化回调函数
typedef void (*p_wifi_state_callback)(WIFI_STATE state);

//扫描完成回调函数 参数1.返回个数；2. AP 接入设备信息
typedef void (*p_wifi_scan_callback)(int num, wifi_ap_record_t *ap_record);


/** 初始化wifi，默认进入STA模式
 * @param f wifi状态变化回调函数
 * @return 无 
*/
void wifi_manager_init(p_wifi_state_callback f);

/** 连接wifi
 * @param ssid
 * @param password
 * @return 成功/失败
*/
esp_err_t wifi_manager_connect(const char* ssid,const char* password);

/** 设置成AP模式
 * @param  void 无
 * @return 成功/失败
*/
esp_err_t wifi_manager_ap(void);


/** 启动扫描函数
 * @param  f 扫描完成回调函数
 * @return 成功/失败
*/
esp_err_t wifi_manager_scan(p_wifi_scan_callback f);


#endif
