/**
* OneNET 云平台 MQTT 客户端 & 物模型 & 设备用户 Token 生成 & OTA 接口
*/

#include "mqtt_onenet.h"
#include "board_def.h"
#include "bsp_ws2812.h"
#include "mbedtls/md5.h"
#include "mbedtls/md.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include "driver/ledc.h"
#include "cJSON.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ================================================================
 *  ──── 通用日志 ────
 * ================================================================ */
static const char *TAG_MQTT = "onenet_mqtt";
static const char *TAG_OTA  = "onenet_ota";


/* ================================================================
 *  1.Token 签名方法
 * ================================================================ */

enum {
    BAD         = 0xFF,  /* invalid encoding */
    PAD         = '=',
    PEM_LINE_SZ = 64
};


static
const byte base64Encode[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
                              'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
                              'U', 'V', 'W', 'X', 'Y', 'Z',
                              'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
                              'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
                              'u', 'v', 'w', 'x', 'y', 'z',
                              '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                              '+', '/'
                            };

/* make sure *i (idx) won't exceed max, store and possibly escape to out,
 * raw means use e w/o decode,  0 on success */
static int CEscape(int escaped, byte e, byte* out, word32* i, word32 max,
                  int raw, int getSzOnly)
{
    int    doEscape = 0;
    word32 needed = 1;
    word32 idx = *i;

    byte basic;
    byte plus    = 0;
    byte equals  = 0;
    byte newline = 0;

    if (raw)
        basic = e;
    else
        basic = base64Encode[e];

    /* check whether to escape. Only escape for EncodeEsc */
    if (escaped == WC_ESC_NL_ENC) {
        switch ((char)basic) {
            case '+' :
                plus     = 1;
                doEscape = 1;
                needed  += 2;
                break;
            case '=' :
                equals   = 1;
                doEscape = 1;
                needed  += 2;
                break;
            case '\n' :
                newline  = 1;
                doEscape = 1;
                needed  += 2;
                break;
            default:
                /* do nothing */
                break;
        }
    }

    /* check size */
    if ( (idx+needed) > max && !getSzOnly) {
        return -132;
    }

    /* store it */
    if (doEscape == 0) {
        if(getSzOnly)
            idx++;
        else
            out[idx++] = basic;
    }
    else {
        if(getSzOnly)
            idx+=3;
        else {
            out[idx++] = '%';  /* start escape */

            if (plus) {
                out[idx++] = '2';
                out[idx++] = 'B';
            }
            else if (equals) {
                out[idx++] = '3';
                out[idx++] = 'D';
            }
            else if (newline) {
                out[idx++] = '0';
                out[idx++] = 'A';
            }
        }
    }
    *i = idx;

    return 0;
}

/* internal worker, handles both escaped and normal line endings.
   If out buffer is NULL, will return sz needed in outLen */
static int DoBase64_Encode(const byte* in, word32 inLen, byte* out,
                           word32* outLen, int escaped)
{
    int    ret = 0;
    word32 i = 0,
           j = 0,
           n = 0;   /* new line counter */

    int    getSzOnly = (out == NULL);

    word32 outSz = (inLen + 3 - 1) / 3 * 4;
    word32 addSz = (outSz + PEM_LINE_SZ - 1) / PEM_LINE_SZ;  /* new lines */

    if (escaped == WC_ESC_NL_ENC)
        addSz *= 3;   /* instead of just \n, we're doing %0A triplet */
    else if (escaped == WC_NO_NL_ENC)
        addSz = 0;    /* encode without \n */

    outSz += addSz;

    /* if escaped we can't predetermine size for one pass encoding, but
     * make sure we have enough if no escapes are in input
     * Also need to ensure outLen valid before dereference */
    if (!outLen || (outSz > *outLen && !getSzOnly)) return -2;

    while (inLen > 2) {
        byte b1 = in[j++];
        byte b2 = in[j++];
        byte b3 = in[j++];

        /* encoded idx */
        byte e1 = b1 >> 2;
        byte e2 = (byte)(((b1 & 0x3) << 4) | (b2 >> 4));
        byte e3 = (byte)(((b2 & 0xF) << 2) | (b3 >> 6));
        byte e4 = b3 & 0x3F;

        /* store */
        ret = CEscape(escaped, e1, out, &i, *outLen, 0, getSzOnly);
        if (ret != 0) break;
        ret = CEscape(escaped, e2, out, &i, *outLen, 0, getSzOnly);
        if (ret != 0) break;
        ret = CEscape(escaped, e3, out, &i, *outLen, 0, getSzOnly);
        if (ret != 0) break;
        ret = CEscape(escaped, e4, out, &i, *outLen, 0, getSzOnly);
        if (ret != 0) break;

        inLen -= 3;

        /* Insert newline after PEM_LINE_SZ, unless no \n requested */
        if (escaped != WC_NO_NL_ENC && (++n % (PEM_LINE_SZ/4)) == 0 && inLen){
            ret = CEscape(escaped, '\n', out, &i, *outLen, 1, getSzOnly);
            if (ret != 0) break;
        }
    }

    /* last integral */
    if (inLen && ret == 0) {
        int twoBytes = (inLen == 2);

        byte b1 = in[j++];
        byte b2 = (twoBytes) ? in[j++] : 0;

        byte e1 = b1 >> 2;
        byte e2 = (byte)(((b1 & 0x3) << 4) | (b2 >> 4));
        byte e3 = (byte)((b2 & 0xF) << 2);

        ret = CEscape(escaped, e1, out, &i, *outLen, 0, getSzOnly);
        if (ret == 0)
            ret = CEscape(escaped, e2, out, &i, *outLen, 0, getSzOnly);
        if (ret == 0) {
            /* third */
            if (twoBytes)
                ret = CEscape(escaped, e3, out, &i, *outLen, 0, getSzOnly);
            else
                ret = CEscape(escaped, '=', out, &i, *outLen, 1, getSzOnly);
        }
        /* fourth always pad */
        if (ret == 0)
            ret = CEscape(escaped, '=', out, &i, *outLen, 1, getSzOnly);
    }

    if (ret == 0 && escaped != WC_NO_NL_ENC)
        ret = CEscape(escaped, '\n', out, &i, *outLen, 1, getSzOnly);

    if (i != outSz && escaped != 1 && ret == 0)
        return -154;

    *outLen = i;
    if(ret == 0)
        return getSzOnly ? -202 : 0;
    return ret;
}

/* Base64 Encode, PEM style, with \n line endings */
int Base64_Encode(const byte* in, word32 inLen, byte* out, word32* outLen)
{
    return DoBase64_Encode(in, inLen, out, outLen, WC_STD_ENC);
}


