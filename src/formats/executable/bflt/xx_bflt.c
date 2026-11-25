/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/bflt/xx_bflt.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

static bool bflt_read_header(const Abstractformat *format, xx_bflt_header *header)
{
    uint8_t bytes[XX_BFLT_HEADER_SIZE];
    size_t index;
    if (!format || !format->device || !header || format->base_address < 0 || !xx_io_read_at(format->device, format->base_address, bytes, sizeof(bytes)) ||
        memcmp(bytes, "bFLT", 4) != 0) {
        return false;
    }
    header->revision = xx_data_get_u32(bytes, sizeof(bytes), 4, true);
    header->entry = xx_data_get_u32(bytes, sizeof(bytes), 8, true);
    header->data_start = xx_data_get_u32(bytes, sizeof(bytes), 12, true);
    header->data_end = xx_data_get_u32(bytes, sizeof(bytes), 16, true);
    header->bss_end = xx_data_get_u32(bytes, sizeof(bytes), 20, true);
    header->stack_size = xx_data_get_u32(bytes, sizeof(bytes), 24, true);
    header->relocation_start = xx_data_get_u32(bytes, sizeof(bytes), 28, true);
    header->relocation_count = xx_data_get_u32(bytes, sizeof(bytes), 32, true);
    header->flags = xx_data_get_u32(bytes, sizeof(bytes), 36, true);
    header->build_date = xx_data_get_u32(bytes, sizeof(bytes), 40, true);
    for (index = 0U; index < 5U; ++index) {
        header->filler[index] = xx_data_get_u32(bytes, sizeof(bytes), 44U + index * 4U, true);
    }
    /* Several toolchains store a CPU bit in the last filler word. */
    header->cpu_type = header->filler[4];
    if (!header->cpu_type || header->cpu_type > UINT32_C(0x100)) header->cpu_type = XX_BFLT_CPU_ARM;
    return true;
}

static xx_arch_t bflt_arch(uint32_t cpu)
{
    switch (cpu) {
        case XX_BFLT_CPU_M68K:
        case XX_BFLT_CPU_COLDFIRE: return XX_ARCH_M68K;
        case XX_BFLT_CPU_386: return XX_ARCH_X86;
        case XX_BFLT_CPU_MIPS: return XX_ARCH_MIPS;
        case XX_BFLT_CPU_PPC: return XX_ARCH_PPC;
        case XX_BFLT_CPU_SH: return XX_ARCH_SH;
        case XX_BFLT_CPU_ARM:
        default: return XX_ARCH_ARM;
    }
}

static bool bflt_append_relocation(xx_bflt *reader, size_t *capacity, uint32_t patch, uint32_t target, bool from_got)
{
    xx_bflt_relocation *grown;
    size_t wanted;
    size_t next;
    if (!reader || !capacity || reader->number_of_relocations == UINT32_MAX) return false;
    wanted = (size_t)reader->number_of_relocations + 1U;
    if (wanted > *capacity) {
        next = *capacity ? *capacity * 2U : 32U;
        if (next < wanted) next = wanted;
        if (next > UINT32_MAX) next = UINT32_MAX;
        if (next < wanted || next > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_bflt_relocation *)xx_mem_realloc(reader->relocations, next * sizeof(*grown));
        if (!grown) return false;
        reader->relocations = grown;
        *capacity = next;
    }
    reader->relocations[reader->number_of_relocations].patch_offset = patch;
    reader->relocations[reader->number_of_relocations].target_offset = target;
    reader->relocations[reader->number_of_relocations].from_got = from_got;
    ++reader->number_of_relocations;
    if (from_got) ++reader->number_of_got_relocations;
    return true;
}

static bool bflt_read_be32_relative(const xx_bflt *reader, uint32_t relative_offset, uint32_t *value)
{
    uint8_t bytes[4];
    int64_t absolute;
    if (!reader || !value || reader->format.base_address < 0 || relative_offset > INT64_MAX - reader->format.base_address) return false;
    absolute = reader->format.base_address + (int64_t)relative_offset;
    if (!xx_io_read_at(reader->format.device, absolute, bytes, sizeof(bytes))) return false;
    *value = xx_data_get_u32(bytes, sizeof(bytes), 0, true);
    return true;
}

