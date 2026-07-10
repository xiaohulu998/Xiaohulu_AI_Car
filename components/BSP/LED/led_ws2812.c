/**
 * @file    led_ws2812.c
 * @brief   WS2812B 硬件底层驱动实现
 * @author  xiaohulu
 * @date    2026-07-10
 *
 * 方案：ESP32-S3 RMT 外设 → 精确脉冲编码 → WS2812B 单总线
 * 效果引擎：独立 FreeRTOS 任务驱动，异步非阻塞，线程安全。
 */

#include "led_ws2812.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_random.h"

static const char *TAG = "ws2812";

/* ================================================================
 *  WS2812B 时序常量（RMT 80MHz 时钟 → 1 tick = 12.5ns）
 * ================================================================ */
#define RMT_CLOCK_HZ            (80000000UL)    /* RMT 源时钟 80MHz */
#define RMT_CLOCK_DIV           1               /* 不分频 → 12.5ns/tick */

/* WS2812B 时序 (单位: RMT ticks @ 12.5ns) */
#define T0H_TICKS   32      /* 0 码高电平 0.40us */
#define T0L_TICKS   68      /* 0 码低电平 0.85us → 1 bit = 1.25us */
#define T1H_TICKS   64      /* 1 码高电平 0.80us */
#define T1L_TICKS   36      /* 1 码低电平 0.45us → 1 bit = 1.25us */

/* 复位信号: 低电平 > 50us，这里取 300us 保证余量 */
#define RESET_TICKS 2000    /* 300us @ 12.5ns（取整） */

/* 预编码查找表大小 */
#define RMT_SYMBOLS_PER_LED     (24)    /* 8bit×3ch = 24 RMT symbols */
#define MAX_RMT_SYMBOLS         (WS2812_LED_NUM * RMT_SYMBOLS_PER_LED + 2) /* +2: reset code */

/* 效果引擎栈大小 & 优先级 */
#define EFFECT_TASK_STACK_SIZE  4096
#define EFFECT_TASK_PRIORITY    5       /* 中等优先级，不阻塞关键任务 */

/* ================================================================
 *  内部数据结构
 * ================================================================ */

/* 效果上下文（每个效果函数返回下一步要 sleep 的 ms，0=立即重跑，<0=效果结束） */
typedef int32_t (*effect_step_fn_t)(const ws2812_effect_cfg_t *cfg);

typedef struct {
    gpio_num_t        gpio;             /* DIN 引脚 */
    uint8_t           led_num;          /* 实际灯珠数 */
    uint8_t           brightness;       /* 全局亮度 0~100 */

    /* LED 颜色缓冲区（GRB） */
    ws2812_color_t    buffer[WS2812_LED_NUM];

    /* RMT 资源 */
    rmt_channel_handle_t   rmt_chan;
    rmt_encoder_handle_t   copy_encoder;
    rmt_symbol_word_t      rmt_symbols[MAX_RMT_SYMBOLS];

    /* 效果引擎状态 */
    TaskHandle_t       task_handle;
    SemaphoreHandle_t  mutex;           /* 保护 buffer / 状态 */
    ws2812_effect_t    current_effect;
    ws2812_effect_cfg_t cfg;
    volatile bool      running;
    volatile bool      stop_request;

    /* 效果内部计数器（步数、相位等） */
    uint32_t           step_count;
    void              *effect_state;    /* 效果私有状态 */
} ws2812_ctx_t;

static ws2812_ctx_t g_ctx = {0};
static bool g_initialized = false;

/* ================================================================
 *  前向声明
 * ================================================================ */
static void ws2812_encode_buffer(void);
static void ws2812_effect_task(void *arg);
static void ws2812_effect_init_state(void);

