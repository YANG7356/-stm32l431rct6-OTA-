#ifndef ZD25WQ32_H
#define ZD25WQ32_H

#include "stm32l4xx_hal.h"

/* 外部 QSPI 句柄，CubeMX 生成在 main.c 或 qspi.c 中 */
extern QSPI_HandleTypeDef hqspi;

/* ========== 基础驱动函数 ========== */

/* 硬件初始化：拉高 WP/IO2 和 HOLD/IO3，解除写保护与保持 */
void ZD25WQ32_Init(void);

/* 读取 JEDEC ID（3 字节，0xBA 0x60 0x16） */
HAL_StatusTypeDef ZD25WQ32_ReadID(uint8_t *pID);

/* 写使能 (0x06)，擦写前必须调用 */
HAL_StatusTypeDef ZD25WQ32_WriteEnable(void);

/* 等待芯片空闲（BUSY 位清 0），超时时间 ms */
HAL_StatusTypeDef ZD25WQ32_WaitBusy(uint32_t timeout_ms);

/* 擦除 4 KB 扇区 */
HAL_StatusTypeDef ZD25WQ32_EraseSector(uint32_t address);

/* 擦除 64 KB 块（推荐用于擦除大片区域） */
HAL_StatusTypeDef ZD25WQ32_EraseBlock64(uint32_t address);

/* 页编程（单页最多 256 字节），地址与长度不能跨页 */
HAL_StatusTypeDef ZD25WQ32_WritePage(uint32_t address, uint8_t *pData, uint16_t size);

/* 读取任意长度数据（使用标准读 0x03） */
HAL_StatusTypeDef ZD25WQ32_Read(uint32_t address, uint8_t *pData, uint16_t size);

#endif
