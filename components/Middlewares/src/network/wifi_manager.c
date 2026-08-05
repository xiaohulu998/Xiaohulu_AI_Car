/**
* WiFi 连接管理 & AP 配网 & WebSocket 服务接口
*/

#include "wifi_manager.h"
#include "board_def.h"
#include "bsp_board.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"   //二值信号量
#include "freertos/timers.h"
#include "lwip/ip4_addr.h"

/* ================================================================
 *  日志标签
 * ================================================================ */
static const char *TAG_WIFI     = "wifi_manager";
static const char *TAG_APCFG    = "ap_wifi";
static const char *TAG_WS       = "ws_server";

/* ================================================================
 *  宏定义
 * ================================================================ */

/* AP 模式默认热点 */
#define WIFI_AP_SSID    "ESP32S3_AP"
#define WIFI_AP_PSWD    "qwer1234"



/* STA 断线最大重连次数 */
#define MAX_CONNECT_RETRY 6

/* SPIFFS */
#define SPIFFS_BASE_PATH    "/spiffs"
#define HTML_PATH           "/spiffs/apcfg.html"

/* 事件标志位 */
#define APCFG_BIT           (BIT0)

/* ================================================================
 *  1.WiFi 驱动管理
 * ================================================================ */

static int                   sta_connect_count = 0;
static p_wifi_state_callback wifi_state_cb     = NULL;   // 回调函数
static esp_netif_t          *esp_netif_ap       = NULL;  // 定义esp_netif_t 结构体指针，代表一个网络接口实例（Wi-Fi AP 专用 netif）
static SemaphoreHandle_t     scansem            = NULL;  // 定义二值信号量句柄
static bool                  is_sta_connected   = false; // sta连接状态

/** 事件回调函数
 * @param arg   用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return 无
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:  // WIFI以STA模式启动后触发此事件
        {
            // 仅在已配置过目标 SSID 时才主动连接。
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if (mode == WIFI_MODE_STA) {
                wifi_config_t conf = {0};
                esp_wifi_get_config(WIFI_IF_STA, &conf);
                if (conf.sta.ssid[0] != '\0') {
                    esp_wifi_connect();
                }
            }
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: // WIFI连上路由器后，触发此事件
            ESP_LOGI(TAG_WIFI, "Connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: // WIFI从路由器断开连接后触发此事件
            if (is_sta_connected) 
            {
                if (wifi_state_cb) wifi_state_cb(WIFI_STATE_DISCONNECTED);
                is_sta_connected = false;
            }
            if (sta_connect_count < MAX_CONNECT_RETRY) // 如果小于 设置的继续重连次数
            {
                wifi_mode_t mode;
                esp_wifi_get_mode(&mode);
                if (mode == WIFI_MODE_STA) esp_wifi_connect(); // 继续重连
                sta_connect_count++;
            }
            ESP_LOGI(TAG_WIFI, "STA disconnected, retrying...");
            break;
        case WIFI_EVENT_AP_STACONNECTED: // AP连接
            ESP_LOGI(TAG_WIFI, "STA device connected to AP");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED: // AP断开连接
            ESP_LOGI(TAG_WIFI, "STA device disconnected from AP");
            break;
        default: // 都不符合情况，跳出
            break;
        }
    }
    if (event_base == IP_EVENT)  // IP相关事件
    {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP: // 只有获取到路由器分配的IP，才认为是连上了路由器
                ESP_LOGI(TAG_WIFI, "Get ip address");
                is_sta_connected = true;
                if (wifi_state_cb)
                    wifi_state_cb(WIFI_STATE_CONNECTED);
                break;
            default:
                break;
        }
    }
}

/* ── 公开 API ── */
 /**
 * @brief  初始化 WiFi（TCP/IP + 事件循环 + STA 默认模式）
 * @param  f  WiFi 状态变化回调
 */
