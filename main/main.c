#include <stdio.h>
#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    printf("===== ESP32 WiFi 智能配网系统启动 =====\n");
    printf("流程: 检查NVS → 有WiFi则STA直连 → 无则AP配网\n");

    // 初始化WiFi模块（自动判断AP配网 / STA直连）
    wifi_conect_init();

    // 主循环由FreeRTOS任务调度接管，此处无需循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
