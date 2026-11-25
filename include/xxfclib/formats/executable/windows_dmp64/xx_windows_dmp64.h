/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_windows_dmp64.h @brief Windows PAGEDU64 kernel crash-dump reader. */

#ifndef XXFCLIB_FORMAT_WINDOWS_DMP64_H
#define XXFCLIB_FORMAT_WINDOWS_DMP64_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DMP64_HEADER_SIZE UINT32_C(0x2000)
#define XX_DMP64_PAGE_SIZE UINT32_C(0x1000)
#define XX_DMP64_BITMAP_HEADER_SIZE UINT32_C(0x38)
#define XX_DMP64_DUMP_TYPE_FULL UINT32_C(1)
#define XX_DMP64_DUMP_TYPE_BITMAP_FULL UINT32_C(5)
#define XX_DMP64_DUMP_TYPE_BITMAP_KERNEL UINT32_C(6)

typedef struct xx_windows_dmp64_header {
    uint32_t major_version;
    uint32_t minor_version;
    uint64_t directory_table_base;
    uint64_t pfn_database;
    uint64_t loaded_module_list;
    uint64_t active_process_head;
    uint32_t machine_image_type;
    uint32_t number_of_processors;
    uint32_t bugcheck_code;
    uint64_t bugcheck_parameters[4];
    uint64_t debugger_data_block;
    uint32_t dump_type;
    uint64_t required_dump_space;
    uint32_t secondary_data_state;
    uint32_t product_type;
    uint32_t suite_mask;
} xx_windows_dmp64_header;

typedef struct xx_windows_dmp64_bitmap_header {
    uint64_t first_page_offset;
    uint64_t present_page_count;
    uint64_t addressable_page_count;
    uint64_t bitmap_size;
} xx_windows_dmp64_bitmap_header;

typedef struct xx_windows_dmp64_page {
    uint64_t address;
    int64_t file_offset;
} xx_windows_dmp64_page;

typedef struct xx_windows_dmp64 {
    Abstractformat format;
    xx_windows_dmp64_header header;
    xx_windows_dmp64_bitmap_header bitmap_header;
    xx_windows_dmp64_page *pages;
    uint64_t number_of_pages;
} xx_windows_dmp64;

XXFC_API void xx_windows_dmp64_init(xx_windows_dmp64 *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_windows_dmp64 *xx_windows_dmp64_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_windows_dmp64_destroy(xx_windows_dmp64 *reader);
XXFC_API void xx_windows_dmp64_free(xx_windows_dmp64 *reader);

XXFC_API bool xx_windows_dmp64_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_windows_dmp64_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_windows_dmp64_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_windows_dmp64_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_windows_dmp64_header *xx_windows_dmp64_get_header(xx_windows_dmp64 *reader, xx_pd_struct *pd);
XXFC_API const xx_windows_dmp64_bitmap_header *xx_windows_dmp64_get_bitmap_header(xx_windows_dmp64 *reader, xx_pd_struct *pd);
XXFC_API uint64_t xx_windows_dmp64_get_number_of_pages(xx_windows_dmp64 *reader, xx_pd_struct *pd);
XXFC_API const xx_windows_dmp64_page *xx_windows_dmp64_get_page(xx_windows_dmp64 *reader, uint64_t index, xx_pd_struct *pd);
XXFC_API bool xx_windows_dmp64_read_memory(xx_windows_dmp64 *reader, uint64_t address, void *buffer, size_t size, xx_pd_struct *pd);

static inline Abstractformat *xx_windows_dmp64_to_format(xx_windows_dmp64 *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WINDOWS_DMP64_H */
