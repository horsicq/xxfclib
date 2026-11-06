/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/formats/installer/apk/xx_apk.h"
#include "xxfclib/formats/installer/ipa/xx_ipa.h"
#include "xxfclib/formats/executable/jar/xx_jar.h"
#include "xxfclib/formats/installer/npm/xx_npm.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"


/* XZip::isValid, the check XFormats types a ZIP by: an end record whose whole
 * central directory agrees with every local header (XZip::findECDOffset), or,
 * without one, a bare stream of local headers with no end record after it.
 * A damaged local header, overlapping or shared member data, or ZIP64 or
 * multi-disk end records make the input no ZIP for DIE. */
#define XX_SCAN_ZIP_LFH 0x04034b50U
#define XX_SCAN_ZIP_CFH 0x02014b50U
#define XX_SCAN_ZIP_ECD 0x06054b50U

/* A central directory larger than this is walked record by record. */
#define XX_SCAN_ZIP_DIRECTORY_LIMIT ((int64_t)64 * 1024 * 1024)
/* One scratch slot: a fixed header plus a 16-bit name or extra field. */
#define XX_SCAN_ZIP_SLOT 65600U

typedef struct xx_scan_zip_entry {
    int64_t header, central, end;
    uint32_t crc, compressed, uncompressed;
    uint16_t flags, method, name_length;
    uint8_t version[2];
} xx_scan_zip_entry;

/* One walked (directory offset, entry count). */
typedef struct xx_scan_zip_walk {
    int64_t directory, end;
    uint32_t total;
    bool valid;
    uint8_t *trail;
} xx_scan_zip_walk;

typedef struct xx_scan_zip_context {
    xx_io_device *device;
    int64_t size, base, window;
    const uint8_t *tail;
    uint8_t *scratch;
    xx_scan_zip_walk *walks;
    size_t walk_count, walk_capacity;
    xx_pd_struct *pd;
} xx_scan_zip_context;

/* Exact-size read. Scattered ZIP headers come from the whole-file cache when
 * it holds the file, else straight from the cached device's source, so each
 * costs its own bytes rather than a cache-window refill. */
