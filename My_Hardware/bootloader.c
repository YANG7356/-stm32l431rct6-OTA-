#include "bootloader.h"
#include "internal_flash.h"
#include "ota_crc.h"
#include "zd25wq32.h"
#include "string.h"


/* ==================== 本地静态函数声明 ==================== */
static uint8_t Bootloader_IsParamValid(const Param_t *param);
static uint8_t Bootloader_IsFwInfoValid(const Param_t *param);
static HAL_StatusTypeDef Bootloader_CheckDownloadImage(const Param_t *param);
static HAL_StatusTypeDef Bootloader_CheckAppImage(const Param_t *param);
static uint32_t Bootloader_CalcExternalCRC(uint32_t ext_addr, uint32_t len);

/* APP 跳转函数类型 */
typedef void (*pFunction)(void);

/* ==================== 对外接口实现 ==================== */

/**
 * @brief Bootloader 主流程
 * @note  建议在 main() 完成 HAL/时钟/GPIO/QSPI 初始化后调用
 */
void Bootloader_Main(void)
{
    Param_t param;

    /* 外部 Flash 相关硬件初始化 */
    ZD25WQ32_Init();

    /* 读取参数区 */
    if (Bootloader_ReadParam(&param) != HAL_OK)
    {
        /* 参数区读失败，尝试直接启动 APP */
        if (Bootloader_IsAppValid())
        {
            Bootloader_JumpToApp();
        }

        /* APP 也无效，则停在这里 */
        while (1)
        {
        }
    }

    /* 参数区无效：通常说明从未升级过，直接跳 APP */
    if (!Bootloader_IsParamValid(&param))
    {
        if (Bootloader_IsAppValid())
        {
            Bootloader_JumpToApp();
        }

        while (1)
        {
        }
    }

    /* 如果发现有待升级固件，则执行升级 */
    if (param.status == UPGRADE_STATUS_DOWNLOAD_DONE)
    {
        if (Bootloader_PerformUpgrade() == HAL_OK)
        {
            /* 升级成功后尝试跳转 APP */
            if (Bootloader_IsAppValid())
            {
                Bootloader_JumpToApp();
            }
        }
        else
        {
            /* 升级失败，如果原 APP 仍然有效，可以选择继续启动旧 APP */
            if (Bootloader_IsAppValid())
            {
                Bootloader_JumpToApp();
            }
        }

        /* 走到这里表示无法跳转 APP */
        while (1)
        {
        }
    }

    /* 如果当前不是待升级状态，则直接跳 APP */
    if (Bootloader_IsAppValid())
    {
        Bootloader_JumpToApp();
    }

    /* APP 无效，且没有可升级固件 */
    while (1)
    {
    }
}

/**
 * @brief 执行完整升级流程
 * @retval HAL_OK      升级成功
 * @retval HAL_ERROR   升级失败
 * @retval HAL_TIMEOUT 升级超时
 */
HAL_StatusTypeDef Bootloader_PerformUpgrade(void)
{
    Param_t param;
    HAL_StatusTypeDef ret;

    /* 重新读取参数，避免使用旧副本 */
    if (Bootloader_ReadParam(&param) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 检查参数合法性 */
    if (!Bootloader_IsParamValid(&param) || !Bootloader_IsFwInfoValid(&param))
    {
        return HAL_ERROR;
    }

    /* 1. 校验外部 Flash 下载区固件 */
    ret = Bootloader_CheckDownloadImage(&param);
    if (ret != HAL_OK)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        Bootloader_WriteParam(&param);
        return ret;
    }

    /* 2. 更新状态：正在升级 */
    param.status = UPGRADE_STATUS_UPDATING;
    ret = Bootloader_WriteParam(&param);
    if (ret != HAL_OK)
    {
        return ret;
    }

    /* 3. 擦除 APP 区 */
    ret = IntFlash_EraseAppArea();
    if (ret != HAL_OK)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        Bootloader_WriteParam(&param);
        return ret;
    }

    /* 4. 从外部 Flash 搬运到内部 APP 区 */
    ret = IntFlash_WriteAppFromExternal(DOWNLOAD_CACHE_ADDR, param.fw_size);
    if (ret != HAL_OK)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        Bootloader_WriteParam(&param);
        return ret;
    }

    /* 5. 校验内部 APP 区 */
    ret = Bootloader_CheckAppImage(&param);
    if (ret != HAL_OK)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        Bootloader_WriteParam(&param);
        return ret;
    }

    /* 6. 升级成功，恢复为空闲状态 */
    param.status = UPGRADE_STATUS_IDLE;
    ret = Bootloader_WriteParam(&param);
    if (ret != HAL_OK)
    {
        return ret;
    }

    return HAL_OK;
}

/**
 * @brief 检查 APP 是否有效
 * @retval 1 有效
 * @retval 0 无效
 */
