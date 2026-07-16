#include "bootloader.h"
#include "internal_flash.h"
#include "ota_crc.h"
#include "zd25wq32.h"
#include <string.h>

/* APP 入口函数类型 */
typedef void (*pFunction)(void);

/* 内部静态函数声明 */
static uint8_t Bootloader_IsParamValid(const Param_t *param); // 判断参数区的参数是否合法
static void Bootloader_DeInitBeforeJump(void);

/**
 * @brief Bootloader 主流程
 * @note  建议在 main() 初始化完 HAL / 时钟 / QSPI / 串口后调用
 */
void Bootloader_Run(void)
{
    const Param_t *param = (const Param_t *)PARAM_ADDR; // 读取参数区的内容

    /* 1. 参数区无效则初始化（一般第一次上电时参数区都是无效的） */
    if (!Bootloader_IsParamValid(param))
    {
        if (Bootloader_InitParamArea() != HAL_OK)
        {
            /* 参数区初始化失败，停留 Bootloader */
            return;
        }

        param = (const Param_t *)PARAM_ADDR; // 读取初始化后的参数区
    }

    /* 2. 若下载完成，执行升级 */
    if (param->status == UPGRADE_STATUS_DOWNLOAD_DONE)
    {
        if (Bootloader_DoUpgrade() == HAL_OK) //执行OTA升级任务
        {
            /* 升级成功后，如 APP 有效则跳转 */
            if (Bootloader_IsAppValid())
            {
                Bootloader_JumpToApp();
            }
        }

        /* 失败则停留在 Bootloader */
        return;
    }

    /* 3. 没有升级任务，但 APP 有效，直接跳转 */
    if (Bootloader_IsAppValid())
    {
        Bootloader_JumpToApp();
    }

    /* 4. 否则留在 Bootloader */
}

/**
 * @brief 判断参数区内容是否合法
 */
static uint8_t Bootloader_IsParamValid(const Param_t *param)
{
    if (param == NULL)
        return 0;

    if (param->magic != PARAM_MAGIC)
        return 0;

    switch (param->status)
    {
        case UPGRADE_STATUS_IDLE:
        case UPGRADE_STATUS_DOWNLOADING:
        case UPGRADE_STATUS_DOWNLOAD_DONE:
        case UPGRADE_STATUS_UPDATING:
        case UPGRADE_STATUS_UPDATE_FAILED:
            return 1;

        default:
            return 0;
    }
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
    param.fw_size    = 0;
    param.fw_crc     = 0;
    param.fw_version = 0;

    return IntFlash_UpdateParam(&param);
}

/**
 * @brief 检查 APP 是否有效
 * @return 1=有效，0=无效
 * @note  检查：
 *        - 初始栈顶是否位于 SRAM
 *        - Reset_Handler 是否位于 APP 区间
 */
uint8_t Bootloader_IsAppValid(void)
{
    uint32_t app_sp    = *(__IO uint32_t *)APP_ADDR;          //初始化指针
    uint32_t app_reset = *(__IO uint32_t *)(APP_ADDR + 4U);

    /* 栈顶是否落在 SRAM 区
       经典判断方式，适合 Cortex-M */
    if ((app_sp & 0x2FFE0000U) != 0x20000000U)
        return 0;

    /* 复位向量是否落在 APP 分区 */
    if ((app_reset < APP_ADDR) || (app_reset > APP_END))
        return 0;

    return 1;
}

/**
 * @brief 执行 OTA 升级
 * @return HAL_OK 表示升级成功
 */
HAL_StatusTypeDef Bootloader_DoUpgrade(void)
{
    Param_t param;
    uint32_t calc_crc;

    /* 读出当前参数 */
    memcpy(&param, (const void *)PARAM_ADDR, sizeof(Param_t));

    /* 基本合法性检查 */
    /* 魔数不对 */
    if (param.magic != PARAM_MAGIC) 
        return HAL_ERROR;
    
    /* 无数据或数据超过app区 */
    if (param.fw_size == 0U || param.fw_size > APP_SIZE)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }
    
    /* 数据超过下载区 */
    if (param.fw_size > DOWNLOAD_CACHE_SIZE)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    /* 状态改为 upgrading */
    param.status = UPGRADE_STATUS_UPDATING;
    if (IntFlash_UpdateParam(&param) != HAL_OK)
        return HAL_ERROR;

    /* 擦除 APP 区 */
    if (IntFlash_EraseAppArea() != HAL_OK)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    /* 从外部 Flash 搬运到内部 APP 区 */
    if (IntFlash_WriteAppFromExternal(DOWNLOAD_CACHE_ADDR, param.fw_size) != HAL_OK)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    /* 对内部 APP 重新计算 CRC 校验 */
    calc_crc = ota_crc32_flash(APP_ADDR, param.fw_size);
    if (calc_crc != param.fw_crc)
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    /* 再次检查 APP 向量表基本有效性 */
    if (!Bootloader_IsAppValid())
    {
        param.status = UPGRADE_STATUS_UPDATE_FAILED;
        IntFlash_UpdateParam(&param);
        return HAL_ERROR;
    }

    /* 升级成功，回到 idle */
    param.status = UPGRADE_STATUS_IDLE;
    if (IntFlash_UpdateParam(&param) != HAL_OK)
        return HAL_ERROR;

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
        return;

    /* 关闭systick，清除中断 */
    Bootloader_DeInitBeforeJump();

    /* 重定位中断向量表到 APP */
    SCB->VTOR = APP_ADDR;

    /* 设置 MSP 为 APP 栈顶 */
    __set_MSP(app_stack);

    /* 跳转到 APP Reset_Handler */
    JumpToApplication();
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
