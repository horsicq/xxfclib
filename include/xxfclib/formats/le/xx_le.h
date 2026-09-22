/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_LE_H
#define XXFCLIB_FORMAT_LE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LE_SIGNATURE UINT16_C(0x454c)
#define XX_LX_SIGNATURE UINT16_C(0x584c)

typedef enum xx_linear_data_struct_id_e {
    XX_LINEAR_DATA_STRUCT_UNKNOWN = 0,
    XX_LINEAR_DATA_STRUCT_DOS_HEADER,
    XX_LINEAR_DATA_STRUCT_HEADER,
    XX_LINEAR_DATA_STRUCT_OBJECT_TABLE,
    XX_LINEAR_DATA_STRUCT_PAGE_MAP,
    XX_LINEAR_DATA_STRUCT_RESOURCE_TABLE,
    XX_LINEAR_DATA_STRUCT_NONRESIDENT_NAME_TABLE,
    XX_LINEAR_DATA_STRUCT_DEBUG_DATA
} xx_linear_data_struct_id_t;

typedef struct xx_linear_object_s {
    uint32_t virtual_size;
    uint32_t base_address;
    uint32_t flags;
    uint32_t first_page;
    uint32_t page_count;
    uint32_t reserved;
} xx_linear_object;

typedef struct xx_linear_page_s {
    uint32_t data_offset;
    uint16_t data_size;
    uint16_t flags;
} xx_linear_page;

typedef struct xx_linear_executable {
    Abstractformat format;
    uint16_t expected_signature;
    uint16_t signature;
    uint32_t linear_offset;
    uint8_t byte_order;
    uint8_t word_order;
    uint16_t cpu_type;
    uint16_t target_os;
    uint32_t module_version;
    uint32_t module_flags;
    uint32_t module_page_count;
    uint32_t start_object;
    uint32_t entry_offset;
    uint32_t stack_object;
    uint32_t stack_offset;
    uint32_t page_size;
    uint32_t last_page_size_or_shift;
    uint32_t fixup_size;
    uint32_t loader_size;
    uint32_t object_table_offset;
    uint32_t object_count;
    uint32_t object_page_map_offset;
    uint32_t resource_table_offset;
    uint32_t resource_count;
    uint32_t entry_table_offset;
    uint32_t fixup_page_table_offset;
    uint32_t fixup_record_table_offset;
    uint32_t import_module_table_offset;
    uint32_t import_module_count;
    uint32_t import_procedure_table_offset;
    uint32_t data_pages_offset;
    uint32_t nonresident_name_table_offset;
    uint32_t nonresident_name_table_size;
    uint32_t debug_info_offset;
    uint32_t debug_info_size;
    uint32_t windows_resource_offset;
    uint32_t windows_resource_size;
    xx_linear_object *objects;
    xx_linear_page *pages;
} xx_linear_executable;

typedef xx_linear_executable xx_le;
typedef xx_linear_executable xx_le_t;
typedef xx_linear_executable XLE;

XXFC_API void xx_le_init(xx_le *le, xx_io_device *device,
                         int64_t base_address);
XXFC_API xx_le *xx_le_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_le_destroy(xx_le *le);
XXFC_API void xx_le_free(xx_le *le);

XXFC_API bool xx_le_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_le_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_le_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_le_get_number_of_imports(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_le_get_number_of_exports(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_le_get_number_of_resources(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_le_get_memory_map(Abstractformat *self,
                                   xx_memory_map_mode_t mode,
                                   xx_memory_map *output,
                                   xx_pd_struct *pd);

XXFC_API uint32_t xx_le_get_number_of_objects(const xx_le *le);
XXFC_API const xx_linear_object *xx_le_get_object(const xx_le *le,
                                                  uint32_t index);

static inline Abstractformat *xx_le_to_format(xx_le *le) {
    return le ? &le->format : NULL;
}

static inline const Abstractformat *xx_le_to_format_const(const xx_le *le) {
    return le ? &le->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LE_H */
