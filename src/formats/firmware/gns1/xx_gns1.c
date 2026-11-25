/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/firmware/gns1/xx_gns1.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>
#include <string.h>

#define GNS1_FTAB_MAGIC_OFFSET UINT64_C(0x20)
#define GNS1_FTAB_ENTRY_COUNT_OFFSET UINT64_C(0x28)
#define GNS1_REGION_A_BASE UINT32_C(0x12000000)
#define GNS1_REGION_B_BASE UINT32_C(0x15000000)
#define GNS1_REGION_B_END UINT32_C(0x16000000)
#define GNS1_ADDRESS_MASK UINT32_C(0x00ffffff)

typedef struct gns1_parsed_s {
    uint64_t available;
    uint64_t base_offset;
    uint64_t image_size;
    uint64_t table_size;
    xx_gns1_segment *segments;
    uint32_t segment_count;
    uint32_t segment_capacity;
    xx_gns1_ftab_entry *ftab_entries;
    uint32_t ftab_entry_count;
    uint32_t ftab_entry_capacity;
    bool is_ftab;
} gns1_parsed;

static void gns1_vtable_destroy(Abstractformat *format);

static bool gns1_get_available(const Abstractformat *format, uint64_t *available)
{
    int64_t total;
    if (!format || !format->device || !available || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    *available = (uint64_t)(total - format->base_address);
    return true;
}

static bool gns1_range(uint64_t limit, uint64_t offset, uint64_t size)
{
    return offset <= limit && size <= limit - offset;
}

static bool gns1_read(const Abstractformat *format, uint64_t available, uint64_t offset, void *buffer, size_t size)
{
    if (!format || !buffer || !size || !gns1_range(available, offset, (uint64_t)size) || offset > (uint64_t)(INT64_MAX - format->base_address)) return false;
    return xx_io_read_at(format->device, format->base_address + (int64_t)offset, buffer, size);
}

static xx_gns1_region gns1_region(uint32_t address)
{
    if (address >= GNS1_REGION_A_BASE && address < GNS1_REGION_B_BASE) return XX_GNS1_REGION_A;
    if (address >= GNS1_REGION_B_BASE && address < GNS1_REGION_B_END) return XX_GNS1_REGION_B;
    return XX_GNS1_REGION_UNKNOWN;
}

static uint64_t gns1_translate_address(uint32_t address)
{
    if (gns1_region(address & UINT32_C(0xff000000)) == XX_GNS1_REGION_A) return (uint64_t)XX_GNS1_INTERNAL_BASE + (uint64_t)(address & GNS1_ADDRESS_MASK);
    return address;
}

static bool gns1_is_erased(uint32_t size, uint32_t address, uint32_t offset)
{
    return size == UINT32_MAX && address == UINT32_MAX && offset == UINT32_MAX;
}

static bool gns1_zero_filled(const Abstractformat *format, uint64_t available, uint64_t offset, uint64_t end, xx_pd_struct *pd)
{
    uint8_t buffer[256];
    while (offset < end) {
        size_t take = end - offset > sizeof(buffer) ? sizeof(buffer) : (size_t)(end - offset);
        size_t index;
        if (xx_pd_is_stopped(pd) || !gns1_read(format, available, offset, buffer, take)) return false;
        for (index = 0U; index < take; ++index) {
            if (buffer[index] != 0U) return false;
        }
        offset += take;
    }
    return true;
}

static bool gns1_read_segment(const Abstractformat *format, const gns1_parsed *parsed, uint64_t table_offset, uint32_t *size, uint32_t *address, uint32_t *image_offset)
{
    uint8_t bytes[XX_GNS1_SEGMENT_ENTRY_SIZE];
    uint64_t relative;
    if (!parsed || table_offset > UINT64_MAX - parsed->base_offset) return false;
    relative = parsed->base_offset + table_offset;
    if (!gns1_read(format, parsed->available, relative, bytes, sizeof(bytes))) return false;
    *size = xx_data_get_u32(bytes, sizeof(bytes), 0U, false);
    *address = xx_data_get_u32(bytes, sizeof(bytes), 4U, false);
    *image_offset = xx_data_get_u32(bytes, sizeof(bytes), 8U, false);
    return true;
}

static bool gns1_valid_segment(uint32_t size, uint32_t address, uint32_t offset, uint64_t image_size, uint64_t data_start)
{
    xx_gns1_region region = gns1_region(address);
    return size != 0U && !gns1_is_erased(size, address, offset) && region != XX_GNS1_REGION_UNKNOWN && (uint64_t)offset >= data_start &&
           gns1_range(image_size, offset, size);
}

static bool gns1_parse_segment_table(const Abstractformat *format, gns1_parsed *parsed, xx_pd_struct *pd)
{
    uint32_t first_size;
    uint32_t first_address;
    uint32_t first_offset;
    uint64_t data_start;
    uint64_t table_offset = 0U;
    uint32_t count = 0U;
    bool terminated = false;

    if (!format || !parsed || parsed->image_size < XX_GNS1_MIN_FILE_SIZE || !gns1_range(parsed->available, parsed->base_offset, parsed->image_size) ||
        !gns1_read_segment(format, parsed, 0U, &first_size, &first_address, &first_offset))
        return false;
    data_start = first_offset;
    if ((first_address & GNS1_ADDRESS_MASK) != 0U || gns1_region(first_address) == XX_GNS1_REGION_UNKNOWN || data_start < XX_GNS1_MIN_DATA_OFFSET ||
        !gns1_valid_segment(first_size, first_address, first_offset, parsed->image_size, data_start))
        return false;

    while (table_offset < data_start) {
        uint32_t size;
        uint32_t address;
        uint32_t image_offset;
        xx_gns1_segment_type type;
        xx_gns1_region region;
        xx_gns1_segment *segment;

        if (xx_pd_is_stopped(pd)) return false;
        if (data_start - table_offset < XX_GNS1_SEGMENT_ENTRY_SIZE) {
            if (!gns1_zero_filled(format, parsed->available, parsed->base_offset + table_offset, parsed->base_offset + data_start, pd)) return false;
            table_offset = data_start;
            terminated = true;
            break;
        }
        if (!gns1_read_segment(format, parsed, table_offset, &size, &address, &image_offset)) return false;
        table_offset += XX_GNS1_SEGMENT_ENTRY_SIZE;
        if (gns1_is_erased(size, address, image_offset)) {
            terminated = true;
            break;
        }
        if (size == 0U) {
            if (address != 0U || image_offset != 0U) return false;
            terminated = true;
            break;
        }
        if (count >= XX_GNS1_MAX_SEGMENTS || !gns1_valid_segment(size, address, image_offset, parsed->image_size, data_start)) return false;
        type = (address & GNS1_ADDRESS_MASK) == 0U ? XX_GNS1_SEGMENT_TEXT : XX_GNS1_SEGMENT_DATA;
        region = gns1_region(address);
        if (parsed->segments) {
            if (count >= parsed->segment_capacity || parsed->base_offset > UINT64_MAX - image_offset ||
                parsed->base_offset + image_offset > (uint64_t)(INT64_MAX - format->base_address))
                return false;
            segment = &parsed->segments[count];
            xx_mem_zero(segment, sizeof(*segment));
            segment->size = size;
            segment->physical_address = address;
            segment->image_offset = image_offset;
            segment->offset = format->base_address + (int64_t)(parsed->base_offset + image_offset);
            segment->address = gns1_translate_address(address);
            segment->permissions = XX_GNS1_PERMISSION_READ | (type == XX_GNS1_SEGMENT_TEXT ? XX_GNS1_PERMISSION_EXECUTE : XX_GNS1_PERMISSION_WRITE);
            segment->type = type;
            segment->region = region;
        }
        ++count;
    }
    if (!terminated || count == 0U || !gns1_zero_filled(format, parsed->available, parsed->base_offset + table_offset, parsed->base_offset + data_start, pd))
        return false;
    parsed->segment_count = count;
    parsed->table_size = data_start;
    return true;
}

static bool gns1_parse_ftab(const Abstractformat *format, gns1_parsed *parsed, xx_pd_struct *pd)
{
    uint8_t bytes[XX_GNS1_FTAB_ENTRY_SIZE];
    uint32_t count;
    uint32_t reserved;
    uint64_t table_size;
    uint32_t index;
    bool found_gns1 = false;

    if (!format || !parsed || parsed->available < XX_GNS1_FTAB_ENTRY_OFFSET + XX_GNS1_FTAB_ENTRY_SIZE ||
        !gns1_read(format, parsed->available, GNS1_FTAB_MAGIC_OFFSET, bytes, 8U) || memcmp(bytes, "rkosftab", 8U) != 0 ||
        !gns1_read(format, parsed->available, GNS1_FTAB_ENTRY_COUNT_OFFSET, bytes, 8U))
        return false;
    count = xx_data_get_u32(bytes, sizeof(bytes), 0U, false);
    reserved = xx_data_get_u32(bytes, sizeof(bytes), 4U, false);
    if (count == 0U || count > XX_GNS1_FTAB_MAX_ENTRIES || reserved != 0U) return false;
    table_size = (uint64_t)count * XX_GNS1_FTAB_ENTRY_SIZE;
    if (!gns1_range(parsed->available, XX_GNS1_FTAB_ENTRY_OFFSET, table_size) || (parsed->ftab_entries && count > parsed->ftab_entry_capacity)) return false;

    for (index = 0U; index < count; ++index) {
        uint64_t offset = XX_GNS1_FTAB_ENTRY_OFFSET + (uint64_t)index * XX_GNS1_FTAB_ENTRY_SIZE;
        uint32_t image_offset;
        uint32_t size;
        uint32_t entry_reserved;
        if (xx_pd_is_stopped(pd) || !gns1_read(format, parsed->available, offset, bytes, sizeof(bytes))) return false;
        image_offset = xx_data_get_u32(bytes, sizeof(bytes), 4U, false);
        size = xx_data_get_u32(bytes, sizeof(bytes), 8U, false);
        entry_reserved = xx_data_get_u32(bytes, sizeof(bytes), 12U, false);
        if (!gns1_range(parsed->available, image_offset, size)) return false;
        if (parsed->ftab_entries) {
            xx_gns1_ftab_entry *entry = &parsed->ftab_entries[index];
            xx_mem_zero(entry, sizeof(*entry));
            memcpy(entry->tag, bytes, 4U);
            entry->image_offset = image_offset;
            entry->size = size;
            entry->reserved = entry_reserved;
        }
        if (memcmp(bytes, "GNS1", 4U) == 0) {
            if (size == 0U) return false;
            parsed->base_offset = image_offset;
            parsed->image_size = size;
            found_gns1 = true;
        }
    }
    parsed->ftab_entry_count = count;
    return found_gns1;
}

static bool gns1_parse_layout(const Abstractformat *format, gns1_parsed *parsed, xx_pd_struct *pd)
{
    uint64_t available;
    uint64_t ignored;
    if (!format || !parsed || xx_pd_is_stopped(pd) || !gns1_get_available(format, &available)) return false;
    parsed->available = available;
    parsed->base_offset = 0U;
    parsed->image_size = available;
    parsed->table_size = 0U;
    parsed->segment_count = 0U;
    parsed->ftab_entry_count = 0U;
    parsed->is_ftab = false;
    if (gns1_parse_segment_table(format, parsed, pd)) return true;
    if (xx_pd_is_stopped(pd)) return false;
    ignored = parsed->table_size;
    (void)ignored;
    parsed->base_offset = 0U;
    parsed->image_size = 0U;
    parsed->table_size = 0U;
    parsed->segment_count = 0U;
    if (!gns1_parse_ftab(format, parsed, pd)) return false;
    parsed->is_ftab = true;
    return gns1_parse_segment_table(format, parsed, pd);
}

static void gns1_free_parsed(gns1_parsed *parsed)
{
    if (!parsed) return;
    if (parsed->segments) xx_mem_free(parsed->segments);
    if (parsed->ftab_entries) xx_mem_free(parsed->ftab_entries);
    parsed->segments = NULL;
    parsed->ftab_entries = NULL;
}

static bool gns1_load(const Abstractformat *format, gns1_parsed *result, xx_pd_struct *pd)
{
    gns1_parsed layout;
    gns1_parsed filled;
    if (!result) return false;
    xx_mem_zero(&layout, sizeof(layout));
    if (!gns1_parse_layout(format, &layout, pd)) return false;
    filled = layout;
    filled.segment_capacity = layout.segment_count;
    filled.ftab_entry_capacity = layout.ftab_entry_count;
    filled.segments = (xx_gns1_segment *)xx_mem_alloc((size_t)layout.segment_count * sizeof(*filled.segments));
    if (!filled.segments) return false;
    xx_mem_zero(filled.segments, (size_t)layout.segment_count * sizeof(*filled.segments));
    if (layout.ftab_entry_count != 0U) {
        filled.ftab_entries = (xx_gns1_ftab_entry *)xx_mem_alloc((size_t)layout.ftab_entry_count * sizeof(*filled.ftab_entries));
        if (!filled.ftab_entries) {
            gns1_free_parsed(&filled);
            return false;
        }
        xx_mem_zero(filled.ftab_entries, (size_t)layout.ftab_entry_count * sizeof(*filled.ftab_entries));
    }
    if (!gns1_parse_layout(format, &filled, pd) || filled.segment_count != layout.segment_count || filled.ftab_entry_count != layout.ftab_entry_count ||
        filled.base_offset != layout.base_offset || filled.image_size != layout.image_size || filled.table_size != layout.table_size ||
        filled.is_ftab != layout.is_ftab) {
        gns1_free_parsed(&filled);
        return false;
    }
    *result = filled;
    return true;
}

void xx_gns1_init(xx_gns1 *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_GNS1;
    reader->format.check_is_valid = xx_gns1_check_is_valid;
    reader->format.handle_base_info = xx_gns1_handle_base_info;
    reader->format.get_format_size = xx_gns1_get_format_size;
    reader->format.get_memory_map = xx_gns1_get_memory_map;
    reader->format.destroy = gns1_vtable_destroy;
    reader->format.format_type = XX_TYPE_FIRMWARE;
    reader->format.arch = XX_ARCH_ARC;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.is_executable = true;
    reader->format.module_address = XX_GNS1_INTERNAL_BASE;
    xx_format_set_extension(&reader->format, "bin");
    xx_format_set_mime_type(&reader->format, "application/x-gns1-firmware");
}

xx_gns1 *xx_gns1_create(xx_io_device *device, int64_t base_address)
{
    xx_gns1 *reader = (xx_gns1 *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_gns1_init(reader, device, base_address);
    return reader;
}

void xx_gns1_destroy(xx_gns1 *reader)
{
    if (!reader) return;
    if (reader->segments) xx_mem_free(reader->segments);
    if (reader->ftab_entries) xx_mem_free(reader->ftab_entries);
    reader->segments = NULL;
    reader->ftab_entries = NULL;
    reader->segment_count = 0U;
    reader->ftab_entry_count = 0U;
    reader->entry_count = 0U;
    xx_format_cleanup_extra_parameters(&reader->format);
}

static void gns1_vtable_destroy(Abstractformat *format)
{
    xx_gns1_destroy((xx_gns1 *)format);
}

void xx_gns1_free(xx_gns1 *reader)
{
    if (!reader) return;
    xx_gns1_destroy(reader);
    xx_mem_free(reader);
}

bool xx_gns1_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    gns1_parsed parsed;
    bool result;
    xx_mem_zero(&parsed, sizeof(parsed));
    result = gns1_parse_layout(format, &parsed, pd);
    return result;
}

bool xx_gns1_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_gns1 *reader = (xx_gns1 *)format;
    gns1_parsed parsed;
    uint32_t index;
    const xx_gns1_segment *region_a = NULL;
    const xx_gns1_segment *region_b = NULL;
    if (!reader || !format->device || xx_pd_is_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    xx_mem_zero(&parsed, sizeof(parsed));
    if (!gns1_load(format, &parsed, pd)) return false;
    if (parsed.available > (uint64_t)INT64_MAX) {
        gns1_free_parsed(&parsed);
        return false;
    }
    reader->base_offset = parsed.base_offset;
    reader->image_size = parsed.image_size;
    reader->table_size = parsed.table_size;
    reader->segments = parsed.segments;
    reader->segment_count = parsed.segment_count;
    reader->ftab_entries = parsed.ftab_entries;
    reader->ftab_entry_count = parsed.ftab_entry_count;
    reader->is_ftab = parsed.is_ftab;
    parsed.segments = NULL;
    parsed.ftab_entries = NULL;
    for (index = 0U; index < reader->segment_count; ++index) {
        const xx_gns1_segment *segment = &reader->segments[index];
        if (segment->type != XX_GNS1_SEGMENT_TEXT) continue;
        if (segment->region == XX_GNS1_REGION_A && (!region_a || segment->physical_address < region_a->physical_address)) region_a = segment;
        if (segment->region == XX_GNS1_REGION_B && (!region_b || segment->physical_address < region_b->physical_address)) region_b = segment;
    }
    if (region_a) {
        reader->entries[reader->entry_count].offset = region_a->offset;
        reader->entries[reader->entry_count].address = region_a->address;
        reader->entries[reader->entry_count].region = region_a->region;
        ++reader->entry_count;
    }
    if (region_b) {
        reader->entries[reader->entry_count].offset = region_b->offset;
        reader->entries[reader->entry_count].address = region_b->address;
        reader->entries[reader->entry_count].region = region_b->region;
        ++reader->entry_count;
    }
    format->format_size = (int64_t)parsed.available;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_FILE_TYPE_GNS1;
    format->format_type = XX_TYPE_FIRMWARE;
    format->arch = XX_ARCH_ARC;
    format->endian = XX_ENDIAN_LITTLE;
    format->is_executable = true;
    format->module_address = XX_GNS1_INTERNAL_BASE;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_gns1_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_gns1_handle_base_info(format, pd)) ? format->format_size : -1;
}