/* Base64 Encode, with %0A escaped line endings instead of \n */
int Base64_EncodeEsc(const byte* in, word32 inLen, byte* out, word32* outLen)
{
    return DoBase64_Encode(in, inLen, out, outLen, WC_ESC_NL_ENC);
}

int Base64_Encode_NoNl(const byte* in, word32 inLen, byte* out, word32* outLen)
{
    return DoBase64_Encode(in, inLen, out, outLen, WC_NO_NL_ENC);
}


static
const byte base64Decode[] = { 62, BAD, BAD, BAD, 63,   /* + starts at 0x2B */
                              52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
                              BAD, BAD, BAD, BAD, BAD, BAD, BAD,
                              0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                              10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                              20, 21, 22, 23, 24, 25,
                              BAD, BAD, BAD, BAD, BAD, BAD,
                              26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
                              36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
                              46, 47, 48, 49, 50, 51
                            };


int Base64_Decode(const byte* in, word32 inLen, byte* out, word32* outLen)
{
    word32 i = 0;
    word32 j = 0;
    word32 plainSz = inLen - ((inLen + (PEM_LINE_SZ - 1)) / PEM_LINE_SZ );
    const byte maxIdx = (byte)sizeof(base64Decode) + 0x2B - 1;

    plainSz = (plainSz * 3 + 3) / 4;
    if (plainSz > *outLen) return -173;

    while (inLen > 3) {
        byte b1, b2, b3;
        byte e1 = in[j++];
        byte e2 = in[j++];
        byte e3 = in[j++];
        byte e4 = in[j++];

        int pad3 = 0;
        int pad4 = 0;

        if (e1 == 0)            /* end file 0's */
            break;
        if (e3 == PAD)
            pad3 = 1;
        if (e4 == PAD)
            pad4 = 1;

        if (e1 < 0x2B || e2 < 0x2B || e3 < 0x2B || e4 < 0x2B) {
            return -154;
        }

        if (e1 > maxIdx || e2 > maxIdx || e3 > maxIdx || e4 > maxIdx) {
            return -154;
        }

        e1 = base64Decode[e1 - 0x2B];
        e2 = base64Decode[e2 - 0x2B];
        e3 = (e3 == PAD) ? 0 : base64Decode[e3 - 0x2B];
        e4 = (e4 == PAD) ? 0 : base64Decode[e4 - 0x2B];

        b1 = (byte)((e1 << 2) | (e2 >> 4));
        b2 = (byte)(((e2 & 0xF) << 4) | (e3 >> 2));
        b3 = (byte)(((e3 & 0x3) << 6) | e4);

        out[i++] = b1;
        if (!pad3)
            out[i++] = b2;
        if (!pad4)
            out[i++] = b3;
        else
            break;

        inLen -= 4;
        if (inLen && (in[j] == ' ' || in[j] == '\r' || in[j] == '\n')) {
            byte endLine = in[j++];
            inLen--;
            while (inLen && endLine == ' ') {   /* allow trailing whitespace */
                endLine = in[j++];
                inLen--;
            }
            if (endLine == '\r') {
                if (inLen) {
                    endLine = in[j++];
                    inLen--;
                }
            }
            if (endLine != '\n') {
                return -154;
            }
        }
    }
    *outLen = i;

    return 0;
}

/**
 * 计算hmd
 * @param key 秘钥
 * @param content 内容
 * @param output 输出md5值
 * @return 无
 */
static void calc_hmd(enum sig_method_e method,unsigned char* key,size_t key_len,unsigned char *content,size_t content_len,unsigned char *output)
{
    mbedtls_md_context_t md_ctx;
    const mbedtls_md_info_t *md_info = NULL;
    if (SIG_METHOD_MD5 == method) {
        md_info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    } else if (SIG_METHOD_SHA1 == method) {
        md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    } else if (SIG_METHOD_SHA256 == method) {
        md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    }

    mbedtls_md_init(&md_ctx);
    mbedtls_md_setup(&md_ctx, md_info, 1);
    mbedtls_md_hmac_starts(&md_ctx,key,key_len);
    mbedtls_md_hmac_update(&md_ctx,content,content_len);
    mbedtls_md_hmac_finish(&md_ctx,output);
    mbedtls_md_free(&md_ctx);
}


#define DEV_TOKEN_LEN 256
#define DEV_TOKEN_VERISON_STR "2018-10-31"
#define USER_TOKEN_VERSION_STR "2022-05-01"

#define DEV_TOKEN_SIG_METHOD_MD5 "md5"
#define DEV_TOKEN_SIG_METHOD_SHA1 "sha1"
#define DEV_TOKEN_SIG_METHOD_SHA256 "sha256"

/* ================================================================  */

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
                                   const char *access_key)
{
    uint8_t  base64_data[128] = { 0 };
    uint8_t  str_for_sig[256] = { 0 };
    uint8_t  sign_buf[64]     = { 0 };
    unsigned int base64_data_len = sizeof(base64_data);
    const char* sig_method_str  = NULL;
    unsigned int sign_len        = 0;
    uint32_t i               = 0;
    char* tmp             = NULL;
    int ret;

    if (!token || !product_id || !access_key) {
        return -1;
    }

    sprintf(token, "version=%s", DEV_TOKEN_VERISON_STR);

    if (dev_name && dev_name[0] != '\0') {
        sprintf(token + strlen(token), "&res=products%%2F%s%%2Fdevices%%2F%s", product_id, dev_name);
    } else {
        sprintf(token + strlen(token), "&res=products%%2F%s", product_id);
    }

    sprintf(token + strlen(token), "&et=%u", (unsigned)exp_time);

    ret = Base64_Decode((const byte*)access_key, strlen(access_key), base64_data, &base64_data_len);
    if (ret != 0 || base64_data_len == 0) {
        return -2;
    }

    if (SIG_METHOD_MD5 == method) {
        sig_method_str = DEV_TOKEN_SIG_METHOD_MD5;
        sign_len       = 16;
    } else if (SIG_METHOD_SHA1 == method) {
        sig_method_str = DEV_TOKEN_SIG_METHOD_SHA1;
        sign_len       = 20;
    } else if (SIG_METHOD_SHA256 == method) {
        sig_method_str = DEV_TOKEN_SIG_METHOD_SHA256;
        sign_len       = 32;
    } else {
        return -3;
    }

    sprintf(token + strlen(token), "&method=%s", sig_method_str);
    if (dev_name && dev_name[0] != '\0') {
        sprintf((char*)str_for_sig, "%u\n%s\nproducts/%s/devices/%s\n%s",
                (unsigned)exp_time, sig_method_str, product_id, dev_name, DEV_TOKEN_VERISON_STR);
    } else {
        sprintf((char*)str_for_sig, "%u\n%s\nproducts/%s\n%s",
                (unsigned)exp_time, sig_method_str, product_id, DEV_TOKEN_VERISON_STR);
    }

    calc_hmd(method, base64_data, base64_data_len, str_for_sig, strlen((char*)str_for_sig), sign_buf);

    memset(base64_data, 0, sizeof(base64_data));
    base64_data_len = sizeof(base64_data);
    ret = Base64_Encode_NoNl(sign_buf, sign_len, base64_data, &base64_data_len);
    if (ret != 0) {
        return -4;
    }

    strcat(token, "&sign=");
    tmp = token + strlen(token);

    for (i = 0; i < base64_data_len; i++) {
        switch (base64_data[i]) {
            case '+':
                strcat(tmp, "%2B");
                tmp += 3;
                break;
            case ' ':
                strcat(tmp, "%20");
                tmp += 3;
                break;
            case '/':
                strcat(tmp, "%2F");
                tmp += 3;
                break;
            case '?':
                strcat(tmp, "%3F");
                tmp += 3;
                break;
            case '%':
                strcat(tmp, "%25");
                tmp += 3;
                break;
            case '#':
                strcat(tmp, "%23");
                tmp += 3;
                break;
            case '&':
                strcat(tmp, "%26");
                tmp += 3;
                break;
            case '=':
                strcat(tmp, "%3D");
                tmp += 3;
                break;
            default:
                *tmp = base64_data[i];
                tmp += 1;
                break;
        }
    }
    *tmp = '\0';

    return 0;
}
                               