void wifi_manager_init(p_wifi_state_callback f)
{
    /* 确保板级资源已初始化 */
    bsp_board_init();

    esp_netif_create_default_wifi_sta(); // 使用默认配置创建STA对象
    esp_netif_ap = esp_netif_create_default_wifi_ap(); // 使用默认配置创建AP对象
    
    // 初始化WIFI
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // 使用默认值赋值
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &event_handler, NULL));

    wifi_state_cb = f; //将回调函数传进变量，调用变量时实现回调函数内容

    scansem = xSemaphoreCreateBinary(); //创建一个二进制信号量，返回句柄
    xSemaphoreGive(scansem); //手动释放一次，为第一次创建扫描 做准备
    
    // 启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // 设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_start()); // 启动WIFI

    ESP_LOGI(TAG_WIFI, "wifi_manager init finished");
}

/**
 * @brief  STA 模式连接指定路由器
 * @param  ssid     WiFi 名称
 * @param  password WiFi 密码
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    sta_connect_count = 0;
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,  // 加密方式
        },
    };
    snprintf((char *)wifi_config.sta.ssid, 31, "%s", ssid); // 拷贝形参ssid至sta.ssid存储区
    snprintf((char *)wifi_config.sta.password, 63, "%s", password); // 拷贝形参password至sta.password存储区
    ESP_ERROR_CHECK(esp_wifi_disconnect());  //断开当前已经连接的WiFi

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_STA) 
    {
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_start();
    } else 
    {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }
    return ESP_OK;
}

/**
 * @brief  切换到 AP+STA 混杂模式（开启热点）
 * @return ESP_OK 成功
 */

esp_err_t wifi_manager_ap(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_APSTA) 
    {
        return ESP_OK;
    }

    esp_wifi_disconnect(); // 停止WiFi连接
    esp_wifi_stop(); // 停止WiFi
    esp_wifi_set_mode(WIFI_MODE_APSTA); // 设置WiFi模式为STA_AP混杂模式

    wifi_config_t wifi_config = {
        .ap = {
            .channel        = 5, // 通讯信道
            .max_connection = 2, // 最大连接数
            .authmode       = WIFI_AUTH_WPA2_PSK, // 加密方式
        },
    };
    snprintf((char *)wifi_config.ap.ssid, 32, "%s", WIFI_AP_SSID);     // 拷贝WIFI_SSID至ap.ssid存储区
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);                    // 账户名称长度
    snprintf((char *)wifi_config.ap.password, 64, "%s", WIFI_AP_PSWD); // 拷贝WIFI_PSWD至ap.password存储区
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);                     // 配置AP模式参数

    /* 设置静态 IP , 不设置会默认192.168.4.1*/
    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192, 168, 100, 1);
    IP4_ADDR(&ipInfo.gw, 192, 168, 100, 1);
    IP4_ADDR(&ipInfo.netmask, 255, 255, 255, 0);
    
    // 改 IP 前必须先停 DHCP；设完 IP 后务必再 start，否则手机连上热点却拿不到地址，网页打不开
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ipInfo));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));


    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG_WIFI, "AP started: SSID=%s IP=192.168.100.1", WIFI_AP_SSID);
    } else {
        ESP_LOGE(TAG_WIFI, "AP start failed: %s", esp_err_to_name(err));
    }
    return err;
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
    ESP_LOGI(TAG_WIFI, "总共扫描的个数为%d,实际获取到的个数为%d",ap_count,ap_num);
    if(callback)
    {
        callback(ap_num,ap_list);
    }
    free(ap_list);  // 释放内存
    ap_list = NULL; // 置空，防止野指针
    xSemaphoreGive(scansem); //释放信号量，为下次创建 扫描做准备
    vTaskDelete(NULL);  //非循环任务，用完退出
}

/**
 * @brief  启动 WiFi 扫描
 * @param  f  扫描完成回调
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_scan(p_wifi_scan_callback f)
{
    if (xSemaphoreTake(scansem, 0) == pdTRUE) 
    {
        esp_wifi_clear_ap_list();
        xTaskCreatePinnedToCore(scan_task, "scan", 8192, f, 3, NULL, 1);
    }
    return ESP_OK;
}

/* ================================================================
 * 2.智能配网
 * ================================================================ */

