/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Wise Installation System installers (Wise16 NE and Wise32 PE stubs).
 * xx_wise_installation_system.h has the overview.
 *
 * Locating the payload.  The stub is recognised by the NUL-delimited
 * "WiseMain" import name inside its image; the image end (PE: furthest
 * section raw data; NE: segments with their relocations, resources and
 * the non-resident name table) is where the Wise header starts.  Header
 * sizes measured on the corpus, relative to the image end:
 *
 *   0x1E  NE images ending at 0x3C20/0x3E10/0x3E50 (oldest Wise16)
 *   0x22  NE 0x3C30, PK mode
 *   0x40  NE 0x3660, PK mode
 *   0x51  NE 0x3780..0x37D0, PE 0x3000/0x6E00
 *   0x5C + byte[0x5B]  NE 0x3BD0, PE 0x3800/0x3A00: a length-prefixed block
 *         of dialog font strings ("MS Sans Serif") closes the header
 *
 * Those offsets are tried first; a bounded scan of the next 16 KiB covers
 * any other revision.  A native chain is accepted only when its first
 * raw-DEFLATE stream is followed by the CRC-32 of what it decodes to; a PK
 * chain only when its local headers run back to back into a central
 * directory whose entries all point at them (absolute offsets) and whose
 * end record is consistent.
 *
 * The walk itself (CRC after each stream, up to three zero bytes before
 * it, the unindexed first member of the PK mode) follows XArchive's
 * installers/xwisesfxarchive.cpp (MIT, hors); the header table, the NE
 * image end and the recovery of names from the script are written here
 * from the layouts measured on the corpus.
 *
 * Names.  The script (the first or second member) holds, for every
 * installed file, a record with its (start, end) byte range relative to
 * the first installed stream - the range covers the stream and its CRC -
 * followed at a version-dependent distance (40 bytes in Wise32, 16 in the
 * oldest Wise16) by the destination path "%VAR%\\dir\\file".  The base is
 * found by voting over every (start, end) pair whose length equals some
 * member's length; the distance to the path by voting over the matched
 * records.  "%MAINDIR%\\a\\b.txt" becomes "MAINDIR/a/b.txt"; a path that
 * would escape the output directory is not used.  A Wise patch stream
 * (0x01, then the record's DOS date and time) gets ".Patch" appended, as
 * E_WISE does.  Members without a record are named "WiseColors.dib",
 * "WiseScript.bin" or "entry_<index>.<ext>"; PK-mode members keep their
 * local-header names.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wise_installation_system/xx_wise_installation_system.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "../../algo/deflate/xx_deflate_internal.h"

#include <limits.h>

/* Registration placeholder: picks up the real file type as soon as the
 * enumerator is registered in xxfc_defs.h. */
#ifdef WISE_INSTALLATION_SYSTEM
#define XX_WISE_INSTALLATION_SYSTEM_FILE_TYPE \
    XX_FILE_TYPE_WISE_INSTALLATION_SYSTEM
#else
#define XX_WISE_INSTALLATION_SYSTEM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define WISE_DOS_HEADER 64U
#define WISE_MIN_FILE 0x400
/* The stub images measured end at 0x3000..0x6E00; anything past this is
 * not a Wise stub. */
#define WISE_MAX_IMAGE 0x40000
#define WISE_MAX_SECTIONS 96U
#define WISE_MAX_SEGMENTS 1024U
#define WISE_SCAN 0x4000U
#define WISE_HEAD (WISE_SCAN + 0x1000U)
#define WISE_MARKER_CHUNK 0x8000U
/* The first member is the colour DIB or the script: the corpus peaks at
 * 16824 bytes.  Candidates are decoded up to WISE_FIRST_QUICK, and at most
 * WISE_MAX_BIG_ATTEMPTS of them again up to WISE_FIRST_RAW_MAX. */
#define WISE_FIRST_QUICK (64U * 1024U)
#define WISE_FIRST_RAW_MAX (1024U * 1024U)
#define WISE_MAX_ATTEMPTS 64U
#define WISE_MAX_BIG_ATTEMPTS 4U
/* Sizes in the script and in the local headers are u32. */
#define WISE_MEMBER_RAW_MAX UINT64_C(0xFFFFFFFF)
#define WISE_WALK_RATIO 64U
#define WISE_WALK_EXTRA (UINT64_C(64) * 1024U * 1024U)
#define WISE_MAX_MEMBERS 65536U
#define WISE_WINDOW (1024U * 1024U)
#define WISE_SCRIPT_MAX (4U * 1024U * 1024U)
#define WISE_SCRIPT_CANDIDATES 3U
#define WISE_MAX_MATCHES 262144U
#define WISE_VOTE_SLOTS 4096U
/* Voting work: members sharing one span that a single (start, end) pair
 * may vote for, and pair/member checks per script in total.  Real scripts
 * need a few thousand checks; a crafted one (thousands of identical
 * members, a script full of pairs of that span) would need billions. */
#define WISE_VOTE_SAME_SPAN 64U
#define WISE_VOTE_WORK (8U * 1024U * 1024U)
#define WISE_NAME_DISTANCE_MIN 8U
#define WISE_NAME_DISTANCE_MAX 64U
#define WISE_PATH_MAX 260U
#define WISE_RENAME_PASSES 3U
#define WISE_EOCD_SCAN (65535 + 22)
#define WISE_CD_MAX (16 * 1024 * 1024)
#define WISE_PEEK 4096U

typedef struct wise_member_s {
    char *name;               /* UTF-8, '/' separated */
    int64_t header_offset;    /* relative to base */
    int64_t header_size;
    int64_t data_offset;      /* relative to base */
    int64_t packed_size;      /* DEFLATE (or stored) bytes */
    int64_t span;             /* data_offset to the next member */
    uint64_t raw_size;
    uint32_t crc32;
    uint16_t method;          /* 0 stored, 8 deflate */
    uint16_t dos_time;
    uint16_t dos_date;
    bool has_time;
    bool extractable;
    uint8_t head[8];          /* first decoded bytes, for naming */
    uint8_t head_size;
} wise_member;

typedef struct wise_table_s {
    unsigned refs;
    wise_member *items;
    size_t count;
    size_t capacity;
    int64_t overlay;
    int64_t payload;
    int64_t chain_end;
    uint64_t named;
    bool is_ne;
    bool pk_mode;
    bool truncated;
} wise_table;

typedef struct wise_cursor_s {
    wise_table *table;
    size_t index;
} wise_cursor;

typedef struct wise_found_s {
    int64_t total;   /* bytes from base to the end of the device */
    int64_t overlay; /* image end, relative */
    int64_t first;   /* first member (stream or local header), relative */
    bool is_ne;
    bool pk_mode;
} wise_found;

typedef struct wise_inflated_s {
    int64_t consumed;
    uint64_t raw_size;
    uint32_t crc32;
    uint8_t head[8];
    uint8_t head_size;
    bool over_limit;   /* the output limit stopped the decode */
} wise_inflated;

typedef struct wise_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
    uint32_t crc32;
    uint8_t head[8];
    uint8_t head_size;
    bool failed;
    bool over_limit;
} wise_sink;

typedef struct wise_window_s {
    xx_io_device *device;
    int64_t base;
    int64_t limit;     /* relative end of the data */
    uint8_t *buffer;
    size_t capacity;
    int64_t start;     /* relative */
    size_t size;
} wise_window;

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint16_t wise_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t wise_le32(const uint8_t *bytes) {
    return (uint32_t)wise_le16(bytes) |
           ((uint32_t)wise_le16(bytes + 2U) << 16U);
}

static bool wise_range(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static bool wise_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || offset < 0 || (size != 0U && !buffer)) return false;
    if (size == 0U) return true;
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (got <= 0) return false;
        done += (size_t)got;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Inflate through the library's RFC 1951 engine, keeping the exact number */
/* of source bytes consumed (the CRC follows the last one).                */

static ssize_t wise_sink_write(xx_io_device *device, const void *data,
                               size_t size) {
    wise_sink *sink = device ? (wise_sink *)device->priv : NULL;
    size_t copied = 0U;
    if (!sink || sink->failed || (size != 0U && !data)) {
        if (sink) sink->failed = true;
        return -1;
    }
    if ((uint64_t)size > sink->limit - sink->written) {
        sink->failed = true;
        sink->over_limit = true;
        return -1;
    }
    if (sink->target && size != 0U &&
        xx_io_write(sink->target, data, size) != (ssize_t)size) {
        sink->failed = true;
        return -1;
    }
    while (sink->head_size < sizeof(sink->head) && copied < size)
        sink->head[sink->head_size++] = ((const uint8_t *)data)[copied++];
    sink->crc32 = xx_crc32_calc(sink->crc32, data, size);
    sink->written += (uint64_t)size;
    return (ssize_t)size;
}

