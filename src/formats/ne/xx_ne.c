/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ne/xx_ne.h"
#include "xx_ne_defs.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct xx_ne_data_stream_s {
    xx_data_struct *items;
    size_t count;
} xx_ne_data_stream;

typedef struct xx_ne_record_stream_s {
    const xx_data_struct_field_desc *fields;
    size_t count;
} xx_ne_record_stream;

static bool xx_ne_range(const Abstractformat *format, uint64_t relative,
                        uint64_t size, int64_t *absolute) {
    int64_t total;
    uint64_t available;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (relative > available || size > available - relative ||
        relative > (uint64_t)(INT64_MAX - format->base_address)) return false;
    if (absolute) *absolute = format->base_address + (int64_t)relative;
    return true;
}

static bool xx_ne_add_u64(uint64_t left, uint64_t right, uint64_t *result) {
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static void xx_ne_parsed_cleanup(xx_ne *parsed) {
    if (parsed && parsed->segments) {
        xx_mem_free(parsed->segments);
        parsed->segments = NULL;
    }
}

static xx_os_t xx_ne_os(uint8_t target) {
    switch (target) {
        case 1:
        case 0x81: return XX_OS_OS2;
        case 2:
        case 4:
        case 0x82: return XX_OS_WINDOWS;
        case 3: return XX_OS_DOS;
        default: return XX_OS_UNKNOWN;
    }
}

static bool xx_ne_parse(Abstractformat *format, xx_ne *parsed,
                        xx_pd_struct *pd) {
    int64_t total;
    uint64_t available;
    uint64_t ne_offset;
    uint64_t table_relative;
    uint64_t table_size;
    uint64_t format_end;
    uint64_t import_count = 0U;
    uint64_t export_count = 0U;
    uint64_t resource_count = 0U;
    int64_t absolute;
    uint16_t index;

    if (!parsed) return false;
    xx_mem_zero(parsed, sizeof(*parsed));
    if (!format || !format->device || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (available < XX_NE_DOS_HEADER_SIZE ||
        xx_io_get_u16(format->device, format->base_address, false) !=
            UINT16_C(0x5a4d)) return false;

    ne_offset = xx_io_get_u32(format->device, format->base_address + 0x3c,
                              false);
    if (!xx_ne_range(format, ne_offset, XX_NE_HEADER_SIZE, &absolute) ||
        xx_io_get_u16(format->device, absolute, false) != XX_NE_SIGNATURE)
        return false;

    parsed->ne_offset = (uint32_t)ne_offset;
    parsed->entry_table_offset = xx_io_get_u16(format->device, absolute + 4,
                                                false);
    parsed->entry_table_size = xx_io_get_u16(format->device, absolute + 6,
                                              false);
    parsed->flags = xx_io_get_u16(format->device, absolute + 0x0c, false);
    parsed->initial_cs_ip = xx_io_get_u32(format->device, absolute + 0x14,
                                           false);
    parsed->initial_ss_sp = xx_io_get_u32(format->device, absolute + 0x18,
                                           false);
    parsed->segment_count = xx_io_get_u16(format->device, absolute + 0x1c,
                                           false);
    parsed->module_reference_count =
        xx_io_get_u16(format->device, absolute + 0x1e, false);
    parsed->nonresident_name_table_size =
        xx_io_get_u16(format->device, absolute + 0x20, false);
    parsed->segment_table_offset =
        xx_io_get_u16(format->device, absolute + 0x22, false);
    parsed->resource_table_offset =
        xx_io_get_u16(format->device, absolute + 0x24, false);
    parsed->resident_name_table_offset =
        xx_io_get_u16(format->device, absolute + 0x26, false);
    parsed->module_reference_table_offset =
        xx_io_get_u16(format->device, absolute + 0x28, false);
    parsed->imported_name_table_offset =
        xx_io_get_u16(format->device, absolute + 0x2a, false);
    parsed->nonresident_name_table_offset =
        xx_io_get_u32(format->device, absolute + 0x2c, false);
    parsed->movable_entry_count =
        xx_io_get_u16(format->device, absolute + 0x30, false);
    parsed->alignment_shift =
        xx_io_get_u16(format->device, absolute + 0x32, false);
    parsed->resource_segment_count =
        xx_io_get_u16(format->device, absolute + 0x34, false);
    parsed->target_os = xx_io_get_u8(format->device, absolute + 0x36);
    parsed->other_flags = xx_io_get_u8(format->device, absolute + 0x37);
    parsed->expected_version =
        xx_io_get_u16(format->device, absolute + 0x3e, false);
    if (parsed->alignment_shift > 47U) return false;

    table_relative = ne_offset + parsed->segment_table_offset;
    table_size = (uint64_t)parsed->segment_count * XX_NE_SEGMENT_RECORD_SIZE;
    if (!xx_ne_range(format, table_relative, table_size, &absolute))
        return false;
    if (parsed->segment_count != 0U) {
        parsed->segments = (xx_ne_segment *)xx_mem_alloc(
            (size_t)parsed->segment_count * sizeof(*parsed->segments));
        if (!parsed->segments) return false;
        xx_mem_zero(parsed->segments,
                    (size_t)parsed->segment_count * sizeof(*parsed->segments));
    }

    format_end = ne_offset + XX_NE_HEADER_SIZE;
    if (table_relative + table_size > format_end)
        format_end = table_relative + table_size;
    for (index = 0U; index < parsed->segment_count; ++index) {
        xx_ne_segment *segment = &parsed->segments[index];
        uint64_t record_relative = table_relative +
            (uint64_t)index * XX_NE_SEGMENT_RECORD_SIZE;
        uint64_t segment_relative;
        uint64_t declared_size;
        uint64_t clipped_size = 0U;
        uint64_t segment_end;
        int64_t record_absolute;
        (void)xx_ne_range(format, record_relative,
                          XX_NE_SEGMENT_RECORD_SIZE, &record_absolute);
        segment->sector_offset = xx_io_get_u16(format->device,
                                                record_absolute, false);
        segment->file_size_field = xx_io_get_u16(format->device,
                                                  record_absolute + 2, false);
        segment->flags = xx_io_get_u16(format->device,
                                        record_absolute + 4, false);
        segment->minimum_allocation = xx_io_get_u16(
            format->device, record_absolute + 6, false);
        declared_size = segment->file_size_field
                            ? segment->file_size_field : UINT64_C(0x10000);
        if (segment->sector_offset == 0U ||
            (uint64_t)segment->sector_offset >
                (UINT64_MAX >> parsed->alignment_shift)) {
            segment->file_offset = -1;
            segment->file_size = 0;
            continue;
        }
        segment_relative = (uint64_t)segment->sector_offset
                           << parsed->alignment_shift;
        if (segment_relative < available) {
            clipped_size = declared_size;
            if (clipped_size > available - segment_relative)
                clipped_size = available - segment_relative;
        }
        segment->file_offset = clipped_size != 0U
                                   ? (int64_t)segment_relative : -1;
        segment->file_size = (int64_t)clipped_size;
        if (clipped_size != 0U &&
            xx_ne_add_u64(segment_relative, clipped_size, &segment_end) &&
            segment_end > format_end) format_end = segment_end;

        if ((segment->flags & XX_NE_SEGMENT_HAS_RELOCATIONS) != 0U &&
            xx_ne_add_u64(segment_relative, declared_size, &segment_end) &&
            xx_ne_range(format, segment_end, 2U, &record_absolute)) {
            uint16_t relocation_count = xx_io_get_u16(
                format->device, record_absolute, false);
            uint64_t relocation_size =
                UINT64_C(2) + (uint64_t)relocation_count * UINT64_C(8);
            if (xx_ne_range(format, segment_end, relocation_size,
                            &record_absolute)) {
                uint16_t relocation_index;
                uint64_t relocation_end = segment_end + relocation_size;
                if (relocation_end > format_end) format_end = relocation_end;
                for (relocation_index = 0U;
                     relocation_index < relocation_count;
                     ++relocation_index) {
                    uint8_t target = xx_io_get_u8(
                        format->device, record_absolute + 2 +
                        (int64_t)relocation_index * 8 + 1) & 3U;
                    if (target == 1U || target == 2U) ++import_count;
                }
            }
        }
    }

    if (xx_ne_add_u64(ne_offset, parsed->entry_table_offset,
                      &table_relative) &&
        xx_ne_range(format, table_relative, parsed->entry_table_size,
                    &absolute)) {
        uint64_t current = 0U;
        while (current + 2U <= parsed->entry_table_size) {
            uint8_t count = xx_io_get_u8(format->device,
                                          absolute + (int64_t)current);
            uint8_t type = xx_io_get_u8(format->device,
                                         absolute + (int64_t)current + 1);
            uint64_t record_size;
            current += 2U;
            if (count == 0U) break;
            if (type == 0U) continue;
            record_size = type == 0xffU ? 6U : 3U;
            if ((uint64_t)count >
                (parsed->entry_table_size - current) / record_size) break;
            export_count += count;
            current += (uint64_t)count * record_size;
        }
        if (table_relative + parsed->entry_table_size > format_end)
            format_end = table_relative + parsed->entry_table_size;
    }

    if (xx_ne_add_u64(ne_offset, parsed->resource_table_offset,
                      &table_relative) &&
        xx_ne_range(format, table_relative, 2U, &absolute)) {
        uint16_t resource_shift = xx_io_get_u16(format->device, absolute,
                                                 false);
        uint64_t current = table_relative + 2U;
        if (resource_shift <= 47U) {
            while (xx_ne_range(format, current,
                               XX_NE_RESOURCE_TYPE_SIZE, &absolute)) {
                uint16_t type = xx_io_get_u16(format->device, absolute, false);
                uint16_t count;
                uint64_t names_size;
                uint16_t resource_index;
                if (type == 0U) {
                    current += 2U;
                    if (current > format_end) format_end = current;
                    break;
                }
                count = xx_io_get_u16(format->device, absolute + 2, false);
                names_size = (uint64_t)count * XX_NE_RESOURCE_NAME_SIZE;
                if (!xx_ne_range(format, current + XX_NE_RESOURCE_TYPE_SIZE,
                                 names_size, &absolute)) break;
                resource_count += count;
                for (resource_index = 0U; resource_index < count;
                     ++resource_index) {
                    int64_t name_absolute = absolute +
                        (int64_t)resource_index * XX_NE_RESOURCE_NAME_SIZE;
                    uint64_t resource_relative =
                        (uint64_t)xx_io_get_u16(format->device,
                                               name_absolute, false)
                        << resource_shift;
                    uint64_t resource_size =
                        (uint64_t)xx_io_get_u16(format->device,
                                               name_absolute + 2, false)
                        << resource_shift;
                    uint64_t resource_end;
                    if (xx_ne_range(format, resource_relative, resource_size,
                                    NULL) &&
                        xx_ne_add_u64(resource_relative, resource_size,
                                      &resource_end) &&
                        resource_end > format_end)
                        format_end = resource_end;
                }
                current += XX_NE_RESOURCE_TYPE_SIZE + names_size;
                if (current > format_end) format_end = current;
            }
        }
    }

    if (parsed->nonresident_name_table_offset != 0U &&
        xx_ne_range(format, parsed->nonresident_name_table_offset,
                    parsed->nonresident_name_table_size, NULL)) {
        uint64_t end = (uint64_t)parsed->nonresident_name_table_offset +
                       parsed->nonresident_name_table_size;
        if (end > format_end) format_end = end;
    }

    if (format_end > available) format_end = available;
    parsed->format.format_size = (int64_t)format_end;
    parsed->format.overlay_offset = format_end < available
        ? format->base_address + (int64_t)format_end : -1;
    parsed->format.overlay_size = format_end < available
        ? (int64_t)(available - format_end) : 0;
    parsed->format.number_of_imports = import_count != 0U
        ? import_count : parsed->module_reference_count;
    parsed->format.number_of_exports = export_count;
    parsed->format.number_of_resources = resource_count;
    return !xx_pd_is_stopped(pd);
}

static const char *xx_ne_data_struct_id_to_string(Abstractformat *format,
                                                   uint32_t id) {
    (void)format;
    switch (id) {
        case XX_NE_DATA_STRUCT_DOS_HEADER: return "IMAGE_DOS_HEADER";
        case XX_NE_DATA_STRUCT_HEADER: return "IMAGE_OS2_HEADER";
        case XX_NE_DATA_STRUCT_SEGMENT_TABLE: return "NE_SEGMENT_TABLE";
        case XX_NE_DATA_STRUCT_RESOURCE_TABLE: return "NE_RESOURCE_TABLE";
        case XX_NE_DATA_STRUCT_ENTRY_TABLE: return "NE_ENTRY_TABLE";
        case XX_NE_DATA_STRUCT_NONRESIDENT_NAME_TABLE:
            return "NE_NONRESIDENT_NAME_TABLE";
        case XX_DATA_STRUCT_ID_RAW_DATA: return "RAW_DATA";
        default: return "UNKNOWN";
    }
}

static uint32_t xx_ne_data_struct_string_to_id(Abstractformat *format,
                                                const char *name) {
    uint32_t id;
    (void)format;
    if (!name) return XX_NE_DATA_STRUCT_UNKNOWN;
    for (id = XX_NE_DATA_STRUCT_DOS_HEADER;
         id <= XX_NE_DATA_STRUCT_NONRESIDENT_NAME_TABLE; ++id) {
        if (xx_rt_strcmp(name, xx_ne_data_struct_id_to_string(format, id)) == 0)
            return id;
    }
    return xx_rt_strcmp(name, "RAW_DATA") == 0
               ? XX_DATA_STRUCT_ID_RAW_DATA : XX_NE_DATA_STRUCT_UNKNOWN;
}

static void xx_ne_data_stream_free(void *pointer) {
    xx_ne_data_stream *stream = (xx_ne_data_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_ne_data_stream_set(xx_data_struct_state *state,
                                  size_t index) {
    xx_ne_data_stream *stream;
    if (!state || !(stream = (xx_ne_data_stream *)state->internal_state) ||
        index >= stream->count) return false;
    state->current_struct = stream->items[index];
    state->current_index = (int64_t)index;
    state->has_struct = true;
    return true;
}

static bool xx_ne_append_data_struct(xx_ne_data_stream *stream,
                                     uint32_t id, int64_t offset,
                                     int64_t entry_size, uint64_t count,
                                     xx_data_struct_type_t type) {
    xx_data_struct *item;
    if (!stream || !stream->items || stream->count >= 8U ||
        entry_size < 0 || count > (uint64_t)INT64_MAX ||
        (count != 0U && (uint64_t)entry_size >
             (uint64_t)INT64_MAX / count)) return false;
    item = &stream->items[stream->count++];
    xx_mem_zero(item, sizeof(*item));
    item->id = id;
    item->offset = offset;
    item->address = -1;
    item->entry_size = entry_size;
    item->count = count;
    item->total_size = entry_size * (int64_t)count;
    item->type = type;
    return true;
}

static xx_data_struct_state *xx_ne_create_data_structs_reading(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_ne *ne = (xx_ne *)format;
    xx_data_struct_state *state;
    xx_ne_data_stream *stream;
    uint64_t ne_base;
    if (!format || (!format->base_info_handled &&
                    !xx_ne_handle_base_info(format, pd))) return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ne_data_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_data_struct *)xx_mem_alloc(8U * sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_mem_free(state);
        return NULL;
    }
    ne_base = (uint64_t)format->base_address + ne->ne_offset;
    (void)xx_ne_append_data_struct(stream, XX_NE_DATA_STRUCT_DOS_HEADER,
                                   format->base_address, 64, 1,
                                   XX_DATA_STRUCT_TYPE_STRUCT);
    (void)xx_ne_append_data_struct(stream, XX_NE_DATA_STRUCT_HEADER,
                                   (int64_t)ne_base, 64, 1,
                                   XX_DATA_STRUCT_TYPE_STRUCT);
    if (ne->segment_count != 0U)
        (void)xx_ne_append_data_struct(
            stream, XX_NE_DATA_STRUCT_SEGMENT_TABLE,
            (int64_t)(ne_base + ne->segment_table_offset), 8,
            ne->segment_count, XX_DATA_STRUCT_TYPE_ENTRY);
    if (ne->entry_table_size != 0U)
        (void)xx_ne_append_data_struct(
            stream, XX_NE_DATA_STRUCT_ENTRY_TABLE,
            (int64_t)(ne_base + ne->entry_table_offset),
            ne->entry_table_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
    if (ne->resource_table_offset != 0U &&
        ne->resident_name_table_offset > ne->resource_table_offset)
        (void)xx_ne_append_data_struct(
            stream, XX_NE_DATA_STRUCT_RESOURCE_TABLE,
            (int64_t)(ne_base + ne->resource_table_offset),
            ne->resident_name_table_offset - ne->resource_table_offset, 1,
            XX_DATA_STRUCT_TYPE_RAW_DATA);
    if (ne->nonresident_name_table_offset != 0U &&
        ne->nonresident_name_table_size != 0U)
        (void)xx_ne_append_data_struct(
            stream, XX_NE_DATA_STRUCT_NONRESIDENT_NAME_TABLE,
            format->base_address + ne->nonresident_name_table_offset,
            ne->nonresident_name_table_size, 1,
            XX_DATA_STRUCT_TYPE_RAW_DATA);
    if (format->overlay_size > 0)
        (void)xx_ne_append_data_struct(stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                       format->overlay_offset,
                                       format->overlay_size, 1,
                                       XX_DATA_STRUCT_TYPE_RAW_DATA);

    xx_data_struct_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_ne_data_stream_free;
    state->total_structs = (int64_t)stream->count;
    if (!xx_ne_data_stream_set(state, 0U)) {
        xx_data_struct_state_free(state);
        return NULL;
    }
    return state;
}

static const xx_data_struct *xx_ne_get_current_data_struct(
    Abstractformat *format, xx_data_struct_state *state) {
    return format && state && state->format == format && state->has_struct
               ? &state->current_struct : NULL;
}

static bool xx_ne_data_struct_move_to_next(Abstractformat *format,
                                            xx_data_struct_state *state,
                                            xx_pd_struct *pd) {
    (void)pd;
    if (!format || !state || state->format != format ||
        state->current_index < 0) return false;
    if (!xx_ne_data_stream_set(state,
                               (size_t)state->current_index + 1U)) {
        state->has_struct = false;
        return false;
    }
    return true;
}

static void xx_ne_free_data_structs_reading(Abstractformat *format,
                                             xx_data_struct_state *state) {
    (void)format;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc xx_ne_dos_fields[] = {
    {L"e_magic", L"uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"e_cblp", L"uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e_cp", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"e_crlc", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"e_cparhdr", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e_lfarlc", L"uint16", 24, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e_lfanew", L"uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER}
};

static const xx_data_struct_field_desc xx_ne_header_fields[] = {
    {L"ne_magic", L"uint16", 0x00, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"ne_ver", L"uint8", 0x02, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"ne_rev", L"uint8", 0x03, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"ne_enttab", L"uint16", 0x04, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"ne_cbenttab", L"uint16", 0x06, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"ne_flags", L"uint16", 0x0c, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"ne_csip", L"uint32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
    {L"ne_sssp", L"uint32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
    {L"ne_cseg", L"uint16", 0x1c, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"ne_cmod", L"uint16", 0x1e, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"ne_cbnrestab", L"uint16", 0x20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"ne_segtab", L"uint16", 0x22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"ne_rsrctab", L"uint16", 0x24, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"ne_restab", L"uint16", 0x26, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"ne_modtab", L"uint16", 0x28, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"ne_imptab", L"uint16", 0x2a, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"ne_nrestab", L"uint32", 0x2c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"ne_cmovent", L"uint16", 0x30, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"ne_align", L"uint16", 0x32, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"ne_cres", L"uint16", 0x34, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"ne_exetyp", L"uint8", 0x36, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"ne_flagsothers", L"uint8", 0x37, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"ne_expver", L"uint16", 0x3e, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE}
};

static const xx_data_struct_field_desc xx_ne_segment_fields[] = {
    {L"FileOffset", L"uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"FileSize", L"uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"Flags", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"MinimumAllocation", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE}
};

static void xx_ne_record_stream_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

static bool xx_ne_populate_record(Abstractformat *format,
                                   xx_data_struct_record_state *state,
                                   size_t index) {
    xx_ne_record_stream *stream;
    if (!format || !format->device || !state ||
        !(stream = (xx_ne_record_stream *)state->internal_state) ||
        index >= stream->count) return false;
    return xx_data_struct_record_populate(&state->current_record,
                                           format->device,
                                           state->parent_struct.offset,
                                           &stream->fields[index], false);
}

static xx_data_struct_record_state *xx_ne_create_data_struct_records_reading(
    Abstractformat *format, const xx_data_struct *data_struct,
    xx_pd_struct *pd) {
    const xx_data_struct_field_desc *fields = NULL;
    size_t count = 0U;
    xx_data_struct_record_state *state;
    xx_ne_record_stream *stream;
    (void)pd;
    if (!format || !data_struct ||
        data_struct->type == XX_DATA_STRUCT_TYPE_RAW_DATA) return NULL;
    switch (data_struct->id) {
        case XX_NE_DATA_STRUCT_DOS_HEADER:
            fields = xx_ne_dos_fields;
            count = sizeof(xx_ne_dos_fields) / sizeof(xx_ne_dos_fields[0]);
            break;
        case XX_NE_DATA_STRUCT_HEADER:
            fields = xx_ne_header_fields;
            count = sizeof(xx_ne_header_fields) /
                    sizeof(xx_ne_header_fields[0]);
            break;
        case XX_NE_DATA_STRUCT_SEGMENT_TABLE:
            fields = xx_ne_segment_fields;
            count = sizeof(xx_ne_segment_fields) /
                    sizeof(xx_ne_segment_fields[0]);
            break;
        default: return NULL;
    }
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ne_record_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_record_state_init(state, format, data_struct);
    stream->fields = fields;
    stream->count = count;
    state->internal_state = stream;
    state->free_internal = xx_ne_record_stream_free;
    state->total_records = (int64_t)count;
    if (!xx_ne_populate_record(format, state, 0U)) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

static const xx_data_struct_record *xx_ne_get_current_data_struct_record(
    Abstractformat *format, xx_data_struct_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

static bool xx_ne_data_struct_record_move_to_next(
    Abstractformat *format, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    size_t next;
    (void)pd;
    if (!format || !state || state->format != format ||
        state->current_index < 0) return false;
    next = (size_t)state->current_index + 1U;
    if (next >= (size_t)state->total_records) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_ne_populate_record(format, state, next)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)next;
    state->has_record = true;
    return true;
}

static void xx_ne_free_data_struct_records_reading(
    Abstractformat *format, xx_data_struct_record_state *state) {
    (void)format;
    xx_data_struct_record_state_free(state);
}

static void xx_ne_vtable_destroy(Abstractformat *format) {
    xx_ne_destroy((xx_ne *)format);
}

void xx_ne_init(xx_ne *ne, xx_io_device *device, int64_t base_address) {
    if (!ne) return;
    xx_mem_zero(ne, sizeof(*ne));
    xx_format_init(&ne->format, device, base_address);
    ne->format.endian = XX_ENDIAN_LITTLE;
    ne->format.file_type = XX_FILE_TYPE_NE;
    ne->format.os = XX_OS_UNKNOWN;
    ne->format.arch = XX_ARCH_X86_16;
    ne->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    ne->format.is_executable = true;
    xx_format_set_mime_type(&ne->format, "application/x-ne-executable");
    xx_format_set_extension(&ne->format, "exe");
    ne->format.check_is_valid = xx_ne_check_is_valid;
    ne->format.handle_base_info = xx_ne_handle_base_info;
    ne->format.get_format_size = xx_ne_get_format_size;
    ne->format.get_number_of_imports = xx_ne_get_number_of_imports;
    ne->format.get_number_of_exports = xx_ne_get_number_of_exports;
    ne->format.get_number_of_resources = xx_ne_get_number_of_resources;
    ne->format.get_memory_map = xx_ne_get_memory_map;
    ne->format.data_struct_id_to_string = xx_ne_data_struct_id_to_string;
    ne->format.data_struct_string_to_id = xx_ne_data_struct_string_to_id;
    ne->format.create_data_structs_reading = xx_ne_create_data_structs_reading;
    ne->format.get_current_data_struct = xx_ne_get_current_data_struct;
    ne->format.data_struct_move_to_next = xx_ne_data_struct_move_to_next;
    ne->format.free_data_structs_reading = xx_ne_free_data_structs_reading;
    ne->format.create_data_struct_records_reading =
        xx_ne_create_data_struct_records_reading;
    ne->format.get_current_data_struct_record =
        xx_ne_get_current_data_struct_record;
    ne->format.data_struct_record_move_to_next =
        xx_ne_data_struct_record_move_to_next;
    ne->format.free_data_struct_records_reading =
        xx_ne_free_data_struct_records_reading;
    ne->format.destroy = xx_ne_vtable_destroy;
}

xx_ne *xx_ne_create(xx_io_device *device, int64_t base_address) {
    xx_ne *ne = (xx_ne *)xx_mem_alloc(sizeof(*ne));
    if (ne) xx_ne_init(ne, device, base_address);
    return ne;
}

void xx_ne_destroy(xx_ne *ne) {
    if (!ne) return;
    if (ne->segments) {
        xx_mem_free(ne->segments);
        ne->segments = NULL;
    }
    xx_format_cleanup_extra_parameters(&ne->format);
}

void xx_ne_free(xx_ne *ne) {
    if (!ne) return;
    xx_ne_destroy(ne);
    xx_mem_free(ne);
}

bool xx_ne_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xx_ne parsed;
    bool result = xx_ne_parse(format, &parsed, pd);
    xx_ne_parsed_cleanup(&parsed);
    return result;
}

bool xx_ne_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_ne parsed;
    xx_ne *ne;
    Abstractformat saved;
    bool parsed_ok;
    if (!format) return false;
    parsed_ok = xx_ne_parse(format, &parsed, pd);
    if (!parsed_ok) {
        xx_ne_parsed_cleanup(&parsed);
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
            xx_format_invalidate_memory_map(format);
        }
        return false;
    }
    ne = (xx_ne *)format;
    saved = ne->format;
    if (ne->segments) xx_mem_free(ne->segments);
    *ne = parsed;
    ne->format = saved;
    parsed.segments = NULL;
    ne->format.file_type = XX_FILE_TYPE_NE;
    ne->format.os = xx_ne_os(ne->target_os);
    ne->format.arch = ((ne->flags & XX_NE_FLAG_80386) != 0U ||
                       ne->target_os == 4U || ne->target_os == 5U)
                          ? XX_ARCH_X86 : XX_ARCH_X86_16;
    ne->format.format_type = (ne->flags & XX_NE_FLAG_LIBRARY) != 0U
                                 ? XX_TYPE_LIBRARY
                                 : XX_TYPE_CONSOLE_APPLICATION;
    if (ne->format.format_type == XX_TYPE_LIBRARY)
        xx_format_set_extension(&ne->format, "dll");
    else
        xx_format_set_extension(&ne->format, "exe");
    ne->format.format_size = parsed.format.format_size;
    ne->format.overlay_offset = parsed.format.overlay_offset;
    ne->format.overlay_size = parsed.format.overlay_size;
    ne->format.number_of_imports = parsed.format.number_of_imports;
    ne->format.number_of_exports = parsed.format.number_of_exports;
    ne->format.number_of_resources = parsed.format.number_of_resources;
    ne->format.is_valid = true;
    ne->format.base_info_handled = true;
    xx_format_invalidate_memory_map(&ne->format);
    return true;
}

int64_t xx_ne_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ne_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ne_get_number_of_imports(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ne_handle_base_info(format, pd))
               ? format->number_of_imports : 0U;
}

uint64_t xx_ne_get_number_of_exports(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ne_handle_base_info(format, pd))
               ? format->number_of_exports : 0U;
}

uint64_t xx_ne_get_number_of_resources(Abstractformat *format,
                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ne_handle_base_info(format, pd))
               ? format->number_of_resources : 0U;
}

bool xx_ne_get_memory_map(Abstractformat *format,
                          xx_memory_map_mode_t mode,
                          xx_memory_map *output,
                          xx_pd_struct *pd) {
    xx_ne *ne = (xx_ne *)format;
    int64_t total;
    int64_t binary_size;
    int64_t header_size;
    int64_t first_load = -1;
    uint64_t module;
    uint16_t index;
    if (!format || !output || !format->device ||
        !format->base_info_handled || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN)
        mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    if (mode != XX_MEMORY_MAP_MODE_SEGMENTS) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    binary_size = total - format->base_address;
    header_size = (int64_t)ne->ne_offset + XX_NE_HEADER_SIZE;
    if (header_size > binary_size) header_size = binary_size;
    module = format->module_address != XX_INVALID_ADDRESS
                 ? format->module_address : UINT64_C(0x10000);

    output->binary_offset = format->base_address;
    output->binary_size = binary_size;
    output->module_address = module;
    output->is_image = format->is_mapped;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = mode;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = format->base_address + header_size;

    if (!xx_memory_map_add_part(output, format->base_address, header_size,
                                module, 0x200,
                                XX_FILE_PART_HEADER, 0,
                                "NE header", false)) return false;
    for (index = 0U; index < ne->segment_count; ++index) {
        const xx_ne_segment *segment = &ne->segments[index];
        uint64_t displacement = ((uint64_t)index + 1U) << 16U;
        uint64_t address;
        int64_t offset = -1;
        char name[XX_MEMORY_RECORD_NAME_SIZE];
        if (!xx_ne_add_u64(module, displacement, &address) ||
            address == XX_INVALID_ADDRESS) return false;
        if (segment->file_offset >= 0) {
            if (segment->file_offset > INT64_MAX - format->base_address)
                return false;
            offset = format->base_address + segment->file_offset;
            if (first_load < 0 || offset < first_load) first_load = offset;
        }
        (void)xx_rt_snprintf(name, sizeof(name), "Segment %u",
                       (unsigned)index + 1U);
        if (!xx_memory_map_add_part(output, offset, segment->file_size,
                                    address, 0x10000,
                                    XX_FILE_PART_SEGMENT,
                                    (int32_t)index + 1, name, false))
            return false;
    }
    if (first_load >= 0) output->start_load_offset = first_load;
    {
        uint16_t entry_segment = (uint16_t)(ne->initial_cs_ip >> 16U);
        uint16_t entry_offset = (uint16_t)ne->initial_cs_ip;
        if (entry_segment != 0U && entry_segment <= ne->segment_count) {
            uint64_t code;
            if (xx_ne_add_u64(module,
                              (uint64_t)entry_segment << 16U, &code) &&
                code <= (uint64_t)INT64_MAX) {
                output->code_base = (int64_t)code;
                (void)xx_ne_add_u64(code, entry_offset,
                                    &output->entry_point_address);
            }
        }
    }
    if (format->overlay_size > 0 &&
        !xx_memory_map_add_part(output, format->overlay_offset,
                                format->overlay_size,
                                XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_OVERLAY,
                                (int32_t)ne->segment_count + 1,
                                "Overlay", false)) return false;
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

uint16_t xx_ne_get_number_of_segments(const xx_ne *ne) {
    return ne ? ne->segment_count : 0U;
}

const xx_ne_segment *xx_ne_get_segment(const xx_ne *ne, uint16_t index) {
    return ne && ne->segments && index < ne->segment_count
               ? &ne->segments[index] : NULL;
}
