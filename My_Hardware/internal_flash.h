#ifndef INTERNAL_FLASH_H
#define INTERNAL_FLASH_H

#include "stm32l4xx_hal.h"
#include "partition.h"

/* 基础操作 */
HAL_StatusTypeDef IntFlash_ErasePage(uint32_t page_address);
HAL_StatusTypeDef IntFlash_WriteDoubleWord(uint32_t address, uint64_t data);
HAL_StatusTypeDef IntFlash_WriteBuffer(uint32_t address, const uint8_t *pData, uint32_t size);

/* 分区封装操作 */
HAL_StatusTypeDef IntFlash_EraseAppArea(void);
HAL_StatusTypeDef IntFlash_EraseParamArea(void);
HAL_StatusTypeDef IntFlash_WriteAppFromExternal(uint32_t ext_addr, uint32_t size);
HAL_StatusTypeDef IntFlash_UpdateParam(const Param_t *new_param);

#endif