static int64_t wise_sink_size(xx_io_device *device) {
    const wise_sink *sink = device ? (const wise_sink *)device->priv : NULL;
    return sink && sink->written <= (uint64_t)INT64_MAX
               ? (int64_t)sink->written
               : -1;
}

static void wise_sink_init(wise_sink *sink, xx_io_device *target,
                           uint64_t limit) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->limit = limit;
    sink->device.write = wise_sink_write;
    sink->device.total_size = wise_sink_size;
    sink->device.get_total_size = wise_sink_size;
    sink->device.size = wise_sink_size;
    sink->device.priv = sink;
}

static void wise_sink_result(const wise_sink *sink, int64_t consumed,
                             wise_inflated *result) {
    result->consumed = consumed;
    result->raw_size = sink->written;
    result->crc32 = sink->crc32;
    xx_rt_memcpy(result->head, sink->head, sizeof(result->head));
    result->head_size = sink->head_size;
}

/* Inflates from memory.  *exhausted is set when the decoder asked for
 * bytes past @p size: the outcome is then only final if nothing follows
 * the buffer in the file. */
static bool wise_inflate_memory(const uint8_t *data, size_t size,
                                xx_io_device *target, uint64_t limit,
                                wise_inflated *result, bool *exhausted,
                                xx_pd_struct *pd) {
    xx_bit_reader reader;
    wise_sink sink;
    uint64_t bits;
    bool ok;
    *exhausted = false;
    result->over_limit = false;
    if (!data || size == 0U || size > (size_t)INT64_MAX / 8U ||
        !xx_br_init(&reader, NULL, data, size, (int64_t)size))
        return false;
    wise_sink_init(&sink, target, limit);
    ok = xx_deflate_decompress_stream(&reader, &sink.device, NULL, 0U, NULL,
                                      false, pd);
    *exhausted = reader.eof;
    if (reader.bit_count < 0 || reader.mem_pos > size ||
        (uint64_t)reader.bit_count > (uint64_t)reader.mem_pos * 8U)
        ok = false;
    if (ok) {
        bits = (uint64_t)reader.mem_pos * 8U - (uint64_t)reader.bit_count;
        if (bits == 0U || (bits + 7U) / 8U > (uint64_t)size) ok = false;
        else wise_sink_result(&sink, (int64_t)((bits + 7U) / 8U), result);
    }
    if (sink.failed) ok = false;
    result->over_limit = sink.over_limit;
    xx_br_free(&reader);
    return ok;
}

static bool wise_inflate_device(xx_io_device *device, int64_t offset,
                                int64_t available, xx_io_device *target,
                                uint64_t limit, wise_inflated *result,
                                xx_pd_struct *pd) {
    xx_bit_reader reader;
    wise_sink sink;
    uint64_t loaded, unread;
    bool ok;
    result->over_limit = false;
    if (!device || offset < 0 || available <= 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0 ||
        !xx_br_init(&reader, device, NULL, 0U, available))
        return false;
    wise_sink_init(&sink, target, limit);
    ok = xx_deflate_decompress_stream(&reader, &sink.device, NULL, 0U, NULL,
                                      false, pd);
    if (reader.remaining_input < 0 || reader.remaining_input > available ||
        reader.buffer_pos > reader.buffer_len || reader.bit_count < 0) {
        ok = false;
    } else if (ok) {
        loaded = (uint64_t)(available - reader.remaining_input);
        unread = (uint64_t)(reader.buffer_len - reader.buffer_pos) +
                 (uint64_t)(reader.bit_count / 8);
        if (sink.failed || unread >= loaded ||
            loaded - unread > (uint64_t)available)
            ok = false;
        else
            wise_sink_result(&sink, (int64_t)(loaded - unread), result);
    }
    result->over_limit = sink.over_limit;
    xx_br_free(&reader);
    return ok;
}

/* Up to eight decoded bytes of the stream at @p data, from memory: true
 * when the stream is well formed that far (or ends earlier, intact). */
static bool wise_quick_peek(const uint8_t *data, size_t size, uint8_t *out,
                            size_t *produced) {
    xx_bit_reader reader;
    size_t written = 0U;
    bool ok;
    *produced = 0U;
    if (!data || size == 0U ||
        !xx_br_init(&reader, NULL, data, size, (int64_t)size))
        return false;
    ok = xx_deflate_decompress_stream(&reader, NULL, out, 8U, &written,
                                      false, NULL);
    xx_br_free(&reader);
    *produced = written;
    return ok || written == 8U;
}

static bool wise_window_load(wise_window *window, int64_t position) {
    int64_t left = window->limit - position;
    size_t want;
    if (position < 0 || left <= 0) return false;
    want = left < (int64_t)window->capacity ? (size_t)left : window->capacity;
    if (!wise_read_at(window->device, window->base + position, window->buffer,
                      want)) {
        window->size = 0U;
        return false;
    }
    window->start = position;
    window->size = want;
    return true;
}

/* Inflates the stream at @p position (output measured, not kept) from the
 * window, reloading it when the stream starts outside or too close to its
 * end, and falling back to a streaming decode for a stream longer than
 * the window. */
static bool wise_window_inflate(wise_window *window, int64_t position,
                                uint64_t limit, wise_inflated *result,
                                xx_pd_struct *pd) {
    int64_t window_end = window->start + (int64_t)window->size;
    size_t slack = window->capacity / 8U;
    size_t offset;
    bool exhausted = false;
    bool ok;
    result->over_limit = false;
    if (position < 0 || position >= window->limit) return false;
    if (window->size == 0U || position < window->start ||
        position >= window_end ||
        (window_end - position < (int64_t)slack && window_end < window->limit)) {
        if (!wise_window_load(window, position)) return false;
        window_end = window->start + (int64_t)window->size;
    }
    offset = (size_t)(position - window->start);
    ok = wise_inflate_memory(window->buffer + offset, window->size - offset,
                             NULL, limit, result, &exhausted, pd);
    if (!exhausted || window_end >= window->limit) return ok;
    return wise_inflate_device(window->device, window->base + position,
                               window->limit - position, NULL, limit, result,
                               pd);
}

/* The CRC-32 after a stream, possibly behind up to three zero bytes.
 * Returns the padding, or -1. */
static int wise_find_crc(xx_io_device *device, int64_t base, int64_t limit,
                         int64_t at, uint32_t crc32) {
    uint8_t bytes[7];
    size_t available;
    size_t pad;
    if (at < 0 || at > limit || limit - at < 4) return -1;
    available = limit - at < 7 ? (size_t)(limit - at) : 7U;
    if (!wise_read_at(device, base + at, bytes, available)) return -1;
    for (pad = 0U; pad <= 3U && pad + 4U <= available; ++pad) {
        if (pad > 0U && bytes[pad - 1U] != 0U) break;
        if (wise_le32(bytes + pad) == crc32) return (int)pad;
    }
    return -1;
}

/* ---------------------------------------------------------------------- */
/* The stub                                                                */

static bool wise_pe_image_end(xx_io_device *device, int64_t base,
                              int64_t total, int64_t nt, int64_t *end) {
    uint8_t header[24];
    uint8_t optional[64];
    uint8_t *sections;
    uint16_t count, optional_size, magic;
    int64_t table, image_end;
    uint16_t index;
    if (!wise_range(total, nt, 24 + 64) ||
        !wise_read_at(device, base + nt, header, sizeof(header)) ||
        !wise_read_at(device, base + nt + 24, optional, sizeof(optional)))
        return false;
    count = wise_le16(header + 6U);
    optional_size = wise_le16(header + 20U);
    magic = wise_le16(optional);
    if (count == 0U || count > WISE_MAX_SECTIONS || optional_size < 64U ||
        (magic != 0x010BU && magic != 0x020BU))
        return false;
    image_end = (int64_t)wise_le32(optional + 60U); /* SizeOfHeaders */
    table = nt + 24 + optional_size;
    if (!wise_range(total, table, (int64_t)count * 40)) return false;
    sections = (uint8_t *)xx_mem_alloc((size_t)count * 40U);
    if (!sections) return false;
    if (!wise_read_at(device, base + table, sections, (size_t)count * 40U)) {
        xx_mem_free(sections);
        return false;
    }
    for (index = 0U; index < count; ++index) {
        const uint8_t *section = sections + (size_t)index * 40U;
        int64_t raw_size = (int64_t)wise_le32(section + 16U);
        int64_t raw_offset = (int64_t)wise_le32(section + 20U);
        if (raw_size == 0) continue;
        if (!wise_range(total, raw_offset, raw_size)) {
            xx_mem_free(sections);
            return false;
        }
        if (raw_offset + raw_size > image_end) image_end = raw_offset + raw_size;
    }
    xx_mem_free(sections);
    *end = image_end;
    return true;
}

