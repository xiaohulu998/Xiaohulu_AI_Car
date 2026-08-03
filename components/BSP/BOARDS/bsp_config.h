#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>


//I2S 主时钟
#define I2S_BCLK    GPIO_NUM_46
//I2S 声道选择线
#define I2S_WS      GPIO_NUM_9
//I2S 数据线
#define I2S_SDOUT   GPIO_NUM_8
//功放芯片使能引脚对应XL9555的IO0_0
#define SPK_EN_IO   IO0_0
//PDM麦克风的CLK
#define PDM_CLK     GPIO_NUM_3
//PDM麦克风数据引脚
#define PDM_DATA    GPIO_NUM_42
//采样率
#define SAMPLE_RATE     24000


/* 音频相关引脚定义 */
// 采样率
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 麦克风引脚
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
// 喇叭引脚
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_WS   GPIO_NUM_16


/* 灯相关引脚定义 */
#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

#define TOUCH_BUTTON_GPIO       GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39



/* 显示屏相关引脚定义 */
#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42

#define DISPLAY_WIDTH   128


#endif
