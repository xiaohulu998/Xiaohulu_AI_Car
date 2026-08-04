/**
* 全局硬件总初始化入口，应在其他 BSP 模块初始化之前调用，且全局仅调用一次
* 1.NVS Flash；2.TCP/IP 协议栈 (lwIP)；3.默认事件循环
*/

#include "bsp_board.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

static const char *TAG = "bsp_board";

/* 全局变量，确保系统级初始化只执行一次 */
static bool s_bsp_inited = false;


esp_err_t bsp_board_init(void)
{
    if (s_bsp_inited) {
        ESP_LOGW(TAG, "开发板硬件已完成初始化，跳过该步骤...");
        return ESP_OK;
    }

    /* ---- 1. NVS Flash 初始化 ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        // 先擦除后初始化
        ESP_LOGE(TAG, "NVS 分区需要擦除，执行 nvs_flash_erase...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "nvs初始化失败: %s", esp_err_to_name(ret));  // 原始错误码转化成字符串名称 
        return ret;
    }
   
    /* ---- 2. TCP/IP 协议栈初始化 ---- */
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netif初始化失败: %s", esp_err_to_name(ret)); // 原始错误码转化成字符串名称 
        return ret;
    }

    /* ---- 3. 默认事件循环创建 ---- */
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "默认事件循环创建失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_bsp_inited = true;   //置系统级初始化标志位为true
    ESP_LOGI(TAG, "开发板硬件初始化完成。");
    return ESP_OK;

}
    