/* ================================================================  */

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
                                    const char *access_key)
{
    uint8_t  base64_data[128] = { 0 };
    uint8_t  str_for_sig[256] = { 0 };
    uint8_t  sign_buf[64]     = { 0 };
    unsigned int base64_data_len = sizeof(base64_data);
    const char* sig_method_str  = NULL;
    unsigned int sign_len        = 0;
    uint32_t i               = 0;
    char* tmp             = NULL;
    int ret;

    if (!token || !user_id || !access_key) {
        return -1;
    }

    sprintf(token, "version=%s", USER_TOKEN_VERSION_STR);
    sprintf(token + strlen(token), "&res=userid%%2F%s", user_id);
    sprintf(token + strlen(token), "&et=%u", (unsigned)exp_time);

    ret = Base64_Decode((const byte*)access_key, strlen(access_key), base64_data, &base64_data_len);
    if (ret != 0 || base64_data_len == 0) {
        return -2;
    }

    if (SIG_METHOD_MD5 == method) {
        sig_method_str = DEV_TOKEN_SIG_METHOD_MD5;
        sign_len       = 16;
    } else if (SIG_METHOD_SHA1 == method) {
        sig_method_str = DEV_TOKEN_SIG_METHOD_SHA1;
        sign_len       = 20;
    } else if (SIG_METHOD_SHA256 == method) {
        sig_method_str = DEV_TOKEN_SIG_METHOD_SHA256;
        sign_len       = 32;
    } else {
        return -3;
    }

    sprintf(token + strlen(token), "&method=%s", sig_method_str);
    sprintf((char*)str_for_sig, "%u\n%s\nuserid/%s\n%s",
            (unsigned)exp_time, sig_method_str, user_id, USER_TOKEN_VERSION_STR);

    calc_hmd(method, base64_data, base64_data_len, str_for_sig, strlen((char*)str_for_sig), sign_buf);

    memset(base64_data, 0, sizeof(base64_data));
    base64_data_len = sizeof(base64_data);
    ret = Base64_Encode_NoNl(sign_buf, sign_len, base64_data, &base64_data_len);
    if (ret != 0) {
        return -4;
    }

    strcat(token, "&sign=");
    tmp = token + strlen(token);

    for (i = 0; i < base64_data_len; i++) {
        switch (base64_data[i]) {
            case '+':
                strcat(tmp, "%2B");
                tmp += 3;
                break;
            case ' ':
                strcat(tmp, "%20");
                tmp += 3;
                break;
            case '/':
                strcat(tmp, "%2F");
                tmp += 3;
                break;
            case '?':
                strcat(tmp, "%3F");
                tmp += 3;
                break;
            case '%':
                strcat(tmp, "%25");
                tmp += 3;
                break;
            case '#':
                strcat(tmp, "%23");
                tmp += 3;
                break;
            case '&':
                strcat(tmp, "%26");
                tmp += 3;
                break;
            case '=':
                strcat(tmp, "%3D");
                tmp += 3;
                break;
            default:
                *tmp = base64_data[i];
                tmp += 1;
                break;
        }
    }
    *tmp = '\0';

    return 0;
}

/* ================================================================
 *  2.MQTT 连接管理
 * ================================================================ */

 static esp_mqtt_client_handle_t mqtt_handle = NULL;

 /* 前置声明 */
static void onenet_property_ack(const char *id, int code, const char *message);
static void onenet_ota_ack(const char *id, int code, const char *message);
static void onenet_handle_property_get(const char *payload, int payload_len);

