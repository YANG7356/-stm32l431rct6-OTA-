#ifndef __BOOTLOADER_H__
#define __BOOTLOADER_H__

#include "stm32l4xx_hal.h"
#include "partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bootloader 主入口 */
void Bootloader_Main(void);

/* 尝试执行升级流程
 * 返回 HAL_OK: 升级成功
 * 返回 HAL_ERROR/HAL_TIMEOUT: 升级失败
 */
HAL_StatusTypeDef Bootloader_PerformUpgrade(void);

/* 跳转到 APP */
void Bootloader_JumpToApp(void);

/* 检查 APP 是否有效 */
uint8_t Bootloader_IsAppValid(void);

/* 读取参数区 */
HAL_StatusTypeDef Bootloader_ReadParam(Param_t *param);

/* 写回参数区 */
HAL_StatusTypeDef Bootloader_WriteParam(Param_t *param);

/* 恢复/初始化参数区为默认值 */
HAL_StatusTypeDef Bootloader_ResetParam(void);

#ifdef __cplusplus
}
#endif

#endif
