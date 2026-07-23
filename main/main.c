/*注释掉，后期优化拼接
//AP智能配网相关
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ap_wifi.h"


//wifi状态通知回调函数
void wifi_state_handle(WIFI_STATE state)
{
    if(state == WIFI_STATE_CONNECTED)    //wifi连接成功
    {
        ESP_LOGI(TAG,"Wifi connected");
    }
    else if(state == WIFI_STATE_DISCONNECTED)    //wifi连接失败/断开
    {
        ESP_LOGI(TAG,"Wifi disconnected");
    }
}


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 初始化 WiFi 协议栈 / 事件 / STA 框架
    ap_wifi_init(wifi_state_handle);

    // 当前还没有保存的账号密码时，默认进入 AP 配网：
    // 热点: ESP32S3_AP / qwer1234
    // 网页: http://192.168.100.1
    // WebSocket JSON 协议见 ap_wifi.c / 参考.json
    ESP_LOGI(TAG, "进入 AP 配网模式");
    ap_wifi_apcfg();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
*/



#define TAG     "main"


//mqtt 相关
#include "onenet_mqtt.h"
#include "wifi_manager.h"
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"


//事件标志组句柄
static EventGroupHandle_t wifi_ev = NULL;

#define WIFI_CONECT_BIT (BIT0)

//wifi状态通知回调函数
void wifi_state_callback(WIFI_STATE state)
{
    if(state == WIFI_STATE_CONNECTED)    //wifi连接成功
    {
        xEventGroupSetBits(wifi_ev, WIFI_CONECT_BIT);

    }
}


void app_main(void)
{
     //nvs初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
    wifi_ev = xEventGroupCreate();
    wifi_manager_init(wifi_state_callback);
    wifi_manager_connect("xiaomi17", "qwer1234");

    //onenet物模型数据初始化
    onenet_dm_init();

    /* 等 WiFi 连上后只启动一次 MQTT，避免重复 init */
    EventBits_t ev = xEventGroupWaitBits(wifi_ev, WIFI_CONECT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (ev & WIFI_CONECT_BIT) {
        esp_err_t err = onenet_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "onenet_start failed: %s", esp_err_to_name(err));
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

