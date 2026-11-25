/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/cgc/xx_cgc.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

static bool cgc_span(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static bool cgc_read_header(const Abstractformat *format, xx_cgc_header *header)
{
    uint8_t bytes[XX_CGC_HEADER_SIZE];
    bool big_endian;
    if (!format || !format->device || !header || format->base_address < 0 || !xx_io_read_at(format->device, format->base_address, bytes, sizeof(bytes)) ||
        bytes[0] != 0x7fU || bytes[1] != 'C' || bytes[2] != 'G' || bytes[3] != 'C' || bytes[4] != 1U || (bytes[5] != 1U && bytes[5] != 2U) || bytes[6] != 1U) {
        return false;
    }
    big_endian = bytes[5] == 2U;
    header->data_encoding = bytes[5];
    header->abi = bytes[7];
    header->type = xx_data_get_u16(bytes, sizeof(bytes), 16, big_endian);
    header->machine = xx_data_get_u16(bytes, sizeof(bytes), 18, big_endian);
    header->version = xx_data_get_u32(bytes, sizeof(bytes), 20, big_endian);
    header->entry = xx_data_get_u32(bytes, sizeof(bytes), 24, big_endian);
    header->program_header_offset = xx_data_get_u32(bytes, sizeof(bytes), 28, big_endian);
    header->section_header_offset = xx_data_get_u32(bytes, sizeof(bytes), 32, big_endian);
    header->flags = xx_data_get_u32(bytes, sizeof(bytes), 36, big_endian);
    header->header_size = xx_data_get_u16(bytes, sizeof(bytes), 40, big_endian);
    header->program_header_entry_size = xx_data_get_u16(bytes, sizeof(bytes), 42, big_endian);
    header->program_header_count = xx_data_get_u16(bytes, sizeof(bytes), 44, big_endian);
    header->section_header_entry_size = xx_data_get_u16(bytes, sizeof(bytes), 46, big_endian);
    header->section_header_count = xx_data_get_u16(bytes, sizeof(bytes), 48, big_endian);
    header->section_name_index = xx_data_get_u16(bytes, sizeof(bytes), 50, big_endian);
    return true;
}

static xx_arch_t cgc_machine_arch(uint16_t machine)
{
    switch (machine) {
        case 3U: return XX_ARCH_X86;
        case 4U: return XX_ARCH_M68K;
        case 8U: return XX_ARCH_MIPS;
        case 20U: return XX_ARCH_PPC;
        case 40U: return XX_ARCH_ARM;
        default: return XX_ARCH_UNKNOWN;
    }
}

static bool cgc_read_program_headers(xx_cgc *reader, uint64_t file_size)
{
    const xx_cgc_header *header = &reader->header;
    uint16_t index;
    bool big_endian = header->data_encoding == 2U;
    if (!header->program_header_count) return true;
    if (header->program_header_entry_size < XX_CGC_PROGRAM_HEADER_SIZE ||
        !cgc_span(header->program_header_offset, (uint64_t)header->program_header_count * header->program_header_entry_size, file_size)) {
        return false;
    }
    reader->program_headers = (xx_cgc_program_header *)xx_mem_alloc((size_t)header->program_header_count * sizeof(*reader->program_headers));
    if (!reader->program_headers) return false;
    xx_mem_zero(reader->program_headers, (size_t)header->program_header_count * sizeof(*reader->program_headers));
    for (index = 0U; index < header->program_header_count; ++index) {
        uint8_t bytes[XX_CGC_PROGRAM_HEADER_SIZE];
        xx_cgc_program_header *program = &reader->program_headers[index];
        uint64_t relative = (uint64_t)header->program_header_offset + (uint64_t)index * header->program_header_entry_size;
        int64_t absolute;
        if (relative > (uint64_t)(INT64_MAX - reader->format.base_address)) return false;
        absolute = reader->format.base_address + (int64_t)relative;
        if (!xx_io_read_at(reader->format.device, absolute, bytes, sizeof(bytes))) return false;
        program->type = xx_data_get_u32(bytes, sizeof(bytes), 0, big_endian);
        program->offset = xx_data_get_u32(bytes, sizeof(bytes), 4, big_endian);
        program->virtual_address = xx_data_get_u32(bytes, sizeof(bytes), 8, big_endian);
        program->physical_address = xx_data_get_u32(bytes, sizeof(bytes), 12, big_endian);
        program->file_size = xx_data_get_u32(bytes, sizeof(bytes), 16, big_endian);
        program->memory_size = xx_data_get_u32(bytes, sizeof(bytes), 20, big_endian);
        program->flags = xx_data_get_u32(bytes, sizeof(bytes), 24, big_endian);
        program->alignment = xx_data_get_u32(bytes, sizeof(bytes), 28, big_endian);
        if (program->type == XX_CGC_PT_LOAD && (program->memory_size < program->file_size || !cgc_span(program->offset, program->file_size, file_size))) return false;
    }
    reader->number_of_program_headers = header->program_header_count;
    return true;
}

void xx_cgc_init(xx_cgc *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_CGC;
    reader->format.check_is_valid = xx_cgc_check_is_valid;
    reader->format.handle_base_info = xx_cgc_handle_base_info;
    reader->format.get_format_size = xx_cgc_get_format_size;
    reader->format.get_memory_map = xx_cgc_get_memory_map;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_X86;
    reader->format.os = XX_OS_GENERIC;
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "cgc");
    xx_format_set_mime_type(&reader->format, "application/x-cgc");
}

