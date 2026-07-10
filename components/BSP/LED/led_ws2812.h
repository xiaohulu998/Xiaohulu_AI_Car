/**
 * @file    led_ws2812.h
 * @brief   WS2812B 可编程RGB灯带 硬件底层驱动
 * @author  xiaohulu
 * @date    2026-07-10
 *
 * 基于 ESP32-S3 RMT 外设实现单总线精确时序控制。
 * 内置多特效引擎（呼吸/闪烁/流水/彩虹/警示/跑马等），
 * FreeRTOS 任务异步驱动，线程安全，上层直接调用即可。
 *
 * 硬件接线：WS2812B DIN → ESP32-S3 任意 GPIO
 * 灯珠排列：前左(0)  前右(1)  后左(2)  后右(3)  中左(4)  中右(5)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/rmt_tx.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  配置宏
 * ================================================================ */
#define WS2812_LED_NUM          6       /**< 灯珠总数 */
#define WS2812_RESET_TIME_US    300     /**< 复位信号低电平时间（微秒），WS2812B 要求 >50us */

/* ================================================================
 *  LED 物理位置枚举（语义化索引）
 * ================================================================ */
typedef enum {
    LED_FRONT_LEFT  = 0,    /**< 前左灯 */
    LED_FRONT_RIGHT = 1,    /**< 前右灯 */
    LED_REAR_LEFT   = 2,    /**< 后左灯 */
    LED_REAR_RIGHT  = 3,    /**< 后右灯 */
    LED_MID_LEFT    = 4,    /**< 中左灯 */
    LED_MID_RIGHT   = 5,    /**< 中右灯 */
} ws2812_led_pos_t;

/* ================================================================
 *  颜色结构体（WS2812B 原生 GRB 顺序）
 * ================================================================ */
typedef struct {
    uint8_t g;  /**< 绿色分量 */
    uint8_t r;  /**< 红色分量 */
    uint8_t b;  /**< 蓝色分量 */
} ws2812_color_t;

/* ---- 预定义常用颜色 ---- */
#define WS2812_COLOR_BLACK      ((ws2812_color_t){0,   0,   0  })
#define WS2812_COLOR_RED        ((ws2812_color_t){0,   255, 0  })
#define WS2812_COLOR_GREEN      ((ws2812_color_t){255, 0,   0  })
#define WS2812_COLOR_BLUE       ((ws2812_color_t){0,   0,   255})
#define WS2812_COLOR_YELLOW     ((ws2812_color_t){255, 255, 0  })
#define WS2812_COLOR_CYAN       ((ws2812_color_t){255, 0,   255})
#define WS2812_COLOR_MAGENTA    ((ws2812_color_t){0,   255, 255})
#define WS2812_COLOR_WHITE      ((ws2812_color_t){255, 255, 255})
#define WS2812_COLOR_ORANGE     ((ws2812_color_t){128, 255, 0  })
#define WS2812_COLOR_PURPLE     ((ws2812_color_t){0,   128, 128})
#define WS2812_COLOR_WARM_WHITE ((ws2812_color_t){200, 255, 64 })
#define WS2812_COLOR_GOLD       ((ws2812_color_t){180, 255, 0  })

/* ================================================================
 *  灯效类型枚举
 * ================================================================ */
typedef enum {
    WS2812_EFFECT_NONE = 0,         /**< 无效果（空闲） */
    WS2812_EFFECT_STATIC,           /**< 静态常亮 */
    WS2812_EFFECT_BREATHING,        /**< 呼吸灯 */
    WS2812_EFFECT_BLINK,            /**< 闪烁 */
    WS2812_EFFECT_FLOW,             /**< 流水灯（单方向流动） */
    WS2812_EFFECT_RAINBOW,          /**< 彩虹渐变 */
    WS2812_EFFECT_WARNING,          /**< 警示爆闪 */
    WS2812_EFFECT_MARQUEE,          /**< 跑马灯（带拖尾） */
    WS2812_EFFECT_DUAL_BLINK,       /**< 双闪（两组交替闪烁） */
    WS2812_EFFECT_COLOR_WIPE,       /**< 颜色填充（逐个点亮） */
    WS2812_EFFECT_THEATER_CHASE,    /**< 剧院追逐 */
    WS2812_EFFECT_COMET,            /**< 彗星拖尾 */
    WS2812_EFFECT_FIRE,             /**< 火焰模拟 */
    WS2812_EFFECT_COUNT
} ws2812_effect_t;

/* ================================================================
 *  各效果配置结构体
 * ================================================================ */

/** 静态常亮 */
typedef struct {
    ws2812_color_t color;       /**< 目标颜色 */
} ws2812_static_cfg_t;

