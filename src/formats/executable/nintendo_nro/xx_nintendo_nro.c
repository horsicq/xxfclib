/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/nintendo_nro/xx_nintendo_nro.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

static bool nro_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool nro_read_header(const Abstractformat *format, uint8_t header[XX_NRO_ENTRY_OFFSET])
{
    int64_t total;
    if (!format || !format->device || !header || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    return total >= format->base_address && total - format->base_address >= XX_NRO_ENTRY_OFFSET &&
           xx_io_read_at(format->device, format->base_address, header, XX_NRO_ENTRY_OFFSET) && memcmp(header + XX_NRO_HEADER_OFFSET, "NRO0", 4U) == 0;
}

static bool nro_span(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset <= limit && size <= limit - offset;
}

void xx_nintendo_nro_init(xx_nintendo_nro *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_NINTENDO_NRO;
    reader->format.check_is_valid = xx_nintendo_nro_check_is_valid;
    reader->format.handle_base_info = xx_nintendo_nro_handle_base_info;
    reader->format.get_format_size = xx_nintendo_nro_get_format_size;
    reader->format.get_memory_map = xx_nintendo_nro_get_memory_map;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_ARM64;
    reader->format.os = XX_OS_NINTENDO_SWITCH;
    reader->format.format_type = XX_TYPE_OBJECT;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "nro");
    xx_format_set_mime_type(&reader->format, "application/x-nintendo-switch-nro");
}

xx_nintendo_nro *xx_nintendo_nro_create(xx_io_device *device, int64_t base_address)
{
    xx_nintendo_nro *reader = (xx_nintendo_nro *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_nintendo_nro_init(reader, device, base_address);
    return reader;
}

void xx_nintendo_nro_destroy(xx_nintendo_nro *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_nintendo_nro_free(xx_nintendo_nro *reader)
{
    if (!reader) return;
    xx_nintendo_nro_destroy(reader);
    xx_mem_free(reader);
}

bool xx_nintendo_nro_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t header[XX_NRO_ENTRY_OFFSET];
    uint32_t image_size;
    (void)pd;
    if (!nro_read_header(self, header)) return false;
    image_size = xx_data_get_u32(header, sizeof(header), 0x18U, false);
    return image_size >= XX_NRO_ENTRY_OFFSET;
}

bool xx_nintendo_nro_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    static const uint8_t offset_fields[3] = {0x20U, 0x28U, 0x30U};
    static const uint8_t size_fields[3] = {0x24U, 0x2cU, 0x34U};
    static const uint32_t flags[3] = {5U, 4U, 6U};
    xx_nintendo_nro *reader = (xx_nintendo_nro *)self;
    uint8_t header[XX_NRO_ENTRY_OFFSET];
    uint8_t asset_magic[4];
    int64_t total;
    uint64_t available;
    uint32_t previous_end = 0U;
    unsigned index;

    if (!reader || !self->device || self->base_address < 0 || nro_stopped(pd)) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || !nro_read_header(self, header)) return false;
    available = (uint64_t)(total - self->base_address);
    if (available > INT64_MAX) return false;

    reader->module_offset = xx_data_get_u32(header, sizeof(header), 4U, false);
    reader->image_size = xx_data_get_u32(header, sizeof(header), 0x18U, false);
    reader->asset_offset = reader->image_size;
    reader->bss_size = xx_data_get_u32(header, sizeof(header), 0x38U, false);
    if (reader->image_size < XX_NRO_ENTRY_OFFSET || reader->image_size > available || reader->bss_size > XX_NRO_MAX_MEMORY_SIZE) return false;

    for (index = 0U; index < 3U; ++index) {
        xx_nro_segment *segment = &reader->segments[index];
        segment->file_offset = xx_data_get_u32(header, sizeof(header), offset_fields[index], false);
        segment->memory_offset = segment->file_offset;
        segment->file_size = xx_data_get_u32(header, sizeof(header), size_fields[index], false);
        if (index == XX_NRO_SEGMENT_DATA && reader->bss_size > XX_NRO_MAX_MEMORY_SIZE - segment->file_size) return false;
        segment->memory_size = segment->file_size + ((index == XX_NRO_SEGMENT_DATA) ? reader->bss_size : 0U);
        segment->flags = flags[index];
        if (!nro_span(segment->file_offset, segment->file_size, reader->image_size) || (index != 0U && segment->file_offset < previous_end) ||
            segment->memory_size < segment->file_size)
            return false;
        previous_end = segment->file_offset + segment->file_size;
    }
    if (reader->segments[XX_NRO_SEGMENT_TEXT].file_offset != 0U || XX_NRO_ENTRY_OFFSET >= reader->segments[XX_NRO_SEGMENT_TEXT].file_size) return false;

    reader->has_asset_header = available >= (uint64_t)reader->asset_offset + 0x38U &&
                               xx_io_read_at(self->device, self->base_address + reader->asset_offset, asset_magic, sizeof(asset_magic)) &&
                               memcmp(asset_magic, "ASET", 4U) == 0;
    if (available > reader->image_size && !reader->has_asset_header) return false;

    self->format_size = (int64_t)available;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->is_valid = true;
    self->base_info_handled = true;
    return !nro_stopped(pd);
}

int64_t xx_nintendo_nro_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_nintendo_nro_handle_base_info(self, pd)) ? self->format_size : -1;
}

const xx_nro_segment *xx_nintendo_nro_get_segment(xx_nintendo_nro *reader, xx_nro_segment_kind kind, xx_pd_struct *pd)
{
    return reader && (unsigned)kind < 3U && (reader->format.base_info_handled || xx_nintendo_nro_handle_base_info(&reader->format, pd))
               ? &reader->segments[(unsigned)kind]
               : NULL;
}

bool xx_nintendo_nro_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    static const char *const names[3] = {"text", "ro", "data"};
    xx_nintendo_nro *reader = (xx_nintendo_nro *)self;
    unsigned index;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SEGMENTS) ||
        (!self->base_info_handled && !xx_nintendo_nro_handle_base_info(self, pd)))
        return false;
    output->binary_offset = self->base_address;
    output->binary_size = self->format_size;
    output->module_address = 0U;
    output->entry_point_address = XX_NRO_ENTRY_OFFSET;
    output->code_base = 0;
    output->start_load_offset = self->base_address;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    output->is_image = true;
    if (!xx_memory_map_add_part(output, self->base_address, XX_NRO_ENTRY_OFFSET, 0U, XX_NRO_ENTRY_OFFSET, XX_FILE_PART_HEADER, 0, "header", false)) return false;
    for (index = 0U; index < 3U; ++index) {
        const xx_nro_segment *segment = &reader->segments[index];
        if (!xx_memory_map_add_part(output, self->base_address + segment->file_offset, segment->file_size, segment->memory_offset, segment->memory_size,
                                    XX_FILE_PART_SEGMENT, (int32_t)index + 1, names[index], false))
            return false;
    }
    if (reader->has_asset_header && self->format_size > reader->asset_offset &&
        !xx_memory_map_add_part(output, self->base_address + reader->asset_offset, self->format_size - reader->asset_offset, XX_INVALID_ADDRESS, 0, XX_FILE_PART_RESOURCE,
                                4, "ASET assets", false))
        return false;
    return !nro_stopped(pd) && xx_memory_map_finalize(output);
}
