#include "esp_ota_ops.h"
#include "onenet_ota.h"
#include "stdio.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define TAG "onenet_ota"


// 接收到的http 数据
#define OTA_BUFF_LEN 1024
static uint8_t ota_data_buff[OTA_BUFF_LEN];

//数据可能过长，一次收不完。已接收到的http数据长度
static int ota_data_size = 0;


/**
 * http事件回调函数
 * @param evt 包含http的数据
 * @return 错误码
 */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:   //接收数据事件
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            printf("HTTP_EVENT_ON_DATA data=%.*s\r\n", evt->data_len,(char*)evt->data);  // %. *S 打印固定长度的字符串，防止内存越界乱读，乱码、程序崩溃。
           
            //防止拷贝溢出
            int copy_len = 0;
            if(evt->data_len > (OTA_BUFF_LEN - ota_data_size))   //如果大于剩余长度
            {
                copy_len = OTA_BUFF_LEN - ota_data_size;   //只拷贝剩余长度
            }
            else 
            {
                copy_len = evt->data_len;   //否则拷贝原始长度
            }
            memcpy(&ota_data_buff[ota_data_size],evt->data,copy_len);  //拷贝内存
            ota_data_size += copy_len;   //更新已接收到的http数据长度
            
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;

        default: 
            break;    
    }
    return ESP_OK;
}



/**
 * 获取应用程序版本号
 * @param 无
 * @return 版本号
 */
const char* get_app_version(void)
{
    static char app_version[32] = {0};
    if (app_version[0] == 0)   //惰性加载（只初始化一次）
    {
    
    //获取当前运行的app分区信息
    const esp_partition_t* running = esp_ota_get_running_partition();

    //根据app分区信息获取app描述信息
    esp_app_desc_t esp_app_desc;
    esp_ota_get_partition_description(running, &esp_app_desc);
    snprintf(app_version, sizeof(app_version), "%s",esp_app_desc.version);
    // return esp_app_desc.version; //不能直接返回地址，函数退出栈回收，地址会变成野指针，数值无效
    }
    return app_version;    
}


/**
 * 设置合法启动分区
 * @param vaild 是否合法
 * @return 无
 */
void set_app_valid(int valid)
{
    //获取当前运行的app分区信息
    const esp_partition_t* running = esp_ota_get_running_partition();  

    //获取当前运行的app状态
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running , &ota_state) == ESP_OK) 
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY )  //上电启动新app分区，会自动把新app分区从new 标记成 ESP_OTA_IMG_PENDING_VERIFY
        {
            if (valid)  //合法
            {
                esp_ota_mark_app_valid_cancel_rollback(); //将新app分区设置为 ESP_OTA_IMG_VALID 合法 
            }
            else 
            {
                esp_ota_mark_app_invalid_rollback_and_reboot(); //设置成非法并重启
            }
        }
    }

}


//封装
static esp_err_t onenet_ota_http_connect


/**
 * 上报版本号
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_ota_upload_version(void)
{
    esp_http_client_config_t config = {
        .url = "/",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

   

    // POST
    const char *post_data = "{\"field1\":\"value1\"}";
    esp_http_client_set_url(client, "http://"CONFIG_EXAMPLE_HTTP_ENDPOINT"/post");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }



}

