/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ne_private.h"
#include "xxfclib/formats/executable/ne/xx_ne.h"
#include "xx_ne_defs.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool xx_ne_range(const Abstractformat *format, uint64_t relative, uint64_t size, int64_t *absolute)
{
    int64_t total;
    uint64_t available;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (relative > available || size > available - relative || relative > (uint64_t)(INT64_MAX - format->base_address)) return false;
    if (absolute) *absolute = format->base_address + (int64_t)relative;
    return true;
}

static bool xx_ne_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool xx_ne_header_at(const Abstractformat *format, uint64_t relative, int64_t *absolute)
{
    int64_t offset;
    if (!xx_ne_range(format, relative, XX_NE_HEADER_SIZE, &offset) || xx_io_get_u16(format->device, offset, false) != XX_NE_SIGNATURE) return false;
    if (absolute) *absolute = offset;
    return true;
}

/* Old NE linkers stored a 16-bit header pointer at 0x3c. Some damaged files
 * retain a valid low word while the high word contains unrelated bytes. Try
 * the full pointer first, then its low word only when the full pointer does not
 * identify a complete NE header. */
static bool xx_ne_header_offset(const Abstractformat *format, uint64_t *relative, int64_t *absolute)
{
    uint32_t candidate;
    uint16_t legacy;
    if (!format || !relative || !absolute || !xx_ne_range(format, 0U, XX_NE_DOS_HEADER_SIZE, NULL) ||
        xx_io_get_u16(format->device, format->base_address, false) != UINT16_C(0x5a4d))
        return false;
    candidate = xx_io_get_u32(format->device, format->base_address + 0x3c, false);
    if (xx_ne_header_at(format, candidate, absolute)) {
        *relative = candidate;
        return true;
    }
    legacy = (uint16_t)candidate;
    if ((uint32_t)legacy != candidate && xx_ne_header_at(format, legacy, absolute)) {
        *relative = legacy;
        return true;
    }
    return false;
}

static void xx_ne_parsed_cleanup(xx_ne *parsed)
{
    if (!parsed) return;
    if (parsed->segments) xx_mem_free(parsed->segments);
    if (parsed->entries) xx_mem_free(parsed->entries);
    parsed->segments = NULL;
    parsed->entries = NULL;
}

static bool xx_ne_entry_offset(const xx_ne *parsed, uint16_t segment_index, uint16_t segment_offset, uint64_t *offset)
{
    const xx_ne_segment *segment;
    uint64_t base;
    if (!parsed || !offset || segment_index == 0U || segment_index > parsed->parsed_segment_count) return false;
    segment = &parsed->segments[segment_index - 1U];
    base = (uint64_t)segment->sector_offset << parsed->effective_alignment_shift;
    return xx_ne_add_u64(base, segment_offset, offset);
}

