#ifndef __AP_WIFI_H
#define __AP_WIFI_H

#include "wifi_manager.h"


/** wifi功能初始化
 * @param f 状态通知回调函数
 * @return 无
*/
void ap_wifi_init(void);


void ap_wifi_connect(void);

/** 进入AP配网
 * @return 无
*/
void ap_wifi_apcfg(void);

#endif
