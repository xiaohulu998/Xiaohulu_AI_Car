#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "board_def.h"
//#include "audio.h"
//#include "ble.h"
#include "mqtt_onenet.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG     "main"


//wifi状态通知回调函数
//typedef void (*p_wifi_state_callback)(WIFI_STATE state);
void wifi_state_handle(WIFI_STATE state)
{
    if(state == WIFI_STATE_CONNECTED)    //wifi连接成功
    {
      ESP_LOGI(TAG,"WIFI 连接成功");
      
      onenet_dm_init();  //onenet物模型数据初始化
      onenet_mqtt_start(); //启动maqtt

    }else if(state == WIFI_STATE_DISCONNECTED)    //wifi连接失败/断开
    {
        ESP_LOGI(TAG,"WIFI 连接失败");
    }
}

void app_main(void)
{
    // 初始化 WiFi 并自动判断：NVS 有密码直连，无密码进入 AP 配网
    wifi_apcfg_init(wifi_state_handle);
    while (1)
    {
    vTaskDelay(pdMS_TO_TICKS(1000));
    }

}