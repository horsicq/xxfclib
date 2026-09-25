/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "IFAH" installer package.  xx_ifah_installer.h carries the field tables
 * and the member-name rules.
 *
 * Written from the format's structure as measured on the two known setup
 * files (76 records each, every one inflating to its declared size with a
 * matching CRC-32, the chain ending at the header's file size).  The header
 * checks U3's "SFX IFAH" handler makes (signature, positive size and count,
 * non-negative sizes, non-empty name) are a subset of the ones here; no code
 * was taken from it.
 *
 * Only as much of the executable is read as locating its overlay needs: the
 * DOS header's e_lfanew, the PE file header and the section table.  The
 * package is then walked, never loaded: one header of at most 284 bytes per
 * record.  The walk is also the validator, so a file that merely carries the
 * signature is refused before anything is allocated, and the probe costs a
 * handful of small reads on an executable without the package.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ifah_installer/xx_ifah_installer.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef IFAH_INSTALLER
#define XX_IFAH_INSTALLER_FILE_TYPE XX_FILE_TYPE_IFAH_INSTALLER
#else
#define XX_IFAH_INSTALLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IFAH_HEADER 17
#define IFAH_SIZE_OFFSET 4
#define IFAH_COUNT_OFFSET 13

#define IFAH_RECORD 29
#define IFAH_UNPACKED_OFFSET 4
#define IFAH_PACKED_OFFSET 8
#define IFAH_CRC_OFFSET 12
#define IFAH_TIME_OFFSET 16
#define IFAH_DATE_OFFSET 18
#define IFAH_NAME_LENGTH_OFFSET 28
#define IFAH_MAX_NAME 255
#define IFAH_RECORD_MAX (IFAH_RECORD + IFAH_MAX_NAME)

/* Both sizes are read as signed by the installer, so a set top bit is not a
 * size it can have written. */
#define IFAH_MAX_FIELD UINT32_C(0x7FFFFFFF)
/* Deflate cannot expand past 1032:1 (two bits per 258-byte match); anything
 * beyond that, with a small allowance, is not a stream of this size. */
#define IFAH_DEFLATE_RATIO 1032U
#define IFAH_DEFLATE_SLACK 258U
/* A record is at least 31 bytes (header, one name byte, the shortest
 * stream), so a device bounds the walk anyway; this ceiling keeps the
 * bookkeeping of a corrupt header small. */
#define IFAH_MAX_RECORDS 262144U

/* PE parsing: just enough to find the end of the section data. */
#define IFAH_DOS_HEADER 64
#define IFAH_PE_HEADER 24
#define IFAH_PE_MAX_LFANEW 0x10000U
#define IFAH_PE_SECTION 40U
#define IFAH_PE_MAX_SECTIONS 96U

/* A converted name: at most "%XX" per raw byte, then "%_" and up to ten
 * digits of record index, then the terminator. */
#define IFAH_NAME_BUFFER (3 * IFAH_MAX_NAME + 2 + 10 + 1)
#define IFAH_POLL_MASK 0x3FFU
#define IFAH_METHOD_DEFLATE 8U

typedef struct ifah_layout_s {
    int64_t payload;  /**< Absolute offset of the "IFAH" header. */
    int64_t end;      /**< Absolute end of the last record. */
    uint32_t declared;
    uint32_t count;
    bool is_sfx;
} ifah_layout;

typedef struct ifah_member_s {
    int64_t header; /**< Absolute offset of the record header. */
    uint32_t packed;
    uint32_t unpacked;
    uint32_t crc;
    uint16_t dos_time;
    uint16_t dos_date;
    uint8_t name_length;
    bool renamed;
} ifah_member;

/* The packed stream follows the header and the name. */
static int64_t ifah_member_data(const ifah_member *member) {
    return member->header + IFAH_RECORD + (int64_t)member->name_length;
}

typedef struct ifah_key_s {
    uint64_t hash; /**< Of the converted name, ASCII folded to lower case. */
    uint32_t index;
} ifah_key;

typedef struct ifah_stream_s {
    ifah_layout layout;
    ifah_member *items;
    size_t count;
    size_t index;  /**< Current record. */
    bool control;  /**< The current record's raw name has a control byte. */
    char name[IFAH_NAME_BUFFER];
} ifah_stream;

/* A write-only device that counts, caps, checksums and forwards (or
 * discards). */
typedef struct ifah_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t limit;
    uint64_t written;
    uint32_t crc;
} ifah_sink;