static bool wise_ne_image_end(xx_io_device *device, int64_t base,
                              int64_t total, int64_t ne, int64_t *end) {
    uint8_t header[64];
    uint8_t *table = NULL;
    uint16_t segment_count, nonresident_size, shift;
    uint32_t segment_table, resource_table, resident_names;
    int64_t nonresident_offset, image_end;
    bool ok = false;
    uint32_t index;
    if (!wise_range(total, ne, 64) ||
        !wise_read_at(device, base + ne, header, sizeof(header)) ||
        header[0] != 'N' || header[1] != 'E')
        return false;
    segment_count = wise_le16(header + 0x1CU);
    nonresident_size = wise_le16(header + 0x20U);
    segment_table = (uint32_t)ne + wise_le16(header + 0x22U);
    resource_table = (uint32_t)ne + wise_le16(header + 0x24U);
    resident_names = (uint32_t)ne + wise_le16(header + 0x26U);
    nonresident_offset = (int64_t)wise_le32(header + 0x2CU);
    shift = wise_le16(header + 0x32U);
    if (shift == 0U) shift = 9U;
    if (shift > 15U || segment_count > WISE_MAX_SEGMENTS) return false;
    image_end = ne + 64;
    if (nonresident_size != 0U) {
        if (!wise_range(total, nonresident_offset, nonresident_size))
            return false;
        if (nonresident_offset + nonresident_size > image_end)
            image_end = nonresident_offset + nonresident_size;
    }
    if (segment_count != 0U) {
        if (!wise_range(total, segment_table, (int64_t)segment_count * 8))
            return false;
        table = (uint8_t *)xx_mem_alloc((size_t)segment_count * 8U);
        if (!table ||
            !wise_read_at(device, base + segment_table, table,
                          (size_t)segment_count * 8U))
            goto done;
        for (index = 0U; index < segment_count; ++index) {
            const uint8_t *segment = table + (size_t)index * 8U;
            int64_t start = (int64_t)wise_le16(segment) << shift;
            int64_t length = wise_le16(segment + 2U);
            uint16_t flags = wise_le16(segment + 4U);
            int64_t stop;
            if (wise_le16(segment) == 0U) continue;
            if (length == 0) length = 0x10000;
            if (!wise_range(total, start, length)) goto done;
            stop = start + length;
            if (flags & 0x0100U) {
                uint8_t count_bytes[2];
                int64_t relocations;
                if (!wise_range(total, stop, 2) ||
                    !wise_read_at(device, base + stop, count_bytes, 2U))
                    goto done;
                relocations = (int64_t)wise_le16(count_bytes) * 8;
                if (!wise_range(total, stop, 2 + relocations)) goto done;
                stop += 2 + relocations;
            }
            if (stop > image_end) image_end = stop;
        }
        xx_mem_free(table);
        table = NULL;
    }
    if (resident_names > resource_table) {
        uint32_t size = resident_names - resource_table;
        uint16_t resource_shift;
        size_t at = 2U;
        if (!wise_range(total, resource_table, size) || size < 2U) goto done;
        table = (uint8_t *)xx_mem_alloc(size);
        if (!table || !wise_read_at(device, base + resource_table, table, size))
            goto done;
        resource_shift = wise_le16(table);
        if (resource_shift > 15U) goto done;
        while (at + 2U <= size && wise_le16(table + at) != 0U) {
            uint16_t count;
            uint16_t item;
            if (at + 8U > size) goto done;
            count = wise_le16(table + at + 2U);
            at += 8U;
            for (item = 0U; item < count; ++item) {
                int64_t start, length;
                if (at + 12U > size) goto done;
                start = (int64_t)wise_le16(table + at) << resource_shift;
                length = (int64_t)wise_le16(table + at + 2U) << resource_shift;
                if (!wise_range(total, start, length)) goto done;
                if (start + length > image_end) image_end = start + length;
                at += 12U;
            }
        }
    }
    *end = image_end;
    ok = true;
done:
    if (table) xx_mem_free(table);
    return ok;
}

/* The stub imports WiseMain from WISE0001.DLL by name: "\0WiseMain\0". */
static bool wise_has_marker(xx_io_device *device, int64_t base,
                            int64_t image_end) {
    static const uint8_t marker[10] = {0, 'W', 'i', 's', 'e',
                                       'M', 'a', 'i', 'n', 0};
    uint8_t *buffer;
    int64_t position = 0;
    size_t keep = 0U;
    bool found = false;
    buffer = (uint8_t *)xx_mem_alloc(WISE_MARKER_CHUNK + sizeof(marker));
    if (!buffer) return false;
    while (!found && position < image_end) {
        size_t chunk = image_end - position < (int64_t)WISE_MARKER_CHUNK
                           ? (size_t)(image_end - position)
                           : WISE_MARKER_CHUNK;
        size_t have, at;
        if (!wise_read_at(device, base + position, buffer + keep, chunk)) break;
        have = keep + chunk;
        for (at = 0U; at + sizeof(marker) <= have; ++at) {
            if (buffer[at] == 0U && buffer[at + 1U] == 'W' &&
                xx_rt_memcmp(buffer + at, marker, sizeof(marker)) == 0) {
                found = true;
                break;
            }
        }
        keep = have < sizeof(marker) - 1U ? have : sizeof(marker) - 1U;
        xx_rt_memmove(buffer, buffer + have - keep, keep);
        position += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return found;
}

/* ---------------------------------------------------------------------- */
/* Member table                                                            */

static void wise_table_release(wise_table *table) {
    size_t index;
    if (!table || --table->refs != 0U) return;
    for (index = 0U; index < table->count; ++index)
        if (table->items[index].name) xx_mem_free(table->items[index].name);
    if (table->items) xx_mem_free(table->items);
    xx_mem_free(table);
}

static wise_member *wise_table_add(wise_table *table) {
    wise_member *member;
    if (table->count >= WISE_MAX_MEMBERS) return NULL;
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2U : 32U;
        wise_member *grown;
        if (capacity > WISE_MAX_MEMBERS) capacity = WISE_MAX_MEMBERS;
        grown = (wise_member *)xx_mem_realloc(table->items,
                                              capacity * sizeof(*grown));
        if (!grown) return NULL;
        table->items = grown;
        table->capacity = capacity;
    }
    member = &table->items[table->count++];
    xx_mem_zero(member, sizeof(*member));
    member->extractable = true;
    return member;
}

/* ---------------------------------------------------------------------- */
/* PK mode                                                                 */

