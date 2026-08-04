
#include <stdio.h>
#include "audio.h"
#include <driver/gpio.h>

#include "esp_spiffs.h"
#include <sys/stat.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 包含板级引脚配置（由选定的 board 提供的 config.h）
// 使用 __has_include 避免在静态分析时因文件不可用导致错误
#if defined(__has_include)
#  if __has_include("config.h")
#    include "BSP/BOARDS/config.h"
#  endif
#else
/* __has_include not available; rely on build system to provide config.h */
#endif
// 如果没有提供 BOARD 层的定义，提供默认值以便本文件在不同环境下也能编译


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

#define SDA  GPIO_NUM_10
#define SCL  GPIO_NUM_11





//spiffs文件系统挂载点
#define AUDIO_MOUNT     "/spiffs"

#define  TAG    "main"

//初始化spiffs文件系统
void audio_spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
      .base_path = AUDIO_MOUNT,     //挂载点
      .partition_label = "audio",  //指定spiffs分区，如果为NULL，则默认为分区表中第一个spiffs类型的分区
      .max_files = 2,               //最大可同时打开的文件数
      .format_if_mount_failed = true
    };

    //初始化和挂载spiffs分区
    esp_vfs_spiffs_register(&conf);
}

//开始录音
void start_record(uint32_t rec_time)
{
    int flash_wr_size = 0;
    //每次采样的数据长度（单位：字）
    const size_t read_size_word = 8192;
    //根据录音时间计算出需要录的长度（计算公式：采样率*录音时间 = 录音数据长度）
    const int flash_rec_time = SAMPLE_RATE*rec_time;
    
    //打开录音文件准备写入
    FILE *f = fopen(AUDIO_MOUNT"/record.pcm", "w");
    if (f == NULL) 
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    int16_t* i2s_read_buff = (int16_t*)malloc(read_size_word*sizeof(int16_t));
    ESP_LOGI(TAG,"Start record");
    //开始录音
    while (flash_wr_size < flash_rec_time) 
    {
        int read_word = audio_read(i2s_read_buff,read_size_word);
        if (read_word) 
        {
            //声音放大，可以去掉，放大是为了听得更清楚
            for(int i = 0;i<read_word;i++)
            {
                i2s_read_buff[i] = i2s_read_buff[i]<<1;
            }
            ESP_LOGI(TAG,"audio read word:%d",read_word);
            fwrite(i2s_read_buff, read_word*2, 1, f);
            flash_wr_size += read_word;
        } 
        else 
        {
            ESP_LOGI(TAG,"Read Failed!\n");
        }
    }
    ESP_LOGI(TAG, "Recording done!");
    fclose(f);
}

//开始播放声音
void start_play(void)
{
    const size_t write_size_byte = 8192;
    struct stat st;
    if(stat(AUDIO_MOUNT"/record.pcm",&st) == 0)
    {
        ESP_LOGI(TAG,"record.pcm filesize:%ld",st.st_size);
    }
    FILE *f = fopen(AUDIO_MOUNT"/record.pcm", "r");
    if(!f)
    {
        ESP_LOGI(TAG,"record.pcm open fail!");
        return;
    }
    ESP_LOGI(TAG,"Start play");
    uint8_t *i2s_write_buff = malloc(write_size_byte);
    if(!i2s_write_buff)
    {
        fclose(f);
        return;
    }
    size_t read_byte = 0;
    do
    {
        fread(i2s_write_buff,write_size_byte,1,f);
        audio_write((const int16_t*)i2s_write_buff,write_size_byte/2);
        read_byte += write_size_byte;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (read_byte < st.st_size);
    free(i2s_write_buff);
    fclose(f);
    ESP_LOGI(TAG,"Play done");
}


