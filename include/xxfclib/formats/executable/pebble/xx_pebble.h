/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pebble.h @brief Pebble watch application reader. */
#ifndef XXFCLIB_FORMAT_PEBBLE_H
#define XXFCLIB_FORMAT_PEBBLE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_PEBBLE_APP_HEADER_SIZE UINT32_C(124)
#define XX_PEBBLE_APP_NAME_SIZE 32U
#define XX_PEBBLE_COMPANY_NAME_SIZE 32U
#define XX_PEBBLE_UUID_SIZE 16U
#define XX_PEBBLE_RELOCATION_SIZE UINT32_C(4)

typedef struct xx_pebble_version_s {
    uint8_t major;
    uint8_t minor;
} xx_pebble_version;

typedef struct xx_pebble_s {
    Abstractformat format;
    xx_pebble_version structure_version;
    xx_pebble_version sdk_version;
    xx_pebble_version application_version;
    uint16_t image_size;
    uint32_t entry_point;
    uint32_t image_crc;
    char application_name[XX_PEBBLE_APP_NAME_SIZE + 1U];
    char company_name[XX_PEBBLE_COMPANY_NAME_SIZE + 1U];
    uint32_t icon_resource_id;
    uint32_t symbol_table_address;
    uint32_t flags;
    uint32_t relocation_count;
    uint32_t relocation_offset;
    uint8_t uuid[XX_PEBBLE_UUID_SIZE];
    uint32_t header_crc;
} xx_pebble;

XXFC_API void xx_pebble_init(xx_pebble *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_pebble *xx_pebble_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pebble_destroy(xx_pebble *reader);
XXFC_API void xx_pebble_free(xx_pebble *reader);

XXFC_API bool xx_pebble_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_pebble_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_pebble_get_format_size(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_pebble_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

/** Read one little-endian relocation word from the validated trailing table. */
XXFC_API bool xx_pebble_get_relocation(xx_pebble *reader, uint32_t index, uint32_t *value, xx_pd_struct *pd);

static inline Abstractformat *xx_pebble_to_format(xx_pebble *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PEBBLE_H */
