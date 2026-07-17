#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include <string.h>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/ip4_addr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"    //二值信号量

#define TAG "wifi_manager"

/*WIFI AP模式 账号及密码区*/
#define WIFI_AP_SSID "ESP32S3_AP"
#define WIFI_AP_PSWD "qwer1234"

// 重连次数
#define MAX_CONNECT_RETRY 6
static int sta_connect_count = 0;

// 回调函数
static p_wifi_state_callback wifi_state_cb = NULL;

// 定义esp_netif_t 结构体指针，代表一个网络接口实例（Wi-Fi AP 专用 netif）
static esp_netif_t *esp_netif_ap = NULL; // 赋值null避免野指针

// 定义二值信号量句柄
static SemaphoreHandle_t  scansem = NULL;

// 当前sta连接状态
static bool is_sta_connected = false;

/** 事件回调函数
 * @param arg   用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return 无
 */
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START: // WIFI以STA模式启动后触发此事件
        {
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if (mode == WIFI_MODE_STA)
                esp_wifi_connect(); // 启动WIFI连接
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: // WIFI连上路由器后，触发此事件
            ESP_LOGI(TAG, "Connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: // WIFI从路由器断开连接后触发此事件
            if (is_sta_connected)
            {
                if (wifi_state_cb)
                    wifi_state_cb(WIFI_STATE_DISCONNECTED);
                is_sta_connected = false;
            }
            if (sta_connect_count < MAX_CONNECT_RETRY) // 如果小于 设置的继续重连次数
            {
                wifi_mode_t mode;
                esp_wifi_get_mode(&mode);
                if (mode == WIFI_MODE_STA)
                    esp_wifi_connect(); // 继续重连
                sta_connect_count++;
            }
            ESP_LOGI(TAG, "connect to the AP fail,retry now"); // 大于 设置的继续重连次数，需重新开始
            break;
        case WIFI_EVENT_AP_STACONNECTED: // AP连接
            ESP_LOGI(TAG, "sta device connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED: // AP断开连接
            ESP_LOGI(TAG, "sta device disconnected");
            break;
        default: // 都不符合情况，跳出
            break;
        }
    }
    if (event_base == IP_EVENT) // IP相关事件
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP: // 只有获取到路由器分配的IP，才认为是连上了路由器
            ESP_LOGI(TAG, "Get ip address");
            is_sta_connected = true;
            if (wifi_state_cb)
                wifi_state_cb(WIFI_STATE_CONNECTED);
            break;
        default:
            break;
        }
    }
}

/** 初始化wifi，默认进入STA模式
 * @param 无
 * @return 无
 */
void wifi_manager_init(p_wifi_state_callback f)
{
    ESP_ERROR_CHECK(esp_netif_init());                // 用于初始化tcpip协议栈
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // 创建一个默认系统事件调度循环，之后可以注册回调函数来处理系统的一些事件
    esp_netif_create_default_wifi_sta();              // 使用默认配置创建STA对象

    esp_netif_ap = esp_netif_create_default_wifi_ap(); // 使用默认配置创建AP对象

    // 初始化WIFI
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // 使用默认值赋值
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    //将回调函数传进变量，调用变量时实现回调函数内容
    wifi_state_cb = f;

    scansem = xSemaphoreCreateBinary();   //创建一个二进制信号量，返回句柄
    xSemaphoreGive(scansem);   //手动释放一次，为第一次创建扫描 做准备
    // 启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // 设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_start());                 // 启动WIFI

    ESP_LOGI(TAG, "wifi_init finished.");
}

/** 连接wifi，用sta模式
 * @param ssid
 * @param password
 * @return 成功/失败
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    sta_connect_count = 0;
    wifi_config_t wifi_config =
        {
            .sta =
                {
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK, // 加密方式
                },
        };
    snprintf((char *)wifi_config.sta.ssid, 31, "%s", ssid);         // 拷贝形参ssid至sta.ssid存储区
    snprintf((char *)wifi_config.sta.password, 63, "%s", password); // 拷贝形参password至sta.password存储区
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_STA)
    {
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_start();
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }
    return ESP_OK;
}

/** 设置成AP模式
 * @param  无
 * @return 成功/失败
 */
