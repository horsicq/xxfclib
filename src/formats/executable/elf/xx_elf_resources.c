/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/elf/xx_elf.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/sort/xx_sort.h"

#include <limits.h>

#define ELF_GRESOURCE_PREFIX ".gresource."
#define ELF_GRESOURCE_HEADER_SIZE UINT64_C(24)
#define ELF_GRESOURCE_HASH_HEADER_SIZE UINT64_C(8)
#define ELF_GRESOURCE_ITEM_SIZE UINT64_C(24)
#define ELF_GRESOURCE_VALUE_HEADER_SIZE UINT64_C(8)
#define ELF_GRESOURCE_VARIANT_TRAILER_SIZE UINT64_C(7)
#define ELF_GRESOURCE_FLAG_COMPRESSED UINT32_C(1)
#define ELF_GRESOURCE_ITEM_LIMIT UINT64_C(65536)
#define ELF_GRESOURCE_RECORD_LIMIT UINT64_C(65536)
#define ELF_GRESOURCE_NAME_LIMIT UINT64_C(65536)
#define ELF_GRESOURCE_NAMES_LIMIT UINT64_C(67108864)

typedef struct elf_gresource_item {
    uint32_t parent;
    uint32_t key_start;
    uint16_t key_size;
    uint8_t type;
    uint32_t value_start;
    uint32_t value_end;
} elf_gresource_item;

typedef struct elf_gresource_leaf {
    const char *name;
    size_t item_index;
} elf_gresource_leaf;

typedef struct elf_gresource_entry {
    char *name;
    char *type_name;
    int64_t offset;
    int64_t size;
} elf_gresource_entry;

typedef struct elf_gresource_cursor {
    xx_elf *elf;
    elf_gresource_entry *entries;
    size_t count;
    size_t capacity;
    size_t next;
    uint64_t names_size;
    bool finished;
} elf_gresource_cursor;

static bool elf_gresource_error(xx_pd_struct *pd, int code, const char *message)
{
    xx_pd_set_error(pd, code, message);
    return false;
}

static bool elf_gresource_cancelled(xx_pd_struct *pd)
{
    if (!xx_pd_is_stopped(pd)) return false;
    xx_pd_set_error(pd, XXFC_ERR_GENERIC, "ELF GResource traversal cancelled");
    return true;
}

static uint16_t elf_gresource_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t elf_gresource_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool elf_gresource_absolute(const xx_elf *elf, uint64_t relative, size_t size, int64_t *absolute, xx_pd_struct *pd)
{
    int64_t total;
    int64_t base;
    if (!elf || !elf->format.device || !absolute || elf->format.base_address < 0 || relative > (uint64_t)(INT64_MAX - elf->format.base_address))
        return elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF GResource offset outside input");
    base = elf->format.base_address + (int64_t)relative;
    total = xx_io_total_size(elf->format.device);
    if (base < elf->format.base_address || total < base || (uint64_t)size > (uint64_t)(total - base))
        return elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF GResource range outside input");
    *absolute = base;
    return true;
}

static bool elf_gresource_read(const xx_elf *elf, uint64_t relative, void *buffer, size_t size, xx_pd_struct *pd)
{
    int64_t absolute;
    if (!elf_gresource_absolute(elf, relative, size, &absolute, pd)) return false;
    if (!xx_io_read_at(elf->format.device, absolute, buffer, size)) return elf_gresource_error(pd, XXFC_ERR_IO, "Cannot read ELF GResource bytes");
    return true;
}

static char *elf_gresource_read_string(const xx_elf *elf, const xx_elf_section_header *strings, uint32_t offset, uint64_t maximum, xx_pd_struct *pd)
{
    uint64_t remaining;
    uint64_t length = 0U;
    char *result;
    if (!strings || strings->type != XX_ELF_SECTION_STRTAB || offset >= strings->size) return NULL;
    remaining = strings->size - offset;
    if (remaining > maximum + 1U) remaining = maximum + 1U;
    result = (char *)xx_mem_alloc((size_t)remaining);
    if (!result) {
        elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate ELF section name");
        return NULL;
    }
    if (!elf_gresource_read(elf, strings->offset + offset, result, (size_t)remaining, pd)) {
        xx_mem_free(result);
        return NULL;
    }
    while (length < remaining && result[length]) ++length;
    if (length == remaining) {
        xx_mem_free(result);
        elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Unterminated ELF section name");
        return NULL;
    }
    return result;
}