xx_cgc *xx_cgc_create(xx_io_device *device, int64_t base_address)
{
    xx_cgc *reader = (xx_cgc *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_cgc_init(reader, device, base_address);
    return reader;
}

void xx_cgc_destroy(xx_cgc *reader)
{
    if (!reader) return;
    if (reader->program_headers) xx_mem_free(reader->program_headers);
    reader->program_headers = NULL;
    reader->number_of_program_headers = 0U;
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_cgc_free(xx_cgc *reader)
{
    if (!reader) return;
    xx_cgc_destroy(reader);
    xx_mem_free(reader);
}

bool xx_cgc_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cgc_header header;
    int64_t total;
    uint64_t file_size;
    (void)pd;
    if (!self || self->base_address < 0 || !cgc_read_header(self, &header) || header.version != 1U || header.header_size < XX_CGC_HEADER_SIZE) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    file_size = (uint64_t)(total - self->base_address);
    if (header.program_header_count && (header.program_header_entry_size < XX_CGC_PROGRAM_HEADER_SIZE ||
                                        !cgc_span(header.program_header_offset, (uint64_t)header.program_header_count * header.program_header_entry_size, file_size)))
        return false;
    if (header.section_header_count && (header.section_header_entry_size < XX_CGC_SECTION_HEADER_SIZE ||
                                        !cgc_span(header.section_header_offset, (uint64_t)header.section_header_count * header.section_header_entry_size, file_size)))
        return false;
    return true;
}

bool xx_cgc_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cgc *reader = (xx_cgc *)self;
    int64_t total;
    uint64_t file_size;
    if (!reader || !self->device || xx_pd_is_stopped(pd) || self->base_address < 0) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || !xx_cgc_check_is_valid(self, pd) || !cgc_read_header(self, &reader->header)) return false;
    file_size = (uint64_t)(total - self->base_address);
    if (!cgc_read_program_headers(reader, file_size)) return false;
    self->format_size = (int64_t)file_size;
    self->endian = reader->header.data_encoding == 2U ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->arch = cgc_machine_arch(reader->header.machine);
    self->os = XX_OS_GENERIC;
    self->format_type = XX_TYPE_CONSOLE_APPLICATION;
    self->is_executable = true;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_cgc_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_cgc_handle_base_info(self, pd)) ? self->format_size : -1;
}

bool xx_cgc_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_cgc *reader = (xx_cgc *)self;
    uint16_t index;
    bool added = false;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SEGMENTS && mode != XX_MEMORY_MAP_MODE_REGIONS) ||
        (!self->base_info_handled && !xx_cgc_handle_base_info(self, pd)))
        return false;
    output->binary_offset = self->base_address;
    output->binary_size = self->format_size;
    output->module_address = XX_INVALID_ADDRESS;
    output->entry_point_address = reader->header.entry;
    output->code_base = reader->header.entry;
    output->start_load_offset = self->base_address;
    output->file_type = XX_FILE_TYPE_CGC;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    output->is_image = true;
    for (index = 0U; index < reader->number_of_program_headers; ++index) {
        const xx_cgc_program_header *program = &reader->program_headers[index];
        if (program->type != XX_CGC_PT_LOAD || (!program->file_size && !program->memory_size)) continue;
        if (!xx_memory_map_add_part(output, self->base_address + program->offset, program->file_size, program->virtual_address, program->memory_size,
                                    XX_FILE_PART_SEGMENT, index, "LOAD", false))
            return false;
        added = true;
    }
    if (!added && self->format_size > 0 &&
        !xx_memory_map_add_part(output, self->base_address, self->format_size, 0U, self->format_size, XX_FILE_PART_REGION, 0, "CGC image", false))
        return false;
    return xx_memory_map_finalize(output);
}

const xx_cgc_header *xx_cgc_get_header(xx_cgc *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_cgc_handle_base_info(&reader->format, pd)) ? &reader->header : NULL;
}

uint16_t xx_cgc_get_number_of_program_headers(xx_cgc *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_cgc_handle_base_info(&reader->format, pd)) ? reader->number_of_program_headers : 0U;
}

const xx_cgc_program_header *xx_cgc_get_program_header(xx_cgc *reader, uint16_t index, xx_pd_struct *pd)
{
    return reader && index < xx_cgc_get_number_of_program_headers(reader, pd) ? &reader->program_headers[index] : NULL;
}
