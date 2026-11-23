/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_nintendo_nso.h @brief Nintendo Switch NSO0 executable reader. */

#ifndef XXFCLIB_FORMAT_NINTENDO_NSO_H
#define XXFCLIB_FORMAT_NINTENDO_NSO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_NSO_HEADER_SIZE UINT32_C(0x100)
#define XX_NSO_LOAD_ADDRESS UINT64_C(0x08000000)
#define XX_NSO_MAX_DECOMPRESSED_SIZE UINT32_C(0x20000000)

typedef enum xx_nso_segment_kind_e {
    XX_NSO_SEGMENT_TEXT = 0,
    XX_NSO_SEGMENT_READ_ONLY = 1,
    XX_NSO_SEGMENT_DATA = 2
} xx_nso_segment_kind;

typedef struct xx_nso_segment {
    uint32_t file_offset;
    uint32_t memory_offset;
    uint32_t compressed_size;
    uint32_t memory_size;
    uint32_t flags;
    bool compressed;
} xx_nso_segment;

typedef struct xx_nintendo_nso {
    Abstractformat format;
    uint32_t flags;
    uint32_t bss_size;
    xx_nso_segment segments[3];
} xx_nintendo_nso;

XXFC_API void xx_nintendo_nso_init(xx_nintendo_nso *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_nintendo_nso *xx_nintendo_nso_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nintendo_nso_destroy(xx_nintendo_nso *reader);
XXFC_API void xx_nintendo_nso_free(xx_nintendo_nso *reader);

XXFC_API bool xx_nintendo_nso_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_nso_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nintendo_nso_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_nso_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);
XXFC_API const xx_nso_segment *xx_nintendo_nso_get_segment(xx_nintendo_nso *reader, xx_nso_segment_kind kind, xx_pd_struct *pd);

/** Decode one section into a caller-owned buffer. The destination must be at
 * least segment.memory_size bytes. Uncompressed sections are copied directly. */
XXFC_API bool xx_nintendo_nso_decode_segment(xx_nintendo_nso *reader, xx_nso_segment_kind kind, void *destination, size_t destination_size,
                                             size_t *written, xx_pd_struct *pd);

static inline Abstractformat *xx_nintendo_nso_to_format(xx_nintendo_nso *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NINTENDO_NSO_H */
