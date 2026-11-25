/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_pe_resources_internal.h"

#define PE_METADATA_BUDGET UINT64_C(100000)

typedef struct pe_version_frame {
    int64_t end;
    int64_t next;
    char *key;
    bool strings;
} pe_version_frame;

typedef struct pe_metadata_cursor {
    Abstractformat *format;
    uint32_t header_index;
    uint32_t phase;
    xx_resource_state *resources;
    bool resource_consumed;
    int64_t version_base;
    int64_t fixed_value;
    uint32_t fixed_stage;
    pe_version_frame version_frames[PE_RESOURCE_DEPTH];
    uint32_t version_depth;
    uint64_t version_budget;
    uint32_t debug_index;
    bool finished;
} pe_metadata_cursor;

typedef struct pe_version_block {
    int64_t end;
    int64_t value;
    int64_t children;
    uint32_t value_bytes;
    uint16_t type;
    char *key;
} pe_version_block;

static int64_t pe_version_align(int64_t base, int64_t offset)
{
    int64_t relative = (offset - base + 3) & ~(int64_t)3;
    return relative > INT64_MAX - base ? INT64_MAX : base + relative;
}

static void pe_version_clear(pe_metadata_cursor *cursor)
{
    while (cursor->version_depth) {
        pe_version_frame *frame = &cursor->version_frames[--cursor->version_depth];
        if (frame->key) xx_mem_free(frame->key);
        frame->key = NULL;
    }
    cursor->fixed_stage = 0U;
}

static bool pe_version_parse(pe_metadata_cursor *cursor, int64_t start, int64_t limit, pe_version_block *block, xx_pd_struct *pd)
{
    uint16_t length;
    uint16_t value_length;
    uint16_t key_unit;
    uint32_t key_units = 0U;
    int64_t key_end;
    xx_mem_zero(block, sizeof(*block));
    if (start < cursor->version_base || start > limit || limit - start < 6) return pe_stream_fail(pd, "Truncated PE VERSIONINFO block header");
    if (!pe_read_u16(cursor->format, start, &length, pd) || !pe_read_u16(cursor->format, start + 2, &value_length, pd) ||
        !pe_read_u16(cursor->format, start + 4, &block->type, pd))
        return false;
    if (length < 8U || length > limit - start || block->type > 1U) return pe_stream_fail(pd, "Malformed PE VERSIONINFO block");
    block->end = start + length;
    key_end = start + 6;
    for (;;) {
        if (pe_stream_cancelled(pd)) return false;
        if (block->end - key_end < 2) return pe_stream_fail(pd, "Unterminated PE VERSIONINFO key");
        if (!pe_read_u16(cursor->format, key_end, &key_unit, pd)) return false;
        key_end += 2;
        if (!key_unit) break;
        ++key_units;
    }
    block->value = pe_version_align(cursor->version_base, key_end);
    block->value_bytes = (uint32_t)value_length * (block->type ? 2U : 1U);
    if (block->value_bytes && (block->value > block->end || block->value_bytes > block->end - block->value)) return pe_stream_fail(pd, "Truncated PE VERSIONINFO value");
    if (block->value > block->end) block->value = block->end;
    block->children = pe_version_align(cursor->version_base, block->value + block->value_bytes);
    if (block->children > block->end) block->children = block->end;
    block->key = pe_utf16(cursor->format, start + 6, key_units, false, pd);
    return block->key != NULL;
}

static bool pe_version_begin(pe_metadata_cursor *cursor, const xx_resource_record *resource, xx_pd_struct *pd)
{
    pe_version_block root;
    uint32_t signature;
    pe_version_clear(cursor);
    cursor->version_base = resource->offset;
    cursor->version_budget = PE_METADATA_BUDGET;
    if (resource->offset < 0 || resource->size < 8 || resource->size > INT64_MAX - resource->offset) return pe_stream_fail(pd, "Invalid PE VERSIONINFO resource range");
    if (!pe_version_parse(cursor, resource->offset, resource->offset + resource->size, &root, pd)) return false;
    if (xx_rt_strcmp(root.key, "VS_VERSION_INFO") != 0 || root.type != 0U || (root.value_bytes && root.value_bytes < 52U)) {
        xx_mem_free(root.key);
        return pe_stream_fail(pd, "Invalid PE VERSIONINFO root");
    }
    cursor->version_frames[0].end = root.end;
    cursor->version_frames[0].next = root.children;
    cursor->version_frames[0].key = root.key;
    cursor->version_frames[0].strings = false;
    cursor->version_depth = 1U;
    if (!root.type && root.value_bytes >= 52U) {
        if (!pe_read_u32(cursor->format, root.value, &signature, pd)) return false;
        if (signature != UINT32_C(0xfeef04bd)) return pe_stream_fail(pd, "Invalid PE fixed version signature");
        cursor->fixed_value = root.value;
        cursor->fixed_stage = 1U;
    }
    return true;
}

