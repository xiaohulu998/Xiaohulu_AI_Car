/**
* 全局硬件总初始化入口，应在其他 BSP 模块初始化之前调用，且全局仅调用一次
* 1.NVS Flash；2.TCP/IP 协议栈 (lwIP)；3.默认事件循环
*/

#ifndef _BSP_BOARD_H_
#define _BSP_BOARD_H_

#include "esp_err.h"


/**
 * @brief  板级系统初始化（NVS + TCP/IP + Event Loop）
 * @return ESP_OK 成功，其他值失败
 */
esp_err_t bsp_board_init(void);

#endif