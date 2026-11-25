/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/pebble/xx_pebble.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

typedef struct xx_pebble_fields_s {
    xx_pebble_version structure_version;
    xx_pebble_version sdk_version;
    xx_pebble_version application_version;
    uint16_t image_size;
    uint32_t entry_point;
    uint32_t image_crc;
    char application_name[XX_PEBBLE_APP_NAME_SIZE + 1U];
    char company_name[XX_PEBBLE_COMPANY_NAME_SIZE + 1U];
    uint32_t icon_resource_id;
    uint32_t symbol_table_address;
    uint32_t flags;
    uint32_t relocation_count;
    uint32_t relocation_offset;
    uint8_t uuid[XX_PEBBLE_UUID_SIZE];
    uint32_t header_crc;
    int64_t format_size;
} xx_pebble_fields;

static bool pebble_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool pebble_parse(const Abstractformat *format, xx_pebble_fields *fields, xx_pd_struct *pd)
{
    uint8_t header[XX_PEBBLE_APP_HEADER_SIZE];
    int64_t total;
    int64_t available;
    uint64_t relocation_size;
    uint64_t format_size;
    if (!format || !format->device || !fields || format->base_address < 0 || pebble_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    if (available < (int64_t)sizeof(header) || !xx_io_read_at(format->device, format->base_address, header, sizeof(header)) ||
        xx_rt_memcmp(header, "PBLAPP\0\0", 8U) != 0)
        return false;

    xx_mem_zero(fields, sizeof(*fields));
    fields->structure_version.major = header[8];
    fields->structure_version.minor = header[9];
    fields->sdk_version.major = header[10];
    fields->sdk_version.minor = header[11];
    fields->application_version.major = header[12];
    fields->application_version.minor = header[13];
    fields->image_size = xx_data_get_u16(header, sizeof(header), 14U, false);
    fields->entry_point = xx_data_get_u32(header, sizeof(header), 16U, false);
    fields->image_crc = xx_data_get_u32(header, sizeof(header), 20U, false);
    xx_mem_copy(fields->application_name, header + 24U, XX_PEBBLE_APP_NAME_SIZE);
    xx_mem_copy(fields->company_name, header + 56U, XX_PEBBLE_COMPANY_NAME_SIZE);
    fields->icon_resource_id = xx_data_get_u32(header, sizeof(header), 88U, false);
    fields->symbol_table_address = xx_data_get_u32(header, sizeof(header), 92U, false);
    fields->flags = xx_data_get_u32(header, sizeof(header), 96U, false);
    /* The on-disk v0x10 header has a relocation count here. The historical
     * Radare2 reader treated it as a file offset and consumed the UUID as a
     * second count, producing the negative-region regression in pebble:2. */
    fields->relocation_count = xx_data_get_u32(header, sizeof(header), 100U, false);
    fields->relocation_offset = fields->image_size;
    xx_mem_copy(fields->uuid, header + 104U, XX_PEBBLE_UUID_SIZE);
    fields->header_crc = xx_data_get_u32(header, sizeof(header), 120U, false);

    if (fields->image_size < XX_PEBBLE_APP_HEADER_SIZE || fields->image_size > (uint64_t)available || fields->entry_point < XX_PEBBLE_APP_HEADER_SIZE ||
        fields->entry_point >= fields->image_size || (fields->symbol_table_address != 0U && fields->symbol_table_address >= fields->image_size))
        return false;
    relocation_size = (uint64_t)fields->relocation_count * XX_PEBBLE_RELOCATION_SIZE;
    format_size = (uint64_t)fields->image_size + relocation_size;
    if (format_size > (uint64_t)available || format_size > (uint64_t)INT64_MAX) return false;
    fields->format_size = (int64_t)format_size;
    return !pebble_stopped(pd);
}

static void pebble_commit(xx_pebble *reader, const xx_pebble_fields *fields)
{
    reader->structure_version = fields->structure_version;
    reader->sdk_version = fields->sdk_version;
    reader->application_version = fields->application_version;
    reader->image_size = fields->image_size;
    reader->entry_point = fields->entry_point;
    reader->image_crc = fields->image_crc;
    xx_mem_copy(reader->application_name, fields->application_name, sizeof(reader->application_name));
    xx_mem_copy(reader->company_name, fields->company_name, sizeof(reader->company_name));
    reader->icon_resource_id = fields->icon_resource_id;
    reader->symbol_table_address = fields->symbol_table_address;
    reader->flags = fields->flags;
    reader->relocation_count = fields->relocation_count;
    reader->relocation_offset = fields->relocation_offset;
    xx_mem_copy(reader->uuid, fields->uuid, sizeof(reader->uuid));
    reader->header_crc = fields->header_crc;
}

void xx_pebble_init(xx_pebble *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_PEBBLE;
    reader->format.check_is_valid = xx_pebble_check_is_valid;
    reader->format.handle_base_info = xx_pebble_handle_base_info;
    reader->format.get_format_size = xx_pebble_get_format_size;
    reader->format.get_memory_map = xx_pebble_get_memory_map;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_ARM;
    reader->format.os = XX_OS_PEBBLE;
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "bin");
    xx_format_set_mime_type(&reader->format, "application/x-pebble-app");
}

