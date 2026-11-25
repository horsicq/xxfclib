/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/game/msx_rom/xx_msx_rom.h"
#include "../../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool msx_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool msx_power_of_two(uint64_t value)
{
    return value && !(value & (value - 1U));
}
static bool msx_rom_pointer(uint16_t value, uint16_t base, uint64_t size)
{
    return value == 0 || (value >= base && (uint64_t)(value - base) < size);
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    xx_msx_rom *reader = (xx_msx_rom *)format;
    uint8_t header[16];
    int64_t available = pm_available(format);
    uint16_t base;
    xx_mem_zero(header, sizeof(header));
    if (available < 8 || available > UINT32_C(0x100000) || msx_stop(pd) ||
        !pm_read(format, 0, header, (size_t)(available < (int64_t)sizeof(header) ? available : (int64_t)sizeof(header))))
        return false;
    if (header[0] == 'A' && header[1] == 'B') {
        uint16_t init = xx_data_get_u16(header + 2, 2, 0, false);
        uint16_t runtime = xx_data_get_u16(header + 4, 2, 0, false);
        uint16_t device = xx_data_get_u16(header + 6, 2, 0, false);
        uint16_t text = xx_data_get_u16(header + 8, 2, 0, false);
        if (available < 8192 || (available & 0x1fff) || !msx_power_of_two((uint64_t)available / 8192U) || !init) return false;
        base = (uint16_t)(init & 0xff00U);
        if (!msx_rom_pointer(init, base, (uint64_t)available) || !msx_rom_pointer(runtime, base, (uint64_t)available) ||
            !msx_rom_pointer(device, base, (uint64_t)available) || !msx_rom_pointer(text, base, (uint64_t)available))
            return false;
        reader->image_kind = XX_MSX_IMAGE_ROM;
        reader->init_address = init;
        reader->runtime_address = runtime;
        reader->device_address = device;
        reader->text_address = text;
        reader->start_address = base;
        reader->end_address = (uint16_t)(base + (uint16_t)((uint64_t)available > UINT16_MAX ? UINT16_MAX : (uint64_t)available - 1U));
        reader->header_size = 16;
    } else if (header[0] == 0xfe) {
        uint16_t start = xx_data_get_u16(header + 1, 2, 0, false);
        uint16_t end = xx_data_get_u16(header + 3, 2, 0, false);
        uint16_t init = xx_data_get_u16(header + 5, 2, 0, false);
        uint32_t payload;
        if (end < start || init < start || init > end) return false;
        payload = (uint32_t)end - start + 1U;
        if ((uint64_t)payload + 7U > (uint64_t)available) return false;
        reader->image_kind = XX_MSX_IMAGE_BINARY;
        reader->start_address = start;
        reader->end_address = end;
        reader->init_address = init;
        reader->header_size = 7;
    } else return false;
    if (!pm_add(format, stream, "header.bin", 0, reader->header_size) ||
        (available > reader->header_size && !pm_add(format, stream, "image.bin", reader->header_size, available - reader->header_size)))
        return false;
    stream->size = available;
    return true;
}

void xx_msx_rom_init(xx_msx_rom *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, XX_FILE_TYPE_MSX_ROM, "msx");
    reader->format.format_type = XX_TYPE_ROM;
    reader->format.os = XX_OS_MSX;
    reader->format.arch = XX_ARCH_Z80;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.is_executable = true;
    reader->format.get_memory_map = xx_msx_rom_get_memory_map;
}
xx_msx_rom *xx_msx_rom_create(xx_io_device *device, int64_t base_address)
{
    xx_msx_rom *reader = (xx_msx_rom *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_msx_rom_init(reader, device, base_address);
    return reader;
}
void xx_msx_rom_destroy(xx_msx_rom *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}
void xx_msx_rom_free(xx_msx_rom *reader)
{
    if (reader) {
        xx_msx_rom_destroy(reader);
        xx_mem_free(reader);
    }
}
bool xx_msx_rom_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_valid(format, pd);
}
bool xx_msx_rom_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_handle(format, pd);
}

bool xx_msx_rom_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *map, xx_pd_struct *pd)
{
    xx_msx_rom *reader = (xx_msx_rom *)format;
    int64_t file_offset;
    int64_t size;
    uint64_t address;
    if (!reader || !map || !format->base_info_handled || msx_stop(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_MAPS;
    if (mode != XX_MEMORY_MAP_MODE_MAPS) return false;
    if (reader->image_kind == XX_MSX_IMAGE_BINARY) {
        file_offset = format->base_address + reader->header_size;
        size = (int64_t)reader->end_address - reader->start_address + 1;
        address = reader->start_address;
    } else {
        file_offset = format->base_address;
        size = format->format_size;
        address = reader->start_address;
    }
    map->binary_offset = format->base_address;
    map->module_address = address;
    map->binary_size = format->format_size;
    map->entry_point_address = reader->init_address;
    map->code_base = (int64_t)address;
    map->start_load_offset = file_offset;
    map->file_type = format->file_type;
    map->format_type = format->format_type;
    map->endian = format->endian;
    map->arch = format->arch;
    map->mode = mode;
    return size > 0 && xx_memory_map_add_part(map, file_offset, size, address, size, XX_FILE_PART_REGION, 0, "ROM", false) && xx_memory_map_finalize(map);
}
