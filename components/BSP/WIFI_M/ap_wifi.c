#include "ap_wifi.h"
#include "wifi_manager.h"
#include "esp_spiffs.h"
#include "ws_server.h"
#include <sys/stat.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


#define TAG "AP_WIFI"
#define  SPIFFS_BASE_PATH   "/spiffs"   //spiffs挂载点
#define  HTML_PATH          "/spiffs/apcfg.html"  //html网页在spiffs文件系统中的路径

//客户端发过来的密码
static char current_ssid[32];
static char current_password[64];


// 事件标志组句柄
static EventGroupHandle_t apcfg_ev;
//定义事件标志位
#define APCFG_BIT   (BIT0)

static char* html_code = NULL;

/** 从spiffs中加载html页面到内存   //Flash 读取速度远慢于 RAM，网页卡顿
 * @param 无
 * @return 无 
*/
static char* init_web_page_buffer(void)
{
    esp_vfs_spiffs_conf_t conf ={
        .base_path = SPIFFS_BASE_PATH, //挂载点
        .format_if_mount_failed = false, //挂载失败是否执行格式化
        .max_files = 3,    //最大打开的文件数
        .partition_label = "html", //分区名称
    };
    //挂载spiffs
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s (partition html 是否已烧录?)", esp_err_to_name(ret));
        return NULL;
    }

    struct stat st;  //用来存放stat()函数查询到的文件属性，文件大小存起来
    //查找文件是否存在
    if(stat(HTML_PATH,&st))  //返回值0代表成功，非0代表失败
    {
        ESP_LOGE(TAG, "apcfg.html没有找到...... path=%s", HTML_PATH);
        return NULL;
    }
    ESP_LOGI(TAG, "apcfg.html size=%ld", (long)st.st_size);
     //堆上分配内存，存储html网页
    char* buf = (char* )malloc(st.st_size + 1);  //+1避免字符串/0
    if(!buf)
    {
        return NULL;
    }
    memset(buf, 0, st.st_size + 1);  //清空堆缓冲区
    FILE *fp = fopen(HTML_PATH, "r");    //只读方式打开html文件
    if(fp)
    {
        //从文件读取二进制数据到内存缓冲区
        if(fread(buf,st.st_size, 1, fp) == 0)
        {
            free(buf); //释放掉
            buf = NULL;
        }
        fclose(fp);  //关掉文件夹
    }
    else
    {
        free(buf); //释放掉
        buf = NULL; 
    }
    return buf;  //返回内存的地址
}

/** 任务函数
 * @param param 
 * @return 无
*/
static void ap_wifi_task(void* param)
{
    EventBits_t ev; 
    while(1)
    {
       //等待事件标志位，读取后清除，等待10s
        ev = xEventGroupWaitBits(apcfg_ev,APCFG_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10*1000));
        if(ev & APCFG_BIT)  //bit0位被置1
        {
            web_ws_stop();  //停止服务器
            wifi_manager_connect(current_ssid, current_password);   //连接WIFI
        }  
    }
}

/** wifi功能初始化
 * @param f 状态通知回调函数
 * @return 无
*/
void ap_wifi_init(p_wifi_state_callback f)
{
    wifi_manager_init(f);  //调用wifi_manager_init初始化wifi
    html_code = init_web_page_buffer(); //加载html网页至内存中
    apcfg_ev = xEventGroupCreate();    //创建事件标志组
    xTaskCreatePinnedToCore(ap_wifi_task,"apcfg",4096,NULL,3,NULL,1);   //创建freertos任务函数
    

}


/** wifi扫描完成回调函数，扫描完成弄成json
 * @param num 扫描到的ap个数
 * @param ap_records ap信息
 * @return 无 
*/
void wifi_scan_cb(int num, wifi_ap_record_t *ap_record)
{
    cJSON* root = cJSON_CreateObject();   //创建一个JSON 根对象 {}
    cJSON* wifi_list_js = cJSON_AddArrayToObject(root, "wifi_list"); //在 root 对象里新增数组键值对
    for (int i = 0;i < num; i++)   //遍历ap_records，生成对应的JSON格式
    {   
        cJSON* wifi_js = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_js, "ssid", (char *)ap_record[i].ssid);  //填充账号名称
        cJSON_AddNumberToObject(wifi_js, "rssi", ap_record[i].rssi);   //填充信号强度
        if(ap_record[i].authmode == WIFI_AUTH_OPEN)  //判断一下是否为加密WIFI
        {
            cJSON_AddBoolToObject(wifi_js, "encrypted", 0);
        }
        else
        {
            cJSON_AddBoolToObject(wifi_js, "encrypted", 1);
        }
        cJSON_AddItemToArray(wifi_list_js, wifi_js);  //添加进wifi_list_js数组
    }
    char * data = cJSON_Print(root);    //返回字符串
    if(data)
    {
        ESP_LOGI(TAG, "ws 发送:%s", data);
        web_ws_send((uint8_t*) data, strlen(data));  //生成完JSON字符串后，发送列表数据给客户端
        cJSON_free(data);    //释放data内存，回收
    }
    cJSON_Delete(root);  //删除根节点，子节点递归删除
}


//ws接收到的处理回调函数  以JSON格式进行交互
/** ws接收回调函数
 * @param payload 数据
 * @param len 数据长度
 * @return 无 
*/
static void ws_receive_cb (uint8_t* payload, int len)
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
                //web_ws_stop();
                //wifi_manager_connect(ssid_value, password_value);   //切换至sta模式连接
            }
        }

        cJSON_Delete(root);  //释放JSON对象树
    }
}

/** 进入AP配网
 * @return 无
*/
void ap_wifi_apcfg()
{
    if (html_code == NULL)
    {
        ESP_LOGE(TAG, "html_code is NULL, 网页未加载，配网页面将无法显示");
    }
    wifi_manager_ap();   //调用函数设置成AP模式
    ws_cfg_t ws_cfg ={
        .html_code = html_code,
        .receive_fn = ws_receive_cb,
    };
    if (web_ws_start(&ws_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "web_ws_start failed");
    }
}