esp_err_t wifi_manager_ap(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_APSTA)
    {
        return ESP_OK;
    }

    esp_wifi_disconnect();              // 停止WiFi连接
    esp_wifi_stop();                    // 停止WiFi
    esp_wifi_set_mode(WIFI_MODE_APSTA); // 设置WiFi模式为STA_AP混杂模式
    wifi_config_t wifi_config = {
        .ap = {
            .channel = 5,                   // 通讯信道
            .max_connection = 2,            // 最大连接数
            .authmode = WIFI_AUTH_WPA2_PSK, // 加密方式
        }};
    snprintf((char *)wifi_config.ap.ssid, 32, "%s", WIFI_AP_SSID);     // 拷贝WIFI_SSID至ap.ssid存储区
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);                    // 账户名称长度
    snprintf((char *)wifi_config.ap.password, 64, "%s", WIFI_AP_PSWD); // 拷贝WIFI_PSWD至ap.password存储区
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);                     // 配置AP模式参数

    // 设置 IP地址，不设置会默认192.168.4.1
    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192, 168, 100, 1);      // 设置IP地址
    IP4_ADDR(&ipInfo.gw, 192, 168, 100, 1);      // 设置网关地址
    IP4_ADDR(&ipInfo.netmask, 255, 255, 255, 0); // 设置子网掩码

    // DHCP必须停止才能设置网络
    esp_netif_dhcps_stop(esp_netif_ap);           // 关闭DHCP
    esp_netif_set_ip_info(esp_netif_ap, &ipInfo); // 设置网络参数

    return esp_wifi_start(); // 启动wifi
}

/** 启动扫描函数
 * @param  param 扫描完成回调函数，为wifi_manager_scan()函数传参
 * @return 无
 */

static void scan_task(void *param)
{
    p_wifi_scan_callback callback = (p_wifi_scan_callback)param; // 强制转换一下属性
    uint16_t ap_count = 0;
    uint16_t ap_num = 20;

    // 栈空间极小，热点数量不确定，静态数组有严重缺陷，存在栈溢出风险，建议在堆空间申请内存，ap_list无须[]，加ap_list[]为栈上静态数组，直接定义指针变量为申请内存的返回值
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_num); // 申请 ap_num个wifi_ap_record_t大小的内存 malloc返回值是void * 需强转类型
    esp_wifi_scan_start(NULL, true);                                                           // 启动扫描
    esp_wifi_scan_get_ap_num(&ap_count);                                                       // 获取热点数量
    esp_wifi_scan_get_ap_records(&ap_num, ap_list);                                            // 获取扫描到的列表
    ESP_LOGI(TAG, "总共扫描的个数为%d,实际获取到的个数为%d",ap_count,ap_num);
    if(callback)
    {
        callback(ap_num,ap_list);
    }
    free(ap_list);  // 释放内存
    ap_list = NULL; // 置空，防止野指针
    xSemaphoreGive(scansem); //释放信号量，为下次创建 扫描做准备
    vTaskDelete(NULL);  //非循环任务，用完退出
}

/** 启动扫描函数
 * @param  f 扫描完成回调函数
 * @return 成功/失败
 */
esp_err_t wifi_manager_scan(p_wifi_scan_callback f)
{
    if(xSemaphoreTake(scansem, 0) == pdTRUE)    //获取信号量，防止重复创建 扫描
    {
         esp_wifi_clear_ap_list(); // 清空扫描列表

    // xTaskCreate 任务无内核绑定，FreeRTOS 调度器会自动在两个核之间切换运行。风险：如果你的业务任务跑到 Core0，会和 Wi-Fi / 蓝牙 / 事件循环抢算力，容易出现网络卡顿、蓝牙丢包、WiFi 重连异常。
        xTaskCreatePinnedToCore(scan_task, "scan", 8192, f, 3, NULL, 1); // 创建一个扫描 freertos任务
    } 
   return ESP_OK;
}