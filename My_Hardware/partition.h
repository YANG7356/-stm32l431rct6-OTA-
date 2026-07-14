#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>

/* ==================== 内部 Flash (256 KB) ==================== */
#define INT_FLASH_BASE          0x08000000UL
#define INT_FLASH_SIZE          (256 * 1024)

/* Bootloader */
#define BOOTLOADER_ADDR         INT_FLASH_BASE
#define BOOTLOADER_SIZE         (32 * 1024)         // 32 KB
#define BOOTLOADER_END          (BOOTLOADER_ADDR + BOOTLOADER_SIZE - 1)

/* APP 区 */
#define APP_ADDR                (BOOTLOADER_ADDR + BOOTLOADER_SIZE)  // 0x08008000
#define APP_SIZE                (216 * 1024)        // 216 KB
#define APP_END                 (APP_ADDR + APP_SIZE - 1)

/* 参数区（单页 2 KB） */
#define PARAM_ADDR              (APP_ADDR + APP_SIZE)   // 0x0803E000
#define PARAM_PAGE_SIZE         (2 * 1024)
#define PARAM_SIZE              PARAM_PAGE_SIZE
#define PARAM_END               (PARAM_ADDR + PARAM_SIZE - 1)

/* ==================== 外部 Flash (ZD25WQ32, 4 MB) ==================== */
#define EXT_FLASH_BASE          0x00000000UL
#define EXT_FLASH_SIZE          (4 * 1024 * 1024)

/* 固件下载缓存区 */
#define DOWNLOAD_CACHE_ADDR     EXT_FLASH_BASE          // 0x000000
#define DOWNLOAD_CACHE_SIZE     (256 * 1024)            // 256 KB
#define DOWNLOAD_CACHE_END      (DOWNLOAD_CACHE_ADDR + DOWNLOAD_CACHE_SIZE - 1)

/* 用户数据区（剩余空间） */
#define USER_DATA_ADDR          (DOWNLOAD_CACHE_ADDR + DOWNLOAD_CACHE_SIZE)
#define USER_DATA_SIZE          (EXT_FLASH_SIZE - DOWNLOAD_CACHE_SIZE)

/* ==================== 升级状态定义 ==================== */
typedef enum {
    UPGRADE_STATUS_IDLE             = 0x00,     // 空闲
    UPGRADE_STATUS_DOWNLOADING      = 0x01,     // 正在下载
    UPGRADE_STATUS_DOWNLOAD_DONE    = 0x02,     // 下载完成
    UPGRADE_STATUS_UPDATING         = 0x03,     // 正在升级（搬运中）
    UPGRADE_STATUS_UPDATE_FAILED    = 0x04,     // 升级失败
} UpgradeStatus_t;

/* 参数区结构体（必须双字对齐，建议总大小是8的倍数） */
typedef struct {
    uint32_t magic;             // 魔数 0x4F54414C ("OTAL")
    UpgradeStatus_t status;     // 当前升级状态
    uint32_t fw_size;           // 新固件大小（字节）
    uint32_t fw_crc;            // 新固件 CRC32 校验值
    uint32_t fw_version;        // 固件版本号（可选）
    uint32_t reserved[3];       // 保留，扩展用（保证结构体8字节对齐）
} Param_t;

/* 魔数定义 */
#define PARAM_MAGIC             0x4F54414C      // "OTAL"

/* 参数区存储的实例（通过指针访问） */
#define PARAM_PTR               ((volatile Param_t *)PARAM_ADDR)

#endif
