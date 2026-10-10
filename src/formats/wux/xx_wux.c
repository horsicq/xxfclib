/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native reader based on the WUX layout, not imported project code.
 * Primary producer/consumer layout:
 * https://github.com/cemu-project/Cemu/blob/main/src/Cafe/Filesystem/WUD/wud.h
 * https://github.com/cemu-project/Cemu/blob/main/src/Cafe/Filesystem/WUD/wud.cpp
 */
#include "xxfclib/formats/wux/xx_wux.h"
#include "xx_disk_containers_native.h"
#include "xxfclib/data/xx_data.h"

#ifdef WUX
#define DC_FILE_TYPE XX_FILE_TYPE_WUX
#else
#define DC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool dc_parse(Abstractformat *f, dc_image *image, const xx_list_s *options, xx_pd_struct *pd)
{
    uint8_t header[32], entries[4096];
    uint64_t size, count, table_end, extent;
    uint32_t i;
    if (!dc_read(f, image, 0U, header, sizeof(header), pd) || xx_rt_memcmp(header, "WUX0", 4U) || xx_data_get_u32(header + 4U, 4, 0, false) != UINT32_C(0x1099d02e))
        return false;
    image->block_size = xx_data_get_u32(header + 8U, 4, 0, false);
    size = xx_data_get_u64(header + 16U, 8, 0, false);
    if (image->block_size < 256U || image->block_size >= UINT32_C(0x10000000) || image->block_size % 256U || size == 0U || size > DC_MAX_IMAGE_SIZE ||
        xx_data_get_u32(header + 24U, 4, 0, false) != 0U)
        return false;
    count = size / image->block_size + (size % image->block_size != 0U);
    if (count > DC_MAX_MAP_ENTRIES || !dc_memory_limit(f, options, sizeof(*image) + count * sizeof(uint32_t))) return false;
    table_end = 32U + count * 4U;
    image->data_offset = table_end + image->block_size - 1U;
    image->data_offset -= image->data_offset % image->block_size;
    if (!dc_span(32U, count * 4U, image->available) || image->data_offset > image->available) return false;
    image->map_count = (uint32_t)count;
    image->map = (uint32_t *)xx_mem_alloc((size_t)count * sizeof(uint32_t));
    if (!image->map) return false;
    extent = image->data_offset;
    for (i = 0U; i < image->map_count;) {
        uint32_t j, amount = image->map_count - i;
        if (amount > sizeof(entries) / 4U) amount = sizeof(entries) / 4U;
        if (!dc_read(f, image, 32U + (uint64_t)i * 4U, entries, (size_t)amount * 4U, pd)) return false;
        for (j = 0U; j < amount; ++j) {
            uint32_t mapped = xx_data_get_u32(entries + (size_t)j * 4U, 4, 0, false);
            uint64_t at = image->data_offset + (uint64_t)mapped * image->block_size;
            if (dc_stopped(pd) || !dc_span(at, image->block_size, image->available)) return false;
            image->map[i + j] = mapped;
            if (at + image->block_size > extent) extent = at + image->block_size;
        }
        i += amount;
    }
    image->extent = extent;
    return dc_add(image, "disk.wud", image->data_offset, size, extent - image->data_offset, DC_WUX_MAP, 0U, table_end);
}

XX_DC_IMPLEMENT(wux, DC_FILE_TYPE, "wux", "application/x-wux")