static bool xx_scan_zip_read(xx_io_device *device, int64_t size, int64_t offset, uint8_t *bytes, size_t length)
{
    const unsigned char *whole;
    xx_io_device *source;
    if (offset < 0 || offset > size || (int64_t)length > size - offset) return false;
    if (!length) return true;
    whole = xx_io_buffer_whole(device, 0);
    if (whole) {
        xx_mem_copy(bytes, whole + offset, length);
        return true;
    }
    source = xx_io_buffer_source(device);
    return xx_scan_type_read_at(source ? source : device, offset, bytes, length);
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

static int xx_scan_zip_entry_compare(const void *left, const void *right)
{
    int64_t a = ((const xx_scan_zip_entry *)left)->header, b = ((const xx_scan_zip_entry *)right)->header;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* The XZip::findECDOffset entry checks of one (directory offset, entry count),
 * with records bounded by the largest end record that claims them. A later
 * (smaller) end record passes exactly when *end_out <= it: every check but
 * the bound is independent of the end record. scratch holds 3 slots. */
static bool xx_scan_zip_walk_entries(xx_scan_zip_context *context, int64_t directory, uint32_t total, int64_t bound, int64_t *end_out)
{
    xx_io_device *device = context->device;
    int64_t size = context->size, current = directory;
    uint8_t *scratch = context->scratch, *directory_bytes = NULL;
    xx_scan_zip_entry *entries;
    uint32_t index;
    bool valid = false;
    entries = (xx_scan_zip_entry *)xx_mem_alloc((size_t)total * sizeof(*entries));
    if (!entries) return false;
    /* The whole central directory in one read when it is of sane size. */
    if (bound - directory <= XX_SCAN_ZIP_DIRECTORY_LIMIT) {
        directory_bytes = (uint8_t *)xx_mem_alloc((size_t)(bound - directory) + 1U);
        if (directory_bytes && !xx_scan_zip_read(device, size, directory, directory_bytes, (size_t)(bound - directory))) {
            xx_mem_free(directory_bytes);
            directory_bytes = NULL;
        }
    }
    for (index = 0; index < total; ++index) {
        xx_scan_zip_entry *entry = entries + index;
        const uint8_t *central = scratch;
        int64_t record_size;
        if ((context->pd && context->pd->is_stop) || bound - current < 46) goto done;
        if (directory_bytes) central = directory_bytes + (current - directory);
        else if (!xx_scan_zip_read(device, size, current, scratch, 46)) goto done;
        if (xx_scan_type_u32le(central) != XX_SCAN_ZIP_CFH) goto done;
        entry->flags = xx_scan_type_u16le(central + 8);
        entry->method = xx_scan_type_u16le(central + 10);
        entry->crc = xx_scan_type_u32le(central + 16);
        entry->compressed = xx_scan_type_u32le(central + 20);
        entry->uncompressed = xx_scan_type_u32le(central + 24);
        entry->name_length = xx_scan_type_u16le(central + 28);
        entry->version[0] = central[6];
        entry->version[1] = central[7];
        record_size = 46 + (int64_t)entry->name_length + xx_scan_type_u16le(central + 30) + xx_scan_type_u16le(central + 32);
        if (xx_scan_type_u16le(central + 34) > 1U || entry->compressed == 0xFFFFFFFFU || entry->uncompressed == 0xFFFFFFFFU ||
            xx_scan_type_u32le(central + 42) == 0xFFFFFFFFU || (entry->method == 0U && !(entry->flags & 1U) && entry->compressed != entry->uncompressed) ||
            record_size > bound - current)
            goto done;
        if (!directory_bytes && !xx_scan_zip_read(device, size, current + 46, scratch + 46, entry->name_length)) goto done;
        if ((entry->flags & 0x0800U) && !xx_scan_zip_utf8(central + 46, entry->name_length)) goto done;
        entry->header = (int64_t)xx_scan_type_u32le(central + 42);
        entry->central = current;
        if (directory - entry->header < 30) goto done;
        current += record_size;
    }
    /* Local headers in file order, each read at its exact size. */
    xx_rt_qsort(entries, total, sizeof(*entries), xx_scan_zip_entry_compare);
    for (index = 0; index < total; ++index) {
        const xx_scan_zip_entry *entry = entries + index;
        uint8_t *local = scratch + XX_SCAN_ZIP_SLOT, *extra = scratch + 2 * XX_SCAN_ZIP_SLOT, descriptor[24];
        const uint8_t *central_name = scratch;
        uint16_t central_flags = entry->flags, method = entry->method, name_length = entry->name_length, local_flags, local_name_length, local_extra_length;
        uint32_t crc = entry->crc, compressed = entry->compressed, uncompressed = entry->uncompressed, local_crc, local_compressed, local_uncompressed;
        int64_t header = entry->header, data, end;
        bool local_zip64, flags_ok;
        size_t position;
        if ((context->pd && context->pd->is_stop) || !xx_scan_zip_read(device, size, header, local, 30U + (size_t)name_length)) goto done;
        local_flags = xx_scan_type_u16le(local + 6);
        local_crc = xx_scan_type_u32le(local + 14);
        local_compressed = xx_scan_type_u32le(local + 18);
        local_uncompressed = xx_scan_type_u32le(local + 22);
        local_name_length = xx_scan_type_u16le(local + 26);
        local_extra_length = xx_scan_type_u16le(local + 28);
        data = header + 30 + (int64_t)local_name_length + local_extra_length;
        flags_ok = local_flags == central_flags || (xx_scan_type_u16le(local + 8) == 8U && ((local_flags ^ central_flags) & 0x7FF9U) == 0U); /* ~0x8006 */
        if (xx_scan_type_u32le(local) != XX_SCAN_ZIP_LFH || local[4] != entry->version[0] || local[5] != entry->version[1] || !flags_ok ||
            xx_scan_type_u16le(local + 8) != method || local_name_length != name_length || data > directory || (int64_t)compressed > directory - data ||
            (!(local_flags & 8U) && (local_crc != crc || local_compressed != compressed || local_uncompressed != uncompressed)))
            goto done;
        if (directory_bytes) central_name = directory_bytes + (entry->central - directory) + 46;
        else if (!xx_scan_zip_read(device, size, entry->central + 46, scratch, name_length)) goto done;
        /* Names agree byte for byte, except where both carry a codepage byte. */
        for (position = 0; position < name_length; ++position)
            if (local[30 + position] != central_name[position] && (local[30 + position] < 0x80U || central_name[position] < 0x80U)) goto done;
        local_zip64 = local_compressed == 0xFFFFFFFFU || local_uncompressed == 0xFFFFFFFFU;
        if (local_zip64) {
            bool found = false, sizes_ok = true;
            if (local[4] < 45U || !(local_flags & 8U) || !xx_scan_zip_read(device, size, data - local_extra_length, extra, local_extra_length)) goto done;
            position = 0;
            while (position < local_extra_length) {
                uint16_t tag, length;
                if (local_extra_length - position < 4) {
                    sizes_ok = false;
                    break;
                }
                tag = xx_scan_type_u16le(extra + position);
                length = xx_scan_type_u16le(extra + position + 2);
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
                        uint64_t field = xx_scan_type_u64le(extra + value);
                        if (field && field != uncompressed) sizes_ok = false;
                        value += 8;
                    }
                    if (pair || missing_compressed) {
                        uint64_t field = xx_scan_type_u64le(extra + value);
                        if (field && field != compressed) sizes_ok = false;
                    }
                }
                position += length;
            }
            if (!sizes_ok || !found) goto done;
        }
        end = data + (int64_t)compressed;
        if (local_flags & 8U) {
            /* Bit 3: the data descriptor, not the local header, holds the values. */
            int64_t available = directory - end, length = local_zip64 ? 24 : 16, descriptor_size = 0;
            if ((local_crc && local_crc != crc) || (local_compressed && local_compressed != 0xFFFFFFFFU && local_compressed != compressed) ||
                (local_uncompressed && local_uncompressed != 0xFFFFFFFFU && local_uncompressed != uncompressed))
                goto done;
            if (available < length) length = available < 0 ? 0 : available;
            if (length >= 12 && !xx_scan_zip_read(device, size, end, descriptor, (size_t)length)) goto done;
            if (local_zip64) {
                if (length >= 24 && xx_scan_type_u32le(descriptor) == 0x08074b50U && xx_scan_type_u32le(descriptor + 4) == crc &&
                    xx_scan_type_u64le(descriptor + 8) == compressed && xx_scan_type_u64le(descriptor + 16) == uncompressed)
                    descriptor_size = 24;
                if (!descriptor_size && length >= 20 && xx_scan_type_u32le(descriptor) == crc && xx_scan_type_u64le(descriptor + 4) == compressed &&
                    xx_scan_type_u64le(descriptor + 12) == uncompressed)
                    descriptor_size = 20;
            } else {
                if (length >= 16 && xx_scan_type_u32le(descriptor) == 0x08074b50U && xx_scan_type_u32le(descriptor + 4) == crc &&
                    xx_scan_type_u32le(descriptor + 8) == compressed && xx_scan_type_u32le(descriptor + 12) == uncompressed)
                    descriptor_size = 16;
                if (!descriptor_size && length >= 12 && xx_scan_type_u32le(descriptor) == crc && xx_scan_type_u32le(descriptor + 4) == compressed &&
                    xx_scan_type_u32le(descriptor + 8) == uncompressed)
                    descriptor_size = 12;
            }
            if (!descriptor_size) goto done;
            end += descriptor_size;
        }
        /* Each member owns its own bytes: shared or overlapping data is no ZIP
         * (this also rejects two entries naming the same local header). */
        if (end <= header || end > directory || (index && header < entries[index - 1].end)) goto done;
        entries[index].end = end;
    }
    *end_out = current;
    valid = true;
done:
    xx_mem_free(directory_bytes);
    xx_mem_free(entries);
    return valid;
}

