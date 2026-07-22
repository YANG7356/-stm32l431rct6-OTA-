#include "internal_flash.h"
#include "partition.h"
#include "zd25wq32.h"
#include <string.h>

/* ==================== 本地宏 ==================== */
#ifndef INT_FLASH_WRITE_UNIT
#define INT_FLASH_WRITE_UNIT    8U
#endif

#ifndef EXT_FLASH_CHUNK_SIZE
#define EXT_FLASH_CHUNK_SIZE    256U
#endif

/* ==================== 内部静态函数声明 ==================== */
static uint8_t IntFlash_IsAddressInApp(uint32_t addr);
static uint8_t IntFlash_IsAddressInParam(uint32_t addr);
static HAL_StatusTypeDef IntFlash_VerifyBuffer(uint32_t address, const uint8_t *pData, uint32_t size);

/* ---------- 基础内部 Flash 驱动 ---------- */
/**
 * @brief 擦除内部 Flash 的一页（2 KB）
 * @param page_address 页内任意地址（函数内部自动对齐到页边界）
 */
HAL_StatusTypeDef IntFlash_ErasePage(uint32_t page_address)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0U;
    uint32_t aligned_addr;

    if (page_address < INT_FLASH_BASE)
        return HAL_ERROR;

    if (page_address > INT_FLASH_END)
        return HAL_ERROR;

    aligned_addr = page_address - (page_address % FLASH_PAGE_SIZE);

    HAL_FLASH_Unlock();

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Page      = (aligned_addr - INT_FLASH_BASE) / FLASH_PAGE_SIZE;
    erase_init.NbPages   = 1U;
    erase_init.Banks     = FLASH_BANK_1;   /* 当前分区均在 Bank1 */

    status = HAL_FLASHEx_Erase(&erase_init, &page_error);

    HAL_FLASH_Lock();
    return status;
}

/**
 * @brief 在内部 Flash 的 address 地址处写入一个双字（8字节）
 * @param address 目标地址（必须为双字对齐）
 * @param data    要写入的 64 位数据
 */
HAL_StatusTypeDef IntFlash_WriteDoubleWord(uint32_t address, uint64_t data)
{
    HAL_StatusTypeDef status;

    if ((address % INT_FLASH_WRITE_UNIT) != 0U)
        return HAL_ERROR;

    if (address < INT_FLASH_BASE)
        return HAL_ERROR;

    if ((address + INT_FLASH_WRITE_UNIT - 1U) > INT_FLASH_END)
        return HAL_ERROR;

    HAL_FLASH_Unlock();
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data);
    HAL_FLASH_Lock();

    if (status != HAL_OK)
        return status;

    /* 写后读回校验 */
    if (*(const uint64_t *)address != data)
        return HAL_ERROR;

    return HAL_OK;
}

/**
 * @brief 向内部 Flash 写入指定长度的字节数据（自动处理对齐）
 * @param address 起始地址
 * @param pData   数据指针
 * @param size    写入字节数
 * @note  内部会将 size 向上按 8 字节处理，不足部分用 0xFF 填充
 */
HAL_StatusTypeDef IntFlash_WriteBuffer(uint32_t address, const uint8_t *pData, uint32_t size)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint64_t dw_data;
    uint32_t i = 0U;

    if ((pData == NULL) || (size == 0U))
        return HAL_ERROR;

    if ((address % INT_FLASH_WRITE_UNIT) != 0U)
        return HAL_ERROR;

    if (address < INT_FLASH_BASE)
        return HAL_ERROR;

    if ((address + size - 1U) > INT_FLASH_END)
        return HAL_ERROR;

    while (i < size)
    {
        uint32_t j;

        dw_data = 0xFFFFFFFFFFFFFFFFULL;

        for (j = 0U; j < INT_FLASH_WRITE_UNIT; j++)
        {
            if ((i + j) < size)
                ((uint8_t *)&dw_data)[j] = pData[i + j];
            else
                ((uint8_t *)&dw_data)[j] = 0xFFU;
        }

        status = IntFlash_WriteDoubleWord(address + i, dw_data);
        if (status != HAL_OK)
            return status;

        if (memcmp((const void *)(address + i), &dw_data, INT_FLASH_WRITE_UNIT) != 0)
            return HAL_ERROR;

        i += INT_FLASH_WRITE_UNIT;
    }

    return status;
}

/* ---------- 分区操作封装 ---------- */
/**
 * @brief 擦除整个 APP 区
 */