static void elf_gresource_entries_cleanup(elf_gresource_cursor *cursor)
{
    size_t index;
    if (!cursor) return;
    for (index = 0U; index < cursor->count; ++index) {
        xx_mem_free(cursor->entries[index].name);
        xx_mem_free(cursor->entries[index].type_name);
    }
    xx_mem_free(cursor->entries);
    cursor->entries = NULL;
    cursor->count = cursor->capacity = 0U;
}

static void elf_gresource_cursor_free(void *pointer)
{
    elf_gresource_cursor *cursor = (elf_gresource_cursor *)pointer;
    if (!cursor) return;
    elf_gresource_entries_cleanup(cursor);
    xx_mem_free(cursor);
}

static bool elf_gresource_append(elf_gresource_cursor *cursor, const char *name, const char *type_name, uint64_t relative_offset, uint64_t size, xx_pd_struct *pd)
{
    elf_gresource_entry *grown;
    elf_gresource_entry *entry;
    size_t capacity;
    int64_t absolute;
    size_t name_size;
    size_t type_size;
    if (!cursor || !name || !type_name || cursor->count >= ELF_GRESOURCE_RECORD_LIMIT || size > INT64_MAX || size > SIZE_MAX ||
        !elf_gresource_absolute(cursor->elf, relative_offset, (size_t)size, &absolute, pd))
        return false;
    name_size = xx_str_len(name) + 1U;
    type_size = xx_str_len(type_name) + 1U;
    if (name_size > ELF_GRESOURCE_NAME_LIMIT || type_size > ELF_GRESOURCE_NAME_LIMIT || cursor->names_size > ELF_GRESOURCE_NAMES_LIMIT - name_size ||
        cursor->names_size + name_size > ELF_GRESOURCE_NAMES_LIMIT - type_size)
        return elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "ELF GResource names exceed bounded budget");
    if (cursor->count == cursor->capacity) {
        capacity = cursor->capacity ? cursor->capacity * 2U : 16U;
        if (capacity > ELF_GRESOURCE_RECORD_LIMIT) capacity = ELF_GRESOURCE_RECORD_LIMIT;
        if (capacity <= cursor->count || capacity > SIZE_MAX / sizeof(*grown))
            return elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "ELF GResource record array exceeds bounded budget");
        grown = (elf_gresource_entry *)xx_mem_realloc(cursor->entries, capacity * sizeof(*grown));
        if (!grown) return elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate ELF GResource records");
        cursor->entries = grown;
        cursor->capacity = capacity;
    }
    entry = &cursor->entries[cursor->count];
    xx_mem_zero(entry, sizeof(*entry));
    entry->name = xx_str_dup(name);
    entry->type_name = xx_str_dup(type_name);
    if (!entry->name || !entry->type_name) {
        xx_mem_free(entry->name);
        xx_mem_free(entry->type_name);
        xx_mem_zero(entry, sizeof(*entry));
        return elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot copy ELF GResource record name");
    }
    entry->offset = absolute;
    entry->size = (int64_t)size;
    cursor->names_size += name_size + type_size;
    ++cursor->count;
    return true;
}

static bool elf_gresource_section_read(const elf_gresource_cursor *cursor, const xx_elf_section_header *section, uint64_t offset, void *buffer, size_t size,
                                       xx_pd_struct *pd)
{
    if (!section || offset > section->size || (uint64_t)size > section->size - offset || section->offset > UINT64_MAX - offset)
        return elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF GResource section range outside input");
    return elf_gresource_read(cursor->elf, section->offset + offset, buffer, size, pd);
}

