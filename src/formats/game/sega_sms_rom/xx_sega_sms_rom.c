/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/game/sega_sms_rom/xx_sega_sms_rom.h"
#include "../../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool sms_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    static const uint32_t offsets[] = {UINT32_C(0x1ff0), UINT32_C(0x3ff0), UINT32_C(0x7ff0), UINT32_C(0x8ff0)};
    static const uint32_t sizes_kib[16] = {256, 512, 1024, 0, 0, 0, 0, 0, 0, 0, 8, 16, 32, 48, 64, 128};
    xx_sega_sms_rom *reader = (xx_sega_sms_rom *)format;
    uint8_t header[16];
    int64_t available = pm_available(format);
    size_t i;
    bool found = false;
    if (available < 8192 || available > UINT32_C(0x400000) || sms_stop(pd)) return false;
    for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        if ((uint64_t)offsets[i] + sizeof(header) > (uint64_t)available) continue;
        if (pm_read(format, offsets[i], header, sizeof(header)) && !xx_rt_memcmp(header, "TMR SEGA", 8)) {
            reader->header_offset = offsets[i];
            found = true;
            break;
        }
    }
    if (!found) return false;
    reader->checksum = xx_data_get_u16(header + 10, 2, 0, false);
    reader->product_code = (uint32_t)header[12] | ((uint32_t)header[13] << 8U) | ((uint32_t)(header[14] >> 4U) << 16U);
    reader->version = header[14] & 0x0fU;
    reader->region = header[15] >> 4U;
    reader->declared_rom_size = sizes_kib[header[15] & 0x0fU] * 1024U;
    if (!pm_add(format, stream, "rom.bin", 0, available)) return false;
    stream->size = available;
    return !sms_stop(pd);
}

void xx_sega_sms_rom_init(xx_sega_sms_rom *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, XX_FILE_TYPE_SEGA_SMS_ROM, "sms");
    reader->format.format_type = XX_TYPE_ROM;
    reader->format.os = XX_OS_SMS;
    reader->format.arch = XX_ARCH_Z80;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.is_executable = true;
    reader->format.get_memory_map = xx_sega_sms_rom_get_memory_map;
}
xx_sega_sms_rom *xx_sega_sms_rom_create(xx_io_device *device, int64_t base_address)
{
    xx_sega_sms_rom *reader = (xx_sega_sms_rom *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_sega_sms_rom_init(reader, device, base_address);
    return reader;
}
void xx_sega_sms_rom_destroy(xx_sega_sms_rom *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}
void xx_sega_sms_rom_free(xx_sega_sms_rom *reader)
{
    if (reader) {
        xx_sega_sms_rom_destroy(reader);
        xx_mem_free(reader);
    }
}
bool xx_sega_sms_rom_probe_device(xx_io_device *device, int64_t base_address)
{
    static const uint32_t offsets[] = {UINT32_C(0x1ff0), UINT32_C(0x3ff0), UINT32_C(0x7ff0), UINT32_C(0x8ff0)};
    uint8_t signature[8];
    int64_t total;
    int64_t available;
    size_t index;
    if (!device || base_address < 0) return false;
    total = xx_io_total_size(device);
    if (total < base_address) return false;
    available = total - base_address;
    if (available < 8192 || available > UINT32_C(0x400000)) return false;
    for (index = 0U; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        if ((int64_t)offsets[index] + (int64_t)sizeof(signature) > available) continue;
        if (xx_io_read_at(device, base_address + (int64_t)offsets[index], signature, sizeof(signature)) && xx_rt_memcmp(signature, "TMR SEGA", sizeof(signature)) == 0)
            return true;
    }
    return false;
}
bool xx_sega_sms_rom_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_valid(format, pd);
}
bool xx_sega_sms_rom_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_handle(format, pd);
}

bool xx_sega_sms_rom_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *map, xx_pd_struct *pd)
{
    if (!format || !map || !format->base_info_handled || sms_stop(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_MAPS;
    if (mode != XX_MEMORY_MAP_MODE_MAPS) return false;
    map->binary_offset = format->base_address;
    map->module_address = 0;
    map->binary_size = format->format_size;
    map->entry_point_address = 0;
    map->code_base = 0;
    map->start_load_offset = format->base_address;
    map->file_type = format->file_type;
    map->format_type = format->format_type;
    map->endian = format->endian;
    map->arch = format->arch;
    map->mode = mode;
    return xx_memory_map_add_part(map, format->base_address, format->format_size, 0, format->format_size, XX_FILE_PART_REGION, 0, "ROM", false) &&
           xx_memory_map_finalize(map);
}
