#include "ws_server.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include <string.h>

#define TAG "ws_server"

//html网页
static const char* http_html =NULL;

//websocket 接收数据回调函数
static web_receive_cb web_receive_fn;

//客户端fds
static int client_fds = -1;

//http服务器句柄
static httpd_handle_t sever_handle;

/** 响应HTTP GET请求的回调函数，这里处理方法就是简单的把HTML网页发回去
 * @param r http请求信息
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t get_http_req(httpd_req_t *r)
{
    if (http_html == NULL || http_html[0] == '\0')
    {
        ESP_LOGE(TAG, "html_code is NULL, webpage not loaded from SPIFFS");
        httpd_resp_set_status(r, "500 Internal Server Error");
        return httpd_resp_send(r, "apcfg.html missing", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, http_html, HTTPD_RESP_USE_STRLEN);
}

/** 响应Websocket数据的服务函数
 * @param r http请求信息
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t handle_ws_req(httpd_req_t *r)
{
    //过滤掉GET请求，GET请求是握手阶段
    if(r->method == HTTP_GET)
    {
        client_fds = httpd_req_to_sockfd(r);    //将fds保存下来，用于发送数据,脱离 httpd 回调函数时，也能直接给这个客户端发数据。
        return ESP_OK;
    }
    esp_err_t ret;
    httpd_ws_frame_t pkt; ////websocket帧
    //局部变量第一次赋值可以用{0},多次用memeset
    memset(&pkt,0,sizeof(pkt));   //清零
    //第一次调用recv_frame接收数据
    ret = httpd_ws_recv_frame(r, &pkt, 0);   //len 为0，方式A 栈安全。只填充长度，不填充payload数据地址
    if(ret != ESP_OK)
    {
        return ret;
    }
    uint8_t *buf = (uint8_t*)malloc(pkt.len + 1); //获取到数据长度后在堆上分配内存，准备接收缓冲区的数据
    if(buf == NULL)
    {
        return ESP_FAIL;
    }

    pkt.payload = buf; //在websocket帧中payload指针指向在堆上分配的内存
   
     //第二次调用recv_frame接收数据
    ret = httpd_ws_recv_frame(r, &pkt, pkt.len);
   if(ret == ESP_OK)
   {
        if(pkt.type ==  HTTPD_WS_TYPE_TEXT)  //判断是文本内容才执行自定义的接收数据回调函数
        {
            ESP_LOGI(TAG, "获取websocket数据为:%s",pkt.payload);
            if(web_receive_fn)
            {
                web_receive_fn(pkt.payload, pkt.len); //执行自定义的接收数据回调函数
            }
        }
    }

    free(buf);  //释放内存
    return ESP_OK;
}

/** 启动ws
 * @param cfg ws一些配置,请看ws_cfg_t定义
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t web_ws_start(ws_cfg_t* cfg)
{
    if(cfg == NULL)
        return ESP_FAIL;
    if (sever_handle)
    {
        ESP_LOGW(TAG, "http server already running");
        return ESP_OK;
    }

    http_html = cfg->html_code;  //赋值html网页
    web_receive_fn = cfg->receive_fn; //赋值websocket数据回调函数
    if (http_html == NULL)
    {
        ESP_LOGE(TAG, "start with empty html_code — open / will return 500");
    }

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();  //默认端口 80
    http_cfg.lru_purge_enable = true;
    esp_err_t err = httpd_start(&sever_handle, &http_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        sever_handle = NULL;
        return err;
    }

    //http请求参数设置
    httpd_uri_t uri_get ={
        .uri = "/",  //根目录
        .method = HTTP_GET, //get请求模式
        .handler = get_http_req,  //HTTP请求处理回调函数
    };
    httpd_register_uri_handler(sever_handle, &uri_get);

    //websocket请求参数设置 要在系统打开websocket server支持
    httpd_uri_t uri_ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .is_websocket = true,
    };
    httpd_register_uri_handler(sever_handle, &uri_ws);
    ESP_LOGI(TAG, "HTTP+WS started on port %d, uri=/ and /ws", http_cfg.server_port);
    return ESP_OK;
}



/** 停止ws
 * @param 无
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t web_ws_stop(void)
{
    if(sever_handle)
    {
        httpd_stop(sever_handle); //停止http服务器
        sever_handle = NULL;   //赋值，避免野指针
    }
    return ESP_OK;
}


/** 使用websocket协议向客户端发送数据
 * @param data 数据内容，
 * @param len 数据长度
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t web_ws_send(uint8_t* data, int len)
{
    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(pkt)); //清零
    pkt.payload = data;   
    pkt.len = len;
    pkt.type = HTTPD_WS_TYPE_TEXT;  //text格式 

    return httpd_ws_send_data(sever_handle, client_fds, &pkt);   //发送数据
}