static EventGroupHandle_t apcfg_ev; // 事件标志组句柄
static char  current_ssid[32]; // 客户端发过来的账号
static char  current_password[64]; // 客户端发过来的密码
static char *html_code = NULL;

/** 从spiffs中加载html页面到内存   //Flash 读取速度远慢于 RAM，网页卡顿
 * @param 无
 * @return 无 
*/
static char *load_html_from_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = SPIFFS_BASE_PATH, //挂载点
        .format_if_mount_failed = false, //挂载失败是否执行格式化
        .max_files              = 3, //最大打开的文件数
        .partition_label        = "html", //分区名称
    };

    //挂载spiffs
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG_APCFG, "SPIFFS mount failed: %s (partition html 是否已烧录？)",
                 esp_err_to_name(ret));
        return NULL;
    }

    struct stat st; //用来存放stat()函数查询到的文件属性，文件大小存起来
    if (stat(HTML_PATH, &st) != 0)  //查找文件是否存在 返回值0代表成功，非0代表失败
    {
        ESP_LOGE(TAG_APCFG, "apcfg.html没有找到...... 路径=%s", HTML_PATH);
        return NULL;
    }
    ESP_LOGI(TAG_APCFG, "apcfg.html size=%ld", (long)st.st_size);

    char *buf = (char *)malloc(st.st_size + 1);  //堆上分配内存，存储html网页 +1避免字符串/0
    if (!buf) return NULL;
    memset(buf, 0, st.st_size + 1);  //清空堆缓冲区

    FILE *fp = fopen(HTML_PATH, "r");
    if (fp) 
    {   
        //从文件读取二进制数据到内存缓冲区
        if (fread(buf, st.st_size, 1, fp) == 0) 
        {
            free(buf); //释放掉
            buf = NULL;
        }
        fclose(fp); //关掉文件夹
    } 
    else 
    {
        free(buf);
        buf = NULL;
    }
    return buf; //返回内存的地址
}

/** wifi扫描完成回调函数，扫描完成弄成json
 * @param num 扫描到的ap个数
 * @param ap_records ap信息
 * @return 无 
*/
static void wifi_scan_cb(int num, wifi_ap_record_t *ap_record)
{
    /*
    {
    "wifi_list":[
        {
            "ssid":"test1",
            "rssi":-23,
            "encrypted":true
        },
        {
            "ssid":"test2",
            "rssi":-70,
            "encrypted":true
        }
       ]
    }
    */
    cJSON *root        = cJSON_CreateObject(); //创建一个JSON 根对象 {}
    cJSON *wifi_list_js = cJSON_AddArrayToObject(root, "wifi_list"); //在 root 对象里新增数组键值对

    for (int i = 0; i < num; i++)  //遍历ap_records，生成对应的JSON格式
    {
        cJSON* wifi_js = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_js, "ssid", (char *)ap_record[i].ssid);  //填充账号名称
        cJSON_AddNumberToObject(wifi_js, "rssi", ap_record[i].rssi);   //填充信号强度
        if(ap_record[i].authmode == WIFI_AUTH_OPEN)  //判断一下是否为加密WIFI
        {
            cJSON_AddBoolToObject(wifi_js, "encrypted", 0);
        }else
        {
            cJSON_AddBoolToObject(wifi_js, "encrypted", 1);
        }
        cJSON_AddItemToArray(wifi_list_js, wifi_js);  //添加进wifi_list_js数组
    }

    char *data = cJSON_Print(root); //打包成字符串
    if (data) 
    {
        ESP_LOGI(TAG_APCFG, "ws 发送:%s", data);
        ws_server_send((uint8_t*) data, strlen(data));  //生成完JSON字符串后，发送列表数据给客户端
        cJSON_free(data);    //释放data内存，回收
    }
    cJSON_Delete(root);
}

/** ws接收回调函数 解析 scan / ssid+password 指令
 * @param payload 数据
 * @param len 数据长度
 * @return 无 
 */
