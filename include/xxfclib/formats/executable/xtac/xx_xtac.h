/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xtac.h @brief Windows x86-to-ARM64 XTA cache reader. */
#ifndef XXFCLIB_FORMAT_XTAC_H
#define XXFCLIB_FORMAT_XTAC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_XTAC_HEADER_SIZE UINT32_C(56)
#define XX_XTAC_MAX_ADDRESS_PAIRS UINT32_C(0x000fffff)
#define XX_XTAC_MAX_NAME_BYTES UINT32_C(0x1ffe)
#define XX_XTAC_MAX_BLOCK_STUBS UINT32_C(20)
#define XX_XTAC_SECTION_NAME_SIZE 32U
#define XX_XTAC_SYMBOL_NAME_SIZE 16U

#define XX_XTAC_PERMISSION_EXECUTE UINT32_C(1)
#define XX_XTAC_PERMISSION_WRITE UINT32_C(2)
#define XX_XTAC_PERMISSION_READ UINT32_C(4)

typedef struct xx_xtac_header_s {
    uint32_t magic;
    uint32_t version;
    uint32_t is_updated;
    uint32_t address_pairs_offset;
    uint32_t address_pair_count;
    uint32_t module_name_offset;
    uint32_t module_name_size;
    uint32_t nt_path_offset;
    uint32_t nt_path_size;
    uint32_t head_block_stub_offset;
    uint32_t tail_block_stub_offset;
    uint32_t block_stub_code_size;
    uint32_t linked_list_head_offset;
    uint32_t linked_list_tail_offset;
} xx_xtac_header;

typedef struct xx_xtac_address_pair_s {
    uint32_t x86_rva;
    uint32_t arm64_rva;
} xx_xtac_address_pair;

typedef struct xx_xtac_block_stub_s {
    uint32_t magic;
    uint32_t offset_to_next_entry;
    uint32_t next_entry_offset;
    uint32_t padding;
    uint32_t entry_offset;
} xx_xtac_block_stub;

typedef struct xx_xtac_linked_entry_s {
    uint32_t meta_and_offset;
    uint32_t forward_edge_address;
    uint32_t backward_edge_address;
    uint32_t entry_offset;
    uint8_t metadata;
    bool has_forward_edge;
    bool has_backward_edge;
} xx_xtac_linked_entry;

/** Direct section metadata preserves Radare2's declared size=0 and vsize. */
typedef struct xx_xtac_section_s {
    char name[XX_XTAC_SECTION_NAME_SIZE];
    uint32_t file_offset;
    uint32_t file_size;
    uint32_t address;
    uint32_t virtual_size;
    uint32_t permissions;
    uint32_t index;
} xx_xtac_section;

typedef struct xx_xtac_symbol_s {
    char name[XX_XTAC_SYMBOL_NAME_SIZE];
    uint32_t x86_rva;
    uint32_t arm64_rva;
    uint32_t ordinal;
} xx_xtac_symbol;

typedef struct xx_xtac_entry_s {
    int64_t offset;
    uint64_t address;
} xx_xtac_entry;

typedef struct xx_xtac_s {
    Abstractformat format;
    xx_xtac_header header;
    char *module_name;
    char *nt_path;
    xx_xtac_address_pair *address_pairs;
    uint32_t address_pair_count;
    xx_xtac_block_stub *block_stubs;
    uint32_t block_stub_count;
    xx_xtac_linked_entry *linked_entries;
    uint32_t linked_entry_count;
    xx_xtac_section *sections;
    uint32_t section_count;
    xx_xtac_symbol *symbols;
    uint32_t symbol_count;
    xx_xtac_entry entry;
    bool has_entry;
} xx_xtac;

/** Optional stream providers, available when their build module is enabled. */
XXFC_API const StreamSymbols *xx_xtac_get_stream_symbols(void);

XXFC_API void xx_xtac_init(xx_xtac *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_xtac *xx_xtac_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_xtac_destroy(xx_xtac *reader);
XXFC_API void xx_xtac_free(xx_xtac *reader);

XXFC_API bool xx_xtac_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_xtac_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_xtac_get_format_size(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_xtac_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_xtac_header *xx_xtac_get_header(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API const char *xx_xtac_get_module_name(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API const char *xx_xtac_get_nt_path(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_xtac_get_number_of_address_pairs(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API const xx_xtac_address_pair *xx_xtac_get_address_pair(xx_xtac *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_xtac_get_number_of_block_stubs(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API const xx_xtac_block_stub *xx_xtac_get_block_stub(xx_xtac *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_xtac_get_number_of_linked_entries(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API const xx_xtac_linked_entry *xx_xtac_get_linked_entry(xx_xtac *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_xtac_get_number_of_sections(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API const xx_xtac_section *xx_xtac_get_section(xx_xtac *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_xtac_get_number_of_public_symbols(xx_xtac *reader, xx_pd_struct *pd);
XXFC_API const xx_xtac_symbol *xx_xtac_get_public_symbol(xx_xtac *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API const xx_xtac_entry *xx_xtac_get_entry(xx_xtac *reader, xx_pd_struct *pd);

XXFC_API uint64_t xx_xtac_get_number_of_symbols(Abstractformat *format, xx_pd_struct *pd);
XXFC_API xx_symbol_state *xx_xtac_create_symbols_reading(Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_symbol_record *xx_xtac_get_current_symbol(Abstractformat *format, xx_symbol_state *state);
XXFC_API bool xx_xtac_symbol_move_to_next(Abstractformat *format, xx_symbol_state *state, xx_pd_struct *pd);
XXFC_API void xx_xtac_free_symbols_reading(Abstractformat *format, xx_symbol_state *state);

static inline Abstractformat *xx_xtac_to_format(xx_xtac *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_FORMAT_XTAC_H */