/**
 * mqtt连接事件处理函数
 * @param event 事件参数
 * @return 无
 */
 
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:   //握手成功
        ESP_LOGI(TAG_MQTT, "MQTT 已连接");
        onenet_mqtt_subscribe();  //订阅数据
        
        /* 连接成功后上报当前状态 */
        //为了数据同步，上报数据, 主动把当前最新状态上报给云端
        cJSON* property_js = onenet_dm_upload_all();  //jeson树
        char* data = cJSON_PrintUnformatted(property_js); //将cJSON 节点树转换成一段连续的字符串
        onenet_mqtt_post_property(data); //上报数据给云端

        //上报一下设备的版本号
        onenet_ota_upload_version();

        // 设置当前app程序为合法
        onenet_ota_set_valid(1);

        //释放
        cJSON_free(data);
        cJSON_Delete(property_js);
        break;

    case MQTT_EVENT_DISCONNECTED: //连接断开
        ESP_LOGI(TAG_MQTT, "MQTT 已断开");
        break;

    case MQTT_EVENT_SUBSCRIBED: //请求得到服务器确认
        ESP_LOGI(TAG_MQTT, "MQTT 订阅确认, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED: //取消订阅确认
        ESP_LOGI(TAG_MQTT, "MQTT 取消订阅, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED: //发布的消息收到 PUBAC 日志查看消息是否送达平台
        ESP_LOGI(TAG_MQTT, "MQTT 发布确认, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA: //收到云端下发消息
        ESP_LOGI(TAG_MQTT, "MQTT 收到下发数据");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        {
            /* event->topic / event->data 不一定以 '\0' 结尾，先拷贝再匹配 */
            char topic[160] = {0};
            int tlen = event->topic_len;
            if (tlen >= (int)sizeof(topic)) {
                tlen = (int)sizeof(topic) - 1;
            }
            if (event->topic && tlen > 0) {
                memcpy(topic, event->topic, tlen);
            }
             
            // 收到云端下发消息 进行处理并返回属性设置确认
            if (strstr(topic, "property/set") && !strstr(topic, "set_reply"))   //strstr是模糊匹配，容易误触发，增加&& 
             {
                cJSON *property_js = cJSON_ParseWithLength(event->data, event->data_len);
                if (property_js) 
                {
                    onenet_dm_property_handle(property_js);
                    cJSON *id_js = cJSON_GetObjectItem(property_js, "id");
                    const char *id = cJSON_GetStringValue(id_js);
                    onenet_property_ack(id ? id : "0", 200, "success");
                    cJSON_Delete(property_js);
                } 
                else 
                {
                    ESP_LOGE(TAG_MQTT, "属性设置 JSON 解析失败");
                }
            } 
            
            /* 属性获取 */
            // 响应property/get 设备属性获取，上报数据
            else if (strstr(topic, "property/get") && !strstr(topic, "get_reply")
                       && !strstr(topic, "post/reply")) 
            {
                onenet_handle_property_get(event->data, event->data_len);
            }
            
            /* OTA 升级通知 */
            //OTA远程升级,返回属性设置确认
            else if (strstr(topic, "ota/inform"))
            {
                cJSON *ota_js = cJSON_ParseWithLength(event->data, event->data_len);
                if (ota_js) 
                {
                    // 提取 ID
                    cJSON *id_js = cJSON_GetObjectItem(ota_js, "id");
                    const char *id = cJSON_GetStringValue(id_js);

                    // 响应回复
                    onenet_ota_ack(id ? id : "0", 200, "success");
                    cJSON_Delete(ota_js);

                    // 开始ota升级流程
                    onenet_ota_start();

                } 
                else 
                {
                    ESP_LOGE(TAG_MQTT, "OTA 通知 JSON 解析失败");
                }
            }
        }
        break;

    case MQTT_EVENT_ERROR:  // 各类异常：tls 失败、连接超时、内存、协议错误
        ESP_LOGE(TAG_MQTT, "MQTT 异常");
        if (event->error_handle) {
            ESP_LOGE(TAG_MQTT, "  错误类型=%d", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG_MQTT, "  esp_tls_last_esp_err=0x%x",
                         event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG_MQTT, "  esp_tls_stack_err=0x%x",
                         event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG_MQTT, "  socket 错误码=%d",
                         event->error_handle->esp_transport_sock_errno);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                /* CONNACK return code:
                 * 1=协议不支持 2=client_id非法 3=服务不可用
                 * 4=用户名/密码错误 5=未授权
                 */
                ESP_LOGE(TAG_MQTT, "  连接返回码=%d (4=用户名/密码错误)",
                         event->error_handle->connect_return_code);
            }
        }
        break;

    default:
        ESP_LOGI(TAG_MQTT, "其他事件 id:%d", (int)event->event_id);
        break;
    }
}

/**
 * 启动mqtt连接
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_config;
    int32_t token_ret;

    if (mqtt_handle != NULL) {
        ESP_LOGW(TAG_MQTT, "MQTT 已启动，跳过重复初始化");
        return ESP_OK;
    }
    memset(&mqtt_config, 0, sizeof(mqtt_config));

    /* OneNET Studio 非加密 MQTT
     * host: mqtts.heclouds.com  port: 1883
     * 加密通道: mqttstls.heclouds.com:8883
     */
    mqtt_config.broker.address.uri = "mqtt://mqtts.heclouds.com";
    mqtt_config.broker.address.port = 1883;

    /* 鉴权三元组（OneNET 官方约定）
     *   client_id = 设备名称 DeviceName
     *   username  = 产品ID   ProductId
     *   password  = Token（设备级 res）
     */
    mqtt_config.credentials.client_id = ONENET_DEVICE_NAME;
    mqtt_config.credentials.username  = ONENET_PRODUCT_ID;

    /* token 必须 static/全局：esp_mqtt_client_init 只保存指针，不拷贝内容 */
    static char token[256];
    token_ret = onenet_token_dev_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME,
                                          ONENET_PRODUCT_ID, ONENET_DEVICE_NAME,
                                          ONENET_PRODUCT_ACCESS_KE);
    if (token_ret != 0) 
    {
        ESP_LOGE(TAG_MQTT, "设备 Token 生成失败: %d", (int)token_ret);
        return ESP_FAIL;
    }

    mqtt_config.credentials.authentication.password = token;

    //将鉴权信息打印出来
    ESP_LOGI(TAG_MQTT, "OneNET 鉴权参数: clientId=%s, username=%s, password=%s",
             mqtt_config.credentials.client_id,
             mqtt_config.credentials.username,
             mqtt_config.credentials.authentication.password);
    
    //设置mqtt的配置，返回一个mqtt句柄，此句柄后续用来发送数据、注册事件、断开连接使用    
    mqtt_handle = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_handle == NULL) {
        ESP_LOGE(TAG_MQTT, "MQTT 客户端初始化失败");
        return ESP_FAIL;
    }

    //注册回调函数
    esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    //启动mqtt连接，注意此函数会创建一个mqtt任务，并不会启动mqtt连接
    return esp_mqtt_client_start(mqtt_handle);
}

/**
 * 订阅 属性上报 / 设置 / 获取 / OTA 主题
 * @param 无
 * @return 错误
 */
void onenet_mqtt_subscribe(void)
{
    //“平台！以后往【这个主题】发消息的时候，请把消息转发给我。”
       char topic[128];

    //订阅上报属性回复主题,必须订阅这个主题，平台才会把处理结果推送给你
    //平台处理完成后，会往这个主题下发应答回执（成功 / 失败）
    /* 属性上报回复 */
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post/reply",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle,topic,1);  //订阅主题
    
    //订阅下行 “设置属性” 主题
    /* 属性设置（下行） */
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle,topic,1);  //订阅主题

    // 订阅下行 “获取属性” 主题
    /* 属性获取（下行） */
    snprintf(topic, sizeof(topic),"$sys/%s/%s/thing/property/get",
        ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);

    // 订阅下行 “OTA远程升级通知” 主题$sys/{pid}/{device-name}/ota/inform
    /* OTA 升级通知 */
    snprintf(topic, sizeof(topic),"$sys/%s/%s/ota/inform",
    ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);
}

