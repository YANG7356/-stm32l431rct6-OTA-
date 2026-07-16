#include "internal_flash.h"
#include "partition.h"
#include "zd25wq32.h"
#include "string.h"

/* ---------- 基础内部 Flash 驱动 ---------- */
/**
 * @brief 擦除内部 Flash 的一页（2 KB）
 * @param page_address 页内任意地址（函数内部自动对齐到页边界）
 */
HAL_StatusTypeDef IntFlash_ErasePage(uint32_t page_address)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error;

    HAL_FLASH_Unlock(); // 解锁Flash

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;                             // 按页擦除模式
    erase_init.Page      = (page_address - FLASH_BASE) / FLASH_PAGE_SIZE;     // 要擦除的起始页编号
    erase_init.NbPages   = 1;                                                 // 连续擦除的页数
    erase_init.Banks     = FLASH_BANK_1;                                      // 要操作哪个BANK

    status = HAL_FLASHEx_Erase(&erase_init, &page_error);                     // 按设定的要求擦除2kb，并将状态存储在state变量中

    HAL_FLASH_Lock(); // 锁上Flash
    return status;
}

/**
 * @brief 在内部 Flash 的 address 地址处写入一个双字（8字节）
 * @param address 目标地址（必须为双字对齐的地址）
 * @param data    要写入的 64 位数据
 */
HAL_StatusTypeDef IntFlash_WriteDoubleWord(uint32_t address, uint64_t data)
{
    HAL_StatusTypeDef status;
    
    if ((address % 8U) != 0U)   // 底层兜底，如果不是以8字节对齐就报错
        return HAL_ERROR;
    
    HAL_FLASH_Unlock();
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data); // 按双字(64位)的方式写入 64bit 的数据
    HAL_FLASH_Lock();
    return status;
}

/**
 * @brief 向内部 Flash 写入指定长度的字节数据（自动处理对齐）
 * @param address 起始地址
 * @param pData   数据指针
 * @param size    写入字节数
 * @note  内部会将 size 向上对齐到 8 的倍数，不足部分用 0xFF 填充，
 *        不会影响 Flash 原有内容（擦除后即为 0xFF）。
 */
HAL_StatusTypeDef IntFlash_WriteBuffer(uint32_t address, const uint8_t *pData, uint32_t size)
{ 
    HAL_StatusTypeDef status = HAL_OK;
    uint64_t dw_data;
    uint32_t i = 0;
    
    if (pData == NULL || size == 0U)                              //输入检测
        return HAL_ERROR;
    if ((address % 8U) != 0U)
        return HAL_ERROR;
    
    while (i < size)
    {
        dw_data = 0xFFFFFFFFFFFFFFFFULL;                         // 将 dw_data 置高，准备重新拼装当前 8 字节块
        for (int j = 0; j < 8; j++)                              // 把原始字节数组 pData 中连续的 8 个字节，装填进 dw_data
        {
            if ((i + j) < size)                                  // 字节位置仍在 pData 的有效范围内，把 pData[i + j] 原样复制进来
                ((uint8_t *)&dw_data)[j] = pData[i + j];         // 取 dw_data 的地址，然后强制转换成指向字节的指针。此时，dw_data 的 8 个字节就可以通过下标 [0] 到 [7] 来访问了 
            else
                ((uint8_t *)&dw_data)[j] = 0xFF;                 // 当一次的数据长度达不到64位时以0xFF补齐，和擦除后的状态一致
        }

        status = IntFlash_WriteDoubleWord(address + i, dw_data); // 传入写入的地址以及写入的数据
        if (status != HAL_OK)
            return status;

        i += 8;                                                  // 每完成一轮 8 字节数据传递，pData 本轮数组起始下标都要相应偏移 8 
    }

    return status;
}

/* ---------- 分区操作封装 ---------- */
/**
 * @brief 擦除整个 APP 区（216 KB，108 页）
 */
HAL_StatusTypeDef IntFlash_EraseAppArea(void)
{ 
    uint32_t page_addr = APP_ADDR;
    uint32_t end_addr  = APP_ADDR + APP_SIZE;
   
    if (APP_SIZE == 0U)
        return HAL_ERROR;
    if ((APP_ADDR < FLASH_BASE) || (end_addr > PARAM_ADDR))
        return HAL_ERROR;    

    while (page_addr < end_addr)
    {
        if (IntFlash_ErasePage(page_addr) != HAL_OK)
            return HAL_ERROR;
        page_addr += FLASH_PAGE_SIZE;        // 每擦完一页，下一轮起始地址增加2048 FLASH_PAGE_SIZE == 2048 == 2kb     
    }
    return HAL_OK;
}

/**
 * @brief 擦除参数区（单页 2 KB），谨慎使用
 */
HAL_StatusTypeDef IntFlash_EraseParamArea(void)
{
    return IntFlash_ErasePage(PARAM_ADDR);
}

/**
 * @brief 从外部 Flash 缓存区搬运固件到内部 APP 区
 * @param ext_addr 外部 Flash 源地址
 * @param size     固件大小（字节）
 */
HAL_StatusTypeDef IntFlash_WriteAppFromExternal(uint32_t ext_addr, uint32_t size)
{   
    uint8_t buf[256];                        // 使用数组一次存储zd25wq32一页的数据
    uint32_t offset = 0;                     // 用于标记搬运了多少数据
    
    if (size == 0U || size > APP_SIZE)       // 输入检测
        return HAL_ERROR;
    if ((ext_addr + size) > EXT_FLASH_SIZE)
        return HAL_ERROR;    

    /* 一次最多写入256字节，分多次写入 */
    while (offset < size)
    {
        uint16_t chunk = (size - offset >= 256) ? 256 : (uint16_t)(size - offset); //如果剩余值大于256，一次搬运chunk=256，否则一次搬运大小为剩余小于256的部分

        if (ZD25WQ32_Read(ext_addr + offset, buf, chunk) != HAL_OK) // 将外部Flash ext_addr + offset地址处的数据读入buf
            return HAL_ERROR;
        if (IntFlash_WriteBuffer(APP_ADDR + offset, buf, chunk) != HAL_OK) // 将buf中的内容写入内部Flash APP_ADDR + offset地址处
            return HAL_ERROR;
        offset += chunk;                     // 每完成一轮搬运，下一次用于zd25wq32的读取地址和内部Flash的写入地址都要偏移chunk
    }
    return HAL_OK;
}

/**
 * @brief 安全更新参数区（读-擦除-写）
 * @param new_param 新参数结构体指针
 */
HAL_StatusTypeDef IntFlash_UpdateParam(const Param_t *new_param)
{   
    Param_t backup;
    
    if (new_param == NULL)
        return HAL_ERROR;

    /* 备份原有参数，保证后续未显示覆盖的结构体成员不是垃圾值 */
    memcpy(&backup, (void *)PARAM_ADDR, sizeof(Param_t));

    /* 只覆盖需要更新的字段 */
    backup.magic      = new_param->magic;
    backup.status     = new_param->status;
    backup.fw_size    = new_param->fw_size;
    backup.fw_crc     = new_param->fw_crc;
    backup.fw_version = new_param->fw_version;

    /* 擦除参数页 */
    if (IntFlash_EraseParamArea() != HAL_OK)
        return HAL_ERROR;
    if (IntFlash_WriteBuffer(PARAM_ADDR, (const uint8_t *)&backup, sizeof(Param_t)) != HAL_OK)
        return HAL_ERROR;
    if (memcmp((void *)PARAM_ADDR, &backup, sizeof(Param_t)) != 0)
        return HAL_ERROR;
    return HAL_OK;
}