static int32_t effect_step_static(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_breathing(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_blink(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_flow(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_rainbow(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_warning(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_marquee(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_dual_blink(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_color_wipe(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_theater(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_comet(const ws2812_effect_cfg_t *cfg);
static int32_t effect_step_fire(const ws2812_effect_cfg_t *cfg);

static const effect_step_fn_t effect_table[WS2812_EFFECT_COUNT] = {
    [WS2812_EFFECT_STATIC]        = effect_step_static,
    [WS2812_EFFECT_BREATHING]     = effect_step_breathing,
    [WS2812_EFFECT_BLINK]         = effect_step_blink,
    [WS2812_EFFECT_FLOW]          = effect_step_flow,
    [WS2812_EFFECT_RAINBOW]       = effect_step_rainbow,
    [WS2812_EFFECT_WARNING]       = effect_step_warning,
    [WS2812_EFFECT_MARQUEE]       = effect_step_marquee,
    [WS2812_EFFECT_DUAL_BLINK]    = effect_step_dual_blink,
    [WS2812_EFFECT_COLOR_WIPE]    = effect_step_color_wipe,
    [WS2812_EFFECT_THEATER_CHASE] = effect_step_theater,
    [WS2812_EFFECT_COMET]         = effect_step_comet,
    [WS2812_EFFECT_FIRE]          = effect_step_fire,
};

/* ================================================================
 *  HSV → RGB 转换（用于彩虹效果）
 * ================================================================ */
static ws2812_color_t hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v)
{
    /* h: 0~360, s: 0~255, v: 0~255 */
    ws2812_color_t c = {0, 0, 0};
    if (s == 0) {
        c.r = c.g = c.b = v;
        return c;
    }
    uint16_t region = h / 60;
    uint16_t remainder = (h % 60) * 255 / 60;
    uint8_t p = (uint8_t)(((uint16_t)v * (255 - s)) / 255);
    uint8_t q = (uint8_t)(((uint16_t)v * (255 - ((uint16_t)s * remainder) / 255)) / 255);
    uint8_t t = (uint8_t)(((uint16_t)v * (255 - ((uint16_t)s * (255 - remainder)) / 255)) / 255);

    switch (region) {
    case 0:  c.r = v; c.g = t; c.b = p; break;
    case 1:  c.r = q; c.g = v; c.b = p; break;
    case 2:  c.r = p; c.g = v; c.b = t; break;
    case 3:  c.r = p; c.g = q; c.b = v; break;
    case 4:  c.r = t; c.g = p; c.b = v; break;
    default: c.r = v; c.g = p; c.b = q; break;
    }
    return c;
}

/* ================================================================
 *  RMT 编码：将 LED buffer 转为 RMT 符号序列
 * ================================================================ */
static void ws2812_encode_buffer(void)
{
    ws2812_ctx_t *ctx = &g_ctx;
    uint8_t brightness = ctx->brightness;
    rmt_symbol_word_t *sym = ctx->rmt_symbols;

    for (uint8_t i = 0; i < ctx->led_num; i++) {
        ws2812_color_t c = ctx->buffer[i];

        /* 应用全局亮度 */
        if (brightness < 100) {
            c.r = (uint8_t)((uint16_t)c.r * brightness / 100);
            c.g = (uint8_t)((uint16_t)c.g * brightness / 100);
            c.b = (uint8_t)((uint16_t)c.b * brightness / 100);
        }

        /* WS2812B 数据顺序: G → R → B，MSB first */
        uint8_t bytes[3] = { c.g, c.r, c.b };

        for (int b = 0; b < 3; b++) {
            for (int bit = 7; bit >= 0; bit--) {
                if (bytes[b] & (1 << bit)) {
                    /* 1 码 */
                    sym->duration0 = T1H_TICKS;
                    sym->level0    = 1;
                    sym->duration1 = T1L_TICKS;
                    sym->level1    = 0;
                } else {
                    /* 0 码 */
                    sym->duration0 = T0H_TICKS;
                    sym->level0    = 1;
                    sym->duration1 = T0L_TICKS;
                    sym->level1    = 0;
                }
                sym++;
            }
        }
    }

    /* 复位信号：超过 50us 的低电平 */
    sym->duration0 = RESET_TICKS;
    sym->level0    = 0;
    sym->duration1 = 0;
    sym->level1    = 0;
}

/* ================================================================
 *  RMT 发送
 * ================================================================ */
static esp_err_t ws2812_rmt_transmit(void)
{
    ws2812_encode_buffer();

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags.eot_level = 0,  /* 发送完毕后拉低 */
    };

    uint32_t symbol_count = g_ctx.led_num * RMT_SYMBOLS_PER_LED + 1; /* +1: reset */
    return rmt_transmit(g_ctx.rmt_chan, g_ctx.copy_encoder,
                        g_ctx.rmt_symbols, symbol_count * sizeof(rmt_symbol_word_t),
                        &tx_cfg);
}

/* ================================================================
 *  默认配置填充
 * ================================================================ */
static void ws2812_fill_default_cfg(ws2812_effect_t effect, ws2812_effect_cfg_t *cfg)
{
    if (cfg == NULL) return; /* should not happen */

    switch (effect) {
    case WS2812_EFFECT_STATIC:
        if (cfg->static_cfg.color.g == 0 && cfg->static_cfg.color.r == 0 &&
            cfg->static_cfg.color.b == 0)
            cfg->static_cfg.color = WS2812_COLOR_BLUE;
        break;
    case WS2812_EFFECT_BREATHING:
        if (cfg->breathing_cfg.period_ms == 0)        cfg->breathing_cfg.period_ms = 2000;
        if (cfg->breathing_cfg.min_brightness == 0 && cfg->breathing_cfg.max_brightness == 0) {
            cfg->breathing_cfg.min_brightness = 5;
            cfg->breathing_cfg.max_brightness = 100;
        }
        if (cfg->breathing_cfg.color.g == 0 && cfg->breathing_cfg.color.r == 0 &&
            cfg->breathing_cfg.color.b == 0)
            cfg->breathing_cfg.color = WS2812_COLOR_BLUE;
        break;
    case WS2812_EFFECT_BLINK:
        if (cfg->blink_cfg.on_ms == 0)    cfg->blink_cfg.on_ms = 500;
        if (cfg->blink_cfg.off_ms == 0)   cfg->blink_cfg.off_ms = 500;
        if (cfg->blink_cfg.count == 0)    cfg->blink_cfg.count = -1; /* 无限 */
        if (cfg->blink_cfg.color.g == 0 && cfg->blink_cfg.color.r == 0 &&
            cfg->blink_cfg.color.b == 0)
            cfg->blink_cfg.color = WS2812_COLOR_RED;
        break;
    case WS2812_EFFECT_FLOW:
        if (cfg->flow_cfg.speed_ms == 0)   cfg->flow_cfg.speed_ms = 100;
        if (cfg->flow_cfg.width == 0)      cfg->flow_cfg.width = 1;
        cfg->flow_cfg.loop = true; /* 默认循环 */
        if (cfg->flow_cfg.color.g == 0 && cfg->flow_cfg.color.r == 0 &&
            cfg->flow_cfg.color.b == 0)
            cfg->flow_cfg.color = WS2812_COLOR_CYAN;
        break;
    case WS2812_EFFECT_RAINBOW:
        if (cfg->rainbow_cfg.speed_ms == 0)   cfg->rainbow_cfg.speed_ms = 30;
        if (cfg->rainbow_cfg.brightness == 0) cfg->rainbow_cfg.brightness = 80;
        break;
    case WS2812_EFFECT_WARNING:
        if (cfg->warning_cfg.interval_ms == 0)  cfg->warning_cfg.interval_ms = 200;
        if (cfg->warning_cfg.duty_on_pct == 0)  cfg->warning_cfg.duty_on_pct = 30;
        if (cfg->warning_cfg.color.g == 0 && cfg->warning_cfg.color.r == 0 &&
            cfg->warning_cfg.color.b == 0)
            cfg->warning_cfg.color = WS2812_COLOR_RED;
        break;
    case WS2812_EFFECT_MARQUEE:
        if (cfg->marquee_cfg.speed_ms == 0)     cfg->marquee_cfg.speed_ms = 80;
        if (cfg->marquee_cfg.tail_length == 0)  cfg->marquee_cfg.tail_length = 3;
        if (cfg->marquee_cfg.color.g == 0 && cfg->marquee_cfg.color.r == 0 &&
            cfg->marquee_cfg.color.b == 0)
            cfg->marquee_cfg.color = WS2812_COLOR_GREEN;
        break;
    case WS2812_EFFECT_DUAL_BLINK:
        if (cfg->dual_blink_cfg.interval_ms == 0) cfg->dual_blink_cfg.interval_ms = 500;
        if (cfg->dual_blink_cfg.group_a_mask == 0) cfg->dual_blink_cfg.group_a_mask = 0x15; /* bit0,2,4 */
        if (cfg->dual_blink_cfg.group_b_mask == 0) cfg->dual_blink_cfg.group_b_mask = 0x2A; /* bit1,3,5 */
        if (cfg->dual_blink_cfg.color_a.g == 0 && cfg->dual_blink_cfg.color_a.r == 0 &&
            cfg->dual_blink_cfg.color_a.b == 0)
            cfg->dual_blink_cfg.color_a = WS2812_COLOR_RED;
        if (cfg->dual_blink_cfg.color_b.g == 0 && cfg->dual_blink_cfg.color_b.r == 0 &&
            cfg->dual_blink_cfg.color_b.b == 0)
            cfg->dual_blink_cfg.color_b = WS2812_COLOR_BLUE;
        break;
    case WS2812_EFFECT_COLOR_WIPE:
        if (cfg->color_wipe_cfg.interval_ms == 0) cfg->color_wipe_cfg.interval_ms = 100;
        if (cfg->color_wipe_cfg.color.g == 0 && cfg->color_wipe_cfg.color.r == 0 &&
            cfg->color_wipe_cfg.color.b == 0)
            cfg->color_wipe_cfg.color = WS2812_COLOR_GREEN;
        break;
    case WS2812_EFFECT_THEATER_CHASE:
        if (cfg->theater_cfg.interval_ms == 0) cfg->theater_cfg.interval_ms = 120;
        if (cfg->theater_cfg.width == 0)       cfg->theater_cfg.width = 1;
        if (cfg->theater_cfg.color.g == 0 && cfg->theater_cfg.color.r == 0 &&
            cfg->theater_cfg.color.b == 0)
            cfg->theater_cfg.color = WS2812_COLOR_WHITE;
        break;
    case WS2812_EFFECT_COMET:
        if (cfg->comet_cfg.speed_ms == 0)      cfg->comet_cfg.speed_ms = 70;
        if (cfg->comet_cfg.tail_length == 0)   cfg->comet_cfg.tail_length = 4;
        if (cfg->comet_cfg.head_color.g == 0 && cfg->comet_cfg.head_color.r == 0 &&
            cfg->comet_cfg.head_color.b == 0)
            cfg->comet_cfg.head_color = WS2812_COLOR_CYAN;
        break;
    case WS2812_EFFECT_FIRE:
        if (cfg->fire_cfg.speed_ms == 0)       cfg->fire_cfg.speed_ms = 40;
        if (cfg->fire_cfg.intensity == 0)      cfg->fire_cfg.intensity = 80;
        break;
    default:
        break;
    }
}

/* ================================================================
 *  效果引擎状态初始化
 * ================================================================ */
static void ws2812_effect_init_state(void)
{
    g_ctx.step_count = 0;
    if (g_ctx.effect_state) {
        free(g_ctx.effect_state);
        g_ctx.effect_state = NULL;
    }
}

/* ================================================================
 *  效果引擎任务
 * ================================================================ */
static void ws2812_effect_task(void *arg)
{
    ws2812_ctx_t *ctx = &g_ctx;
    int32_t delay_ms;

    while (1) {
        /* 等待启动信号 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ctx->running = true;
        ctx->stop_request = false;
        ws2812_effect_init_state();

        ESP_LOGI(TAG, "Effect %d started", (int)ctx->current_effect);

        while (ctx->running && !ctx->stop_request) {
            effect_step_fn_t step_fn = effect_table[ctx->current_effect];
            if (!step_fn) {
                ESP_LOGW(TAG, "No step function for effect %d", (int)ctx->current_effect);
                break;
            }

            delay_ms = step_fn(&ctx->cfg);

            if (delay_ms < 0) {
                /* 效果结束 */
                ESP_LOGI(TAG, "Effect %d finished", (int)ctx->current_effect);
                break;
            }

            ctx->step_count++;

            if (delay_ms > 0) {
                /* 等待期间检查是否需要停止 */
                TickType_t ticks = pdMS_TO_TICKS(delay_ms);
                if (ticks == 0) ticks = 1;
                /* 使用带超时的等待，以便能及时响应停止请求 */
                uint32_t notified = ulTaskNotifyTake(pdTRUE, ticks);
                if (notified > 0) {
                    /* 收到新的启动信号，退出当前效果 */
                    break;
                }
            }
        }

        if (ctx->stop_request) {
            ESP_LOGI(TAG, "Effect stopped by request");
        }

        ctx->running = false;
        if (g_ctx.effect_state) {
            free(g_ctx.effect_state);
            g_ctx.effect_state = NULL;
        }
    }
}

/* ================================================================
 *  Public API
 * ================================================================ */

esp_err_t ws2812_init(gpio_num_t gpio_num, uint8_t led_num)
{
    if (g_initialized) {
        ESP_LOGW(TAG, "Already initialized, deinit first");
        ws2812_deinit();
    }

    if (led_num == 0 || led_num > WS2812_LED_NUM) {
        ESP_LOGE(TAG, "Invalid led_num: %d (max %d)", led_num, WS2812_LED_NUM);
        return ESP_ERR_INVALID_ARG;
    }

    ws2812_ctx_t *ctx = &g_ctx;
    memset(ctx, 0, sizeof(*ctx));
    ctx->gpio = gpio_num;
    ctx->led_num = led_num;
    ctx->brightness = 100;
    ctx->current_effect = WS2812_EFFECT_NONE;

    /* ---- 初始化 RMT ---- */
    rmt_tx_channel_config_t tx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,     /* 使用默认时钟源（APB 80MHz） */
        .gpio_num = gpio_num,
        .mem_block_symbols = 64,
        .resolution_hz = 80000000,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
        .intr_priority = 0,                 /* 自动分配 */
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_cfg, &ctx->rmt_chan), TAG, "rmt_new_tx_channel");

    /* 使能 RMT 通道 */
    ESP_RETURN_ON_ERROR(rmt_enable(ctx->rmt_chan), TAG, "rmt_enable");

    /* 拷贝编码器：直接把 RMT 符号原样发出 */
    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &ctx->copy_encoder), TAG, "rmt_new_copy_encoder");

    /* ---- 初始化 GPIO（设为推挽输出，初始化时拉低） ---- */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);
    gpio_set_level(gpio_num, 0);

    /* ---- 创建互斥锁 ---- */
    ctx->mutex = xSemaphoreCreateMutex();
    if (!ctx->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        rmt_disable(ctx->rmt_chan);
        rmt_del_channel(ctx->rmt_chan);
        return ESP_ERR_NO_MEM;
    }

    /* ---- 创建效果引擎任务 ---- */
    BaseType_t ret = xTaskCreate(ws2812_effect_task, "ws2812_effect",
                                 EFFECT_TASK_STACK_SIZE, NULL,
                                 EFFECT_TASK_PRIORITY, &ctx->task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create effect task");
        vSemaphoreDelete(ctx->mutex);
        rmt_disable(ctx->rmt_chan);
        rmt_del_channel(ctx->rmt_chan);
        return ESP_ERR_NO_MEM;
    }

    /* 初始状态：全灭 */
    ws2812_clear_all();

    g_initialized = true;
    ESP_LOGI(TAG, "Initialized on GPIO%d, %d LEDs", (int)gpio_num, (int)led_num);
    return ESP_OK;
}

void ws2812_deinit(void)
{
    if (!g_initialized) return;

    ws2812_ctx_t *ctx = &g_ctx;

    /* 停止效果并清理 */
    ws2812_effect_stop_and_clear();

    /* 删除任务 */
    if (ctx->task_handle) {
        vTaskDelete(ctx->task_handle);
        ctx->task_handle = NULL;
    }

    /* 释放 RMT */
    if (ctx->copy_encoder) {
        rmt_del_encoder(ctx->copy_encoder);
        ctx->copy_encoder = NULL;
    }
    if (ctx->rmt_chan) {
        rmt_disable(ctx->rmt_chan);
        rmt_del_channel(ctx->rmt_chan);
        ctx->rmt_chan = NULL;
    }

    /* 释放互斥锁 */
    if (ctx->mutex) {
        vSemaphoreDelete(ctx->mutex);
        ctx->mutex = NULL;
    }

    /* 释放效果私有状态 */
    if (ctx->effect_state) {
        free(ctx->effect_state);
        ctx->effect_state = NULL;
    }

    gpio_set_level(ctx->gpio, 0);
    memset(ctx, 0, sizeof(*ctx));
    g_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

/* ---------- 底层直接控制 ---------- */

void ws2812_set_all(ws2812_color_t color)
{
    if (!g_initialized) return;
    ws2812_ctx_t *ctx = &g_ctx;

    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    for (uint8_t i = 0; i < ctx->led_num; i++) {
        ctx->buffer[i] = color;
    }
    xSemaphoreGive(ctx->mutex);

    ws2812_rmt_transmit();
}

void ws2812_set_led(uint8_t index, ws2812_color_t color)
{
    if (!g_initialized || index >= g_ctx.led_num) return;
    ws2812_ctx_t *ctx = &g_ctx;

    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    ctx->buffer[index] = color;
    xSemaphoreGive(ctx->mutex);
}

void ws2812_set_range(uint8_t start, uint8_t end, ws2812_color_t color)
{
    if (!g_initialized) return;
    ws2812_ctx_t *ctx = &g_ctx;

    if (start >= ctx->led_num) start = 0;
    if (end >= ctx->led_num) end = ctx->led_num - 1;
    if (start > end) { uint8_t t = start; start = end; end = t; }

    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    for (uint8_t i = start; i <= end; i++) {
        ctx->buffer[i] = color;
    }
    xSemaphoreGive(ctx->mutex);
}

void ws2812_clear_all(void)
{
    ws2812_set_all(WS2812_COLOR_BLACK);
}

void ws2812_flush(void)
{
    if (!g_initialized) return;
    ws2812_rmt_transmit();
}

ws2812_color_t ws2812_get_led_color(uint8_t index)
{
    ws2812_color_t c = {0, 0, 0};
    if (!g_initialized || index >= g_ctx.led_num) return c;

    xSemaphoreTake(g_ctx.mutex, portMAX_DELAY);
    c = g_ctx.buffer[index];
    xSemaphoreGive(g_ctx.mutex);
    return c;
}

/* ---------- 效果引擎 ---------- */

esp_err_t ws2812_effect_start(ws2812_effect_t effect, const ws2812_effect_cfg_t *cfg)
{
    if (!g_initialized) return ESP_ERR_INVALID_STATE;
    if (effect <= WS2812_EFFECT_NONE || effect >= WS2812_EFFECT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    ws2812_ctx_t *ctx = &g_ctx;

    /* 先请求停止当前效果 */
    ctx->stop_request = true;

    /* 等待当前效果结束 */
    while (ctx->running) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* 设置新效果 */
    if (cfg) {
        memcpy(&ctx->cfg, cfg, sizeof(ws2812_effect_cfg_t));
    } else {
        memset(&ctx->cfg, 0, sizeof(ws2812_effect_cfg_t));
    }
    ws2812_fill_default_cfg(effect, &ctx->cfg);
    ctx->current_effect = effect;

    /* 通知效果任务启动 */
    xTaskNotifyGive(ctx->task_handle);

    return ESP_OK;
}

void ws2812_effect_stop(void)
{
    if (!g_initialized) return;
    g_ctx.stop_request = true;
}

void ws2812_effect_stop_and_clear(void)
{
    ws2812_effect_stop();
    /* 等待停止完成 */
    while (g_ctx.running) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ws2812_clear_all();
}

ws2812_effect_t ws2812_effect_get_current(void)
{
    if (!g_initialized) return WS2812_EFFECT_NONE;
    return g_ctx.current_effect;
}

bool ws2812_effect_is_idle(void)
{
    return !g_ctx.running;
}

/* ---------- 全局亮度 ---------- */

void ws2812_set_global_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    g_ctx.brightness = percent;
}

uint8_t ws2812_get_global_brightness(void)
{
    return g_ctx.brightness;
}

/* ================================================================
 *  效果实现
 * ================================================================ */

/* ---- 静态常亮 ---- */
static int32_t effect_step_static(const ws2812_effect_cfg_t *cfg)
{
    for (uint8_t i = 0; i < g_ctx.led_num; i++) {
        g_ctx.buffer[i] = cfg->static_cfg.color;
    }
    ws2812_rmt_transmit();
    return -1; /* 一次性效果，立即结束 */
}

/* ---- 呼吸灯：正弦波控制亮度 ---- */
static int32_t effect_step_breathing(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_breathing_cfg_t *bc = &cfg->breathing_cfg;
    uint32_t step = g_ctx.step_count;
    uint32_t steps_per_cycle = bc->period_ms / 20; /* 每 20ms 一帧 */
    if (steps_per_cycle == 0) steps_per_cycle = 100;

    /* 正弦波 0~PI，输出 0.0~1.0 */
    float phase = (float)(step % steps_per_cycle) / (float)steps_per_cycle;
    float sin_val = sinf(M_PI * phase); /* sin(0)~sin(PI) = 0→1→0 */
    uint8_t brightness = (uint8_t)(bc->min_brightness +
                         (bc->max_brightness - bc->min_brightness) * sin_val);

    for (uint8_t i = 0; i < g_ctx.led_num; i++) {
        g_ctx.buffer[i] = ws2812_color_dim(bc->color, brightness);
    }
    ws2812_rmt_transmit();
    return 20; /* 20ms 帧间隔 */
}

/* ---- 闪烁 ---- */
static int32_t effect_step_blink(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_blink_cfg_t *bc = &cfg->blink_cfg;
    uint32_t step = g_ctx.step_count;

    /* 偶步=亮，奇步=灭 */
    bool on = (step % 2 == 0);
    ws2812_color_t color = on ? bc->color : WS2812_COLOR_BLACK;

    for (uint8_t i = 0; i < g_ctx.led_num; i++) {
        g_ctx.buffer[i] = color;
    }
    ws2812_rmt_transmit();

    /* 检查闪烁次数限制 */
    if (bc->count > 0 && step >= (uint32_t)(bc->count * 2)) {
        return -1;
    }

    return on ? (int32_t)bc->on_ms : (int32_t)bc->off_ms;
}

/* ---- 流水灯 ---- */
static int32_t effect_step_flow(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_flow_cfg_t *fc = &cfg->flow_cfg;
    uint8_t num = g_ctx.led_num;
    int32_t pos = (int32_t)(g_ctx.step_count % num);

    /* 禁用状态的线性索引 */
    /* 全灭 */
    for (uint8_t i = 0; i < num; i++) {
        g_ctx.buffer[i] = WS2812_COLOR_BLACK;
    }

    /* 点亮 width 颗灯珠 */
    for (uint8_t w = 0; w < fc->width; w++) {
        int32_t idx = fc->reverse ? (num - 1 - (pos + w) % num) : ((pos + w) % num);
        if (idx < 0) idx += num;
        g_ctx.buffer[idx] = fc->color;
    }

    ws2812_rmt_transmit();

    /* 如果非循环且已经跑完一圈 */
    if (!fc->loop && g_ctx.step_count >= num + fc->width - 1) {
        return -1;
    }

    return (int32_t)fc->speed_ms;
}

/* ---- 彩虹 ---- */
static int32_t effect_step_rainbow(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_rainbow_cfg_t *rc = &cfg->rainbow_cfg;
    uint8_t num = g_ctx.led_num;
    uint16_t hue_per_led = 360 / num;
    uint16_t offset = (uint16_t)(g_ctx.step_count * 3 % 360); /* 每步偏移3度 */

    for (uint8_t i = 0; i < num; i++) {
        uint16_t hue = rc->reverse ?
                       (360 - (i * hue_per_led + offset) % 360) :
                       ((i * hue_per_led + offset) % 360);
        ws2812_color_t c = hsv_to_rgb(hue, 255, rc->brightness * 255 / 100);
        g_ctx.buffer[i] = c;
    }

    ws2812_rmt_transmit();
    return (int32_t)rc->speed_ms;
}

/* ---- 警示爆闪 ---- */
static int32_t effect_step_warning(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_warning_cfg_t *wc = &cfg->warning_cfg;
    uint32_t step = g_ctx.step_count;

    /* 高频闪烁：按占空比决定亮灭 */
    bool on = (step % 2 == 0);

    ws2812_color_t c = on ? wc->color : WS2812_COLOR_BLACK;
    for (uint8_t i = 0; i < g_ctx.led_num; i++) {
        g_ctx.buffer[i] = c;
    }
    ws2812_rmt_transmit();

    uint32_t on_time = wc->interval_ms * wc->duty_on_pct / 100;
    uint32_t off_time = wc->interval_ms - on_time;
    if (on_time == 0) on_time = 10;
    if (off_time == 0) off_time = 10;

    return on ? (int32_t)on_time : (int32_t)off_time;
}

/* ---- 跑马灯（带拖尾） ---- */
static int32_t effect_step_marquee(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_marquee_cfg_t *mc = &cfg->marquee_cfg;
    uint8_t num = g_ctx.led_num;
    int32_t head = (int32_t)(g_ctx.step_count % num);

    /* 全灭 */
    for (uint8_t i = 0; i < num; i++) {
        g_ctx.buffer[i] = WS2812_COLOR_BLACK;
    }

    /* 从头部开始，向后画拖尾 */
    for (uint8_t t = 0; t <= mc->tail_length; t++) {
        int32_t idx = mc->reverse ? (head + t) : (head - t);
        /* 循环取模 */
        idx = ((idx % num) + num) % num;

        /* 越远的尾巴越暗 */
        uint8_t dim_pct = 100 - (uint8_t)(t * 100 / (mc->tail_length + 1));
        g_ctx.buffer[idx] = ws2812_color_dim(mc->color, dim_pct);
    }

    ws2812_rmt_transmit();
    return (int32_t)mc->speed_ms;
}

/* ---- 双闪（交替闪烁） ---- */
static int32_t effect_step_dual_blink(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_dual_blink_cfg_t *dc = &cfg->dual_blink_cfg;
    uint8_t num = g_ctx.led_num;
    bool phase_a = (g_ctx.step_count % 2 == 0);

    for (uint8_t i = 0; i < num; i++) {
        if (dc->group_a_mask & (1 << i)) {
            g_ctx.buffer[i] = phase_a ? dc->color_a : WS2812_COLOR_BLACK;
        } else if (dc->group_b_mask & (1 << i)) {
            g_ctx.buffer[i] = phase_a ? WS2812_COLOR_BLACK : dc->color_b;
        } else {
            g_ctx.buffer[i] = WS2812_COLOR_BLACK;
        }
    }

    ws2812_rmt_transmit();
    return (int32_t)dc->interval_ms;
}

/* ---- 颜色填充（逐个点亮） ---- */
static int32_t effect_step_color_wipe(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_color_wipe_cfg_t *wc = &cfg->color_wipe_cfg;
    uint8_t num = g_ctx.led_num;
    int32_t pos = (int32_t)(g_ctx.step_count % (num + 1)); /* +1 留出清空状态 */

    if (wc->clear_after && pos == 0 && g_ctx.step_count > 0) {
        /* 上一轮填充完，清空重新开始 */
        for (uint8_t i = 0; i < num; i++) {
            g_ctx.buffer[i] = WS2812_COLOR_BLACK;
        }
        ws2812_rmt_transmit();
        return (int32_t)wc->interval_ms;
    }

    uint8_t idx = wc->reverse ? (num - 1 - (uint8_t)pos) : (uint8_t)pos;
    if (idx < num) {
        g_ctx.buffer[idx] = wc->color;
    }

    ws2812_rmt_transmit();
    return (int32_t)wc->interval_ms;
}

/* ---- 剧院追逐 ---- */
static int32_t effect_step_theater(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_theater_cfg_t *tc = &cfg->theater_cfg;
    uint8_t num = g_ctx.led_num;
    uint8_t cycle = tc->width * 2;
    uint8_t offset = (uint8_t)(g_ctx.step_count % cycle);

    for (uint8_t i = 0; i < num; i++) {
        if ((i + offset) % cycle < tc->width) {
            g_ctx.buffer[i] = tc->color;
        } else {
            g_ctx.buffer[i] = WS2812_COLOR_BLACK;
        }
    }

    ws2812_rmt_transmit();
    return (int32_t)tc->interval_ms;
}

/* ---- 彗星拖尾 ---- */
static int32_t effect_step_comet(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_comet_cfg_t *cc = &cfg->comet_cfg;
    uint8_t num = g_ctx.led_num;
    int32_t head = (int32_t)(g_ctx.step_count % (num + cc->tail_length));

    /* 全灭 */
    for (uint8_t i = 0; i < num; i++) {
        g_ctx.buffer[i] = WS2812_COLOR_BLACK;
    }

    for (uint8_t t = 0; t < cc->tail_length; t++) {
        int32_t idx = cc->reverse ? (head + t) : (head - t);
        if (idx < 0 || idx >= num) continue;

        uint8_t brightness = (uint8_t)(100 - t * 100 / cc->tail_length);
        if (t == 0) {
            g_ctx.buffer[idx] = cc->head_color; /* 头部全亮 */
        } else {
            g_ctx.buffer[idx] = ws2812_color_dim(cc->head_color, brightness);
        }
    }

    ws2812_rmt_transmit();
    return (int32_t)cc->speed_ms;
}

/* ---- 火焰模拟（伪随机闪烁） ---- */
static int32_t effect_step_fire(const ws2812_effect_cfg_t *cfg)
{
    const ws2812_fire_cfg_t *fc = &cfg->fire_cfg;
    uint8_t num = g_ctx.led_num;

    /* 火焰色板：红、橙、黄 */
    static const ws2812_color_t fire_palette[] = {
        {128, 255, 0  }, /* 橙 */
        {180, 255, 0  }, /* 金橙 */
        {0,   255, 0  }, /* 红 */
        {255, 255, 0  }, /* 黄 */
        {64,  255, 0  }, /* 橙红 */
    };
    #define FIRE_PALETTE_SIZE  (sizeof(fire_palette) / sizeof(fire_palette[0]))

    for (uint8_t i = 0; i < num; i++) {
        /* 根据离边缘距离决定亮度权重 */
        uint8_t dist_from_edge = (i < num / 2) ? i : (num - 1 - i);

        if (fc->dual_fire) {
            /* 双火源：两边亮中间暗 */
            ;
        }

        /* 用伪随机决定当前帧该灯珠的亮度与色相 */
        uint32_t seed = g_ctx.step_count * 1103515245 + i * 12345;
        uint8_t r = (uint8_t)((seed >> 16) & 0xFF);

        /* 离中心越近越亮（双火源）或离一侧越近越亮（单火源） */
        uint8_t dim_factor;
        if (fc->dual_fire) {
            int edge_dist = (i < num / 2) ? i : (num - 1 - i);
            dim_factor = (uint8_t)(100 - edge_dist * 30);
        } else {
            dim_factor = (uint8_t)(100 - i * 20);
        }
        if (dim_factor > 100) dim_factor = 0;

        uint8_t final_bright = (uint8_t)((uint16_t)fc->intensity * dim_factor / 100 * (50 + r % 50) / 100);

        /* 随机选火焰色 */
        uint8_t color_idx = (uint8_t)((r * FIRE_PALETTE_SIZE) / 256);
        g_ctx.buffer[i] = ws2812_color_dim(fire_palette[color_idx], final_bright);
    }

    ws2812_rmt_transmit();
    return (int32_t)fc->speed_ms;
}
