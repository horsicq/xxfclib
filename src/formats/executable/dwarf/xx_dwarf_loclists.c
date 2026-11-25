/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Bounded DWARF .debug_loclists and legacy .debug_loc decoding.  This module
 * deliberately consumes section bytes rather than an object-format reader so
 * ELF, Mach-O and other containers can share it.
 */

#include "xxfclib/formats/executable/dwarf/xx_dwarf_loclists.h"

#include <limits.h>
#include <string.h>

static bool xx_dwarf_span(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static bool xx_dwarf_add_size(size_t left, size_t right, size_t *sum)
{
    if (!sum || right > SIZE_MAX - left) return false;
    *sum = left + right;
    return true;
}

static bool xx_dwarf_add_u64(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (!sum || right > UINT64_MAX - left) return false;
    *sum = left + right;
    return true;
}

static bool xx_dwarf_read_uint(const xx_dwarf_loclists_section *section, size_t offset, uint8_t width, uint64_t *value)
{
    uint64_t result = 0U;
    uint8_t index;
    if (!section || !value || width == 0U || width > 8U || !xx_dwarf_span(section->size, offset, width)) return false;
    if (section->big_endian) {
        for (index = 0U; index < width; ++index) result = (result << 8U) | section->data[offset + index];
    } else {
        for (index = 0U; index < width; ++index) result |= (uint64_t)section->data[offset + index] << (8U * index);
    }
    *value = result;
    return true;
}

static bool xx_dwarf_read_uleb(const xx_dwarf_loclists_section *section, size_t *offset, size_t limit, uint64_t *value)
{
    uint64_t result = 0U;
    unsigned shift = 0U;
    size_t at;
    if (!section || !offset || !value || *offset > limit || limit > section->size) return false;
    at = *offset;
    while (at < limit) {
        uint8_t byte = section->data[at++];
        uint64_t payload = (uint64_t)(byte & UINT8_C(0x7f));
        if (shift >= 64U || (shift == 63U && payload > 1U)) return false;
        result |= payload << shift;
        if ((byte & UINT8_C(0x80)) == 0U) {
            *offset = at;
            *value = result;
            return true;
        }
        shift += 7U;
    }
    return false;
}

bool xx_dwarf_loclists_section_init(xx_dwarf_loclists_section *section, const void *data, size_t size, bool big_endian)
{
    if (!section || (!data && size != 0U)) return false;
    section->data = (const uint8_t *)data;
    section->size = size;
    section->big_endian = big_endian;
    return true;
}

bool xx_dwarf_loclists_read_contribution(const xx_dwarf_loclists_section *section, size_t header_offset, xx_dwarf_loclists_contribution *contribution)
{
    xx_dwarf_loclists_contribution parsed;
    uint64_t initial_length;
    uint64_t count;
    size_t body_offset;
    size_t total_size;
    size_t table_size;
    if (!section || !contribution || !section->data || !xx_dwarf_read_uint(section, header_offset, 4U, &initial_length)) return false;
    memset(&parsed, 0, sizeof(parsed));
    parsed.header_offset = header_offset;
    if (initial_length == UINT32_MAX) {
        parsed.dwarf64 = true;
        parsed.offset_size = 8U;
        if (!xx_dwarf_read_uint(section, header_offset + 4U, 8U, &parsed.unit_length) || parsed.unit_length < 8U || parsed.unit_length > SIZE_MAX - 12U) {
            return false;
        }
        total_size = 12U + (size_t)parsed.unit_length;
        body_offset = header_offset + 12U;
    } else {
        if (initial_length >= UINT32_C(0xfffffff0) || initial_length < 8U) return false;
        parsed.offset_size = 4U;
        parsed.unit_length = initial_length;
        total_size = 4U + (size_t)initial_length;
        body_offset = header_offset + 4U;
    }
    if (!xx_dwarf_span(section->size, header_offset, total_size) || !xx_dwarf_span(section->size, body_offset, 8U)) return false;
    parsed.end_offset = header_offset + total_size;
    if (!xx_dwarf_read_uint(section, body_offset, 2U, &count)) return false;
    parsed.version = (uint16_t)count;
    parsed.address_size = section->data[body_offset + 2U];
    parsed.segment_selector_size = section->data[body_offset + 3U];
    if (!xx_dwarf_read_uint(section, body_offset + 4U, 4U, &count)) return false;
    parsed.offset_entry_count = (uint32_t)count;
    parsed.offsets_offset = body_offset + 8U;
    if (parsed.version != 5U || parsed.address_size == 0U || parsed.address_size > 8U || parsed.segment_selector_size != 0U ||
        parsed.offset_entry_count > SIZE_MAX / parsed.offset_size) {
        return false;
    }
    table_size = (size_t)parsed.offset_entry_count * parsed.offset_size;
    if (!xx_dwarf_add_size(parsed.offsets_offset, table_size, &parsed.lists_offset) || parsed.lists_offset > parsed.end_offset) return false;
    *contribution = parsed;
    return true;
}

bool xx_dwarf_loclists_find_contribution_by_base(const xx_dwarf_loclists_section *section, size_t offsets_offset, xx_dwarf_loclists_contribution *contribution)
{
    xx_dwarf_loclists_contribution parsed;
    if (!section || !contribution || offsets_offset > section->size) return false;
    /* Try the 64-bit initial-length layout first, as prescribed by DWARF. */
    if (offsets_offset >= 20U && xx_dwarf_loclists_read_contribution(section, offsets_offset - 20U, &parsed) && parsed.offsets_offset == offsets_offset) {
        *contribution = parsed;
        return true;
    }
    if (offsets_offset >= 12U && xx_dwarf_loclists_read_contribution(section, offsets_offset - 12U, &parsed) && parsed.offsets_offset == offsets_offset) {
        *contribution = parsed;
        return true;
    }
    return false;
}

bool xx_dwarf_loclists_find_contribution_containing(const xx_dwarf_loclists_section *section, size_t list_offset, xx_dwarf_loclists_contribution *contribution)
{
    xx_dwarf_loclists_contribution parsed;
    size_t at = 0U;
    if (!section || !contribution || list_offset >= section->size) return false;
    while (at < section->size) {
        if (!xx_dwarf_loclists_read_contribution(section, at, &parsed) || parsed.end_offset <= at) return false;
        if (list_offset >= parsed.lists_offset && list_offset < parsed.end_offset) {
            *contribution = parsed;
            return true;
        }
        if (list_offset < parsed.end_offset) return false;
        at = parsed.end_offset;
    }
    return false;
}

bool xx_dwarf_loclists_resolve_index(const xx_dwarf_loclists_section *section, const xx_dwarf_loclists_contribution *contribution, uint64_t index, size_t *list_offset)
{
    uint64_t relative;
    uint64_t minimum;
    uint64_t maximum;
    size_t entry;
    size_t resolved;
    if (!section || !contribution || !list_offset || contribution->offsets_offset > contribution->lists_offset || contribution->lists_offset > contribution->end_offset ||
        contribution->end_offset > section->size || (contribution->offset_size != 4U && contribution->offset_size != 8U) ||
        (uint64_t)(contribution->lists_offset - contribution->offsets_offset) != (uint64_t)contribution->offset_entry_count * contribution->offset_size ||
        index >= contribution->offset_entry_count || index > (SIZE_MAX - contribution->offsets_offset) / contribution->offset_size) {
        return false;
    }
    entry = contribution->offsets_offset + (size_t)index * contribution->offset_size;
    if (!xx_dwarf_read_uint(section, entry, contribution->offset_size, &relative)) return false;
    minimum = contribution->lists_offset - contribution->offsets_offset;
    maximum = contribution->end_offset - contribution->offsets_offset;
    if (relative < minimum || relative >= maximum || relative > SIZE_MAX - contribution->offsets_offset) return false;
    resolved = contribution->offsets_offset + (size_t)relative;
    if (resolved < contribution->lists_offset || resolved >= contribution->end_offset) return false;
    *list_offset = resolved;
    return true;
}

bool xx_dwarf_loclists_cursor_init(xx_dwarf_loclists_cursor *cursor, const xx_dwarf_loclists_section *section, const xx_dwarf_loclists_contribution *contribution,
                                   size_t list_offset, uint64_t initial_base, xx_dwarf_loclists_address_resolver resolve_address, void *resolve_user)
{
    if (!cursor || !section || !contribution || !section->data || contribution->version != 5U || contribution->end_offset > section->size ||
        contribution->lists_offset > contribution->end_offset || list_offset < contribution->lists_offset || list_offset >= contribution->end_offset ||
        contribution->address_size == 0U || contribution->address_size > 8U || contribution->segment_selector_size != 0U) {
        return false;
    }
    memset(cursor, 0, sizeof(*cursor));
    cursor->section = section;
    cursor->contribution = *contribution;
    cursor->position = list_offset;
    cursor->base_address = initial_base;
    cursor->resolve_address = resolve_address;
    cursor->resolve_user = resolve_user;
    return true;
}

static xx_dwarf_loclists_result xx_dwarf_resolve_address(const xx_dwarf_loclists_cursor *cursor, size_t *at, bool indexed, uint64_t *address)
{
    uint64_t value;
    if (indexed) {
        if (!xx_dwarf_read_uleb(cursor->section, at, cursor->contribution.end_offset, &value)) return XX_DWARF_LOCLISTS_MALFORMED;
        if (!cursor->resolve_address || !cursor->resolve_address(cursor->resolve_user, value, address)) {
            return XX_DWARF_LOCLISTS_UNRESOLVED_ADDRESS;
        }
        return XX_DWARF_LOCLISTS_ENTRY;
    }
    if (!xx_dwarf_read_uint(cursor->section, *at, cursor->contribution.address_size, address)) return XX_DWARF_LOCLISTS_MALFORMED;
    *at += cursor->contribution.address_size;
    return XX_DWARF_LOCLISTS_ENTRY;
}

xx_dwarf_loclists_result xx_dwarf_loclists_cursor_next(xx_dwarf_loclists_cursor *cursor, xx_dwarf_loclists_entry *entry)
{
    xx_dwarf_loclists_entry parsed;
    xx_dwarf_loclists_result status;
    uint64_t first = 0U;
    uint64_t second = 0U;
    uint64_t expression_size;
    size_t at;
    uint8_t kind;
    if (!cursor || !entry || !cursor->section) return XX_DWARF_LOCLISTS_MALFORMED;
    if (cursor->finished) return XX_DWARF_LOCLISTS_END;
    if (cursor->position >= cursor->contribution.end_offset) return XX_DWARF_LOCLISTS_MALFORMED;
    memset(&parsed, 0, sizeof(parsed));
    parsed.offset = cursor->position;
    at = cursor->position;
    kind = cursor->section->data[at++];
    parsed.kind = (xx_dwarf_loclists_entry_kind)kind;
    switch (kind) {
        case XX_DWARF_LLE_END_OF_LIST:
            parsed.next_offset = at;
            cursor->position = at;
            cursor->finished = true;
            *entry = parsed;
            return XX_DWARF_LOCLISTS_ENTRY;
        case XX_DWARF_LLE_BASE_ADDRESSX:
            status = xx_dwarf_resolve_address(cursor, &at, true, &parsed.base_address);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            cursor->base_address = parsed.base_address;
            parsed.next_offset = at;
            cursor->position = at;
            *entry = parsed;
            return XX_DWARF_LOCLISTS_ENTRY;
        case XX_DWARF_LLE_BASE_ADDRESS:
            status = xx_dwarf_resolve_address(cursor, &at, false, &parsed.base_address);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            cursor->base_address = parsed.base_address;
            parsed.next_offset = at;
            cursor->position = at;
            *entry = parsed;
            return XX_DWARF_LOCLISTS_ENTRY;
        case XX_DWARF_LLE_STARTX_ENDX:
            status = xx_dwarf_resolve_address(cursor, &at, true, &first);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            status = xx_dwarf_resolve_address(cursor, &at, true, &second);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            break;
        case XX_DWARF_LLE_STARTX_LENGTH:
            status = xx_dwarf_resolve_address(cursor, &at, true, &first);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            if (!xx_dwarf_read_uleb(cursor->section, &at, cursor->contribution.end_offset, &second) || !xx_dwarf_add_u64(first, second, &second)) {
                return XX_DWARF_LOCLISTS_MALFORMED;
            }
            break;
        case XX_DWARF_LLE_OFFSET_PAIR:
            if (!xx_dwarf_read_uleb(cursor->section, &at, cursor->contribution.end_offset, &first) ||
                !xx_dwarf_read_uleb(cursor->section, &at, cursor->contribution.end_offset, &second) || !xx_dwarf_add_u64(cursor->base_address, first, &first) ||
                !xx_dwarf_add_u64(cursor->base_address, second, &second)) {
                return XX_DWARF_LOCLISTS_MALFORMED;
            }
            break;
        case XX_DWARF_LLE_DEFAULT_LOCATION:
            if (cursor->has_default) return XX_DWARF_LOCLISTS_MALFORMED;
            parsed.is_default = true;
            break;
        case XX_DWARF_LLE_START_END:
            status = xx_dwarf_resolve_address(cursor, &at, false, &first);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            status = xx_dwarf_resolve_address(cursor, &at, false, &second);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            break;
        case XX_DWARF_LLE_START_LENGTH:
            status = xx_dwarf_resolve_address(cursor, &at, false, &first);
            if (status != XX_DWARF_LOCLISTS_ENTRY) return status;
            if (!xx_dwarf_read_uleb(cursor->section, &at, cursor->contribution.end_offset, &second) || !xx_dwarf_add_u64(first, second, &second)) {
                return XX_DWARF_LOCLISTS_MALFORMED;
            }
            break;
        default: return XX_DWARF_LOCLISTS_MALFORMED;
    }
    if (!parsed.is_default && second < first) return XX_DWARF_LOCLISTS_MALFORMED;
    if (!xx_dwarf_read_uleb(cursor->section, &at, cursor->contribution.end_offset, &expression_size) || expression_size > SIZE_MAX ||
        expression_size > cursor->contribution.end_offset - at) {
        return XX_DWARF_LOCLISTS_MALFORMED;
    }
    parsed.start_address = first;
    parsed.end_address = second;
    parsed.has_range = !parsed.is_default;
    parsed.expression = cursor->section->data + at;
    parsed.expression_size = (size_t)expression_size;
    parsed.next_offset = at + (size_t)expression_size;
    cursor->position = parsed.next_offset;
    if (parsed.is_default) cursor->has_default = true;
    *entry = parsed;
    return XX_DWARF_LOCLISTS_ENTRY;
}

xx_dwarf_loclists_result xx_dwarf_loclists_select_expression(const xx_dwarf_loclists_cursor *cursor, uint64_t scope_start, uint64_t scope_end,
                                                             xx_dwarf_loclists_selection selection, xx_dwarf_loclists_entry *entry)
{
    xx_dwarf_loclists_cursor scan;
    xx_dwarf_loclists_entry current;
    xx_dwarf_loclists_entry best = {0};
    xx_dwarf_loclists_entry fallback = {0};
    xx_dwarf_loclists_result status;
    uint64_t best_overlap = 0U;
    bool have_best = false;
    bool have_fallback = false;
    if (!cursor || !entry || scope_end < scope_start || (selection != XX_DWARF_LOCLISTS_SELECT_ENTRY && selection != XX_DWARF_LOCLISTS_SELECT_WIDEST)) {
        return XX_DWARF_LOCLISTS_MALFORMED;
    }
    scan = *cursor;
    while ((status = xx_dwarf_loclists_cursor_next(&scan, &current)) == XX_DWARF_LOCLISTS_ENTRY) {
        uint64_t from;
        uint64_t to;
        uint64_t overlap;
        if (current.kind == XX_DWARF_LLE_END_OF_LIST) {
            if (have_best) {
                *entry = best;
                return XX_DWARF_LOCLISTS_ENTRY;
            }
            if (have_fallback) {
                *entry = fallback;
                return XX_DWARF_LOCLISTS_ENTRY;
            }
            return XX_DWARF_LOCLISTS_END;
        }
        if (current.is_default) {
            fallback = current;
            have_fallback = true;
            continue;
        }
        if (!current.has_range) continue;
        from = current.start_address > scope_start ? current.start_address : scope_start;
        to = current.end_address < scope_end ? current.end_address : scope_end;
        if (to <= from) continue;
        overlap = to - from;
        if (!have_best || (selection == XX_DWARF_LOCLISTS_SELECT_ENTRY && current.start_address < best.start_address) ||
            (selection == XX_DWARF_LOCLISTS_SELECT_WIDEST && overlap > best_overlap)) {
            best = current;
            best_overlap = overlap;
            have_best = true;
        }
    }
    return status == XX_DWARF_LOCLISTS_END ? XX_DWARF_LOCLISTS_MALFORMED : status;
}

bool xx_dwarf_loc_cursor_init(xx_dwarf_loc_cursor *cursor, const xx_dwarf_loclists_section *section, size_t list_offset, uint8_t address_size, uint64_t initial_base)
{
    if (!cursor || !section || !section->data || list_offset >= section->size || address_size == 0U || address_size > 8U) return false;
    memset(cursor, 0, sizeof(*cursor));
    cursor->section = section;
    cursor->position = list_offset;
    cursor->base_address = initial_base;
    cursor->address_size = address_size;
    return true;
}

xx_dwarf_loclists_result xx_dwarf_loc_cursor_next(xx_dwarf_loc_cursor *cursor, xx_dwarf_loc_entry *entry)
{
    xx_dwarf_loc_entry parsed;
    uint64_t first;
    uint64_t second;
    uint64_t maximum;
    uint64_t expression_size;
    size_t at;
    if (!cursor || !entry || !cursor->section) return XX_DWARF_LOCLISTS_MALFORMED;
    if (cursor->finished) return XX_DWARF_LOCLISTS_END;
    at = cursor->position;
    if (!xx_dwarf_read_uint(cursor->section, at, cursor->address_size, &first)) return XX_DWARF_LOCLISTS_MALFORMED;
    at += cursor->address_size;
    if (!xx_dwarf_read_uint(cursor->section, at, cursor->address_size, &second)) return XX_DWARF_LOCLISTS_MALFORMED;
    at += cursor->address_size;
    memset(&parsed, 0, sizeof(parsed));
    parsed.offset = cursor->position;
    if (first == 0U && second == 0U) {
        parsed.kind = XX_DWARF_LOC_END_OF_LIST;
        parsed.next_offset = at;
        cursor->position = at;
        cursor->finished = true;
        *entry = parsed;
        return XX_DWARF_LOCLISTS_ENTRY;
    }
    maximum = cursor->address_size == 8U ? UINT64_MAX : (UINT64_C(1) << (cursor->address_size * 8U)) - 1U;
    if (first == maximum && second != maximum) {
        parsed.kind = XX_DWARF_LOC_BASE_ADDRESS;
        parsed.base_address = second;
        parsed.next_offset = at;
        cursor->position = at;
        cursor->base_address = second;
        *entry = parsed;
        return XX_DWARF_LOCLISTS_ENTRY;
    }
    if (!xx_dwarf_read_uint(cursor->section, at, 2U, &expression_size)) return XX_DWARF_LOCLISTS_MALFORMED;
    at += 2U;
    if (expression_size > SIZE_MAX || expression_size > cursor->section->size - at || !xx_dwarf_add_u64(cursor->base_address, first, &parsed.start_address) ||
        !xx_dwarf_add_u64(cursor->base_address, second, &parsed.end_address) || parsed.end_address < parsed.start_address) {
        return XX_DWARF_LOCLISTS_MALFORMED;
    }
    parsed.kind = XX_DWARF_LOC_RANGE;
    parsed.expression = cursor->section->data + at;
    parsed.expression_size = (size_t)expression_size;
    parsed.next_offset = at + (size_t)expression_size;
    cursor->position = parsed.next_offset;
    *entry = parsed;
    return XX_DWARF_LOCLISTS_ENTRY;
}
