/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WarpIN installer package (.wpi).  xx_sfx_warpin_package.h carries the
 * field tables and the member-name rules.
 *
 * Written from the format's structure as measured on real packages.  The
 * acceptance rules (script is a bzip2 stream, the first package starts
 * right after the table, member records carry 0xF012 and a zero extension
 * byte, zero-byte members have no stream) follow XArchive's
 * installers/xwarpin.cpp (MIT); the revision-4 extension header follows the
 * layout U3's WarpIN handler reads.  No code is taken from either.
 *
 * The package is walked, never loaded: the header, the package table in
 * small batches and one 0x11D-byte member header at a time.  The walk is
 * also the validator, so a file that merely opens with the magic dword is
 * refused before anything is allocated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_warpin_package/xx_sfx_warpin_package.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef SFX_WARPIN_PACKAGE
#define XX_SFX_WARPIN_PACKAGE_FILE_TYPE XX_FILE_TYPE_SFX_WARPIN_PACKAGE
#else
#define XX_SFX_WARPIN_PACKAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define WPI_HEADER_SIZE 0x214
#define WPI_REVISION_OFFSET 0x004
#define WPI_PACKAGES_OFFSET 0x20A
#define WPI_SCRIPT_UNPACKED_OFFSET 0x20C
#define WPI_SCRIPT_PACKED_OFFSET 0x20E
#define WPI_BLOB_OFFSET 0x210
#define WPI_MAX_REVISION 4U
/* Revision 4 extension header: its own size first, at least 0x28 bytes.
 * The cap only rules out a size field that is plainly garbage. */
#define WPI_EXTENSION_MIN 0x28
#define WPI_EXTENSION_MAX 0x10000

#define WPI_PACKAGE_ENTRY 0x30
#define WPI_PACKAGE_BATCH 32U
#define WPI_LABEL_OFFSET 0x10
#define WPI_LABEL_SIZE 0x20

#define WPI_MEMBER_HEADER 0x11D
#define WPI_MEMBER_MAGIC 0xF012U
#define WPI_MEMBER_METHOD 0x004
#define WPI_MEMBER_PACKAGE 0x006
#define WPI_MEMBER_UNPACKED 0x008
#define WPI_MEMBER_PACKED 0x00C
#define WPI_MEMBER_NAME 0x014
#define WPI_MEMBER_NAME_SIZE 0x100
#define WPI_MEMBER_MTIME 0x114
#define WPI_MEMBER_EXTENSION 0x11C

#define WPI_METHOD_STORED 0U
#define WPI_METHOD_BZIP2 1U

/* The smallest bzip2 stream: "BZh9", the end-of-stream magic and its CRC. */
#define WPI_MIN_BZIP2 14
/* A package count and a member count are u16 each; this ceiling keeps a
 * corrupt table from driving an enormous walk or bookkeeping allocation. */
#define WPI_MAX_MEMBERS 1000000U
/* The script size field is u16 and holds the low 16 bits of the real size;
 * a script is decoded up to this many bytes and no further. */
#define WPI_SCRIPT_MAX (16U * 1024U * 1024U)
/* A converted name: at most "%XX" per raw byte, then "%_" and up to ten
 * digits of record index, then the terminator. */
#define WPI_NAME_BUFFER (3 * (WPI_MEMBER_NAME_SIZE - 1) + 2 + 10 + 1)
#define WPI_COPY_CHUNK 65536U
#define WPI_POLL_MASK 0x3FFU

typedef struct wpi_layout_s {
    int64_t size;           /**< Bytes from the base address to EOF. */
    int64_t script_offset;  /**< From the base address. */
    int64_t table_offset;   /**< From the base address. */
    int64_t members_offset; /**< End of the package table, from base. */
    int64_t end;            /**< End of the last member, from base. */
    int64_t origin;         /**< Absolute offset package offsets count from. */
    uint32_t script_packed;
    uint32_t script_unpacked;
    uint32_t packages;
    uint32_t revision;
    uint32_t members;
} wpi_layout;

typedef struct wpi_member_s {
    int64_t header; /**< Absolute offset of the member header. */
    uint32_t packed;
    uint32_t unpacked;
    uint32_t mtime;
    uint16_t method;
    uint16_t package; /**< Index of the owning package-table entry. */
    bool renamed;
} wpi_member;

typedef struct wpi_key_s {
    uint64_t hash; /**< Of the converted name, ASCII folded to lower case. */
    uint32_t index;
} wpi_key;

