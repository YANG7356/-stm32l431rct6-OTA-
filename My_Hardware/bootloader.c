#include "bootloader.h"
#include "internal_flash.h"
#include "ota_crc.h"
#include "zd25wq32.h"
#include <string.h>
#include <stdio.h>

/* APP 入口函数类型 */
typedef void (*pFunction)(void);

/* 内部静态函数声明 */
static uint8_t Bootloader_IsParamValid(const Param_t *param);
static uint8_t Bootloader_IsUpgradeInfoValid(const Param_t *param);
static void Bootloader_DeInitBeforeJump(void);
static uint32_t Bootloader_CalcExtFlashCrc(uint32_t ext_addr, uint32_t size);
static void Bootloader_PrintParam(const Param_t *param);

/**
 * @brief Bootloader 主流程
 * @note  建议在 main() 初始化完 HAL / 时钟 / QSPI / 串口后调用
 */
void Bootloader_Run(void)
{
    Param_t param;

    memcpy(&param, (const void *)PARAM_ADDR, sizeof(Param_t));
    Bootloader_PrintParam(&param);

    /* 1. 参数区无效则初始化 */
    if (!Bootloader_IsParamValid(&param))
    {
        printf("Param invalid, init...\r\n");

        if (Bootloader_InitParamArea() != HAL_OK)
        {
            printf("Param init fail!\r\n");
            return;
        }

        printf("Param init success!\r\n");
        memcpy(&param, (const void *)PARAM_ADDR, sizeof(Param_t));
        Bootloader_PrintParam(&param);
    }

    /* 2. 若下载完成，先校验下载区，再决定是否执行升级 */
    if (param.status == UPGRADE_STATUS_DOWNLOAD_DONE)
    {
        uint32_t ext_crc;

        printf("Upgrade request detected\r\n");

        if (!Bootloader_IsUpgradeInfoValid(&param))
        {
            printf("Upgrade info invalid\r\n");
            param.status = UPGRADE_STATUS_UPDATE_FAILED;
            IntFlash_UpdateParam(&param);

            if (Bootloader_IsAppValid())
            {
                printf("Current APP still valid, jump to APP\r\n");
                Bootloader_JumpToApp();
            }

            printf("No valid APP, stay in Bootloader\r\n");
            return;
        }

        /* 先校验外部 Flash 下载区内容，避免假参数直接破坏 APP */
        printf("Verify external firmware CRC...\r\n");
        ext_crc = Bootloader_CalcExtFlashCrc(DOWNLOAD_CACHE_ADDR, param.fw_size);
        printf("Expected CRC = 0x%08lX, Ext CRC = 0x%08lX\r\n", param.fw_crc, ext_crc);

        if (ext_crc != param.fw_crc)
        {
            printf("External firmware CRC check failed!\r\n");
            param.status = UPGRADE_STATUS_UPDATE_FAILED;
            IntFlash_UpdateParam(&param);

            /* 外部升级包无效，不擦 APP，优先尝试原 APP */
            if (Bootloader_IsAppValid())
            {
                printf("Current APP still valid, jump to APP\r\n");
                Bootloader_JumpToApp();
            }

            printf("No valid APP, stay in Bootloader\r\n");
            return;
        }

        printf("External firmware CRC check passed\r\n");

        if (Bootloader_DoUpgrade() == HAL_OK)
        {
            printf("OTA upgrade success, redirecting to APP...\r\n");

            if (Bootloader_IsAppValid())
            {
                Bootloader_JumpToApp();
            }

            printf("APP invalid after upgrade, stay in Bootloader\r\n");
            return;
        }

        printf("OTA upgrade failed!\r\n");

        /* 升级失败后，如果 APP 仍有效，则尝试跳回 */
        if (Bootloader_IsAppValid())
        {
            printf("Current APP still valid, jump to APP\r\n");
            Bootloader_JumpToApp();
        }

        printf("No valid APP, stay in Bootloader\r\n");
        return;
    }

    /* 3. 没有升级任务，但 APP 有效，直接跳转 */
    if (Bootloader_IsAppValid())
    {
        printf("No upgrade task, jump to APP\r\n");
        Bootloader_JumpToApp();
    }

    /* 4. 否则留在 Bootloader */
    printf("No valid APP, stay in Bootloader\r\n");
}

