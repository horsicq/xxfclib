/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_DWARF_LOCLISTS_H
#define XXFCLIB_FORMAT_DWARF_LOCLISTS_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** DWARF 5 location list entry encodings (DW_LLE_*). */
typedef enum xx_dwarf_loclists_entry_kind_e {
    XX_DWARF_LLE_END_OF_LIST = 0x00,
    XX_DWARF_LLE_BASE_ADDRESSX = 0x01,
    XX_DWARF_LLE_STARTX_ENDX = 0x02,
    XX_DWARF_LLE_STARTX_LENGTH = 0x03,
    XX_DWARF_LLE_OFFSET_PAIR = 0x04,
    XX_DWARF_LLE_DEFAULT_LOCATION = 0x05,
    XX_DWARF_LLE_BASE_ADDRESS = 0x06,
    XX_DWARF_LLE_START_END = 0x07,
    XX_DWARF_LLE_START_LENGTH = 0x08
} xx_dwarf_loclists_entry_kind;

/** Result of advancing a location list cursor. */
typedef enum xx_dwarf_loclists_result_e {
    XX_DWARF_LOCLISTS_MALFORMED = -1,
    XX_DWARF_LOCLISTS_UNRESOLVED_ADDRESS = -2,
    XX_DWARF_LOCLISTS_END = 0,
    XX_DWARF_LOCLISTS_ENTRY = 1
} xx_dwarf_loclists_result;

/** Non-owning view of a .debug_loclists or .debug_loc byte stream. */
typedef struct xx_dwarf_loclists_section_s {
    const uint8_t *data;
    size_t size;
    bool big_endian;
} xx_dwarf_loclists_section;

/** One bounded DWARF 5 .debug_loclists contribution. */
typedef struct xx_dwarf_loclists_contribution_s {
    size_t header_offset;
    size_t offsets_offset; /* DW_AT_loclists_base points here. */
    size_t lists_offset;
    size_t end_offset; /* Exclusive and bounded by the section. */
    uint64_t unit_length;
    uint32_t offset_entry_count;
    uint16_t version;
    uint8_t address_size;
    uint8_t segment_selector_size;
    uint8_t offset_size;
    bool dwarf64;
} xx_dwarf_loclists_contribution;

/** Resolve one DWARF address-table index for an address-index LLE. */
typedef bool (*xx_dwarf_loclists_address_resolver)(void *user, uint64_t index, uint64_t *address);

/** One decoded DWARF 5 location list entry. Expression bytes alias the section. */
typedef struct xx_dwarf_loclists_entry_s {
    size_t offset;
    size_t next_offset;
    xx_dwarf_loclists_entry_kind kind;
    uint64_t start_address;
    uint64_t end_address;
    uint64_t base_address;
    const uint8_t *expression;
    size_t expression_size;
    bool has_range;
    bool is_default;
} xx_dwarf_loclists_entry;

/** Stack-allocated cursor for exactly one DWARF 5 list. */
typedef struct xx_dwarf_loclists_cursor_s {
    const xx_dwarf_loclists_section *section;
    xx_dwarf_loclists_contribution contribution;
    size_t position;
    uint64_t base_address;
    xx_dwarf_loclists_address_resolver resolve_address;
    void *resolve_user;
    bool has_default;
    bool finished;
} xx_dwarf_loclists_cursor;

/** Location selection policy used by analysis clients. */
typedef enum xx_dwarf_loclists_selection_e {
    /** Choose the range starting earliest in the requested scope. */
    XX_DWARF_LOCLISTS_SELECT_ENTRY,
    /** Choose the range covering the largest part of the requested scope. */
    XX_DWARF_LOCLISTS_SELECT_WIDEST
} xx_dwarf_loclists_selection;

XXFC_API bool xx_dwarf_loclists_section_init(xx_dwarf_loclists_section *section, const void *data, size_t size, bool big_endian);
XXFC_API bool xx_dwarf_loclists_read_contribution(const xx_dwarf_loclists_section *section, size_t header_offset, xx_dwarf_loclists_contribution *contribution);
XXFC_API bool xx_dwarf_loclists_find_contribution_by_base(const xx_dwarf_loclists_section *section, size_t offsets_offset, xx_dwarf_loclists_contribution *contribution);
XXFC_API bool xx_dwarf_loclists_find_contribution_containing(const xx_dwarf_loclists_section *section, size_t list_offset, xx_dwarf_loclists_contribution *contribution);
XXFC_API bool xx_dwarf_loclists_resolve_index(const xx_dwarf_loclists_section *section, const xx_dwarf_loclists_contribution *contribution, uint64_t index,
                                              size_t *list_offset);
XXFC_API bool xx_dwarf_loclists_cursor_init(xx_dwarf_loclists_cursor *cursor, const xx_dwarf_loclists_section *section,
                                            const xx_dwarf_loclists_contribution *contribution, size_t list_offset, uint64_t initial_base,
                                            xx_dwarf_loclists_address_resolver resolve_address, void *resolve_user);
XXFC_API xx_dwarf_loclists_result xx_dwarf_loclists_cursor_next(xx_dwarf_loclists_cursor *cursor, xx_dwarf_loclists_entry *entry);
XXFC_API xx_dwarf_loclists_result xx_dwarf_loclists_select_expression(const xx_dwarf_loclists_cursor *cursor, uint64_t scope_start, uint64_t scope_end,
                                                                      xx_dwarf_loclists_selection selection, xx_dwarf_loclists_entry *entry);

/** Legacy .debug_loc entry encodings exposed by the bounded cursor. */
typedef enum xx_dwarf_loc_entry_kind_e {
    XX_DWARF_LOC_END_OF_LIST = 0,
    XX_DWARF_LOC_RANGE = 1,
    XX_DWARF_LOC_BASE_ADDRESS = 2
} xx_dwarf_loc_entry_kind;

typedef struct xx_dwarf_loc_entry_s {
    size_t offset;
    size_t next_offset;
    xx_dwarf_loc_entry_kind kind;
    uint64_t start_address;
    uint64_t end_address;
    uint64_t base_address;
    const uint8_t *expression;
    size_t expression_size;
} xx_dwarf_loc_entry;

/** Stack-allocated cursor for one legacy .debug_loc list. */
typedef struct xx_dwarf_loc_cursor_s {
    const xx_dwarf_loclists_section *section;
    size_t position;
    uint64_t base_address;
    uint8_t address_size;
    bool finished;
} xx_dwarf_loc_cursor;

XXFC_API bool xx_dwarf_loc_cursor_init(xx_dwarf_loc_cursor *cursor, const xx_dwarf_loclists_section *section, size_t list_offset, uint8_t address_size,
                                       uint64_t initial_base);
XXFC_API xx_dwarf_loclists_result xx_dwarf_loc_cursor_next(xx_dwarf_loc_cursor *cursor, xx_dwarf_loc_entry *entry);

#ifdef __cplusplus
}
#endif

#endif
