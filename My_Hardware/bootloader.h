#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "stm32l4xx_hal.h"
#include "partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bootloader 主处理流程：
 * - 参数区无效则初始化
 * - 若存在待升级固件则执行升级
 * - 若 APP 有效则跳转
 * - 否则停留在 Bootloader
 */
void Bootloader_Run(void);

/* 检查 APP 区是否存在有效应用 */
uint8_t Bootloader_IsAppValid(void);

/* 跳转到 APP */
void Bootloader_JumpToApp(void);

/* 执行一次 OTA 升级流程 */
HAL_StatusTypeDef Bootloader_DoUpgrade(void);

/* 初始化参数区为默认状态 */
HAL_StatusTypeDef Bootloader_InitParamArea(void);

#ifdef __cplusplus
}
#endif

#endif
