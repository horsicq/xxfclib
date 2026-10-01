/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Container framing follows https://tukaani.org/xz/xz-file-format.txt. */
#include "xxfclib/formats/xz/xx_xz.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xx_xz_defs.h"
#include <limits.h>

static void xz_writer_le(uint8_t *output, uint64_t value, size_t bytes)
{
    size_t index;
    for (index = 0; index < bytes; ++index)
        output[index] = (uint8_t)(value >> (index * 8));
}

static size_t xz_writer_vli(uint8_t *output, uint64_t value)
{
    size_t count = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7fu);
        value >>= 7;
        output[count++] = (uint8_t)(byte | (value ? 0x80u : 0u));
    } while (value);
    return count;
}

static bool xz_writer_write(xx_io_device *destination, const uint8_t *data,
                              size_t size, xx_pd_struct *pd)
{
    size_t done = 0;
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

bool xx_xz_pack_to_device(xx_io_device *source, int64_t source_offset,
                           int64_t uncompressed_size,
                           xx_io_device *destination, int level,
                           xx_pd_struct *pd)
{
    uint8_t stream_header[12] = {0xFD, 0x37, 0x7A, 0x58, 0x5A, 0, 0, XX_XZ_CHECK_CRC64};
    uint8_t block_header[12] = {2, 0, XX_XZ_FILTER_LZMA2, 1};
    uint8_t index[32] = {0, 1};
    uint8_t footer[12] = {0};
    uint8_t check[8];
    static const uint8_t padding[3] = {0, 0, 0};
    uint8_t property, actual_property;
    uint64_t crc64;
    int64_t compressed_size, source_size;
    size_t padding_size, index_size = 2;

    if (!source || !destination || source == destination || source_offset < 0 ||
        uncompressed_size < 0 || (uint64_t)uncompressed_size > (uint64_t)SIZE_MAX ||
        (pd && xx_pd_is_stopped(pd)) || !xx_lzma2_get_properties(&property))
        return false;
    source_size = xx_io_total_size(source);
    if (source_size < source_offset || uncompressed_size > source_size - source_offset ||
        xx_io_seek64(source, source_offset, SEEK_SET) != 0) return false;

    xz_writer_le(stream_header + 8, xx_crc32_calc(0, stream_header + 6, 2), 4);
    block_header[4] = property;
    xz_writer_le(block_header + 8, xx_crc32_calc(0, block_header, 8), 4);
    if (!xz_writer_write(destination, stream_header, sizeof(stream_header), pd) ||
        !xz_writer_write(destination, block_header, sizeof(block_header), pd) ||
        !xx_lzma2_pack_device_with_crc64(source, source_offset, uncompressed_size,
                                         destination, level, &actual_property,
                                         &compressed_size, &crc64, pd) ||
        actual_property != property || compressed_size <= 0 ||
        compressed_size > INT64_MAX - 128) return false;

    padding_size = (size_t)((4u - ((uint64_t)compressed_size & 3u)) & 3u);
    xz_writer_le(check, crc64, sizeof(check));
    if (!xz_writer_write(destination, padding, padding_size, pd) ||
        !xz_writer_write(destination, check, sizeof(check), pd)) return false;

    /* Unpadded block size includes its header and check, but no block padding. */
    index_size += xz_writer_vli(index + index_size,
                                (uint64_t)compressed_size + sizeof(block_header) + sizeof(check));
    index_size += xz_writer_vli(index + index_size, (uint64_t)uncompressed_size);
    while (index_size & 3u) index[index_size++] = 0;
    xz_writer_le(index + index_size, xx_crc32_calc(0, index, index_size), 4);
    index_size += 4;

    xz_writer_le(footer + 4, (uint64_t)(index_size / 4 - 1), 4);
    footer[8] = 0;
    footer[9] = XX_XZ_CHECK_CRC64;
    footer[10] = 0x59;
    footer[11] = 0x5A;
    xz_writer_le(footer, xx_crc32_calc(0, footer + 4, 6), 4);
    return xz_writer_write(destination, index, index_size, pd) &&
           xz_writer_write(destination, footer, sizeof(footer), pd);
}
