/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_NE_H
#define XXFCLIB_FORMAT_NE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_NE_SIGNATURE UINT16_C(0x454e)

typedef enum xx_ne_data_struct_id_e {
    XX_NE_DATA_STRUCT_UNKNOWN = 0,
    XX_NE_DATA_STRUCT_DOS_HEADER,
    XX_NE_DATA_STRUCT_HEADER,
    XX_NE_DATA_STRUCT_SEGMENT_TABLE,
    XX_NE_DATA_STRUCT_RESOURCE_TABLE,
    XX_NE_DATA_STRUCT_ENTRY_TABLE,
    XX_NE_DATA_STRUCT_NONRESIDENT_NAME_TABLE
} xx_ne_data_struct_id_t;

typedef struct xx_ne_segment_s {
    uint16_t sector_offset;
    uint16_t file_size_field;
    uint16_t flags;
    uint16_t minimum_allocation;
    int64_t file_offset;
    int64_t file_size;
} xx_ne_segment;

typedef struct xx_ne {
    Abstractformat format;
    uint32_t ne_offset;
    uint16_t entry_table_offset;
    uint16_t entry_table_size;
    uint16_t flags;
    uint32_t initial_cs_ip;
    uint32_t initial_ss_sp;
    uint16_t segment_count;
    uint16_t module_reference_count;
    uint16_t nonresident_name_table_size;
    uint16_t segment_table_offset;
    uint16_t resource_table_offset;
    uint16_t resident_name_table_offset;
    uint16_t module_reference_table_offset;
    uint16_t imported_name_table_offset;
    uint32_t nonresident_name_table_offset;
    uint16_t movable_entry_count;
    uint16_t alignment_shift;
    uint16_t resource_segment_count;
    uint8_t target_os;
    uint8_t other_flags;
    uint16_t expected_version;
    xx_ne_segment *segments;
} xx_ne;

typedef xx_ne xx_ne_t;
typedef xx_ne XNE;

XXFC_API void xx_ne_init(xx_ne *ne, xx_io_device *device,
                         int64_t base_address);
XXFC_API xx_ne *xx_ne_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ne_destroy(xx_ne *ne);
XXFC_API void xx_ne_free(xx_ne *ne);

XXFC_API bool xx_ne_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ne_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ne_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_ne_get_number_of_imports(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_ne_get_number_of_exports(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_ne_get_number_of_resources(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_ne_get_memory_map(Abstractformat *self,
                                   xx_memory_map_mode_t mode,
                                   xx_memory_map *output,
                                   xx_pd_struct *pd);

XXFC_API uint16_t xx_ne_get_number_of_segments(const xx_ne *ne);
XXFC_API const xx_ne_segment *xx_ne_get_segment(const xx_ne *ne,
                                                uint16_t index);

static inline Abstractformat *xx_ne_to_format(xx_ne *ne) {
    return ne ? &ne->format : NULL;
}

static inline const Abstractformat *xx_ne_to_format_const(const xx_ne *ne) {
    return ne ? &ne->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NE_H */