static bool elf_gresource_read_item(const elf_gresource_cursor *cursor, const xx_elf_section_header *section, uint64_t offset, elf_gresource_item *item, xx_pd_struct *pd)
{
    uint8_t bytes[24];
    if (!elf_gresource_section_read(cursor, section, offset, bytes, sizeof(bytes), pd)) return false;
    item->parent = elf_gresource_u32(bytes + 4U);
    item->key_start = elf_gresource_u32(bytes + 8U);
    item->key_size = elf_gresource_u16(bytes + 12U);
    item->type = bytes[14U];
    item->value_start = elf_gresource_u32(bytes + 16U);
    item->value_end = elf_gresource_u32(bytes + 20U);
    return true;
}

static char *elf_gresource_item_name(const elf_gresource_cursor *cursor, const xx_elf_section_header *section, const elf_gresource_item *item, const char *parent,
                                     uint64_t *names_size, xx_pd_struct *pd)
{
    size_t parent_size = parent ? xx_str_len(parent) : 0U;
    size_t name_size;
    char *name;
    if (!item || !names_size || item->key_start > section->size || item->key_size > section->size - item->key_start || parent_size > SIZE_MAX - item->key_size)
        return NULL;
    name_size = parent_size + item->key_size;
    if (name_size >= ELF_GRESOURCE_NAME_LIMIT || *names_size > ELF_GRESOURCE_NAMES_LIMIT - name_size - 1U) return NULL;
    name = (char *)xx_mem_alloc(name_size + 1U);
    if (!name) return NULL;
    if (parent_size) xx_mem_copy(name, parent, parent_size);
    if (!elf_gresource_section_read(cursor, section, item->key_start, name + parent_size, item->key_size, pd) ||
        xx_rt_memchr(name + parent_size, 0, item->key_size) != NULL) {
        xx_mem_free(name);
        return NULL;
    }
    name[name_size] = '\0';
    *names_size += name_size + 1U;
    return name;
}

static bool elf_gresource_build_names(const elf_gresource_cursor *cursor, const xx_elf_section_header *section, const elf_gresource_item *items, size_t count,
                                      char **names, xx_pd_struct *pd)
{
    uint8_t *status;
    size_t *stack;
    uint64_t names_size = 0U;
    size_t index;
    if (!count) return true;
    if (count > SIZE_MAX / sizeof(*stack)) return false;
    status = (uint8_t *)xx_mem_calloc(count, sizeof(*status));
    stack = (size_t *)xx_mem_alloc(count * sizeof(*stack));
    if (!status || !stack) {
        xx_mem_free(status);
        xx_mem_free(stack);
        return elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate ELF GResource name graph");
    }
    for (index = 0U; index < count && !elf_gresource_cancelled(pd); ++index) {
        size_t depth = 0U;
        size_t current = index;
        bool valid = true;
        if (status[index]) continue;
        for (;;) {
            uint32_t parent;
            if (current >= count || status[current] == 2U || status[current] == 3U) {
                valid = false;
                break;
            }
            if (status[current] == 1U) break;
            status[current] = 3U;
            stack[depth++] = current;
            parent = items[current].parent;
            if (parent == UINT32_MAX) break;
            current = parent;
        }
        while (depth) {
            uint32_t parent;
            const char *parent_name = NULL;
            current = stack[--depth];
            parent = items[current].parent;
            if (parent != UINT32_MAX) {
                if (parent >= count || status[parent] != 1U) valid = false;
                else parent_name = names[parent];
            }
            if (valid) {
                names[current] = elf_gresource_item_name(cursor, section, &items[current], parent_name, &names_size, pd);
                valid = names[current] != NULL;
            }
            status[current] = valid ? 1U : 2U;
        }
    }
    xx_mem_free(status);
    xx_mem_free(stack);
    return !elf_gresource_cancelled(pd);
}

static int elf_gresource_leaf_compare(const void *left, const void *right)
{
    const elf_gresource_leaf *a = (const elf_gresource_leaf *)left;
    const elf_gresource_leaf *b = (const elf_gresource_leaf *)right;
    return xx_str_cmp(a->name, b->name);
}

