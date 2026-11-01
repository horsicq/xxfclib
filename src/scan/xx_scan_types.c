/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/executable/atarist/xx_atarist.h"
#include "xxfclib/formats/executable/dos16m/xx_dos16m.h"
#include "xxfclib/formats/installer/apk/xx_apk.h"
#include "xxfclib/formats/installer/ipa/xx_ipa.h"
#include "xxfclib/formats/executable/jar/xx_jar.h"
#include "xxfclib/formats/installer/npm/xx_npm.h"
#include "xxfclib/formats/executable/pyc/xx_pyc.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet.h"
#include "xxfclib/formats/archive/lha/xx_lha.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xxfclib/formats/archive/bz2/xx_bz2.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <stdio.h>
#include <string.h>

typedef struct xx_scan_type_window {
    xx_io_device view;
    xx_io_device *source;
    int64_t base;
    int64_t length; /* -1 when the source size is unknown. */
    int64_t position;
} xx_scan_type_window;

static ssize_t xx_scan_window_read(xx_io_device *view, void *buffer, size_t size)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    ssize_t count;
    if (!window || (!buffer && size)) return -1;
    if (!size || (window->length >= 0 && window->position >= window->length)) return 0;
    if (size > (size_t)PTRDIFF_MAX) size = (size_t)PTRDIFF_MAX;
    if (window->length < 0) {
        int64_t remaining = INT64_MAX - window->base - window->position;
        if (remaining <= 0) return 0;
        if ((uint64_t)size > (uint64_t)remaining) size = (size_t)remaining;
    }
    if (window->length >= 0 && (uint64_t)size > (uint64_t)(window->length - window->position)) size = (size_t)(window->length - window->position);
    if (xx_io_seek64(window->source, window->base + window->position, SEEK_SET) != 0) return -1;
    count = xx_io_read(window->source, buffer, size);
    if (count > 0 && (uint64_t)count > (uint64_t)size) return -1;
    if (count > 0) window->position += count;
    return count;
}

static int xx_scan_window_seek64(xx_io_device *view, int64_t offset, int whence)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    int64_t origin;
    int64_t maximum;
    if (!window) return -1;
    maximum = window->length >= 0 ? window->length : INT64_MAX - window->base;
    if (whence == SEEK_SET) origin = 0;
    else if (whence == SEEK_CUR) origin = window->position;
    else if (whence == SEEK_END && window->length >= 0) origin = window->length;
    else return -1;
    if (offset < -origin || offset > maximum - origin) return -1;
    window->position = origin + offset;
    return 0;
}

static int xx_scan_window_seek(xx_io_device *view, long offset, int whence)
{
    return xx_scan_window_seek64(view, (int64_t)offset, whence);
}

static int64_t xx_scan_window_size(xx_io_device *view)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    return window ? window->length : -1;
}

static int64_t xx_scan_window_tell(xx_io_device *view)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    return window ? window->position : -1;
}

static bool xx_scan_read_at(xx_io_device *device, int64_t offset, uint8_t *bytes, size_t size)
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

static size_t xx_scan_read_head(xx_io_device *device, uint8_t head[64])
{
    size_t done = 0;
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) return 0;
    while (done < 64U) {
        ssize_t count = xx_io_read(device, head + done, 64U - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    return done;
}

static uint32_t xx_scan_u32le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t xx_scan_u32(const uint8_t *bytes, bool big_endian)
{
    return big_endian ? ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3] : xx_scan_u32le(bytes);
}

static uint64_t xx_scan_u64(const uint8_t *bytes, bool big_endian)
{
    uint32_t first = xx_scan_u32(bytes, big_endian);
    uint32_t second = xx_scan_u32(bytes + 4, big_endian);
    return big_endian ? ((uint64_t)first << 32U) | second : ((uint64_t)second << 32U) | first;
}

static bool xx_scan_is_macho_magic(const uint8_t magic[4])
{
    return (!memcmp(magic, "\xFE\xED\xFA\xCE", 4) || !memcmp(magic, "\xCE\xFA\xED\xFE", 4) || !memcmp(magic, "\xFE\xED\xFA\xCF", 4) ||
            !memcmp(magic, "\xCF\xFA\xED\xFE", 4));
}

/* A valid fat table, including every slice, distinguishes CAFEBABE from a
 * Java class header. Both 32-bit and 64-bit fat tables are supported. */
