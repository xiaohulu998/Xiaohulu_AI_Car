/**
 * @file    wifi_mode.c
 * @brief   ESP32 Wi-Fi 模式管理：AP 配网 + STA 联网
 *
 * 本模块负责 Wi-Fi 的启动、模式切换与停止：
 *   - AP 模式：设备作为热点，供手机连接配网，同时启动 Web 配网服务
 *   - STA 模式：设备作为客户端连接路由器
 *   - 提供连接回调机制，供上层在获取 IP 后执行业务逻辑
 */

#include "wifi_mode.h"

//静态变量区

/* 事件组句柄，用于同步等待 STA 连接结果 */
static EventGroupHandle_t wifi_event_group;

/* 用户注册的 STA 连接成功回调 */
static wifi_sta_connected_cb sta_conn_cb = NULL;

/* Wi-Fi 事件处理器实例句柄（用于注销，避免模式切换时重复注册） */
static esp_event_handler_instance_t s_wifi_evt_handle = NULL;
/* IP 事件处理器实例句柄 */
static esp_event_handler_instance_t s_ip_evt_handle   = NULL;


/* ========================================================================
 *  内部函数
 * ======================================================================== */

/**
 * @brief  Wi-Fi & IP 事件统一处理
 *
 * - WIFI_EVENT_STA_DISCONNECTED：掉线后自动重连
 * - IP_EVENT_STA_GOT_IP：获取 IP 后通知事件组并触发用户回调
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED)    //站点断开连接
        {
            /* STA 断线自动重连 */
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        //在esp-idf循环事件基础上，再套一层freertos事件标志组
        /* 拿到 IP 才算真正连上，设置标志位并回调上层 */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (sta_conn_cb) sta_conn_cb();
    }
}

/* ========================================================================
 *  对外 API
 * ======================================================================== */

/**
 * @brief  注册 STA 连接成功后的业务回调
 * @param  cb  回调函数指针（传 NULL 可取消）
 */
void wifi_set_sta_connect_cb(wifi_sta_connected_cb cb)
{
    sta_conn_cb = cb;
}

/**
 * @brief  启动 AP（热点）模式，用于手机配网
 *
 * 流程：
 *   1. 停止当前 Wi-Fi（如有）
 *   2. 创建 AP 网络接口，初始化 Wi-Fi
 *   3. 配置 SSID/密码/加密方式，启动 AP
 *   4. 启动配网 Web 服务（端口 80）
 */
void wifi_start_ap_mode(void)
{
    /* 先停掉当前模式，确保干净初始化 */
    wifi_stop_all();

    /* 创建默认 AP 网络接口 */
    esp_netif_create_default_wifi_ap();

    /* Wi-Fi 初始化（默认配置） */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    /* 填充 AP 参数 */
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = AP_SSID,
            .password       = AP_PASS,
            .ssid_len       = strlen(AP_SSID),
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK
        }
    };

    /* 设置为 AP 模式并应用配置 */
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();      //必须启动，不然没效果

    /* 启动网页配网服务（桥接 C++ WebServer） */
    web_server_start(80);
}

/**
 * @brief  启动 STA（客户端）模式，连接指定路由器
 * @param  ssid  目标 Wi-Fi SSID
 * @param  pass  目标 Wi-Fi 密码
 * @return true 连接成功（10 秒内获取到 IP），false 超时失败
 */
bool wifi_start_sta_mode(const char *ssid, const char *pass)
{
    /* 先停掉当前模式，确保干净初始化 */
    wifi_stop_all();

    /* 创建事件组，用于同步等待连接结果 */
    wifi_event_group = xEventGroupCreate();

    /* 创建默认 STA 网络接口 */
    esp_netif_create_default_wifi_sta();

    /* Wi-Fi 初始化（默认配置） */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    /* 注册 Wi-Fi & IP 事件处理器 */
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL,
                                        &s_wifi_evt_handle);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL,
                                        &s_ip_evt_handle);

    /* 填充 STA 连接参数 */
    wifi_config_t sta_cfg = {0};
    strcpy((char *)sta_cfg.sta.ssid, ssid);
    strcpy((char *)sta_cfg.sta.password, pass);

    /* 设置为 STA 模式，加载配置并发起连接 */
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();
    esp_wifi_connect();

    /* 阻塞等待连接成功（超时 10 秒） */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,    /* 不清除标志位 */
                                           pdTRUE,     /* 所有标志位都满足 */
                                           pdMS_TO_TICKS(10000));
    if (bits & WIFI_CONNECTED_BIT)
        return true;
    return false;
}

/**
 * @brief  停止所有 Wi-Fi 模式并释放资源
 *
 * 关闭顺序：
 *   1. 停止配网 Web 服务
 *   2. 注销事件处理器
 *   3. 停止并反初始化 Wi-Fi 驱动
 *
 * 注意：本函数可安全重复调用（各项均做了空指针/未启动检查）。
 */
void wifi_stop_all(void)
{
    /* 关闭配网网页服务（未启动时调用也安全） */
    web_server_stop();

    /* 注销 STA 事件处理器（避免模式切换时重复注册） */
    if (s_wifi_evt_handle) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_evt_handle);
        s_wifi_evt_handle = NULL;
    }
    if (s_ip_evt_handle) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              s_ip_evt_handle);
        s_ip_evt_handle = NULL;
    }

    /* 停止并反初始化 Wi-Fi 驱动 */
    esp_wifi_stop();
    esp_wifi_deinit();
}