/* Where "only a digital signature or an archive extra data record may follow"
 * the directory can end: one bit per tail-window offset, walked once from
 * current up to limit (the largest end record that asks). */
static uint8_t *xx_scan_zip_trail(xx_scan_zip_context *context, int64_t current, int64_t limit)
{
    uint8_t *trail = (uint8_t *)xx_mem_calloc((size_t)(context->window / 8) + 1U, 1U);
    if (!trail) return NULL;
    for (;;) {
        uint8_t optional[8];
        int64_t record_size;
        uint32_t signature;
        size_t length;
        if (current >= context->base) trail[(size_t)((current - context->base) >> 3)] |= (uint8_t)(1U << ((current - context->base) & 7));
        if (current >= limit || limit - current < 6 || (context->pd && context->pd->is_stop)) break;
        length = limit - current < 8 ? (size_t)(limit - current) : 8U;
        if (current >= context->base) xx_mem_copy(optional, context->tail + (current - context->base), length);
        else if (!xx_scan_zip_read(context->device, context->size, current, optional, length)) break;
        signature = xx_scan_type_u32le(optional);
        if (signature == 0x05054b50U) record_size = 6 + (int64_t)xx_scan_type_u16le(optional + 4);
        else if (signature == 0x08064b50U && limit - current >= 8) record_size = 8 + (int64_t)xx_scan_type_u32le(optional + 4);
        else break;
        if (record_size > limit - current) break;
        current += record_size;
    }
    return trail;
}

