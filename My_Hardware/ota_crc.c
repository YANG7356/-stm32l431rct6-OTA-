#include "ota_crc.h"

/**
 * @brief 初始化 CRC-32 计算，返回标准初始值
 * @return 初始 CRC 值 0xFFFFFFFF
 */
uint32_t ota_crc32_init(void)
{
    return 0xFFFFFFFF;   // CRC-32/ISO-HDLC 规定的初始值
}

/**
 * @brief 逐字节更新 CRC-32 值（逐位计算法）
 * @param crc   当前 CRC 值
 * @param data  待计算的数据指针
 * @param len   数据长度（字节）
 * @return 更新后的 CRC 值
 * @note  采用反射多项式 0xEDB88320，与标准 CRC-32 兼容
 */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];                 // 将当前字节并入 CRC
        for (uint32_t j = 0; j < 8; j++) // 处理该字节的 8 个位（最低位优先）
        {
            if (crc & 1)                // 若最低位为 1
                crc = (crc >> 1) ^ 0xEDB88320; // 右移并异或反射多项式
            else
                crc >>= 1;              // 否则仅右移
        }
    }
    return crc;
}

/**
 * @brief 完成 CRC-32 计算，执行最终异或
 * @param crc   update 后的 CRC 值
 * @return 最终 CRC-32 校验值
 */
uint32_t ota_crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFF;   // 标准 CRC-32 要求最终异或全 1
}

/**
 * @brief 一次性计算数据块的 CRC-32（适合 RAM 中完整数据）
 * @param data  数据指针
 * @param len   数据长度（字节）
 * @return 标准 CRC-32 值
 */
uint32_t ota_crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = ota_crc32_init();
    crc = ota_crc32_update(crc, data, len);
    return ota_crc32_finalize(crc);
}

/**
 * @brief 直接从内部 Flash 地址计算 CRC-32
 * @param addr  Flash 起始地址（必须是可直接读取的内存映射地址）
 * @param len   数据长度（字节）
 * @return 标准 CRC-32 值
 * @note  用于搬运后校验内部 APP 区固件，无需额外缓冲区
 */
uint32_t ota_crc32_flash(uint32_t addr, uint32_t len)
{
    uint32_t crc = ota_crc32_init();
    uint8_t *p = (uint8_t *)addr;        // 将 Flash 地址视为字节指针

    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= p[i];                     // 逐字节读取并更新 CRC
        for (uint32_t j = 0; j < 8; j++) // 逐位处理，逻辑与 ota_crc32_update 相同
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ota_crc32_finalize(crc);
}
