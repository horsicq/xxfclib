/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/qnx_lmf/xx_qnx_lmf.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

typedef struct qnx_lmf_scan_counts {
    uint32_t records;
    uint32_t sections;
    uint32_t resources;
    uint32_t fixups;
    uint64_t image_end;
} qnx_lmf_scan_counts;

static bool qnx_lmf_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool qnx_lmf_read_record_header(const Abstractformat *format, uint64_t available, uint64_t offset, xx_qnx_lmf_record_type *type, uint16_t *data_size)
{
    uint8_t bytes[XX_QNX_LMF_RECORD_HEADER_SIZE];
    uint16_t spare;
    if (!format || !format->device || format->base_address < 0 || !type || !data_size || offset > available || available - offset < sizeof(bytes) ||
        offset > (uint64_t)INT64_MAX || format->base_address > INT64_MAX - (int64_t)offset ||
        !xx_io_read_at(format->device, format->base_address + (int64_t)offset, bytes, sizeof(bytes)))
        return false;
    *type = (xx_qnx_lmf_record_type)bytes[0];
    *data_size = xx_data_get_u16(bytes, sizeof(bytes), 2U, false);
    spare = xx_data_get_u16(bytes, sizeof(bytes), 4U, false);
    return bytes[1] == 0U && spare == 0U && (unsigned)*type <= XX_QNX_LMF_RECORD_LINEAR_FIXUP && *data_size <= XX_QNX_LMF_MAX_RECORD_DATA_SIZE &&
           (uint64_t)*data_size <= available - offset - sizeof(bytes);
}

static bool qnx_lmf_record_shape_is_valid(xx_qnx_lmf_record_type type, uint16_t data_size)
{
    switch (type) {
        case XX_QNX_LMF_RECORD_HEADER: return false;
        case XX_QNX_LMF_RECORD_LOAD:
        case XX_QNX_LMF_RECORD_FIXUP:
        case XX_QNX_LMF_RECORD_8087_FIXUP:
        case XX_QNX_LMF_RECORD_LINEAR_FIXUP: return data_size >= XX_QNX_LMF_DATA_HEADER_SIZE;
        case XX_QNX_LMF_RECORD_RESOURCE: return data_size >= XX_QNX_LMF_RESOURCE_HEADER_SIZE;
        case XX_QNX_LMF_RECORD_READ_WRITE_END:
        case XX_QNX_LMF_RECORD_IMAGE_END: return data_size >= 6U;
        case XX_QNX_LMF_RECORD_COMMENT: return true;
        default: return false;
    }
}

static bool qnx_lmf_scan(const Abstractformat *format, uint64_t available, uint16_t first_size, qnx_lmf_scan_counts *counts, xx_pd_struct *pd)
{
    uint64_t offset = XX_QNX_LMF_RECORD_HEADER_SIZE + first_size;
    qnx_lmf_scan_counts result = {1U, 0U, 0U, 0U, 0U};
    while (offset < available) {
        xx_qnx_lmf_record_type type;
        uint16_t data_size;
        uint64_t next;
        if (result.records >= XX_QNX_LMF_MAX_RECORDS || qnx_lmf_stopped(pd) || !qnx_lmf_read_record_header(format, available, offset, &type, &data_size) ||
            !qnx_lmf_record_shape_is_valid(type, data_size))
            return false;
        next = offset + XX_QNX_LMF_RECORD_HEADER_SIZE + data_size;
        ++result.records;
        if (type == XX_QNX_LMF_RECORD_LOAD) ++result.sections;
        else if (type == XX_QNX_LMF_RECORD_RESOURCE) ++result.resources;
        else if (type == XX_QNX_LMF_RECORD_FIXUP || type == XX_QNX_LMF_RECORD_8087_FIXUP || type == XX_QNX_LMF_RECORD_LINEAR_FIXUP) ++result.fixups;
        if (type == XX_QNX_LMF_RECORD_IMAGE_END) {
            result.image_end = next;
            *counts = result;
            return true;
        }
        offset = next;
    }
    return false;
}

