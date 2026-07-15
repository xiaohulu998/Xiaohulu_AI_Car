#include "ap_wifi.h"
#include "wifi_manager.h"
#include "esp_spiffs.h"

#define  SPIFFS_BASE_PATH   "/spiffs"

/** 从spiffs中加载html页面到内存   //Flash 读取速度远慢于 RAM，网页卡顿
 * @param 无
 * @return 无 
*/
char *init_web_page_buffer()
{
    

}


typedef void (*p_wifi_state_callback)(WIFI_STATE state);


/** wifi功能初始化
 * @param f 状态通知回调函数
 * @return 无
*/
void ap_wifi_init()
{
    wifi_manager_init(f); //


}




void ap_wifi_connect();

/** 进入AP配网
 * @return 无
*/
void ap_wifi_apcfg(void);