static void ws_receive_cb(uint8_t *payload, int len)
{
   cJSON* root = cJSON_Parse((char*)payload);  //解析JSON字符串为cJSON对象树
    if(root)
    {
        cJSON* scan_js = cJSON_GetObjectItem(root, "scan");  //接收键值对
        cJSON* ssid_js = cJSON_GetObjectItem(root, "ssid");  //接收键值对
        cJSON* password_js = cJSON_GetObjectItem(root, "password");

        //如果提取到"scan"，说明这个是下发扫描启动的指令，需要启动扫描
        if(scan_js)
        {
            char* scan_value = cJSON_GetStringValue(scan_js);  //提取scan_js值，字符串
            if(scan_value && strcmp(scan_value, "start") == 0)  //判断字符串是否相等
            {
                //启动扫描
                wifi_manager_scan(wifi_scan_cb);
            }
        }

        //如果提取到"ssid"和"password"，说明这个是客户端发来要求连接的SSID和密码
        if(ssid_js && password_js)
        {
            char* ssid_value = cJSON_GetStringValue(ssid_js);   //提取ssid_js值，字符串
            char* password_value = cJSON_GetStringValue(password_js); //提取password_js值，字符串

            if(ssid_value && password_value)
            {
                snprintf(current_ssid, sizeof(current_ssid), "%s", ssid_value);  //复制ssid值到全局变量
                snprintf(current_password, sizeof(current_password), "%s", password_value);  //复制password值到全局变量

                xEventGroupSetBits(apcfg_ev, APCFG_BIT);   //设置事件标志位

                //此回调函数里面由websocket底层调用，不宜直接调用关闭服务器操作
                //ws_server_stop();
                //wifi_manager_connect(ssid_value, password_value);   //切换至sta模式连接
            }
        }
        cJSON_Delete(root);
    }
}

/** AP 配网任务,等待配网提交后切换 STA
 * @param param 
 * @return 无
*/

static void apcfg_task(void *param)
{
    EventBits_t ev; 
    while(1)
    {
       //等待事件标志位，读取后清除，等待10s
        ev = xEventGroupWaitBits(apcfg_ev,APCFG_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10*1000));
        if(ev & APCFG_BIT)  //bit0位被置1
        {
            ws_server_stop();  //停止服务器
            wifi_manager_connect(current_ssid, current_password);   //连接WIFI
        }  
    }
}

/* ── 公开 API ── */
/** wifi功能初始化
 * @param f 状态通知回调函数
 * @return 无
*/
void wifi_apcfg_init(p_wifi_state_callback f)
{
    wifi_manager_init(f);  //调用wifi_manager_init初始化wifi
    html_code = load_html_from_spiffs(); //加载html网页至内存中
    apcfg_ev = xEventGroupCreate();    //创建事件标志组
    xTaskCreatePinnedToCore(apcfg_task,"apcfg",4096,NULL,3,NULL,1);   //创建freertos任务函数
}

/**
 * @brief  进入 AP 配网模式（开启热点 + WebSocket 服务）
 */
void wifi_apcfg_start(void)
{
    if (html_code == NULL)
    {
        ESP_LOGE(TAG_APCFG, "html_code is NULL, 网页未加载，配网页面将无法显示");
    }
    wifi_manager_ap();   //调用函数设置成AP模式
    ws_cfg_t ws_cfg ={
        .html_code = html_code,
        .receive_fn = ws_receive_cb,
    };
    if (ws_server_start(&ws_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG_APCFG, "ws_server_start failed");
    }
}

/* ================================================================
 *  3.HTTP + WebSocket 服务器 
 * ================================================================ */

static const char     *s_http_html       = NULL; //html网页
static web_receive_cb  s_web_receive_fn  = NULL; //websocket 接收数据回调函数
static int             s_client_fds      = -1;  //客户端fds
static httpd_handle_t  s_server_handle   = NULL; //http服务器句柄

