/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/menuet/xx_menuet.h"
#include "../../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool menuet_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool menuet_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    xx_menuet *reader = (xx_menuet *)format;
    uint8_t header[52];
    int64_t available = pm_available(format);
    uint32_t header_size;
    uint32_t declared_version;
    uint32_t entry;
    uint32_t image_size;
    uint32_t memory_size;

    if (available < 24 || available > UINT32_MAX || menuet_stop(pd) || !pm_read(format, 0, header, 8) || xx_rt_memcmp(header, "MENUET0", 7)) return false;
    if (header[7] == '0') header_size = 28;
    else if (header[7] == '1') header_size = 36;
    else if (header[7] == '2') header_size = 52;
    else return false;
    if (available < header_size || !pm_read(format, 0, header, header_size)) return false;

    declared_version = xx_data_get_u32(header + 8, 4, 0, false);
    entry = xx_data_get_u32(header + 12, 4, 0, false);
    image_size = xx_data_get_u32(header + 16, 4, 0, false);
    memory_size = xx_data_get_u32(header + 20, 4, 0, false);
    if ((header[7] != '0' && declared_version != 1U) || entry < header_size || entry >= image_size || image_size < header_size || image_size > (uint32_t)available ||
        memory_size < image_size || memory_size > UINT32_C(0x80000000))
        return false;
    if (header[7] == '2') {
        uint32_t import_start = xx_data_get_u32(header + 40, 4, 0, false);
        uint32_t import_end = xx_data_get_u32(header + 44, 4, 0, false);
        uint32_t main_entry = xx_data_get_u32(header + 48, 4, 0, false);
        if (import_end < import_start || import_end > image_size || (main_entry && (main_entry < header_size || main_entry >= image_size))) return false;
        if (main_entry) entry = main_entry;
    }
    if (!pm_add(format, stream, "header.bin", 0, header_size) ||
        (image_size > header_size && !pm_add(format, stream, "image.bin", header_size, image_size - header_size)))
        return false;
    reader->header_version = (uint8_t)(header[7] - '0');
    reader->entry_point = entry;
    reader->image_size = image_size;
    reader->memory_size = memory_size;
    stream->size = image_size;
    return !menuet_stop(pd);
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    return menuet_parse(format, stream, pd);
}

void xx_menuet_init(xx_menuet *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, XX_FILE_TYPE_MENUET, "menuet");
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.os = XX_OS_MENUET;
    reader->format.arch = XX_ARCH_X86;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.is_executable = true;
    reader->format.get_memory_map = xx_menuet_get_memory_map;
}
xx_menuet *xx_menuet_create(xx_io_device *device, int64_t base_address)
{
    xx_menuet *reader = (xx_menuet *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_menuet_init(reader, device, base_address);
    return reader;
}
void xx_menuet_destroy(xx_menuet *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}
void xx_menuet_free(xx_menuet *reader)
{
    if (reader) {
        xx_menuet_destroy(reader);
        xx_mem_free(reader);
    }
}
bool xx_menuet_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_valid(format, pd);
}
bool xx_menuet_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_handle(format, pd);
}

bool xx_menuet_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *map, xx_pd_struct *pd)
{
    xx_menuet *reader = (xx_menuet *)format;
    if (!reader || !map || !format->base_info_handled || menuet_stop(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_MAPS;
    if (mode != XX_MEMORY_MAP_MODE_MAPS) return false;
    map->binary_offset = format->base_address;
    map->module_address = 0;
    map->binary_size = reader->image_size;
    map->entry_point_address = reader->entry_point;
    map->code_base = 0;
    map->start_load_offset = format->base_address;
    map->file_type = format->file_type;
    map->format_type = format->format_type;
    map->endian = format->endian;
    map->arch = format->arch;
    map->mode = mode;
    return xx_memory_map_add_part(map, format->base_address, reader->image_size, 0, reader->memory_size, XX_FILE_PART_REGION, 0, "image", false) &&
           xx_memory_map_finalize(map);
}
