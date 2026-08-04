#include "bsp_ble.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "NimBLE_Demo"   //定义打印标签
static bool ble_adv_active =false;   //定义蓝牙广播
#define DEVICE_NAME "ESP32S3_NimBL"  //定义蓝牙名称

static uint16_t rx_value_hander;
static uint16_t tx_value_hander;

// 专门处理通讯阶段事件。特征列表回调函数， 手机写，或者手机读的时候会触发此回调函数
//ctxt->op：区分是读还是写（操作类型）,可能会有很多特征都有，attr_handle：区分操作的是哪一个特征（操作对象）

static int gatt_event_handle(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{   
    if(ctxt ->op == BLE_GATT_ACCESS_OP_WRITE_CHR)    //写特征
    {
        if(attr_handle == rx_value_hander)
        {
            if(ctxt ->om ->om_data[0] == 0x00)   //数据存放区域
            {
                gpio_set_level(GPIO_NUM_38,1);
                ESP_LOGI(TAG,"已收到0x00数据");
            }
            else if(ctxt ->om ->om_data[0] == 0x01)
            {
               gpio_set_level(GPIO_NUM_38,0); 
               ESP_LOGI(TAG,"已收到0x01数据");
            }
                

        }
    }
    else if(ctxt ->op == BLE_GATT_ACCESS_OP_READ_CHR)  //读特征
    {
        if(attr_handle == tx_value_hander)
        {
            const char *value = "hello";
            os_mbuf_append(ctxt ->om,value,strlen(value));   //调用官方函数 自动处理链表

        }
    }
    return 0;
}

// 专门处理蓝牙连接阶段。蓝牙连接成功，或断开的回调函数 

static int gap_event_handle(struct ble_gap_event *event, void *arg)
{
    if(event -> type == BLE_GAP_EVENT_CONNECT)
    {
        if(event -> connect.status == 0)
        {
            ESP_LOGI(TAG,"设备已连接.....");
            ble_adv_active =false;  //置标志位为flase
        }
        else
        {
            ESP_LOGI(TAG,"设备连接失败，重新广播");
            if(ble_adv_active == false)
            {
                start_advertising(); //开启广播
            }
        }
    }
    else if (event ->type == BLE_GAP_EVENT_DISCONNECT)
    {
        ESP_LOGI(TAG,"设备断开连接");
        if(ble_adv_active == false)
        {
            start_advertising(); //开启广播
        }

    }
    
    return 0;
}

// 特征服务结构体
const struct ble_gatt_svc_def gatt_svcs[]={
        {
           
           .type = BLE_GATT_SVC_TYPE_PRIMARY,   //主模式
           .uuid = BLE_UUID16_DECLARE(0x00FF),
        // .includes= ,不配置次级服务，无须此参数
           .characteristics = (struct ble_gatt_chr_def[])
           {
            //ESP32S3接收手机数据
            {
                .uuid = BLE_UUID16_DECLARE(0xFF01),
                .access_cb = gatt_event_handle,
                .flags = BLE_GATT_CHR_F_WRITE,  //手机写的时候
                .val_handle = &rx_value_hander, //句柄地址
                .arg = NULL,

            },
            //ESP32S3向手机发送数据
            {
                .uuid = BLE_UUID16_DECLARE(0xFF02),
                .access_cb = gatt_event_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, //手机读  
                .val_handle = &tx_value_hander,
                .arg = NULL,
            },
            {0} 
           }

        },
        {0}
    };





//开启蓝牙广播
void start_advertising(void)
{
    struct ble_hs_adv_fields fields={0};
    fields.name = (uint8_t *)DEVICE_NAME;   //要求要指向uint8_t的数，强转类型
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;   //是否完整的名字
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP ;  //持续广播状态 和 对外宣称只支持经典蓝牙
    fields.tx_pwr_lvl_is_present = 1;   //带发射功率信息
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO; //芯片自动配置发射功率
//1.设置广播内容
    int rc = ble_gap_adv_set_fields(&fields);   //配置广播内容
    if(rc != 0)
    {
        ESP_LOGI(TAG,"广播内容设置失败");
        return;
    }


    struct ble_gap_adv_params adv_params= {
        .conn_mode = BLE_GAP_CONN_MODE_UND,  //能不能被连接，NON 可发现不能连接，DIR 定向连接，UND 非定向可连接模式
        .disc_mode = BLE_GAP_DISC_MODE_GEN,  //什么时候被发现，广播信号发现模式，NON 不可被发现 ，LTD 有限的时间可被发现 GEN，一直可被发现        
        .itvl_min = BLE_GAP_ADV_ITVL_MS(200), //200ms
        .itvl_max = BLE_GAP_ADV_ITVL_MS(500), //500ms
    };

//1.地址选择 使用出厂地址;2.是否定向广播;3.无限广播时间;4.使用结构体进一步配置广播内容;5.连接成功或断开的回调函数,6.无给回调传参
//2.启动 BLE 广播    
rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC,NULL,BLE_HS_FOREVER,&adv_params,gap_event_handle,NULL);  
    if(rc != 0)
    {
        ESP_LOGI(TAG,"广播启动失败");
    }
    else 
    {
       ESP_LOGI(TAG,"广播已启动，等待连接.......");
       ble_adv_active = true ;

    }

}

//定义初始化完毕的回调函数
static void on_sync(void)
{
    ESP_LOGI(TAG,"设备初始化完毕，正在等待启动广播.....");
    //开启广播
    start_advertising();

    
}

//监听 任务函数
void host_task( void * arg )
{
    //实时监听蓝牙协议栈产生的事件
    nimble_port_run();
}

void ble_init(void)
{
   
   /*1. 初始化部分*/
    nimble_port_init();  //初始化蓝牙协议栈

    ble_svc_gap_init();  //初始化gap连接标准
    ble_svc_gatt_init(); //初始化gatt通讯标准

    ble_svc_gap_device_name_set(DEVICE_NAME);  //设置设备名称

    ble_gatts_count_cfg(gatt_svcs); //注册特征服务列表
    ble_gatts_add_svcs(gatt_svcs);

    /*初始化完毕，设置回调函数。初始化完成会自动调用此回调函数*/
    ble_hs_cfg.sync_cb = on_sync ;  //注册初始化完毕回调函数

    /*创建监听任务*/
    nimble_port_freertos_init(host_task);

}