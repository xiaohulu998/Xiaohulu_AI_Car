#include <stdio.h>
#include "audio.h"
#include "bsp_config.h"
#include <driver/gpio.h>


#include <sys/stat.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define TAG "AUDIO"

// 初始化音频配置
void start_audio(void)
{
    //初始化喇叭
    init_speaker(AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_WS, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_OUTPUT_SAMPLE_RATE);

    //初始化麦克风
    init_microphone(AUDIO_I2S_MIC_GPIO_BCLK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN, AUDIO_INPUT_SAMPLE_RATE);

}


// 开始录音
void start_record(uint32_t rec_time)
{
    int flash_wr_size = 0;
    //每次采样的数据长度（单位：字）
    const size_t read_size_word = 8192;
    //根据录音时间计算出需要录的长度（计算公式：采样率*录音时间 = 录音数据长度）
    const int flash_rec_time = AUDIO_INPUT_SAMPLE_RATE*rec_time;
    
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
            ESP_LOGI(TAG,"录音失败......\n");
        }
    }
    ESP_LOGI(TAG, "录音完成\n");
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