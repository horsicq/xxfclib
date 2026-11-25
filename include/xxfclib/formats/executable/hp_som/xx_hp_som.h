/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_hp_som.h @brief Bounded HP-UX SOM executable and shared-library reader. */
#ifndef XXFCLIB_FORMAT_HP_SOM_H
#define XXFCLIB_FORMAT_HP_SOM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_HP_SOM_HEADER_SIZE UINT32_C(128)
#define XX_HP_SOM_SPACE_RECORD_SIZE UINT32_C(36)
#define XX_HP_SOM_SECTION_RECORD_SIZE UINT32_C(40)
#define XX_HP_SOM_SYMBOL_RECORD_SIZE UINT32_C(20)
#define XX_HP_SOM_DL_HEADER_SIZE UINT32_C(112)
#define XX_HP_SOM_DL_LIBRARY_RECORD_SIZE UINT32_C(8)
#define XX_HP_SOM_DL_IMPORT_RECORD_SIZE UINT32_C(8)
#define XX_HP_SOM_BASE_ADDRESS UINT64_C(0xc0000000)

#define XX_HP_SOM_MAGIC_RELOC UINT16_C(0x0106)
#define XX_HP_SOM_MAGIC_EXEC UINT16_C(0x0107)
#define XX_HP_SOM_MAGIC_SHARE UINT16_C(0x0108)
#define XX_HP_SOM_MAGIC_SHMEM UINT16_C(0x0109)
#define XX_HP_SOM_MAGIC_DEMAND UINT16_C(0x010b)
#define XX_HP_SOM_MAGIC_DL UINT16_C(0x010d)
#define XX_HP_SOM_MAGIC_SHL UINT16_C(0x010e)

#define XX_HP_SOM_SECTION_LOADABLE (UINT32_C(1) << 21)
#define XX_HP_SOM_SECTION_CODE_ONLY (UINT32_C(1) << 16)

typedef struct xx_hp_som_header_s {
    uint16_t system_id;
    uint16_t magic;
    uint32_t version_id;
    uint32_t file_time_seconds;
    uint32_t file_time_nanoseconds;
    uint32_t entry_space;
    uint32_t entry_subspace;
    uint32_t entry_offset;
    uint32_t auxiliary_header_location;
    uint32_t auxiliary_header_size;
    uint32_t som_length;
    uint32_t presumed_dp;
    uint32_t space_location;
    uint32_t space_total;
    uint32_t subspace_location;
    uint32_t subspace_total;
    uint32_t loader_fixup_location;
    uint32_t loader_fixup_total;
    uint32_t space_strings_location;
    uint32_t space_strings_size;
    uint32_t initialization_array_location;
    uint32_t initialization_array_total;
    uint32_t compiler_location;
    uint32_t compiler_total;
    uint32_t symbol_location;
    uint32_t symbol_total;
    uint32_t fixup_request_location;
    uint32_t fixup_request_total;
    uint32_t symbol_strings_location;
    uint32_t symbol_strings_size;
    uint32_t unloadable_space_location;
    uint32_t unloadable_space_size;
    uint32_t checksum;
} xx_hp_som_header;

/** A 36-byte space dictionary record. The final four words are retained verbatim. */
typedef struct xx_hp_som_space_s {
    char *name;
    uint32_t name_offset;
    uint32_t flags;
    uint32_t space_number;
    uint32_t subspace_index;
    uint32_t subspace_quantity;
    uint32_t control[4];
} xx_hp_som_space;

/** A SOM subspace exposed as a section with physical and virtual extents. */
typedef struct xx_hp_som_section_s {
    char *name;
    uint32_t name_offset;
    uint32_t space_index;
    uint32_t flags;
    uint32_t file_offset; /**< File byte offset when file_size != 0; otherwise the stored 32-bit initialization pattern. */
    uint32_t file_size;   /**< Bytes physically initialized from the file; zero selects the pattern union. */
    uint32_t subspace_start;
    uint32_t virtual_size;
    uint32_t alignment;
    uint32_t fixup_request_index;
    uint32_t fixup_request_quantity;
    uint64_t address;
} xx_hp_som_section;