/** 呼吸灯 */
typedef struct {
    ws2812_color_t color;       /**< 呼吸颜色 */
    uint32_t period_ms;         /**< 一次呼吸周期（ms），默认 2000 */
    uint8_t min_brightness;     /**< 最小亮度 0~100，默认 5 */
    uint8_t max_brightness;     /**< 最大亮度 0~100，默认 100 */
} ws2812_breathing_cfg_t;

/** 闪烁 */
typedef struct {
    ws2812_color_t color;       /**< 闪烁颜色 */
    uint32_t on_ms;             /**< 亮起时长（ms） */
    uint32_t off_ms;            /**< 熄灭时长（ms） */
    int16_t  count;             /**< 闪烁次数，-1=无限，0=无效 */
} ws2812_blink_cfg_t;

/** 流水灯 */
typedef struct {
    ws2812_color_t color;       /**< 流动颜色 */
    uint32_t speed_ms;          /**< 每步间隔（ms） */
    uint8_t  width;             /**< 同时点亮的灯珠数（默认1） */
    bool     reverse;           /**< true=反向流动 */
    bool     loop;              /**< true=循环 */
} ws2812_flow_cfg_t;

/** 彩虹渐变 */
typedef struct {
    uint32_t speed_ms;          /**< 变化速度（ms） */
    uint8_t  brightness;        /**< 亮度 0~100，默认 80 */
    bool     reverse;           /**< 色相方向 */
} ws2812_rainbow_cfg_t;

/** 警示爆闪 */
typedef struct {
    ws2812_color_t color;       /**< 警示颜色（默认红） */
    uint32_t interval_ms;       /**< 闪烁间隔（ms），越小越紧急 */
    uint8_t  duty_on_pct;       /**< 亮起占空比 0~100，默认 30 */
} ws2812_warning_cfg_t;

/** 跑马灯（带拖尾） */
typedef struct {
    ws2812_color_t color;       /**< 跑马颜色 */
    uint32_t speed_ms;          /**< 移动间隔（ms） */
    uint8_t  tail_length;       /**< 拖尾衰减级数（默认3） */
    bool     reverse;
} ws2812_marquee_cfg_t;

/** 双闪（两组交替） */
typedef struct {
    ws2812_color_t color_a;     /**< 组A颜色 */
    ws2812_color_t color_b;     /**< 组B颜色 */
    uint32_t interval_ms;       /**< 切换间隔（ms） */
    uint8_t  group_a_mask;      /**< 组A LED 位掩码，bit0=LED0… 0=所有偶数灯 */
    uint8_t  group_b_mask;      /**< 组B LED 位掩码，0=所有奇数灯 */
} ws2812_dual_blink_cfg_t;

/** 颜色填充（逐个点亮，常用于开机动画） */
typedef struct {
    ws2812_color_t color;       /**< 填充颜色 */
    uint32_t interval_ms;       /**< 每步间隔（ms） */
    bool     reverse;           /**< 方向 */
    bool     clear_after;       /**< 填充完成后是否熄灭再重来 */
} ws2812_color_wipe_cfg_t;

/** 剧院追逐 */
typedef struct {
    ws2812_color_t color;       /**< 追逐颜色 */
    uint32_t interval_ms;       /**< 移动间隔（ms） */
    uint8_t  width;             /**< 亮灯宽度 */
} ws2812_theater_cfg_t;

/** 彗星拖尾（单颗亮灯后面跟衰减尾巴） */
typedef struct {
    ws2812_color_t head_color;  /**< 头部颜色 */
    uint32_t speed_ms;          /**< 移动间隔 */
    uint8_t  tail_length;       /**< 拖尾长度 */
    bool     reverse;
} ws2812_comet_cfg_t;

/** 火焰模拟 */
typedef struct {
    uint32_t speed_ms;          /**< 更新速度 */
    uint8_t  intensity;         /**< 强度 0~100 */
    bool     dual_fire;         /**< 双火源（两侧→中间） */
} ws2812_fire_cfg_t;

/* ================================================================
 *  通用效果参数联合体
 * ================================================================ */
typedef union {
    ws2812_static_cfg_t     static_cfg;
    ws2812_breathing_cfg_t  breathing_cfg;
    ws2812_blink_cfg_t      blink_cfg;
    ws2812_flow_cfg_t       flow_cfg;
    ws2812_rainbow_cfg_t    rainbow_cfg;
    ws2812_warning_cfg_t    warning_cfg;
    ws2812_marquee_cfg_t    marquee_cfg;
    ws2812_dual_blink_cfg_t dual_blink_cfg;
    ws2812_color_wipe_cfg_t color_wipe_cfg;
    ws2812_theater_cfg_t    theater_cfg;
    ws2812_comet_cfg_t      comet_cfg;
    ws2812_fire_cfg_t       fire_cfg;
} ws2812_effect_cfg_t;