typedef struct wpi_stream_s {
    wpi_layout layout;
    wpi_member *items;
    size_t count; /**< Members; record 0 is the script, member i is i + 1. */
    size_t index; /**< Current record. */
    char name[WPI_NAME_BUFFER];
} wpi_stream;

/* A write-only device that counts, caps and forwards (or discards). */
typedef struct wpi_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t limit;
    uint64_t written;
} wpi_sink;

static uint32_t wpi_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t wpi_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool wpi_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static ssize_t wpi_sink_write(xx_io_device *self, const void *buffer,
                              size_t n) {
    wpi_sink *sink = self ? (wpi_sink *)self->priv : NULL;
    size_t done = 0U;
    if (!sink || (!buffer && n != 0U)) return -1;
    if ((uint64_t)n > sink->limit - sink->written) return -1;
    while (sink->target && done < n) {
        ssize_t amount = xx_io_write(sink->target,
                                     (const uint8_t *)buffer + done, n - done);
        if (amount <= 0 || (size_t)amount > n - done) return -1;
        done += (size_t)amount;
    }
    sink->written += (uint64_t)n;
    return (ssize_t)n;
}

static void wpi_sink_init(wpi_sink *sink, xx_io_device *target,
                          uint64_t limit) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->device.write = wpi_sink_write;
    sink->device.priv = sink;
    sink->target = target;
    sink->limit = limit;
}

/* Stream `size` stored bytes at `offset` into the sink in fixed chunks. */
static bool wpi_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                           wpi_sink *sink, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(WPI_COPY_CHUNK);
    if (!buffer) return false;
    while (ok && done < size) {
        size_t chunk = size - done > (int64_t)WPI_COPY_CHUNK
                           ? (size_t)WPI_COPY_CHUNK
                           : (size_t)(size - done);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !wpi_read_at(source, offset + done, buffer, chunk) ||
            wpi_sink_write(&sink->device, buffer, chunk) != (ssize_t)chunk)
            ok = false;
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* ---- member names ------------------------------------------------------ */

/* Raw name -> ASCII path (see the header).  `out` holds at least
 * 3 * length + 1 bytes; returns the converted length. */
static size_t wpi_convert_name(const uint8_t *raw, size_t length, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    size_t at = 0U, index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c == (uint8_t)'\\' || c == (uint8_t)'/') {
            out[at++] = '/';
        } else if (c < 0x20U || c > 0x7EU || c == (uint8_t)'%') {
            out[at++] = '%';
            out[at++] = digits[(c >> 4U) & 0x0FU];
            out[at++] = digits[c & 0x0FU];
        } else {
            out[at++] = (char)c;
        }
    }
    out[at] = 0;
    return at;
}

/* 64-bit FNV-1a with ASCII folded to lower case, so names that a
 * case-insensitive file system treats as one hash alike.  A collision
 * between different names only renames a member needlessly. */
static uint64_t wpi_name_hash(const char *name, size_t length) {
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
 * it when there is none).  `name` has room for WPI_NAME_BUFFER bytes. */
