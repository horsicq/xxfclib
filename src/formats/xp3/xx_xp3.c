/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KiriKiri XP3 resource archive.  xx_xp3.h carries the field table.
 *
 * Written from the format's structure.  The acceptance rules for the header,
 * the version-2 continuation record and the index sub-chunks follow GARbro's
 * ArcFormats/KiriKiri/ArcXP3.cs (MIT, (C) 2014-2017 morkt): a mis-sized
 * "info" is clamped to its File chunk, unknown sub-chunks and top-level
 * chunks are skipped, and a File entry without a name or a segment, or with
 * a segment outside the archive, is dropped rather than failing the whole
 * archive.  The member-name safety rules are those of this library's nsa
 * reader (src/formats/nsa/xx_nsa.c, MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xp3/xx_xp3.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef XP3
#define XX_XP3_FILE_TYPE XX_FILE_TYPE_XP3
#else
#define XX_XP3_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XP3_SIGNATURE_SIZE 11
#define XP3_HEADER_SIZE 19            /* signature + u64 index position */
#define XP3_CONTINUE 0x80U
#define XP3_METHOD_STORED 0U
#define XP3_METHOD_ZLIB 1U
#define XP3_METHOD_MASK 7U
/* Largest (unpacked) index accepted; real ones run to a few MiB. */
#define XP3_MAX_INDEX (64LL * 1024 * 1024)
/* Deflate cannot expand beyond about 1032:1; allow some slack. */
#define XP3_RATIO 1032U
#define XP3_RATIO_SLACK 1024U
#define XP3_SEGMENT_SIZE 28U
#define XP3_INFO_FIXED 22U
#define XP3_MAX_NAME_UNITS 1024U
#define XP3_MAX_MEMBERS 2000000U
#define XP3_POLL_MASK 0x3ffU
#define XP3_SUFFIX_ROOM 16U

static const uint8_t xp3_signature[XP3_SIGNATURE_SIZE] = {
    0x58, 0x50, 0x33, 0x0d, 0x0a, 0x20, 0x0a, 0x1a, 0x8b, 0x67, 0x01};

typedef struct xp3_segment_s {
    uint32_t flags;
    uint64_t offset;   /**< Relative to the archive start. */
    uint64_t original;
    uint64_t packed;
} xp3_segment;

typedef struct xp3_member_s {
    size_t name_at;        /**< Into the name pool. */
    size_t name_length;    /**< UTF-8 bytes, without terminator. */
    size_t segment_first;
    size_t segment_count;
    uint64_t original;     /**< Sum of the segments' original sizes. */
    uint64_t packed;       /**< Sum of the bytes the segments occupy. */
    uint32_t flags;        /**< "info" flags. */
    uint32_t adler;
    uint32_t suffix;       /**< Non-zero: duplicate, add "%_<suffix>". */
    int64_t header_offset; /**< Offset of the File chunk in the index. */
    bool unsafe;           /**< Control character in the raw name. */
    bool has_zlib;
} xp3_member;

typedef struct xp3_layout_s {
    int64_t size;          /**< Archive bytes available from base_address. */
    int64_t index_at;      /**< Relative position of the index record. */
    int64_t index_end;     /**< Relative end of the index record. */
    uint64_t index_size;   /**< Unpacked index bytes. */
    uint64_t index_packed;
    uint8_t method;
    uint8_t version;
} xp3_layout;

typedef struct xp3_counts_s {
    size_t members;
    size_t segments;
    size_t name_bytes;
    int64_t extent;        /**< Furthest relative byte used by the archive. */
} xp3_counts;

typedef struct xp3_stream_s {
    xp3_member *members;
    xp3_segment *segments;
    char *pool;
    size_t count;
    size_t segment_count;
    size_t pool_size;
    size_t pool_used;
    size_t index;
    char *name;            /**< The current member's final name. */
} xp3_stream;

/* ---- I/O --------------------------------------------------------------- */

static size_t xp3_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static uint16_t xp3_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t xp3_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint64_t xp3_le64(const uint8_t *p) {
    return (uint64_t)xp3_le32(p) | ((uint64_t)xp3_le32(p + 4) << 32U);
}

static bool xp3_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    const size_t capacity = xp3_capacity();
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > capacity) request = capacity;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xp3_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    if (!destination) return true;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xp3_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