/* ================================================================
 *  API 函数
 * ================================================================ */

/**
 * @brief   初始化 WS2812B 驱动（RMT + 效果引擎任务）
 * @param   gpio_num   接 WS2812B DIN 的 GPIO 编号
 * @param   led_num    实际灯珠数量（≤ WS2812_LED_NUM）
 * @return  ESP_OK 成功，其他失败
 */
esp_err_t ws2812_init(gpio_num_t gpio_num, uint8_t led_num);

/**
 * @brief   反初始化，释放所有资源
 */
void ws2812_deinit(void);

/* ---------- 底层直接控制（绕过效果引擎，立即生效） ---------- */

/**
 * @brief   直接刷新全部灯珠为一个颜色（阻塞发送）
 */
void ws2812_set_all(ws2812_color_t color);

/**
 * @brief   直接设置单颗灯珠颜色（需调用 ws2812_flush 才发送）
 */
void ws2812_set_led(uint8_t index, ws2812_color_t color);

/**
 * @brief   直接设置范围灯珠颜色
 */
void ws2812_set_range(uint8_t start, uint8_t end, ws2812_color_t color);

/**
 * @brief   全灭（立即发送）
 */
void ws2812_clear_all(void);

/**
 * @brief   将缓冲区数据刷新到灯带（RMT 发送）
 */
void ws2812_flush(void);

/**
 * @brief   获取 buffer 中某颗灯珠当前颜色
 */
ws2812_color_t ws2812_get_led_color(uint8_t index);

/* ---------- 效果引擎（异步、可打断） ---------- */

/**
 * @brief   启动一个灯效（会终止当前正在运行的灯效）
 * @param   effect  灯效类型
 * @param   cfg     效果配置指针，传 NULL 使用内置默认值
 * @return  ESP_OK / ESP_ERR_INVALID_ARG / ESP_FAIL
 */
esp_err_t ws2812_effect_start(ws2812_effect_t effect, const ws2812_effect_cfg_t *cfg);

/**
 * @brief   停止当前灯效（不清除灯珠状态，保持最后一帧）
 */
void ws2812_effect_stop(void);

/**
 * @brief   停止当前灯效并全灭
 */
void ws2812_effect_stop_and_clear(void);

/**
 * @brief   查询当前运行的灯效类型
 */
ws2812_effect_t ws2812_effect_get_current(void);

/**
 * @brief   检查效果引擎是否空闲（没有在跑的效果）
 */
bool ws2812_effect_is_idle(void);

/* ---------- 全局设置 ---------- */

/**
 * @brief   设置全局亮度（0~100），影响所有后续输出
 */
void ws2812_set_global_brightness(uint8_t percent);

/**
 * @brief   获取当前全局亮度
 */
uint8_t ws2812_get_global_brightness(void);

/* ================================================================
 *  便捷内联函数
 * ================================================================ */

/**
 * @brief   根据 RGB 三通道构造 ws2812_color_t
 * @note    WS2812B 原生为 GRB 顺序，此函数自动排列
 */
static inline ws2812_color_t ws2812_color(uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_color_t c = { .g = g, .r = r, .b = b };
    return c;
}

/**
 * @brief   按百分比调暗颜色
 */
static inline ws2812_color_t ws2812_color_dim(ws2812_color_t c, uint8_t percent)
{
    if (percent >= 100) return c;
    c.r = (uint8_t)((uint16_t)c.r * percent / 100);
    c.g = (uint8_t)((uint16_t)c.g * percent / 100);
    c.b = (uint8_t)((uint16_t)c.b * percent / 100);
    return c;
}

/**
 * @brief   两颜色混合（weight: 0=纯c1, 255=纯c2）
 */
static inline ws2812_color_t ws2812_color_mix(ws2812_color_t c1, ws2812_color_t c2, uint8_t weight)
{
    ws2812_color_t c;
    c.r = (uint8_t)(((uint16_t)c1.r * (255 - weight) + (uint16_t)c2.r * weight) / 255);
    c.g = (uint8_t)(((uint16_t)c1.g * (255 - weight) + (uint16_t)c2.g * weight) / 255);
    c.b = (uint8_t)(((uint16_t)c1.b * (255 - weight) + (uint16_t)c2.b * weight) / 255);
    return c;
}

#ifdef __cplusplus
}
#endif
