#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s_pdm.h"

static const char* TAG = "AUDIO_RECORD";

//发送句柄和接收句柄
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;



/** 初始化喇叭
 * @param bclk 时钟GPIO
 * @param ws 声道线GPIO
 * @param sd 数据GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_speaker(gpio_num_t bclk,gpio_num_t ws,gpio_num_t sd,uint32_t sample_rate)
{   
    //i2s总线配置结构体
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);  //默认配置， I2S_ROLE_MASTER设置为主模式
    chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));  //初始化api，返回发送句柄，底层会自动分配一块 DMA 内存，自动搬运

    //i2s参数配置结构体
    i2s_std_config_t i2s_tx_cfg ={
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),   // 时钟初始化 默认配置
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),  //声道 默认配置，采样位深，单通道
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   //外部设备作为主时钟才需要使用  设置为-1
            .bclk = bclk,  //位时钟 gpio口
            .ws = ws,       //声道帧时钟gpio口
            .dout = sd,     //数据gpio口
            .din = I2S_GPIO_UNUSED,    //输入引脚不使用
            
            //是否取反，备用
            .invert_flags = {          
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    i2s_tx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;  //手动切换到左声道
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &i2s_tx_cfg));  //设置配置
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));  //使能
    ESP_LOGI(TAG, "喇叭配置成功......");
}


//无PDM麦克风，不使用

/** 初始化PDM麦克风
 * @param dat 数据GPIO
 * @param clk 时钟GPIO
 * @param sample_rate 采样率
 * @return 无
 */
 /*
void init_pdm_microphone(gpio_num_t dat,gpio_num_t clk,uint32_t sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);   //I2S_NUM_0可以用PDM接口传输
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
        //默认单声道插槽为左侧插槽（PDM 麦克风的 “选择引脚” 处于下拉状态）
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = clk,
            .din = dat,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    pdm_rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}*/

/** 初始化麦克风
 * @param bclk 时钟GPIO
 * @param ws 声道线GPIO
 * @param sd 数据GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_microphone(gpio_num_t bclk,gpio_num_t ws,gpio_num_t sd,uint32_t sample_rate)
{
    //i2s总线配置结构体
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);  //默认配置， I2S_ROLE_MASTER设置为主模式
    
    //仅对 tx 有效
    //chan_cfg.auto_clear_after_cb = true;  //回调结束之后再清空缓冲区
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &rx_handle, NULL));  //初始化api，返回接收句柄

    //i2s参数配置结构体
    i2s_std_config_t i2s_rx_cfg ={
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),   // 时钟初始化 默认配置
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),  //声道 默认配置，采样位深，单通道
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   //外部设备作为主时钟才需要使用  设置为-1
            .bclk = bclk,  //位时钟 gpio口
            .ws = ws,       //声道帧时钟gpio口
            .dout = I2S_GPIO_UNUSED,  //输出引脚不使用   
            .din = sd,    //数据gpio口
       
        
        //是否取反，备用
        .invert_flags = {          
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        
        }    
    };
    //INMP441 L/R 引脚接 GND → 数据输出在声道
    i2s_rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;  //手动切换到左声道
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &i2s_rx_cfg));  //设置配置
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));  //使能
}


/** 音频输出
 * @param data 16位pcm数据
 * @param samples 写入的数据长度，单位（字）
 * @return 实际写入的数据长度，单位（字）
 */
int audio_write(const int16_t* data, int samples)
{
    size_t bytes_write;
    i2s_channel_write(tx_handle,data,samples*2,&bytes_write,200);
    bytes_write >>= 1; //右移一位高位补0，等同于除以2
    return bytes_write;
}


/** 录音读取
 * @param data 16位pcm数据
 * @param samples 要求读取的数据长度，单位（字）  一个样本16bit 2个字节 samples数据长度为样本个数，总内存samples*2 
 * @return 实际读取的数据长度，单位（字）
 */
int audio_read(int16_t* data, int samples)
{
    size_t bytes_read;
    i2s_channel_read(rx_handle, data, samples*2, &bytes_read, 2000);   //句柄，数据，要求读取的数据长度，实际长度，超时时间
    bytes_read >>=1;   //右移一位高位补0，等同于除以2
    return bytes_read;
}
