/**
* OneNET 云平台 MQTT 客户端 & 物模型 & 设备用户 Token 生成 & OTA 接口
*/

#ifndef _MQTT_ONENET_H_
#define _MQTT_ONENET_H_

#include "esp_err.h"
#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>
#include "mqtt_onenet_key.h"
/* ================================================================
 *  OneNET 平台三元组（真实key定义在 onenet_mqtt_key.h 中）
 * ================================================================ */

 /* 产品 ID */
#ifndef ONENET_PRODUCT_ID
#define ONENET_PRODUCT_ID    ONENET_PRODUCT_ID_KEY
#endif
/* 产品 access_key */
#ifndef ONENET_PRODUCT_ACCESS_KE
#define ONENET_PRODUCT_ACCESS_KE  ONENET_PRODUCT_ACCESS_KE_KEY
#endif
/* 设备名称 */
#ifndef ONENET_DEVICE_NAME
#define ONENET_DEVICE_NAME   ONENET_DEVICE_NAME_KEY
#endif
/* 平台用户 ID */
#ifndef ONENET_USER_ID
#define ONENET_USER_ID       ONENET_USER_ID_KEY
#endif
/* 用户 access_key */
#ifndef ONENET_USER_ACCESS_KE
#define ONENET_USER_ACCESS_KE ONENET_USER_ACCESS_KE_KEY
#endif
/* Token 过期时间戳 */
#ifndef TM_EXPIRE_TIME
#define TM_EXPIRE_TIME       TM_EXPIRE_TIME_KEY
#endif

/* ================================================================
 *  1.Token 签名方法
 * ================================================================ */
enum Escaped {
        WC_STD_ENC = 0,       /* normal \n line ending encoding */
        WC_ESC_NL_ENC,        /* use escape sequence encoding   */
        WC_NO_NL_ENC          /* no encoding at all             */
    }; /* Encoding types */


#ifndef byte
typedef unsigned char  byte;
#endif
typedef unsigned short word16;
typedef unsigned int   word32;
typedef byte           word24[3]; 	
	
int Base64_Decode(const byte* in, word32 inLen, byte* out,word32* outLen);
int Base64_Encode(const byte* in, word32 inLen, byte* out,word32* outLen);
int Base64_EncodeEsc(const byte* in, word32 inLen, byte* out,word32* outLen);
int Base64_Encode_NoNl(const byte* in, word32 inLen, byte* out,word32* outLen);

enum sig_method_e
{
    SIG_METHOD_MD5,
    SIG_METHOD_SHA1,
    SIG_METHOD_SHA256
};

/**
 * @brief  生成产品/设备级 Token（MQTT 设备接入用）
 * @param  token       输出缓冲区（至少 256 字节）
 * @param  method      签名方法
 * @param  exp_time    过期时间戳
 * @param  product_id  产品 ID
 * @param  dev_name    设备名称（可为 NULL）
 * @param  access_key  产品 access_key
 * @return 0 成功，生成失败
 */
int32_t onenet_token_dev_generate(char *token, enum sig_method_e method,
                                   uint32_t exp_time,
                                   const char *product_id,
                                   const char *dev_name,
                                   const char *access_key);

/**
 * @brief  生成用户级 Token（HTTP OpenAPI 鉴权用）
 * @param  method      签名方法
 * @param  exp_time    过期时间戳
 * @param  user_id     平台用户 ID
 * @param  access_key  用户 access_key
* @return 0 成功，负值失败
 */
int32_t onenet_token_user_generate(char *token, enum sig_method_e method,
                                    uint32_t exp_time,
                                    const char *user_id,
                                    const char *access_key);

/* ================================================================
 *  2.MQTT 连接管理
 * ================================================================ */

/** 启动 MQTT 连接（生成 Token → 初始化客户端 → 连接 OneNET） */
esp_err_t onenet_mqtt_start(void);

/** 订阅属性上报 / 设置 / 获取 / OTA 主题 */
void onenet_mqtt_subscribe(void);

/** 上报属性数据到云平台 */
esp_err_t onenet_mqtt_post_property(const char *data);

/** 发布属性获取应答 */
esp_err_t onenet_mqtt_get_property_reply(const char *data);


/* ================================================================
 *  3.物模型数据处理
 * ================================================================ */

 /** 初始化物模型（初始化 WS2812 + LEDC PWM） */
void onenet_dm_init(void);

/** 处理云平台下发的属性设置 */
void onenet_dm_property_handle(cJSON *property_js);

/** 生成所有属性的上报 JSON */
cJSON *onenet_dm_upload_all(void);

/** 生成属性获取应答 JSON */
cJSON *onenet_dm_get_reply(const char *id);

/* ================================================================
 *  4.OTA 远程升级
 * ================================================================ */

/** 获取当前 APP 版本号 */
const char *onenet_ota_get_version(void);

/** 标记当前固件为合法 / 非法（非法则回滚重启） */
void onenet_ota_set_valid(int valid);

/** 上报当前版本号到 OneNET */
esp_err_t onenet_ota_upload_version(void);

/** 启动 OTA 升级流程（阻塞任务，内部创建 FreeRTOS Task） */
void onenet_ota_start(void);

#define ONENET_OTA_URL  "http://iot-api.heclouds.com/fuse-ota"
#define OTA_BUFF_LEN    1024


#endif
