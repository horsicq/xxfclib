/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"


static bool xx_scan_is_macho_magic(const uint8_t magic[4])
{
    return (!memcmp(magic, "\xFE\xED\xFA\xCE", 4) || !memcmp(magic, "\xCE\xFA\xED\xFE", 4) || !memcmp(magic, "\xFE\xED\xFA\xCF", 4) ||
            !memcmp(magic, "\xCF\xFA\xED\xFE", 4));
}

/* A valid fat table, including every slice, distinguishes CAFEBABE from a
 * Java class header. Both 32-bit and 64-bit fat tables are supported. */
bool xx_scan_type_is_macho_fat(xx_io_device *device, const uint8_t head[64], size_t head_size, int64_t file_size, xx_pd_struct *pd)
{
    bool big_endian;
    bool is_64;
    uint32_t count;
    uint64_t table_end;
    uint32_t index;
    size_t record_size;
    if (head_size < 8 || file_size < 8) return false;
    if (!memcmp(head, "\xCA\xFE\xBA\xBE", 4)) {
        big_endian = true;
        is_64 = false;
    } else if (!memcmp(head, "\xBE\xBA\xFE\xCA", 4)) {
        big_endian = false;
        is_64 = false;
    } else if (!memcmp(head, "\xCA\xFE\xBA\xBF", 4)) {
        big_endian = true;
        is_64 = true;
    } else if (!memcmp(head, "\xBF\xBA\xFE\xCA", 4)) {
        big_endian = false;
        is_64 = true;
    } else {
        return false;
    }
    count = xx_scan_type_u32(head + 4, big_endian);
    record_size = is_64 ? 32U : 20U;
    if (!count || count > 4096U || (uint64_t)count > ((uint64_t)file_size - 8U) / record_size) return false;
    table_end = 8U + (uint64_t)count * record_size;
    for (index = 0; index < count; ++index) {
        uint8_t record[32];
        uint8_t slice_magic[4];
        uint64_t offset;
        uint64_t size;
        uint32_t align;
        uint64_t mask;
        if (pd && pd->is_stop) return false;
        if (!xx_scan_type_read_at(device, (int64_t)(8U + (uint64_t)index * record_size), record, record_size)) return false;
        offset = is_64 ? xx_scan_type_u64(record + 8, big_endian) : xx_scan_type_u32(record + 8, big_endian);
        size = is_64 ? xx_scan_type_u64(record + 16, big_endian) : xx_scan_type_u32(record + 12, big_endian);
        align = xx_scan_type_u32(record + (is_64 ? 24 : 16), big_endian);
        if (!xx_scan_type_u32(record, big_endian) || !size || align > 63U || offset < table_end || offset > (uint64_t)file_size || size > (uint64_t)file_size - offset ||
            size < 4U)
            return false;
        mask = align ? (UINT64_C(1) << align) - 1U : 0U;
        if ((offset & mask) != 0U || !xx_scan_type_read_at(device, (int64_t)offset, slice_magic, 4U) || !xx_scan_is_macho_magic(slice_magic)) return false;
    }
    return true;
}


xx_file_type_t xx_scan_type_detect_macho(const uint8_t *head, size_t head_size)
{
    if (head_size < 28U || !xx_scan_is_macho_magic(head)) return XX_FILE_TYPE_UNKNOWN;
    if (head[3] == 0xCEU || head[0] == 0xCEU) return XX_FILE_TYPE_MACHO32;
    return head_size >= 32U ? XX_FILE_TYPE_MACHO64 : XX_FILE_TYPE_UNKNOWN;
}

