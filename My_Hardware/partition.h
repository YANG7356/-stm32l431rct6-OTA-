#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>

/* ==================== 内部 Flash (256 KB) ==================== */
#define INT_FLASH_BASE          0x08000000UL
#define INT_FLASH_SIZE          (256U * 1024U)
#define INT_FLASH_END           (INT_FLASH_BASE + INT_FLASH_SIZE - 1U)

/* Bootloader */
#define BOOTLOADER_ADDR         INT_FLASH_BASE
#define BOOTLOADER_SIZE         (32U * 1024U)
#define BOOTLOADER_END          (BOOTLOADER_ADDR + BOOTLOADER_SIZE - 1U)

/* APP 区 */
#define APP_ADDR                (BOOTLOADER_ADDR + BOOTLOADER_SIZE)   /* 0x08008000 */
#define APP_SIZE                (216U * 1024U)
#define APP_END                 (APP_ADDR + APP_SIZE - 1U)

/* 参数区（单页 2 KB） */
#define PARAM_ADDR              (APP_ADDR + APP_SIZE)                 /* 0x0803E000 */
#define PARAM_PAGE_SIZE         (2U * 1024U)
#define PARAM_SIZE              PARAM_PAGE_SIZE
#define PARAM_END               (PARAM_ADDR + PARAM_SIZE - 1U)

/* ==================== 外部 Flash (ZD25WQ32, 4 MB) ==================== */
#define EXT_FLASH_BASE          0x00000000UL
#define EXT_FLASH_SIZE          (4U * 1024U * 1024U)
#define EXT_FLASH_END           (EXT_FLASH_BASE + EXT_FLASH_SIZE - 1U)

/* 固件下载缓存区 */
#define DOWNLOAD_CACHE_ADDR     EXT_FLASH_BASE
#define DOWNLOAD_CACHE_SIZE     (256U * 1024U)
#define DOWNLOAD_CACHE_END      (DOWNLOAD_CACHE_ADDR + DOWNLOAD_CACHE_SIZE - 1U)

/* 用户数据区（剩余空间） */
#define USER_DATA_ADDR          (DOWNLOAD_CACHE_ADDR + DOWNLOAD_CACHE_SIZE)
#define USER_DATA_SIZE          (EXT_FLASH_SIZE - DOWNLOAD_CACHE_SIZE)
#define USER_DATA_END           (USER_DATA_ADDR + USER_DATA_SIZE - 1U)

/* ==================== 升级状态定义 ==================== */
typedef enum
{
    UPGRADE_STATUS_IDLE             = 0x00U,    /* 空闲 */
    UPGRADE_STATUS_DOWNLOADING      = 0x01U,    /* 正在下载 */
    UPGRADE_STATUS_DOWNLOAD_DONE    = 0x02U,    /* 下载完成 */
    UPGRADE_STATUS_UPDATING         = 0x03U,    /* 正在升级（搬运中） */
    UPGRADE_STATUS_UPDATE_FAILED    = 0x04U     /* 升级失败 */
} UpgradeStatus_t;

/* 参数区结构体（总大小 32 字节，满足 8 字节对齐写入） */
typedef struct
{
    uint32_t magic;             /* 魔数 0x4F54414C ("OTAL") */
    uint32_t status;            /* UpgradeStatus_t，按 uint32_t 存储更稳 */
    uint32_t fw_size;           /* 新固件大小（字节） */
    uint32_t fw_crc;            /* 新固件 CRC32 校验值 */
    uint32_t fw_version;        /* 固件版本号 */
    uint32_t reserved[3];       /* 保留扩展，保证总大小为 8 的倍数 */
} Param_t;

/* 魔数定义 */
#define PARAM_MAGIC             0x4F54414CUL

/* 参数区直接访问指针 */
#define PARAM_PTR               ((volatile Param_t *)PARAM_ADDR)

#endif /* PARTITION_H */
