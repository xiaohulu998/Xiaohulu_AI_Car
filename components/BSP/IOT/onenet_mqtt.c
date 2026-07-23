#include "onenet_mqtt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "onenet_dm.h"

#define TAG "ONENET_MQTT"




static esp_mqtt_client_handle_t mqtt_handle = NULL;

//函数前置
static void onenet_property_ack(const char* id,int code,const char* message);
void onenet_subscribe(void);
esp_err_t onenet_post_property_data(const char* data);

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
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        onenet_subscribe();  //订阅数据
        
        //为了数据同步，上报数据, 主动把当前最新状态上报给云端
        cJSON* property_js = onenet_property_upload_dm();  //jeson树
        char* data = cJSON_PrintUnformatted(property_js); //将cJSON 节点树转换成一段连续的字符串
        onenet_post_property_data(data); //上报数据给云端

        //释放
        cJSON_free(data);
        cJSON_Delete(property_js);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        if(strstr(event->topic, "property/set"))  //查找字符串，非0为找到
        {
            //将JSON 字符串，解析成 cJSON 结构体对象，方便代码读取里面的键值
            cJSON *property_js = cJSON_Parse(event_data); 
            
            onenet_property_handle(property_js); //调用函数处理下行数据(物模型数据)
            //cJSON_GetObjectItem 获取ID指针
            cJSON* id_js = cJSON_GetObjectItem(property_js, "id");
            onenet_property_ack(cJSON_GetStringValue(id_js), 200, "success");   //处理后应答
            cJSON_Delete(property_js);   //释放 cJSON树
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle) {
            ESP_LOGE(TAG, "  error_type=%d", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "  esp_tls_last_esp_err=0x%x",
                         event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "  esp_tls_stack_err=0x%x",
                         event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "  socket_errno=%d",
                         event->error_handle->esp_transport_sock_errno);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                /* CONNACK return code:
                 * 1=协议不支持 2=client_id非法 3=服务不可用
                 * 4=用户名/密码错误 5=未授权
                 */
                ESP_LOGE(TAG, "  connect_return_code=%d (4=bad user/pass)",
                         event->error_handle->connect_return_code);
            }
        }
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", (int)event->event_id);
        break;
    }
}

/**
 * 启动mqtt连接
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_start(void)
{
    esp_mqtt_client_config_t mqtt_config;
    int32_t token_ret;

    if (mqtt_handle != NULL) {
        ESP_LOGW(TAG, "MQTT already started");
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
    token_ret = dev_token_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME,
                                   ONENET_PRODUCT_ID, ONENET_DEVICE_NAME,
                                   ONENET_PRODUCT_ACCESS_KE);
    if (token_ret != 0) {
        ESP_LOGE(TAG, "dev_token_generate failed: %d", (int)token_ret);
        return ESP_FAIL;
    }

    mqtt_config.credentials.authentication.password = token;

    //将鉴权信息打印出来
    ESP_LOGI(TAG, "onenet connect->clientId:%s, username:%s, password:%s",
             mqtt_config.credentials.client_id,
             mqtt_config.credentials.username,
             mqtt_config.credentials.authentication.password);
    
    //设置mqtt的配置，返回一个mqtt句柄，此句柄后续用来发送数据、注册事件、断开连接使用    
    mqtt_handle = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_handle == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    //注册回调函数
    esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    //启动mqtt连接，注意此函数会创建一个mqtt任务，并不会启动mqtt连接
    return esp_mqtt_client_start(mqtt_handle);  
}

/**
 * 订阅相关主题，有要订阅的主题可以放在这个函数
 * @param 无
 * @return 错误
 */
void onenet_subscribe(void)
{
    //“平台！以后往【这个主题】发消息的时候，请把消息转发给我。”
       char topic[128];
    //订阅上报属性回复主题,必须订阅这个主题，平台才会把处理结果推送给你
    //平台处理完成后，会往这个主题下发应答回执（成功 / 失败）
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post/reply",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle,topic,1);  //订阅主题
    //订阅下行设置属性主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle,topic,1);  //订阅主题
}

/**
 * 返回属性设置确认
 * @param code 错误码
 * @param message 信息
 * @return mqtt连接参数
 */
void onenet_property_ack(const char* id,int code,const char* message)
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
 * 上报数据
 * @param data 数据
 * @return 错误
 */
esp_err_t onenet_post_property_data(const char* data)
{
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    ESP_LOGI(TAG,"Upload topic:%s,payload:%s",topic,data);
    return esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0);
}