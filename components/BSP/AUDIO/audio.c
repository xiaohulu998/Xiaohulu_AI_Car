#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s_pdm.h"

static const char* TAG = "AUDIO_RECORD";

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;



/** 初始化喇叭
 * @param bclk 时钟GPIO
 * @param ws 声道线GPIO
 * @param dout 数据GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_speaker(gpio_num_t bclk,gpio_num_t ws,gpio_num_t dout,uint32_t sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);  //默认欧洲
    chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));
    i2s_std_config_t i2s_rx_cfg ={
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    i2s_rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &i2s_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

/** 初始化PDM喇叭
 * @param dat 数据GPIO
 * @param clk 时钟GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_pdm_microphone(gpio_num_t dat,gpio_num_t clk,uint32_t sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
        /* The default mono slot is the left slot (whose 'select pin' of the PDM microphone is pulled down) */
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
    bytes_write >>= 1;
    return bytes_write;
}

/** 录音读取
 * @param data 16位pcm数据
 * @param samples 要求读取的数据长度，单位（字）
 * @return 实际读取的数据长度，单位（字）
 */
int audio_read(int16_t* dest, int samples)
{
    size_t bytes_read;
    i2s_channel_read(rx_handle, dest, samples*2, &bytes_read, 2000);
    bytes_read >>=1;
    return bytes_read;
}