static bool qnx_lmf_read_header_fields(xx_qnx_lmf *reader)
{
    uint8_t bytes[XX_QNX_LMF_HEADER_SIZE];
    xx_qnx_lmf_header *header;
    unsigned index;
    if (!reader || !xx_io_read_at(reader->format.device, reader->format.base_address + XX_QNX_LMF_RECORD_HEADER_SIZE, bytes, sizeof(bytes))) return false;
    header = &reader->header;
    header->version = xx_data_get_u16(bytes, sizeof(bytes), 0U, false);
    header->cflags = xx_data_get_u16(bytes, sizeof(bytes), 2U, false);
    header->cpu = xx_data_get_u16(bytes, sizeof(bytes), 4U, false);
    header->fpu = xx_data_get_u16(bytes, sizeof(bytes), 6U, false);
    header->code_index = xx_data_get_u16(bytes, sizeof(bytes), 8U, false);
    header->stack_index = xx_data_get_u16(bytes, sizeof(bytes), 10U, false);
    header->heap_index = xx_data_get_u16(bytes, sizeof(bytes), 12U, false);
    header->argv_index = xx_data_get_u16(bytes, sizeof(bytes), 14U, false);
    for (index = 0U; index < 4U; ++index) header->spare2[index] = xx_data_get_u16(bytes, sizeof(bytes), 16U + index * 2U, false);
    header->code_offset = xx_data_get_u32(bytes, sizeof(bytes), 24U, false);
    header->stack_nbytes = xx_data_get_u32(bytes, sizeof(bytes), 28U, false);
    header->heap_nbytes = xx_data_get_u32(bytes, sizeof(bytes), 32U, false);
    header->image_base = xx_data_get_u32(bytes, sizeof(bytes), 36U, false);
    for (index = 0U; index < 2U; ++index) header->spare3[index] = xx_data_get_u32(bytes, sizeof(bytes), 40U + index * 4U, false);
    return true;
}

static void qnx_lmf_free_arrays(xx_qnx_lmf_record *records, xx_qnx_lmf_section *sections, xx_qnx_lmf_resource *resources, xx_qnx_lmf_fixup *fixups)
{
    xx_mem_free(fixups);
    xx_mem_free(resources);
    xx_mem_free(sections);
    xx_mem_free(records);
}

void xx_qnx_lmf_init(xx_qnx_lmf *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_QNX_LMF;
    reader->format.check_is_valid = xx_qnx_lmf_check_is_valid;
    reader->format.handle_base_info = xx_qnx_lmf_handle_base_info;
    reader->format.get_format_size = xx_qnx_lmf_get_format_size;
    reader->format.get_memory_map = xx_qnx_lmf_get_memory_map;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_X86;
    reader->format.os = XX_OS_QNX;
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "lmf");
    xx_format_set_mime_type(&reader->format, "application/x-qnx-lmf");
}