static bool elf_gresource_add_value(elf_gresource_cursor *cursor, const xx_elf_section_header *section, const elf_gresource_item *item, const char *name,
                                    xx_pd_struct *pd)
{
    static const uint8_t variant_type[6] = {'(', 'u', 'u', 'a', 'y', ')'};
    uint8_t trailer[7];
    uint8_t header[8];
    uint64_t value_size;
    uint64_t data_offset;
    uint64_t data_size;
    uint32_t logical_size;
    uint32_t flags;
    uint8_t terminator;
    if (item->value_start > item->value_end || item->value_end > section->size || (item->value_start & 7U)) return true;
    value_size = (uint64_t)item->value_end - item->value_start;
    if (value_size < ELF_GRESOURCE_VALUE_HEADER_SIZE + ELF_GRESOURCE_VARIANT_TRAILER_SIZE) return true;
    if (!elf_gresource_section_read(cursor, section, item->value_end - sizeof(trailer), trailer, sizeof(trailer), pd)) return false;
    if (trailer[0] || xx_mem_compare(trailer + 1U, variant_type, sizeof(variant_type)) != 0) return true;
    if (!elf_gresource_section_read(cursor, section, item->value_start, header, sizeof(header), pd)) return false;
    logical_size = elf_gresource_u32(header);
    flags = elf_gresource_u32(header + 4U);
    if (flags & ~ELF_GRESOURCE_FLAG_COMPRESSED) return true;
    data_offset = (uint64_t)item->value_start + ELF_GRESOURCE_VALUE_HEADER_SIZE;
    data_size = value_size - ELF_GRESOURCE_VALUE_HEADER_SIZE - ELF_GRESOURCE_VARIANT_TRAILER_SIZE;
    if (!(flags & ELF_GRESOURCE_FLAG_COMPRESSED)) {
        if (!data_size || logical_size != data_size - 1U || !elf_gresource_section_read(cursor, section, data_offset + logical_size, &terminator, 1U, pd)) return false;
        if (terminator) return true;
        data_size = logical_size;
    }
    if (section->offset > UINT64_MAX - data_offset) return true;
    return elf_gresource_append(cursor, name, "gresource", section->offset + data_offset, data_size, pd);
}