static bool bflt_parse_relocations(xx_bflt *reader, uint64_t file_size)
{
    const xx_bflt_header *header = &reader->header;
    size_t capacity = 0U;
    uint32_t index;

    reader->relocations_complete = false;
    if (header->flags & (XX_BFLT_FLAG_GZIP | XX_BFLT_FLAG_GZDATA)) return true;

    if ((header->flags & XX_BFLT_FLAG_GOTPIC) && header->data_end >= header->data_start && header->data_end <= file_size) {
        uint64_t cursor;
        for (cursor = header->data_start; cursor + 4U <= header->data_end; cursor += 4U) {
            uint32_t entry;
            if (!bflt_read_be32_relative(reader, (uint32_t)cursor, &entry)) return false;
            if (entry == UINT32_MAX) break;
            if (entry <= UINT32_MAX - XX_BFLT_HEADER_SIZE && (uint64_t)entry + XX_BFLT_HEADER_SIZE + 4U <= file_size) {
                uint32_t patch = entry;
                uint32_t target = entry + XX_BFLT_HEADER_SIZE;
                if (!bflt_append_relocation(reader, &capacity, patch, target, true)) return false;
            }
        }
    }

    if (header->relocation_count == 0U) {
        reader->relocations_complete = true;
        return true;
    }
    if ((uint64_t)header->relocation_start > file_size || (uint64_t)header->relocation_count > (file_size - header->relocation_start) / 4U ||
        header->relocation_count > (UINT32_MAX - header->relocation_start) / 4U) {
        /* Radare accepts the bFLT itself but reports no relocations when this
         * table is truncated or hostile. Keep that useful distinction. */
        if (reader->relocations) xx_mem_free(reader->relocations);
        reader->relocations = NULL;
        reader->number_of_relocations = 0U;
        reader->number_of_got_relocations = 0U;
        return true;
    }
    for (index = 0U; index < header->relocation_count; ++index) {
        uint32_t encoded;
        uint32_t patch;
        uint32_t encoded_target;
        uint32_t target;
        if (!bflt_read_be32_relative(reader, header->relocation_start + index * 4U, &encoded)) return false;
        if (encoded > UINT32_MAX - XX_BFLT_HEADER_SIZE) continue;
        patch = encoded + XX_BFLT_HEADER_SIZE;
        if ((uint64_t)patch + 4U > file_size || patch >= header->bss_end) continue;
        if (!bflt_read_be32_relative(reader, patch, &encoded_target)) return false;
        if (encoded_target > UINT32_MAX - XX_BFLT_HEADER_SIZE) continue;
        target = encoded_target + XX_BFLT_HEADER_SIZE;
        if (!bflt_append_relocation(reader, &capacity, patch, target, false)) return false;
    }
    reader->relocations_complete = true;
    return true;
}

void xx_bflt_init(xx_bflt *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_BFLT;
    reader->format.check_is_valid = xx_bflt_check_is_valid;
    reader->format.handle_base_info = xx_bflt_handle_base_info;
    reader->format.get_format_size = xx_bflt_get_format_size;
    reader->format.get_memory_map = xx_bflt_get_memory_map;
    reader->format.endian = XX_ENDIAN_BIG;
    reader->format.arch = XX_ARCH_ARM;
    reader->format.os = XX_OS_LINUX;
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "bflt");
    xx_format_set_mime_type(&reader->format, "application/x-bflt");
}