xx_pebble *xx_pebble_create(xx_io_device *device, int64_t base_address)
{
    xx_pebble *reader = (xx_pebble *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pebble_init(reader, device, base_address);
    return reader;
}

void xx_pebble_destroy(xx_pebble *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pebble_free(xx_pebble *reader)
{
    if (!reader) return;
    xx_pebble_destroy(reader);
    xx_mem_free(reader);
}

bool xx_pebble_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    xx_pebble_fields fields;
    return pebble_parse(format, &fields, pd);
}

bool xx_pebble_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_pebble *reader = (xx_pebble *)format;
    xx_pebble_fields fields;
    int64_t available;
    if (!reader || !format->device || pebble_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    if (!pebble_parse(format, &fields, pd)) return false;
    pebble_commit(reader, &fields);
    format->format_size = fields.format_size;
    available = xx_io_total_size(format->device) - format->base_address;
    format->overlay_size = available - fields.format_size;
    format->overlay_offset = format->overlay_size > 0 ? format->base_address + fields.format_size : -1;
    format->is_valid = true;
    format->base_info_handled = true;
    return !pebble_stopped(pd);
}

int64_t xx_pebble_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_pebble_handle_base_info(format, pd)) ? format->format_size : -1;
}

bool xx_pebble_get_relocation(xx_pebble *reader, uint32_t index, uint32_t *value, xx_pd_struct *pd)
{
    uint8_t bytes[XX_PEBBLE_RELOCATION_SIZE];
    int64_t offset;
    if (!reader || !value || pebble_stopped(pd) || (!reader->format.base_info_handled && !xx_pebble_handle_base_info(&reader->format, pd)) ||
        index >= reader->relocation_count)
        return false;
    offset = reader->format.base_address + (int64_t)reader->relocation_offset + (int64_t)index * XX_PEBBLE_RELOCATION_SIZE;
    if (!xx_io_read_at(reader->format.device, offset, bytes, sizeof(bytes))) return false;
    *value = xx_data_get_u32(bytes, sizeof(bytes), 0U, false);
    return true;
}

bool xx_pebble_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_pebble *reader = (xx_pebble *)format;
    int64_t image_payload;
    int64_t relocation_size;
    if (!reader || !output || pebble_stopped(pd) || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SECTIONS && mode != XX_MEMORY_MAP_MODE_MAPS) ||
        (!format->base_info_handled && !xx_pebble_handle_base_info(format, pd)))
        return false;
    image_payload = (int64_t)reader->image_size - XX_PEBBLE_APP_HEADER_SIZE;
    relocation_size = (int64_t)reader->relocation_count * XX_PEBBLE_RELOCATION_SIZE;
    output->binary_offset = format->base_address;
    output->binary_size = format->format_size;
    output->module_address = 0U;
    output->entry_point_address = reader->entry_point;
    output->code_base = XX_PEBBLE_APP_HEADER_SIZE;
    output->start_load_offset = format->base_address;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    if (!xx_memory_map_add_part(output, format->base_address, XX_PEBBLE_APP_HEADER_SIZE, 0U, XX_PEBBLE_APP_HEADER_SIZE, XX_FILE_PART_HEADER, 0, "header", false) ||
        !xx_memory_map_add_part(output, format->base_address + XX_PEBBLE_APP_HEADER_SIZE, image_payload, XX_PEBBLE_APP_HEADER_SIZE, image_payload, XX_FILE_PART_SECTION,
                                1, "image", false))
        return false;
    if (relocation_size > 0 && !xx_memory_map_add_part(output, format->base_address + reader->relocation_offset, relocation_size, reader->relocation_offset,
                                                       relocation_size, XX_FILE_PART_TABLE, 2, "relocations", false))
        return false;
    return !pebble_stopped(pd) && xx_memory_map_finalize(output);
}