/* One XZip::findECDOffset candidate. The entry walk is shared by every
 * candidate naming the same (directory offset, entry count). */
static bool xx_scan_zip_directory(xx_scan_zip_context *context, int64_t eocd, const uint8_t *ecd)
{
    uint16_t total = xx_scan_type_u16le(ecd + 10);
    uint32_t directory_size = xx_scan_type_u32le(ecd + 12), directory_offset = xx_scan_type_u32le(ecd + 16);
    int64_t directory = (int64_t)directory_offset, offset;
    xx_scan_zip_walk *walk = NULL;
    size_t index;
    if (xx_scan_type_u16le(ecd + 4) || xx_scan_type_u16le(ecd + 6) || xx_scan_type_u16le(ecd + 8) != total || total == 0xFFFFU || directory_size == 0xFFFFFFFFU ||
        directory_offset == 0xFFFFFFFFU)
        return false;
    if (!total) return !directory_size && directory == eocd;
    if (directory > eocd || (int64_t)directory_size != eocd - directory) return false;
    for (index = 0; index < context->walk_count && !walk; ++index)
        if (context->walks[index].directory == directory && context->walks[index].total == total) walk = context->walks + index;
    if (!walk) {
        if (context->walk_count == context->walk_capacity) {
            size_t capacity = context->walk_capacity ? context->walk_capacity * 2U : 8U;
            xx_scan_zip_walk *walks = (xx_scan_zip_walk *)xx_mem_realloc(context->walks, capacity * sizeof(*walks));
            if (!walks) return false;
            context->walks = walks;
            context->walk_capacity = capacity;
        }
        walk = context->walks + context->walk_count++;
        walk->directory = directory;
        walk->total = total;
        walk->end = 0;
        walk->trail = NULL;
        walk->valid = xx_scan_zip_walk_entries(context, directory, total, eocd, &walk->end);
    }
    if (!walk->valid || walk->end > eocd) return false;
    if (walk->end == eocd) return true;
    if (!walk->trail && !(walk->trail = xx_scan_zip_trail(context, walk->end, eocd))) return false;
    offset = eocd - context->base;
    return offset >= 0 && ((walk->trail[(size_t)(offset >> 3)] >> (offset & 7)) & 1U) != 0U;
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
        if (!xx_scan_zip_read(device, size, current, local, 30) || xx_scan_type_u32le(local) != XX_SCAN_ZIP_LFH) break;
        flags = xx_scan_type_u16le(local + 6);
        name_length = xx_scan_type_u16le(local + 26);
        record_size = 30 + (int64_t)name_length + xx_scan_type_u16le(local + 28) + (int64_t)xx_scan_type_u32le(local + 18);
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
        if (!xx_scan_type_read_at(device, offset, window + carried, amount)) return false;
        for (index = 0; index + 4 <= carried + amount; ++index)
            if (xx_scan_type_u32le(window + index) == XX_SCAN_ZIP_ECD) return true;
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
        xx_scan_zip_context context;
        size_t index;
        xx_mem_zero(&context, sizeof(context));
        context.device = device;
        context.size = size;
        context.base = base;
        context.window = window;
        context.tail = tail;
        context.pd = pd;
        if (tail && xx_scan_type_read_at(device, base, tail, (size_t)window)) {
            /* The last candidate first, as XZip tries them. */
            for (position = window - 4; position >= 0 && !found; --position) {
                int64_t current = base + position;
                if (xx_scan_type_u32le(tail + position) != XX_SCAN_ZIP_ECD || size - current < 22 ||
                    (int64_t)xx_scan_type_u16le(tail + position + 20) > size - current - 22)
                    continue;
                if (!scratch && !(scratch = (uint8_t *)xx_mem_alloc(3U * XX_SCAN_ZIP_SLOT))) break;
                context.scratch = scratch;
                found = xx_scan_zip_directory(&context, current, tail + position);
            }
        }
        for (index = 0; index < context.walk_count; ++index) xx_mem_free(context.walks[index].trail);
        xx_mem_free(context.walks);
        xx_mem_free(tail);
    }
    if (!found && head_size >= 4U && xx_scan_type_u32le(head) == XX_SCAN_ZIP_LFH && (scratch || (scratch = (uint8_t *)xx_mem_alloc(3U * XX_SCAN_ZIP_SLOT)))) {
        stream_end = xx_scan_zip_local_stream(device, size, scratch, &records, pd);
        found = records && stream_end > 0 && !xx_scan_zip_has_end_signature(device, size, stream_end);
    }
    xx_mem_free(scratch);
    return found && !(pd && pd->is_stop);
}


