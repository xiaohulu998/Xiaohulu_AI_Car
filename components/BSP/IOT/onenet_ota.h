#ifndef _ONENET_OTA_H_
#define _ONENET_OTA_H_

/**
 * 获取应用程序版本号
 * @param 无
 * @return 版本号
 */
const char* get_app_version(void);


/**
 * 设置合法启动分区
 * @param vaild 是否合法
 * @return 无
 */
void set_app_valid(int valid);


#endif