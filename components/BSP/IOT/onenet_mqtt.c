#include "onenet_mqtt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_client.h"
#include "onenet_token.h"

#define TAG "ONENET_MQTT"

static esp_mqtt_client_handle_t mqtt_handle = NULL;

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
        /* OneNET 属性上报主题示例，后续按物模型改 params */
        {
            char topic[128];
            snprintf(topic, sizeof(topic),
                     "$sys/%s/%s/thing/property/post",
                     ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
            msg_id = esp_mqtt_client_publish(client, topic,
                "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{}}", 0, 0, 0);
            ESP_LOGI(TAG, "property post msg_id=%d topic=%s", msg_id, topic);
        }
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