static bool pe_metadata_key(xx_metadata_record *record, const char *key, int64_t offset, int64_t size, xx_pd_struct *pd)
{
    record->key = xx_str_dup(key);
    record->offset = offset;
    record->size = size;
    return record->key || pe_stream_allocation_failed(pd, "Cannot allocate PE metadata key");
}

static char *pe_version_metadata_key(pe_metadata_cursor *cursor, const char *leaf, xx_pd_struct *pd)
{
    size_t length = 8U + xx_rt_strlen(leaf);
    size_t used = 8U;
    uint32_t index;
    char *result;
    for (index = 1U; index < cursor->version_depth; ++index) length += xx_rt_strlen(cursor->version_frames[index].key) + 1U;
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) {
        pe_stream_allocation_failed(pd, "Cannot allocate PE version metadata key");
        return NULL;
    }
    xx_rt_memcpy(result, "version.", 8U);
    for (index = 1U; index < cursor->version_depth; ++index) {
        size_t part = xx_rt_strlen(cursor->version_frames[index].key);
        xx_rt_memcpy(result + used, cursor->version_frames[index].key, part);
        used += part;
        result[used++] = '.';
    }
    xx_rt_memcpy(result + used, leaf, xx_rt_strlen(leaf) + 1U);
    return result;
}

/* Returns 1 for a record, 0 for normal exhaustion, -1 for malformed input. */
static int pe_version_next(pe_metadata_cursor *cursor, xx_metadata_record *record, xx_pd_struct *pd)
{
    if (cursor->fixed_stage) {
        uint32_t high;
        uint32_t low;
        char value[48];
        int64_t offset = cursor->fixed_value + (cursor->fixed_stage == 1U ? 8 : 16);
        const char *key = cursor->fixed_stage == 1U ? "version.FileVersion" : "version.ProductVersion";
        if (!pe_read_u32(cursor->format, offset, &high, pd) || !pe_read_u32(cursor->format, offset + 4, &low, pd)) return -1;
        xx_rt_snprintf(value, sizeof(value), "%u.%u.%u.%u", (unsigned int)(high >> 16), (unsigned int)(high & 0xffffU), (unsigned int)(low >> 16),
                       (unsigned int)(low & 0xffffU));
        if (!pe_metadata_key(record, key, offset, 8, pd)) return -1;
        if (!xx_var_set_str(&record->value, value)) {
            pe_stream_allocation_failed(pd, "Cannot allocate PE fixed version value");
            return -1;
        }
        cursor->fixed_stage = cursor->fixed_stage == 1U ? 2U : 0U;
        return 1;
    }
    while (cursor->version_depth) {
        pe_version_frame *parent = &cursor->version_frames[cursor->version_depth - 1U];
        pe_version_block block;
        bool string_value;
        bool strings;
        if (pe_stream_cancelled(pd)) return -1;
        if (parent->next >= parent->end) {
            xx_mem_free(parent->key);
            parent->key = NULL;
            --cursor->version_depth;
            continue;
        }
        if (!cursor->version_budget--) {
            pe_stream_fail(pd, "PE VERSIONINFO traversal budget exceeded");
            return -1;
        }
        if (!pe_version_parse(cursor, parent->next, parent->end, &block, pd)) return -1;
        parent->next = pe_version_align(cursor->version_base, block.end);
        if (parent->next > parent->end) parent->next = parent->end;
        strings = parent->strings || xx_rt_strcmp(block.key, "StringFileInfo") == 0;
        string_value = parent->strings && cursor->version_depth >= 3U && block.type == 1U;
        if (string_value) {
            char *value;
            record->key = pe_version_metadata_key(cursor, block.key, pd);
            record->offset = block.value;
            record->size = block.value_bytes;
            value = record->key ? pe_utf16(cursor->format, block.value, block.value_bytes / 2U, true, pd) : NULL;
            if (!value || !xx_var_set_str_take(&record->value, value, xx_rt_strlen(value))) {
                if (value) xx_mem_free(value);
                xx_mem_free(block.key);
                return -1;
            }
        }
        if (block.children < block.end) {
            pe_version_frame *child;
            if (cursor->version_depth >= PE_RESOURCE_DEPTH) {
                xx_mem_free(block.key);
                pe_stream_fail(pd, "PE VERSIONINFO depth limit exceeded");
                return -1;
            }
            child = &cursor->version_frames[cursor->version_depth++];
            child->end = block.end;
            child->next = block.children;
            child->key = block.key;
            child->strings = strings;
        } else xx_mem_free(block.key);
        if (string_value) return 1;
    }
    return 0;
}