static bool xx_scan_is_macho_fat(xx_io_device *device, const uint8_t head[64], size_t head_size, int64_t file_size, xx_pd_struct *pd)
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
    count = xx_scan_u32(head + 4, big_endian);
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
        if (!xx_scan_read_at(device, (int64_t)(8U + (uint64_t)index * record_size), record, record_size)) return false;
        offset = is_64 ? xx_scan_u64(record + 8, big_endian) : xx_scan_u32(record + 8, big_endian);
        size = is_64 ? xx_scan_u64(record + 16, big_endian) : xx_scan_u32(record + 12, big_endian);
        align = xx_scan_u32(record + (is_64 ? 24 : 16), big_endian);
        if (!xx_scan_u32(record, big_endian) || !size || align > 63U || offset < table_end || offset > (uint64_t)file_size || size > (uint64_t)file_size - offset ||
            size < 4U)
            return false;
        mask = align ? (UINT64_C(1) << align) - 1U : 0U;
        if ((offset & mask) != 0U || !xx_scan_read_at(device, (int64_t)offset, slice_magic, 4U) || !xx_scan_is_macho_magic(slice_magic)) return false;
    }
    return true;
}

static bool xx_scan_has_com_suffix(const char *file_name)
{
    size_t length;
    if (!file_name) return false;
    length = strlen(file_name);
    if (length < 4U || file_name[length - 4U] != '.') return false;
    return (file_name[length - 3U] == 'c' || file_name[length - 3U] == 'C') && (file_name[length - 2U] == 'o' || file_name[length - 2U] == 'O') &&
           (file_name[length - 1U] == 'm' || file_name[length - 1U] == 'M');
}

/* PDF scan detection accepts a header after a BOM or other leading bytes when the
 * complete "%PDF-" marker begins within the first 1024 bytes. Its byte-zero
 * fast path also accepts "%PDF" with any fifth byte. The scan window already
 * limits reads to options.offset/size. */