/**
 * @brief 判断参数区内容是否合法
 */
static uint8_t Bootloader_IsParamValid(const Param_t *param)
{
    if (param == NULL)
        return 0U;

    if (param->magic != PARAM_MAGIC)
        return 0U;

    switch (param->status)
    {
        case UPGRADE_STATUS_IDLE:
        case UPGRADE_STATUS_DOWNLOADING:
        case UPGRADE_STATUS_DOWNLOAD_DONE:
        case UPGRADE_STATUS_UPDATING:
        case UPGRADE_STATUS_UPDATE_FAILED:
            return 1U;

        default:
            return 0U;
    }
    
}

/**
 * @brief 判断升级信息是否合法
 * @note  这里只检查“升级参数本身”，不检查外部 Flash 实际内容
 */
static uint8_t Bootloader_IsUpgradeInfoValid(const Param_t *param)
{
    if (param == NULL)
        return 0U;

    if (param->magic != PARAM_MAGIC)
        return 0U;

    if (param->status != UPGRADE_STATUS_DOWNLOAD_DONE)
        return 0U;

    if (param->fw_size == 0U)
        return 0U;

    if (param->fw_size > APP_SIZE)
        return 0U;

    if (param->fw_size > DOWNLOAD_CACHE_SIZE)
        return 0U;

    return 1U;
}

/**
 * @brief 初始化参数区
 * @return HAL 状态
 */
HAL_StatusTypeDef Bootloader_InitParamArea(void)
{
    Param_t param;

    memset(&param, 0, sizeof(param));
    param.magic      = PARAM_MAGIC;
    param.status     = UPGRADE_STATUS_IDLE;
    param.fw_size    = 0U;
    param.fw_crc     = 0U;
    param.fw_version = 0U;

    return IntFlash_UpdateParam(&param);
}

/**
 * @brief 检查 APP 是否有效
 * @return 1=有效，0=无效
 */
uint8_t Bootloader_IsAppValid(void)
{
    uint32_t app_sp       = *(__IO uint32_t *)APP_ADDR;
    uint32_t app_reset    = *(__IO uint32_t *)(APP_ADDR + 4U);
    uint32_t app_reset_pc = app_reset & 0xFFFFFFFEU;

    if ((app_sp & 0x2FFE0000U) != 0x20000000U)
        return 0;

    if ((app_reset & 0x1U) == 0U)
        return 0;

    if ((app_reset_pc < APP_ADDR) || (app_reset_pc > APP_END))
        return 0;

    return 1;
}

/**
 * @brief 计算外部 Flash 指定区域 CRC32
 * @param ext_addr 外部 Flash 起始地址
 * @param size     数据长度
 * @return CRC32，失败返回 0xFFFFFFFF
 */
static uint32_t Bootloader_CalcExtFlashCrc(uint32_t ext_addr, uint32_t size)
{
    uint8_t  buf[256];
    uint32_t offset = 0U;
    uint32_t chunk;
    uint32_t crc;

    if (size == 0U)
        return 0xFFFFFFFFU;

    crc = ota_crc32_init();

    while (offset < size)
    {
        chunk = size - offset;
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);

        if (ZD25WQ32_Read(ext_addr + offset, buf, chunk) != HAL_OK)
        {
            printf("Ext flash read failed at 0x%08lX\r\n", ext_addr + offset);
            return 0xFFFFFFFFU;
        }

        crc = ota_crc32_update(crc, buf, chunk);
        offset += chunk;
    }

    return ota_crc32_finalize(crc);
}