/**
 * 上报属性数据到云平台 
 * @param data 数据
 * @return 错误
 */
esp_err_t onenet_mqtt_post_property(const char* data)
{
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    ESP_LOGI(TAG_MQTT,"属性上报: 主题=%s, 数据=%s",topic,data);
    return esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0);
}

/**
 * 平台主动获取属性 设备应答上报数据
 * @param data 已序列化的 get_reply JSON
 * @return 错误码
 */
esp_err_t onenet_mqtt_get_property_reply(const char *data)
{
    char topic[128];
    if (data == NULL || mqtt_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/get_reply",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    ESP_LOGI(TAG_MQTT, "属性获取应答: 主题=%s, 数据=%s", topic, data);
    return esp_mqtt_client_publish(mqtt_handle, topic, data, strlen(data), 1, 0);
}





/* ================================================================ */
/* ── 内部函数 ── */

/**
 * onenet下发数据，返回属性设置确认
 * @param code 错误码
 * @param message 信息
 * @return 无
 */
static void onenet_property_ack(const char* id,int code,const char* message)
{
   /* 参考json
   {
    "id":"123",
    "code":200,
    "msg":"xxxx"
    }
   */
    char topic[128];   // 存储主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set_reply",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME); //占位符填充

    cJSON *reply_js = cJSON_CreateObject();    //创建根节点
    cJSON_AddStringToObject(reply_js,"id",id); //字符串子节点
    cJSON_AddNumberToObject(reply_js,"code",code); //整型子节点
    cJSON_AddStringToObject(reply_js,"msg",message); //字符串子节点
    char* data = cJSON_PrintUnformatted(reply_js);   //将cJSON 对象（cJSON*）序列化为 JSON 字符串；相反：cJSON_Parse()将JSON 字符串转为cJSON 对象
    // 向MQTT主题发布消息
    // s_onenet_client：MQTT客户端句柄
    // topic：目标发布主题字符串
    // data：要发送的负载
    // strlen(data)：负载字节长度
    // qos = 1：QoS1，至少送达一次
    // retain = 0：不设置保留消息
    esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0); 
    
    cJSON_free(data);  //释放字符串
    cJSON_Delete(reply_js); //释放 cJSON 对象树
}

/**
 * OTA远程升级 返回属性设置确认
 * @param code 错误码
 * @param message 信息
 * @return mqtt连接参数
 */
static void onenet_ota_ack(const char* id,int code,const char* message)
{
   
   // $sys/{pid}/{device-name}/ota/inform_reply
    /* 参考json
   {
    
    "id":"123",
    "code":200,
    "msg":"xxxx"
    "data":
    {
        “Xxxx”
    }
   */
    char topic[128];   // 存储主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/ota/inform_reply",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME); //占位符填充

    cJSON *reply_js = cJSON_CreateObject();    //创建根节点
    cJSON_AddStringToObject(reply_js,"id",id); //字符串子节点
    cJSON_AddNumberToObject(reply_js,"code",code); //整型子节点
    cJSON_AddStringToObject(reply_js,"msg",message); //字符串子节点
    char* data = cJSON_PrintUnformatted(reply_js);   //将cJSON 对象（cJSON*）序列化为 JSON 字符串；相反：cJSON_Parse()将JSON 字符串转为cJSON 对象
    // 向MQTT主题发布消息
    // s_onenet_client：MQTT客户端句柄
    // topic：目标发布主题字符串
    // data：要发送的负载
    // strlen(data)：负载字节长度
    // qos = 1：QoS1，至少送达一次
    // retain = 0：不设置保留消息
    esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0); 
    
    cJSON_free(data);  //释放字符串
    cJSON_Delete(reply_js); //释放 cJSON 对象树
}

/**
 * 平台主动获取属性 设备应答上报数据
 * @param payload 接收到平台的数据
 * @param payload_len 数据长度
 * @return 无
 */

static void onenet_handle_property_get(const char *payload, int payload_len)
{
    cJSON *req_js = cJSON_ParseWithLength(payload, payload_len);   //cJSON\_Parse靠判断字符串的 \0停止符，容易溢出
    if (req_js == NULL) {
        ESP_LOGE(TAG_MQTT, "属性获取请求 JSON 解析失败");
        return;
    }

    // 通过前面的DATA 获取ID
    cJSON *id_js = cJSON_GetObjectItem(req_js, "id");
    const char *id = cJSON_GetStringValue(id_js);

    // 生成属性获取应答 get_reply 的 cJSON
    cJSON *reply_js = onenet_dm_get_reply(id);
    if (reply_js == NULL) 
    {
        ESP_LOGE(TAG_MQTT, "属性获取应答生成失败");
        cJSON_Delete(req_js);
        return;
    }
    if (reply_js) 
    {
        //将cJSON转换成字符串
        char *data = cJSON_PrintUnformatted(reply_js);
        if (data) 
        {
            //组装格式，并发布属性获取应答到 get_reply 主题
            onenet_mqtt_get_property_reply(data);
            cJSON_free(data);
        }
        cJSON_Delete(reply_js);
    }
    cJSON_Delete(req_js);
}

/* ================================================================
 *  3.物模型数据处理
 * ================================================================ */

static ws2812_strip_handle_t ws2812_handle = NULL;
static int led_brightness    = 0;   //亮度
static int led_LightSwitch   = 0;   //开关

static uint8_t ws2812_red    = 0;   
static uint8_t ws2812_green  = 0;
static uint8_t ws2812_blue   = 0;


/**
 * 物模型数据初始化
 * @param 无
 * @return 无
 */   
void onenet_dm_init(void)
{
    /* WS2812 初始化 */
    ws2812_init(GPIO_NUM_38, 3, &ws2812_handle);

    /* LEDC PWM 定时器 */
    ledc_timer_config_t led_timer = {
        .clk_cfg         = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .freq_hz         = 5000,
        .timer_num       = LEDC_TIMER_0,
    };
    ledc_timer_config(&led_timer);

    /* LEDC 通道（板载 LED） */
        ledc_channel_config_t led_channel = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = LEDC_CHANNEL_0,
            .timer_sel  = LEDC_TIMER_0,
            .gpio_num   = BUILTIN_LED_GPIO,
            .duty       = 0,
            .hpoint     = 0,
        };
        ledc_channel_config(&led_channel);
        ledc_fade_func_install(0);
    }

/**
 * 处理onenet下行的数据
 * @param property_js 包含下行数据的json
 * @return 无
 */
