/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_nintendo_rel.h @brief Nintendo Wii REL relocatable module reader. */

#ifndef XXFCLIB_FORMAT_NINTENDO_REL_H
#define XXFCLIB_FORMAT_NINTENDO_REL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_NINTENDO_REL_HEADER_SIZE UINT32_C(0x40)
#define XX_NINTENDO_REL_V3_HEADER_SIZE UINT32_C(0x4c)
#define XX_NINTENDO_REL_MAX_SECTIONS UINT32_C(128)
#define XX_NINTENDO_REL_MAX_IMPORTS UINT32_C(128)
#define XX_NINTENDO_REL_MAX_RELOCATIONS UINT32_C(1048576)
#define XX_NINTENDO_REL_MAIN_DOL_MODULE UINT32_C(0)
#define XX_NINTENDO_REL_BSS_ADDRESS_GAP UINT32_C(0x3c)
#define XX_NINTENDO_REL_INVALID_FILE_OFFSET UINT32_MAX

typedef enum xx_nintendo_rel_relocation_type_e {
    XX_NINTENDO_REL_R_PPC_NONE = 0,
    XX_NINTENDO_REL_R_PPC_ADDR32 = 1,
    XX_NINTENDO_REL_R_PPC_ADDR24 = 2,
    XX_NINTENDO_REL_R_PPC_ADDR16 = 3,
    XX_NINTENDO_REL_R_PPC_ADDR16_LO = 4,
    XX_NINTENDO_REL_R_PPC_ADDR16_HI = 5,
    XX_NINTENDO_REL_R_PPC_ADDR16_HA = 6,
    XX_NINTENDO_REL_R_PPC_ADDR14 = 7,
    XX_NINTENDO_REL_R_PPC_ADDR14_BRTAKEN = 8,
    XX_NINTENDO_REL_R_PPC_ADDR14_BRNTAKEN = 9,
    XX_NINTENDO_REL_R_PPC_REL24 = 10,
    XX_NINTENDO_REL_R_PPC_REL14 = 11,
    XX_NINTENDO_REL_R_PPC_REL14_BRTAKEN = 12,
    XX_NINTENDO_REL_R_PPC_REL14_BRNTAKEN = 13,
    XX_NINTENDO_REL_R_RVL_NONE = 201,
    XX_NINTENDO_REL_R_RVL_SECT = 202,
    XX_NINTENDO_REL_R_RVL_STOP = 203
} xx_nintendo_rel_relocation_type;

typedef enum xx_nintendo_rel_special_symbol_e {
    XX_NINTENDO_REL_SYMBOL_PROLOG = 0,
    XX_NINTENDO_REL_SYMBOL_EPILOG = 1,
    XX_NINTENDO_REL_SYMBOL_UNRESOLVED = 2
} xx_nintendo_rel_special_symbol;

typedef struct xx_nintendo_rel_header {
    uint32_t module_id;
    uint32_t next_module_address;
    uint32_t previous_module_address;
    uint32_t section_count;
    uint32_t section_table_offset;
    uint32_t module_name_offset;
    uint32_t module_name_size;
    uint32_t version;
    uint32_t bss_size;
    uint32_t relocation_table_offset;
    uint32_t import_table_offset;
    uint32_t import_table_size;
    uint8_t prolog_section;
    uint8_t epilog_section;
    uint8_t unresolved_section;
    uint8_t bss_section;
    uint32_t prolog_offset;
    uint32_t epilog_offset;
    uint32_t unresolved_offset;
} xx_nintendo_rel_header;

typedef struct xx_nintendo_rel_v3_extension {
    uint32_t section_alignment;
    uint32_t bss_alignment;
    uint32_t fix_size;
} xx_nintendo_rel_v3_extension;

typedef struct xx_nintendo_rel_section {
    uint32_t packed_offset;
    uint32_t file_offset;
    uint32_t size;
    uint32_t virtual_address;
    uint32_t index;
    bool executable;
    bool bss;
} xx_nintendo_rel_section;

typedef struct xx_nintendo_rel_import {
    uint32_t module_id;
    uint32_t relocation_table_offset;
    uint32_t first_relocation;
    uint32_t relocation_count;
    bool resolvable;
} xx_nintendo_rel_import;

typedef struct xx_nintendo_rel_relocation {
    uint32_t table_offset;
    uint32_t import_index;
    uint32_t module_id;
    uint32_t addend;
    uint32_t patch_address;
    uint32_t patch_file_offset;
    uint16_t offset_delta;
    uint8_t type;
    uint8_t target_section;
    uint8_t patch_section;
    bool patch_address_valid;
} xx_nintendo_rel_relocation;

typedef struct xx_nintendo_rel_apply_report {
    uint32_t applied_relocations;
    uint32_t unsupported_relocations;
    uint32_t invalid_relocations;
    uint32_t external_imports;
    uint32_t external_relocations;
} xx_nintendo_rel_apply_report;

typedef struct xx_nintendo_rel {
    Abstractformat format;
    xx_nintendo_rel_header header;
    xx_nintendo_rel_v3_extension v3;
    xx_nintendo_rel_section *sections;
    xx_nintendo_rel_import *imports;
    xx_nintendo_rel_relocation *relocations;
    uint32_t section_count;
    uint32_t import_count;
    uint32_t relocation_count;
    uint32_t load_address;
} xx_nintendo_rel;

XXFC_API void xx_nintendo_rel_init(xx_nintendo_rel *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_nintendo_rel *xx_nintendo_rel_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nintendo_rel_destroy(xx_nintendo_rel *reader);
XXFC_API void xx_nintendo_rel_free(xx_nintendo_rel *reader);

/** Set the module load address before parsing. */
XXFC_API bool xx_nintendo_rel_set_load_address(xx_nintendo_rel *reader, uint32_t load_address);
XXFC_API bool xx_nintendo_rel_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_rel_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nintendo_rel_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_rel_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_nintendo_rel_header *xx_nintendo_rel_get_header(xx_nintendo_rel *reader, xx_pd_struct *pd);
XXFC_API const xx_nintendo_rel_section *xx_nintendo_rel_get_section(xx_nintendo_rel *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API const xx_nintendo_rel_import *xx_nintendo_rel_get_import(xx_nintendo_rel *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API const xx_nintendo_rel_relocation *xx_nintendo_rel_get_relocation(xx_nintendo_rel *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_rel_get_special_symbol_address(xx_nintendo_rel *reader, xx_nintendo_rel_special_symbol symbol, uint32_t *address, xx_pd_struct *pd);

/**
 * Apply relocations targeting the main DOL or this REL to a mutable copy of
 * the module file. Imports from other REL modules are counted and skipped.
 */
XXFC_API bool xx_nintendo_rel_apply_relocations(xx_nintendo_rel *reader, uint8_t *module, size_t module_size, xx_nintendo_rel_apply_report *report, xx_pd_struct *pd);

static inline Abstractformat *xx_nintendo_rel_to_format(xx_nintendo_rel *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NINTENDO_REL_H */
