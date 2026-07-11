#ifndef __WIFI_H
#define __WIFI_H

#include <stdio.h>
#include <string.h>

//自建头文件
#include "wifi_nvs.h"
#include "wifi_mode.h"
#include "web_server_bridge.h"

//官方头文件
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


//启动 WiFi智能配网模式
void wifi_conect_init(void);


#endif