static bool elf_gresource_parse_section(elf_gresource_cursor *cursor, const xx_elf_section_header *section, const char *section_name, xx_pd_struct *pd)
{
    uint8_t header[24];
    uint8_t hash_header[8];
    uint32_t root_start;
    uint32_t root_end;
    uint64_t bloom_words;
    uint64_t buckets;
    uint64_t items_offset;
    uint64_t items_size;
    uint64_t item_count64;
    size_t item_count;
    elf_gresource_item *items = NULL;
    char **names = NULL;
    elf_gresource_leaf *leaves = NULL;
    size_t leaf_count = 0U;
    size_t index;
    bool ok = false;
    if (!elf_gresource_section_read(cursor, section, 0U, header, sizeof(header), pd) ||
        (xx_mem_compare(header, "GVariant", 8U) != 0 && xx_mem_compare(header, "raVGtnai", 8U) != 0) || elf_gresource_u32(header + 8U) != 0U)
        return elf_gresource_error(pd, XXFC_ERR_INVALID_ARG, "Invalid ELF GResource header");
    root_start = elf_gresource_u32(header + 16U);
    root_end = elf_gresource_u32(header + 20U);
    if (root_start < ELF_GRESOURCE_HEADER_SIZE || (root_start & 3U) || root_start > root_end || root_end > section->size ||
        (uint64_t)root_end - root_start < ELF_GRESOURCE_HASH_HEADER_SIZE)
        return elf_gresource_error(pd, XXFC_ERR_INVALID_ARG, "Invalid ELF GResource root table");
    if (!elf_gresource_section_read(cursor, section, root_start, hash_header, sizeof(hash_header), pd)) return false;
    bloom_words = elf_gresource_u32(hash_header) & UINT32_C(0x07ffffff);
    buckets = elf_gresource_u32(hash_header + 4U);
    items_offset = (uint64_t)root_start + ELF_GRESOURCE_HASH_HEADER_SIZE;
    if (bloom_words > (UINT64_MAX - items_offset) / 4U) return elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF GResource bloom table overflow");
    items_offset += bloom_words * 4U;
    if (buckets > (UINT64_MAX - items_offset) / 4U) return elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF GResource bucket table overflow");
    items_offset += buckets * 4U;
    if (items_offset > root_end || ((uint64_t)root_end - items_offset) % ELF_GRESOURCE_ITEM_SIZE)
        return elf_gresource_error(pd, XXFC_ERR_INVALID_ARG, "Invalid ELF GResource item table");
    items_size = (uint64_t)root_end - items_offset;
    item_count64 = items_size / ELF_GRESOURCE_ITEM_SIZE;
    if (item_count64 > ELF_GRESOURCE_ITEM_LIMIT || item_count64 > SIZE_MAX)
        return elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF GResource item count exceeds bounded limit");
    item_count = (size_t)item_count64;
    if (item_count) {
        if (item_count > SIZE_MAX / sizeof(*items) || item_count > SIZE_MAX / sizeof(*names) || item_count > SIZE_MAX / sizeof(*leaves))
            return elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "ELF GResource item arrays overflow");
        items = (elf_gresource_item *)xx_mem_calloc(item_count, sizeof(*items));
        names = (char **)xx_mem_calloc(item_count, sizeof(*names));
        leaves = (elf_gresource_leaf *)xx_mem_calloc(item_count, sizeof(*leaves));
        if (!items || !names || !leaves) {
            elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate ELF GResource item arrays");
            goto cleanup;
        }
    }
    for (index = 0U; index < item_count; ++index) {
        if (elf_gresource_cancelled(pd) || !elf_gresource_read_item(cursor, section, items_offset + index * ELF_GRESOURCE_ITEM_SIZE, &items[index], pd)) goto cleanup;
    }
    if (!elf_gresource_build_names(cursor, section, items, item_count, names, pd)) goto cleanup;
    for (index = 0U; index < item_count; ++index) {
        if (items[index].type == 'v' && names[index] && names[index][0] == '/') {
            leaves[leaf_count].name = names[index];
            leaves[leaf_count].item_index = index;
            ++leaf_count;
        }
    }
    if (leaf_count > 1U) xx_sort(leaves, leaf_count, sizeof(*leaves), elf_gresource_leaf_compare);
    if (!elf_gresource_append(cursor, section_name, "gresource-bundle", section->offset, section->size, pd)) goto cleanup;
    for (index = 0U; index < leaf_count; ++index) {
        if (!elf_gresource_add_value(cursor, section, &items[leaves[index].item_index], leaves[index].name, pd)) goto cleanup;
    }
    ok = true;
cleanup:
    if (names) {
        for (index = 0U; index < item_count; ++index) xx_mem_free(names[index]);
    }
    xx_mem_free(items);
    xx_mem_free(names);
    xx_mem_free(leaves);
    return ok;
}

static bool elf_gresource_collect(elf_gresource_cursor *cursor, xx_pd_struct *pd)
{
    const xx_elf_section_header *name_section;
    uint64_t index;
    if (cursor->elf->section_name_index == 0 || cursor->elf->section_name_index >= cursor->elf->section_header_count) return true;
    name_section = &cursor->elf->section_headers[cursor->elf->section_name_index];
    for (index = 0U; index < cursor->elf->section_header_count; ++index) {
        const xx_elf_section_header *section = &cursor->elf->section_headers[index];
        char *name;
        bool is_resource;
        if (elf_gresource_cancelled(pd)) return false;
        if (section->type != XX_ELF_SECTION_PROGBITS || !section->size) continue;
        name = elf_gresource_read_string(cursor->elf, name_section, section->name_offset, ELF_GRESOURCE_NAME_LIMIT, pd);
        if (!name) return false;
        is_resource = xx_str_starts_with(name, ELF_GRESOURCE_PREFIX);
        if (is_resource && !elf_gresource_parse_section(cursor, section, name, pd)) {
            xx_mem_free(name);
            return false;
        }
        xx_mem_free(name);
    }
    return true;
}

