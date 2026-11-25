/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/nintendo_nso/xx_nintendo_nso.h"

#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>
#include <string.h>

static bool nso_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool nso_read_header(const Abstractformat *format, uint8_t header[XX_NSO_HEADER_SIZE])
{
    int64_t total;
    if (!format || !format->device || !header || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    return total >= format->base_address && total - format->base_address >= XX_NSO_HEADER_SIZE &&
           xx_io_read_at(format->device, format->base_address, header, XX_NSO_HEADER_SIZE) && memcmp(header, "NSO0", 4U) == 0;
}

static bool nso_span(uint32_t offset, uint32_t size, uint64_t limit)
{
    return offset <= limit && size <= limit - offset;
}

void xx_nintendo_nso_init(xx_nintendo_nso *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_NINTENDO_NSO;
    reader->format.check_is_valid = xx_nintendo_nso_check_is_valid;
    reader->format.handle_base_info = xx_nintendo_nso_handle_base_info;
    reader->format.get_format_size = xx_nintendo_nso_get_format_size;
    reader->format.get_memory_map = xx_nintendo_nso_get_memory_map;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_ARM64;
    reader->format.os = XX_OS_NINTENDO_SWITCH;
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "nso");
    xx_format_set_mime_type(&reader->format, "application/x-nintendo-switch-nso");
}

xx_nintendo_nso *xx_nintendo_nso_create(xx_io_device *device, int64_t base_address)
{
    xx_nintendo_nso *reader = (xx_nintendo_nso *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_nintendo_nso_init(reader, device, base_address);
    return reader;
}

void xx_nintendo_nso_destroy(xx_nintendo_nso *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_nintendo_nso_free(xx_nintendo_nso *reader)
{
    if (!reader) return;
    xx_nintendo_nso_destroy(reader);
    xx_mem_free(reader);
}

bool xx_nintendo_nso_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t header[XX_NSO_HEADER_SIZE];
    (void)pd;
    return nso_read_header(self, header);
}

bool xx_nintendo_nso_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    static const uint8_t file_fields[3] = {0x10U, 0x20U, 0x30U};
    static const uint8_t memory_fields[3] = {0x14U, 0x24U, 0x34U};
    static const uint8_t size_fields[3] = {0x18U, 0x28U, 0x38U};
    static const uint8_t packed_fields[3] = {0x60U, 0x64U, 0x68U};
    static const uint32_t permissions[3] = {5U, 4U, 6U};
    xx_nintendo_nso *reader = (xx_nintendo_nso *)self;
    uint8_t header[XX_NSO_HEADER_SIZE];
    int64_t total;
    uint64_t available;
    uint64_t memory_total = 0U;
    uint64_t maximum_file_end = XX_NSO_HEADER_SIZE;
    uint64_t previous_file_end = XX_NSO_HEADER_SIZE;
    uint64_t previous_memory_end = 0U;
    unsigned index;

    if (!reader || !self->device || self->base_address < 0 || nso_stopped(pd)) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || !nso_read_header(self, header)) return false;
    available = (uint64_t)(total - self->base_address);
    reader->flags = xx_data_get_u32(header, sizeof(header), 0x0cU, false);
    reader->bss_size = xx_data_get_u32(header, sizeof(header), 0x3cU, false);

    for (index = 0U; index < 3U; ++index) {
        xx_nso_segment *segment = &reader->segments[index];
        uint64_t file_end;
        uint64_t memory_end;
        segment->file_offset = xx_data_get_u32(header, sizeof(header), file_fields[index], false);
        segment->memory_offset = xx_data_get_u32(header, sizeof(header), memory_fields[index], false);
        segment->memory_size = xx_data_get_u32(header, sizeof(header), size_fields[index], false);
        segment->compressed = (reader->flags & (UINT32_C(1) << index)) != 0U;
        segment->compressed_size = segment->compressed ? xx_data_get_u32(header, sizeof(header), packed_fields[index], false) : segment->memory_size;
        segment->flags = permissions[index];
        if (!segment->memory_size || !segment->compressed_size || segment->file_offset < XX_NSO_HEADER_SIZE ||
            !nso_span(segment->file_offset, segment->compressed_size, available))
            return false;
        file_end = (uint64_t)segment->file_offset + segment->compressed_size;
        memory_end = (uint64_t)segment->memory_offset + segment->memory_size;
        if (index && (segment->file_offset < previous_file_end || segment->memory_offset < previous_memory_end)) return false;
        previous_file_end = file_end;
        previous_memory_end = memory_end;
        if (file_end > maximum_file_end) maximum_file_end = file_end;
        memory_total += segment->memory_size;
        if (memory_total > XX_NSO_MAX_DECOMPRESSED_SIZE || memory_end > XX_NSO_MAX_DECOMPRESSED_SIZE) return false;
    }
    if (reader->segments[XX_NSO_SEGMENT_TEXT].memory_offset != 0U || reader->bss_size > XX_NSO_MAX_DECOMPRESSED_SIZE - previous_memory_end ||
        maximum_file_end != available)
        return false;

    self->format_size = (int64_t)available;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->is_valid = true;
    self->base_info_handled = true;
    return !nso_stopped(pd);
}

