/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Zisofs v1 and zisofs2 paged-zlib members inside ISO 9660 Rock Ridge.
 */
#include "xx_iso_zisofs_native.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_ZF_POINTER_TABLE_LIMIT (16U * 1024U * 1024U)
static const uint8_t XX_ZF_MAGIC_V1[8] = {0x37U, 0xe4U, 0x53U, 0x96U, 0xc9U, 0xdbU, 0xd6U, 0x07U};
static const uint8_t XX_ZF_MAGIC_V2[8] = {0xefU, 0x22U, 0x55U, 0xa1U, 0xbcU, 0x1bU, 0x95U, 0xa0U};

static bool xx_zf_read_at(xx_io_device *source, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t done = 0U;
    if (!source || (!buffer && size) || offset < 0 || xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(source, buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_zf_write_all(xx_io_device *destination, const uint8_t *buffer, size_t size)
{
    size_t done = 0U;
    if (!destination || (!buffer && size)) return false;
    while (done < size) {
        ssize_t wrote = xx_io_write(destination, buffer + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

bool xx_iso_zisofs_parse_record(const uint8_t *record, size_t record_size, xx_iso_zisofs_info *info, bool *present)
{
    size_t system_use;
    bool found = false;
    xx_iso_zisofs_info found_info;
    if (present) *present = false;
    if (info) xx_mem_zero(info, sizeof(*info));
    if (!record || !info || !present || record_size < 34U || record[0] != record_size || (size_t)33U + record[32] > record_size) return false;
    system_use = 33U + record[32] + ((record[32] & 1U) == 0U ? 1U : 0U);
    if (system_use > record_size) return false;
    xx_mem_zero(&found_info, sizeof(found_info));
    while (system_use + 4U <= record_size && record[system_use] != 0U) {
        const uint8_t *entry = record + system_use;
        uint8_t size = entry[2];
        bool marker = (entry[0] == 'Z' && (entry[1] == 'F' || entry[1] == '2'));
        if (size < 4U || size > record_size - system_use) return false;
        if (marker) {
            uint8_t version = entry[3];
            if (found || size != 16U || (version != 1U && version != 2U) || (entry[1] == '2' && version != 2U)) return false;
            if (version == 1U) {
                if (entry[4] != 'p' || entry[5] != 'z' || entry[6] != 4U || entry[7] < 15U || entry[7] > 17U ||
                    xx_data_get_u32(entry + 8U, 4, 0, false) !=
                        (((uint32_t)entry[12] << 24U) | ((uint32_t)entry[13] << 16U) | ((uint32_t)entry[14] << 8U) | (uint32_t)entry[15]))
                    return false;
                found_info.uncompressed_size = xx_data_get_u32(entry + 8U, 4, 0, false);
            } else {
                if (entry[4] != 'P' || entry[5] != 'Z' || entry[6] != 6U || entry[7] < 15U || entry[7] > 20U) return false;
                found_info.uncompressed_size = xx_data_get_u64(entry + 8U, 8, 0, false);
            }
            found_info.version = version;
            found_info.algorithm = 1U;
            found_info.header_words = entry[6];
            found_info.block_shift = entry[7];
            found = true;
        }
        system_use += size;
    }
    if (found) {
        *info = found_info;
        *present = true;
    }
    return true;
}

bool xx_iso_zisofs_extract(xx_io_device *source, int64_t data_offset, uint32_t data_size, const xx_iso_zisofs_info *info, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t header[24];
    uint8_t *pointers = NULL, *packed = NULL, *plain = NULL;
    uint64_t blocks, block_size, position;
    size_t pointer_size, table_size, header_size, max_packed = 0U;
    int64_t saved_position, total;
    bool result = false;
    uint64_t i;
    if (!source || !destination || !info || data_offset < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    saved_position = xx_io_tell(source);
    total = xx_io_total_size(source);
    if (saved_position < 0 || total < data_offset || data_size > (uint64_t)(total - data_offset) || (info->version != 1U && info->version != 2U) ||
        info->algorithm != 1U || info->header_words != (info->version == 1U ? 4U : 6U) || info->block_shift < 15U ||
        info->block_shift > (info->version == 1U ? 17U : 20U) || info->uncompressed_size > INT64_MAX)
        return false;
    header_size = (size_t)info->header_words * 4U;
    pointer_size = info->version == 1U ? 4U : 8U;
    block_size = UINT64_C(1) << info->block_shift;
    blocks = info->uncompressed_size / block_size + ((info->uncompressed_size % block_size) != 0U);
    if (blocks >= XX_ZF_POINTER_TABLE_LIMIT / pointer_size || data_size < header_size || (blocks + 1U) * pointer_size > data_size - header_size) return false;
    table_size = (size_t)(blocks + 1U) * pointer_size;
    if (!xx_zf_read_at(source, data_offset, header, header_size)) goto done;
    if (info->version == 1U) {
        if (xx_mem_compare(header, XX_ZF_MAGIC_V1, 8U) != 0 || xx_data_get_u32(header + 8U, 4, 0, false) != info->uncompressed_size || header[12] != 4U ||
            header[13] != info->block_shift || header[14] != 0U || header[15] != 0U)
            goto done;
    } else {
        if (xx_mem_compare(header, XX_ZF_MAGIC_V2, 8U) != 0 || header[8] != 0U || header[9] != 6U || header[10] != 1U || header[11] != info->block_shift ||
            xx_data_get_u64(header + 12U, 8, 0, false) != info->uncompressed_size)
            goto done;
    }
    pointers = (uint8_t *)xx_mem_alloc(table_size);
    if (!pointers || !xx_zf_read_at(source, data_offset + (int64_t)header_size, pointers, table_size)) goto done;
    if ((info->version == 1U ? (uint64_t)xx_data_get_u32(pointers, 4, 0, false) : xx_data_get_u64(pointers, 8, 0, false)) != header_size + table_size) goto done;
    for (i = 0U; i < blocks; ++i) {
        uint64_t a = info->version == 1U ? (uint64_t)xx_data_get_u32(pointers + (size_t)i * pointer_size, 4, 0, false)
                                         : xx_data_get_u64(pointers + (size_t)i * pointer_size, 8, 0, false);
        uint64_t b = info->version == 1U ? (uint64_t)xx_data_get_u32(pointers + (size_t)(i + 1U) * pointer_size, 4, 0, false)
                                         : xx_data_get_u64(pointers + (size_t)(i + 1U) * pointer_size, 8, 0, false);
        if (a > b || b > data_size || b - a > block_size * 2U + 65536U) goto done;
        if (b - a > max_packed) max_packed = (size_t)(b - a);
    }
    if ((info->version == 1U ? (uint64_t)xx_data_get_u32(pointers + (size_t)blocks * pointer_size, 4, 0, false)
                             : xx_data_get_u64(pointers + (size_t)blocks * pointer_size, 8, 0, false)) != data_size)
        goto done;
    packed = (uint8_t *)xx_mem_alloc(max_packed ? max_packed : 1U);
    plain = (uint8_t *)xx_mem_alloc((size_t)block_size);
    if (!packed || !plain) goto done;
    position = 0U;
    for (i = 0U; i < blocks; ++i) {
        uint64_t a = info->version == 1U ? (uint64_t)xx_data_get_u32(pointers + (size_t)i * pointer_size, 4, 0, false)
                                         : xx_data_get_u64(pointers + (size_t)i * pointer_size, 8, 0, false);
        uint64_t b = info->version == 1U ? (uint64_t)xx_data_get_u32(pointers + (size_t)(i + 1U) * pointer_size, 4, 0, false)
                                         : xx_data_get_u64(pointers + (size_t)(i + 1U) * pointer_size, 8, 0, false);
        size_t wanted = (size_t)(info->uncompressed_size - position < block_size ? info->uncompressed_size - position : block_size);
        size_t written = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (a == b) {
            xx_mem_zero(plain, wanted);
        } else if (!xx_zf_read_at(source, data_offset + (int64_t)a, packed, (size_t)(b - a)) ||
                   !xx_zlib_stream_decode_memory(packed, (size_t)(b - a), plain, wanted, &written) || written != wanted ||
                   !xx_zlib_stream_trailer_matches(packed, (size_t)(b - a), plain, wanted)) {
            goto done;
        }
        if (!xx_zf_write_all(destination, plain, wanted)) goto done;
        position += wanted;
    }
    result = position == info->uncompressed_size;
done:
    if (plain) xx_mem_free(plain);
    if (packed) xx_mem_free(packed);
    if (pointers) xx_mem_free(pointers);
    if (xx_io_seek64(source, saved_position, SEEK_SET) != 0) result = false;
    return result;
}