static void gns1_ftab_name(const xx_gns1_ftab_entry *entry, char name[32])
{
    char tag[5];
    unsigned index;
    for (index = 0U; index < 4U; ++index) {
        unsigned char value = (unsigned char)entry->tag[index];
        tag[index] = value >= 0x20U && value <= 0x7eU ? (char)value : '_';
    }
    tag[4] = '\0';
    (void)xx_rt_snprintf(name, 32U, "ftab_%s", tag);
}

bool xx_gns1_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_gns1 *reader = (xx_gns1 *)format;
    uint32_t index;
    bool okay;
    if (!reader || !output || xx_pd_is_stopped(pd) || (!format->base_info_handled && !xx_gns1_handle_base_info(format, pd))) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_SECTIONS;
    if (mode != XX_MEMORY_MAP_MODE_SECTIONS && mode != XX_MEMORY_MAP_MODE_SEGMENTS) return false;
    output->binary_offset = format->base_address;
    output->binary_size = format->format_size;
    output->module_address = XX_GNS1_INTERNAL_BASE;
    output->entry_point_address = reader->entry_count ? reader->entries[0].address : XX_INVALID_ADDRESS;
    output->code_base = XX_GNS1_INTERNAL_BASE;
    output->start_load_offset = reader->entry_count ? reader->entries[0].offset : format->base_address;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = mode;
    output->is_image = true;
    if (reader->is_ftab) {
        uint64_t entries_size = (uint64_t)reader->ftab_entry_count * XX_GNS1_FTAB_ENTRY_SIZE;
        if (!xx_memory_map_add_part(output, format->base_address, XX_GNS1_FTAB_HEADER_SIZE, 0U, XX_GNS1_FTAB_HEADER_SIZE, XX_FILE_PART_HEADER, 0, "ftab_header", false) ||
            entries_size > INT64_MAX ||
            !xx_memory_map_add_part(output, format->base_address + XX_GNS1_FTAB_ENTRY_OFFSET, (int64_t)entries_size, XX_GNS1_FTAB_ENTRY_OFFSET, (int64_t)entries_size,
                                    XX_FILE_PART_TABLE, 1, "ftab_entries", false))
            return false;
        for (index = 0U; index < reader->ftab_entry_count; ++index) {
            const xx_gns1_ftab_entry *entry = &reader->ftab_entries[index];
            char name[32];
            gns1_ftab_name(entry, name);
            if (!xx_memory_map_add_part(output, format->base_address + entry->image_offset, entry->size, entry->image_offset, entry->size, XX_FILE_PART_SECTION,
                                        (int32_t)index + 2, name, false))
                return false;
        }
    }
    if (reader->base_offset > (uint64_t)(INT64_MAX - format->base_address) || reader->table_size > INT64_MAX ||
        !xx_memory_map_add_part(output, format->base_address + (int64_t)reader->base_offset, (int64_t)reader->table_size, reader->base_offset,
                                (int64_t)reader->table_size, XX_FILE_PART_TABLE, 0, "gns1_segments", false))
        return false;
    for (index = 0U; index < reader->segment_count; ++index) {
        const xx_gns1_segment *segment = &reader->segments[index];
        const char *region = segment->region == XX_GNS1_REGION_A ? "region_a" : "region_b";
        const char *type = segment->type == XX_GNS1_SEGMENT_TEXT ? "text" : "data";
        char name[64];
        (void)xx_rt_snprintf(name, sizeof(name), "%s_%s_%u", region, type, index);
        if (!xx_memory_map_add_part(output, segment->offset, segment->size, segment->address, segment->size,
                                    mode == XX_MEMORY_MAP_MODE_SECTIONS ? XX_FILE_PART_SECTION : XX_FILE_PART_SEGMENT, (int32_t)index + 1, name, false))
            return false;
    }
    okay = xx_memory_map_finalize(output);
    if (okay) output->module_address = XX_GNS1_INTERNAL_BASE;
    return okay;
}