xx_qnx_lmf *xx_qnx_lmf_create(xx_io_device *device, int64_t base_address)
{
    xx_qnx_lmf *reader = (xx_qnx_lmf *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_qnx_lmf_init(reader, device, base_address);
    return reader;
}

void xx_qnx_lmf_destroy(xx_qnx_lmf *reader)
{
    if (!reader) return;
    qnx_lmf_free_arrays(reader->records, reader->sections, reader->resources, reader->fixups);
    reader->records = NULL;
    reader->sections = NULL;
    reader->resources = NULL;
    reader->fixups = NULL;
    reader->record_count = 0U;
    reader->section_count = 0U;
    reader->resource_count = 0U;
    reader->fixup_count = 0U;
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_qnx_lmf_free(xx_qnx_lmf *reader)
{
    if (!reader) return;
    xx_qnx_lmf_destroy(reader);
    xx_mem_free(reader);
}

bool xx_qnx_lmf_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    qnx_lmf_scan_counts counts;
    xx_qnx_lmf_record_type type;
    uint16_t data_size;
    int64_t total;
    uint64_t available;
    (void)pd;
    if (!self || !self->device || self->base_address < 0 || (total = xx_io_total_size(self->device)) < self->base_address) return false;
    available = (uint64_t)(total - self->base_address);
    return qnx_lmf_read_record_header(self, available, 0U, &type, &data_size) && type == XX_QNX_LMF_RECORD_HEADER && data_size >= XX_QNX_LMF_HEADER_SIZE &&
           qnx_lmf_scan(self, available, data_size, &counts, pd);
}

bool xx_qnx_lmf_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_qnx_lmf *reader = (xx_qnx_lmf *)self;
    qnx_lmf_scan_counts counts;
    xx_qnx_lmf_record_type first_type;
    uint16_t first_size;
    xx_qnx_lmf_record *records = NULL;
    xx_qnx_lmf_section *sections = NULL;
    xx_qnx_lmf_resource *resources = NULL;
    xx_qnx_lmf_fixup *fixups = NULL;
    uint32_t record_index = 1U, section_index = 0U, resource_index = 0U, fixup_index = 0U;
    uint64_t offset;
    int64_t total;
    uint64_t available;
    bool result = false;

    if (!reader || !self->device || self->base_address < 0 || qnx_lmf_stopped(pd)) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = (uint64_t)(total - self->base_address);
    if (!qnx_lmf_read_record_header(self, available, 0U, &first_type, &first_size) || first_type != XX_QNX_LMF_RECORD_HEADER || first_size < XX_QNX_LMF_HEADER_SIZE ||
        !qnx_lmf_scan(self, available, first_size, &counts, pd) || !qnx_lmf_read_header_fields(reader))
        return false;

    records = (xx_qnx_lmf_record *)xx_mem_alloc((size_t)counts.records * sizeof(*records));
    if (!records) goto cleanup;
    xx_mem_zero(records, (size_t)counts.records * sizeof(*records));
    if (counts.sections) {
        sections = (xx_qnx_lmf_section *)xx_mem_alloc((size_t)counts.sections * sizeof(*sections));
        if (!sections) goto cleanup;
        xx_mem_zero(sections, (size_t)counts.sections * sizeof(*sections));
    }
    if (counts.resources) {
        resources = (xx_qnx_lmf_resource *)xx_mem_alloc((size_t)counts.resources * sizeof(*resources));
        if (!resources) goto cleanup;
        xx_mem_zero(resources, (size_t)counts.resources * sizeof(*resources));
    }
    if (counts.fixups) {
        fixups = (xx_qnx_lmf_fixup *)xx_mem_alloc((size_t)counts.fixups * sizeof(*fixups));
        if (!fixups) goto cleanup;
        xx_mem_zero(fixups, (size_t)counts.fixups * sizeof(*fixups));
    }

    records[0].type = XX_QNX_LMF_RECORD_HEADER;
    records[0].file_offset = 0;
    records[0].data_offset = XX_QNX_LMF_RECORD_HEADER_SIZE;
    records[0].data_size = first_size;
    offset = XX_QNX_LMF_RECORD_HEADER_SIZE + first_size;
    while (record_index < counts.records) {
        xx_qnx_lmf_record_type type;
        uint16_t data_size;
        uint8_t small[XX_QNX_LMF_RESOURCE_HEADER_SIZE];
        uint64_t data_offset;
        if (qnx_lmf_stopped(pd) || !qnx_lmf_read_record_header(self, available, offset, &type, &data_size)) goto cleanup;
        data_offset = offset + XX_QNX_LMF_RECORD_HEADER_SIZE;
        records[record_index].type = type;
        records[record_index].file_offset = (int64_t)offset;
        records[record_index].data_offset = (int64_t)data_offset;
        records[record_index].data_size = data_size;
        if (type == XX_QNX_LMF_RECORD_LOAD || type == XX_QNX_LMF_RECORD_FIXUP || type == XX_QNX_LMF_RECORD_8087_FIXUP || type == XX_QNX_LMF_RECORD_LINEAR_FIXUP) {
            if (!xx_io_read_at(self->device, self->base_address + (int64_t)data_offset, small, XX_QNX_LMF_DATA_HEADER_SIZE)) goto cleanup;
            if (type == XX_QNX_LMF_RECORD_LOAD) {
                xx_qnx_lmf_section *section = &sections[section_index++];
                section->segment = xx_data_get_u16(small, sizeof(small), 0U, false);
                section->load_offset = xx_data_get_u32(small, sizeof(small), 2U, false);
                section->record_offset = (int64_t)offset;
                section->raw_data_offset = (int64_t)data_offset;
                section->payload_offset = (int64_t)data_offset + XX_QNX_LMF_DATA_HEADER_SIZE;
                section->size = data_size - XX_QNX_LMF_DATA_HEADER_SIZE;
            } else {
                xx_qnx_lmf_fixup *fixup = &fixups[fixup_index++];
                fixup->type = type;
                fixup->segment = xx_data_get_u16(small, sizeof(small), 0U, false);
                fixup->offset = xx_data_get_u32(small, sizeof(small), 2U, false);
                fixup->record_offset = (int64_t)offset;
            }
        } else if (type == XX_QNX_LMF_RECORD_RESOURCE) {
            xx_qnx_lmf_resource *resource = &resources[resource_index++];
            if (!xx_io_read_at(self->device, self->base_address + (int64_t)data_offset, small, sizeof(small))) goto cleanup;
            resource->type = xx_data_get_u16(small, sizeof(small), 0U, false);
            resource->record_offset = (int64_t)offset;
            resource->data_offset = (int64_t)data_offset + XX_QNX_LMF_RESOURCE_HEADER_SIZE;
            resource->size = data_size - XX_QNX_LMF_RESOURCE_HEADER_SIZE;
        } else if (type == XX_QNX_LMF_RECORD_READ_WRITE_END) {
            if (!xx_io_read_at(self->device, self->base_address + (int64_t)data_offset, small, 6U)) goto cleanup;
            reader->verification = xx_data_get_u16(small, sizeof(small), 0U, false);
            reader->signature = xx_data_get_u32(small, sizeof(small), 2U, false);
            reader->has_read_write_end = true;
        }
        offset = data_offset + data_size;
        ++record_index;
        if (type == XX_QNX_LMF_RECORD_IMAGE_END) break;
    }
    if (record_index != counts.records || section_index != counts.sections || resource_index != counts.resources || fixup_index != counts.fixups) goto cleanup;

    reader->records = records;
    reader->sections = sections;
    reader->resources = resources;
    reader->fixups = fixups;
    reader->record_count = counts.records;
    reader->section_count = counts.sections;
    reader->resource_count = counts.resources;
    reader->fixup_count = counts.fixups;
    records = NULL;
    sections = NULL;
    resources = NULL;
    fixups = NULL;
    self->format_size = (int64_t)counts.image_end;
    self->overlay_offset = counts.image_end < available ? self->base_address + (int64_t)counts.image_end : -1;
    self->overlay_size = (int64_t)(available - counts.image_end);
    self->is_valid = true;
    self->base_info_handled = true;
    result = true;
cleanup:
    qnx_lmf_free_arrays(records, sections, resources, fixups);
    return result && !qnx_lmf_stopped(pd);
}