static uint32_t ifah_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t ifah_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool ifah_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---- output sink ------------------------------------------------------- */

static ssize_t ifah_sink_write(xx_io_device *self, const void *buffer,
                               size_t n) {
    ifah_sink *sink = self ? (ifah_sink *)self->priv : NULL;
    size_t done = 0U;
    if (!sink || (!buffer && n != 0U)) return -1;
    if ((uint64_t)n > sink->limit - sink->written) return -1;
    while (sink->target && done < n) {
        ssize_t amount = xx_io_write(sink->target,
                                     (const uint8_t *)buffer + done, n - done);
        if (amount <= 0 || (size_t)amount > n - done) return -1;
        done += (size_t)amount;
    }
    sink->crc = xx_crc32_calc(sink->crc, buffer, n);
    sink->written += (uint64_t)n;
    return (ssize_t)n;
}

static void ifah_sink_init(ifah_sink *sink, xx_io_device *target,
                           uint64_t limit) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->device.write = ifah_sink_write;
    sink->device.priv = sink;
    sink->target = target;
    sink->limit = limit;
    sink->crc = 0U;
}

/* ---- member names ------------------------------------------------------ */

/* Raw name -> ASCII path (see the header).  `out` holds at least
 * 3 * length + 1 bytes; returns the converted length.  `control` reports a
 * byte below 0x20 or 0x7F, which makes the name unsafe to extract. */
static size_t ifah_convert_name(const uint8_t *raw, size_t length, char *out,
                                bool *control) {
    static const char digits[] = "0123456789ABCDEF";
    size_t at = 0U, index;
    bool found = false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c == (uint8_t)'\\' || c == (uint8_t)'/') {
            out[at++] = '/';
        } else if (c < 0x20U || c > 0x7EU || c == (uint8_t)'%') {
            if (c < 0x20U || c == 0x7FU) found = true;
            out[at++] = '%';
            out[at++] = digits[(c >> 4U) & 0x0FU];
            out[at++] = digits[c & 0x0FU];
        } else {
            out[at++] = (char)c;
        }
    }
    out[at] = 0;
    if (control) *control = found;
    return at;
}

/* 64-bit FNV-1a with ASCII folded to lower case, so names that a
 * case-insensitive file system treats as one hash alike.  A collision
 * between different names only renames a member needlessly. */
static uint64_t ifah_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
            c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* Insert "%_<index>" before the extension of the last component (or append
 * it when there is none).  `name` has room for IFAH_NAME_BUFFER bytes. */
static void ifah_insert_suffix(char *name, size_t length, uint32_t index) {
    char suffix[2 + 10];
    char digits[10];
    size_t suffix_length = 0U, digit_count = 0U, component = 0U, at, tail;
    size_t dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    if (length + suffix_length >= IFAH_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$,
 * CONOUT$) as the part of a component before its first '.', trailing spaces
 * ignored. */
static bool ifah_reserved_component(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ')
        --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length &&
            xx_rt_memcmp(stem, devices[index], stem_length) == 0)
            return true;
    return false;
}

/* Refuse absolute paths, drive letters and streams (any ':'), empty
 * components, components ending in '.' or ' ' (this covers "." and ".."),
 * device names, control characters and the characters no Windows path may
 * carry. */