uint64_t xx_gns1_get_base_offset(const xx_gns1 *reader)
{
    return reader ? reader->base_offset : 0U;
}

uint64_t xx_gns1_get_image_size(const xx_gns1 *reader)
{
    return reader ? reader->image_size : 0U;
}

uint64_t xx_gns1_get_table_size(const xx_gns1 *reader)
{
    return reader ? reader->table_size : 0U;
}

bool xx_gns1_is_ftab(const xx_gns1 *reader)
{
    return reader && reader->is_ftab;
}

uint32_t xx_gns1_get_segment_count(const xx_gns1 *reader)
{
    return reader ? reader->segment_count : 0U;
}

const xx_gns1_segment *xx_gns1_get_segment(const xx_gns1 *reader, uint32_t index)
{
    return reader && index < reader->segment_count ? &reader->segments[index] : NULL;
}

uint32_t xx_gns1_get_ftab_entry_count(const xx_gns1 *reader)
{
    return reader ? reader->ftab_entry_count : 0U;
}

const xx_gns1_ftab_entry *xx_gns1_get_ftab_entry(const xx_gns1 *reader, uint32_t index)
{
    return reader && index < reader->ftab_entry_count ? &reader->ftab_entries[index] : NULL;
}

uint32_t xx_gns1_get_entry_count(const xx_gns1 *reader)
{
    return reader ? reader->entry_count : 0U;
}

const xx_gns1_entry *xx_gns1_get_entry(const xx_gns1 *reader, uint32_t index)
{
    return reader && index < reader->entry_count ? &reader->entries[index] : NULL;
}

const char *xx_gns1_get_cpu_name(void)
{
    return "hs";
}

const char *xx_gns1_get_machine_name(void)
{
    return "Apple C4000 Baseband";
}

const char *xx_gns1_get_subsystem_name(void)
{
    return "baseband";
}