int64_t xx_qnx_lmf_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_qnx_lmf_handle_base_info(self, pd)) ? self->format_size : -1;
}

const xx_qnx_lmf_header *xx_qnx_lmf_get_header(xx_qnx_lmf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_qnx_lmf_handle_base_info(&reader->format, pd)) ? &reader->header : NULL;
}

uint32_t xx_qnx_lmf_get_number_of_records(xx_qnx_lmf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_qnx_lmf_handle_base_info(&reader->format, pd)) ? reader->record_count : 0U;
}

const xx_qnx_lmf_record *xx_qnx_lmf_get_record(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_qnx_lmf_get_number_of_records(reader, pd) ? &reader->records[index] : NULL;
}

uint32_t xx_qnx_lmf_get_number_of_sections(xx_qnx_lmf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_qnx_lmf_handle_base_info(&reader->format, pd)) ? reader->section_count : 0U;
}

const xx_qnx_lmf_section *xx_qnx_lmf_get_section(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_qnx_lmf_get_number_of_sections(reader, pd) ? &reader->sections[index] : NULL;
}

bool xx_qnx_lmf_read_section(xx_qnx_lmf *reader, uint32_t index, void *buffer, size_t size, xx_pd_struct *pd)
{
    const xx_qnx_lmf_section *section = xx_qnx_lmf_get_section(reader, index, pd);
    if (!section || size > section->size || qnx_lmf_stopped(pd)) return false;
    if (!size) return true;
    return buffer && xx_io_read_at(reader->format.device, reader->format.base_address + section->payload_offset, buffer, size);
}

