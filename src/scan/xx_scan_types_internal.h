/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_SCAN_TYPES_INTERNAL_H
#define XX_SCAN_TYPES_INTERNAL_H

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

static bool xx_scan_type_read_at(xx_io_device *device, int64_t offset, uint8_t *bytes, size_t size)
{
    size_t done = 0;
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t count = xx_io_read(device, bytes + done, size - done);
        if (count <= 0) return false;
        done += (size_t)count;
    }
    return true;
}

static uint16_t xx_scan_type_u16le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t xx_scan_type_u32le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t xx_scan_type_u32(const uint8_t *bytes, bool big_endian)
{
    return big_endian ? ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3] :
                        xx_scan_type_u32le(bytes);
}

static uint64_t xx_scan_type_u64(const uint8_t *bytes, bool big_endian)
{
    uint32_t first = xx_scan_type_u32(bytes, big_endian);
    uint32_t second = xx_scan_type_u32(bytes + 4, big_endian);
    return big_endian ? ((uint64_t)first << 32U) | second : ((uint64_t)second << 32U) | first;
}

static uint64_t xx_scan_type_u64le(const uint8_t *bytes)
{
    return (uint64_t)xx_scan_type_u32le(bytes) | ((uint64_t)xx_scan_type_u32le(bytes + 4) << 32U);
}

/* The set picker follows xx_fs_path_suffix's hidden-name rule; direct type
 * detection retains its older byte-suffix rule. */
static bool xx_scan_has_com_suffix(const char *file_name, bool leading_dot_is_suffix)
{
    char *suffix;
    const char *name;
    size_t length;
    if (!file_name) return false;
    if (!leading_dot_is_suffix) {
        bool result;
        suffix = xx_fs_path_suffix(file_name);
        if (!suffix) return false;
        result = xx_str_icmp(suffix, "COM") == 0;
        xx_str_free(suffix);
        return result;
    }
    name = file_name;
    while (*file_name) {
        if (*file_name == '/' || *file_name == '\\') name = file_name + 1;
        ++file_name;
    }
    length = strlen(name);
    if (length < 4U || name[length - 4U] != '.') return false;
    return (name[length - 3U] == 'c' || name[length - 3U] == 'C') && (name[length - 2U] == 'o' || name[length - 2U] == 'O') &&
           (name[length - 1U] == 'm' || name[length - 1U] == 'M');
}

xx_file_type_t xx_scan_type_detect_pe(xx_io_device *device, const uint8_t head[64], size_t head_size, int64_t size,
                                      bool *managed, xx_pd_struct *pd);
xx_file_type_t xx_scan_type_detect_msdos(xx_io_device *device);
xx_file_type_t xx_scan_type_detect_ne_le_lx(xx_io_device *device);
xx_file_type_t xx_scan_type_detect_elf(const uint8_t *head, size_t head_size);
bool xx_scan_type_is_macho_fat(xx_io_device *device, const uint8_t head[64], size_t head_size, int64_t file_size, xx_pd_struct *pd);
xx_file_type_t xx_scan_type_detect_macho(const uint8_t *head, size_t head_size);
xx_file_type_t xx_scan_type_detect_gzip(xx_io_device *device, const uint8_t *head, size_t head_size, xx_pd_struct *pd);
xx_file_type_t xx_scan_type_detect_zip(xx_io_device *device, int64_t size, const uint8_t *head, size_t head_size, xx_pd_struct *pd);
bool xx_scan_type_is_java_class(const uint8_t *head, size_t head_size);
xx_file_type_t xx_scan_type_detect_dex(const uint8_t *head, size_t head_size);
xx_file_type_t xx_scan_type_detect_pyc(const uint8_t *head, size_t head_size);
bool xx_scan_type_is_pdf(xx_io_device *device, int64_t file_size, xx_pd_struct *pd);
xx_file_type_t xx_scan_type_detect_image(const uint8_t *head, size_t head_size);
xx_file_type_t xx_scan_type_detect_amiga_atarist(xx_io_device *device, const uint8_t *head, size_t head_size);
bool xx_scan_type_is_iso9660(xx_io_device *device, int64_t size);

#endif /* XX_SCAN_TYPES_INTERNAL_H */