void onenet_dm_property_handle(cJSON *property_js)
{
    /*下行JSON列子
  {
    "id": "123",
    "version": "1.0",
    "params": {
          "Brightness":50,
          "LightSwitch":true,
          "RGBColor":{
              "Red":100,
              "Green":100,
              "Blue":100,
          }

    }
  }
  */
  //从property_js 这个JSON根对象中，查找键名为"params"的子节点
  cJSON *params_js = cJSON_GetObjectItem(property_js, "params");
  if (params_js) {
    cJSON *name_js = params_js->child; // 第一个子节点
    while (name_js) {
      if (strcmp(name_js->string, "Brightness") == 0) // 比较键名
      {
        // cJSON_GetNumberValue从一个cJSON的ITEM（键值对)中取出数值类型的值
        led_brightness = cJSON_GetNumberValue(name_js);
        int duty = led_brightness * 4095 / 100; // 计算ccr，占空比值
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, 0);

      } else if (strcmp(name_js->string, "LightSwitch") == 0) // 比较键名
      {
        if (cJSON_IsTrue(name_js)) // 判断开关是否打开
        {
          led_LightSwitch = 1;
          led_brightness = 50;
          int duty = 50 * 4095 / 100;
          ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty,
                                   0);
        } else // 关灯
        {
          ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0, 0);
          led_LightSwitch = 0;
          led_brightness = 0;
        }
      } else if (strcmp(name_js->string, "RGBColor") == 0) // 比较键名
      {
        // 取出键名为Red、green、blue的值
        ws2812_red = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Red"));
        ws2812_green =
            cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Green"));
        ws2812_blue =
            cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Blue"));
        // 写入RBG值，每个灯都一样
        for (int i = 0; i < 3; i++) {
          ws2812_write(ws2812_handle, i, ws2812_red, ws2812_green, ws2812_blue);
        }
      }
      // 循环重要点
      name_js = name_js->next; // 指针移动，指向同级下一个 cJSON 节点
    }
}
}

/**
 * 生成上报所有数据的cJSON对象
 * @param 无
 * @return cJSON对象，包含所有属性值
 */
cJSON *onenet_dm_upload_all(void)
{
/*
//参考JSON
  {
    "id": "123",
    "version": "1.0",
    "params": {
    "Brightness": {
        "value": 50,  //led_brightness
        },
    "LightSwitch": {
        "value": ture, //led_LightSwitch
        }
    "RGBColor":{
        "value":{
            "red":100,
            "green":100,
            "blue":100,
        }
    }
  */
  cJSON *root = cJSON_CreateObject();                        // 根节点
  cJSON_AddStringToObject(root, "id", "123");                // 子节点
  cJSON_AddStringToObject(root, "version", "1.0");           // 子节点
  cJSON *params_js = cJSON_AddObjectToObject(root, "params"); // 子节点  //已修复，这里是Object写成了 Array 

  // 往params中填充灯亮度值
  cJSON *Brightness_js = cJSON_AddObjectToObject(params_js, "Brightness");
  cJSON_AddNumberToObject(Brightness_js, "value", led_brightness);

  // 往params中填充灯开关值
  cJSON *LightSwitch_js = cJSON_AddObjectToObject(params_js, "LightSwitch");
  cJSON_AddBoolToObject(LightSwitch_js, "value", led_LightSwitch);

  // 往params中填充RGB值
  cJSON *RGBColor_js = cJSON_AddObjectToObject(params_js, "RGBColor");

  cJSON *RGBColor_value_js = cJSON_AddObjectToObject(RGBColor_js, "value");
  cJSON_AddNumberToObject(RGBColor_value_js, "Red", ws2812_red);
  cJSON_AddNumberToObject(RGBColor_value_js, "Green", ws2812_green);
  cJSON_AddNumberToObject(RGBColor_value_js, "Blue", ws2812_blue);

  return root;
}


/**
 * 生成属性获取应答 JSON
 * @param id
 * @return cJSON对象，包含所有属性值
 * {"id":"...","code":200,"msg":"success","data":{属性直接值}}
 */

cJSON *onenet_dm_get_reply(const char *id)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", id ? id : "0");
    cJSON_AddNumberToObject(root, "code", 200);
    cJSON_AddStringToObject(root, "msg", "success");

    cJSON *data_js = cJSON_AddObjectToObject(root, "data");
    cJSON_AddNumberToObject(data_js, "Brightness", led_brightness);
    cJSON_AddBoolToObject(data_js, "LightSwitch", led_LightSwitch);

    cJSON *rgb_js = cJSON_AddObjectToObject(data_js, "RGBColor");
    cJSON_AddNumberToObject(rgb_js, "Red",   ws2812_red);
    cJSON_AddNumberToObject(rgb_js, "Green", ws2812_green);
    cJSON_AddNumberToObject(rgb_js, "Blue",  ws2812_blue);
    return root;
}

/* ================================================================
 * 4.OTA 远程升级 
 * ================================================================ */

#define ONENET_OTA_URL  "http://iot-api.heclouds.com/fuse-ota"
#define OTA_BUFF_LEN    1024

static uint8_t ota_data_buff[OTA_BUFF_LEN]; // 接收到的http 数据
static int ota_data_size = 0; // 数据可能过长，一次收不完。已接收到的http数据长度
static char target_version[32] = {0}; // 升级任务的目标版本
static int task_id = 0;               // OTA任务唯一ID
static bool ota_is_running = false;  // OTA状态指示

/* ── HTTP 事件回调 ── */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG_OTA, "HTTP 异常");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG_OTA, "HTTP 已连接");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG_OTA, "HTTP 请求头已发送");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG_OTA, "HTTP 响应头: %s = %s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:   //接收数据事件
            ESP_LOGI(TAG_OTA, "HTTP 接收数据, 长度=%d", evt->data_len);
            printf("HTTP_EVENT_ON_DATA data=%.*s\r\n", evt->data_len,(char*)evt->data);  // %. *S 打印固定长度的字符串，防止内存越界乱读，乱码、程序崩溃。
           
            //防止拷贝溢出
            int copy_len = 0;
            if(evt->data_len > (OTA_BUFF_LEN - ota_data_size))   //如果大于剩余长度
            {
                copy_len = OTA_BUFF_LEN - ota_data_size;   //只拷贝剩余长度
            }
            else 
            {
                copy_len = evt->data_len;   //否则拷贝原始长度
            }
            memcpy(&ota_data_buff[ota_data_size],evt->data,copy_len);  //拷贝内存
            ota_data_size += copy_len;   //更新已接收到的http数据长度
            
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG_OTA, "HTTP 请求完成");
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_OTA, "HTTP 已断开");
            break;

        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG_OTA, "HTTP 重定向");
            break;

        default: 
            break;    
    }
    return ESP_OK;
}

