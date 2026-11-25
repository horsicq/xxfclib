/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/game/tic80/xx_tic80.h"
#include "../../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool tic_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static const char *tic_name(uint8_t type)
{
    switch (type) {
        case 1: return "tiles";
        case 2: return "sprites";
        case 3: return "cover";
        case 4: return "map";
        case 5: return "code";
        case 6: return "flags";
        case 9: return "samples";
        case 10: return "waveform";
        case 12: return "palette";
        case 13: return "patterns";
        case 14: return "music";
        case 15: return "patterns";
        case 16: return "zip";
        case 17: return "default";
        case 18: return "screen";
        default: return NULL;
    }
}
static uint32_t tic_address(uint8_t type, uint32_t payload_offset)
{
    switch (type) {
        case 1: return UINT32_C(0x4000);
        case 2: return UINT32_C(0x6000);
        case 4: return UINT32_C(0x8000);
        case 6: return UINT32_C(0x14404);
        case 9: return UINT32_C(0x100e4);
        case 10: return UINT32_C(0xffe4);
        case 12: return UINT32_C(0x3fc0);
        case 14: return UINT32_C(0x13e64);
        case 15: return UINT32_C(0x11164);
        default: return payload_offset;
    }
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    xx_tic80 *reader = (xx_tic80 *)format;
    xx_tic80_chunk *chunks;
    uint64_t offset = 0;
    uint32_t count = 0;
    int64_t available = pm_available(format);
    size_t capacity;
    if (available <= 255 || available > 10 * 1024 * 1024 || tic_stop(pd)) return false;
    capacity = (size_t)(((uint64_t)available + 3U) / 4U);
    if (capacity > XX_TIC80_MAX_CHUNKS) capacity = XX_TIC80_MAX_CHUNKS;
    chunks = (xx_tic80_chunk *)xx_mem_alloc(capacity * sizeof(*chunks));
    if (!chunks) return false;
    xx_mem_zero(chunks, capacity * sizeof(*chunks));
    while (offset < (uint64_t)available) {
        uint8_t header[4];
        uint8_t type, bank;
        uint16_t size;
        const char *name;
        xx_tic80_chunk *chunk;
        if (count >= capacity || tic_stop(pd) || (uint64_t)available - offset < 4U || !pm_read(format, (int64_t)offset, header, sizeof(header))) goto fail;
        bank = header[0] >> 5U;
        type = header[0] & 0x1fU;
        size = xx_data_get_u16(header + 1, 2, 0, false);
        name = tic_name(type);
        offset += 4U;
        if (!name || size > (uint64_t)available - offset) goto fail;
        chunk = &chunks[count];
        chunk->bank = bank;
        chunk->type = type;
        chunk->size = size;
        chunk->offset = (uint32_t)offset;
        chunk->address = tic_address(type, (uint32_t)offset);
        (void)xx_rt_snprintf(chunk->name, sizeof(chunk->name), "%s.%u", name, (unsigned)bank);
        if (!pm_add(format, stream, chunk->name, (int64_t)offset, size)) goto fail;
        ++count;
        offset += size;
    }
    if (!count || offset != (uint64_t)available) goto fail;
    if (reader->chunks) xx_mem_free(reader->chunks);
    reader->chunks = chunks;
    reader->chunk_count = count;
    stream->size = available;
    return true;
fail:
    xx_mem_free(chunks);
    return false;
}

void xx_tic80_init(xx_tic80 *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, XX_FILE_TYPE_TIC80, "tic80");
    reader->format.format_type = XX_TYPE_ROM;
    reader->format.os = XX_OS_GENERIC;
    reader->format.arch = XX_ARCH_Z80;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.is_executable = true;
    reader->format.get_memory_map = xx_tic80_get_memory_map;
}
xx_tic80 *xx_tic80_create(xx_io_device *device, int64_t base_address)
{
    xx_tic80 *reader = (xx_tic80 *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_tic80_init(reader, device, base_address);
    return reader;
}
void xx_tic80_destroy(xx_tic80 *reader)
{
    if (!reader) return;
    if (reader->chunks) xx_mem_free(reader->chunks);
    reader->chunks = NULL;
    reader->chunk_count = 0;
    xx_format_cleanup_extra_parameters(&reader->format);
}
void xx_tic80_free(xx_tic80 *reader)
{
    if (reader) {
        xx_tic80_destroy(reader);
        xx_mem_free(reader);
    }
}
bool xx_tic80_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_valid(format, pd);
}
bool xx_tic80_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_handle(format, pd);
}
uint32_t xx_tic80_get_chunk_count(const xx_tic80 *reader)
{
    return reader && reader->format.base_info_handled ? reader->chunk_count : 0;
}
const xx_tic80_chunk *xx_tic80_get_chunk(const xx_tic80 *reader, uint32_t index)
{
    return reader && reader->format.base_info_handled && index < reader->chunk_count ? &reader->chunks[index] : NULL;
}

bool xx_tic80_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *map, xx_pd_struct *pd)
{
    xx_tic80 *reader = (xx_tic80 *)format;
    uint32_t i;
    if (!reader || !map || !format->base_info_handled || tic_stop(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_MAPS;
    if (mode != XX_MEMORY_MAP_MODE_MAPS) return false;
    map->binary_offset = format->base_address;
    map->module_address = 0;
    map->binary_size = format->format_size;
    map->entry_point_address = UINT32_C(0xffff0);
    map->code_base = -1;
    map->start_load_offset = format->base_address;
    map->file_type = format->file_type;
    map->format_type = format->format_type;
    map->endian = format->endian;
    map->arch = format->arch;
    map->mode = mode;
    for (i = 0; i < reader->chunk_count; ++i) {
        const xx_tic80_chunk *chunk = &reader->chunks[i];
        uint64_t address = chunk->address + (uint64_t)chunk->bank * UINT64_C(0x20000);
        if (chunk->size &&
            !xx_memory_map_add_part(map, format->base_address + chunk->offset, chunk->size, address, chunk->size, XX_FILE_PART_SECTION, (int32_t)i, chunk->name, false))
            return false;
    }
    return xx_memory_map_finalize(map);
}
