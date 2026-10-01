/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native PCE Block Image v0 translation, independently checked against the
 * original PCE image writer and block reader. No PCE code is linked here.
 */
#include "xxfclib/formats/pce_pbi/xx_pce_pbi.h"
#include "../xx_payload_members.h"
#include <string.h>

#ifdef PCE_PBI
#define PBI_FILE_TYPE XX_FILE_TYPE_PCE_PBI
#else
#define PBI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PBI_MAX_FILE (128U * 1024U * 1024U)
#define PBI_MAX_IMAGE (64U * 1024U * 1024U)

static uint64_t pbi_be64(const uint8_t *p) {
    return ((uint64_t)pm_be32(p) << 32U) | pm_be32(p + 4U);
}

static bool pbi_extent(uint64_t offset, uint64_t length, uint64_t end) {
    return offset <= end && length <= end - offset;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t header[48], entry[8];
    uint64_t available, file_size, image_size, l1_offset, l1_size, l2_size;
    uint64_t block_size, block_count, block;
    uint8_t l1_bits, l2_bits, block_bits;
    uint8_t *image;
    bool alternate = false;
    if (pm_available(format) < 48 || pm_available(format) > PBI_MAX_FILE ||
        (pd && xx_pd_is_stopped(pd)) ||
        !pm_read(format, 0, header, sizeof(header))) return false;
    available = (uint64_t)pm_available(format);
    if (memcmp(header, "PBIn", 4U) == 0) {
        uint8_t first_block_bits = header[14];
        uint64_t first_block_size;
        if (first_block_bits < 9U || first_block_bits > 24U) return false;
        first_block_size = UINT64_C(1) << first_block_bits;
        if (available < 3U * first_block_size ||
            !pm_read(format, (int64_t)(available - first_block_size),
                     header, sizeof(header)) ||
            header[14] != first_block_bits) return false;
        alternate = true;
    }
    if (memcmp(header, "PBI ", 4U) != 0 || pm_be32(header + 4U) != 0U ||
        pm_be32(header + 8U) < 48U || header[15] != 0U) return false;
    l1_bits = header[12];
    l2_bits = header[13];
    block_bits = header[14];
    if (l1_bits > 24U || l2_bits > 24U ||
        block_bits < 9U || block_bits > 24U) return false;
    block_size = UINT64_C(1) << block_bits;
    l1_size = (UINT64_C(1) << l1_bits) * 8U;
    l2_size = (UINT64_C(1) << l2_bits) * 8U;
    image_size = pbi_be64(header + 16U);
    l1_offset = pbi_be64(header + 24U);
    file_size = pbi_be64(header + 32U);
    if (image_size == 0U || image_size > PBI_MAX_IMAGE ||
        (image_size & 511U) != 0U || file_size > available ||
        file_size < block_size ||
        pm_be32(header + 8U) > block_size ||
        l1_offset < block_size || (l1_offset & (block_size - 1U)) != 0U ||
        !pbi_extent(l1_offset, l1_size, file_size) ||
        (alternate && l1_offset + l1_size > available - block_size))
        return false;
    block_count = (image_size + block_size - 1U) / block_size;
    if (block_count > (UINT64_C(1) << l1_bits) *
                      (UINT64_C(1) << l2_bits)) return false;

    image = (uint8_t *)xx_mem_alloc((size_t)image_size);
    if (!image) return false;
    xx_mem_zero(image, (size_t)image_size);
    for (block = 0U; block < block_count; ++block) {
        uint64_t l1_index = block >> l2_bits;
        uint64_t l2_index = block & ((UINT64_C(1) << l2_bits) - 1U);
        uint64_t l1_entry, l2_entry;
        uint64_t output_offset = block * block_size;
        size_t take = (size_t)((image_size - output_offset < block_size) ?
                               image_size - output_offset : block_size);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_read(format, (int64_t)(l1_offset + l1_index * 8U),
                     entry, sizeof(entry))) goto fail;
        l1_entry = pbi_be64(entry);
        if (l1_entry == 0U) continue;
        if ((l1_entry & (block_size - 1U)) != 0U ||
            l1_entry < block_size ||
            !pbi_extent(l1_entry, l2_size, file_size) ||
            (alternate && l1_entry + l2_size > available - block_size) ||
            !pm_read(format, (int64_t)(l1_entry + l2_index * 8U),
                     entry, sizeof(entry))) goto fail;
        l2_entry = pbi_be64(entry);
        if (l2_entry == 0U) continue;
        if (l2_entry & 2U) {
            size_t i;
            uint32_t fill = pm_be32(entry);
            if (pm_be32(entry + 4U) != 2U) goto fail;
            for (i = 0U; i < take; ++i)
                image[(size_t)output_offset + i] =
                    (uint8_t)(fill >> (24U - (unsigned)(i & 3U) * 8U));
        } else {
            if ((l2_entry & (block_size - 1U)) != 0U ||
                l2_entry < block_size ||
                !pbi_extent(l2_entry, block_size, file_size) ||
                (alternate && l2_entry + block_size > available - block_size) ||
                !pm_read(format, (int64_t)l2_entry,
                         image + (size_t)output_offset, take)) goto fail;
        }
    }
    if (!pm_add(format, stream, "disk.img", 0, 0)) goto fail;
    stream->items[0].memory = image;
    stream->items[0].offset = -1;
    stream->items[0].size = (int64_t)image_size;
    stream->items[0].packed_size = (int64_t)available;
    stream->size = (int64_t)available;
    return true;
fail:
    xx_mem_free(image);
    return false;
}

void xx_pce_pbi_init(xx_pce_pbi *reader, xx_io_device *device,
                     int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, PBI_FILE_TYPE, "pbi");
}

xx_pce_pbi *xx_pce_pbi_create(xx_io_device *device, int64_t base_address) {
    xx_pce_pbi *reader = (xx_pce_pbi *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_pbi_init(reader, device, base_address);
    return reader;
}

void xx_pce_pbi_destroy(xx_pce_pbi *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_pbi_free(xx_pce_pbi *reader) {
    if (reader) {
        xx_pce_pbi_destroy(reader);
        xx_mem_free(reader);
    }
}