/** 响应HTTP GET请求的回调函数，这里处理方法就是简单的把HTML网页发回去
 * @param req http请求信息
 * @return  ESP_OK or ESP_FAIL
*/
static esp_err_t http_get_handler(httpd_req_t *req)
{
    if (s_http_html == NULL || s_http_html[0] == '\0') {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "apcfg.html missing", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_http_html, HTTPD_RESP_USE_STRLEN);
}

/** 响应Websocket数据的服务函数
 * @param req http请求信息
 * @return  ESP_OK or ESP_FAIL
*/
static esp_err_t ws_handler(httpd_req_t *req)
{
    //过滤掉GET请求，GET请求是握手阶段
    if (req->method == HTTP_GET) {
        s_client_fds = httpd_req_to_sockfd(req); //将fds保存下来，用于发送数据,脱离 httpd 回调函数时，也能直接给这个客户端发数据。
        return ESP_OK;
    }

    /* 接收 WebSocket 帧 */
    httpd_ws_frame_t pkt; //websocket帧
    memset(&pkt, 0, sizeof(pkt)); //清零

    //第一次调用recv_frame接收数据
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0); //len 为0，方式A 栈安全。只填充长度，不填充payload数据地址
    if (ret != ESP_OK) return ret;   

    uint8_t *buf = (uint8_t *)malloc(pkt.len + 1);
    if (!buf) return ESP_FAIL;

    //第二次调用recv_frame接收数据
    pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len); //获取到数据长度后在堆上分配内存，准备接收缓冲区的数据

    if (ret == ESP_OK && pkt.type == HTTPD_WS_TYPE_TEXT)  //判断是文本内容才执行自定义的接收数据回调函数
    {
        ESP_LOGI(TAG_WS, "获取websocket数据为: %s", pkt.payload);
        if (s_web_receive_fn) {
            s_web_receive_fn(pkt.payload, pkt.len); //执行自定义的接收数据回调函数
        } 
    }

    free(buf);
    return ESP_OK;
}

/* ── 公开 API ── */
/**
 * @brief  启动 HTTP + WebSocket 服务器
 * @param  cfg  配置（HTML 页面 + 接收回调）
 * @return ESP_OK 成功
 */
esp_err_t ws_server_start(ws_cfg_t *cfg)
{
    if (!cfg) return ESP_FAIL;
    if (s_server_handle) {
        ESP_LOGW(TAG_WS, "HTTP server already running");
        return ESP_OK;
    }

    s_http_html      = cfg->html_code; //赋值html网页
    s_web_receive_fn = cfg->receive_fn; //赋值websocket数据回调函数

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG(); //默认端口 80
    http_cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server_handle, &http_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WS, "httpd_start failed: %s", esp_err_to_name(err));
        s_server_handle = NULL;
        return err;
    }

    /* 注册 GET / 路由 */
    httpd_uri_t uri_get = {
        .uri      = "/", //根目录
        .method   = HTTP_GET,
        .handler  = http_get_handler, //HTTP请求处理回调函数
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server_handle, &uri_get);

    /* 注册 WebSocket /ws 路由，要在系统打开websocket server支持*/
    httpd_uri_t uri_ws = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server_handle, &uri_ws);

    ESP_LOGI(TAG_WS, "HTTP+WS started on port %d", http_cfg.server_port);
    return ESP_OK;
}

/**
 * @brief  停止 HTTP + WebSocket 服务器
 * @return ESP_OK 成功
 */
esp_err_t ws_server_stop(void)
{
    if (s_server_handle) {
        httpd_stop(s_server_handle);
        s_server_handle = NULL;
        ESP_LOGI(TAG_WS, "HTTP+WS server stopped");
    }
    return ESP_OK;
}

/**
 * @brief  通过 WebSocket 向客户端发送文本数据
 * @param  data  数据内容
 * @param  len   数据长度
 * @return ESP_OK 成功
 */
esp_err_t ws_server_send(uint8_t *data, int len)
{
    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.payload = data;
    pkt.len     = len;
    pkt.type    = HTTPD_WS_TYPE_TEXT;  //text格式 

    return httpd_ws_send_data(s_server_handle, s_client_fds, &pkt); //发送数据
}