static bool xx_ne_parse_entries(Abstractformat *format, xx_ne *parsed, uint64_t available, uint64_t *format_end)
{
    uint64_t table_relative;
    uint64_t table_size;
    uint64_t current = 0U;
    uint64_t capacity;
    uint32_t ordinal = 1U;
    int64_t absolute;
    uint16_t initial_segment;
    uint16_t initial_offset;

    if (!format || !parsed || !format_end || !xx_ne_add_u64(parsed->ne_offset, parsed->entry_table_offset, &table_relative) ||
        table_relative > available)
        return true;
    table_size = parsed->entry_table_size;
    if (table_size > available - table_relative) table_size = available - table_relative;
    parsed->parsed_entry_table_size = (uint32_t)table_size;
    if (table_relative + table_size > *format_end) *format_end = table_relative + table_size;

    /* Every materialized table record consumes at least three bytes. */
    capacity = table_size / 3U + 2U;
    if (capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
    parsed->entries = (xx_ne_entry *)xx_mem_calloc((size_t)capacity, sizeof(*parsed->entries));
    if (!parsed->entries) return false;

    initial_segment = (uint16_t)(parsed->initial_cs_ip >> 16U);
    initial_offset = (uint16_t)parsed->initial_cs_ip;
    if (initial_segment != 0U) {
        xx_ne_entry *entry = &parsed->entries[parsed->entry_count++];
        entry->segment_index = initial_segment;
        entry->segment_offset = initial_offset;
        entry->offset = initial_offset;
        entry->is_initial = true;
        (void)xx_ne_entry_offset(parsed, initial_segment, initial_offset, &entry->offset);
    }

    if (!xx_ne_range(format, table_relative, table_size, &absolute)) return false;
    while (current + 2U <= table_size) {
        uint8_t count = xx_io_get_u8(format->device, absolute + (int64_t)current);
        uint8_t bundle_type = xx_io_get_u8(format->device, absolute + (int64_t)current + 1);
        uint64_t record_size;
        uint8_t index;
        current += 2U;
        if (count == 0U) break;
        if (bundle_type == 0U) {
            ordinal += count;
            continue;
        }
        record_size = bundle_type == 0xffU ? 6U : 3U;
        for (index = 0U; index < count; ++index, ++ordinal) {
            xx_ne_entry *entry;
            uint16_t segment_index;
            uint16_t segment_offset;
            if (record_size > table_size - current || parsed->entry_count >= capacity) return true;
            if (bundle_type == 0xffU) {
                segment_index = xx_io_get_u8(format->device, absolute + (int64_t)current + 3);
                segment_offset = xx_io_get_u16(format->device, absolute + (int64_t)current + 4, false);
            }
            else {
                segment_index = bundle_type;
                segment_offset = xx_io_get_u16(format->device, absolute + (int64_t)current + 1, false);
            }
            entry = &parsed->entries[parsed->entry_count++];
            entry->ordinal = ordinal;
            entry->segment_index = segment_index;
            entry->segment_offset = segment_offset;
            entry->is_movable = bundle_type == 0xffU;
            entry->offset = 0U;
            (void)xx_ne_entry_offset(parsed, segment_index, segment_offset, &entry->offset);
            current += record_size;
        }
    }
    return true;
}

static xx_os_t xx_ne_os(uint8_t target)
{
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

static bool xx_ne_parse(Abstractformat *format, xx_ne *parsed, xx_pd_struct *pd)
{
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
    if (!format || !format->device || format->base_address < 0 || xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (available < XX_NE_DOS_HEADER_SIZE || xx_io_get_u16(format->device, format->base_address, false) != UINT16_C(0x5a4d)) return false;

    if (!xx_ne_header_offset(format, &ne_offset, &absolute)) return false;

    parsed->ne_offset = (uint32_t)ne_offset;
    parsed->entry_table_offset = xx_io_get_u16(format->device, absolute + 4, false);
    parsed->entry_table_size = xx_io_get_u16(format->device, absolute + 6, false);
    parsed->file_load_crc = xx_io_get_u32(format->device, absolute + 8, false);
    parsed->flags = xx_io_get_u16(format->device, absolute + 0x0c, false);
    parsed->initial_cs_ip = xx_io_get_u32(format->device, absolute + 0x14, false);
    parsed->initial_ss_sp = xx_io_get_u32(format->device, absolute + 0x18, false);
    parsed->segment_count = xx_io_get_u16(format->device, absolute + 0x1c, false);
    parsed->module_reference_count = xx_io_get_u16(format->device, absolute + 0x1e, false);
    parsed->nonresident_name_table_size = xx_io_get_u16(format->device, absolute + 0x20, false);
    parsed->segment_table_offset = xx_io_get_u16(format->device, absolute + 0x22, false);
    parsed->resource_table_offset = xx_io_get_u16(format->device, absolute + 0x24, false);
    parsed->resident_name_table_offset = xx_io_get_u16(format->device, absolute + 0x26, false);
    parsed->module_reference_table_offset = xx_io_get_u16(format->device, absolute + 0x28, false);
    parsed->imported_name_table_offset = xx_io_get_u16(format->device, absolute + 0x2a, false);
    parsed->nonresident_name_table_offset = xx_io_get_u32(format->device, absolute + 0x2c, false);
    parsed->movable_entry_count = xx_io_get_u16(format->device, absolute + 0x30, false);
    parsed->alignment_shift = xx_io_get_u16(format->device, absolute + 0x32, false);
    parsed->resource_segment_count = xx_io_get_u16(format->device, absolute + 0x34, false);
    parsed->target_os = xx_io_get_u8(format->device, absolute + 0x36);
    parsed->other_flags = xx_io_get_u8(format->device, absolute + 0x37);
    parsed->expected_version = xx_io_get_u16(format->device, absolute + 0x3e, false);
    parsed->effective_alignment_shift = parsed->alignment_shift > XX_NE_MAX_ALIGNMENT_SHIFT ? XX_NE_MAX_ALIGNMENT_SHIFT : parsed->alignment_shift;

    table_relative = ne_offset + parsed->segment_table_offset;
    table_size = (uint64_t)parsed->segment_count * XX_NE_SEGMENT_RECORD_SIZE;
    if (table_relative > available) table_size = 0U;
    else if (table_size > available - table_relative) table_size = available - table_relative;
    parsed->parsed_segment_count = (uint16_t)(table_size / XX_NE_SEGMENT_RECORD_SIZE);
    table_size = (uint64_t)parsed->parsed_segment_count * XX_NE_SEGMENT_RECORD_SIZE;
    if (table_size != 0U && !xx_ne_range(format, table_relative, table_size, &absolute)) return false;
    if (parsed->parsed_segment_count != 0U) {
        parsed->segments = (xx_ne_segment *)xx_mem_alloc((size_t)parsed->parsed_segment_count * sizeof(*parsed->segments));
        if (!parsed->segments) return false;
        xx_mem_zero(parsed->segments, (size_t)parsed->parsed_segment_count * sizeof(*parsed->segments));
    }

    format_end = ne_offset + XX_NE_HEADER_SIZE;
    if (table_relative + table_size > format_end) format_end = table_relative + table_size;
    for (index = 0U; index < parsed->parsed_segment_count; ++index) {
        xx_ne_segment *segment = &parsed->segments[index];
        uint64_t record_relative = table_relative + (uint64_t)index * XX_NE_SEGMENT_RECORD_SIZE;
        uint64_t segment_relative;
        uint64_t declared_size;
        uint64_t clipped_size = 0U;
        uint64_t segment_end;
        int64_t record_absolute;
        (void)xx_ne_range(format, record_relative, XX_NE_SEGMENT_RECORD_SIZE, &record_absolute);
        segment->sector_offset = xx_io_get_u16(format->device, record_absolute, false);
        segment->file_size_field = xx_io_get_u16(format->device, record_absolute + 2, false);
        segment->flags = xx_io_get_u16(format->device, record_absolute + 4, false);
        segment->minimum_allocation = xx_io_get_u16(format->device, record_absolute + 6, false);
        declared_size = segment->file_size_field ? segment->file_size_field : UINT64_C(0x10000);
        if (segment->sector_offset == 0U) {
            segment->file_offset = -1;
            segment->file_size = 0;
            continue;
        }
        segment_relative = (uint64_t)segment->sector_offset << parsed->effective_alignment_shift;
        if (segment_relative < available) {
            clipped_size = declared_size;
            if (clipped_size > available - segment_relative) clipped_size = available - segment_relative;
        }
        segment->file_offset = clipped_size != 0U ? (int64_t)segment_relative : -1;
        segment->file_size = (int64_t)clipped_size;
        if (clipped_size != 0U && xx_ne_add_u64(segment_relative, clipped_size, &segment_end) && segment_end > format_end) format_end = segment_end;

        if ((segment->flags & XX_NE_SEGMENT_HAS_RELOCATIONS) != 0U && xx_ne_add_u64(segment_relative, declared_size, &segment_end) &&
            xx_ne_range(format, segment_end, 2U, &record_absolute)) {
            uint16_t relocation_count = xx_io_get_u16(format->device, record_absolute, false);
            uint64_t relocation_size = UINT64_C(2) + (uint64_t)relocation_count * UINT64_C(8);
            if (xx_ne_range(format, segment_end, relocation_size, &record_absolute)) {
                uint16_t relocation_index;
                uint64_t relocation_end = segment_end + relocation_size;
                if (relocation_end > format_end) format_end = relocation_end;
                for (relocation_index = 0U; relocation_index < relocation_count; ++relocation_index) {
                    uint8_t target = xx_io_get_u8(format->device, record_absolute + 2 + (int64_t)relocation_index * 8 + 1) & 3U;
                    if (target == 1U || target == 2U) ++import_count;
                }
            }
        }
    }

    if (!xx_ne_parse_entries(format, parsed, available, &format_end)) return false;
    export_count = parsed->entry_count;
    if (export_count != 0U && parsed->entries[0].is_initial) --export_count;

    if (parsed->resource_table_offset != 0U && parsed->resident_name_table_offset > parsed->resource_table_offset &&
        xx_ne_add_u64(ne_offset, parsed->resource_table_offset, &table_relative) && table_relative < available) {
        uint64_t declared_span = (uint64_t)parsed->resident_name_table_offset - parsed->resource_table_offset;
        uint64_t bounded_span = declared_span < available - table_relative ? declared_span : available - table_relative;
        parsed->parsed_resource_table_size = (uint32_t)bounded_span;
        if (table_relative + bounded_span > format_end) format_end = table_relative + bounded_span;
    }

    if (xx_ne_add_u64(ne_offset, parsed->resource_table_offset, &table_relative) && xx_ne_range(format, table_relative, 2U, &absolute)) {
        uint16_t resource_shift = xx_io_get_u16(format->device, absolute, false);
        uint64_t current = table_relative + 2U;
        if (resource_shift <= 47U) {
            while (xx_ne_range(format, current, XX_NE_RESOURCE_TYPE_SIZE, &absolute)) {
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
                if (!xx_ne_range(format, current + XX_NE_RESOURCE_TYPE_SIZE, names_size, &absolute)) break;
                resource_count += count;
                for (resource_index = 0U; resource_index < count; ++resource_index) {
                    int64_t name_absolute = absolute + (int64_t)resource_index * XX_NE_RESOURCE_NAME_SIZE;
                    uint64_t resource_relative = (uint64_t)xx_io_get_u16(format->device, name_absolute, false) << resource_shift;
                    uint64_t resource_size = (uint64_t)xx_io_get_u16(format->device, name_absolute + 2, false) << resource_shift;
                    uint64_t resource_end;
                    if (xx_ne_range(format, resource_relative, resource_size, NULL) && xx_ne_add_u64(resource_relative, resource_size, &resource_end) &&
                        resource_end > format_end)
                        format_end = resource_end;
                }
                current += XX_NE_RESOURCE_TYPE_SIZE + names_size;
                if (current > format_end) format_end = current;
            }
        }
    }

    if (parsed->nonresident_name_table_offset != 0U && parsed->nonresident_name_table_offset < available) {
        uint64_t bounded_span = parsed->nonresident_name_table_size;
        uint64_t end;
        if (bounded_span > available - parsed->nonresident_name_table_offset)
            bounded_span = available - parsed->nonresident_name_table_offset;
        parsed->parsed_nonresident_name_table_size = (uint32_t)bounded_span;
        end = (uint64_t)parsed->nonresident_name_table_offset + bounded_span;
        if (end > format_end) format_end = end;
    }

    if (format_end > available) format_end = available;
    parsed->format.format_size = (int64_t)format_end;
    parsed->format.overlay_offset = format_end < available ? format->base_address + (int64_t)format_end : -1;
    parsed->format.overlay_size = format_end < available ? (int64_t)(available - format_end) : 0;
    parsed->format.number_of_imports = import_count != 0U ? import_count : parsed->module_reference_count;
    parsed->format.number_of_exports = export_count;
    parsed->format.number_of_resources = resource_count;
    return !xx_pd_is_stopped(pd);
}

static void xx_ne_vtable_destroy(Abstractformat *format)
{
    xx_ne_destroy((xx_ne *)format);
}

void xx_ne_init(xx_ne *ne, xx_io_device *device, int64_t base_address)
{
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
    ne->format.get_memory_map = xx_ne_get_memory_map;
    ne->format.destroy = xx_ne_vtable_destroy;
}

xx_ne *xx_ne_create(xx_io_device *device, int64_t base_address)
{
    xx_ne *ne = (xx_ne *)xx_mem_alloc(sizeof(*ne));
    if (ne) xx_ne_init(ne, device, base_address);
    return ne;
}

void xx_ne_destroy(xx_ne *ne)
{
    if (!ne) return;
    if (ne->segments) xx_mem_free(ne->segments);
    if (ne->entries) xx_mem_free(ne->entries);
    ne->segments = NULL;
    ne->entries = NULL;
    xx_format_cleanup_extra_parameters(&ne->format);
}

void xx_ne_free(xx_ne *ne)
{
    if (!ne) return;
    xx_ne_destroy(ne);
    xx_mem_free(ne);
}

bool xx_ne_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    xx_ne parsed;
    bool result = xx_ne_parse(format, &parsed, pd);
    xx_ne_parsed_cleanup(&parsed);
    return result;
}

bool xx_ne_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
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
    if (ne->entries) xx_mem_free(ne->entries);
    *ne = parsed;
    ne->format = saved;
    parsed.segments = NULL;
    parsed.entries = NULL;
    ne->format.file_type = XX_FILE_TYPE_NE;
    ne->format.os = xx_ne_os(ne->target_os);
    ne->format.arch = ((ne->flags & XX_NE_FLAG_80386) != 0U || ne->target_os == 4U || ne->target_os == 5U) ? XX_ARCH_X86 : XX_ARCH_X86_16;
    ne->format.format_type = (ne->flags & XX_NE_FLAG_LIBRARY) != 0U ? XX_TYPE_LIBRARY : XX_TYPE_CONSOLE_APPLICATION;
    if (ne->format.format_type == XX_TYPE_LIBRARY) xx_format_set_extension(&ne->format, "dll");
    else xx_format_set_extension(&ne->format, "exe");
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

int64_t xx_ne_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ne_handle_base_info(format, pd)) ? format->format_size : -1;
}

