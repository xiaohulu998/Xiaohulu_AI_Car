#include "esp_ota_ops.h"
#include "onenet_ota.h"
#include "stdio.h"




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
                esp_ota_mark_app_invalid_rollback_and_reboot(); //设置为不合法
            }
        }
    }

}
