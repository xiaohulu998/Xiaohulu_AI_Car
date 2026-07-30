#ifndef __ONENET_MQTT_H
#define __ONENET_MQTT_H

#include "esp_err.h"
#include "onenet_mqtt_key.h"


/*秘钥等信息在onenet_mqtt_key.h文件，未上传github，需自定义
格式为：

//1.主用户访问res为：userid/{userid}, 
// userid为平台用户id，在DMP控制台首页右上角查看。access_key为主用户access_key

// 平台用户ID
#define ONENET_USER_ID_KEY_KEY "userid"

// 主用户access_key
#define ONENET_USER_ACCESS_KE_KEY  "access_key"


// 2. 产品访问res为：products/{productid}，access_key为产品access_key
// 产品ID
#define ONENET_PRODUCT_ID_KEY "productid"

// 产品秘钥
#define ONENET_PRODUCT_ACCESS_KE_KEY "access_key"

// 3.其他秘钥
// 设备名称
#define ONENET_DEVICE_NAME_KEY "esp32_car"
// 定义时间戳
#define TM_EXPIRE_TIME_KEY 1889712420

    */



// 平台用户ID
 #define ONENET_USER_ID ONENET_USER_ID_KEY
// 主用户access_key
#define ONENET_USER_ACCESS_KE ONENET_USER_ACCESS_KE_KEY 

// 产品ID
#define ONENET_PRODUCT_ID  ONENET_PRODUCT_ID_KEY 
// 产品秘钥
#define ONENET_PRODUCT_ACCESS_KE  ONENET_PRODUCT_ACCESS_KE_KEY

// 设备名称
#define ONENET_DEVICE_NAME ONENET_DEVICE_NAME_KEY
// 定义时间戳
#define TM_EXPIRE_TIME TM_EXPIRE_TIME_KEY

/**
 * 启动mqtt连接
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_start(void);

/**
 * 订阅相关主题，有要订阅的主题可以放在这个函数
 * @param 无
 * @return 无
 */
void onenet_subscribe(void);


/**
 * 上报数据
 * @param data 数据
 * @return 错误
 */
esp_err_t onenet_post_property_data(const char* data);

/**
 * 发布属性获取应答到 get_reply 主题
 * @param data 已序列化的 get_reply JSON 字符串
 * @return 错误码
 */
esp_err_t onenet_get_property_data(const char *data);




#endif
