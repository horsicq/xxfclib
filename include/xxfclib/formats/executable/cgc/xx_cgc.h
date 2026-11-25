/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_cgc.h @brief DARPA Cyber Grand Challenge executable reader. */

#ifndef XXFCLIB_FORMAT_CGC_H
#define XXFCLIB_FORMAT_CGC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_CGC_HEADER_SIZE UINT32_C(52)
#define XX_CGC_PROGRAM_HEADER_SIZE UINT32_C(32)
#define XX_CGC_SECTION_HEADER_SIZE UINT32_C(40)
#define XX_CGC_PT_LOAD UINT32_C(1)

typedef struct xx_cgc_header {
    uint8_t data_encoding;
    uint8_t abi;
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t program_header_offset;
    uint32_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} xx_cgc_header;

typedef struct xx_cgc_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t virtual_address;
    uint32_t physical_address;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t flags;
    uint32_t alignment;
} xx_cgc_program_header;

typedef struct xx_cgc {
    Abstractformat format;
    xx_cgc_header header;
    xx_cgc_program_header *program_headers;
    uint16_t number_of_program_headers;
} xx_cgc;

XXFC_API void xx_cgc_init(xx_cgc *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_cgc *xx_cgc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cgc_destroy(xx_cgc *reader);
XXFC_API void xx_cgc_free(xx_cgc *reader);

XXFC_API bool xx_cgc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cgc_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cgc_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cgc_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_cgc_header *xx_cgc_get_header(xx_cgc *reader, xx_pd_struct *pd);
XXFC_API uint16_t xx_cgc_get_number_of_program_headers(xx_cgc *reader, xx_pd_struct *pd);
XXFC_API const xx_cgc_program_header *xx_cgc_get_program_header(xx_cgc *reader, uint16_t index, xx_pd_struct *pd);

static inline Abstractformat *xx_cgc_to_format(xx_cgc *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CGC_H */