static void wpi_insert_suffix(char *name, size_t length, uint32_t index) {
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
    if (length + suffix_length >= WPI_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$,
 * CONOUT$) as the part of a component before its first '.', trailing spaces
 * ignored. */
static bool wpi_reserved_component(const char *segment, size_t length) {
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
static bool wpi_safe_name(const char *name) {
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
                wpi_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- structure walk ---------------------------------------------------- */

static bool wpi_read_layout(Abstractformat *format, wpi_layout *out) {
    uint8_t header[WPI_HEADER_SIZE];
    uint8_t probe[10];
    wpi_layout layout;
    int64_t total, blob, table_size, position;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&layout, sizeof(layout));
    layout.size = total - format->base_address;
    if (layout.size < (int64_t)(WPI_HEADER_SIZE + WPI_MIN_BZIP2 +
                                WPI_PACKAGE_ENTRY + WPI_MEMBER_HEADER) ||
        !wpi_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    if (header[0] != 0x77U || header[1] != 0x04U || header[2] != 0x02U ||
        header[3] != 0xBEU)
        return false;
    layout.revision = wpi_le16(header + WPI_REVISION_OFFSET);
    layout.packages = wpi_le16(header + WPI_PACKAGES_OFFSET);
    layout.script_unpacked = wpi_le16(header + WPI_SCRIPT_UNPACKED_OFFSET);
    layout.script_packed = wpi_le16(header + WPI_SCRIPT_PACKED_OFFSET);
    blob = (int64_t)(int32_t)wpi_le32(header + WPI_BLOB_OFFSET);
    if (layout.revision > WPI_MAX_REVISION || layout.packages == 0U ||
        layout.script_packed < (uint32_t)WPI_MIN_BZIP2 || blob < 0)
        return false;
    position = WPI_HEADER_SIZE;
    if (layout.revision == WPI_MAX_REVISION) {
        uint8_t field[4];
        int64_t extension;
        if (!wpi_read_at(format->device, format->base_address + position,
                         field, sizeof(field)))
            return false;
        extension = (int64_t)(int32_t)wpi_le32(field);
        if (extension < WPI_EXTENSION_MIN || extension > WPI_EXTENSION_MAX ||
            extension > layout.size - position)
            return false;
        position += extension;
    }
    layout.script_offset = position;
    if ((int64_t)layout.script_packed > layout.size - position) return false;
    /* The script is one bzip2 stream: its header, then a block or the
     * end-of-stream magic.  This is what separates a package from a file
     * that merely opens with the magic dword. */
    if (!wpi_read_at(format->device, format->base_address + position, probe,
                     sizeof(probe)) ||
        probe[0] != 'B' || probe[1] != 'Z' || probe[2] != 'h' ||
        probe[3] < '1' || probe[3] > '9' ||
        !((probe[4] == 0x31U && probe[5] == 0x41U && probe[6] == 0x59U &&
           probe[7] == 0x26U && probe[8] == 0x53U && probe[9] == 0x59U) ||
          (probe[4] == 0x17U && probe[5] == 0x72U && probe[6] == 0x45U &&
           probe[7] == 0x38U && probe[8] == 0x50U && probe[9] == 0x90U)))
        return false;
    position += (int64_t)layout.script_packed;
    if (blob > layout.size - position) return false;
    position += blob;
    layout.table_offset = position;
    table_size = (int64_t)layout.packages * WPI_PACKAGE_ENTRY;
    if (table_size > layout.size - position) return false;
    position += table_size;
    layout.members_offset = position;
    if ((int64_t)WPI_MEMBER_HEADER > layout.size - position) return false;
    *out = layout;
    return true;
}

/* The fields of one member header that the walk and the records use.
 * Returns false when the header is not a member record. */
static bool wpi_parse_member(const uint8_t *header, wpi_member *member,
                             size_t *name_length) {
    int64_t packed, unpacked;
    size_t length = 0U;
    if (wpi_le16(header) != WPI_MEMBER_MAGIC ||
        header[WPI_MEMBER_EXTENSION] != 0U)
        return false;
    unpacked = (int64_t)(int32_t)wpi_le32(header + WPI_MEMBER_UNPACKED);
    packed = (int64_t)(int32_t)wpi_le32(header + WPI_MEMBER_PACKED);
    if (unpacked < 0 || packed < 0) return false;
    while (length < WPI_MEMBER_NAME_SIZE &&
           header[WPI_MEMBER_NAME + length] != 0U)
        ++length;
    /* The writer terminates the name inside the field. */
    if (length == 0U || length >= WPI_MEMBER_NAME_SIZE) return false;
    member->packed = (uint32_t)packed;
    member->unpacked = (uint32_t)unpacked;
    member->method = (uint16_t)wpi_le16(header + WPI_MEMBER_METHOD);
    member->mtime = wpi_le32(header + WPI_MEMBER_MTIME);
    member->package = 0U;
    member->renamed = false;
    *name_length = length;
    return true;
}

/* Walk the package table and every member header.  With `items` NULL this
 * is the probe and keeps nothing; otherwise it fills items[] and keys[] (at
 * most `capacity` members; keys[0] is left to the caller for the script)
 * using `name` (WPI_NAME_BUFFER bytes) to hash every converted name. */
static bool wpi_walk(Abstractformat *format, wpi_layout *layout,
                     wpi_member *items, wpi_key *keys, size_t capacity,
                     char *name, xx_pd_struct *pd) {
    uint8_t table[WPI_PACKAGE_BATCH * WPI_PACKAGE_ENTRY];
    uint8_t header[WPI_MEMBER_HEADER];
    const int64_t base = format->base_address;
    const int64_t limit = base + layout->size;
    int64_t previous_end = base + layout->members_offset;
    int64_t origin = base;
    uint32_t package, loaded = 0U, first = 0U, count = 0U;
    for (package = 0U; package < layout->packages; ++package) {
        const uint8_t *entry;
        int64_t offset, cursor;
        uint32_t files, file;
        if (package - first >= loaded) {
            uint32_t batch = layout->packages - package;
            if (batch > WPI_PACKAGE_BATCH) batch = WPI_PACKAGE_BATCH;
            if (!wpi_read_at(format->device,
                             base + layout->table_offset +
                                 (int64_t)package * WPI_PACKAGE_ENTRY,
                             table, (size_t)batch * WPI_PACKAGE_ENTRY))
                return false;
            first = package;
            loaded = batch;
        }
        entry = table + (size_t)(package - first) * WPI_PACKAGE_ENTRY;
        files = wpi_le16(entry + 2U);
        offset = (int64_t)(int32_t)wpi_le32(entry + 4U);
        if (offset < 0 || (int32_t)wpi_le32(entry + 8U) < 0 ||
            (int32_t)wpi_le32(entry + 12U) < 0)
            return false;
        if (package == 0U) {
            /* The first package starts right after the table.  Its offset
             * is from the package start, or - for a package inside a larger
             * file - from the start of that file. */
            if (offset == layout->members_offset)
                origin = base;
            else if (base > 0 && offset == base + layout->members_offset)
                origin = 0;
            else
                return false;
        }
        cursor = origin + offset;
        /* Packages follow one another without overlapping, so the output
         * is bounded by the file. */
        if (cursor < previous_end || cursor > limit) return false;
        for (file = 0U; file < files; ++file) {
            wpi_member member;
            size_t name_length;
            int64_t data;
            if ((count & WPI_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
                return false;
            if (count >= WPI_MAX_MEMBERS ||
                (int64_t)WPI_MEMBER_HEADER > limit - cursor ||
                !wpi_read_at(format->device, cursor, header, sizeof(header)) ||
                !wpi_parse_member(header, &member, &name_length))
                return false;
            data = cursor + WPI_MEMBER_HEADER;
            if ((int64_t)member.packed > limit - data) return false;
            if (items) {
                size_t converted;
                if (count >= capacity) return false;
                member.header = cursor;
                member.package = (uint16_t)package;
                items[count] = member;
                converted = wpi_convert_name(header + WPI_MEMBER_NAME,
                                             name_length, name);
                keys[count + 1U].hash = wpi_name_hash(name, converted);
                keys[count + 1U].index = count + 1U;
            }
            ++count;
            cursor = data + (int64_t)member.packed;
        }
        previous_end = cursor;
    }
    if (count == 0U) return false;
    layout->members = count;
    layout->origin = origin;
    layout->end = previous_end - base;
    return true;
}

static bool wpi_scan(Abstractformat *format, wpi_layout *layout,
                     xx_pd_struct *pd) {
    return wpi_read_layout(format, layout) &&
           wpi_walk(format, layout, NULL, NULL, 0U, NULL, pd);
}

static int wpi_compare_keys(const void *left, const void *right) {
    const wpi_key *a = (const wpi_key *)left;
    const wpi_key *b = (const wpi_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Sorting puts every group of equal (case-folded) names together, lowest
 * record index first; that one keeps its name and the rest are renamed.
 * The script is record 0, so it always keeps its name. */
static void wpi_mark_duplicates(wpi_member *items, wpi_key *keys,
                                size_t records) {
    size_t index;
    if (records < 2U) return;
    xx_rt_qsort(keys, records, sizeof(*keys), wpi_compare_keys);
    for (index = 1U; index < records; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            keys[index].index >= 1U && keys[index].index < records)
            items[keys[index].index - 1U].renamed = true;
}

static void wpi_stream_free(void *opaque) {
    wpi_stream *stream = (wpi_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool wpi_open_stream(Abstractformat *format, wpi_stream **result,
                            xx_pd_struct *pd) {
    wpi_layout layout;
    wpi_stream *stream = NULL;
    wpi_key *keys = NULL;
    size_t records;
    if (!result || !wpi_scan(format, &layout, pd)) return false;
    records = (size_t)layout.members + 1U;
    stream = (wpi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    /* At most WPI_MAX_MEMBERS members, each proven by a 0x11D-byte header
     * in the file: 40 bytes of bookkeeping per member, never more. */
    stream->items = (wpi_member *)xx_mem_calloc(layout.members,
                                                sizeof(*stream->items));
    keys = (wpi_key *)xx_mem_alloc(records * sizeof(*keys));
    if (!stream->items || !keys) goto fail;
    keys[0].hash = wpi_name_hash(XX_SFX_WARPIN_PACKAGE_SCRIPT_NAME,
                                 xx_str_len(XX_SFX_WARPIN_PACKAGE_SCRIPT_NAME));
    keys[0].index = 0U;
    /* The second walk must see exactly what the first one counted. */
    {
        wpi_layout check = layout;
        if (!wpi_walk(format, &check, stream->items, keys, layout.members,
                      stream->name, pd) ||
            check.members != layout.members || check.end != layout.end)
            goto fail;
    }
    wpi_mark_duplicates(stream->items, keys, records);
    xx_mem_free(keys);
    stream->layout = layout;
    stream->count = layout.members;
    *result = stream;
    return true;
fail:
    if (keys) xx_mem_free(keys);
    wpi_stream_free(stream);
    return false;
}

/* Leave record `index`'s (converted, and for a duplicate suffixed) name in
 * stream->name; for a member also re-read and re-check its header. */
static bool wpi_load_name(Abstractformat *format, wpi_stream *stream,
                          size_t index) {
    uint8_t header[WPI_MEMBER_HEADER];
    wpi_member check;
    const wpi_member *member;
    size_t name_length, length;
    if (index == 0U) {
        xx_rt_memcpy(stream->name, XX_SFX_WARPIN_PACKAGE_SCRIPT_NAME,
                     sizeof(XX_SFX_WARPIN_PACKAGE_SCRIPT_NAME));
        return true;
    }
    if (index > stream->count) return false;
    member = &stream->items[index - 1U];
    if (!wpi_read_at(format->device, member->header, header, sizeof(header)) ||
        !wpi_parse_member(header, &check, &name_length) ||
        check.packed != member->packed || check.unpacked != member->unpacked)
        return false;
    length = wpi_convert_name(header + WPI_MEMBER_NAME, name_length,
                              stream->name);
    if (member->renamed)
        wpi_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

static bool wpi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool wpi_set_record(Abstractformat *format, xx_archive_record *record,
                           wpi_stream *stream, size_t index) {
    uint64_t packed, unpacked, method;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!wpi_load_name(format, stream, index)) return false;
    if (index == 0U) {
        record->header_offset = format->base_address;
        record->header_size = stream->layout.script_offset;
        record->data_offset =
            format->base_address + stream->layout.script_offset;
        packed = stream->layout.script_packed;
        unpacked = stream->layout.script_unpacked;
        method = WPI_METHOD_BZIP2;
    } else {
        const wpi_member *member = &stream->items[index - 1U];
        record->header_offset = member->header;
        record->header_size = WPI_MEMBER_HEADER;
        record->data_offset = member->header + WPI_MEMBER_HEADER;
        packed = member->packed;
        unpacked = member->unpacked;
        method = member->method;
        if (!xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                            member->mtime))
            return false;
        /* The owning package's label ("Pck001", ...) as the comment; it is
         * display text only, so anything unprintable is dropped. */
        {
            uint8_t raw[WPI_LABEL_SIZE];
            char label[WPI_LABEL_SIZE + 1];
            size_t at, length = 0U;
            if (wpi_read_at(format->device,
                            format->base_address +
                                stream->layout.table_offset +
                                (int64_t)member->package * WPI_PACKAGE_ENTRY +
                                WPI_LABEL_OFFSET,
                            raw, sizeof(raw))) {
                for (at = 0U; at < sizeof(raw) && raw[at] != 0U; ++at)
                    if (raw[at] >= 0x20U && raw[at] <= 0x7EU)
                        label[length++] = (char)raw[at];
                label[length] = 0;
                if (length != 0U &&
                    !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                                    label))
                    return false;
            }
        }
    }
    record->compressed_size = (int64_t)packed;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Decode record `index` into `destination` (NULL only verifies). */
static bool wpi_extract(Abstractformat *format, const wpi_stream *stream,
                        size_t index, xx_io_device *destination,
                        xx_pd_struct *pd) {
    wpi_sink sink;
    if (index == 0U) {
        const wpi_layout *layout = &stream->layout;
        wpi_sink_init(&sink, destination, WPI_SCRIPT_MAX);
        /* The size field holds the low 16 bits of the script's size. */
        return xx_bzip2_unpack_device(
                   format->device,
                   format->base_address + layout->script_offset,
                   (int64_t)layout->script_packed, &sink.device, pd) &&
               (sink.written & 0xFFFFU) == (uint64_t)layout->script_unpacked;
    }
    {
        const wpi_member *member = &stream->items[index - 1U];
        const int64_t data = member->header + WPI_MEMBER_HEADER;
        wpi_sink_init(&sink, destination, member->unpacked);
        /* A zero-byte file has no stream at all, whatever its method. */
        if (member->packed == 0U) return member->unpacked == 0U;
        if (member->method == WPI_METHOD_STORED)
            return member->packed == member->unpacked &&
                   wpi_copy_range(format->device, data,
                                  (int64_t)member->packed, &sink, pd) &&
                   sink.written == member->unpacked;
        if (member->method == WPI_METHOD_BZIP2)
            return xx_bzip2_unpack_device(format->device, data,
                                          (int64_t)member->packed,
                                          &sink.device, pd) &&
                   sink.written == member->unpacked;
        return false;
    }
}

/* ---- public API -------------------------------------------------------- */

void xx_sfx_warpin_package_init(xx_sfx_warpin_package *archive,
                                xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_WARPIN_PACKAGE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-warpin");
    xx_format_set_extension(&archive->format, "wpi");
    archive->format.check_is_valid = xx_sfx_warpin_package_check_is_valid;
    archive->format.handle_base_info = xx_sfx_warpin_package_handle_base_info;
    archive->format.get_format_size = xx_sfx_warpin_package_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_warpin_package_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_warpin_package_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_warpin_package_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_warpin_package_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_warpin_package_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_warpin_package_free_archive_records_reading;
}

xx_sfx_warpin_package *xx_sfx_warpin_package_create(xx_io_device *device,
                                                    int64_t base_address) {
    xx_sfx_warpin_package *archive =
        (xx_sfx_warpin_package *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_warpin_package_init(archive, device, base_address);
    return archive;
}

void xx_sfx_warpin_package_destroy(xx_sfx_warpin_package *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_warpin_package_free(xx_sfx_warpin_package *archive) {
    if (!archive) return;
    xx_sfx_warpin_package_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_warpin_package_check_is_valid(Abstractformat *format,
                                          xx_pd_struct *pd) {
    wpi_layout layout;
    return wpi_scan(format, &layout, pd);
}

bool xx_sfx_warpin_package_handle_base_info(Abstractformat *format,
                                            xx_pd_struct *pd) {
    wpi_layout layout;
    xx_sfx_warpin_package *archive;
    if (!wpi_scan(format, &layout, pd)) return false;
    archive = (xx_sfx_warpin_package *)format;
    archive->number_of_records = (uint64_t)layout.members + 1U;
    archive->number_of_packages = layout.packages;
    archive->revision = layout.revision;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = layout.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_warpin_package_get_format_size(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_warpin_package_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_warpin_package_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_warpin_package_handle_base_info(format, pd))
               ? ((xx_sfx_warpin_package *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_sfx_warpin_package_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    wpi_stream *stream;
    xx_archive_record_state *state;
    if (!wpi_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        wpi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = wpi_stream_free;
    state->total_records = (uint64_t)stream->count + 1U;
    if (!wpi_copy_options(&state->options, options) ||
        !wpi_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_warpin_package_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_warpin_package_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    wpi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (wpi_stream *)state->internal_state) ||
        ++stream->index > stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        wpi_set_record(format, &state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_sfx_warpin_package_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    wpi_stream *stream;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint64_t unpacked;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (wpi_stream *)state->internal_state) ||
        stream->index > stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    unpacked = stream->index == 0U
                   ? (uint64_t)stream->layout.script_unpacked
                   : (uint64_t)stream->items[stream->index - 1U].unpacked;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && unpacked > xx_var_get_u64(option)) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option)
        /* No destination: decode the record through, which verifies it. */
        return wpi_extract(format, stream, stream->index, NULL, pd);
    /* stream->name was built from the file by wpi_load_name: refuse it
     * before anything is created when it could escape the output folder or
     * name a device. */
    if (!wpi_safe_name(stream->name)) return false;
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
        result = wpi_extract(format, stream, stream->index, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_warpin_package_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
