#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include "ap_Wifi.h"


#define TAG     "main"


//wifi状态通知回调函数
void wifi_state_handle(WIFI_STATE state)
{
    if(state == WIFI_STATE_CONNECTED)    //wifi连接成功
    {
        ESP_LOGI(TAG,"Wifi connected");
    }
    else if(state == WIFI_STATE_CONNECTED)    //wifi连接失败
    {
        ESP_LOGI(TAG,"Wifi disconnected");
    }
}

void app_main(void)
{
  nvs_flash_init();
  ap_wifi_init(wifi_state_handle);
  while(1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