xx_bflt *xx_bflt_create(xx_io_device *device, int64_t base_address)
{
    xx_bflt *reader = (xx_bflt *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_bflt_init(reader, device, base_address);
    return reader;
}

void xx_bflt_destroy(xx_bflt *reader)
{
    if (!reader) return;
    if (reader->relocations) xx_mem_free(reader->relocations);
    reader->relocations = NULL;
    reader->number_of_relocations = 0U;
    reader->number_of_got_relocations = 0U;
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_bflt_free(xx_bflt *reader)
{
    if (!reader) return;
    xx_bflt_destroy(reader);
    xx_mem_free(reader);
}

bool xx_bflt_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_bflt_header header;
    (void)pd;
    return bflt_read_header(self, &header) && header.revision == XX_BFLT_VERSION;
}

bool xx_bflt_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_bflt *reader = (xx_bflt *)self;
    int64_t total;
    uint64_t file_size;
    if (!reader || !self->device || xx_pd_is_stopped(pd) || self->base_address < 0) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || !bflt_read_header(self, &reader->header) || reader->header.revision != XX_BFLT_VERSION) return false;
    file_size = (uint64_t)(total - self->base_address);
    self->format_size = (int64_t)file_size;
    self->is_valid = true;
    self->arch = bflt_arch(reader->header.cpu_type);
    self->endian = XX_ENDIAN_BIG;
    self->os = XX_OS_LINUX;
    self->format_type = XX_TYPE_CONSOLE_APPLICATION;
    self->is_executable = true;
    if (!bflt_parse_relocations(reader, file_size)) {
        self->is_valid = false;
        return false;
    }
    self->base_info_handled = true;
    return true;
}

int64_t xx_bflt_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_bflt_handle_base_info(self, pd)) ? self->format_size : -1;
}

bool xx_bflt_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_bflt *reader = (xx_bflt *)self;
    int64_t size;
    uint64_t data_end;
    uint64_t bss_end;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_REGIONS) ||
        (!self->base_info_handled && !xx_bflt_handle_base_info(self, pd)))
        return false;
    size = self->format_size;
    if (size < 0) return false;
    output->binary_offset = self->base_address;
    output->binary_size = size;
    output->module_address = 0U;
    output->entry_point_address = reader->header.entry;
    output->code_base = XX_BFLT_HEADER_SIZE;
    output->start_load_offset = self->base_address;
    output->file_type = XX_FILE_TYPE_BFLT;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_REGIONS;
    output->is_image = true;
    data_end = reader->header.data_end;
    bss_end = reader->header.bss_end;
    if (size >= XX_BFLT_HEADER_SIZE && data_end >= XX_BFLT_HEADER_SIZE && data_end <= (uint64_t)size && bss_end >= data_end && bss_end <= INT64_MAX) {
        if (!xx_memory_map_add_part(output, self->base_address, XX_BFLT_HEADER_SIZE, 0U, XX_BFLT_HEADER_SIZE, XX_FILE_PART_HEADER, 0, "bFLT header", false) ||
            !xx_memory_map_add_part(output, self->base_address + XX_BFLT_HEADER_SIZE, (int64_t)data_end - XX_BFLT_HEADER_SIZE, XX_BFLT_HEADER_SIZE,
                                    (int64_t)bss_end - XX_BFLT_HEADER_SIZE, XX_FILE_PART_REGION, 0, "Load image", false))
            return false;
        if (data_end < (uint64_t)size && !xx_memory_map_add_part(output, self->base_address + (int64_t)data_end, size - (int64_t)data_end, XX_INVALID_ADDRESS,
                                                                 size - (int64_t)data_end, XX_FILE_PART_TABLE, 0, "Relocations/trailer", false))
            return false;
    } else if (size > 0 && !xx_memory_map_add_part(output, self->base_address, size, 0U, size, XX_FILE_PART_REGION, 0, "bFLT image", false)) {
        return false;
    }
    return xx_memory_map_finalize(output);
}

const xx_bflt_header *xx_bflt_get_header(xx_bflt *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_bflt_handle_base_info(&reader->format, pd)) ? &reader->header : NULL;
}

uint32_t xx_bflt_get_number_of_relocations(xx_bflt *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_bflt_handle_base_info(&reader->format, pd)) ? reader->number_of_relocations : 0U;
}

uint32_t xx_bflt_get_number_of_got_relocations(xx_bflt *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_bflt_handle_base_info(&reader->format, pd)) ? reader->number_of_got_relocations : 0U;
}

const xx_bflt_relocation *xx_bflt_get_relocation(xx_bflt *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_bflt_get_number_of_relocations(reader, pd) ? &reader->relocations[index] : NULL;
}

bool xx_bflt_are_relocations_complete(xx_bflt *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_bflt_handle_base_info(&reader->format, pd)) && reader->relocations_complete;
}
