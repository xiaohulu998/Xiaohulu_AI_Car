/**
* 全局唯一硬件引脚宏定义
 */

#ifndef _BOARD_DEF_H_
#define _BOARD_DEF_H_

#include "driver/gpio.h"

/* ================================================================
 *  音频 I2S 引脚
 * ================================================================ */

/* 音频采样率 */
#define AUDIO_INPUT_SAMPLE_RATE   16000
#define AUDIO_OUTPUT_SAMPLE_RATE  24000

 /* 麦克风 (I2S 标准模式输入) */
#define AUDIO_I2S_MIC_GPIO_WS    GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK   GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN   GPIO_NUM_6

/* 喇叭 (I2S 标准模式输出) */
#define AUDIO_I2S_SPK_GPIO_DOUT  GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK  GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK  GPIO_NUM_16



/* ================================================================
 *  LED / WS2812 引脚
 * ================================================================ */

 /* 板载 LED (PWM 调光) */
#define BUILTIN_LED_GPIO         GPIO_NUM_48

/* WS2812 灯带 (RMT 驱动) */
#define WS2812_GPIO              GPIO_NUM_38
#define WS2812_NUM_LEDS          3

/* ================================================================
 *  按键引脚
 * ================================================================ */


/* ================================================================
 *  显示屏 I2C 引脚
 * ================================================================ */
#define DISPLAY_SDA_PIN          GPIO_NUM_41
#define DISPLAY_SCL_PIN          GPIO_NUM_42
#define DISPLAY_WIDTH            128   

/* ================================================================
 *  通用 I2C 引脚
 * ================================================================ */


#endif