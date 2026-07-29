#include "esp_ota_ops.h"
#include "onenet_ota.h"
#include "stdio.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "onenet_token.h"
#include "onenet_mqtt.h"
#include "string.h"
#include "cJSON.h"



#define TAG "onenet_ota"

#define ONENET_OTA_URL "http://iot-api.heclouds.com/fuse-ota"

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


/**
 * 发起http请求
 * @param url 请求地址
 * @param method 请求方法
 * @param payload 消息体内容
 * @return 错误码
 */
static esp_err_t onenet_ota_http_connect(const char* url, esp_http_client_method_t method, const char* payload )
{
    /*
    #POST是说明设备使用的http协议中需要使用POST方法来提交版本信息
    #POST之后是Http的url地址，地址里面pro_id是产品ID，dev_name是设备名称
    POST http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/version

    #数据类型
    Content-Type: application/json

    #token鉴权
    Authorization:version=2022-05-01&res=userid%2F112&et=1662515432&method=sha1&sign=Pd14JLeTo77e0FOpKN8bR1INPLA%3D

    #主机
    host:iot-api.heclouds.com
    #消息体的长度
    Content-Length:xx

    #消息体的内容
    {"s_version":"V1.3", "f_version": "V2.0"}
    */

    char* token =(char*)malloc(256); //在堆上申请地址
    memset(token,0, 256);  
    dev_token_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME,
                                   ONENET_PRODUCT_ID, ONENET_DEVICE_NAME,
                                   ONENET_PRODUCT_ACCESS_KE);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t http_client = esp_http_client_init(&config);

   

    // POST
    //设置发送请求头
    esp_http_client_set_method(http_client, method);   //模式
    esp_http_client_set_header(http_client, "Content-Type", "application/json"); //添加数据类型
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");  //添加主机
    esp_http_client_set_header(http_client,"Authorization",token);   //添加token

    if(payload)
    {
       ESP_LOGI(TAG,"post data:%s",payload);
       esp_http_client_set_post_field(http_client, payload, strlen(payload)); //添加消息体内容
    }

    //清零
    memset(ota_data_buff, 0, sizeof(ota_data_buff));
    ota_data_size = 0;

    //esp_http_client_perform这个函数会阻塞，直到完整的http请求结束才返回
    esp_err_t err = esp_http_client_perform(http_client);  //发送配置好的请求

    //返回ESP_OK 仅体现TCP 连接正常，400、401、404、500 这类错误码时，依旧返回ESP_OK
    if (err == ESP_OK)    //通信链路正常
    {
        int status = esp_http_client_get_status_code(http_client);     //获取HTTP 响应状态
        int64_t body_len = esp_http_client_get_content_length(http_client); //获取服务器返回响应体（Body）的字节长度
        ESP_LOGI(TAG,"http status:%d, body len:%lld", status, body_len);
        if(status == 200)
        {
        // 业务正常，读取响应JSON，解析返回结果
        }
        else
        {
        // 服务器返回错误(401/2402等)，虽然通信通了，但是业务失败
        ESP_LOGE(TAG,"云端返回错误码 %d", status);
        }
    } 
    else 
    {
        // 网络层面失败，连服务器都没连上
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));  //把数字错误码转换成可读字符串。
    }
    
    //清理操作
    esp_http_client_cleanup(http_client);
    free(token);
    
    return err;
}


/**
 * 上报版本号
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_ota_upload_version(void)
{
    esp_err_t ret = ESP_FAIL;
    //生成url
    char url[256];
    snprintf(url, sizeof(url), ONENET_OTA_URL"/%s/%s/version", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME); 
    
    //生成消息体，消息体较简单，用snprintf直接生成
    char version[128];
    const char *app_version = get_app_version();  //获取app版本号
    //生成消息体内容（版本号)
    snprintf(version, sizeof(version), "{\"s_version\":\"%s\", \"f_version\": \"app_version\"}", app_version, app_version ); //  \转义，代表字符串内部内容，填充字符串的值
    
    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_POST, version))
    {
        cJSON* root = cJSON_Parse((char*)ota_data_buff);
       
        /* 返回的数据格式
        {
        "code": 0,
        "msg": "succ",
        "request_id": "**********"
        }
        */
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root," code");
            if(code_js && cJSON_GetStringValue(code_js) == 0)
            {
               ret = ESP_OK;
               cJSON_Delete(root);
            }
       }
     }
    if(ret != ESP_OK)
    {
        ESP_LOGI(TAG, "上报版本号失败");
    }
    return ret;
}

/**
 * 查询升级任务状态
 * @param type = 1,说明是完整包，type=2,说明是差分包
 * @param version 当前设备版本
 * @return 错误码
 */
esp_err_t  onenet_ota_check_task(const char* type,const char* version)
{
    /*请求头参考
    GET http://iot-api.heclouds.com/fuse-ota/{pro_id}/{dev_name}/check?type=1&version=1.2
    Content-Type: application/json

    Authorization:version=2022-05-01&res=userid%2F112&et=1662515432&method=sha1&sign=Pd14JLeTo77e0FOpKN8bR1INPLA%3D

    host:iot-api.heclouds.com

    Content-Length:20
    */
    
    esp_err_t ret = ESP_FAIL;
    //生成url
    char url[256];
    //生成URL
    snprintf(url, sizeof(url), ONENET_OTA_URL"/%s/%s/check?type=%s&version=%s", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, type, version);  //%s 字符串首地址，%c单个字符。拷贝值
       
    if(ESP_OK == onenet_ota_http_connect(url, HTTP_METHOD_GET, version))
    {
        cJSON* root = cJSON_Parse((char*)ota_data_buff);
       
        /* 返回的数据格式
        {
        "code": 0,
        "msg": "succ",
        "request_id": "**********",
        "data": {
            "target": "1.2", // 升级任务的目标版本
            "tid": 12, //任务ID
            "size": 123, //文件大小
            "md5": "dfkdajkfd", //升级文件的md5
            "status": 1 | 2 | 3, //1 ：待升级， 2 ：下载中， 3 ：升级中
            "type": 1 | 2 // 1:完整包，2：差分包  
	        }
        }
        */
        if(root)
        {
            cJSON* code_js = cJSON_GetObjectItem(root, "code");
            cJSON* data_js = cJSON_GetObjectItem(root, "data");
            cJSON* target_js = cJSON_GetObjectItem(data_js, "target");
            cJSON* tid_js = cJSON_GetObjectItem(data_js, "tid");

            if(code_js && cJSON_GetStringValue(code_js) == 0)  //code为0代表成功
            {
               ret = ESP_OK;
              
              
              
               cJSON_Delete(root);
            }
       }
     }
    if(ret != ESP_OK)
    {
        ESP_LOGI(TAG, "上报版本号失败");
    }
    return ret;    



}
