/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_nintendo_nro.h @brief Nintendo Switch NRO0 executable reader. */

#ifndef XXFCLIB_FORMAT_NINTENDO_NRO_H
#define XXFCLIB_FORMAT_NINTENDO_NRO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_NRO_HEADER_OFFSET UINT32_C(0x10)
#define XX_NRO_HEADER_SIZE UINT32_C(0x70)
#define XX_NRO_ENTRY_OFFSET UINT32_C(0x80)
#define XX_NRO_MAX_MEMORY_SIZE UINT32_C(0x20000000)

typedef enum xx_nro_segment_kind_e {
    XX_NRO_SEGMENT_TEXT = 0,
    XX_NRO_SEGMENT_READ_ONLY = 1,
    XX_NRO_SEGMENT_DATA = 2
} xx_nro_segment_kind;

typedef struct xx_nro_segment {
    uint32_t file_offset;
    uint32_t memory_offset;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t flags;
} xx_nro_segment;

typedef struct xx_nintendo_nro {
    Abstractformat format;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t module_offset;
    uint32_t asset_offset;
    bool has_asset_header;
    xx_nro_segment segments[3];
} xx_nintendo_nro;

XXFC_API void xx_nintendo_nro_init(xx_nintendo_nro *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_nintendo_nro *xx_nintendo_nro_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nintendo_nro_destroy(xx_nintendo_nro *reader);
XXFC_API void xx_nintendo_nro_free(xx_nintendo_nro *reader);

XXFC_API bool xx_nintendo_nro_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_nro_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nintendo_nro_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_nro_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);
XXFC_API const xx_nro_segment *xx_nintendo_nro_get_segment(xx_nintendo_nro *reader, xx_nro_segment_kind kind, xx_pd_struct *pd);

static inline Abstractformat *xx_nintendo_nro_to_format(xx_nintendo_nro *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NINTENDO_NRO_H */