/**
 * 获取当前 APP 版本号
 * @param 无
 * @return 版本号
 */
const char *onenet_ota_get_version(void)
{
    static char app_version[32] = {0};
    if (app_version[0] == 0)   //惰性加载（只初始化一次）
    {
    
    //获取当前运行的app分区信息
    const esp_partition_t* running = esp_ota_get_running_partition();

    //根据app分区信息获取app描述信息
    esp_app_desc_t esp_app_desc;
    esp_ota_get_partition_description(running, &esp_app_desc);
    snprintf(app_version, sizeof(app_version), "%s",esp_app_desc.version);
    // return esp_app_desc.version; //不能直接返回地址，函数退出栈回收，地址会变成野指针，数值无效
    }
    return app_version;    
}

/** 标记当前固件为合法 / 非法（非法则回滚重启） */
void onenet_ota_set_valid(int valid)
{
    //获取当前运行的app分区信息
    const esp_partition_t* running = esp_ota_get_running_partition();  

    //获取当前运行的app状态
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running , &ota_state) == ESP_OK) 
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY )  //上电启动新app分区，会自动把新app分区从new 标记成 ESP_OTA_IMG_PENDING_VERIFY
        {
            if (valid)  //合法
            {
                esp_ota_mark_app_valid_cancel_rollback(); //将新app分区设置为 ESP_OTA_IMG_VALID 合法 
            }
            else 
            {
                esp_ota_mark_app_invalid_rollback_and_reboot(); //设置成非法并重启
            }
        }
    }
  
}

/**
 * 发起http请求
 * @param url 请求地址
 * @param method 请求方法
 * @param payload 消息体内容
 * @return 错误码
 */
 static esp_err_t onenet_ota_http_connect(const char* url, esp_http_client_method_t method, const char* payload )
{
    /*
    #POST是说明设备使用的http协议中需要使用POST方法来提交版本信息
    #POST之后是Http的url地址，地址里面pro_id是产品ID，dev_name是设备名称
    POST http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/version

    #数据类型
    Content-Type: application/json

    #token鉴权
    Authorization:version=2022-05-01&res=userid%2F112&et=1662515432&method=sha1&sign=Pd14JLeTo77e0FOpKN8bR1INPLA%3D

    #主机
    host:iot-api.heclouds.com
    #消息体的长度
    Content-Length:xx

    #消息体的内容
    {"s_version":"V1.3", "f_version": "V2.0"}
    */

    char* token =(char*)malloc(256); //在堆上申请地址
    memset(token,0, 256);  
    /*dev_token_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME, 
                                   ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, 
                                   ONENET_PRODUCT_ACCESS_KE); */
  
    //使用用户鉴权
    onenet_token_user_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME,
                              ONENET_USER_ID, ONENET_USER_ACCESS_KE);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t http_client = esp_http_client_init(&config);

   

    // POST
    //设置发送请求头
    esp_http_client_set_method(http_client, method);   //模式
    esp_http_client_set_header(http_client, "Content-Type", "application/json"); //添加数据类型
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");  //添加主机
    esp_http_client_set_header(http_client,"Authorization",token);   //添加token

    if(payload)
    {
       ESP_LOGI(TAG_OTA,"POST 数据: %s",payload);
       esp_http_client_set_post_field(http_client, payload, strlen(payload)); //添加消息体内容
    }

    //清零
    memset(ota_data_buff, 0, sizeof(ota_data_buff));
    ota_data_size = 0;

    //esp_http_client_perform这个函数会阻塞，直到完整的http请求结束才返回
    esp_err_t err = esp_http_client_perform(http_client);  //发送配置好的请求

    //返回ESP_OK 仅体现TCP 连接正常，400、401、404、500 这类错误码时，依旧返回ESP_OK
    if (err == ESP_OK)    //通信链路正常
    {
        int status = esp_http_client_get_status_code(http_client);     //获取HTTP 响应状态
        int64_t body_len = esp_http_client_get_content_length(http_client); //获取服务器返回响应体（Body）的字节长度
        ESP_LOGI(TAG_OTA,"HTTP 响应: 状态码=%d, 内容长度=%lld", status, body_len);
        if(status == 200)
        {
        // 业务正常，读取响应JSON，解析返回结果
        }
        else
        {
        // 服务器返回错误(401/2402等)，虽然通信通了，但是业务失败
        ESP_LOGE(TAG_OTA,"云端返回错误码 %d", status);
        }
    } 
    else 
    {
        // 网络层面失败，连服务器都没连上
        ESP_LOGE(TAG_OTA, "HTTP 请求失败: %s", esp_err_to_name(err));  //把数字错误码转换成可读字符串。
    }
    
    //清理操作
    esp_http_client_cleanup(http_client);
    free(token);

    return err;
}

/** 上报当前版本号到 OneNET */
esp_err_t onenet_ota_upload_version(void)
{
    esp_err_t ret = ESP_FAIL;
    //生成url
    char url[256];
    snprintf(url, sizeof(url), ONENET_OTA_URL"/%s/%s/version", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME); 
    
    //生成消息体，消息体较简单，用snprintf直接生成
    char version[128];
    const char *app_version = onenet_ota_get_version();  //获取app版本号
    //生成消息体内容（版本号)
    snprintf(version, sizeof(version), "{\"s_version\":\"%s\", \"f_version\": \"%s\"}", app_version, app_version ); //  \转义，代表字符串内部内容，填充字符串的值
    
    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_POST, version))
    {
        cJSON* root = cJSON_Parse((char*)ota_data_buff);
       
        /* 返回的数据格式
        {
        "code": 0,
        "msg": "succ",
        "request_id": "**********"
        }
        */
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
            {
               ret = ESP_OK;

            }
            cJSON_Delete(root);
       }
     }
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG_OTA, "上报版本号失败");
    }
    return ret;
}

/**
 * 查询升级任务状态
 * @param type = 1,说明是完整包，type=2,说明是差分包
 * @param version 当前设备版本
 * @return 错误码
 */