static int wise_offset_compare(const void *left, const void *right) {
    int64_t a = *(const int64_t *)left, b = *(const int64_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static bool wise_offset_known(const int64_t *offsets, size_t count,
                              int64_t value) {
    size_t low = 0U, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (offsets[middle] == value) return true;
        if (offsets[middle] < value) low = middle + 1U;
        else high = middle;
    }
    return false;
}

/* Walks the local headers from @p first, then checks the central
 * directory and its end record.  Fills @p table when given. */
static bool wise_pk_walk(xx_io_device *device, int64_t base, int64_t total,
                         int64_t first, wise_table *table,
                         int64_t *chain_end) {
    int64_t *offsets = NULL;
    size_t count = 0U, capacity = 0U;
    uint8_t *tail = NULL;
    uint8_t *directory = NULL;
    int64_t position = first, scan_start, eocd = -1, cd_offset, cd_size;
    size_t tail_size, at;
    uint16_t entries;
    uint16_t comment;
    bool ok = false;
    while (count < WISE_MAX_MEMBERS && wise_range(total, position, 30)) {
        uint8_t header[30];
        uint16_t flags, method, name_size, extra_size;
        int64_t packed, raw, header_size;
        if (!wise_read_at(device, base + position, header, sizeof(header)))
            goto done;
        if (wise_le32(header) != 0x04034B50U) break;
        flags = wise_le16(header + 6U);
        method = wise_le16(header + 8U);
        packed = (int64_t)wise_le32(header + 18U);
        raw = (int64_t)wise_le32(header + 22U);
        name_size = wise_le16(header + 26U);
        extra_size = wise_le16(header + 28U);
        header_size = 30 + (int64_t)name_size + extra_size;
        if ((flags & 0x0009U) != 0U || (method != 0U && method != 8U) ||
            (method == 0U && packed != raw) ||
            !wise_range(total, position, header_size) ||
            !wise_range(total, position + header_size, packed))
            goto done;
        if (count == capacity) {
            size_t grown_capacity = capacity ? capacity * 2U : 64U;
            int64_t *grown = (int64_t *)xx_mem_realloc(
                offsets, grown_capacity * sizeof(*grown));
            if (!grown) goto done;
            offsets = grown;
            capacity = grown_capacity;
        }
        offsets[count++] = position;
        if (table) {
            wise_member *member = wise_table_add(table);
            if (!member) goto done;
            member->header_offset = position;
            member->header_size = header_size;
            member->data_offset = position + header_size;
            member->packed_size = packed;
            member->span = packed;
            member->raw_size = (uint64_t)raw;
            member->crc32 = wise_le32(header + 14U);
            member->method = method;
            member->dos_time = wise_le16(header + 10U);
            member->dos_date = wise_le16(header + 12U);
            member->has_time = true;
            if (name_size != 0U) {
                member->name = (char *)xx_mem_alloc((size_t)name_size + 1U);
                if (!member->name ||
                    !wise_read_at(device, base + position + 30, member->name,
                                  name_size))
                    goto done;
                member->name[name_size] = 0;
            }
        }
        position += header_size + packed;
    }
    if (count < 2U) goto done;

    /* The end record sits in the last 64 KiB + 22 bytes (it may be
     * followed by other data, e.g. a signature). */
    scan_start = total - WISE_EOCD_SCAN > position ? total - WISE_EOCD_SCAN
                                                   : position;
    if (total - scan_start < 22) goto done;
    tail_size = (size_t)(total - scan_start);
    tail = (uint8_t *)xx_mem_alloc(tail_size);
    if (!tail || !wise_read_at(device, base + scan_start, tail, tail_size))
        goto done;
    for (at = tail_size - 22U + 1U; at-- > 0U;) {
        if (wise_le32(tail + at) == 0x06054B50U &&
            at + 22U + wise_le16(tail + at + 20U) <= tail_size) {
            eocd = scan_start + (int64_t)at;
            break;
        }
    }
    if (eocd < 0) goto done;
    at = (size_t)(eocd - scan_start);
    entries = wise_le16(tail + at + 10U);
    cd_size = (int64_t)wise_le32(tail + at + 12U);
    cd_offset = (int64_t)wise_le32(tail + at + 16U);
    comment = wise_le16(tail + at + 20U);
    /* Absolute offsets; the unindexed first member(s) are not counted. */
    if (wise_le16(tail + at + 4U) != 0U || wise_le16(tail + at + 6U) != 0U ||
        wise_le16(tail + at + 8U) != entries || entries == 0U ||
        entries > count || count - entries > 4U || cd_offset != position ||
        cd_size != eocd - cd_offset || cd_size > WISE_CD_MAX)
        goto done;
    directory = (uint8_t *)xx_mem_alloc((size_t)cd_size);
    if (!directory ||
        !wise_read_at(device, base + cd_offset, directory, (size_t)cd_size))
        goto done;
    xx_rt_qsort(offsets, count, sizeof(*offsets), wise_offset_compare);
    {
        size_t cursor = 0U;
        uint32_t seen = 0U;
        while (cursor < (size_t)cd_size) {
            size_t record;
            if ((size_t)cd_size - cursor < 46U ||
                wise_le32(directory + cursor) != 0x02014B50U)
                goto done;
            record = 46U + (size_t)wise_le16(directory + cursor + 28U) +
                     wise_le16(directory + cursor + 30U) +
                     wise_le16(directory + cursor + 32U);
            if (record > (size_t)cd_size - cursor ||
                !wise_offset_known(offsets, count,
                                   (int64_t)wise_le32(directory + cursor +
                                                      42U)))
                goto done;
            cursor += record;
            if (++seen > entries) goto done;
        }
        if (seen != entries) goto done;
    }
    *chain_end = eocd + 22 + comment;
    ok = true;
done:
    if (offsets) xx_mem_free(offsets);
    if (tail) xx_mem_free(tail);
    if (directory) xx_mem_free(directory);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Locating the payload                                                    */

/* The first native stream at @p position: decodes (at most @p limit
 * bytes), is followed by its CRC-32, and a second well-formed stream
 * follows. */
static bool wise_native_first(wise_window *window, int64_t position,
                              uint64_t limit, bool *over_limit,
                              xx_pd_struct *pd) {
    wise_inflated first;
    uint8_t peek[WISE_PEEK];
    uint8_t out[8];
    size_t produced, available;
    int64_t next;
    int pad;
    bool ok = wise_window_inflate(window, position, limit, &first, pd);
    *over_limit = !ok && first.over_limit;
    if (!ok || first.raw_size == 0U) return false;
    pad = wise_find_crc(window->device, window->base, window->limit,
                        position + first.consumed, first.crc32);
    if (pad < 0) return false;
    next = position + first.consumed + pad + 4;
    if (next >= window->limit) return false;
    available = window->limit - next < (int64_t)sizeof(peek)
                    ? (size_t)(window->limit - next)
                    : sizeof(peek);
    if (!wise_read_at(window->device, window->base + next, peek, available))
        return false;
    return wise_quick_peek(peek, available, out, &produced);
}

/* Every colour table and script seen starts with a small, even u16
 * record/header size; used only to thin out the fallback scan. */
static bool wise_plausible_first(const uint8_t *head, size_t size) {
    uint8_t out[8];
    size_t produced;
    if (!wise_quick_peek(head, size, out, &produced) || produced < 4U)
        return false;
    return out[1] == 0U && out[0] >= 4U && out[0] <= 0xA0U &&
           (out[0] & 1U) == 0U;
}

typedef struct wise_effort_s {
    unsigned attempts;      /* first-member decodes tried */
    unsigned big_attempts;  /* of those, retried at the full limit */
} wise_effort;

static bool wise_try_candidate(xx_io_device *device, int64_t base,
                               int64_t total, wise_window *window,
                               const uint8_t *head, size_t head_size,
                               size_t offset, bool scanning,
                               wise_effort *effort, wise_found *found,
                               xx_pd_struct *pd) {
    int64_t position = found->overlay + (int64_t)offset;
    int64_t chain_end;
    bool over_limit = false;
    bool ok;
    if (offset + 8U > head_size) return false;
    if (head[offset] == 'P' && head[offset + 1U] == 'K' &&
        head[offset + 2U] == 3U && head[offset + 3U] == 4U) {
        if (!wise_pk_walk(device, base, total, position, NULL, &chain_end))
            return false;
        found->first = position;
        found->pk_mode = true;
        return true;
    }
    /* A raw-DEFLATE stream never opens with the reserved block type. */
    if (((head[offset] >> 1U) & 3U) == 3U) return false;
    if (scanning && !wise_plausible_first(head + offset, head_size - offset))
        return false;
    /* Bounded effort: every colour table and script measured decodes to
     * less than WISE_FIRST_QUICK bytes; only a few candidates may use the
     * full limit, so a crafted header costs a few MiB of decoding at most. */
    if (effort->attempts >= WISE_MAX_ATTEMPTS) return false;
    ++effort->attempts;
    ok = wise_native_first(window, position, WISE_FIRST_QUICK, &over_limit,
                           pd);
    if (!ok && over_limit && effort->big_attempts < WISE_MAX_BIG_ATTEMPTS) {
        ++effort->big_attempts;
        ok = wise_native_first(window, position, WISE_FIRST_RAW_MAX,
                               &over_limit, pd);
    }
    if (!ok) return false;
    found->first = position;
    found->pk_mode = false;
    return true;
}

static bool wise_locate(Abstractformat *format, wise_found *found,
                        xx_pd_struct *pd) {
    uint8_t dos[WISE_DOS_HEADER];
    uint8_t signature[4];
    uint8_t *head = NULL;
    size_t head_size;
    xx_io_device *device;
    int64_t base, total, nt, image_end = 0;
    wise_window window;
    size_t candidates[10];
    size_t candidate_count = 0U, index;
    wise_effort effort;
    bool ok = false;
    if (!format || !format->device || format->base_address < 0) return false;
    xx_mem_zero(&effort, sizeof(effort));
    device = format->device;
    base = format->base_address;
    total = xx_io_total_size(device);
    if (total < base) return false;
    total -= base;
    if (total < WISE_MIN_FILE ||
        !wise_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    nt = (int64_t)wise_le32(dos + 0x3CU);
    if (nt < (int64_t)WISE_DOS_HEADER || nt >= WISE_MAX_IMAGE ||
        !wise_range(total, nt, 64) ||
        !wise_read_at(device, base + nt, signature, sizeof(signature)))
        return false;
    xx_mem_zero(found, sizeof(*found));
    found->total = total;
    if (signature[0] == 'P' && signature[1] == 'E' && signature[2] == 0U &&
        signature[3] == 0U) {
        if (!wise_pe_image_end(device, base, total, nt, &image_end))
            return false;
    } else if (signature[0] == 'N' && signature[1] == 'E') {
        if (!wise_ne_image_end(device, base, total, nt, &image_end))
            return false;
        found->is_ne = true;
    } else {
        return false;
    }
    if (image_end < (int64_t)WISE_DOS_HEADER || image_end > WISE_MAX_IMAGE ||
        total - image_end < 16 || !wise_has_marker(device, base, image_end))
        return false;
    found->overlay = image_end;

    head_size = total - image_end < (int64_t)WISE_HEAD
                    ? (size_t)(total - image_end)
                    : WISE_HEAD;
    head = (uint8_t *)xx_mem_alloc(head_size);
    xx_mem_zero(&window, sizeof(window));
    window.buffer = (uint8_t *)xx_mem_alloc(WISE_HEAD);
    if (!head || !window.buffer ||
        !wise_read_at(device, base + image_end, head, head_size))
        goto done;
    window.device = device;
    window.base = base;
    window.limit = total;
    window.capacity = WISE_HEAD;

    candidates[candidate_count++] = 0x51U;
    if (head_size > 0x5BU) candidates[candidate_count++] = 0x5CU + head[0x5BU];
    candidates[candidate_count++] = 0x1EU;
    candidates[candidate_count++] = 0x22U;
    candidates[candidate_count++] = 0x40U;
    candidates[candidate_count++] = 0x41U;
    candidates[candidate_count++] = 0x49U;
    candidates[candidate_count++] = 0x11U;
    candidates[candidate_count++] = 0x12U;
    for (index = 0U; index < candidate_count && !ok; ++index)
        ok = wise_try_candidate(device, base, total, &window, head, head_size,
                                candidates[index], false, &effort, found, pd);
    for (index = 0U; !ok && index + 8U <= head_size && index < WISE_SCAN &&
                     effort.attempts < WISE_MAX_ATTEMPTS;
         ++index) {
        if (pd && xx_pd_is_stopped(pd)) break;
        ok = wise_try_candidate(device, base, total, &window, head, head_size,
                                index, true, &effort, found, pd);
    }
done:
    if (head) xx_mem_free(head);
    if (window.buffer) xx_mem_free(window.buffer);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Native walk                                                             */

static bool wise_native_walk(xx_io_device *device, int64_t base,
                             int64_t total, int64_t first, wise_table *table,
                             xx_pd_struct *pd) {
    wise_window window;
    int64_t position = first;
    /* Everything decoded while walking, capped far above any real
     * installer (the corpus peaks at 8x its file size) so that a chain of
     * DEFLATE bombs cannot keep the walk busy for long. */
    uint64_t budget = (uint64_t)total * WISE_WALK_RATIO + WISE_WALK_EXTRA;
    uint64_t decoded = 0U;
    bool ok = true;
    xx_mem_zero(&window, sizeof(window));
    window.device = device;
    window.base = base;
    window.limit = total;
    window.capacity = WISE_WINDOW;
    window.buffer = (uint8_t *)xx_mem_alloc(WISE_WINDOW);
    if (!window.buffer) return false;
    while (position < total) {
        wise_inflated result;
        wise_member *member;
        int pad;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (table->count >= WISE_MAX_MEMBERS) {
            table->truncated = true;
            break;
        }
        if (!wise_window_inflate(&window, position,
                                 budget - decoded < WISE_MEMBER_RAW_MAX
                                     ? budget - decoded
                                     : WISE_MEMBER_RAW_MAX,
                                 &result, pd)) {
            table->truncated = true;
            break;
        }
        decoded += result.raw_size;
        pad = wise_find_crc(device, base, total, position + result.consumed,
                            result.crc32);
        if (pad < 0) {
            table->truncated = true;
            break;
        }
        member = wise_table_add(table);
        if (!member) {
            ok = false;
            break;
        }
        member->header_offset = position;
        member->header_size = 0;
        member->data_offset = position;
        member->packed_size = result.consumed;
        member->span = result.consumed + pad + 4;
        member->raw_size = result.raw_size;
        member->crc32 = result.crc32;
        member->method = 8U;
        xx_rt_memcpy(member->head, result.head, sizeof(member->head));
        member->head_size = result.head_size;
        position += member->span;
    }
    xx_mem_free(window.buffer);
    table->chain_end = position;
    /* wise_locate already saw the first member and a well-formed start of
     * the second; a chain broken after that still lists what is intact. */
    if (ok && table->count == 0U) ok = false;
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static bool wise_stem_is(const char *segment, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index) {
        char c = segment[index];
        if (c >= 'a' && c <= 'z') c = (char)(c - 0x20);
        if (!word[index] || c != word[index]) return false;
    }
    return word[stem] == 0;
}

/* Refuses a component Windows would resolve to something else: "." and
 * "..", only dots and spaces, a trailing dot or space, reserved
 * punctuation, control bytes, and the device names with or without an
 * extension (CON, AUX, NUL, PRN, COM0-9, LPT0-9, COM/LPT with a
 * superscript digit, CONIN$, CONOUT$, CLOCK$). */
static bool wise_safe_segment(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)segment[index];
        if (c < 0x20U || c == 0x7FU || c == '\\' || c == '/' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || segment[length - 1U] == '.' ||
        segment[length - 1U] == ' ')
        return false;
    while (stem < length && segment[stem] != '.') ++stem;
    while (stem > 0U && segment[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (wise_stem_is(segment, stem, devices[index])) return false;
    if (stem >= 3U &&
        (wise_stem_is(segment, 3U, "COM") || wise_stem_is(segment, 3U, "LPT"))) {
        if (stem == 4U && segment[3] >= '0' && segment[3] <= '9') return false;
        if (stem == 5U && (unsigned char)segment[3] == 0xC2U &&
            ((unsigned char)segment[4] == 0xB9U ||
             (unsigned char)segment[4] == 0xB2U ||
             (unsigned char)segment[4] == 0xB3U))
            return false;
    }
    return true;
}

static bool wise_safe_output_name(const char *name) {
    size_t start = 0U, at;
    if (!name || !name[0] || name[0] == '/') return false;
    for (at = 0U;; ++at) {
        if (name[at] == '/' || name[at] == 0) {
            if (!wise_safe_segment(name + start, at - start)) return false;
            if (name[at] == 0) return true;
            start = at + 1U;
        }
    }
}

/* Windows-1252 0x80..0x9F; 0 marks the five unassigned bytes. */
static const uint16_t wise_cp1252[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

static size_t wise_put_utf8(char *out, uint8_t byte) {
    uint32_t code = byte;
    if (byte >= 0x80U && byte < 0xA0U) {
        code = wise_cp1252[byte - 0x80U];
        if (code == 0U) code = '_';
    }
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

/* "%MAINDIR%\\a\\\\b.txt" -> "MAINDIR/a/b.txt": the '%' of the
 * variables go, both separators split, empty components collapse.  NULL
 * when a component is unsafe or nothing is left. */
static char *wise_clean_path(const uint8_t *source, size_t length) {
    char *out;
    size_t used = 0U, component = 0U, index;
    if (!source || length == 0U || length > WISE_PATH_MAX * 4U) return NULL;
    out = (char *)xx_mem_alloc(length * 3U + 1U);
    if (!out) return NULL;
    for (index = 0U; index <= length; ++index) {
        uint8_t byte = index < length ? source[index] : (uint8_t)'/';
        if (byte == '%') continue;
        if (byte == '\\' || byte == '/') {
            if (used > component) {
                if (!wise_safe_segment(out + component, used - component)) {
                    xx_mem_free(out);
                    return NULL;
                }
                out[used++] = '/';
                component = used;
            }
            continue;
        }
        used += wise_put_utf8(out + used, byte);
    }
    if (used == 0U) {
        xx_mem_free(out);
        return NULL;
    }
    out[used - 1U] = 0; /* the separator added after the last component */
    return out;
}

/* A destination path in the script: "%VAR%..." NUL-terminated, printable. */
static size_t wise_script_path(const uint8_t *script, size_t size,
                               size_t at) {
    size_t index, variable = 0U;
    if (at >= size || script[at] != '%') return 0U;
    for (index = at + 1U; index < size; ++index) {
        uint8_t c = script[index];
        if (c == '%') break;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0U;
        ++variable;
    }
    if (variable == 0U || index >= size) return 0U;
    for (; index < size && index - at <= WISE_PATH_MAX; ++index) {
        uint8_t c = script[index];
        if (c == 0U) return index - at >= 3U ? index - at : 0U;
        if (c < 0x20U || c == 0x7FU) return 0U;
    }
    return 0U;
}

/* "%TEMP%\\%EULAFILE%": the last component is only a variable. */
static bool wise_path_ends_in_variable(const uint8_t *path, size_t length) {
    size_t start = 0U, index;
    for (index = 0U; index < length; ++index)
        if (path[index] == '\\' || path[index] == '/') start = index + 1U;
    if (length - start < 3U || path[start] != '%' || path[length - 1U] != '%')
        return false;
    for (index = start + 1U; index + 1U < length; ++index)
        if (path[index] == '%') return false;
    return true;
}

static const char *wise_extension(const uint8_t *head, size_t size) {
    if (size >= 2U && head[0] == 'M' && head[1] == 'Z') return "exe";
    if (size >= 2U && head[0] == 'B' && head[1] == 'M') return "bmp";
    if (size >= 4U && xx_rt_memcmp(head, "RIFF", 4U) == 0) return "riff";
    if (size >= 4U && xx_rt_memcmp(head, "PK\x03\x04", 4U) == 0) return "zip";
    if (size >= 8U && xx_rt_memcmp(head, "\x89PNG\r\n\x1a\n", 8U) == 0)
        return "png";
    if (size >= 6U && (xx_rt_memcmp(head, "GIF87a", 6U) == 0 ||
                       xx_rt_memcmp(head, "GIF89a", 6U) == 0))
        return "gif";
    if (size >= 4U && xx_rt_memcmp(head, "?_\x03\x00", 4U) == 0) return "hlp";
    if (size >= 4U && xx_rt_memcmp(head, "MSCF", 4U) == 0) return "cab";
    return "bin";
}

static bool wise_is_dib(const wise_member *member) {
    return member->head_size >= 4U && wise_le32(member->head) == 40U;
}

static char *wise_dup(const char *text) {
    size_t length = xx_rt_strlen(text);
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (copy) xx_rt_memcpy(copy, text, length + 1U);
    return copy;
}

static bool wise_generic_name(wise_member *member, size_t index) {
    char name[32];
    (void)xx_rt_snprintf(name, sizeof(name), "entry_%05u.%s", (unsigned)index,
                         wise_extension(member->head, member->head_size));
    member->name = wise_dup(name);
    return member->name != NULL;
}

typedef struct wise_vote_s {
    int64_t key;
    uint32_t count;
    bool used;
} wise_vote;

typedef struct wise_span_s {
    int64_t span;
    size_t index;
} wise_span;

static int wise_span_compare(const void *left, const void *right) {
    const wise_span *a = (const wise_span *)left;
    const wise_span *b = (const wise_span *)right;
    if (a->span != b->span) return a->span < b->span ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* First entry in @p spans (sorted) with this span, or count. */
static size_t wise_span_find(const wise_span *spans, size_t count,
                             int64_t span) {
    size_t low = 0U, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (spans[middle].span < span) low = middle + 1U;
        else high = middle;
    }
    return low < count && spans[low].span == span ? low : count;
}

/* The member whose stream starts at @p position, or count. */
static size_t wise_member_at(const wise_table *table, int64_t position) {
    size_t low = 0U, high = table->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (table->items[middle].data_offset == position) return middle;
        if (table->items[middle].data_offset < position) low = middle + 1U;
        else high = middle;
    }
    return table->count;
}

static uint8_t *wise_load_member(xx_io_device *device, int64_t base,
                                 const wise_member *member,
                                 xx_pd_struct *pd) {
    uint8_t *data;
    size_t written = 0U;
    if (member->method != 8U || member->raw_size == 0U ||
        member->raw_size > WISE_SCRIPT_MAX)
        return NULL;
    data = (uint8_t *)xx_mem_alloc((size_t)member->raw_size);
    if (!data) return NULL;
    if (!xx_deflate_unpack_device_to_memory(
            device, base + member->data_offset, member->packed_size, data,
            (size_t)member->raw_size, &written, false, pd) ||
        written != member->raw_size ||
        xx_crc32_calc(0U, data, written) != member->crc32) {
        xx_mem_free(data);
        return NULL;
    }
    return data;
}

/* Finds the base that most (start, end) pairs in @p script agree on. */
static bool wise_vote_base(const wise_table *table, const wise_span *spans,
                           const uint8_t *script, size_t size, size_t script_index,
                           int64_t *base_out, uint32_t *votes_out) {
    wise_vote *votes;
    size_t used_slots = 0U, at, slot, work = 0U;
    uint32_t best = 0U;
    int64_t best_key = 0;
    votes = (wise_vote *)xx_mem_calloc(WISE_VOTE_SLOTS, sizeof(*votes));
    if (!votes) return false;
    for (at = 0U; at + 8U <= size && work < WISE_VOTE_WORK; ++at) {
        uint32_t start = wise_le32(script + at);
        uint32_t stop = wise_le32(script + at + 4U);
        size_t hit, same = 0U;
        if (stop <= start) continue;
        hit = wise_span_find(spans, table->count, (int64_t)(stop - start));
        for (; hit < table->count &&
               spans[hit].span == (int64_t)(stop - start) &&
               same < WISE_VOTE_SAME_SPAN;
             ++hit, ++same) {
            int64_t key;
            uint64_t hash;
            size_t probes = 0U;
            ++work;
            if (spans[hit].index <= script_index) continue;
            key = table->items[spans[hit].index].data_offset - (int64_t)start;
            if (key <= table->items[script_index].data_offset) continue;
            hash = (uint64_t)key * UINT64_C(0x9E3779B97F4A7C15);
            slot = (size_t)(hash >> 52U) & (WISE_VOTE_SLOTS - 1U);
            while (votes[slot].used && votes[slot].key != key &&
                   probes++ < WISE_VOTE_SLOTS)
                slot = (slot + 1U) & (WISE_VOTE_SLOTS - 1U);
            if (!votes[slot].used) {
                if (used_slots >= WISE_VOTE_SLOTS * 3U / 4U) continue;
                votes[slot].used = true;
                votes[slot].key = key;
                ++used_slots;
            }
            if (++votes[slot].count > best ||
                (votes[slot].count == best && key < best_key)) {
                best = votes[slot].count;
                best_key = key;
            }
        }
    }
    xx_mem_free(votes);
    *base_out = best_key;
    *votes_out = best;
    return best != 0U;
}

typedef struct wise_match_s {
    size_t at;
    size_t member;
} wise_match;

static void wise_names_from_script(xx_io_device *device, int64_t base,
                                   wise_table *table, size_t *script_out,
                                   xx_pd_struct *pd) {
    wise_span *spans = NULL;
    uint8_t *best_script = NULL;
    size_t best_size = 0U, best_index = 0U, index, candidates;
    int64_t best_base = 0;
    uint32_t best_votes = 0U;
    wise_match *matches = NULL;
    size_t match_count = 0U, match_capacity;
    uint32_t distance_votes[WISE_NAME_DISTANCE_MAX];
    size_t distance = 0U;
    unsigned pass;

    *script_out = table->count;
    if (table->count < 3U) return;
    spans = (wise_span *)xx_mem_alloc(table->count * sizeof(*spans));
    if (!spans) return;
    for (index = 0U; index < table->count; ++index) {
        spans[index].span = table->items[index].span;
        spans[index].index = index;
    }
    xx_rt_qsort(spans, table->count, sizeof(*spans), wise_span_compare);

    candidates = table->count < WISE_SCRIPT_CANDIDATES ? table->count
                                                       : WISE_SCRIPT_CANDIDATES;
    for (index = 0U; index < candidates; ++index) {
        uint8_t *script;
        int64_t vote_base;
        uint32_t votes;
        if (pd && xx_pd_is_stopped(pd)) break;
        if (table->items[index].raw_size < 16U) continue;
        script = wise_load_member(device, base, &table->items[index], pd);
        if (!script) continue;
        if (wise_vote_base(table, spans, script,
                           (size_t)table->items[index].raw_size, index,
                           &vote_base, &votes) &&
            votes > best_votes) {
            if (best_script) xx_mem_free(best_script);
            best_script = script;
            best_size = (size_t)table->items[index].raw_size;
            best_index = index;
            best_base = vote_base;
            best_votes = votes;
        } else {
            xx_mem_free(script);
        }
    }
    xx_mem_free(spans);
    if (!best_script) return;
    *script_out = best_index;

    /* Every (start, end) pair naming a member under that base. */
    match_capacity = best_size < WISE_MAX_MATCHES ? best_size : WISE_MAX_MATCHES;
    matches = (wise_match *)xx_mem_alloc(match_capacity * sizeof(*matches));
    if (!matches) {
        xx_mem_free(best_script);
        return;
    }
    for (index = 0U; index + 8U <= best_size && match_count < match_capacity;
         ++index) {
        uint32_t start = wise_le32(best_script + index);
        uint32_t stop = wise_le32(best_script + index + 4U);
        size_t member;
        if (stop <= start) continue;
        member = wise_member_at(table, best_base + (int64_t)start);
        if (member >= table->count || member <= best_index ||
            table->items[member].span != (int64_t)(stop - start))
            continue;
        matches[match_count].at = index;
        matches[match_count].member = member;
        ++match_count;
    }

    /* The distance from the pair to the destination path. */
    xx_mem_zero(distance_votes, sizeof(distance_votes));
    for (index = 0U; index < match_count; ++index) {
        size_t step;
        for (step = WISE_NAME_DISTANCE_MIN; step < WISE_NAME_DISTANCE_MAX;
             ++step)
            if (wise_script_path(best_script, best_size,
                                 matches[index].at + step) != 0U)
                ++distance_votes[step];
    }
    {
        size_t step;
        uint32_t top = 0U;
        for (step = WISE_NAME_DISTANCE_MIN; step < WISE_NAME_DISTANCE_MAX;
             ++step) {
            uint32_t count = distance_votes[step];
            bool better = count > top;
            /* Ties: the Wise32 layout, then the Wise16 one. */
            if (count != 0U && count == top &&
                (step == 40U || (step == 16U && distance != 40U)))
                better = true;
            if (better) {
                top = count;
                distance = step;
            }
        }
    }
    /* A stream installed twice ("%TEMP%\\%EULAFILE%" and
     * "%MAINDIR%\\Docs\\License.rtf") takes the name that ends in a real
     * file name; the first record wins otherwise. */
    /* A single vote (an installer with one file) is trusted only with the
     * path at one of the two measured distances. */
    if (best_votes < 2U && distance != 40U && distance != 16U) distance = 0U;
    for (pass = 0U; distance != 0U && pass < 2U; ++pass) {
        for (index = 0U; index < match_count; ++index) {
            wise_member *member = &table->items[matches[index].member];
            size_t at = matches[index].at + distance;
            size_t length;
            char *name;
            if (member->name) continue;
            length = wise_script_path(best_script, best_size, at);
            if (length == 0U ||
                (pass == 0U &&
                 wise_path_ends_in_variable(best_script + at, length)))
                continue;
            name = wise_clean_path(best_script + at, length);
            if (!name) continue;
            /* A Wise patch stream (rebuilds the file from its installed
             * version) opens with 0x01 and the record's DOS date/time. */
            if (member->head_size >= 5U && member->head[0] == 1U &&
                matches[index].at + 12U <= best_size &&
                xx_rt_memcmp(member->head + 1U,
                             best_script + matches[index].at + 8U, 4U) == 0) {
                char *patch = xx_str_concat(name, ".Patch");
                xx_mem_free(name);
                if (!patch) continue;
                name = patch;
            }
            member->name = name;
            ++table->named;
        }
    }
    xx_mem_free(matches);
    xx_mem_free(best_script);
}

/* ---------------------------------------------------------------------- */
/* Unique names                                                            */

static uint32_t wise_name_hash(const char *name) {
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        unsigned char c = (unsigned char)*name;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 0x20);
        hash = (hash ^ c) * 16777619U;
    }
    return hash;
}

static bool wise_name_equal(const char *left, const char *right) {
    for (;; ++left, ++right) {
        unsigned char a = (unsigned char)*left, b = (unsigned char)*right;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 0x20);
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 0x20);
        if (a != b) return false;
        if (a == 0U) return true;
    }
}

static bool wise_table_contains(const wise_table *table,
                                const uint32_t *slots, size_t mask,
                                const char *name) {
    size_t slot = (size_t)wise_name_hash(name) & mask;
    while (slots[slot] != 0U) {
        if (wise_name_equal(table->items[slots[slot] - 1U].name, name))
            return true;
        slot = (slot + 1U) & mask;
    }
    return false;
}

static void wise_table_insert(const wise_table *table, uint32_t *slots,
                              size_t mask, size_t member) {
    size_t slot = (size_t)wise_name_hash(table->items[member].name) & mask;
    while (slots[slot] != 0U) slot = (slot + 1U) & mask;
    slots[slot] = (uint32_t)(member + 1U);
}

/* "dir/name.ext" -> "dir/name_<number>.ext" (plus "_<pass>" later). */
static char *wise_renamed(const char *name, size_t number, unsigned pass) {
    char suffix[48];
    size_t suffix_length, length = xx_rt_strlen(name), component = 0U;
    size_t insert, index;
    char *result;
    if (pass > 1U)
        (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u_%u",
                             (unsigned)number, pass);
    else
        (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u", (unsigned)number);
    suffix_length = xx_rt_strlen(suffix);
    for (index = 0U; index < length; ++index)
        if (name[index] == '/') component = index + 1U;
    insert = length;
    for (index = length; index > component + 1U; --index)
        if (name[index - 1U] == '.') {
            insert = index - 1U;
            break;
        }
    result = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, insert);
    xx_rt_memcpy(result + insert, suffix, suffix_length);
    xx_rt_memcpy(result + insert + suffix_length, name + insert,
                 length - insert);
    result[length + suffix_length] = 0;
    return result;
}

static bool wise_make_unique(wise_table *table) {
    uint32_t *slots;
    size_t capacity = 2U, mask, index;
    if (table->count < 2U) return true;
    while (capacity < table->count * 2U) capacity <<= 1U;
    slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    mask = capacity - 1U;
    for (index = 0U; index < table->count; ++index) {
        wise_member *member = &table->items[index];
        if (wise_table_contains(table, slots, mask, member->name)) {
            unsigned pass;
            bool placed = false;
            for (pass = 1U; pass <= WISE_RENAME_PASSES && !placed; ++pass) {
                char *renamed = wise_renamed(member->name, index, pass);
                if (!renamed) {
                    xx_mem_free(slots);
                    return false;
                }
                if (!wise_table_contains(table, slots, mask, renamed)) {
                    xx_mem_free(member->name);
                    member->name = renamed;
                    placed = true;
                } else {
                    xx_mem_free(renamed);
                }
            }
            if (!placed) {
                member->extractable = false;
                continue;
            }
        }
        wise_table_insert(table, slots, mask, index);
    }
    xx_mem_free(slots);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Parse                                                                   */

/* First decoded bytes of a PK-mode member, for naming. */
static void wise_pk_head(xx_io_device *device, int64_t base,
                         wise_member *member) {
    uint8_t peek[WISE_PEEK];
    size_t available = member->packed_size < (int64_t)sizeof(peek)
                           ? (size_t)member->packed_size
                           : sizeof(peek);
    size_t produced = 0U;
    if (available == 0U ||
        !wise_read_at(device, base + member->data_offset, peek, available))
        return;
    if (member->method == 0U) {
        produced = available < sizeof(member->head) ? available
                                                    : sizeof(member->head);
        xx_rt_memcpy(member->head, peek, produced);
    } else if (!wise_quick_peek(peek, available, member->head, &produced)) {
        produced = 0U;
    }
    member->head_size = (uint8_t)produced;
}

static bool wise_parse(Abstractformat *format, wise_table **out,
                       xx_pd_struct *pd) {
    wise_found found;
    wise_table *table;
    size_t index, script = 0U;
    bool ok;
    *out = NULL;
    if (!wise_locate(format, &found, pd)) return false;
    table = (wise_table *)xx_mem_calloc(1U, sizeof(*table));
    if (!table) return false;
    table->refs = 1U;
    table->overlay = found.overlay;
    table->payload = found.first;
    table->is_ne = found.is_ne;
    table->pk_mode = found.pk_mode;
    if (found.pk_mode) {
        ok = wise_pk_walk(format->device, format->base_address, found.total,
                          found.first, table, &table->chain_end);
        if (ok) {
            bool script_named = false;
            for (index = 0U; index < table->count; ++index) {
                wise_member *member = &table->items[index];
                char *clean = NULL;
                if (member->name) {
                    clean = wise_clean_path((const uint8_t *)member->name,
                                            xx_rt_strlen(member->name));
                    xx_mem_free(member->name);
                    member->name = clean;
                    if (clean) ++table->named;
                }
            }
            for (index = 0U; ok && index < table->count; ++index) {
                wise_member *member = &table->items[index];
                if (member->name) continue;
                wise_pk_head(format->device, format->base_address, member);
                if (index < 2U && wise_is_dib(member)) {
                    member->name = wise_dup("WiseColors.dib");
                } else if (index < 2U && !script_named) {
                    member->name = wise_dup("WiseScript.bin");
                    script_named = true;
                } else {
                    ok = wise_generic_name(member, index);
                }
                if (!member->name) ok = false;
            }
        }
    } else {
        ok = wise_native_walk(format->device, format->base_address,
                              found.total, found.first, table, pd);
        if (ok) {
            wise_names_from_script(format->device, format->base_address,
                                   table, &script, pd);
            if (script >= table->count)
                script = wise_is_dib(&table->items[0]) ? 1U : 0U;
            for (index = 0U; ok && index < table->count; ++index) {
                wise_member *member = &table->items[index];
                if (member->name) continue;
                if (index == 0U && wise_is_dib(member))
                    member->name = wise_dup("WiseColors.dib");
                else if (index == script)
                    member->name = wise_dup("WiseScript.bin");
                else
                    ok = wise_generic_name(member, index);
                if (!member->name) ok = false;
            }
        }
    }
    if (ok) ok = wise_make_unique(table);
    if (ok && table->chain_end < found.total) table->truncated = true;
    if (!ok || table->count == 0U) {
        wise_table_release(table);
        return false;
    }
    *out = table;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static bool wise_unpack_member(xx_io_device *device, int64_t base,
                               const wise_member *member,
                               xx_io_device *destination, xx_pd_struct *pd) {
    wise_inflated result;
    if (member->method == 0U) {
        wise_sink sink;
        uint8_t *buffer;
        int64_t done = 0;
        bool ok = true;
        if ((uint64_t)member->packed_size != member->raw_size) return false;
        buffer = (uint8_t *)xx_mem_alloc(65536U);
        if (!buffer) return false;
        wise_sink_init(&sink, destination, member->raw_size);
        while (ok && done < member->packed_size) {
            size_t chunk = member->packed_size - done < 65536
                               ? (size_t)(member->packed_size - done)
                               : 65536U;
            if (pd && xx_pd_is_stopped(pd)) ok = false;
            else if (!wise_read_at(device, base + member->data_offset + done,
                                   buffer, chunk) ||
                     wise_sink_write(&sink.device, buffer, chunk) !=
                         (ssize_t)chunk)
                ok = false;
            done += (int64_t)chunk;
        }
        xx_mem_free(buffer);
        return ok && sink.crc32 == member->crc32;
    }
    if (member->packed_size <= 0 ||
        !wise_inflate_device(device, base + member->data_offset,
                             member->packed_size, destination,
                             member->raw_size, &result, pd))
        return false;
    return result.raw_size == member->raw_size &&
           result.crc32 == member->crc32 &&
           result.consumed <= member->packed_size;
}

static bool wise_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *wise_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool wise_set_record(xx_archive_record *record, int64_t base,
                            const wise_member *member) {
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base + member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = base + member->data_offset;
    record->compressed_size = member->packed_size;
    ok = xx_archive_record_set_original_name(record, member->name) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        member->raw_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        member->crc32) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        member->method) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (ok && member->has_time)
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                            member->dos_time) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                            member->dos_date);
    return ok;
}