uint8_t Bootloader_IsAppValid(void)
{
    uint32_t app_stack = *(volatile uint32_t *)APP_ADDR;
    uint32_t app_reset = *(volatile uint32_t *)(APP_ADDR + 4);

    /* 1. MSP 必须落在 SRAM 区间
     * STM32L431 SRAM 地址一般从 0x20000000 开始
     * 这里用较宽松的范围判断
     */
    if ((app_stack < 0x20000000UL) || (app_stack > 0x20018000UL))
    {
        return 0;
    }

    /* 2. Reset_Handler 必须落在 APP 区内 */
    if ((app_reset < APP_ADDR) || (app_reset > APP_END))
    {
        return 0;
    }

    /* 3. 避免全 FF / 全 00 情况 */
    if ((app_stack == 0xFFFFFFFFUL) || (app_stack == 0x00000000UL))
    {
        return 0;
    }

    if ((app_reset == 0xFFFFFFFFUL) || (app_reset == 0x00000000UL))
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 跳转到 APP
 */
void Bootloader_JumpToApp(void)
{
    uint32_t app_stack;
    uint32_t app_entry;
    pFunction JumpToApp;

    if (!Bootloader_IsAppValid())
    {
        return;
    }

    app_stack = *(volatile uint32_t *)APP_ADDR;
    app_entry = *(volatile uint32_t *)(APP_ADDR + 4);

    JumpToApp = (pFunction)app_entry;

    __disable_irq();

    /* 关闭 SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 反初始化时钟和外设 */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* 清中断使能和挂起 */
    for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* 切换中断向量表到 APP */
    SCB->VTOR = APP_ADDR;

    /* 设置主栈指针 */
    __set_MSP(app_stack);

    /* 跳转 */
    JumpToApp();

    while (1)
    {
    }
}

/**
 * @brief 读取参数区
 */
HAL_StatusTypeDef Bootloader_ReadParam(Param_t *param)
{
    if (param == NULL)
    {
        return HAL_ERROR;
    }

    memcpy(param, (const void *)PARAM_ADDR, sizeof(Param_t));
    return HAL_OK;
}

/**
 * @brief 写回参数区
 */
HAL_StatusTypeDef Bootloader_WriteParam(Param_t *param)
{
    if (param == NULL)
    {
        return HAL_ERROR;
    }

    return IntFlash_UpdateParam(param);
}

/**
 * @brief 重置参数区为默认值
 */
HAL_StatusTypeDef Bootloader_ResetParam(void)
{
    Param_t param;

    memset(&param, 0xFF, sizeof(param));

    /* 这里按你的参数约定重新写默认值 */
    param.magic      = PARAM_MAGIC;
    param.status     = UPGRADE_STATUS_IDLE;
    param.fw_size    = 0;
    param.fw_crc     = 0;
    param.fw_version = 0;
    param.reserved[0] = 0xFFFFFFFF;
    param.reserved[1] = 0xFFFFFFFF;
    param.reserved[2] = 0xFFFFFFFF;

    return IntFlash_UpdateParam(&param);
}

/* ==================== 本地静态函数实现 ==================== */

/**
 * @brief 检查参数区是否有效
 */
static uint8_t Bootloader_IsParamValid(const Param_t *param)
{
    if (param == NULL)
    {
        return 0;
    }

    if (param->magic != PARAM_MAGIC)
    {
        return 0;
    }

    switch (param->status)
    {
        case UPGRADE_STATUS_IDLE:
        case UPGRADE_STATUS_DOWNLOADING:
        case UPGRADE_STATUS_DOWNLOAD_DONE:
        case UPGRADE_STATUS_UPDATING:
        case UPGRADE_STATUS_UPDATE_FAILED:
            break;
        default:
            return 0;
    }

    return 1;
}

/**
 * @brief 检查固件信息是否合理
 */
static uint8_t Bootloader_IsFwInfoValid(const Param_t *param)
{
    if (param == NULL)
    {
        return 0;
    }

    if (param->fw_size == 0)
    {
        return 0;
    }

    if (param->fw_size > APP_SIZE)
    {
        return 0;
    }

    if (param->fw_size > DOWNLOAD_CACHE_SIZE)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 校验下载区镜像 CRC
 */
static HAL_StatusTypeDef Bootloader_CheckDownloadImage(const Param_t *param)
{
    uint32_t calc_crc;

    calc_crc = Bootloader_CalcExternalCRC(DOWNLOAD_CACHE_ADDR, param->fw_size);
    if (calc_crc != param->fw_crc)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief 校验 APP 区镜像 CRC
 */
static HAL_StatusTypeDef Bootloader_CheckAppImage(const Param_t *param)
{
    uint32_t calc_crc;

    calc_crc = ota_crc32_flash(APP_ADDR, param->fw_size);
    if (calc_crc != param->fw_crc)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief 分块计算外部 Flash 指定区域 CRC32
 */
static uint32_t Bootloader_CalcExternalCRC(uint32_t ext_addr, uint32_t len)
{
    uint8_t buf[256];
    uint32_t crc = ota_crc32_init();
    uint32_t offset = 0;

    while (offset < len)
    {
        uint16_t chunk = (len - offset > sizeof(buf)) ? sizeof(buf) : (uint16_t)(len - offset);

        if (ZD25WQ32_Read(ext_addr + offset, buf, chunk) != HAL_OK)
        {
            return 0; /* 注意：0 可能与真实 CRC 冲突，但概率极低。
                         更严格的做法是把状态单独返回。 */
        }

        crc = ota_crc32_update(crc, buf, chunk);
        offset += chunk;
    }

    return ota_crc32_finalize(crc);
}