/* ---- header and index record ------------------------------------------ */

static bool xp3_fits(int64_t size, uint64_t at, uint64_t length) {
    return size >= 0 && at <= (uint64_t)size && length <= (uint64_t)size - at;
}

static bool xp3_read_layout(Abstractformat *format, xp3_layout *layout) {
    uint8_t head[XP3_HEADER_SIZE];
    uint8_t record[17];
    int64_t total;
    uint64_t at;
    xx_mem_zero(layout, sizeof(*layout));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    layout->size = total - format->base_address;
    if (layout->size < XP3_HEADER_SIZE + 9 ||
        !xp3_read_at(format->device, format->base_address, head, sizeof(head)) ||
        xx_rt_memcmp(head, xp3_signature, XP3_SIGNATURE_SIZE) != 0)
        return false;
    at = xp3_le64(head + XP3_SIGNATURE_SIZE);
    layout->version = 1U;
    if (at < XP3_HEADER_SIZE || !xp3_fits(layout->size, at, 9U) ||
        !xp3_read_at(format->device, format->base_address + (int64_t)at,
                     record, 9U))
        return false;
    if (xp3_le32(record) == XP3_CONTINUE) {
        /* Version 2: {0x80, u64 0, u64 real index position}. */
        if (!xp3_fits(layout->size, at, 17U) ||
            !xp3_read_at(format->device, format->base_address + (int64_t)at,
                         record, 17U))
            return false;
        at = xp3_le64(record + 9);
        layout->version = 2U;
        if (at < XP3_HEADER_SIZE || !xp3_fits(layout->size, at, 9U) ||
            !xp3_read_at(format->device, format->base_address + (int64_t)at,
                         record, 9U))
            return false;
    }
    layout->index_at = (int64_t)at;
    layout->method = record[0];
    if (layout->method == XP3_METHOD_STORED) {
        layout->index_size = xp3_le64(record + 1);
        layout->index_packed = layout->index_size;
        if (layout->index_size > (uint64_t)XP3_MAX_INDEX ||
            !xp3_fits(layout->size, at + 9U, layout->index_size))
            return false;
        layout->index_end = (int64_t)(at + 9U + layout->index_size);
        return true;
    }
    if (layout->method != XP3_METHOD_ZLIB || !xp3_fits(layout->size, at, 17U) ||
        !xp3_read_at(format->device, format->base_address + (int64_t)at,
                     record, 17U))
        return false;
    layout->index_packed = xp3_le64(record + 1);
    layout->index_size = xp3_le64(record + 9);
    if (layout->index_packed < 2U ||
        layout->index_packed > (uint64_t)XP3_MAX_INDEX ||
        layout->index_size > (uint64_t)XP3_MAX_INDEX ||
        layout->index_size >
            layout->index_packed * XP3_RATIO + XP3_RATIO_SLACK ||
        !xp3_fits(layout->size, at + 17U, layout->index_packed))
        return false;
    layout->index_end = (int64_t)(at + 17U + layout->index_packed);
    return true;
}