static bool pe_metadata_header(pe_metadata_cursor *cursor, xx_metadata_record *record, xx_pd_struct *pd)
{
    xx_pe *pe = (xx_pe *)cursor->format;
    int64_t coff = cursor->format->base_address + pe->pe_offset + 4;
    int64_t optional = coff + 20;
    uint64_t value;
    const char *key;
    int64_t offset;
    int64_t size;
    switch (cursor->header_index++) {
        case 0:
            key = "pe.machine";
            value = pe->machine;
            offset = coff;
            size = 2;
            break;
        case 1:
            key = "pe.timestamp";
            value = pe->timestamp;
            offset = coff + 4;
            size = 4;
            break;
        case 2:
            key = "pe.characteristics";
            value = pe->characteristics;
            offset = coff + 18;
            size = 2;
            break;
        case 3:
            key = "pe.optional_magic";
            value = pe->optional_magic;
            offset = optional;
            size = 2;
            break;
        case 4:
            key = "pe.image_base";
            value = pe->image_base;
            offset = optional + (xx_pe_is_64(pe) ? 24 : 28);
            size = xx_pe_is_64(pe) ? 8 : 4;
            break;
        case 5:
            key = "pe.entry_point_rva";
            value = pe->entry_point_rva;
            offset = optional + 16;
            size = 4;
            break;
        case 6:
            key = "pe.section_alignment";
            value = pe->section_alignment;
            offset = optional + 32;
            size = 4;
            break;
        case 7:
            key = "pe.file_alignment";
            value = pe->file_alignment;
            offset = optional + 36;
            size = 4;
            break;
        case 8:
            key = "pe.size_of_image";
            value = pe->size_of_image;
            offset = optional + 56;
            size = 4;
            break;
        case 9:
            key = "pe.size_of_headers";
            value = pe->size_of_headers;
            offset = optional + 60;
            size = 4;
            break;
        case 10:
            key = "pe.subsystem";
            value = pe->subsystem;
            offset = optional + 68;
            size = 2;
            break;
        case 11:
            key = "pe.dll_characteristics";
            value = pe->dll_characteristics;
            offset = optional + 70;
            size = 2;
            break;
        case 12:
            key = "pe.number_of_sections";
            value = pe->number_of_sections;
            offset = coff + 2;
            size = 2;
            break;
        default: return false;
    }
    if (!pe_metadata_key(record, key, offset, size, pd)) return false;
    xx_var_set_u64(&record->value, value);
    return true;
}

static bool pe_metadata_manifest(pe_metadata_cursor *cursor, const xx_resource_record *resource, xx_metadata_record *record, xx_pd_struct *pd)
{
    uint8_t bom[2] = {0U, 0U};
    char *text;
    size_t length;
    if (resource->size > PE_METADATA_TEXT_LIMIT) return pe_stream_fail(pd, "PE manifest exceeds streaming text limit");
    if (resource->size >= 2 && !xx_pe_stream_read(cursor->format, resource->offset, bom, 2U, pd)) return false;
    if (resource->size >= 2 && bom[0] == 0xffU && bom[1] == 0xfeU) {
        if (resource->size & 1) return pe_stream_fail(pd, "Truncated UTF-16 PE manifest");
        text = pe_utf16(cursor->format, resource->offset + 2, (uint32_t)(resource->size - 2) / 2U, true, pd);
        if (!text) return false;
        length = xx_rt_strlen(text);
    } else {
        length = (size_t)resource->size;
        text = (char *)xx_mem_alloc(length + 1U);
        if (!text) return pe_stream_allocation_failed(pd, "Cannot allocate PE manifest");
        if (length && !xx_pe_stream_read(cursor->format, resource->offset, text, length, pd)) {
            xx_mem_free(text);
            return false;
        }
        while (length && text[length - 1U] == '\0') --length;
        text[length] = '\0';
    }
    if (!pe_metadata_key(record, "pe.manifest", resource->offset, resource->size, pd) || !xx_var_set_str_take(&record->value, text, length)) {
        xx_mem_free(text);
        return false;
    }
    return true;
}

