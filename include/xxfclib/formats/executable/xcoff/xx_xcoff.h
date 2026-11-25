/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xcoff.h @brief AIX 32-bit Extended COFF executable reader. */

#ifndef XXFCLIB_FORMAT_XCOFF_H
#define XXFCLIB_FORMAT_XCOFF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_XCOFF_FILE_HEADER_SIZE UINT32_C(20)
#define XX_XCOFF_OPTIONAL_HEADER_SIZE UINT32_C(72)
#define XX_XCOFF_SECTION_HEADER_SIZE UINT32_C(40)
#define XX_XCOFF_LOADER_HEADER_SIZE UINT32_C(32)
#define XX_XCOFF_LOADER_SYMBOL_SIZE UINT32_C(24)
#define XX_XCOFF_MAX_SECTIONS UINT32_C(4096)
#define XX_XCOFF_MAX_LOADER_SYMBOLS UINT32_C(65535)
#define XX_XCOFF_PERMISSION_EXECUTE UINT32_C(1)
#define XX_XCOFF_PERMISSION_WRITE UINT32_C(2)
#define XX_XCOFF_PERMISSION_READ UINT32_C(4)

typedef enum xx_xcoff_section_type_e {
    XX_XCOFF_SECTION_REGULAR = 0x0000,
    XX_XCOFF_SECTION_PAD = 0x0008,
    XX_XCOFF_SECTION_DWARF = 0x0010,
    XX_XCOFF_SECTION_TEXT = 0x0020,
    XX_XCOFF_SECTION_DATA = 0x0040,
    XX_XCOFF_SECTION_BSS = 0x0080,
    XX_XCOFF_SECTION_EXCEPTION = 0x0100,
    XX_XCOFF_SECTION_INFO = 0x0200,
    XX_XCOFF_SECTION_THREAD_DATA = 0x0400,
    XX_XCOFF_SECTION_THREAD_BSS = 0x0800,
    XX_XCOFF_SECTION_LOADER = 0x1000,
    XX_XCOFF_SECTION_DEBUG = 0x2000,
    XX_XCOFF_SECTION_TYPE_CHECK = 0x4000,
    XX_XCOFF_SECTION_OVERFLOW = 0x8000
} xx_xcoff_section_type;

typedef struct xx_xcoff_header {
    uint16_t magic;
    uint16_t section_count;
    uint32_t timestamp;
    uint32_t symbol_table_offset;
    uint32_t symbol_count;
    uint16_t optional_header_size;
    uint16_t flags;
    uint16_t optional_magic;
    uint16_t version_stamp;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t entry_descriptor;
    uint32_t text_start;
    uint32_t data_start;
    uint32_t toc;
    uint16_t entry_section;
    uint16_t text_section;
    uint16_t data_section;
    uint16_t toc_section;
    uint16_t loader_section;
    uint16_t bss_section;
} xx_xcoff_header;

typedef struct xx_xcoff_section {
    char name[9];
    uint32_t physical_address;
    uint32_t virtual_address;
    uint32_t size;
    uint32_t file_offset;
    uint32_t relocation_offset;
    uint32_t line_number_offset;
    uint16_t relocation_count;
    uint16_t line_number_count;
    uint32_t flags;
    xx_xcoff_section_type type;
    uint32_t permissions;
} xx_xcoff_section;

typedef struct xx_xcoff_loader_header {
    uint32_t version;
    uint32_t symbol_count;
    uint32_t relocation_count;
    uint32_t import_string_length;
    uint32_t import_file_count;
    uint32_t import_file_offset;
    uint32_t string_table_length;
    uint32_t string_table_offset;
} xx_xcoff_loader_header;

typedef struct xx_xcoff_loader_symbol {
    char name[256];
    uint32_t value;
    uint16_t section_number;
    uint8_t type_flags;
    uint8_t storage_class;
    uint32_t import_file_index;
    uint32_t parameter_hash;
    bool has_inline_name;
} xx_xcoff_loader_symbol;

typedef struct xx_xcoff_import {
    char name[9];
    uint32_t loader_symbol_index;
    uint32_t import_file_index;
    bool is_function;
} xx_xcoff_import;

typedef struct xx_xcoff {
    Abstractformat format;
    xx_xcoff_header header;
    xx_xcoff_loader_header loader_header;
    xx_xcoff_section *sections;
    xx_xcoff_loader_symbol *loader_symbols;
    xx_xcoff_import *imports;
    uint32_t section_count;
    uint32_t loader_symbol_count;
    uint32_t import_count;
    uint32_t entry_point;
    bool has_loader;
    bool has_entry_point;
} xx_xcoff;

XXFC_API void xx_xcoff_init(xx_xcoff *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_xcoff *xx_xcoff_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_xcoff_destroy(xx_xcoff *reader);
XXFC_API void xx_xcoff_free(xx_xcoff *reader);

XXFC_API bool xx_xcoff_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xcoff_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xcoff_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xcoff_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_xcoff_header *xx_xcoff_get_header(xx_xcoff *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_xcoff_get_number_of_sections(xx_xcoff *reader, xx_pd_struct *pd);
XXFC_API const xx_xcoff_section *xx_xcoff_get_section(xx_xcoff *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API bool xx_xcoff_read_section(xx_xcoff *reader, uint32_t index, uint32_t offset, void *buffer, size_t size, xx_pd_struct *pd);
XXFC_API const xx_xcoff_loader_header *xx_xcoff_get_loader_header(xx_xcoff *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_xcoff_get_number_of_loader_symbols(xx_xcoff *reader, xx_pd_struct *pd);
XXFC_API const xx_xcoff_loader_symbol *xx_xcoff_get_loader_symbol(xx_xcoff *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_xcoff_get_number_of_imports(xx_xcoff *reader, xx_pd_struct *pd);
XXFC_API const xx_xcoff_import *xx_xcoff_get_import(xx_xcoff *reader, uint32_t index, xx_pd_struct *pd);

static inline Abstractformat *xx_xcoff_to_format(xx_xcoff *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XCOFF_H */