bool xx_ne_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_ne *ne = (xx_ne *)format;
    int64_t total;
    int64_t binary_size;
    int64_t header_size;
    int64_t first_load = -1;
    uint64_t module;
    uint16_t index;
    if (!format || !output || !format->device || !format->base_info_handled || format->base_address < 0 || xx_pd_is_stopped(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    if (mode != XX_MEMORY_MAP_MODE_SEGMENTS) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    binary_size = total - format->base_address;
    header_size = (int64_t)ne->ne_offset + XX_NE_HEADER_SIZE;
    if (header_size > binary_size) header_size = binary_size;
    module = format->module_address != XX_INVALID_ADDRESS ? format->module_address : 0U;

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

    if (!xx_memory_map_add_part(output, format->base_address, header_size, module, 0x200, XX_FILE_PART_HEADER, 0, "NE header", false)) return false;
    for (index = 0U; index < ne->parsed_segment_count; ++index) {
        const xx_ne_segment *segment = &ne->segments[index];
        uint64_t displacement = (uint64_t)segment->sector_offset << ne->effective_alignment_shift;
        uint64_t address;
        int64_t virtual_size = segment->minimum_allocation ? segment->minimum_allocation : INT64_C(0x10000);
        int64_t offset = -1;
        char name[XX_MEMORY_RECORD_NAME_SIZE];
        if (!xx_ne_add_u64(module, displacement, &address) || address == XX_INVALID_ADDRESS) return false;
        if (segment->file_offset >= 0) {
            if (segment->file_offset > INT64_MAX - format->base_address) return false;
            offset = format->base_address + segment->file_offset;
            if (first_load < 0 || offset < first_load) first_load = offset;
        }
        if (virtual_size < segment->file_size) virtual_size = segment->file_size;
        (void)xx_rt_snprintf(name, sizeof(name), "Segment %u", (unsigned)index + 1U);
        if (!xx_memory_map_add_part(output, offset, segment->file_size, address, virtual_size, XX_FILE_PART_SEGMENT, (int32_t)index + 1, name, false)) return false;
    }
    if (first_load >= 0) output->start_load_offset = first_load;
    {
        uint16_t entry_segment = (uint16_t)(ne->initial_cs_ip >> 16U);
        uint16_t entry_offset = (uint16_t)ne->initial_cs_ip;
        if (entry_segment != 0U && entry_segment <= ne->parsed_segment_count) {
            const xx_ne_segment *segment = &ne->segments[entry_segment - 1U];
            uint64_t displacement = (uint64_t)segment->sector_offset << ne->effective_alignment_shift;
            uint64_t code;
            if (xx_ne_add_u64(module, displacement, &code) && code <= (uint64_t)INT64_MAX) {
                output->code_base = (int64_t)code;
                (void)xx_ne_add_u64(code, entry_offset, &output->entry_point_address);
            }
        }
    }
    if (format->overlay_size > 0 && !xx_memory_map_add_part(output, format->overlay_offset, format->overlay_size, XX_INVALID_ADDRESS, 0, XX_FILE_PART_OVERLAY,
                                                            (int32_t)ne->parsed_segment_count + 1, "Overlay", false))
        return false;
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

uint16_t xx_ne_get_number_of_segments(const xx_ne *ne)
{
    return ne ? ne->parsed_segment_count : 0U;
}

const xx_ne_segment *xx_ne_get_segment(const xx_ne *ne, uint16_t index)
{
    return ne && ne->segments && index < ne->parsed_segment_count ? &ne->segments[index] : NULL;
}

uint32_t xx_ne_get_number_of_entries(const xx_ne *ne)
{
    return ne ? ne->entry_count : 0U;
}

const xx_ne_entry *xx_ne_get_entry(const xx_ne *ne, uint32_t index)
{
    return ne && ne->entries && index < ne->entry_count ? &ne->entries[index] : NULL;
}
