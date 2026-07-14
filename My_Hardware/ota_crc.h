#ifndef __OTA_CRC_H__
#define __OTA_CRC_H__

#include <stdint.h>

uint32_t ota_crc32_init(void);
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t ota_crc32_finalize(uint32_t crc);
uint32_t ota_crc32_calc(const uint8_t *data, uint32_t len);
uint32_t ota_crc32_flash(uint32_t addr, uint32_t len);

#endif