/* Load the unpacked index.  An empty index yields a NULL buffer. */
static bool xp3_load_index(Abstractformat *format, const xp3_layout *layout,
                           uint8_t **result) {
    uint8_t *packed = NULL, *index = NULL;
    size_t written = 0U;
    int64_t origin = format->base_address + layout->index_at;
    *result = NULL;
    if (layout->index_size == 0U) return true;
    index = (uint8_t *)xx_mem_alloc((size_t)layout->index_size);
    if (!index) return false;
    if (layout->method == XP3_METHOD_STORED) {
        if (!xp3_read_at(format->device, origin + 9, index,
                         (size_t)layout->index_size))
            goto fail;
        *result = index;
        return true;
    }
    {
        uint8_t zhead[2];
        if (!xp3_read_at(format->device, origin + 17, zhead, 2U) ||
            !xx_zlib_stream_header_is_valid(zhead, 2U))
            goto fail;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)layout->index_packed);
    if (!packed ||
        !xp3_read_at(format->device, origin + 17, packed,
                     (size_t)layout->index_packed) ||
        !xx_zlib_stream_decode_memory(packed, (size_t)layout->index_packed,
                                      index, (size_t)layout->index_size,
                                      &written) ||
        written != (size_t)layout->index_size)
        goto fail;
    xx_mem_free(packed);
    *result = index;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    xx_mem_free(index);
    return false;
}

/* ---- names -------------------------------------------------------------- */

/* UTF-8 form of a UTF-16LE name, and its length ('\\' becomes '/', a '%'
 * starting "%_" becomes "%25", a control character becomes '_' and marks
 * the name unsafe).  With @p out NULL only the length is computed. */
static size_t xp3_name_utf8(const uint8_t *units, size_t count, char *out,
                            bool *unsafe) {
    size_t at = 0U, i;
    for (i = 0U; i < count; ++i) {
        uint32_t c = xp3_le16(units + i * 2U);
        if (c >= 0xd800U && c <= 0xdbffU && i + 1U < count) {
            uint32_t low = xp3_le16(units + (i + 1U) * 2U);
            if (low >= 0xdc00U && low <= 0xdfffU) {
                c = 0x10000U + ((c - 0xd800U) << 10U) + (low - 0xdc00U);
                ++i;
            } else {
                c = 0xfffdU;
            }
        } else if (c >= 0xd800U && c <= 0xdfffU) {
            c = 0xfffdU;
        }
        if (c == '\\') c = '/';
        if (c < 0x20U || c == 0x7fU) {
            /* Keep the name printable; extraction refuses it. */
            if (unsafe) *unsafe = true;
            c = '_';
        }
        if (c == '%' && i + 1U < count &&
            xp3_le16(units + (i + 1U) * 2U) == (uint32_t)'_') {
            if (out) { out[at] = '%'; out[at + 1] = '2'; out[at + 2] = '5'; }
            at += 3U;
        } else if (c < 0x80U) {
            if (out) out[at] = (char)c;
            at += 1U;
        } else if (c < 0x800U) {
            if (out) {
                out[at] = (char)(0xc0U | (c >> 6U));
                out[at + 1] = (char)(0x80U | (c & 0x3fU));
            }
            at += 2U;
        } else if (c < 0x10000U) {
            if (out) {
                out[at] = (char)(0xe0U | (c >> 12U));
                out[at + 1] = (char)(0x80U | ((c >> 6U) & 0x3fU));
                out[at + 2] = (char)(0x80U | (c & 0x3fU));
            }
            at += 3U;
        } else {
            if (out) {
                out[at] = (char)(0xf0U | (c >> 18U));
                out[at + 1] = (char)(0x80U | ((c >> 12U) & 0x3fU));
                out[at + 2] = (char)(0x80U | ((c >> 6U) & 0x3fU));
                out[at + 3] = (char)(0x80U | (c & 0x3fU));
            }
            at += 4U;
        }
    }
    return at;
}