esp_err_t onenet_ota_check_task(const char* type,const char* version)
{
    /*请求头参考
    GET http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/check?type=1&version=1.2
    Content-Type: application/json

    Authorization:version=2022-05-01&res=userid%2F112&et=1662515432&method=sha1&sign=Pd14JLeTo77e0FOpKN8bR1INPLA%3D

    host:iot-api.heclouds.com

    Content-Length:20
    */
    
    esp_err_t ret = ESP_FAIL;
    //生成url
    char url[256];
    //生成URL
    snprintf(url, sizeof(url), ONENET_OTA_URL"/%s/%s/check?type=%s&version=%s", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, type, version);  //%s 字符串首地址，%c单个字符。拷贝值
       
    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_GET, version))
    {
        cJSON* root = cJSON_Parse((char*)ota_data_buff);
       
        /* 返回的数据格式
        {
        "code": 0,
        "msg": "succ",
        "request_id": "**********",
        "data": {
            "target": "1.2", // 升级任务的目标版本
            "tid": 12, //任务ID
            "size": 123, //文件大小
            "md5": "dfkdajkfd", //升级文件的md5
            "status": 1 | 2 | 3, //1 ：待升级， 2 ：下载中， 3 ：升级中
            "type": 1 | 2 // 1:完整包，2：差分包  
	        }
        }
        */
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root, "code");
            cJSON* data_js = cJSON_GetObjectItem(root, "data");
            cJSON* target_js = cJSON_GetObjectItem(data_js, "target");
            cJSON* tid_js = cJSON_GetObjectItem(data_js, "tid");

            if(code_js && cJSON_GetNumberValue(code_js) == 0)  //code为0代表成功
            {
               
               if (target_js) 
               {
                snprintf(target_version, sizeof(target_version), "%s",cJSON_GetStringValue(target_js));  //取出版本号
                task_id = cJSON_GetNumberValue(tid_js);  //取出任务id
                ret = ESP_OK;
               }
            }
            else 
            {
            ESP_LOGE(TAG_OTA, "检测 OTA升级失败......");
            }
            cJSON_Delete(root);
       }
     }
    
    return ret;    
}

/**
 * 上报升级状态/进度
 * @param tid 任务id
 * @param step 进度
 * @return 错误码
 */
esp_err_t onenet_ota_upload_status(int tid, int step)

{
    /*
    POST http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/{tid}/status
    Content-Type: application/json
    Authorization:version=2022-05-01&res=userid%2F112&et=1662515432&method=sha1&sign=Pd14JLeTo77e0FOpKN8bR1INPLA%3D 
    host:iot-api.heclouds.com
    Content-Length:20

    {"step":10} 
    */ 
    esp_err_t ret = ESP_FAIL;
    char url[256];
    char payload[16];
    //生成url
    snprintf(url, sizeof(url), ONENET_OTA_URL"/%s/%s/%d/status", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, tid); 
    
    //生成消息体，消息体较简单，用snprintf直接生成
    snprintf(payload, sizeof(payload), "{\"step\":%d}", step);    //生成消息体内容 升级进度

    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_POST, payload))  //http连接
    {
        cJSON* root = cJSON_Parse((char*)ota_data_buff);
       
        /* 返回的数据格式
        {
            "code": 0,
            "msg": "succ",
            "request_id": "**********"
        }
        */
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
            {
               ret = ESP_OK;
            }
            cJSON_Delete(root);
       }
     }
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG_OTA, "上报升级进度失败");
    }
    return ret;
}

/**
 * 初始化回调函数
 * @param http_client http客户端句柄
 * @return 错误码
 */
esp_err_t onenet_ota_init_cb(esp_http_client_handle_t http_client)
{
   /* 参考请求
    GET 
    http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/{tid}/download

    Authorization:version=2022-05-01&res=userid%2F112&et=1662515432&method=sha1&sign=Pd14JLeTo77e0FOpKN8bR1INPLA%3D

    host:iot-api.heclouds.com  
    */
    static char token[256];
    memset(token,0, 256);
    onenet_token_user_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME,
                               ONENET_USER_ID, ONENET_USER_ACCESS_KE);
    // POST
    //设置发送请求头
    esp_http_client_set_method(http_client, HTTP_METHOD_GET);   //模式
    esp_http_client_set_header(http_client, "Content-Type", "application/json"); //添加数据类型
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");  //添加主机
    esp_http_client_set_header(http_client,"Authorization",token);   //添加token
    return ESP_OK;
}

/**
 * 下载升级包
 * @param tid 任务id
 * @return 错误码
 */
esp_err_t onenet_ota_download(int tid)
{
    char url[256];
    snprintf(url, sizeof(url), ONENET_OTA_URL"/%s/%s/%d/download", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, tid);
    esp_http_client_config_t http_cfg ={
        .url = url,
    }; 
    esp_https_ota_config_t ota_cfg ={
        .http_config = &http_cfg,  //http参数
        .http_client_init_cb = onenet_ota_init_cb,   //初始化回调函数，发起http请求之前调用初始化回调函数，设置请求头
    };
    esp_err_t ota_ret = ESP_FAIL;
    ota_ret = esp_https_ota(&ota_cfg);   //执行ota下载,自动完成新固件下载和烧录
    if(ota_ret == ESP_OK)
    {
        ESP_LOGI(TAG_OTA, "更新成功...");
    }
    else 
    {
        ESP_LOGI(TAG_OTA, "更新失败...,错误码: 0x%x", ota_ret);
    }
    return ota_ret;

}

/**
 * 处理OTA流程
 * @param param 任务函数入参
 * @return 错误码
 */
static void onenet_ota_task(void *param)
{
    esp_err_t ret =ESP_FAIL;

    // 1. 上报当前版本号
    ret = onenet_ota_upload_version();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG_OTA,"上报当前版本号失败!");
        goto delete_ota_task;
    }
    // 2. 检测升级任务
    ret = onenet_ota_check_task("1", onenet_ota_get_version());   //请求头传的是当前版本号，返回值的"target"是目标版本号
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG_OTA,"检测升级任务失败!");
        goto delete_ota_task;
    }
    
    // 3. 上报任务升级状态 10%

    ret = onenet_ota_upload_status(task_id, 10);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG_OTA,"上报任务升级状态失败!");
        goto delete_ota_task;
    }
    // 4. 进行http下载
    ret = onenet_ota_download(task_id);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG_OTA,"OTA http下载APP包失败!");
        goto delete_ota_task;
    }

    // 5. 上报任务升级状态 100%
    ret = onenet_ota_upload_status(task_id, 100);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG_OTA,"上报任务升级状态失败!");
        goto delete_ota_task;
    }

    //重启
    esp_restart();  //重启

    delete_ota_task : 
        ota_is_running = false;
        vTaskDelete(NULL);
}



/**
 * 启动onenet ota升级流程
 * @param 无
 * @return 无
 */
void onenet_ota_start()
{
    if(ota_is_running)
    {
        return;
    }
    ota_is_running = true;
    ESP_LOGI(TAG_OTA,"启动OTA升级");
    //创建任务函数，不需要保存句柄，直接在任务函数删除自身。绑定内核1，防止被其他任务函数挤占，而频繁掉线
    xTaskCreatePinnedToCore(onenet_ota_task, "onenet_ota_task", 4096, NULL, 4, NULL, 1);


}