uint32_t xx_qnx_lmf_get_number_of_resources(xx_qnx_lmf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_qnx_lmf_handle_base_info(&reader->format, pd)) ? reader->resource_count : 0U;
}

const xx_qnx_lmf_resource *xx_qnx_lmf_get_resource(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_qnx_lmf_get_number_of_resources(reader, pd) ? &reader->resources[index] : NULL;
}

bool xx_qnx_lmf_read_resource(xx_qnx_lmf *reader, uint32_t index, void *buffer, size_t size, xx_pd_struct *pd)
{
    const xx_qnx_lmf_resource *resource = xx_qnx_lmf_get_resource(reader, index, pd);
    if (!resource || size > resource->size || qnx_lmf_stopped(pd)) return false;
    if (!size) return true;
    return buffer && xx_io_read_at(reader->format.device, reader->format.base_address + resource->data_offset, buffer, size);
}

uint32_t xx_qnx_lmf_get_number_of_fixups(xx_qnx_lmf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_qnx_lmf_handle_base_info(&reader->format, pd)) ? reader->fixup_count : 0U;
}

const xx_qnx_lmf_fixup *xx_qnx_lmf_get_fixup(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_qnx_lmf_get_number_of_fixups(reader, pd) ? &reader->fixups[index] : NULL;
}

bool xx_qnx_lmf_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_qnx_lmf *reader = (xx_qnx_lmf *)self;
    uint32_t section_index = 0U;
    uint32_t resource_index = 0U;
    int32_t part_number = 0;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SECTIONS) ||
        (!self->base_info_handled && !xx_qnx_lmf_handle_base_info(self, pd)))
        return false;
    output->binary_offset = self->base_address;
    output->binary_size = self->format_size;
    output->module_address = reader->header.image_base;
    output->entry_point_address = reader->header.code_offset;
    output->code_base = 0;
    output->start_load_offset = self->base_address;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    if (!xx_memory_map_reserve(output, (size_t)reader->section_count + reader->resource_count)) return false;
    while (section_index < reader->section_count || resource_index < reader->resource_count) {
        bool use_resource = resource_index < reader->resource_count &&
                            (section_index >= reader->section_count || reader->resources[resource_index].data_offset <= reader->sections[section_index].raw_data_offset);
        if (qnx_lmf_stopped(pd)) return false;
        if (use_resource) {
            const xx_qnx_lmf_resource *resource = &reader->resources[resource_index++];
            if (!xx_memory_map_add_part(output, self->base_address + resource->data_offset, resource->size, (uint64_t)resource->data_offset, resource->size,
                                        XX_FILE_PART_RESOURCE, part_number++, "LMF_RESOURCE", false))
                return false;
        } else {
            const xx_qnx_lmf_section *section = &reader->sections[section_index++];
            if (!xx_memory_map_add_part(output, self->base_address + section->raw_data_offset, section->size, (uint64_t)section->raw_data_offset, section->size,
                                        XX_FILE_PART_SECTION, part_number++, "LMF_LOAD", false))
                return false;
        }
    }
    return !qnx_lmf_stopped(pd) && xx_memory_map_finalize(output);
}