/**
 * @brief 执行 OTA 升级
 * @return HAL_OK 表示升级成功
 */
HAL_StatusTypeDef Bootloader_DoUpgrade(void)
{
    Param_t param;
    uint32_t calc_crc;

    memcpy(&param, (const void *)PARAM_ADDR, sizeof(Param_t));

    printf("Bootloader_DoUpgrade start\r\n");

    /* 基本合法性检查 */
    if (!Bootloader_IsUpgradeInfoValid(&param))
    {
        printf("Upgrade info invalid in DoUpgrade\r\n");
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    /* 状态改为 updating */
    param.status = UPGRADE_STATUS_UPDATING;
    if (IntFlash_UpdateParam(&param) != HAL_OK)
    {
        printf("Set status UPDATING failed!\r\n");
        return HAL_ERROR;
    }

    printf("Erase APP area...\r\n");
    if (IntFlash_EraseAppArea() != HAL_OK)
    {
        printf("Internal flash erase failed!\r\n");
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    printf("Copy firmware from ext flash to APP...\r\n");
    if (IntFlash_WriteAppFromExternal(DOWNLOAD_CACHE_ADDR, param.fw_size) != HAL_OK)
    {
        printf("Firmware move failed!\r\n");
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    printf("Verify APP CRC...\r\n");
    calc_crc = ota_crc32_flash(APP_ADDR, param.fw_size);
    printf("Expected CRC = 0x%08lX, APP CRC = 0x%08lX\r\n", param.fw_crc, calc_crc);

    if (calc_crc != param.fw_crc)
    {
        printf("APP CRC check failed!\r\n");
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    printf("Verify APP vector table...\r\n");
    if (!Bootloader_IsAppValid())
    {
        printf("APP vector invalid after upgrade!\r\n");
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    /* 升级成功，回到 idle */
    param.status = UPGRADE_STATUS_IDLE;
    if (IntFlash_UpdateParam(&param) != HAL_OK)
    {
        printf("Set status IDLE failed!\r\n");
        return HAL_ERROR;
    }

    printf("Bootloader_DoUpgrade success\r\n");
    return HAL_OK;
}

/**
 * @brief 跳转到 APP
 */
void Bootloader_JumpToApp(void)
{
    uint32_t app_stack = *(__IO uint32_t *)APP_ADDR;
    uint32_t app_entry = *(__IO uint32_t *)(APP_ADDR + 4U);
    pFunction JumpToApplication = (pFunction)app_entry;

    if (!Bootloader_IsAppValid())
    {
        printf("Jump aborted: APP invalid\r\n");
        return;
    }

    printf("Jumping to APP: SP=0x%08lX, Reset=0x%08lX\r\n", app_stack, app_entry);

    /* 跳转前反初始化 */
    Bootloader_DeInitBeforeJump();

    /* 重定位中断向量表到 APP */
    SCB->VTOR = APP_ADDR;

    /* 设置 MSP 为 APP 栈顶 */
    __DSB();
    __ISB();
    __set_MSP(app_stack);
    __DSB();
    __ISB();

    /* 跳转到 APP Reset_Handler */
    JumpToApplication();
}

/**
 * @brief 打印参数区
 */
static void Bootloader_PrintParam(const Param_t *param)
{
    if (param == NULL)
        return;

    printf("param.magic      = 0x%08lX\r\n", param->magic);
    printf("param.status     = %lu\r\n", param->status);
    printf("param.fw_size    = %lu\r\n", param->fw_size);
    printf("param.fw_crc     = 0x%08lX\r\n", param->fw_crc);
    printf("param.fw_version = %lu\r\n", param->fw_version);
}

/**
 * @brief 跳转前反初始化
 */
static void Bootloader_DeInitBeforeJump(void)
{
    __disable_irq();

    /* 关闭 SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 关闭并清除所有中断 */
    for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    HAL_RCC_DeInit();
    HAL_DeInit();
}
