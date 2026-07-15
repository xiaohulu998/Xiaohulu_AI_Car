#include "ws_server.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include <string.h>

#define TAG     "ws_server"

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
   return httpd_resp_send(r,http_html,HTTPD_RESP_USE_STRLEN);   //发送html网页

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
    if(buf = NULL)
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
    http_html = cfg->html_code;  //赋值html网页
    web_receive_fn = cfg->receive_fn; //赋值websocket数据回调函数

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();  //赋值默认配置
    httpd_start(&sever_handle,&http_cfg);  //传参启动http服务器
    
    //http请求参数设置
    httpd_uri_t uri_get ={
        .uri = "/",  //根目录
        .method = HTTP_GET, //get请求模式
        .handler = get_http_req,  //HTTP请求处理回调函数
    };
    httpd_register_uri_handler(sever_handle, &uri_get);

    //websocket请求参数设置 要在系统打开wesocket server支持
    httpd_uri_t uri_ws = {
        .uri = "/ws",  //根目录下ws文件夹
        .method = HTTP_GET,
        .handler = handle_ws_req, //websocket请求处理回调函数
        .is_websocket = true,
    };
    httpd_register_uri_handler(sever_handle, &uri_ws);
    return ESP_OK;
}







/** 停止ws
 * @param 无
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t web_ws_stop(void);


/** 使用websocket协议向客户端发送数据
 * @param data 数据内容，
 * @param len 数据长度
 * @return  ESP_OK or ESP_FAIL
*/
esp_err_t web_ws_send(uint8_t* data, int len);