static void wise_cursor_free(void *opaque) {
    wise_cursor *cursor = (wise_cursor *)opaque;
    if (!cursor) return;
    wise_table_release(cursor->table);
    xx_mem_free(cursor);
}

/* The cached table (parsed by handle_base_info), or a fresh parse. */
static wise_table *wise_acquire(Abstractformat *format, xx_pd_struct *pd) {
    xx_wise_installation_system *archive =
        (xx_wise_installation_system *)format;
    wise_table *table;
    if (!format) return NULL;
    if (archive->cache) {
        table = (wise_table *)archive->cache;
        ++table->refs;
        return table;
    }
    if (!wise_parse(format, &table, pd)) return NULL;
    return table;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_wise_installation_system_init(xx_wise_installation_system *archive,
                                      xx_io_device *device,
                                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_WISE_INSTALLATION_SYSTEM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-wise-installer");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_wise_installation_system_check_is_valid;
    archive->format.handle_base_info =
        xx_wise_installation_system_handle_base_info;
    archive->format.get_format_size =
        xx_wise_installation_system_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wise_installation_system_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wise_installation_system_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wise_installation_system_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wise_installation_system_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wise_installation_system_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wise_installation_system_free_archive_records_reading;
    archive->overlay_offset = -1;
    archive->payload_offset = -1;
    archive->chain_end = -1;
}

xx_wise_installation_system *xx_wise_installation_system_create(
    xx_io_device *device, int64_t base_address) {
    xx_wise_installation_system *archive =
        (xx_wise_installation_system *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_wise_installation_system_init(archive, device, base_address);
    return archive;
}

void xx_wise_installation_system_destroy(
    xx_wise_installation_system *archive) {
    if (!archive) return;
    if (archive->cache) {
        wise_table_release((wise_table *)archive->cache);
        archive->cache = NULL;
    }
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_wise_installation_system_free(xx_wise_installation_system *archive) {
    if (!archive) return;
    xx_wise_installation_system_destroy(archive);
    xx_mem_free(archive);
}

bool xx_wise_installation_system_check_is_valid(Abstractformat *format,
                                                xx_pd_struct *pd) {
    wise_found found;
    return wise_locate(format, &found, pd);
}

bool xx_wise_installation_system_handle_base_info(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    xx_wise_installation_system *archive;
    wise_table *table;
    if (!format) return false;
    archive = (xx_wise_installation_system *)format;
    if (archive->cache) {
        wise_table_release((wise_table *)archive->cache);
        archive->cache = NULL;
    }
    if (!wise_parse(format, &table, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive->cache = table;
    archive->number_of_records = table->count;
    archive->overlay_offset = format->base_address + table->overlay;
    archive->payload_offset = format->base_address + table->payload;
    archive->chain_end = format->base_address + table->chain_end;
    archive->named_records = table->named;
    archive->is_ne = table->is_ne;
    archive->pk_mode = table->pk_mode;
    archive->truncated = table->truncated;
    format->number_of_archive_records = table->count;
    format->format_size = table->chain_end;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_WISE_INSTALLATION_SYSTEM_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_wise_installation_system_get_format_size(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_wise_installation_system_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_wise_installation_system_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_wise_installation_system_handle_base_info(format, pd))
               ? ((xx_wise_installation_system *)format)->number_of_records
               : 0U;
}

bool xx_wise_installation_system_unpack_record_to_device(
    xx_wise_installation_system *archive, uint64_t index,
    xx_io_device *destination, xx_pd_struct *pd) {
    wise_table *table;
    bool ok;
    if (!archive) return false;
    table = wise_acquire(&archive->format, pd);
    if (!table) return false;
    ok = index < table->count &&
         wise_unpack_member(archive->format.device,
                            archive->format.base_address,
                            &table->items[(size_t)index], destination, pd);
    wise_table_release(table);
    return ok;
}

xx_archive_record_state *
xx_wise_installation_system_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    wise_cursor *cursor;
    xx_archive_record_state *state;
    wise_table *table = wise_acquire(format, pd);
    if (!table) return NULL;
    cursor = (wise_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!cursor) {
        wise_table_release(table);
        return NULL;
    }
    cursor->table = table;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        wise_cursor_free(cursor);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = cursor;
    state->free_internal = wise_cursor_free;
    state->total_records = (int64_t)table->count;
    if (!wise_copy_options(&state->options, options) ||
        !wise_set_record(&state->current_record, format->base_address,
                         &table->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_wise_installation_system_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_wise_installation_system_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    wise_cursor *cursor;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(cursor = (wise_cursor *)state->internal_state) ||
        ++cursor->index >= cursor->table->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        wise_set_record(&state->current_record, format->base_address,
                        &cursor->table->items[cursor->index]);
    return state->has_record;
}

bool xx_wise_installation_system_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    wise_cursor *cursor;
    wise_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(cursor = (wise_cursor *)state->internal_state) ||
        cursor->index >= cursor->table->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &cursor->table->items[cursor->index];
    path_option = wise_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return wise_unpack_member(format->device, format->base_address, member,
                                  NULL, pd);
    if (!member->extractable || !wise_safe_output_name(member->name))
        return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = wise_unpack_member(format->device, format->base_address,
                                    member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_io_file_remove_a(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_wise_installation_system_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