int64_t xx_nintendo_nso_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_nintendo_nso_handle_base_info(self, pd)) ? self->format_size : -1;
}

const xx_nso_segment *xx_nintendo_nso_get_segment(xx_nintendo_nso *reader, xx_nso_segment_kind kind, xx_pd_struct *pd)
{
    return reader && (unsigned)kind < 3U && (reader->format.base_info_handled || xx_nintendo_nso_handle_base_info(&reader->format, pd))
               ? &reader->segments[(unsigned)kind]
               : NULL;
}

bool xx_nintendo_nso_decode_segment(xx_nintendo_nso *reader, xx_nso_segment_kind kind, void *destination, size_t destination_size, size_t *written, xx_pd_struct *pd)
{
    const xx_nso_segment *segment;
    uint8_t *packed;
    size_t decoded = 0U;
    bool result = false;
    if (written) *written = 0U;
    if (!reader || !destination || (unsigned)kind >= 3U || nso_stopped(pd)) return false;
    segment = xx_nintendo_nso_get_segment(reader, kind, pd);
    if (!segment || destination_size < segment->memory_size) return false;
    if (!segment->compressed) {
        if (!xx_io_read_at(reader->format.device, reader->format.base_address + segment->file_offset, destination, segment->memory_size)) return false;
        if (written) *written = segment->memory_size;
        return !nso_stopped(pd);
    }
    packed = (uint8_t *)xx_mem_alloc(segment->compressed_size);
    if (!packed) return false;
    if (xx_io_read_at(reader->format.device, reader->format.base_address + segment->file_offset, packed, segment->compressed_size) && !nso_stopped(pd) &&
        xx_lz4_decompress_block(packed, segment->compressed_size, destination, segment->memory_size, &decoded) && decoded == segment->memory_size && !nso_stopped(pd)) {
        result = true;
        if (written) *written = decoded;
    }
    xx_mem_free(packed);
    return result;
}

bool xx_nintendo_nso_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    static const char *const names[3] = {"text", "ro", "data"};
    xx_nintendo_nso *reader = (xx_nintendo_nso *)self;
    unsigned index;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SEGMENTS) ||
        (!self->base_info_handled && !xx_nintendo_nso_handle_base_info(self, pd)))
        return false;
    output->binary_offset = self->base_address;
    output->binary_size = self->format_size;
    output->module_address = XX_NSO_LOAD_ADDRESS;
    output->entry_point_address = XX_NSO_LOAD_ADDRESS + reader->segments[XX_NSO_SEGMENT_TEXT].memory_offset;
    output->code_base = reader->segments[XX_NSO_SEGMENT_TEXT].file_offset;
    output->start_load_offset = self->base_address;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    output->is_image = true;
    if (!xx_memory_map_add_part(output, self->base_address, XX_NSO_HEADER_SIZE, XX_INVALID_ADDRESS, 0, XX_FILE_PART_HEADER, 0, "header", false)) return false;
    for (index = 0U; index < 3U; ++index) {
        const xx_nso_segment *segment = &reader->segments[index];
        uint64_t address = XX_NSO_LOAD_ADDRESS + segment->memory_offset;
        if (segment->compressed) {
            char packed_name[32];
            (void)xx_rt_snprintf(packed_name, sizeof(packed_name), "%s (compressed)", names[index]);
            if (!xx_memory_map_add_part(output, self->base_address + segment->file_offset, segment->compressed_size, XX_INVALID_ADDRESS, 0, XX_FILE_PART_DATA,
                                        (int32_t)index + 1, packed_name, false) ||
                !xx_memory_map_add_part(output, -1, 0, address, segment->memory_size, XX_FILE_PART_SEGMENT, (int32_t)index + 1, names[index], false))
                return false;
        } else if (!xx_memory_map_add_part(output, self->base_address + segment->file_offset, segment->memory_size, address, segment->memory_size, XX_FILE_PART_SEGMENT,
                                           (int32_t)index + 1, names[index], false))
            return false;
        if (index == XX_NSO_SEGMENT_DATA && reader->bss_size &&
            !xx_memory_map_add_part(output, -1, 0, address + segment->memory_size, reader->bss_size, XX_FILE_PART_SEGMENT, (int32_t)index + 1, "bss", false))
            return false;
    }
    return !nso_stopped(pd) && xx_memory_map_finalize(output);
}