bool xx_elf_resource_move_to_next(Abstractformat *format, xx_resource_state *state, xx_pd_struct *pd)
{
    elf_gresource_cursor *cursor;
    elf_gresource_entry *entry;
    xx_resource_record *record;
    if (!format || !state || state->format != format || !state->internal_state) return false;
    cursor = (elf_gresource_cursor *)state->internal_state;
    record = &state->current_record;
    xx_resource_record_cleanup(record);
    state->has_record = false;
    if (state->failed || cursor->finished) return false;
    if (elf_gresource_cancelled(pd)) goto fail;
    if (cursor->next >= cursor->count) {
        cursor->finished = true;
        state->total_records = (int64_t)cursor->count;
        return false;
    }
    entry = &cursor->entries[cursor->next++];
    record->name = xx_str_dup(entry->name);
    record->type_name = xx_str_dup(entry->type_name);
    if (!record->name || !record->type_name) {
        elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot copy ELF GResource stream record");
        goto fail;
    }
    record->offset = entry->offset;
    record->size = entry->size;
    ++state->current_index;
    state->has_record = true;
    return true;
fail:
    xx_resource_record_cleanup(record);
    state->failed = true;
    cursor->finished = true;
    return false;
}

xx_resource_state *xx_elf_create_resources_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_resource_state *state;
    elf_gresource_cursor *cursor;
    if (!format || !format->device || !format->base_info_handled || !format->is_valid) return NULL;
    state = (xx_resource_state *)xx_mem_alloc(sizeof(*state));
    cursor = (elf_gresource_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        xx_mem_free(state);
        xx_mem_free(cursor);
        elf_gresource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate ELF GResource cursor");
        return NULL;
    }
    xx_resource_state_init(state, format);
    state->internal_state = cursor;
    state->free_internal = elf_gresource_cursor_free;
    cursor->elf = (xx_elf *)format;
    if (!elf_gresource_collect(cursor, pd)) {
        xx_resource_state_free(state);
        return NULL;
    }
    if (cursor->count > INT64_MAX) {
        elf_gresource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF GResource record count cannot be represented");
        xx_resource_state_free(state);
        return NULL;
    }
    state->total_records = (int64_t)cursor->count;
    if (!cursor->count) {
        cursor->finished = true;
        return state;
    }
    if (!xx_elf_resource_move_to_next(format, state, pd) && state->failed) {
        xx_resource_state_free(state);
        return NULL;
    }
    return state;
}

const xx_resource_record *xx_elf_get_current_resource(Abstractformat *format, xx_resource_state *state)
{
    return state && state->format == format && state->has_record && !state->failed ? &state->current_record : NULL;
}

void xx_elf_free_resources_reading(Abstractformat *format, xx_resource_state *state)
{
    (void)format;
    xx_resource_state_free(state);
}

uint64_t xx_elf_get_number_of_resources(Abstractformat *format, xx_pd_struct *pd)
{
    xx_resource_state *state;
    uint64_t count;
    if (!format) return 0U;
    state = xx_format_create_resources_reading(format, pd);
    if (!state) return 0U;
    count = state->failed || state->total_records < 0 ? 0U : (uint64_t)state->total_records;
    xx_format_free_resources_reading(format, state);
    format->number_of_resources = count;
    return count;
}

const StreamResources *xx_elf_get_stream_resources(void)
{
    static const StreamResources stream = {.get_number_of_resources = xx_elf_get_number_of_resources,
                                           .create_resources_reading = xx_elf_create_resources_reading,
                                           .get_current_resource = xx_elf_get_current_resource,
                                           .resource_move_to_next = xx_elf_resource_move_to_next,
                                           .free_resources_reading = xx_elf_free_resources_reading};
    return &stream;
}