HAL_StatusTypeDef IntFlash_EraseAppArea(void)
{
    uint32_t page_addr = APP_ADDR;
    uint32_t end_addr  = APP_ADDR + APP_SIZE;

    if (APP_SIZE == 0U)
        return HAL_ERROR;

    if (!IntFlash_IsAddressInApp(APP_ADDR))
        return HAL_ERROR;

    if (APP_END >= PARAM_ADDR)
        return HAL_ERROR;

    while (page_addr < end_addr)
    {
        if (IntFlash_ErasePage(page_addr) != HAL_OK)
            return HAL_ERROR;

        page_addr += FLASH_PAGE_SIZE;
    }

    return HAL_OK;
}

/**
 * @brief 擦除参数区（单页 2 KB）
 */
HAL_StatusTypeDef IntFlash_EraseParamArea(void)
{
    if (!IntFlash_IsAddressInParam(PARAM_ADDR))
        return HAL_ERROR;

    return IntFlash_ErasePage(PARAM_ADDR);
}

/**
 * @brief 从外部 Flash 缓存区搬运固件到内部 APP 区
 * @param ext_addr 外部 Flash 源地址
 * @param size     固件大小（字节）
 */
HAL_StatusTypeDef IntFlash_WriteAppFromExternal(uint32_t ext_addr, uint32_t size)
{
    uint8_t  buf[EXT_FLASH_CHUNK_SIZE];
    uint32_t offset = 0U;
    uint32_t chunk;

    if ((size == 0U) || (size > APP_SIZE))
        return HAL_ERROR;

    if (ext_addr > DOWNLOAD_CACHE_END)
        return HAL_ERROR;

    if (size > (DOWNLOAD_CACHE_END - ext_addr + 1U))
        return HAL_ERROR;

    if ((APP_ADDR % INT_FLASH_WRITE_UNIT) != 0U)
        return HAL_ERROR;

    while (offset < size)
    {
        chunk = size - offset;
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);

        if (ZD25WQ32_Read(ext_addr + offset, buf, (uint16_t)chunk) != HAL_OK)
            return HAL_ERROR;

        if (IntFlash_WriteBuffer(APP_ADDR + offset, buf, chunk) != HAL_OK)
            return HAL_ERROR;

        offset += chunk;
    }

    return HAL_OK;
}

/**
 * @brief 安全更新参数区（擦除后整结构写入）
 * @param new_param 新参数结构体指针
 */
HAL_StatusTypeDef IntFlash_UpdateParam(const Param_t *new_param)
{
    Param_t backup;

    if (new_param == NULL)
        return HAL_ERROR;

    if (!IntFlash_IsAddressInParam(PARAM_ADDR))
        return HAL_ERROR;

    memset(&backup, 0xFF, sizeof(backup));

    backup.magic      = new_param->magic;
    backup.status     = new_param->status;
    backup.fw_size    = new_param->fw_size;
    backup.fw_crc     = new_param->fw_crc;
    backup.fw_version = new_param->fw_version;

    /* reserved[] 保持 0xFFFFFFFF，方便后续扩展 */

    if (IntFlash_EraseParamArea() != HAL_OK)
        return HAL_ERROR;

    if (IntFlash_WriteBuffer(PARAM_ADDR, (const uint8_t *)&backup, sizeof(Param_t)) != HAL_OK)
        return HAL_ERROR;

    if (IntFlash_VerifyBuffer(PARAM_ADDR, (const uint8_t *)&backup, sizeof(Param_t)) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}

/* ==================== 内部静态函数 ==================== */
/**
 * @brief 判断地址是否落在 APP 区
 */
static uint8_t IntFlash_IsAddressInApp(uint32_t addr)
{
    if ((addr >= APP_ADDR) && (addr <= APP_END))
        return 1U;

    return 0U;
}

/**
 * @brief 判断地址是否落在参数区
 */
static uint8_t IntFlash_IsAddressInParam(uint32_t addr)
{
    if ((addr >= PARAM_ADDR) && (addr <= PARAM_END))
        return 1U;

    return 0U;
}

/**
 * @brief 校验 Flash 中一段内容是否与缓冲区一致
 */
static HAL_StatusTypeDef IntFlash_VerifyBuffer(uint32_t address, const uint8_t *pData, uint32_t size)
{
    if ((pData == NULL) || (size == 0U))
        return HAL_ERROR;

    if (memcmp((const void *)address, pData, size) != 0)
        return HAL_ERROR;

    return HAL_OK;
}