static int64_t xx_scan_pdf_header_offset(xx_io_device *device, int64_t file_size, xx_pd_struct *pd)
{
    uint8_t bytes[1029];
    size_t limit;
    size_t done = 0;
    size_t index;
    if (file_size >= 0 && file_size <= 4) return -1;
    limit = file_size >= 0 && file_size < (int64_t)sizeof(bytes) ? (size_t)file_size : sizeof(bytes);
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) return -1;
    while (done < limit) {
        ssize_t count;
        if (pd && pd->is_stop) return -1;
        count = xx_io_read(device, bytes + done, limit - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    if (done < 5U) return -1;
    if (!memcmp(bytes, "%PDF", 4U)) return 0;
    for (index = 1U; index <= 1024U && index + 5U <= done; ++index) {
        if (!memcmp(bytes + index, "%PDF-", 5U)) return (int64_t)index;
    }
    return -1;
}

/* XZip::isValid, the check XFormats types a ZIP by: an end record whose whole
 * central directory agrees with every local header (XZip::findECDOffset), or,
 * without one, a bare stream of local headers with no end record after it.
 * A damaged local header, overlapping or shared member data, or ZIP64 or
 * multi-disk end records make the input no ZIP for DIE. */
#define XX_SCAN_ZIP_LFH 0x04034b50U
#define XX_SCAN_ZIP_CFH 0x02014b50U
#define XX_SCAN_ZIP_ECD 0x06054b50U

typedef struct xx_scan_zip_range {
    int64_t start;
    int64_t end;
} xx_scan_zip_range;

static uint16_t xx_scan_u16le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint64_t xx_scan_u64le(const uint8_t *bytes)
{
    return (uint64_t)xx_scan_u32le(bytes) | ((uint64_t)xx_scan_u32le(bytes + 4) << 32U);
}

static bool xx_scan_zip_read(xx_io_device *device, int64_t size, int64_t offset, uint8_t *bytes, size_t length)
{
    if (offset < 0 || offset > size || (int64_t)length > size - offset) return false;
    return !length || xx_scan_read_at(device, offset, bytes, length);
}

/* decodeZipUtf8Strict: a name flagged UTF-8 must be well-formed UTF-8. */
static bool xx_scan_zip_utf8(const uint8_t *data, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        uint8_t first = data[offset++];
        uint32_t code, minimum;
        size_t count, index;
        if (first < 0x80U) continue;
        if ((first & 0xE0U) == 0xC0U) {
            code = first & 0x1FU;
            count = 1;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            code = first & 0x0FU;
            count = 2;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            code = first & 0x07U;
            count = 3;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (count > size - offset) return false;
        for (index = 0; index < count; ++index) {
            uint8_t next = data[offset++];
            if ((next & 0xC0U) != 0x80U) return false;
            code = (code << 6U) | (next & 0x3FU);
        }
        if (code < minimum || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) return false;
    }
    return true;
}

static int xx_scan_zip_range_compare(const void *left, const void *right)
{
    int64_t a = ((const xx_scan_zip_range *)left)->start, b = ((const xx_scan_zip_range *)right)->start;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* One XZip::findECDOffset candidate. scratch holds 3 x 65536 bytes. */
static bool xx_scan_zip_directory(xx_io_device *device, int64_t size, int64_t eocd, const uint8_t *ecd, uint8_t *scratch, xx_pd_struct *pd)
{
    uint8_t *local_name = scratch, *central_name = scratch + 65536, *extra = scratch + 131072;
    uint16_t total = xx_scan_u16le(ecd + 10);
    uint32_t directory_size = xx_scan_u32le(ecd + 12), directory_offset = xx_scan_u32le(ecd + 16);
    int64_t directory = (int64_t)directory_offset, current;
    xx_scan_zip_range *ranges;
    uint32_t count = 0, index;
    bool valid = true;
    if (xx_scan_u16le(ecd + 4) || xx_scan_u16le(ecd + 6) || xx_scan_u16le(ecd + 8) != total || total == 0xFFFFU || directory_size == 0xFFFFFFFFU ||
        directory_offset == 0xFFFFFFFFU)
        return false;
    if (!total) return !directory_size && directory == eocd;
    if (directory > eocd || (int64_t)directory_size != eocd - directory) return false;
    ranges = (xx_scan_zip_range *)xx_mem_alloc((size_t)total * sizeof(*ranges));
    if (!ranges) return false;
    current = directory;
    for (index = 0; index < total && valid; ++index) {
        uint8_t central[46], local[30], descriptor[24];
        uint16_t central_flags, method, name_length, local_flags, local_name_length, local_extra_length;
        uint32_t crc, compressed, uncompressed, local_crc, local_compressed, local_uncompressed;
        int64_t record_size, header, data, end;
        bool local_zip64, flags_ok;
        size_t position;
        valid = false;
        if (pd && pd->is_stop) break;
        if (eocd - current < 46 || !xx_scan_zip_read(device, size, current, central, 46) || xx_scan_u32le(central) != XX_SCAN_ZIP_CFH) break;
        central_flags = xx_scan_u16le(central + 8);
        method = xx_scan_u16le(central + 10);
        crc = xx_scan_u32le(central + 16);
        compressed = xx_scan_u32le(central + 20);
        uncompressed = xx_scan_u32le(central + 24);
        name_length = xx_scan_u16le(central + 28);
        record_size = 46 + (int64_t)name_length + xx_scan_u16le(central + 30) + xx_scan_u16le(central + 32);
        if (xx_scan_u16le(central + 34) > 1U || compressed == 0xFFFFFFFFU || uncompressed == 0xFFFFFFFFU || xx_scan_u32le(central + 42) == 0xFFFFFFFFU ||
            (method == 0U && !(central_flags & 1U) && compressed != uncompressed) || record_size > eocd - current)
            break;
        if (!xx_scan_zip_read(device, size, current + 46, central_name, name_length) ||
            ((central_flags & 0x0800U) && !xx_scan_zip_utf8(central_name, name_length)))
            break;
        header = (int64_t)xx_scan_u32le(central + 42);
        if (directory - header < 30 || !xx_scan_zip_read(device, size, header, local, 30)) break;
        local_flags = xx_scan_u16le(local + 6);
        local_crc = xx_scan_u32le(local + 14);
        local_compressed = xx_scan_u32le(local + 18);
        local_uncompressed = xx_scan_u32le(local + 22);
        local_name_length = xx_scan_u16le(local + 26);
        local_extra_length = xx_scan_u16le(local + 28);
        data = header + 30 + (int64_t)local_name_length + local_extra_length;
        if (!xx_scan_zip_read(device, size, header + 30, local_name, local_name_length)) break;
        flags_ok = local_flags == central_flags || (xx_scan_u16le(local + 8) == 8U && ((local_flags ^ central_flags) & 0x7FF9U) == 0U); /* ~0x8006 */
        if (xx_scan_u32le(local) != XX_SCAN_ZIP_LFH || local[4] != central[6] || local[5] != central[7] || !flags_ok || xx_scan_u16le(local + 8) != method ||
            local_name_length != name_length || data > directory || (int64_t)compressed > directory - data ||
            (!(local_flags & 8U) && (local_crc != crc || local_compressed != compressed || local_uncompressed != uncompressed)))
            break;
        /* Names agree byte for byte, except where both carry a codepage byte. */
        for (position = 0; position < name_length; ++position)
            if (local_name[position] != central_name[position] && (local_name[position] < 0x80U || central_name[position] < 0x80U)) break;
        if (position != name_length) break;
        local_zip64 = local_compressed == 0xFFFFFFFFU || local_uncompressed == 0xFFFFFFFFU;
        if (local_zip64) {
            bool found = false, sizes_ok = true;
            if (local[4] < 45U || !(local_flags & 8U) || !xx_scan_zip_read(device, size, data - local_extra_length, extra, local_extra_length)) break;
            position = 0;
            while (position < local_extra_length) {
                uint16_t tag, length;
                if (local_extra_length - position < 4) {
                    sizes_ok = false;
                    break;
                }
                tag = xx_scan_u16le(extra + position);
                length = xx_scan_u16le(extra + position + 2);
                position += 4;
                if (length > local_extra_length - position) {
                    sizes_ok = false;
                    break;
                }
                if (tag == 1U) {
                    bool missing_uncompressed = local_uncompressed == 0xFFFFFFFFU, missing_compressed = local_compressed == 0xFFFFFFFFU;
                    bool pair = length == 16U, single = length == 8U && missing_uncompressed != missing_compressed;
                    size_t value = position;
                    if (found || (!pair && !single)) {
                        sizes_ok = false;
                        break;
                    }
                    found = true;
                    if (pair || missing_uncompressed) {
                        uint64_t field = xx_scan_u64le(extra + value);
                        if (field && field != uncompressed) sizes_ok = false;
                        value += 8;
                    }
                    if (pair || missing_compressed) {
                        uint64_t field = xx_scan_u64le(extra + value);
                        if (field && field != compressed) sizes_ok = false;
                    }
                }
                position += length;
            }
            if (!sizes_ok || !found) break;
        }
        end = data + (int64_t)compressed;
        if (local_flags & 8U) {
            /* Bit 3: the data descriptor, not the local header, holds the values. */
            int64_t available = directory - end, length = local_zip64 ? 24 : 16, descriptor_size = 0;
            if ((local_crc && local_crc != crc) || (local_compressed && local_compressed != 0xFFFFFFFFU && local_compressed != compressed) ||
                (local_uncompressed && local_uncompressed != 0xFFFFFFFFU && local_uncompressed != uncompressed))
                break;
            if (available < length) length = available < 0 ? 0 : available;
            if (length >= 12 && !xx_scan_zip_read(device, size, end, descriptor, (size_t)length)) break;
            if (local_zip64) {
                if (length >= 24 && xx_scan_u32le(descriptor) == 0x08074b50U && xx_scan_u32le(descriptor + 4) == crc &&
                    xx_scan_u64le(descriptor + 8) == compressed && xx_scan_u64le(descriptor + 16) == uncompressed)
                    descriptor_size = 24;
                if (!descriptor_size && length >= 20 && xx_scan_u32le(descriptor) == crc && xx_scan_u64le(descriptor + 4) == compressed &&
                    xx_scan_u64le(descriptor + 12) == uncompressed)
                    descriptor_size = 20;
            } else {
                if (length >= 16 && xx_scan_u32le(descriptor) == 0x08074b50U && xx_scan_u32le(descriptor + 4) == crc &&
                    xx_scan_u32le(descriptor + 8) == compressed && xx_scan_u32le(descriptor + 12) == uncompressed)
                    descriptor_size = 16;
                if (!descriptor_size && length >= 12 && xx_scan_u32le(descriptor) == crc && xx_scan_u32le(descriptor + 4) == compressed &&
                    xx_scan_u32le(descriptor + 8) == uncompressed)
                    descriptor_size = 12;
            }
            if (!descriptor_size) break;
            end += descriptor_size;
        }
        ranges[count].start = header;
        ranges[count].end = end;
        ++count;
        current += record_size;
        valid = true;
    }
    if (valid) {
        /* Each member owns its own bytes: shared or overlapping data is no ZIP
         * (this also rejects two entries naming the same local header). */
        xx_rt_qsort(ranges, count, sizeof(*ranges), xx_scan_zip_range_compare);
        for (index = 0; index < count && valid; ++index)
            if (ranges[index].end <= ranges[index].start || ranges[index].end > directory || (index && ranges[index].start < ranges[index - 1].end)) valid = false;
    }
    xx_mem_free(ranges);
    /* Only a digital signature or an archive extra data record may follow. */
    while (valid && current < eocd) {
        uint8_t optional[8];
        int64_t record_size;
        uint32_t signature;
        if (eocd - current < 6 || !xx_scan_zip_read(device, size, current, optional, eocd - current < 8 ? (size_t)(eocd - current) : 8U)) return false;
        signature = xx_scan_u32le(optional);
        if (signature == 0x05054b50U) record_size = 6 + (int64_t)xx_scan_u16le(optional + 4);
        else if (signature == 0x08064b50U && eocd - current >= 8) record_size = 8 + (int64_t)xx_scan_u32le(optional + 4);
        else return false;
        if (record_size > eocd - current) return false;
        current += record_size;
    }
    return valid && current == eocd;
}

/* XZip::_getNumberOfLocalFileHeaders: complete local records from offset 0,
 * stopping at the first that is not one (or uses a data descriptor). */
static int64_t xx_scan_zip_local_stream(xx_io_device *device, int64_t size, uint8_t *name, uint32_t *records, xx_pd_struct *pd)
{
    int64_t current = 0;
    *records = 0;
    while (!(pd && pd->is_stop) && size - current >= 30) {
        uint8_t local[30];
        uint16_t flags, name_length;
        int64_t record_size;
        if (!xx_scan_zip_read(device, size, current, local, 30) || xx_scan_u32le(local) != XX_SCAN_ZIP_LFH) break;
        flags = xx_scan_u16le(local + 6);
        name_length = xx_scan_u16le(local + 26);
        record_size = 30 + (int64_t)name_length + xx_scan_u16le(local + 28) + (int64_t)xx_scan_u32le(local + 18);
        if (record_size > size - current || (flags & 8U)) break;
        if (!xx_scan_zip_read(device, size, current + 30, name, name_length) || ((flags & 0x0800U) && !xx_scan_zip_utf8(name, name_length))) break;
        ++*records;
        current += record_size;
    }
    return current;
}

/* XZip::_isECDSignaturePresent: the end-record signature anywhere from offset. */
static bool xx_scan_zip_has_end_signature(xx_io_device *device, int64_t size, int64_t offset)
{
    uint8_t window[4096 + 3];
    size_t carried = 0;
    while (offset < size) {
        size_t amount = size - offset > 4096 ? 4096U : (size_t)(size - offset), index;
        if (!xx_scan_read_at(device, offset, window + carried, amount)) return false;
        for (index = 0; index + 4 <= carried + amount; ++index)
            if (xx_scan_u32le(window + index) == XX_SCAN_ZIP_ECD) return true;
        offset += (int64_t)amount;
        if (carried + amount >= 3) {
            xx_mem_move(window, window + carried + amount - 3, 3);
            carried = 3;
        } else {
            carried += amount;
        }
    }
    return false;
}

static bool xx_scan_is_zip(xx_io_device *device, int64_t size, const uint8_t *head, size_t head_size, xx_pd_struct *pd)
{
    uint8_t *scratch = NULL;
    bool found = false;
    uint32_t records = 0;
    int64_t stream_end;
    if (size < 0) return head_size >= 4U && (!memcmp(head, "PK\x03\x04", 4) || !memcmp(head, "PK\x05\x06", 4));
    if (size >= 22) {
        int64_t window = size < 0xFFFF + 22 ? size : 0xFFFF + 22, base = size - window, position;
        uint8_t *tail = (uint8_t *)xx_mem_alloc((size_t)window);
        if (tail && xx_scan_read_at(device, base, tail, (size_t)window)) {
            /* The last candidate first, as XZip tries them. */
            for (position = window - 4; position >= 0 && !found; --position) {
                int64_t current = base + position;
                if (xx_scan_u32le(tail + position) != XX_SCAN_ZIP_ECD || size - current < 22 ||
                    (int64_t)xx_scan_u16le(tail + position + 20) > size - current - 22)
                    continue;
                if (!scratch && !(scratch = (uint8_t *)xx_mem_alloc(3U * 65536U))) break;
                found = xx_scan_zip_directory(device, size, current, tail + position, scratch, pd);
            }
        }
        xx_mem_free(tail);
    }
    if (!found && head_size >= 4U && xx_scan_u32le(head) == XX_SCAN_ZIP_LFH && (scratch || (scratch = (uint8_t *)xx_mem_alloc(3U * 65536U)))) {
        stream_end = xx_scan_zip_local_stream(device, size, scratch, &records, pd);
        found = records && stream_end > 0 && !xx_scan_zip_has_end_signature(device, size, stream_end);
    }
    xx_mem_free(scratch);
    return found && !(pd && pd->is_stop);
}

/* Keep this focused detector independent of the full format dispatcher. */
static xx_file_type_t xx_scan_parent_type(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET: return XX_FILE_TYPE_PE;
        case XX_FILE_TYPE_PE:
        case XX_FILE_TYPE_NE:
        case XX_FILE_TYPE_LE:
        case XX_FILE_TYPE_LX:
        case XX_FILE_TYPE_DOS16M:
        case XX_FILE_TYPE_DOS4G: return XX_FILE_TYPE_MSDOS;
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64: return XX_FILE_TYPE_ELF;
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64:
        case XX_FILE_TYPE_MACHOFAT: return XX_FILE_TYPE_MACHO;
        case XX_FILE_TYPE_APK: return XX_FILE_TYPE_JAR;
        case XX_FILE_TYPE_IPA:
        case XX_FILE_TYPE_JAR: return XX_FILE_TYPE_ZIP;
        case XX_FILE_TYPE_NPM: return XX_FILE_TYPE_TAR_GZ;
        case XX_FILE_TYPE_TAR_GZ: return XX_FILE_TYPE_GZ;
        case XX_FILE_TYPE_GZ:
        case XX_FILE_TYPE_LHA:
        case XX_FILE_TYPE_BZ2:
        case XX_FILE_TYPE_RAR: return XX_FILE_TYPE_ARCHIVE;
        case XX_FILE_TYPE_JPEG:
        case XX_FILE_TYPE_PNG: return XX_FILE_TYPE_IMAGE;
        case XX_FILE_TYPE_BINARY: return XX_FILE_TYPE_UNKNOWN;
        default: return XX_FILE_TYPE_BINARY;
    }
}

static xx_list_t *xx_scan_type_list(xx_file_type_t type, xx_pd_struct *pd)
{
    xx_file_type_t chain[8];
    size_t count = 0;
    xx_list_t *types = xx_list_create(sizeof(xx_file_type_t), NULL);
    if (!types) goto oom;
    while (type != XX_FILE_TYPE_UNKNOWN && count < sizeof(chain) / sizeof(chain[0])) {
        chain[count++] = type;
        type = xx_scan_parent_type(type);
    }
    while (count) {
        if (!xx_list_append(types, &chain[--count])) {
            xx_list_destroy(types);
            goto oom;
        }
    }
    return types;
oom:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan file-type list");
    return NULL;
}

static xx_list_t *xx_scan_detect_type_window(xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    uint8_t head[64] = {0};
    size_t head_size = xx_scan_read_head(device, head);
    int64_t size = xx_io_total_size(device);
    if (head_size >= 2U && ((!memcmp(head, "MZ", 2)) || (!memcmp(head, "ZM", 2)))) {
        if (head_size < 64U) return xx_scan_type_list(XX_FILE_TYPE_MSDOS, pd);
        uint32_t offset = xx_scan_u32le(head + 0x3c);
        uint8_t signature[26];
        if (offset <= 1024U * 1024U && (size < 0 || (uint64_t)offset + sizeof(signature) <= (uint64_t)size) &&
            xx_scan_read_at(device, offset, signature, sizeof(signature))) {
            if (!memcmp(signature, "PE\0\0", 4)) {
                uint16_t magic = (uint16_t)signature[24] | ((uint16_t)signature[25] << 8U);
                if (magic == 0x10bU || magic == 0x20bU) {
                    /* Tiny executable packers overlap their DOS/NT headers.
                     * Require complete bounded PE validation in that case;
                     * a short MZ containing only a PE marker is insufficient. */
                    if (offset < sizeof(head)) {
                        xx_pe compact;
                        bool valid;
                        xx_pe_init(&compact, device, 0);
                        valid = xx_pe_check_is_valid(&compact.format, pd);
                        xx_pe_destroy(&compact);
                        if (!valid) return xx_scan_type_list(XX_FILE_TYPE_MSDOS, pd);
                    }
                    xx_dotnet managed;
                    bool valid_managed;
                    xx_dotnet_init(&managed, device, 0);
                    valid_managed = xx_dotnet_check_is_valid(&managed.pe.format, pd);
                    xx_dotnet_destroy(&managed);
                    xx_list_t *types = xx_scan_type_list(magic == 0x10bU ? XX_FILE_TYPE_PE32 : XX_FILE_TYPE_PE64, pd);
                    if (types && valid_managed) {
                        xx_file_type_t dotnet = XX_FILE_TYPE_DOTNET;
                        if (!xx_list_append(types, &dotnet)) {
                            xx_list_destroy(types);
                            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot append managed scan file type");
                            return NULL;
                        }
                    }
                    return types;
                }
            }
        }
        {
            xx_dos4g dos4g;
            bool valid;
            xx_dos4g_init(&dos4g, device, 0);
            valid = xx_dos4g_check_is_valid(&dos4g.format, NULL);
            xx_dos4g_destroy(&dos4g);
            if (valid) return xx_scan_type_list(XX_FILE_TYPE_DOS4G, pd);
        }
        {
            xx_dos16m dos16m;
            bool valid;
            xx_dos16m_init(&dos16m, device, 0);
            valid = xx_dos16m_check_is_valid(&dos16m.format, NULL);
            xx_dos16m_destroy(&dos16m);
            if (valid) return xx_scan_type_list(XX_FILE_TYPE_DOS16M, pd);
        }
        if (offset >= 64U && offset <= 1024U * 1024U && (size < 0 || (uint64_t)offset + 2U <= (uint64_t)size) && xx_scan_read_at(device, offset, signature, 2U)) {
            if (!memcmp(signature, "NE", 2)) return xx_scan_type_list(XX_FILE_TYPE_NE, pd);
            if (!memcmp(signature, "LE", 2)) return xx_scan_type_list(XX_FILE_TYPE_LE, pd);
            if (!memcmp(signature, "LX", 2)) return xx_scan_type_list(XX_FILE_TYPE_LX, pd);
        }
        return xx_scan_type_list(XX_FILE_TYPE_MSDOS, pd);
    }

    if (head_size >= 16U && !memcmp(head,
                                    "\x7f"
                                    "ELF",
                                    4)) {
        if (head[4] == 1U) return xx_scan_type_list(XX_FILE_TYPE_ELF32, pd);
        if (head[4] == 2U) return xx_scan_type_list(XX_FILE_TYPE_ELF64, pd);
    }
    if (xx_scan_is_macho_fat(device, head, head_size, size, pd)) return xx_scan_type_list(XX_FILE_TYPE_MACHOFAT, pd);
    if (head_size >= 24U && !memcmp(head, "\xCA\xFE\xBA\xBE", 4) && (((uint32_t)head[6] << 8U) | head[7]) >= 45U)
        return xx_scan_type_list(XX_FILE_TYPE_JAVA_CLASS, pd);
    if (head_size >= 28U && xx_scan_is_macho_magic(head)) {
        if (head[3] == 0xCEU || head[0] == 0xCEU) return xx_scan_type_list(XX_FILE_TYPE_MACHO32, pd);
        if (head_size >= 32U) return xx_scan_type_list(XX_FILE_TYPE_MACHO64, pd);
    }
    if (head_size >= 2U && head[0] == 0x1fU && head[1] == 0x8bU) {
        xx_npm npm;
        xx_gz gzip;
        bool valid;
        xx_npm_init(&npm, device, 0);
        valid = xx_npm_check_is_valid(&npm.tar_gz.format, NULL);
        xx_npm_destroy(&npm);
        if (valid) return xx_scan_type_list(XX_FILE_TYPE_NPM, pd);
        xx_gz_init(&gzip, device, 0);
        valid = xx_gz_check_is_valid(&gzip.format, pd);
        xx_gz_destroy(&gzip);
        if (valid) return xx_scan_type_list(XX_FILE_TYPE_GZ, pd);
    }
    if (head_size >= 8U && !memcmp(head, "dex\n", 4) && head[7] == 0U) return xx_scan_type_list(XX_FILE_TYPE_DEX, pd);
    if (xx_scan_pdf_header_offset(device, size, pd) >= 0) return xx_scan_type_list(XX_FILE_TYPE_PDF, pd);
    /* After PDF and DEX, as XFormats orders them: a ZIP is what XZip::isValid
     * accepts, whatever its first bytes. */
    if (xx_scan_is_zip(device, size, head, head_size, pd)) {
        xx_apk apk;
        xx_ipa ipa;
        xx_jar jar;
        bool valid;
        xx_apk_init(&apk, device, 0);
        valid = xx_apk_check_is_valid(&apk.zip.format, NULL);
        xx_apk_destroy(&apk);
        if (valid) return xx_scan_type_list(XX_FILE_TYPE_APK, pd);
        xx_ipa_init(&ipa, device, 0);
        valid = xx_ipa_check_is_valid(&ipa.zip.format, NULL);
        xx_ipa_destroy(&ipa);
        if (valid) return xx_scan_type_list(XX_FILE_TYPE_IPA, pd);
        xx_jar_init(&jar, device, 0);
        valid = xx_jar_check_is_valid(&jar.zip.format, NULL);
        xx_jar_destroy(&jar);
        if (valid) return xx_scan_type_list(XX_FILE_TYPE_JAR, pd);
        return xx_scan_type_list(XX_FILE_TYPE_ZIP, pd);
    }
    if (head_size >= 8U && !memcmp(head, "\x89PNG\r\n\x1a\n", 8)) return xx_scan_type_list(XX_FILE_TYPE_PNG, pd);
    if (head_size >= 3U && head[0] == 0xffU && head[1] == 0xd8U && head[2] == 0xffU) return xx_scan_type_list(XX_FILE_TYPE_JPEG, pd);
    if (head_size >= 4U && (!memcmp(head, "\x00\x00\x03\xF3", 4) || !memcmp(head, "\x00\x00\x03\xE7", 4))) return xx_scan_type_list(XX_FILE_TYPE_AMIGAHUNK, pd);
    if (head_size >= 2U && head[0] == 0x60U && head[1] == 0x1aU) {
        xx_atarist atari;
        bool valid;
        xx_atarist_init(&atari, device, 0);
        valid = xx_atarist_check_is_valid(&atari.format, NULL);
        xx_atarist_destroy(&atari);
        if (valid) return xx_scan_type_list(XX_FILE_TYPE_ATARIST, pd);
    }
    if ((size < 0 || size >= 0x8006) && xx_scan_read_at(device, 0x8001, head, 5U) && !memcmp(head, "CD001", 5U)) return xx_scan_type_list(XX_FILE_TYPE_ISO9660, pd);
    if (head_size >= 4U && head[2] == 0x0dU && head[3] == 0x0aU && xx_pyc_is_known_magic((uint16_t)head[0] | ((uint16_t)head[1] << 8U)))
        return xx_scan_type_list(XX_FILE_TYPE_PYC, pd);
    if (size > 0 && size <= XX_SCAN_COM_MAX_SIZE && options && xx_scan_has_com_suffix(options->file_name)) return xx_scan_type_list(XX_FILE_TYPE_COM, pd);
    return xx_scan_type_list(XX_FILE_TYPE_BINARY, pd);
}

xx_list_t *xx_scan_detect_type(xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_scan_type_window window = {0};
    xx_list_t *types;
    int64_t original_position;
    int64_t total_size;
    int64_t offset = options ? options->offset : 0;
    int64_t requested_size = options ? options->size : -1;
    if (!device) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Scan device is NULL");
        return NULL;
    }
    if (pd && pd->is_stop) return NULL;
    if (offset < 0 || requested_size < -1 || (requested_size >= 0 && requested_size > INT64_MAX - offset)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan type range");
        return NULL;
    }
    total_size = xx_io_total_size(device);
    if (total_size >= 0 && (offset > total_size || (requested_size >= 0 && requested_size > total_size - offset))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan type range exceeds input");
        return NULL;
    }
    window.source = device;
    window.base = offset;
    window.length = requested_size >= 0 ? requested_size : total_size >= 0 ? total_size - offset : -1;
    window.view.priv = &window;
    window.view.read = xx_scan_window_read;
    window.view.seek = xx_scan_window_seek;
    window.view.seek64 = xx_scan_window_seek64;
    window.view.tell = xx_scan_window_tell;
    window.view.total_size = xx_scan_window_size;
    original_position = xx_io_tell(device);
    types = xx_scan_detect_type_window(&window.view, options, pd);
    if (original_position >= 0) (void)xx_io_seek64(device, original_position, SEEK_SET);
    if (pd && pd->is_stop) {
        xx_list_destroy(types);
        return NULL;
    }
    return types;
}

xx_list_t *xx_scan_get_file_types(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    (void)engine;
    return xx_scan_detect_type(device, options, pd);
}
