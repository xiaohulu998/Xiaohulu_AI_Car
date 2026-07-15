#ifndef __AP_WIFI_H
#define __AP_WIFI_H

#include "wifi_manager.h"


/** wifi功能初始化
 * @param f 状态通知回调函数
 * @return 无
*/
void ap_wifi_init(p_wifi_state_callback f);


void ap_wifi_connect();

/** 进入AP配网
 * @return 无
*/
void ap_wifi_apcfg(void);

#endif
