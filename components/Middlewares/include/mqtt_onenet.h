/**
* OneNET 云平台 MQTT 客户端 & 物模型 & 设备用户 Token 生成 & OTA 接口
*/

#ifndef _MQTT_ONENET_H_
#define _MQTT_ONENET_H_

#include "esp_err.h"
#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>

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
 *  Token 签名方法
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






#endif