static bool ifah_safe_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 0x7FU ||
            (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                ifah_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- locating the package ---------------------------------------------- */

/* The end of the section whose raw data ends last, from `base`, or -1 when
 * the image at `base` is not a PE image this reader can read. */
static int64_t ifah_pe_overlay(xx_io_device *device, int64_t base,
                               int64_t total) {
    uint8_t dos[IFAH_DOS_HEADER];
    uint8_t pe[IFAH_PE_HEADER];
    uint8_t table[IFAH_PE_MAX_SECTIONS * IFAH_PE_SECTION];
    uint32_t lfanew, sections, optional, index;
    uint64_t end = 0U;
    int64_t table_offset;
    if (total - base < (int64_t)IFAH_DOS_HEADER ||
        !ifah_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return -1;
    lfanew = ifah_le32(dos + 0x3C);
    if (lfanew < 4U || lfanew > IFAH_PE_MAX_LFANEW ||
        (int64_t)lfanew + IFAH_PE_HEADER > total - base ||
        !ifah_read_at(device, base + (int64_t)lfanew, pe, sizeof(pe)) ||
        pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0U || pe[3] != 0U)
        return -1;
    sections = ifah_le16(pe + 6);
    optional = ifah_le16(pe + 20);
    if (sections == 0U || sections > IFAH_PE_MAX_SECTIONS) return -1;
    table_offset = (int64_t)lfanew + IFAH_PE_HEADER + (int64_t)optional;
    if (table_offset + (int64_t)(sections * IFAH_PE_SECTION) > total - base ||
        !ifah_read_at(device, base + table_offset, table,
                      (size_t)sections * IFAH_PE_SECTION))
        return -1;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *entry = table + (size_t)index * IFAH_PE_SECTION;
        uint32_t raw_size = ifah_le32(entry + 16);
        uint32_t raw_pointer = ifah_le32(entry + 20);
        if (raw_size != 0U &&
            (uint64_t)raw_pointer + (uint64_t)raw_size > end)
            end = (uint64_t)raw_pointer + (uint64_t)raw_size;
    }
    if (end == 0U || end > (uint64_t)(total - base)) return -1;
    return (int64_t)end;
}

/* Walk the header and every record header of the package at
 * layout->payload.  With `items` NULL this is the probe and keeps nothing;
 * otherwise it fills items[] and keys[] (exactly layout->count of each),
 * using `name` (IFAH_NAME_BUFFER bytes) to hash every converted name. */
static bool ifah_walk(xx_io_device *device, int64_t total, ifah_layout *layout,
                      ifah_member *items, ifah_key *keys, char *name,
                      xx_pd_struct *pd) {
    uint8_t header[IFAH_RECORD_MAX];
    int64_t cursor;
    uint32_t count, index;
    if (layout->payload < 0 ||
        (int64_t)(IFAH_HEADER + IFAH_RECORD + 2) > total - layout->payload ||
        !ifah_read_at(device, layout->payload, header, IFAH_HEADER) ||
        header[0] != 'I' || header[1] != 'F' || header[2] != 'A' ||
        header[3] != 'H')
        return false;
    layout->declared = ifah_le32(header + IFAH_SIZE_OFFSET);
    count = ifah_le32(header + IFAH_COUNT_OFFSET);
    if (count == 0U || count > IFAH_MAX_RECORDS ||
        layout->declared > IFAH_MAX_FIELD)
        return false;
    cursor = layout->payload + IFAH_HEADER;
    for (index = 0U; index < count; ++index) {
        uint32_t unpacked, packed;
        size_t name_length, at;
        int64_t data;
        if ((index & IFAH_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if ((int64_t)IFAH_RECORD > total - cursor ||
            !ifah_read_at(device, cursor, header, IFAH_RECORD) ||
            header[0] != 'I' || header[1] != 'F' || header[2] != 'F' ||
            header[3] != 'H')
            return false;
        unpacked = ifah_le32(header + IFAH_UNPACKED_OFFSET);
        packed = ifah_le32(header + IFAH_PACKED_OFFSET);
        name_length = header[IFAH_NAME_LENGTH_OFFSET];
        if (unpacked > IFAH_MAX_FIELD || packed > IFAH_MAX_FIELD ||
            name_length == 0U ||
            (uint64_t)unpacked > (uint64_t)packed * IFAH_DEFLATE_RATIO +
                                     (packed ? IFAH_DEFLATE_SLACK : 0U))
            return false;
        if ((int64_t)(IFAH_RECORD + name_length) > total - cursor ||
            !ifah_read_at(device, cursor + IFAH_RECORD, header + IFAH_RECORD,
                          name_length))
            return false;
        /* The installer keeps names as C strings. */
        for (at = 0U; at < name_length; ++at)
            if (header[IFAH_RECORD + at] == 0U) return false;
        data = cursor + IFAH_RECORD + (int64_t)name_length;
        if ((int64_t)packed > total - data) return false;
        if (items) {
            size_t converted = ifah_convert_name(header + IFAH_RECORD,
                                                 name_length, name, NULL);
            ifah_member *member = &items[index];
            member->header = cursor;
            member->packed = packed;
            member->unpacked = unpacked;
            member->crc = ifah_le32(header + IFAH_CRC_OFFSET);
            member->dos_time = (uint16_t)ifah_le16(header + IFAH_TIME_OFFSET);
            member->dos_date = (uint16_t)ifah_le16(header + IFAH_DATE_OFFSET);
            member->name_length = (uint8_t)name_length;
            member->renamed = false;
            keys[index].hash = ifah_name_hash(name, converted);
            keys[index].index = index;
        }
        cursor = data + (int64_t)packed;
    }
    layout->count = count;
    layout->end = cursor;
    return true;
}

/* Find the package (at the base address, or as the overlay of the PE image
 * there) and walk it. */
static bool ifah_scan(Abstractformat *format, ifah_layout *layout,
                      xx_pd_struct *pd) {
    uint8_t magic[4];
    int64_t total, base;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base || total - base < (int64_t)(IFAH_HEADER + IFAH_RECORD + 2) ||
        !ifah_read_at(format->device, base, magic, sizeof(magic)))
        return false;
    xx_mem_zero(layout, sizeof(*layout));
    if (magic[0] == 'I' && magic[1] == 'F' && magic[2] == 'A' &&
        magic[3] == 'H') {
        layout->payload = base;
        layout->is_sfx = false;
    } else if (magic[0] == 'M' && magic[1] == 'Z') {
        int64_t overlay = ifah_pe_overlay(format->device, base, total);
        if (overlay < 0) return false;
        layout->payload = base + overlay;
        layout->is_sfx = true;
    } else {
        return false;
    }
    if (!ifah_walk(format->device, total, layout, NULL, NULL, NULL, pd))
        return false;
    /* The header gives the size of the whole setup file: exactly what the
     * stub and the chain add up to, and never less than the chain alone. */
    if (layout->is_sfx)
        return (int64_t)layout->declared == layout->end - base;
    return (int64_t)layout->declared >= layout->end - layout->payload;
}

static int ifah_compare_keys(const void *left, const void *right) {
    const ifah_key *a = (const ifah_key *)left;
    const ifah_key *b = (const ifah_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Sorting puts every group of equal (case-folded) names together, lowest
 * record index first; that one keeps its name and the rest are renamed. */
static void ifah_mark_duplicates(ifah_member *items, ifah_key *keys,
                                 size_t records) {
    size_t index;
    if (records < 2U) return;
    xx_rt_qsort(keys, records, sizeof(*keys), ifah_compare_keys);
    for (index = 1U; index < records; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            keys[index].index < records)
            items[keys[index].index].renamed = true;
}

static void ifah_stream_free(void *opaque) {
    ifah_stream *stream = (ifah_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool ifah_open_stream(Abstractformat *format, ifah_stream **result,
                             xx_pd_struct *pd) {
    ifah_layout layout;
    ifah_stream *stream = NULL;
    ifah_key *keys = NULL;
    int64_t total;
    if (!result || !ifah_scan(format, &layout, pd)) return false;
    total = xx_io_total_size(format->device);
    stream = (ifah_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    /* At most IFAH_MAX_RECORDS records, each proven by a header in the
     * file: 48 bytes of bookkeeping per record, never more. */
    stream->items = (ifah_member *)xx_mem_calloc(layout.count,
                                                 sizeof(*stream->items));
    keys = (ifah_key *)xx_mem_alloc((size_t)layout.count * sizeof(*keys));
    if (!stream->items || !keys) goto fail;
    /* The second walk must see exactly what the first one counted. */
    {
        ifah_layout check = layout;
        if (!ifah_walk(format->device, total, &check, stream->items, keys,
                       stream->name, pd) ||
            check.count != layout.count || check.end != layout.end)
            goto fail;
    }
    ifah_mark_duplicates(stream->items, keys, layout.count);
    xx_mem_free(keys);
    stream->layout = layout;
    stream->count = layout.count;
    *result = stream;
    return true;
fail:
    if (keys) xx_mem_free(keys);
    ifah_stream_free(stream);
    return false;
}

/* Leave record `index`'s converted (and for a duplicate suffixed) name in
 * stream->name after re-reading and re-checking its header. */
static bool ifah_load_name(Abstractformat *format, ifah_stream *stream,
                           size_t index) {
    uint8_t header[IFAH_RECORD_MAX];
    const ifah_member *member;
    size_t name_length, length;
    if (index >= stream->count) return false;
    member = &stream->items[index];
    if (!ifah_read_at(format->device, member->header, header, IFAH_RECORD) ||
        ifah_le32(header + IFAH_PACKED_OFFSET) != member->packed ||
        ifah_le32(header + IFAH_UNPACKED_OFFSET) != member->unpacked)
        return false;
    name_length = header[IFAH_NAME_LENGTH_OFFSET];
    if (name_length == 0U || name_length != member->name_length ||
        !ifah_read_at(format->device, member->header + IFAH_RECORD,
                      header + IFAH_RECORD, name_length))
        return false;
    length = ifah_convert_name(header + IFAH_RECORD, name_length,
                               stream->name, &stream->control);
    if (member->renamed)
        ifah_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

static bool ifah_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool ifah_set_record(Abstractformat *format, xx_archive_record *record,
                            ifah_stream *stream, size_t index) {
    const ifah_member *member;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!ifah_load_name(format, stream, index)) return false;
    member = &stream->items[index];
    record->header_offset = member->header;
    record->header_size = IFAH_RECORD + (int64_t)member->name_length;
    record->data_offset = ifah_member_data(member);
    record->compressed_size = (int64_t)member->packed;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          IFAH_METHOD_DEFLATE) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Inflate record `index` into `destination` (NULL only verifies); succeeds
 * only with exactly the declared size and a matching CRC-32. */
static bool ifah_extract(Abstractformat *format, const ifah_stream *stream,
                         size_t index, xx_io_device *destination,
                         xx_pd_struct *pd) {
    const ifah_member *member = &stream->items[index];
    ifah_sink sink;
    ifah_sink_init(&sink, destination, member->unpacked);
    /* No stream at all can only stand for an empty file. */
    if (member->packed == 0U)
        return member->unpacked == 0U && member->crc == 0U;
    return xx_deflate_unpack_device(format->device, ifah_member_data(member),
                                    (int64_t)member->packed, &sink.device,
                                    false, pd) &&
           sink.written == (uint64_t)member->unpacked &&
           sink.crc == member->crc;
}

/* ---- public API -------------------------------------------------------- */

void xx_ifah_installer_init(xx_ifah_installer *archive, xx_io_device *device,
                            int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IFAH_INSTALLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdownload");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_ifah_installer_check_is_valid;
    archive->format.handle_base_info = xx_ifah_installer_handle_base_info;
    archive->format.get_format_size = xx_ifah_installer_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ifah_installer_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ifah_installer_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ifah_installer_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ifah_installer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ifah_installer_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ifah_installer_free_archive_records_reading;
}

xx_ifah_installer *xx_ifah_installer_create(xx_io_device *device,
                                            int64_t base_address) {
    xx_ifah_installer *archive =
        (xx_ifah_installer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ifah_installer_init(archive, device, base_address);
    return archive;
}

void xx_ifah_installer_destroy(xx_ifah_installer *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ifah_installer_free(xx_ifah_installer *archive) {
    if (!archive) return;
    xx_ifah_installer_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ifah_installer_check_is_valid(Abstractformat *format,
                                      xx_pd_struct *pd) {
    ifah_layout layout;
    return ifah_scan(format, &layout, pd);
}

bool xx_ifah_installer_handle_base_info(Abstractformat *format,
                                        xx_pd_struct *pd) {
    ifah_layout layout;
    xx_ifah_installer *archive;
    if (!ifah_scan(format, &layout, pd)) return false;
    archive = (xx_ifah_installer *)format;
    archive->number_of_records = layout.count;
    archive->payload_offset = layout.payload;
    archive->payload_end = layout.end;
    archive->declared_size = layout.declared;
    archive->is_sfx = layout.is_sfx;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = layout.end - format->base_address;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_ifah_installer_get_format_size(Abstractformat *format,
                                          xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ifah_installer_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ifah_installer_get_number_of_archive_records(Abstractformat *format,
                                                         xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ifah_installer_handle_base_info(format, pd))
               ? ((xx_ifah_installer *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ifah_installer_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ifah_stream *stream;
    xx_archive_record_state *state;
    if (!ifah_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ifah_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ifah_stream_free;
    state->total_records = (uint64_t)stream->count;
    if (!ifah_copy_options(&state->options, options) ||
        !ifah_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ifah_installer_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ifah_installer_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ifah_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ifah_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        ifah_set_record(format, &state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_ifah_installer_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ifah_stream *stream;
    const ifah_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ifah_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->unpacked > xx_var_get_u64(option))
        return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option)
        /* No destination: inflate the record through, which verifies it. */
        return ifah_extract(format, stream, stream->index, NULL, pd);
    /* stream->name was built from the file by ifah_load_name: refuse it
     * before anything is created when it could escape the output folder or
     * name a device. */
    if (stream->control || !ifah_safe_name(stream->name)) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = ifah_extract(format, stream, stream->index, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
    if (result && (member->dos_date != 0U || member->dos_time != 0U))
        /* Best effort; a file system that refuses the stamp does not make
         * the extraction a failure. */
        (void)xx_io_apply_dos_time_and_attrs_a(path, member->dos_date,
                                               member->dos_time, 0U);
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ifah_installer_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