static int pe_metadata_debug(pe_metadata_cursor *cursor, xx_metadata_record *record, xx_pd_struct *pd)
{
    xx_pe *pe = (xx_pe *)cursor->format;
    uint32_t rva = pe->data_directory_rva[6];
    uint32_t size = pe->data_directory_size[6];
    if (!rva && !size) return 0;
    if (!rva || size % 28U) return pe_stream_fail(pd, "Malformed PE debug directory") ? 0 : -1;
    while (cursor->debug_index < size / 28U) {
        int64_t entry;
        int64_t payload;
        uint32_t type;
        uint32_t payload_size;
        uint32_t payload_rva;
        uint32_t raw;
        uint32_t signature;
        uint32_t header;
        uint32_t length;
        char *path;
        if (pe_stream_cancelled(pd)) return -1;
        if (cursor->debug_index >= PE_METADATA_BUDGET) {
            pe_stream_fail(pd, "PE debug traversal budget exceeded");
            return -1;
        }
        if (!xx_pe_stream_rva(cursor->format, (uint64_t)rva + (uint64_t)cursor->debug_index++ * 28U, 28U, &entry, pd) ||
            !pe_read_u32(cursor->format, entry + 12, &type, pd) || !pe_read_u32(cursor->format, entry + 16, &payload_size, pd) ||
            !pe_read_u32(cursor->format, entry + 20, &payload_rva, pd) || !pe_read_u32(cursor->format, entry + 24, &raw, pd))
            return -1;
        if (type != 2U || !payload_size) continue;
        if (cursor->format->is_mapped || !raw) {
            if (!xx_pe_stream_rva(cursor->format, payload_rva, payload_size, &payload, pd)) return -1;
        } else {
            if (cursor->format->base_address > INT64_MAX - raw) {
                pe_stream_fail(pd, "Invalid PE CodeView file offset");
                return -1;
            }
            payload = cursor->format->base_address + raw;
        }
        if (payload_size < 4U) {
            pe_stream_fail(pd, "Truncated PE CodeView signature");
            return -1;
        }
        if (!pe_read_u32(cursor->format, payload, &signature, pd)) return -1;
        if (signature == UINT32_C(0x53445352)) header = 24U;
        else if (signature == UINT32_C(0x3031424e)) header = 16U;
        else continue;
        if (payload_size <= header || payload_size - header > PE_METADATA_TEXT_LIMIT) {
            pe_stream_fail(pd, "Malformed PE CodeView path");
            return -1;
        }
        length = payload_size - header;
        path = (char *)xx_mem_alloc((size_t)length + 1U);
        if (!path) {
            pe_stream_allocation_failed(pd, "Cannot allocate PE CodeView path");
            return -1;
        }
        if (!xx_pe_stream_read(cursor->format, payload + header, path, length, pd)) {
            xx_mem_free(path);
            return -1;
        }
        path[length] = '\0';
        if (!xx_rt_memchr(path, 0, length)) {
            xx_mem_free(path);
            pe_stream_fail(pd, "Unterminated PE CodeView path");
            return -1;
        }
        length = (uint32_t)xx_rt_strlen(path);
        if (!pe_metadata_key(record, "pe.debug.pdb_path", payload + header, (int64_t)length + 1, pd) || !xx_var_set_str_take(&record->value, path, length)) {
            xx_mem_free(path);
            return -1;
        }
        return 1;
    }
    return 0;
}

static void pe_metadata_cursor_free(void *pointer)
{
    pe_metadata_cursor *cursor = (pe_metadata_cursor *)pointer;
    if (!cursor) return;
    pe_version_clear(cursor);
    if (cursor->resources) pe_resource_stream_free(cursor->format, cursor->resources);
    xx_mem_free(cursor);
}

