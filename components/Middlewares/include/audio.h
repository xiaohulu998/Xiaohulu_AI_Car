#ifndef _AUDIO_H
#define _AUDIO_H
#include "driver/gpio.h"

/** 初始化音频设置
 * @param 无
 * @return 无
 */
void start_audio(void);


/** 开始录音
 * @param rec_time 录音时间
 * @return 无
 */
void start_record(uint32_t rec_time);

/** 开始录音
 * @param rec_time 录音时间
 * @return 无
 */
void start_play(void);

#endif
