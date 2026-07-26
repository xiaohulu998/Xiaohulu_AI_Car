#ifndef __ONENET_DM_H
#define  __ONENET_DM_H

#include "cJSON.h"

/**
 * 物模型数据初始化
 * @param 无
 * @return 无
 */
void onenet_dm_init(void);

/**
 * 处理onenet下行的数据
 * @param property_js 包含下行数据的json
 * @return 无
 */
void onenet_property_handle(cJSON* property_js);

/**
 * 生成上报所有数据的cJSON对象
 * @param 无
 * @return cJSON对象，包含所有属性值
 */
cJSON* onenet_property_upload_dm(void);

/**
 * 生成属性获取应答 get_reply 的 cJSON
 * @param id 平台 get 请求中的 id，原样带回；NULL 时使用 "0"
 * @return cJSON对象，调用方负责 cJSON_Delete
 */
cJSON *onenet_property_get_reply_dm(const char *id);

#endif