static bool pe_metadata_next(xx_metadata_state *state, xx_pd_struct *pd)
{
    pe_metadata_cursor *cursor = (pe_metadata_cursor *)state->internal_state;
    xx_metadata_record_cleanup(&state->current_record);
    state->has_record = false;
    if (state->failed || cursor->finished) return false;
    if (pe_stream_cancelled(pd)) goto fail;
    if (cursor->phase == 0U) {
        if (cursor->header_index < 13U) {
            if (!pe_metadata_header(cursor, &state->current_record, pd)) goto fail;
            goto found;
        }
        cursor->phase = 1U;
        cursor->resources = pe_resource_stream_create(cursor->format, pd);
        if (!cursor->resources) goto fail;
    }
    if (cursor->phase == 1U) {
        for (;;) {
            const xx_resource_record *resource;
            int version_result;
            if (pe_stream_cancelled(pd)) goto fail;
            if (cursor->version_depth || cursor->fixed_stage) {
                version_result = pe_version_next(cursor, &state->current_record, pd);
                if (version_result < 0) goto fail;
                if (version_result > 0) goto found;
            }
            if (cursor->resource_consumed) {
                if (!pe_resource_stream_next(cursor->format, cursor->resources, pd)) {
                    if (cursor->resources->failed) goto fail;
                    cursor->phase = 2U;
                    break;
                }
                cursor->resource_consumed = false;
            }
            resource = pe_resource_stream_current(cursor->format, cursor->resources);
            if (!resource) {
                if (cursor->resources->failed) goto fail;
                cursor->phase = 2U;
                break;
            }
            cursor->resource_consumed = true;
            if (resource->type_id == 16U) {
                if (!pe_version_begin(cursor, resource, pd)) goto fail;
            } else if (resource->type_id == 24U) {
                if (!pe_metadata_manifest(cursor, resource, &state->current_record, pd)) goto fail;
                goto found;
            }
        }
    }
    if (cursor->phase == 2U) {
        int debug_result = pe_metadata_debug(cursor, &state->current_record, pd);
        if (debug_result < 0) goto fail;
        if (debug_result > 0) goto found;
        cursor->phase = 3U;
    }
    cursor->finished = true;
    state->total_records = state->current_index + 1;
    return false;
found:
    ++state->current_index;
    state->has_record = true;
    return true;
fail:
    xx_metadata_record_cleanup(&state->current_record);
    state->failed = true;
    cursor->finished = true;
    return false;
}

xx_metadata_state *xx_pe_create_metadata_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_metadata_state *state;
    pe_metadata_cursor *cursor;
    if (!xx_pe_stream_prepare(format, pd)) return NULL;
    state = (xx_metadata_state *)xx_mem_calloc(1U, sizeof(*state));
    cursor = (pe_metadata_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        if (state) xx_mem_free(state);
        if (cursor) xx_mem_free(cursor);
        pe_stream_allocation_failed(pd, "Cannot allocate PE metadata cursor");
        return NULL;
    }
    state->format = format;
    state->current_index = -1;
    state->total_records = -1;
    state->internal_state = cursor;
    state->free_internal = pe_metadata_cursor_free;
    cursor->format = format;
    if (!pe_metadata_next(state, pd)) {
        xx_metadata_state_free(state);
        return NULL;
    }
    return state;
}

const xx_metadata_record *xx_pe_get_current_metadata(Abstractformat *format, xx_metadata_state *state)
{
    return state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_pe_metadata_move_to_next(Abstractformat *format, xx_metadata_state *state, xx_pd_struct *pd)
{
    if (!state || state->format != format || !state->internal_state) return false;
    return pe_metadata_next(state, pd);
}

void xx_pe_free_metadata_reading(Abstractformat *format, xx_metadata_state *state)
{
    (void)format;
    xx_metadata_state_free(state);
}

uint64_t xx_pe_get_number_of_metadata(Abstractformat *format, xx_pd_struct *pd)
{
    xx_metadata_state *state = xx_format_create_metadata_reading(format, pd);
    uint64_t count = 0;
    bool failed;
    if (!state) return 0;
    while (xx_format_get_current_metadata(format, state)) {
        ++count;
        if (!xx_format_metadata_move_to_next(format, state, pd)) break;
    }
    failed = state->failed;
    xx_format_free_metadata_reading(format, state);
    if (failed) return 0;
    format->number_of_metadata = count;
    return count;
}

const StreamMetadata *xx_pe_get_stream_metadata(void)
{
    static const StreamMetadata stream = {.get_number_of_metadata = xx_pe_get_number_of_metadata,
                                          .create_metadata_reading = xx_pe_create_metadata_reading,
                                          .get_current_metadata = xx_pe_get_current_metadata,
                                          .metadata_move_to_next = xx_pe_metadata_move_to_next,
                                          .free_metadata_reading = xx_pe_free_metadata_reading};
    return &stream;
}