static bool xp3_reserved_component(const char *segment, size_t length) {
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

/* Relative, no drive, no empty / "." / ".." component, no component ending
 * in '.' or ' ', no Windows-reserved character or device name. */
static bool xp3_safe_name(const char *name) {
    const char *segment, *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 0x7fU ||
            (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                xp3_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static uint64_t xp3_name_hash(const char *name, size_t length) {
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

static bool xp3_name_equal(const char *a, const char *b, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t x = (uint8_t)a[index], y = (uint8_t)b[index];
        if (x >= (uint8_t)'A' && x <= (uint8_t)'Z') x = (uint8_t)(x + 32U);
        if (y >= (uint8_t)'A' && y <= (uint8_t)'Z') y = (uint8_t)(y + 32U);
        if (x != y) return false;
    }
    return true;
}

/* ---- index walk --------------------------------------------------------- */

/* Parse one File chunk body.  Returns false when the entry is dropped (as
 * GARbro drops it); the archive stays valid either way.  With @p stream
 * NULL only the counts advance. */
static bool xp3_parse_file(const uint8_t *body, uint64_t size,
                           const xp3_layout *layout, int64_t header_offset,
                           xp3_stream *stream, xp3_counts *counts) {
    const uint8_t *info = NULL, *segm = NULL;
    uint64_t left = size, info_size = 0U, segm_size = 0U, original = 0U,
             packed = 0U;
    uint32_t adler = 0U;
    size_t units, name_bytes, segments, index;
    int64_t extent = 0;
    bool unsafe = false, has_zlib = false;
    const uint8_t *at = body;
    while (left > 0U) {
        uint64_t section;
        uint32_t tag;
        if (left < 12U) break;
        tag = xp3_le32(at);
        section = xp3_le64(at + 4);
        left -= 12U;
        if (section > left) {
            if (tag != 0x6f666e69U) break; /* only "info" is clamped */
            section = left;
        }
        left -= section;
        if (tag == 0x6f666e69U) { /* "info" */
            if (info) return false;  /* ambiguous entry */
            info = at + 12;
            info_size = section;
        } else if (tag == 0x6d676573U) { /* "segm" */
            if (!segm) {
                segm = at + 12;
                segm_size = section;
            }
        } else if (tag == 0x726c6461U) { /* "adlr" */
            if (section == 4U) adler = xp3_le32(at + 12);
        }
        at += 12U + section;
    }
    if (!info || info_size < XP3_INFO_FIXED || !segm) return false;
    units = xp3_le16(info + 20);
    if (units == 0U || units > XP3_MAX_NAME_UNITS ||
        (uint64_t)XP3_INFO_FIXED + (uint64_t)units * 2U > info_size)
        return false;
    segments = (size_t)(segm_size / XP3_SEGMENT_SIZE);
    if (segments == 0U) return false;
    for (index = 0U; index < segments; ++index) {
        const uint8_t *s = segm + index * XP3_SEGMENT_SIZE;
        uint32_t flags = xp3_le32(s);
        uint64_t offset = xp3_le64(s + 4), orig = xp3_le64(s + 12),
                 pk = xp3_le64(s + 20), span;
        uint32_t method = flags & XP3_METHOD_MASK;
        if (method == XP3_METHOD_STORED)
            span = orig;
        else if (method == XP3_METHOD_ZLIB)
            span = pk;
        else
            return false;
        if (!xp3_fits(layout->size, offset, span)) return false;
        if (method == XP3_METHOD_ZLIB &&
            (pk < 2U || orig > pk * XP3_RATIO + XP3_RATIO_SLACK))
            return false;
        if (method == XP3_METHOD_ZLIB) has_zlib = true;
        if (orig > UINT64_MAX - original || span > UINT64_MAX - packed)
            return false;
        original += orig;
        packed += span;
        if ((int64_t)(offset + span) > extent) extent = (int64_t)(offset + span);
    }
    name_bytes = xp3_name_utf8(info + XP3_INFO_FIXED, units, NULL, &unsafe);
    if (!stream) {
        if (counts->members >= XP3_MAX_MEMBERS) return false;
        ++counts->members;
        counts->segments += segments;
        counts->name_bytes += name_bytes + 1U;
        if (extent > counts->extent) counts->extent = extent;
        return true;
    }
    if (stream->count >= counts->members ||
        stream->segment_count + segments > counts->segments ||
        stream->pool_used + name_bytes + 1U > stream->pool_size)
        return false;
    {
        xp3_member *member = &stream->members[stream->count];
        xx_mem_zero(member, sizeof(*member));
        member->name_at = stream->pool_used;
        member->name_length = name_bytes;
        (void)xp3_name_utf8(info + XP3_INFO_FIXED, units,
                            stream->pool + stream->pool_used, NULL);
        stream->pool[stream->pool_used + name_bytes] = 0;
        stream->pool_used += name_bytes + 1U;
        member->segment_first = stream->segment_count;
        member->segment_count = segments;
        member->original = original;
        member->packed = packed;
        member->flags = xp3_le32(info);
        member->adler = adler;
        member->header_offset = header_offset;
        member->unsafe = unsafe;
        member->has_zlib = has_zlib;
        for (index = 0U; index < segments; ++index) {
            const uint8_t *s = segm + index * XP3_SEGMENT_SIZE;
            xp3_segment *out = &stream->segments[stream->segment_count++];
            out->flags = xp3_le32(s);
            out->offset = xp3_le64(s + 4);
            out->original = xp3_le64(s + 12);
            out->packed = xp3_le64(s + 20);
        }
        ++stream->count;
    }
    return true;
}

/* Walk the top-level chunks.  They must tile the index exactly. */
static bool xp3_walk(const uint8_t *index, uint64_t size,
                     const xp3_layout *layout, xp3_stream *stream,
                     xp3_counts *counts, xx_pd_struct *pd) {
    uint64_t pos = 0U;
    unsigned long step = 0U;
    while (pos < size) {
        uint64_t chunk;
        if ((++step & XP3_POLL_MASK) == 0U && xp3_stopped(pd)) return false;
        if (size - pos < 12U) return false;
        chunk = xp3_le64(index + pos + 4);
        if (chunk > size - pos - 12U) return false;
        if (xp3_le32(index + pos) == 0x656c6946U) /* "File" */
            (void)xp3_parse_file(index + pos + 12U, chunk, layout,
                                 (int64_t)pos, stream, counts);
        pos += 12U + chunk;
    }
    return true;
}

/* ---- stream ------------------------------------------------------------- */

static void xp3_stream_free(void *opaque) {
    xp3_stream *stream = (xp3_stream *)opaque;
    if (!stream) return;
    if (stream->members) xx_mem_free(stream->members);
    if (stream->segments) xx_mem_free(stream->segments);
    if (stream->pool) xx_mem_free(stream->pool);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

/* Mark later case-insensitive duplicates so they get a "%_<n>" suffix.  A
 * literal "%_" in a name is escaped as "%25_", so a suffixed name cannot
 * collide with an original one, and <n> (the member's position) keeps
 * suffixed names apart from each other. */
static bool xp3_mark_duplicates(xp3_stream *stream) {
    size_t slots = 16U, index, *table;
    if (stream->count < 2U) return true;
    while (slots < stream->count * 2U) slots <<= 1U;
    table = (size_t *)xx_mem_calloc(slots, sizeof(size_t));
    if (!table) return false;
    for (index = 0U; index < stream->count; ++index) {
        xp3_member *member = &stream->members[index];
        const char *name = stream->pool + member->name_at;
        size_t slot = (size_t)xp3_name_hash(name, member->name_length) &
                      (slots - 1U);
        for (;;) {
            size_t other = table[slot];
            if (!other) {
                table[slot] = index + 1U;
                break;
            }
            {
                const xp3_member *first = &stream->members[other - 1U];
                if (first->name_length == member->name_length &&
                    xp3_name_equal(stream->pool + first->name_at, name,
                                   member->name_length)) {
                    member->suffix = (uint32_t)(index + 1U);
                    break;
                }
            }
            slot = (slot + 1U) & (slots - 1U);
        }
    }
    xx_mem_free(table);
    return true;
}

static bool xp3_open_stream(Abstractformat *format, xp3_stream **result,
                            xp3_layout *layout_out, xp3_counts *counts_out,
                            xx_pd_struct *pd) {
    xp3_layout layout;
    xp3_counts counts;
    xp3_stream *stream = NULL;
    uint8_t *index = NULL;
    bool ok = false;
    if (result) *result = NULL;
    xx_mem_zero(&counts, sizeof(counts));
    if (!xp3_read_layout(format, &layout) ||
        !xp3_load_index(format, &layout, &index) ||
        !xp3_walk(index, layout.index_size, &layout, NULL, &counts, pd))
        goto done;
    if (counts.extent < layout.index_end) counts.extent = layout.index_end;
    if (layout_out) *layout_out = layout;
    if (counts_out) *counts_out = counts;
    if (!result) {
        ok = true;
        goto done;
    }
    stream = (xp3_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto done;
    if (counts.members) {
        size_t longest = 0U, i;
        stream->members =
            (xp3_member *)xx_mem_calloc(counts.members, sizeof(xp3_member));
        stream->segments = (xp3_segment *)xx_mem_calloc(counts.segments,
                                                        sizeof(xp3_segment));
        stream->pool = (char *)xx_mem_alloc(counts.name_bytes);
        stream->pool_size = counts.name_bytes;
        if (!stream->members || !stream->segments || !stream->pool ||
            !xp3_walk(index, layout.index_size, &layout, stream, &counts, pd) ||
            stream->count != counts.members || !xp3_mark_duplicates(stream))
            goto done;
        for (i = 0U; i < stream->count; ++i)
            if (stream->members[i].name_length > longest)
                longest = stream->members[i].name_length;
        stream->name = (char *)xx_mem_alloc(longest + XP3_SUFFIX_ROOM + 1U);
        if (!stream->name) goto done;
    }
    *result = stream;
    stream = NULL;
    ok = true;
done:
    if (index) xx_mem_free(index);
    if (stream) xp3_stream_free(stream);
    return ok;
}

/* Build the current member's final name: the pool name, with "%_<n>"
 * inserted before the extension of the last component for a duplicate. */
static void xp3_build_name(xp3_stream *stream, size_t index) {
    const xp3_member *member = &stream->members[index];
    const char *name = stream->pool + member->name_at;
    size_t length = member->name_length, dot = length, component = 0U, at,
           suffix_length = 0U;
    char suffix[XP3_SUFFIX_ROOM], digits[12];
    size_t digit_count = 0U;
    uint32_t n = member->suffix;
    xx_rt_memcpy(stream->name, name, length);
    stream->name[length] = 0;
    if (!n) return;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(n % 10U));
        n /= 10U;
    } while (n != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    xx_rt_memcpy(stream->name, name, dot);
    xx_rt_memcpy(stream->name + dot, suffix, suffix_length);
    xx_rt_memcpy(stream->name + dot + suffix_length, name + dot, length - dot);
    stream->name[length + suffix_length] = 0;
}

/* ---- extraction --------------------------------------------------------- */

/* A write-only device that forwards to `target` (or discards) and fails as
 * soon as more than `limit` bytes arrive. */
typedef struct xp3_limit_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} xp3_limit;

static ssize_t xp3_limit_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    xp3_limit *limit = (xp3_limit *)self;
    if (size > (SIZE_MAX >> 1) ||
        (uint64_t)size > limit->limit - limit->written)
        return -1;
    if (!xp3_write_all(limit->target, (const uint8_t *)buffer, size))
        return -1;
    limit->written += (uint64_t)size;
    return (ssize_t)size;
}

static bool xp3_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    const size_t capacity = xp3_capacity();
    uint8_t *buffer;
    uint64_t done = 0U;
    bool ok = true;
    if (size == 0U) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (done < size) {
        size_t chunk = size - done > (uint64_t)capacity ? capacity
                                                        : (size_t)(size - done);
        if (xp3_stopped(pd) ||
            !xp3_read_at(source, offset + (int64_t)done, buffer, chunk) ||
            !xp3_write_all(destination, buffer, chunk)) {
            ok = false;
            break;
        }
        done += chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

static bool xp3_unpack_member(Abstractformat *format, const xp3_stream *stream,
                              const xp3_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    size_t index;
    for (index = 0U; index < member->segment_count; ++index) {
        const xp3_segment *segment =
            &stream->segments[member->segment_first + index];
        int64_t at = format->base_address + (int64_t)segment->offset;
        if (xp3_stopped(pd)) return false;
        if ((segment->flags & XP3_METHOD_MASK) == XP3_METHOD_STORED) {
            if (!xp3_copy_range(format->device, at, segment->original,
                                destination, pd))
                return false;
        } else {
            uint8_t zhead[2];
            xp3_limit limit;
            if (segment->original == 0U) continue;
            if (!xp3_read_at(format->device, at, zhead, 2U) ||
                !xx_zlib_stream_header_is_valid(zhead, 2U))
                return false;
            xx_mem_zero(&limit, sizeof(limit));
            limit.device.write = xp3_limit_write;
            limit.target = destination;
            limit.limit = segment->original;
            if (!xx_deflate_unpack_device(format->device, at + 2,
                                          (int64_t)segment->packed - 2,
                                          &limit.device, false, pd) ||
                limit.written != segment->original)
                return false;
        }
    }
    return true;
}

/* ---- records ------------------------------------------------------------ */

static bool xp3_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *xp3_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool xp3_set_record(Abstractformat *format, xx_archive_record *record,
                           xp3_stream *stream, size_t index) {
    const xp3_member *member = &stream->members[index];
    const xp3_segment *first = &stream->segments[member->segment_first];
    xp3_build_name(stream, index);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = format->base_address + (int64_t)first->offset;
    record->compressed_size = (int64_t)member->packed;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->has_zlib ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->flags != 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API --------------------------------------------------------- */

void xx_xp3_init(xx_xp3 *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_XP3_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "xp3");
    archive->format.check_is_valid = xx_xp3_check_is_valid;
    archive->format.handle_base_info = xx_xp3_handle_base_info;
    archive->format.get_format_size = xx_xp3_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_xp3_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_xp3_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_xp3_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_xp3_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_xp3_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_xp3_free_archive_records_reading;
    archive->index_offset = -1;
}

xx_xp3 *xx_xp3_create(xx_io_device *device, int64_t base_address) {
    xx_xp3 *archive = (xx_xp3 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_xp3_init(archive, device, base_address);
    return archive;
}

void xx_xp3_destroy(xx_xp3 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_xp3_free(xx_xp3 *archive) {
    if (!archive) return;
    xx_xp3_destroy(archive);
    xx_mem_free(archive);
}

bool xx_xp3_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return xp3_open_stream(format, NULL, NULL, NULL, pd);
}

bool xx_xp3_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xp3_layout layout;
    xp3_counts counts;
    xx_xp3 *archive;
    if (!xp3_open_stream(format, NULL, &layout, &counts, pd)) return false;
    archive = (xx_xp3 *)format;
    archive->number_of_records = counts.members;
    archive->index_offset = format->base_address + layout.index_at;
    archive->index_size = (int64_t)layout.index_size;
    archive->index_packed = layout.method == XP3_METHOD_ZLIB;
    archive->version = layout.version;
    format->number_of_archive_records = counts.members;
    format->format_size = counts.extent;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_xp3_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xp3_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_xp3_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xp3_handle_base_info(format, pd))
               ? ((xx_xp3 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_xp3_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xp3_stream *stream;
    xx_archive_record_state *state;
    if (!xp3_open_stream(format, &stream, NULL, NULL, pd)) return NULL;
    if (!stream->count) {
        xp3_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xp3_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xp3_stream_free;
    state->total_records = stream->count;
    if (!xp3_copy_options(&state->options, options) ||
        !xp3_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_xp3_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_xp3_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xp3_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (xp3_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = xp3_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_xp3_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xp3_stream *stream;
    const xp3_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xp3_stream *)state->internal_state) ||
        stream->index >= stream->count || xp3_stopped(pd))
        return false;
    member = &stream->members[stream->index];
    path_option = xp3_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode into nothing, which verifies the member. */
        return xp3_unpack_member(format, stream, member, NULL, pd);
    if (member->unsafe || !xp3_safe_name(stream->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
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
        if (!destination) goto done;
        created = true;
        result = xp3_unpack_member(format, stream, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_xp3_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
