#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "stm32l4xx_hal.h"
#include "partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bootloader 主处理流程
 * @note  流程：
 *        1. 参数区无效则初始化
 *        2. 若存在待升级固件则执行升级
 *        3. 若 APP 有效则跳转
 *        4. 否则停留在 Bootloader
 */
void Bootloader_Run(void);

/**
 * @brief 检查 APP 区是否存在有效应用
 * @return 1=有效，0=无效
 */
uint8_t Bootloader_IsAppValid(void);

/**
 * @brief 跳转到 APP
 */
void Bootloader_JumpToApp(void);

/**
 * @brief 执行一次 OTA 升级流程
 * @return HAL_OK=成功，其它=失败
 */
HAL_StatusTypeDef Bootloader_DoUpgrade(void);

/**
 * @brief 初始化参数区为默认状态
 * @return HAL_OK=成功
 */
HAL_StatusTypeDef Bootloader_InitParamArea(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_H */
