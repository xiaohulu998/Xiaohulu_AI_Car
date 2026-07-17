#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ap_wifi.h"


#define TAG     "main"


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
