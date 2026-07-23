#include "onenet_dm.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "led_ws2812.h"
#include <string.h>

// 定义LED IO口
#define LED_GPIO_NUM GPIO_NUM_15

static ws2812_strip_handle_t ws2812_handle = NULL;

// 保存led亮度
static int led_brightness = 0;
// 保存led开关
static int led_LightSwitch = 0;

// 保存WS2812的RGB值
static uint8_t ws2812_red = 0;
static uint8_t ws2812_green = 0;
static uint8_t ws2812_blue = 0;

/**
 * 物模型数据初始化
 * @param 无
 * @return 无
 */
void onenet_dm_init(void) {
  // 初始化ws2812
  ws2812_init(GPIO_NUM_38, 3, &ws2812_handle);

  // LED定时器初始化
  // 定时器参数结构体
  ledc_timer_config_t led_time = {
      .clk_cfg = LEDC_AUTO_CLK,
      .duty_resolution = LEDC_TIMER_12_BIT,
      .freq_hz = 5000,
      .timer_num = LEDC_TIMER_0,
  };
  // 初始化定时器
  ledc_timer_config(&led_time);

  // 配置 LEDC 通道（绑定 GPIO 与定时器）
  ledc_channel_config_t led_channel = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                       .channel = LEDC_CHANNEL_0,
                                       .timer_sel = LEDC_TIMER_0, // 关联上面的定时器0
                                       .gpio_num = LED_GPIO_NUM, // LED引脚
                                       .duty = 0, // 初始占空比0（熄灭）
                                       .hpoint = 0};

  // 通道初始化
  ledc_channel_config(&led_channel);
  // 安装渐变服务，参数0
  ledc_fade_func_install(0);
}

/**
 * 处理onenet下行的数据
 * @param property_js 包含下行数据的json
 * @return 无
 */
void onenet_property_handle(cJSON *property_js) {

  /*下行JSON列子
  {
    "id": "123",
    "version": "1.0",
    "params": {
          "Brightness":50,
          "LightSwitch":true,
          "RGBColor":{
              "Red":100,
              "Green":100,
              "Blue":100,
          }

    }
  }
  */
  //从property_js 这个JSON根对象中，查找键名为"params"的子节点
  cJSON *params_js = cJSON_GetObjectItem(property_js, "params");
  if (params_js) {
    cJSON *name_js = params_js->child; // 第一个子节点
    while (name_js) {
      if (strcmp(name_js->string, "Brightness") == 0) // 比较键名
      {
        // cJSON_GetNumberValue从一个cJSON的ITEM（键值对)中取出数值类型的值
        led_brightness = cJSON_GetNumberValue(name_js);
        int duty = led_brightness * 4095 / 100; // 计算ccr，占空比值
        ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, 0);

      } else if (strcmp(name_js->string, "LightSwitch") == 0) // 比较键名
      {
        if (cJSON_IsTrue(name_js)) // 判断开关是否打开
        {
          led_LightSwitch = 1;
          led_brightness = 50;
          int duty = 50 * 4095 / 100;
          ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty,
                                   0);
        } else // 关灯
        {
          ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0, 0);
          led_LightSwitch = 0;
          led_brightness = 0;
        }
      } else if (strcmp(name_js->string, "RGBColor") == 0) // 比较键名
      {
        // 取出键名为Red、green、blue的值
        ws2812_red = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Red"));
        ws2812_green =
            cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Green"));
        ws2812_blue =
            cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Blue"));
        // 写入RBG值，每个灯都一样
        for (int i = 0; i < 3; i++) {
          ws2812_write(ws2812_handle, i, ws2812_red, ws2812_green, ws2812_blue);
        }
      }

      // 循环重要点
      name_js = name_js->next; // 指针移动，指向同级下一个 cJSON 节点
    }
  }
}

/**
 * 生成上报所有数据的cJSON对象
 * @param 无
 * @return cJSON对象，包含所有属性值
 */
cJSON *onenet_property_upload_dm(void) {

  /* //参考JSON
  {
"id": "123",
"version": "1.0",
"params": {
  "Brightness": {
    "value": 50,  //led_brightness
    },
  "LightSwitch": {
    "value": ture, //led_LightSwitch
    }
  "RGBColor":{
      "value":{
          "red":100,
          "green":100,
          "blue":100,
       }
  }
  */
  cJSON *root = cJSON_CreateObject();                        // 根节点
  cJSON_AddStringToObject(root, "id", "123");                // 子节点
  cJSON_AddStringToObject(root, "version", "1.0");           // 子节点
  cJSON *params_js = cJSON_AddObjectToObject(root, "params"); // 子节点  //已修复，这里是Object写成了 Array 

  // 往params中填充灯亮度值
  cJSON *Brightness_js = cJSON_AddObjectToObject(params_js, "Brightness");
  cJSON_AddNumberToObject(Brightness_js, "value", led_brightness);

  // 往params中填充灯开关值
  cJSON *LightSwitch_js = cJSON_AddObjectToObject(params_js, "LightSwitch");
  cJSON_AddBoolToObject(LightSwitch_js, "value", led_LightSwitch);

  // 往params中填充RGB值
  cJSON *RGBColor_js = cJSON_AddObjectToObject(params_js, "RGBColor");

  cJSON *RGBColor_value_js = cJSON_AddObjectToObject(RGBColor_js, "value");
  cJSON_AddNumberToObject(RGBColor_value_js, "Red", ws2812_red);
  cJSON_AddNumberToObject(RGBColor_value_js, "Green", ws2812_green);
  cJSON_AddNumberToObject(RGBColor_value_js, "Blue", ws2812_blue);

  return root;
}