xx_file_type_t xx_scan_type_detect_gzip(xx_io_device *device, const uint8_t *head, size_t head_size, xx_pd_struct *pd)
{
    xx_npm npm;
    xx_gz gzip;
    bool valid;
    if (head_size < 2U || head[0] != 0x1fU || head[1] != 0x8bU) return XX_FILE_TYPE_UNKNOWN;
    xx_npm_init(&npm, device, 0);
    valid = xx_npm_check_is_valid(&npm.tar_gz.format, NULL);
    xx_npm_destroy(&npm);
    if (valid) return XX_FILE_TYPE_NPM;
    xx_gz_init(&gzip, device, 0);
    valid = xx_gz_check_is_valid(&gzip.format, pd);
    xx_gz_destroy(&gzip);
    return valid ? XX_FILE_TYPE_GZ : XX_FILE_TYPE_UNKNOWN;
}

xx_file_type_t xx_scan_type_detect_zip(xx_io_device *device, int64_t size, const uint8_t *head, size_t head_size, xx_pd_struct *pd)
{
    xx_apk apk;
    xx_ipa ipa;
    xx_jar jar;
    bool valid;
    if (!xx_scan_is_zip(device, size, head, head_size, pd)) return XX_FILE_TYPE_UNKNOWN;
    xx_apk_init(&apk, device, 0);
    valid = xx_apk_check_is_valid(&apk.zip.format, NULL);
    xx_apk_destroy(&apk);
    if (valid) return XX_FILE_TYPE_APK;
    xx_ipa_init(&ipa, device, 0);
    valid = xx_ipa_check_is_valid(&ipa.zip.format, NULL);
    xx_ipa_destroy(&ipa);
    if (valid) return XX_FILE_TYPE_IPA;
    xx_jar_init(&jar, device, 0);
    valid = xx_jar_check_is_valid(&jar.zip.format, NULL);
    xx_jar_destroy(&jar);
    return valid ? XX_FILE_TYPE_JAR : XX_FILE_TYPE_ZIP;
}
