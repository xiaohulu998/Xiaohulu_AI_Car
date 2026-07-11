#include "wifi.h"

// 确保系统级初始化只执行一次
static bool s_sys_inited = false;

// WiFi配网页面提交回调：收到SSID和密码后保存切STA
void on_wifi_config_submit(const char *ssid, const char *pass)
{
    printf("收到配网信息 SSID:%s PWD:%s\n", ssid, pass);
    wifi_store_t store = {0};   //结构体变量赋初值0
    strncpy(store.ssid, ssid, WIFI_SSID_LEN - 1);     //限制最大复制长度,减1减掉\0 字符串结束符
    strncpy(store.password, pass, WIFI_PASS_LEN - 1);
    wifi_nvs_save(&store);

    // 关闭AP+网页，启动STA
    wifi_stop_all();
    bool connect_ok = wifi_start_sta_mode(ssid, pass);
    if (!connect_ok) {
        // 联网失败，重新切回AP配网
        printf("STA连接失败，重新开启AP配网\n");
        wifi_start_ap_mode();
    }
}

// STA联网成功后的业务数据传输函数
void on_sta_wifi_connected(void)
{
    printf("WiFi STA联网成功，开始数据传输\n");
    // TODO: 在这里添加你的TCP/MQTT/HTTP数据传输业务代码
}

void wifi_conect_init(void)
{
    // ── 系统级资源（全局仅初始化一次） ──
    if (!s_sys_inited) {
        esp_err_t ret = nvs_flash_init(); 
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init(); //后期将优化此部分。此上部分建议放在main.c，避免别的组件初始化了nvs，又需要重新配网。
        }
        esp_netif_init();   //初始化LWIP协议栈
        esp_event_loop_create_default();  //创建循环事件组
        s_sys_inited = true;
    }

    // 注册回调
    web_server_reg_wifi_config_cb(on_wifi_config_submit); //Web 收到配网信息 → 触发 on_wifi_config_submit()。1.存密码；2.切换sta模式
    wifi_set_sta_connect_cb(on_sta_wifi_connected); //STA 联网成功获取 IP → 触发 on_sta_wifi_connected()。开启执行业务函数（未写）

    // 根据NVS中是否有保存的WiFi决定启动模式
    wifi_store_t wifi_cfg;
    bool has_saved_wifi = wifi_nvs_load(&wifi_cfg);
    if (has_saved_wifi) {
        printf("读取到已保存WiFi [%s]，尝试STA连接\n", wifi_cfg.ssid);
        bool ok = wifi_start_sta_mode(wifi_cfg.ssid, wifi_cfg.password);
        if (!ok) {
            printf("WiFi连接失败，进入AP配网模式\n");
            wifi_start_ap_mode();
        }
    } else {
        printf("无保存WiFi，开启AP热点配网\n");
        wifi_start_ap_mode();
    }
}