typedef struct xx_hp_som_dl_header_s {
    uint32_t header_version;
    uint32_t ltptr_value;
    uint32_t library_list_location;
    uint32_t library_list_count;
    uint32_t import_list_location;
    uint32_t import_list_count;
    uint32_t hash_table_location;
    uint32_t hash_table_size;
    uint32_t export_list_location;
    uint32_t export_list_count;
    uint32_t string_table_location;
    uint32_t string_table_size;
    uint32_t dynamic_relocation_location;
    uint32_t dynamic_relocation_count;
    uint32_t dlt_location;
    uint32_t plt_location;
    uint32_t dlt_count;
    uint32_t plt_count;
    uint16_t highwater_mark;
    uint16_t flags;
    uint32_t export_extension_location;
    uint32_t module_location;
    uint32_t module_count;
    uint32_t elaborator;
    uint32_t initializer;
    uint32_t embedded_path;
    uint32_t initializer_count;
    uint32_t thread_data_size;
    uint32_t fastbind_list_location;
} xx_hp_som_dl_header;

typedef struct xx_hp_som_library_s {
    char *name;
    uint32_t name_offset;
    uint8_t reserved;
    bool internal_name;
    bool dash_l_reference;
    uint8_t bind;
    uint16_t highwater_mark;
} xx_hp_som_library;

typedef struct xx_hp_som_import_s {
    char *name;
    uint32_t name_offset;
    uint8_t type;
    uint8_t qualifier;
    uint16_t reserved;
} xx_hp_som_import;

/** A named external symbol. No synthetic names are generated. */
typedef struct xx_hp_som_symbol_s {
    char *name;
    char *qualifier_name;
    uint32_t flags;
    uint32_t name_offset;
    uint32_t qualifier_name_offset;
    uint32_t information;
    uint32_t value;
    uint64_t address;
    uint8_t type;
    uint8_t scope;
} xx_hp_som_symbol;

typedef struct xx_hp_som_s {
    Abstractformat format;
    xx_hp_som_header header;
    xx_hp_som_dl_header dynamic_loader_header;
    xx_hp_som_space *spaces;
    xx_hp_som_section *sections;
    xx_hp_som_library *libraries;
    xx_hp_som_import *imports;
    xx_hp_som_symbol *symbols;
    uint32_t space_count;
    uint32_t section_count;
    uint32_t library_count;
    uint32_t import_count;
    uint32_t symbol_count;
    char *interpreter;
    uint64_t entry_address;
    int64_t entry_file_offset;
    bool has_dynamic_loader_header;
} xx_hp_som;

XXFC_API void xx_hp_som_init(xx_hp_som *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_hp_som *xx_hp_som_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_hp_som_destroy(xx_hp_som *reader);
XXFC_API void xx_hp_som_free(xx_hp_som *reader);

XXFC_API bool xx_hp_som_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_hp_som_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_hp_som_get_format_size(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_hp_som_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_hp_som_header *xx_hp_som_get_header(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API const xx_hp_som_dl_header *xx_hp_som_get_dynamic_loader_header(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_hp_som_get_space_count(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API const xx_hp_som_space *xx_hp_som_get_space(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_hp_som_get_section_count(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API const xx_hp_som_section *xx_hp_som_get_section(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_hp_som_get_library_count(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API const xx_hp_som_library *xx_hp_som_get_library(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_hp_som_get_import_count(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API const xx_hp_som_import *xx_hp_som_get_import(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_hp_som_get_symbol_count(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API const xx_hp_som_symbol *xx_hp_som_get_symbol(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API const char *xx_hp_som_get_interpreter(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API uint64_t xx_hp_som_get_entry_address(xx_hp_som *reader, xx_pd_struct *pd);
XXFC_API int64_t xx_hp_som_get_entry_file_offset(xx_hp_som *reader, xx_pd_struct *pd);

static inline Abstractformat *xx_hp_som_to_format(xx_hp_som *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HP_SOM_H */
