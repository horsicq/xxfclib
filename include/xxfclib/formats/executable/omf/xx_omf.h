/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_omf.h @brief Intel Relocatable Object Module Format reader. */
#ifndef XXFCLIB_FORMAT_OMF_H
#define XXFCLIB_FORMAT_OMF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_OMF_BASE_ADDRESS UINT64_C(0x1000)
#define XX_OMF_NAME_SIZE 256U
#define XX_OMF_SECTION_NAME_SIZE 272U

#define XX_OMF_PERMISSION_EXECUTE UINT32_C(1)
#define XX_OMF_PERMISSION_WRITE UINT32_C(2)
#define XX_OMF_PERMISSION_READ UINT32_C(4)

typedef struct xx_omf_header_s {
    uint8_t record_type;
    char module_name[XX_OMF_NAME_SIZE];
} xx_omf_header;

/** A SEGDEF/SEGDEF32 record after cumulative-address resolution. */
typedef struct xx_omf_segment_s {
    uint32_t declared_size;
    uint32_t name_index; /**< One based LNAMES index, zero when unnamed. */
    uint64_t address;
    uint8_t bits;
} xx_omf_segment;

/** One LEDATA/LEDATA32 block, corresponding to one Radare2 OMF section. */
typedef struct xx_omf_section_s {
    char name[XX_OMF_SECTION_NAME_SIZE];
    int64_t offset; /**< Absolute device offset of the data bytes. */
    uint64_t address;
    uint32_t size;
    uint32_t virtual_size;
    uint32_t segment_offset;
    uint32_t permissions;
    uint32_t index;
    uint16_t segment_index; /**< One based SEGDEF index. */
    uint8_t bits;
} xx_omf_section;

/** One PUBDEF/PUBDEF32 or LPUBDEF/LPUBDEF32 public symbol. */
typedef struct xx_omf_public_symbol_s {
    char name[XX_OMF_NAME_SIZE];
    int64_t offset; /**< Absolute device offset, or -1 when not backed. */
    uint64_t address;
    uint32_t value;
    uint32_t ordinal; /**< One based public-symbol ordinal. */
    uint16_t segment_index;
    uint8_t bits;
} xx_omf_public_symbol;

typedef struct xx_omf_entry_s {
    int64_t offset;
    uint64_t address;
    uint32_t symbol_index;
} xx_omf_entry;

typedef struct xx_omf_s {
    Abstractformat format;
    xx_omf_header header;
    char **logical_names;
    uint32_t logical_name_count;
    xx_omf_segment *segments;
    uint32_t segment_count;
    xx_omf_section *sections;
    uint32_t section_count;
    xx_omf_public_symbol *symbols;
    uint32_t symbol_count;
    xx_omf_entry entry;
    uint8_t bits;
    bool has_entry;
} xx_omf;

/** Optional stream providers, available when their build module is enabled. */
XXFC_API const StreamSymbols *xx_omf_get_stream_symbols(void);

XXFC_API void xx_omf_init(xx_omf *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_omf *xx_omf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_omf_destroy(xx_omf *reader);
XXFC_API void xx_omf_free(xx_omf *reader);

XXFC_API bool xx_omf_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_omf_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_omf_get_format_size(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_omf_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_omf_header *xx_omf_get_header(xx_omf *reader, xx_pd_struct *pd);
XXFC_API uint8_t xx_omf_get_bits(xx_omf *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_omf_get_number_of_segments(xx_omf *reader, xx_pd_struct *pd);
XXFC_API const xx_omf_segment *xx_omf_get_segment(xx_omf *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_omf_get_number_of_sections(xx_omf *reader, xx_pd_struct *pd);
XXFC_API const xx_omf_section *xx_omf_get_section(xx_omf *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_omf_get_number_of_public_symbols(xx_omf *reader, xx_pd_struct *pd);
XXFC_API const xx_omf_public_symbol *xx_omf_get_public_symbol(xx_omf *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API const xx_omf_entry *xx_omf_get_entry(xx_omf *reader, xx_pd_struct *pd);

XXFC_API uint64_t xx_omf_get_number_of_symbols(Abstractformat *format, xx_pd_struct *pd);
XXFC_API xx_symbol_state *xx_omf_create_symbols_reading(Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_symbol_record *xx_omf_get_current_symbol(Abstractformat *format, xx_symbol_state *state);
XXFC_API bool xx_omf_symbol_move_to_next(Abstractformat *format, xx_symbol_state *state, xx_pd_struct *pd);
XXFC_API void xx_omf_free_symbols_reading(Abstractformat *format, xx_symbol_state *state);

static inline Abstractformat *xx_omf_to_format(xx_omf *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_FORMAT_OMF_H */
