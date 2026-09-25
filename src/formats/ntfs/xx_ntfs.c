/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ntfs/xx_ntfs.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* Every bound below is a ceiling on attacker-controlled arithmetic, not a
 * statement about what NTFS permits. A hostile image can name any cluster
 * count, any run length and any MFT size it likes; the parse refuses rather
 * than allocating or seeking on those numbers. */
#define XX_NTFS_MAX_VOLUME (UINT64_C(64) * 1024 * 1024 * 1024)
#define XX_NTFS_MAX_MFT (UINT64_C(64) * 1024 * 1024)
#define XX_NTFS_READ_BUDGET (UINT64_C(128) * 1024 * 1024)
#define XX_NTFS_KEEP_BUDGET (UINT64_C(32) * 1024 * 1024)
#define XX_NTFS_MAX_RECORDS UINT64_C(65536)
#define XX_NTFS_MAX_RUNS ((size_t)262144)
#define XX_NTFS_MAX_ATTRIBUTES 1024U
#define XX_NTFS_MAX_DEPTH 128U
#define XX_NTFS_MAX_NAME_SIZE 4096U
#define XX_NTFS_CHUNK 65536U
#define XX_NTFS_BOOT_SIZE 512U
/* MFT record 5 is the root directory; 0..15 are reserved metadata files that
 * are never published and never appear in a published file's path. */
#define XX_NTFS_ROOT_RECORD UINT64_C(5)
#define XX_NTFS_FIRST_USER_RECORD UINT64_C(16)

/* Reasons a record is listed but not extractable. Static strings: they are
 * borrowed by entries and never freed. */
static const char *const xx_ntfs_unsupported_attribute_list =
    "NTFS attribute-list extensions are not supported";
static const char *const xx_ntfs_unsupported_reparse =
    "NTFS reparse/compressed-provider files are not supported";
static const char *const xx_ntfs_unsupported_multiple_data =
    "NTFS multiple unnamed data attributes are not supported";
static const char *const xx_ntfs_unsupported_compressed =
    "NTFS compressed/encrypted data is not supported";
static const char *const xx_ntfs_unsupported_continuation =
    "NTFS external data-attribute continuation is not supported";
static const char *const xx_ntfs_unsupported_no_data =
    "NTFS file has no supported unnamed data stream";

/* ------------------------------------------------------------- helpers --- */

static uint16_t xx_ntfs_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_ntfs_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint64_t xx_ntfs_u64(const uint8_t *data) {
    return (uint64_t)xx_ntfs_u32(data) |
           ((uint64_t)xx_ntfs_u32(data + 4U) << 32U);
}

/** True when [off, off + n) lies inside [0, size) without wrapping. */
static bool xx_ntfs_span(uint64_t off, uint64_t n, uint64_t size) {
    return off <= size && n <= size - off;
}

static bool xx_ntfs_power2(uint64_t n) {
    return n != 0U && (n & (n - 1U)) == 0U;
}

/* --------------------------------------------------------------- model --- */

typedef struct xx_ntfs_run_s {
    uint64_t logical;  /**< Byte offset of the run inside the stream. */
    uint64_t physical; /**< Byte offset inside the volume; 0 when sparse. */
    uint64_t size;     /**< Run length in bytes. */
    bool sparse;
} xx_ntfs_run;

typedef struct xx_ntfs_stream_s {
    uint64_t size;
    uint64_t initialized;
    uint8_t *resident; /**< Owned; valid when !nonresident. */
    size_t resident_size;
    xx_ntfs_run *runs; /**< Owned; ordered by logical, gapless. */
    size_t run_count;
    size_t run_capacity;
    bool nonresident;
} xx_ntfs_stream;

typedef struct xx_ntfs_name_s {
    char *text; /**< Owned UTF-8 component name. */
    uint64_t parent;
    uint16_t sequence;
    bool present;
} xx_ntfs_name;

typedef struct xx_ntfs_record_s {
    uint64_t index;
    int64_t header_offset; /**< Volume-relative offset of the MFT record. */
    uint16_t sequence;
    bool folder;
    bool extension;
    bool has_data;
    bool present; /**< Slot holds a decoded, non-extension, in-use record. */
    uint32_t named_streams;
    const char *unsupported;
    xx_ntfs_name names[4];
    int selected; /**< Namespace index of the chosen name, or -1. */
    xx_ntfs_stream data;
} xx_ntfs_record;

typedef struct xx_ntfs_entry_s {
    char *name; /**< Owned UTF-8 path, '/' separated, no leading slash. */
    const char *unsupported;
    uint64_t index;
    uint32_t named_streams;
    int64_t header_offset;
    bool folder;
    xx_ntfs_stream data; /**< Moved out of the record; owned here. */
} xx_ntfs_entry;

typedef struct xx_ntfs_private_s {
    xx_ntfs_entry *entries;
    size_t count;
    size_t capacity;
    xx_ntfs_record *records; /**< Indexed by MFT record number. */
    size_t record_count;
    xx_ntfs_stream mft;
    uint64_t volume_length;
    uint64_t cluster_size;
    uint64_t sector_size;
    uint64_t record_size;
    uint64_t mft_cluster;
    int64_t volume_end; /**< Absolute device offset one past the volume. */
} xx_ntfs_private;

/** Parse-wide budgets and cancellation. Mirrors the reference's Reader. */
typedef struct xx_ntfs_reader_s {
    xx_io_device *device;
    int64_t base;
    uint64_t size; /**< Bytes available from base to end of device. */
    uint64_t read_budget;
    uint64_t keep_budget;
    size_t total_runs;
    xx_pd_struct *pd;
    bool failed;
} xx_ntfs_reader;

typedef struct xx_ntfs_archive_stream_s {
    xx_ntfs_private parsed;
    size_t index;
} xx_ntfs_archive_stream;

static void xx_ntfs_vtable_destroy(Abstractformat *self);

/* ---------------------------------------------------------- reader I/O --- */

static bool xx_ntfs_active(const xx_ntfs_reader *reader) {
    if (!reader || reader->failed || !reader->device) return false;
    return !(reader->pd && xx_pd_is_stopped(reader->pd));
}

/** Charge a long-lived allocation against the retained-memory budget. */
static bool xx_ntfs_retain(xx_ntfs_reader *reader, uint64_t bytes) {
    if (!reader || bytes > reader->keep_budget) return false;
    reader->keep_budget -= bytes;
    return true;
}

/**
 * Read @p n bytes at volume-relative @p off. Metadata reads are additionally
 * charged against the scan budget so that a crafted run list cannot turn a
 * validity probe into an unbounded amount of I/O; payload reads during
 * extraction are not, because their total is the declared file size.
 */
static bool xx_ntfs_read(xx_ntfs_reader *reader, uint64_t off, void *data,
                         uint64_t n, bool metadata) {
    uint8_t *out = (uint8_t *)data;
    uint64_t done = 0U;
    int64_t absolute;
    if (!xx_ntfs_active(reader) || (!data && n != 0U) ||
        !xx_ntfs_span(off, n, reader->size) || n > XX_NTFS_CHUNK ||
        (metadata && n > reader->read_budget)) {
        return false;
    }
    if (metadata) reader->read_budget -= n;
    if (n == 0U) return true;
    if (off > (uint64_t)(INT64_MAX - reader->base)) return false;
    absolute = reader->base + (int64_t)off;
    if (xx_io_seek64(reader->device, absolute, XX_RT_SEEK_SET) != 0) {
        return false;
    }
    while (done < n) {
        ssize_t got = xx_io_read(reader->device, out + done,
                                 (size_t)(n - done));
        if (got <= 0 || (uint64_t)got > n - done) return false;
        done += (uint64_t)got;
    }
    return true;
}

/* -------------------------------------------------------------- memory --- */

static void xx_ntfs_stream_cleanup(xx_ntfs_stream *stream) {
    if (!stream) return;
    if (stream->resident) xx_mem_free(stream->resident);
    if (stream->runs) xx_mem_free(stream->runs);
    xx_mem_zero(stream, sizeof(*stream));
}

static void xx_ntfs_record_cleanup(xx_ntfs_record *record) {
    unsigned index;
    if (!record) return;
    for (index = 0U; index < 4U; ++index) {
        if (record->names[index].text) xx_str_free(record->names[index].text);
    }
    xx_ntfs_stream_cleanup(&record->data);
    xx_mem_zero(record, sizeof(*record));
    record->selected = -1;
    record->header_offset = -1;
}

static void xx_ntfs_entry_cleanup(xx_ntfs_entry *entry) {
    if (!entry) return;
    if (entry->name) xx_str_free(entry->name);
    xx_ntfs_stream_cleanup(&entry->data);
    xx_mem_zero(entry, sizeof(*entry));
    entry->header_offset = -1;
}

static void xx_ntfs_private_cleanup(xx_ntfs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        xx_ntfs_entry_cleanup(&parsed->entries[index]);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    for (index = 0U; index < parsed->record_count; ++index) {
        xx_ntfs_record_cleanup(&parsed->records[index]);
    }
    if (parsed->records) xx_mem_free(parsed->records);
    xx_ntfs_stream_cleanup(&parsed->mft);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->volume_end = -1;
}

static bool xx_ntfs_append_run(xx_ntfs_reader *reader, xx_ntfs_stream *stream,
                               const xx_ntfs_run *run) {
    xx_ntfs_run *grown;
    size_t capacity;
    if (!stream || !run) return false;
    if (stream->run_count == stream->run_capacity) {
        capacity = stream->run_capacity ? stream->run_capacity * 2U : 8U;
        if (capacity < stream->run_count ||
            capacity > XX_NTFS_MAX_RUNS ||
            capacity > SIZE_MAX / sizeof(*stream->runs)) {
            return false;
        }
        grown = (xx_ntfs_run *)xx_mem_realloc(
            stream->runs, capacity * sizeof(*stream->runs));
        if (!grown) return false;
        stream->runs = grown;
        stream->run_capacity = capacity;
    }
    if (++reader->total_runs > XX_NTFS_MAX_RUNS ||
        !xx_ntfs_retain(reader, (uint64_t)sizeof(*run))) {
        return false;
    }
    stream->runs[stream->run_count++] = *run;
    return true;
}

static bool xx_ntfs_append_entry(xx_ntfs_private *parsed,
                                 xx_ntfs_entry *entry) {
    xx_ntfs_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        (uint64_t)parsed->count >= XX_NTFS_MAX_RECORDS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_ntfs_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    entry->header_offset = -1;
    return true;
}

/* ------------------------------------------------------ stream reading --- */

/**
 * Locate the run covering stream offset @p off. Runs are appended in ascending
 * logical order and are gapless, so an upper-bound search over their starts
 * finds the only candidate; a miss means the stream never mapped that offset.
 */
static bool xx_ntfs_stream_locate(const xx_ntfs_stream *stream, uint64_t off,
                                  const xx_ntfs_run **out) {
    size_t low = 0U;
    size_t high;
    if (!stream || !out || stream->run_count == 0U) return false;
    high = stream->run_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (stream->runs[middle].logical <= off) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) return false;
    *out = &stream->runs[low - 1U];
    return xx_ntfs_span(off - (*out)->logical, 1U, (*out)->size);
}

/**
 * Copy [off, off + n) of @p stream into @p out. Bytes past the valid data
 * length, and bytes covered by a sparse run, read as zero - the buffer is
 * cleared first so both cases need no special casing.
 */
static bool xx_ntfs_read_stream(xx_ntfs_reader *reader,
                                const xx_ntfs_stream *stream, uint64_t off,
                                uint64_t n, uint8_t *out, bool metadata) {
    uint64_t done = 0U;
    if (!stream || !out || !xx_ntfs_span(off, n, stream->size) ||
        n > XX_NTFS_CHUNK || !xx_ntfs_active(reader)) {
        return false;
    }
    xx_rt_memset(out, 0, (size_t)n);
    if (!stream->nonresident) {
        if ((uint64_t)stream->resident_size != stream->size ||
            !stream->resident) {
            return false;
        }
        if (n != 0U) xx_rt_memcpy(out, stream->resident + off, (size_t)n);
        return true;
    }
    while (done < n) {
        uint64_t position = off + done;
        const xx_ntfs_run *run = NULL;
        uint64_t inside;
        uint64_t count;
        if (!xx_ntfs_active(reader)) return false;
        if (position >= stream->initialized) break;
        if (!xx_ntfs_stream_locate(stream, position, &run)) return false;
        inside = position - run->logical;
        count = n - done;
        if (count > run->size - inside) count = run->size - inside;
        if (count > stream->initialized - position) {
            count = stream->initialized - position;
        }
        if (!run->sparse &&
            !xx_ntfs_read(reader, run->physical + inside, out + done, count,
                          metadata)) {
            return false;
        }
        done += count;
    }
    return xx_ntfs_active(reader);
}

/* --------------------------------------------------------------- names --- */

/** UTF-8 length of one code point, or 0 for a value that cannot be encoded. */
static size_t xx_ntfs_utf8_length(uint32_t code_point) {
    if (code_point < 0x80U) return 1U;
    if (code_point < 0x800U) return 2U;
    if (code_point < 0x10000U) return 3U;
    if (code_point <= 0x10ffffU) return 4U;
    return 0U;
}

static size_t xx_ntfs_utf8_encode(uint32_t code_point, char *out) {
    if (code_point < 0x80U) {
        out[0] = (char)code_point;
        return 1U;
    }
    if (code_point < 0x800U) {
        out[0] = (char)(0xc0U | (code_point >> 6U));
        out[1] = (char)(0x80U | (code_point & 0x3fU));
        return 2U;
    }
    if (code_point < 0x10000U) {
        out[0] = (char)(0xe0U | (code_point >> 12U));
        out[1] = (char)(0x80U | ((code_point >> 6U) & 0x3fU));
        out[2] = (char)(0x80U | (code_point & 0x3fU));
        return 3U;
    }
    out[0] = (char)(0xf0U | (code_point >> 18U));
    out[1] = (char)(0x80U | ((code_point >> 12U) & 0x3fU));
    out[2] = (char)(0x80U | ((code_point >> 6U) & 0x3fU));
    out[3] = (char)(0x80U | (code_point & 0x3fU));
    return 4U;
}

/**
 * Decode @p units UTF-16LE code units at @p data into an owned UTF-8 string.
 * Rejects NUL, both path separators, lone or mis-ordered surrogates and the
 * parent-directory name, so a filename can never escape the extraction root
 * or be silently truncated. Two passes: size, then encode.
 */
static char *xx_ntfs_decode_name(const uint8_t *data, size_t available,
                                 size_t offset, size_t units,
                                 size_t *out_size) {
    size_t index;
    size_t total = 0U;
    size_t written = 0U;
    char *name;
    if (!data || units < 1U || units > 255U || offset > available ||
        units > (available - offset) / 2U) {
        return NULL;
    }
    for (index = 0U; index < units; ++index) {
        uint32_t code_point = xx_ntfs_u16(data + offset + index * 2U);
        size_t length;
        if (code_point == 0U || code_point == (uint32_t)'/' ||
            code_point == (uint32_t)'\\') {
            return NULL;
        }
        if (code_point >= 0xd800U && code_point <= 0xdbffU) {
            uint32_t low;
            if (++index >= units) return NULL;
            low = xx_ntfs_u16(data + offset + index * 2U);
            if (low < 0xdc00U || low > 0xdfffU) return NULL;
            code_point = 0x10000U + ((code_point - 0xd800U) << 10U) +
                         (low - 0xdc00U);
        } else if (code_point >= 0xdc00U && code_point <= 0xdfffU) {
            return NULL;
        }
        length = xx_ntfs_utf8_length(code_point);
        if (length == 0U || total > SIZE_MAX - length) return NULL;
        total += length;
    }
    if (total == 0U) return NULL;
    name = (char *)xx_mem_alloc(total + 1U);
    if (!name) return NULL;
    for (index = 0U; index < units; ++index) {
        uint32_t code_point = xx_ntfs_u16(data + offset + index * 2U);
        if (code_point >= 0xd800U && code_point <= 0xdbffU) {
            uint32_t low = xx_ntfs_u16(data + offset + (index + 1U) * 2U);
            ++index;
            code_point = 0x10000U + ((code_point - 0xd800U) << 10U) +
                         (low - 0xdc00U);
        }
        written += xx_ntfs_utf8_encode(code_point, name + written);
    }
    name[total] = '\0';
    if (total == 2U && name[0] == '.' && name[1] == '.') {
        xx_str_free(name);
        return NULL;
    }
    if (out_size) *out_size = total;
    return name;
}

/**
 * Second gate, applied at extraction time: the decoded path must be relative
 * and free of components the host filesystem would reinterpret. Mirrors the
 * ISO 9660 reader's check so both filesystem walkers refuse the same paths.
 */
static bool xx_ntfs_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* ---------------------------------------------------------- MFT record --- */

/**
 * Apply the update-sequence fixups in place and validate the record header.
 * Every sector trailer must carry the sequence number; a mismatch means the
 * record was captured mid-write or was fabricated, and either way the
 * attribute offsets inside it cannot be trusted.
 */
static bool xx_ntfs_fixup(uint8_t *data, const xx_ntfs_private *volume) {
    uint64_t offset;
    uint64_t count;
    uint64_t index;
    uint64_t used;
    uint64_t allocated;
    uint64_t first;
    uint16_t sequence;
    if (!data || !volume || volume->record_size < 48U ||
        xx_rt_memcmp(data, "FILE", 4U) != 0) {
        return false;
    }
    offset = xx_ntfs_u16(data + 4U);
    count = xx_ntfs_u16(data + 6U);
    if (offset < 42U || (offset % 2U) != 0U ||
        count != volume->record_size / volume->sector_size + 1U ||
        !xx_ntfs_span(offset, count * 2U, volume->sector_size - 2U)) {
        return false;
    }
    sequence = xx_ntfs_u16(data + offset);
    for (index = 1U; index < count; ++index) {
        uint64_t trailer = index * volume->sector_size - 2U;
        if (xx_ntfs_u16(data + trailer) != sequence) return false;
        data[trailer] = data[offset + index * 2U];
        data[trailer + 1U] = data[offset + index * 2U + 1U];
    }
    used = xx_ntfs_u32(data + 24U);
    allocated = xx_ntfs_u32(data + 28U);
    first = xx_ntfs_u16(data + 20U);
    return allocated == volume->record_size && used >= 48U &&
           used <= allocated && first >= 48U &&
           first >= offset + count * 2U && (first % 8U) == 0U &&
           first <= used - 4U;
}

/**
 * Decode the mapping pairs of a non-resident attribute header into runs.
 *
 * Only the LCN delta is signed; the run length is zero-extended, matching the
 * NTFS3 run_unpack encoding. The negation is done on the magnitude so that no
 * signed shift or INT64_MIN negation is ever performed, and every resulting
 * LCN range is re-checked against the volume's cluster count.
 */
static bool xx_ntfs_parse_runs(xx_ntfs_reader *reader, const uint8_t *attr,
                               uint64_t length, const xx_ntfs_private *volume,
                               xx_ntfs_stream *stream) {
    uint64_t high;
    uint64_t offset;
    uint64_t logical = 0U;
    uint64_t lcn = 0U;
    uint64_t clusters;
    bool ended = false;
    if (!attr || !volume || !stream || length < 64U ||
        xx_ntfs_u64(attr + 16U) != 0U) {
        return false;
    }
    high = xx_ntfs_u64(attr + 24U);
    offset = xx_ntfs_u16(attr + 32U);
    if (offset < 64U || offset >= length) return false;
    stream->size = xx_ntfs_u64(attr + 48U);
    stream->initialized = xx_ntfs_u64(attr + 56U);
    stream->nonresident = true;
    if (stream->size > XX_NTFS_MAX_VOLUME ||
        stream->initialized > stream->size) {
        return false;
    }
    clusters = volume->volume_length / volume->cluster_size;
    while (offset < length && xx_ntfs_active(reader)) {
        uint8_t head = attr[offset++];
        uint32_t lengths;
        uint32_t offsets;
        uint32_t index;
        uint64_t run_length = 0U;
        uint64_t delta = 0U;
        uint64_t bytes;
        xx_ntfs_run run;
        if (head == 0U) {
            ended = true;
            break;
        }
        lengths = (uint32_t)(head & 15U);
        offsets = (uint32_t)(head >> 4U);
        if (lengths == 0U || lengths > 8U || offsets > 8U ||
            !xx_ntfs_span(offset, (uint64_t)lengths + offsets, length)) {
            return false;
        }
        for (index = 0U; index < lengths; ++index) {
            run_length |= (uint64_t)attr[offset++] << (8U * index);
        }
        for (index = 0U; index < offsets; ++index) {
            delta |= (uint64_t)attr[offset++] << (8U * index);
        }
        if (run_length == 0U || run_length > clusters ||
            run_length > (XX_NTFS_MAX_VOLUME - logical) / volume->cluster_size) {
            return false;
        }
        if (offsets != 0U) {
            bool negative =
                (delta & ((uint64_t)1 << (offsets * 8U - 1U))) != 0U;
            if (negative) {
                uint64_t mask = (offsets == 8U)
                                    ? ~(uint64_t)0
                                    : (((uint64_t)1 << (offsets * 8U)) - 1U);
                uint64_t magnitude = ((~delta) & mask) + 1U;
                if (magnitude > lcn) return false;
                lcn -= magnitude;
            } else {
                if (delta > clusters || lcn > clusters - delta) return false;
                lcn += delta;
            }
            if (!xx_ntfs_span(lcn, run_length, clusters)) return false;
        }
        bytes = run_length * volume->cluster_size;
        run.logical = logical;
        run.physical = (offsets != 0U) ? lcn * volume->cluster_size : 0U;
        run.size = bytes;
        run.sparse = (offsets == 0U);
        if (!xx_ntfs_append_run(reader, stream, &run)) return false;
        logical += bytes;
    }
    if (!ended || !xx_ntfs_active(reader) || stream->size > logical ||
        xx_ntfs_u64(attr + 40U) > logical) {
        return false;
    }
    if (logical == 0U) {
        return stream->size == 0U && (high == 0U || high == ~(uint64_t)0);
    }
    return high == logical / volume->cluster_size - 1U;
}

/** Store one $FILE_NAME, replacing an earlier one in the same namespace. */
static bool xx_ntfs_store_name(xx_ntfs_reader *reader, xx_ntfs_record *record,
                               const uint8_t *value, uint64_t value_size) {
    uint8_t namespace_id;
    size_t units;
    size_t text_size = 0U;
    char *text;
    if (value_size < 66U) return false;
    namespace_id = value[65];
    units = value[64];
    if (namespace_id > 3U) return false;
    text = xx_ntfs_decode_name(value, (size_t)value_size, 66U, units,
                               &text_size);
    if (!text) return false;
    if (!xx_ntfs_retain(reader,
                        (uint64_t)text_size + (uint64_t)sizeof(xx_ntfs_name))) {
        xx_str_free(text);
        return false;
    }
    if (record->names[namespace_id].text) {
        xx_str_free(record->names[namespace_id].text);
    }
    record->names[namespace_id].text = text;
    /* The parent is a 48-bit MFT reference; the top 16 bits are its sequence. */
    record->names[namespace_id].parent =
        xx_ntfs_u64(value) & UINT64_C(0xffffffffffff);
    record->names[namespace_id].sequence = xx_ntfs_u16(value + 6U);
    record->names[namespace_id].present = true;
    return true;
}

/** Decode the unnamed $DATA attribute, or record why it cannot be decoded. */
static bool xx_ntfs_store_data(xx_ntfs_reader *reader, xx_ntfs_record *record,
                               const xx_ntfs_private *volume,
                               const uint8_t *attr, uint64_t length,
                               uint16_t flags, const uint8_t *value,
                               uint64_t value_size, bool nonresident) {
    if (record->has_data) {
        record->unsupported = xx_ntfs_unsupported_multiple_data;
    }
    record->has_data = true;
    /* 0x8000 is the sparse flag, which the run decoder already handles; any
     * other flag, or a non-zero compression unit, means encoded data. */
    if ((flags & (uint16_t)~(uint16_t)0x8000) != 0U ||
        (nonresident && attr[34] != 0U)) {
        record->unsupported = xx_ntfs_unsupported_compressed;
    }
    if (nonresident) {
        if (xx_ntfs_u64(attr + 16U) != 0U) {
            record->unsupported = xx_ntfs_unsupported_continuation;
        } else if (!record->unsupported &&
                   !xx_ntfs_parse_runs(reader, attr, length, volume,
                                       &record->data)) {
            return false;
        }
        /* The header still states a truthful size for an entry we refuse to
         * extract, so the listing stays accurate. */
        if (record->unsupported) {
            record->data.size = xx_ntfs_u64(attr + 48U);
            record->data.initialized = xx_ntfs_u64(attr + 56U);
        }
        return true;
    }
    if (!xx_ntfs_retain(reader, value_size)) return false;
    /* A second unnamed $DATA is already flagged unsupported above, but it still
     * reaches here; drop the first buffer rather than leaking it. */
    if (record->data.resident) {
        xx_mem_free(record->data.resident);
        record->data.resident = NULL;
        record->data.resident_size = 0U;
    }
    if (value_size != 0U) {
        record->data.resident = (uint8_t *)xx_mem_alloc((size_t)value_size);
        if (!record->data.resident) return false;
        xx_rt_memcpy(record->data.resident, value, (size_t)value_size);
    }
    record->data.resident_size = (size_t)value_size;
    record->data.size = value_size;
    record->data.initialized = value_size;
    return true;
}

/**
 * Walk the attributes of one fixed-up MFT record. @p data is mutated by the
 * fixup pass, so the caller owns a private copy of the record bytes.
 */
static bool xx_ntfs_parse_record(xx_ntfs_reader *reader,
                                 const xx_ntfs_private *volume, uint8_t *data,
                                 uint64_t index, xx_ntfs_record *record) {
    static const int order[4] = {1, 0, 3, 2};
    uint64_t used;
    uint64_t offset;
    unsigned count = 0U;
    unsigned position;
    bool ended = false;
    if (!record) return false;
    if (!xx_ntfs_fixup(data, volume)) return false;
    record->index = index;
    record->sequence = xx_ntfs_u16(data + 16U);
    record->folder = (xx_ntfs_u16(data + 22U) & 2U) != 0U;
    record->extension = xx_ntfs_u64(data + 32U) != 0U;
    record->selected = -1;
    used = xx_ntfs_u32(data + 24U);
    offset = xx_ntfs_u16(data + 20U);
    while (xx_ntfs_span(offset, 4U, used) && xx_ntfs_active(reader)) {
        const uint8_t *attr;
        uint32_t type;
        uint64_t length;
        uint8_t form;
        uint8_t name_length;
        uint64_t name_offset;
        uint16_t flags;
        const uint8_t *value = NULL;
        uint64_t value_size = 0U;
        type = xx_ntfs_u32(data + offset);
        if (type == 0xffffffffU) {
            ended = true;
            break;
        }
        if (++count > XX_NTFS_MAX_ATTRIBUTES ||
            !xx_ntfs_span(offset, 16U, used)) {
            return false;
        }
        length = xx_ntfs_u32(data + offset + 4U);
        if (length < 24U || (length % 8U) != 0U ||
            !xx_ntfs_span(offset, length, used)) {
            return false;
        }
        attr = data + offset;
        form = attr[8];
        name_length = attr[9];
        name_offset = xx_ntfs_u16(attr + 10U);
        flags = xx_ntfs_u16(attr + 12U);
        if (form > 1U || (form != 0U && length < 64U) ||
            (name_length != 0U &&
             (name_offset < (form != 0U ? 64U : 24U) ||
              !xx_ntfs_span(name_offset, (uint64_t)name_length * 2U,
                            length)))) {
            return false;
        }
        if (form == 0U) {
            uint64_t resident_size = xx_ntfs_u32(attr + 16U);
            uint64_t resident_offset = xx_ntfs_u16(attr + 20U);
            if (resident_offset < 24U ||
                !xx_ntfs_span(resident_offset, resident_size, length)) {
                return false;
            }
            value = attr + resident_offset;
            value_size = resident_size;
        }
        if (type == 0x20U) record->unsupported = xx_ntfs_unsupported_attribute_list;
        if (type == 0xc0U) record->unsupported = xx_ntfs_unsupported_reparse;
        if (type == 0x30U && name_length == 0U) {
            if (form != 0U ||
                !xx_ntfs_store_name(reader, record, value, value_size)) {
                return false;
            }
        }
        if (type == 0x80U) {
            if (name_length != 0U) {
                ++record->named_streams;
            } else if (!xx_ntfs_store_data(reader, record, volume, attr,
                                           length, flags, value, value_size,
                                           form != 0U)) {
                return false;
            }
        }
        offset += length;
    }
    /* Win32, then POSIX, then the combined Win32+DOS slot, then DOS. */
    for (position = 0U; position < 4U; ++position) {
        if (record->names[order[position]].present) {
            record->selected = order[position];
            break;
        }
    }
    if (record->data.size > XX_NTFS_MAX_VOLUME) return false;
    return ended && xx_ntfs_active(reader);
}

/* ----------------------------------------------------------- path walk --- */

static const xx_ntfs_record *xx_ntfs_find(const xx_ntfs_private *parsed,
                                          uint64_t index) {
    if (!parsed || index >= (uint64_t)parsed->record_count) return NULL;
    return parsed->records[index].present ? &parsed->records[index] : NULL;
}

/** Concatenate @p count components, most significant first, with '/'. */
static char *xx_ntfs_join(const char *const *parts, size_t count) {
    size_t index;
    size_t total = 0U;
    size_t written = 0U;
    char *joined;
    if (!parts || count == 0U) return NULL;
    for (index = 0U; index < count; ++index) {
        size_t length = xx_str_len(parts[index]);
        if (length == 0U || length >= XX_NTFS_MAX_NAME_SIZE ||
            total > XX_NTFS_MAX_NAME_SIZE - length) {
            return NULL;
        }
        total += length;
        if (index + 1U < count) {
            if (total >= XX_NTFS_MAX_NAME_SIZE) return NULL;
            ++total;
        }
    }
    joined = (char *)xx_mem_alloc(total + 1U);
    if (!joined) return NULL;
    for (index = 0U; index < count; ++index) {
        size_t length = xx_str_len(parts[index]);
        xx_rt_memcpy(joined + written, parts[index], length);
        written += length;
        if (index + 1U < count) joined[written++] = '/';
    }
    joined[total] = '\0';
    return joined;
}

/**
 * Build the full path of @p start by walking parent references up to the root.
 * Returns false only on a corrupt tree; @p excluded reports the benign case of
 * an ancestor inside the reserved metadata range, which is simply not listed.
 */
static bool xx_ntfs_build_path(xx_ntfs_reader *reader,
                               const xx_ntfs_private *parsed, uint64_t start,
                               char **out, bool *excluded) {
    const char *parts[XX_NTFS_MAX_DEPTH];
    uint64_t visited[XX_NTFS_MAX_DEPTH];
    size_t depth = 0U;
    uint64_t id = start;
    size_t index;
    size_t half;
    char *joined;
    *out = NULL;
    *excluded = false;
    while (id != XX_NTFS_ROOT_RECORD) {
        const xx_ntfs_record *item;
        uint64_t parent;
        if (!xx_ntfs_active(reader)) return false;
        if (id < XX_NTFS_FIRST_USER_RECORD) {
            *excluded = true;
            return true;
        }
        for (index = 0U; index < depth; ++index) {
            if (visited[index] == id) return false; /* Parent cycle. */
        }
        if (depth >= XX_NTFS_MAX_DEPTH) return false;
        item = xx_ntfs_find(parsed, id);
        if (!item || item->selected < 0) return false;
        {
            const xx_ntfs_name *name = &item->names[item->selected];
            if (!name->present || !name->text ||
                xx_rt_strcmp(name->text, ".") == 0) {
                return false;
            }
            visited[depth] = id;
            parts[depth] = name->text;
            ++depth;
            parent = name->parent;
            /* A parent outside the reserved range must exist, be a directory
             * and still carry the sequence number the child recorded;
             * otherwise the reference is stale and the path is a guess. */
            if (parent >= XX_NTFS_FIRST_USER_RECORD ||
                parent == XX_NTFS_ROOT_RECORD) {
                const xx_ntfs_record *up = xx_ntfs_find(parsed, parent);
                if (!up || !up->folder || up->sequence != name->sequence) {
                    return false;
                }
            }
            id = parent;
        }
    }
    if (depth == 0U) return false;
    half = depth / 2U;
    for (index = 0U; index < half; ++index) {
        const char *swap = parts[index];
        parts[index] = parts[depth - 1U - index];
        parts[depth - 1U - index] = swap;
    }
    joined = xx_ntfs_join(parts, depth);
    if (!joined) return false;
    if (!xx_ntfs_retain(reader, (uint64_t)xx_str_len(joined) +
                                    (uint64_t)sizeof(xx_ntfs_entry))) {
        xx_str_free(joined);
        return false;
    }
    *out = joined;
    return true;
}

static int xx_ntfs_compare_names(const void *left, const void *right) {
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return xx_rt_strcmp(*a, *b);
}

/**
 * Reject duplicate paths. Two MFT records can legitimately decode to the same
 * name only in a crafted image, and extracting both would let the second
 * overwrite the first. Sorting is what keeps this out of O(n^2).
 */
static bool xx_ntfs_paths_unique(const xx_ntfs_private *parsed) {
    const char **names;
    size_t index;
    bool unique = true;
    if (!parsed || parsed->count < 2U) return true;
    if (parsed->count > SIZE_MAX / sizeof(*names)) return false;
    names = (const char **)xx_mem_alloc(parsed->count * sizeof(*names));
    if (!names) return false;
    for (index = 0U; index < parsed->count; ++index) {
        names[index] = parsed->entries[index].name;
    }
    xx_rt_qsort(names, parsed->count, sizeof(*names), xx_ntfs_compare_names);
    for (index = 1U; index < parsed->count; ++index) {
        if (xx_rt_strcmp(names[index - 1U], names[index]) == 0) {
            unique = false;
            break;
        }
    }
    xx_mem_free((void *)names);
    return unique;
}

/* --------------------------------------------------------------- parse --- */

static bool xx_ntfs_read_geometry(xx_ntfs_reader *reader,
                                  xx_ntfs_private *parsed) {
    uint8_t boot[XX_NTFS_BOOT_SIZE];
    uint64_t sector;
    uint64_t per_cluster;
    uint64_t sectors;
    uint64_t cluster;
    uint64_t bytes;
    uint64_t record_size = 0U;
    int encoded;
    if (!xx_ntfs_read(reader, 0U, boot, sizeof(boot), true) ||
        xx_rt_memcmp(boot + 3U, "NTFS    ", 8U) != 0 || boot[510] != 0x55U ||
        boot[511] != 0xaaU) {
        return false;
    }
    sector = xx_ntfs_u16(boot + 11U);
    per_cluster = boot[13];
    sectors = xx_ntfs_u64(boot + 40U);
    if (!xx_ntfs_power2(sector) || sector < 512U || sector > 4096U ||
        !xx_ntfs_power2(per_cluster) || per_cluster > 128U || sectors == 0U ||
        sectors > reader->size / sector) {
        return false;
    }
    cluster = sector * per_cluster;
    bytes = sectors * sector;
    /* Negative values encode a power of two in bytes; positive ones count
     * clusters. Anything else is not a file-record size. */
    encoded = (int)(int8_t)boot[64];
    if (encoded < 0 && encoded >= -16) {
        record_size = (uint64_t)1 << (unsigned)(-encoded);
    } else if (encoded > 0 && (uint64_t)encoded <= 65536U / cluster) {
        record_size = (uint64_t)encoded * cluster;
    }
    if (!xx_ntfs_power2(record_size) || record_size < sector ||
        record_size > 65536U || (record_size % sector) != 0U ||
        bytes > XX_NTFS_MAX_VOLUME) {
        return false;
    }
    parsed->volume_length = bytes;
    parsed->cluster_size = cluster;
    parsed->sector_size = sector;
    parsed->record_size = record_size;
    parsed->mft_cluster = xx_ntfs_u64(boot + 48U);
    if (parsed->mft_cluster > bytes / cluster ||
        !xx_ntfs_span(parsed->mft_cluster * cluster, record_size, bytes)) {
        return false;
    }
    return true;
}

/** Decode $MFT (record 0) and take ownership of its data stream. */
static bool xx_ntfs_read_mft(xx_ntfs_reader *reader, xx_ntfs_private *parsed) {
    xx_ntfs_record first;
    uint8_t *buffer;
    bool ok;
    size_t index;
    xx_mem_zero(&first, sizeof(first));
    first.selected = -1;
    first.header_offset = -1;
    buffer = (uint8_t *)xx_mem_alloc((size_t)parsed->record_size);
    if (!buffer) return false;
    ok = xx_ntfs_read(reader, parsed->mft_cluster * parsed->cluster_size,
                      buffer, parsed->record_size, true) &&
         xx_ntfs_parse_record(reader, parsed, buffer, 0U, &first);
    xx_mem_free(buffer);
    if (!ok || !first.has_data || first.unsupported || !first.data.nonresident ||
        first.data.size == 0U || first.data.size > XX_NTFS_MAX_MFT ||
        (first.data.size % parsed->record_size) != 0U ||
        first.data.size / parsed->record_size > XX_NTFS_MAX_RECORDS ||
        first.data.initialized != first.data.size) {
        xx_ntfs_record_cleanup(&first);
        return false;
    }
    for (index = 0U; index < first.data.run_count; ++index) {
        if (first.data.runs[index].sparse) {
            xx_ntfs_record_cleanup(&first);
            return false; /* A sparse MFT is not supported. */
        }
    }
    parsed->mft = first.data;
    xx_mem_zero(&first.data, sizeof(first.data));
    xx_ntfs_record_cleanup(&first);
    return true;
}

/** Decode every allocated, non-extension MFT record into parsed->records. */
static bool xx_ntfs_read_records(xx_ntfs_reader *reader,
                                 xx_ntfs_private *parsed) {
    uint64_t total = parsed->mft.size / parsed->record_size;
    uint8_t *buffer;
    uint64_t index;
    bool ok = true;
    if (total == 0U || total > XX_NTFS_MAX_RECORDS ||
        total > reader->keep_budget / sizeof(xx_ntfs_record) ||
        !xx_ntfs_retain(reader, total * (uint64_t)sizeof(xx_ntfs_record))) {
        return false;
    }
    parsed->records =
        (xx_ntfs_record *)xx_mem_calloc((size_t)total, sizeof(xx_ntfs_record));
    if (!parsed->records) return false;
    parsed->record_count = (size_t)total;
    for (index = 0U; index < total; ++index) {
        parsed->records[index].selected = -1;
        parsed->records[index].header_offset = -1;
    }
    buffer = (uint8_t *)xx_mem_alloc((size_t)parsed->record_size);
    if (!buffer) return false;
    for (index = 0U; index < total && ok; ++index) {
        uint64_t stream_offset = index * parsed->record_size;
        const xx_ntfs_run *run = NULL;
        uint64_t position;
        if (!xx_ntfs_active(reader) ||
            !xx_ntfs_read_stream(reader, &parsed->mft, stream_offset,
                                 parsed->record_size, buffer, true)) {
            ok = false;
            break;
        }
        if (xx_rt_memcmp(buffer, "FILE", 4U) != 0) {
            /* A wholly zeroed slot is an unused MFT entry; anything else with
             * a wrong signature means the mapping is not an MFT at all. */
            for (position = 0U; position < parsed->record_size; ++position) {
                if (buffer[position] != 0U) {
                    ok = false;
                    break;
                }
            }
            continue;
        }
        if ((xx_ntfs_u16(buffer + 22U) & 1U) == 0U) continue; /* Not in use. */
        if (!xx_ntfs_parse_record(reader, parsed, buffer, index,
                                  &parsed->records[index])) {
            ok = false;
            break;
        }
        if (parsed->records[index].extension) {
            xx_ntfs_record_cleanup(&parsed->records[index]);
            continue;
        }
        parsed->records[index].present = true;
        if (xx_ntfs_stream_locate(&parsed->mft, stream_offset, &run) &&
            !run->sparse &&
            run->physical + (stream_offset - run->logical) <=
                (uint64_t)(INT64_MAX - reader->base)) {
            parsed->records[index].header_offset =
                (int64_t)(run->physical + (stream_offset - run->logical));
        }
    }
    xx_mem_free(buffer);
    return ok && xx_ntfs_active(reader);
}

/** Publish one user record as an entry, moving its data stream across. */
static bool xx_ntfs_publish(xx_ntfs_reader *reader, xx_ntfs_private *parsed,
                            uint64_t index) {
    xx_ntfs_record *record = &parsed->records[index];
    xx_ntfs_entry entry;
    char *path = NULL;
    bool excluded = false;
    if (!xx_ntfs_build_path(reader, parsed, index, &path, &excluded)) {
        return false;
    }
    if (excluded) return true;
    if (!path) return false;
    /* A directory we cannot fully understand would silently drop its subtree,
     * so the whole volume is refused rather than published incomplete. */
    if (record->folder && record->unsupported) {
        xx_str_free(path);
        return false;
    }
    xx_mem_zero(&entry, sizeof(entry));
    entry.name = path;
    entry.unsupported = record->unsupported;
    entry.index = record->index;
    entry.named_streams = record->named_streams;
    entry.header_offset = record->header_offset;
    entry.folder = record->folder;
    if (record->folder) {
        xx_ntfs_stream_cleanup(&record->data);
    } else {
        entry.data = record->data;
        xx_mem_zero(&record->data, sizeof(record->data));
        if (!record->has_data && !entry.unsupported) {
            entry.unsupported = xx_ntfs_unsupported_no_data;
        }
    }
    if (!xx_ntfs_append_entry(parsed, &entry)) {
        xx_ntfs_entry_cleanup(&entry);
        return false;
    }
    return true;
}

static bool xx_ntfs_parse(Abstractformat *self, xx_ntfs_private *parsed,
                          xx_pd_struct *pd) {
    xx_ntfs_reader reader;
    const xx_ntfs_record *root;
    int64_t total_size;
    uint64_t index;
    /* Initialise before the guard clauses: callers clean up their stack copy
     * whatever this returns, and cleaning an uninitialised one would free
     * indeterminate pointers. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->volume_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < 0 || total_size < self->base_address) return false;
    xx_mem_zero(&reader, sizeof(reader));
    reader.device = self->device;
    reader.base = self->base_address;
    reader.size = (uint64_t)(total_size - self->base_address);
    reader.read_budget = XX_NTFS_READ_BUDGET;
    reader.keep_budget = XX_NTFS_KEEP_BUDGET;
    reader.pd = pd;
    if (reader.size < XX_NTFS_BOOT_SIZE ||
        reader.size > XX_NTFS_MAX_VOLUME ||
        !xx_ntfs_read_geometry(&reader, parsed) ||
        !xx_ntfs_read_mft(&reader, parsed) ||
        !xx_ntfs_read_records(&reader, parsed)) {
        goto fail;
    }
    root = xx_ntfs_find(parsed, XX_NTFS_ROOT_RECORD);
    if (!root || !root->folder) goto fail;
    for (index = XX_NTFS_FIRST_USER_RECORD;
         index < (uint64_t)parsed->record_count; ++index) {
        if (!xx_ntfs_active(&reader)) goto fail;
        if (!parsed->records[index].present ||
            parsed->records[index].selected < 0) {
            continue;
        }
        if (!xx_ntfs_publish(&reader, parsed, index)) goto fail;
    }
    if (!xx_ntfs_paths_unique(parsed) || !xx_ntfs_active(&reader)) goto fail;
    if (parsed->volume_length > (uint64_t)(INT64_MAX - self->base_address)) {
        goto fail;
    }
    parsed->volume_end = self->base_address + (int64_t)parsed->volume_length;
    return true;
fail:
    xx_ntfs_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------- records --- */

static bool xx_ntfs_copy_options(xx_list_s *destination,
                                 const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ntfs_find_option(const xx_list_s *options,
                                         uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_ntfs_populate_record(xx_archive_record *record,
                                    const xx_ntfs_entry *entry,
                                    int64_t base_address) {
    int64_t data_offset = -1;
    if (!record || !entry || !entry->name) return false;
    /* Only a stream that starts with a real, mapped run has a meaningful
     * single device offset; resident and fragmented ones do not. */
    if (entry->data.nonresident && entry->data.run_count != 0U &&
        !entry->data.runs[0].sparse &&
        entry->data.runs[0].physical <= (uint64_t)(INT64_MAX - base_address)) {
        data_offset = base_address + (int64_t)entry->data.runs[0].physical;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        entry->header_offset >= 0
            ? base_address + entry->header_offset
            : (int64_t)-1;
    record->header_size = -1;
    record->data_offset = data_offset;
    record->compressed_size = (int64_t)entry->data.size;
    if (!xx_archive_record_set_original_name(record, entry->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        entry->data.size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        entry->data.size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         entry->folder)) {
        return false;
    }
    if (entry->unsupported &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                        entry->unsupported)) {
        return false;
    }
    return true;
}

static void xx_ntfs_archive_stream_free(void *pointer) {
    xx_ntfs_archive_stream *stream = (xx_ntfs_archive_stream *)pointer;
    if (!stream) return;
    xx_ntfs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------- life cycle --- */

void xx_ntfs_init(xx_ntfs *ntfs, xx_io_device *dev, int64_t base_address) {
    if (!ntfs) return;
    xx_mem_zero(ntfs, sizeof(*ntfs));
    xx_format_init(&ntfs->format, dev, base_address);
    ntfs->format.endian = XX_ENDIAN_LITTLE;
    ntfs->format.file_type = XX_FILE_TYPE_NTFS;
    ntfs->format.format_type = XX_TYPE_ARCHIVE;
    ntfs->format.is_archive = true;
    xx_format_set_mime_type(&ntfs->format, "application/octet-stream");
    xx_format_set_extension(&ntfs->format, "ntfs");
    ntfs->format.check_is_valid = xx_ntfs_check_is_valid;
    ntfs->format.handle_base_info = xx_ntfs_handle_base_info;
    ntfs->format.get_format_size = xx_ntfs_get_format_size;
    ntfs->format.get_number_of_archive_records =
        xx_ntfs_get_number_of_archive_records;
    ntfs->format.create_archive_records_reading =
        xx_ntfs_create_archive_records_reading;
    ntfs->format.get_current_archive_record =
        xx_ntfs_get_current_archive_record;
    ntfs->format.unpack_current_archive_record =
        xx_ntfs_unpack_current_archive_record;
    ntfs->format.archive_record_move_to_next =
        xx_ntfs_archive_record_move_to_next;
    ntfs->format.free_archive_records_reading =
        xx_ntfs_free_archive_records_reading;
    ntfs->format.destroy = xx_ntfs_vtable_destroy;
    ntfs->volume_end = -1;
}

xx_ntfs *xx_ntfs_create(xx_io_device *dev, int64_t base_address) {
    xx_ntfs *ntfs = (xx_ntfs *)xx_mem_alloc(sizeof(*ntfs));
    if (ntfs) xx_ntfs_init(ntfs, dev, base_address);
    return ntfs;
}

void xx_ntfs_destroy(xx_ntfs *ntfs) {
    if (!ntfs) return;
    if (ntfs->internal) {
        xx_ntfs_private_cleanup((xx_ntfs_private *)ntfs->internal);
        xx_mem_free(ntfs->internal);
        ntfs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&ntfs->format);
}

static void xx_ntfs_vtable_destroy(Abstractformat *self) {
    xx_ntfs_destroy((xx_ntfs *)self);
}

void xx_ntfs_free(xx_ntfs *ntfs) {
    if (!ntfs) return;
    xx_ntfs_destroy(ntfs);
    xx_mem_free(ntfs);
}

/* ---------------------------------------------------------------- API --- */

bool xx_ntfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ntfs_private parsed;
    bool result = xx_ntfs_parse(self, &parsed, pd);
    xx_ntfs_private_cleanup(&parsed);
    return result;
}

bool xx_ntfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ntfs_private *parsed;
    xx_ntfs *ntfs = (xx_ntfs *)self;
    int64_t total_size;
    if (!self) return false;
    parsed = (xx_ntfs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_ntfs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (ntfs->internal) {
        xx_ntfs_private_cleanup((xx_ntfs_private *)ntfs->internal);
        xx_mem_free(ntfs->internal);
    }
    ntfs->internal = parsed;
    ntfs->number_of_records = parsed->count;
    ntfs->number_of_members = parsed->count;
    ntfs->bytes_per_sector = (uint32_t)parsed->sector_size;
    ntfs->bytes_per_cluster = (uint32_t)parsed->cluster_size;
    ntfs->file_record_size = (uint32_t)parsed->record_size;
    ntfs->mft_cluster = parsed->mft_cluster;
    ntfs->volume_size = parsed->volume_length;
    ntfs->volume_end = parsed->volume_end;
    self->format_size = parsed->volume_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->volume_end) {
        self->overlay_offset = parsed->volume_end;
        self->overlay_size = total_size - parsed->volume_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ntfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ntfs_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_ntfs *)self)->number_of_records;
}

xx_archive_record_state *xx_ntfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_ntfs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ntfs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_ntfs_copy_options(&state->options, options) ||
        !xx_ntfs_parse(self, &stream->parsed, pd)) {
        xx_ntfs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_ntfs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_ntfs_populate_record(&state->current_record,
                                &stream->parsed.entries[0],
                                self->base_address)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_ntfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ntfs_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_ntfs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ntfs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_ntfs_populate_record(&state->current_record,
                                 &stream->parsed.entries[stream->index],
                                 self->base_address)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

/** Stream one entry's data stream, run by run, into an already-created file. */
static bool xx_ntfs_write_entry(Abstractformat *self,
                                const xx_ntfs_private *parsed,
                                const xx_ntfs_entry *entry,
                                const char *destination, xx_pd_struct *pd) {
    xx_ntfs_reader reader;
    xx_io_device *output;
    uint8_t *buffer;
    uint64_t offset = 0U;
    bool ok = true;
    bool created = false;
    int64_t total_size = xx_io_total_size(self->device);
    (void)parsed;
    if (total_size < 0 || total_size < self->base_address) return false;
    xx_mem_zero(&reader, sizeof(reader));
    reader.device = self->device;
    reader.base = self->base_address;
    reader.size = (uint64_t)(total_size - self->base_address);
    reader.read_budget = XX_NTFS_READ_BUDGET;
    reader.keep_budget = XX_NTFS_KEEP_BUDGET;
    reader.pd = pd;
    buffer = (uint8_t *)xx_mem_alloc(XX_NTFS_CHUNK);
    if (!buffer) return false;
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    if (!output) {
        xx_mem_free(buffer);
        return false;
    }
    while (ok && offset < entry->data.size) {
        uint64_t count = entry->data.size - offset;
        uint64_t written = 0U;
        if (count > XX_NTFS_CHUNK) count = XX_NTFS_CHUNK;
        if (!xx_ntfs_read_stream(&reader, &entry->data, offset, count, buffer,
                                 false)) {
            ok = false;
            break;
        }
        while (written < count) {
            ssize_t put = xx_io_write(output, buffer + written,
                                      (size_t)(count - written));
            if (put <= 0 || (uint64_t)put > count - written) {
                ok = false;
                break;
            }
            written += (uint64_t)put;
        }
        offset += count;
    }
    xx_io_close(output);
    xx_mem_free(buffer);
    if (!ok && created) xx_rt_remove(destination);
    return ok;
}

bool xx_ntfs_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ntfs_archive_stream *stream;
    const xx_ntfs_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ntfs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_ntfs_safe_name(entry->name)) return false;
    /* An entry we could only list is never written to disk. */
    if (entry->unsupported) return false;
    option = xx_ntfs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the stream is fully mapped instead. */
        return entry->folder || entry->data.size == 0U ||
               entry->data.run_count != 0U || !entry->data.nonresident;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", entry->name);
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (!destination) goto cleanup;
    if (entry->folder) {
        result = xx_io_create_dirs_a(destination, true);
    } else if (xx_io_create_dirs_a(destination, false)) {
        result = xx_ntfs_write_entry(self, &stream->parsed, entry, destination,
                                     pd);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_ntfs_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_ntfs_get_number_of_records(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->number_of_records : 0U;
}
uint64_t xx_ntfs_get_number_of_members(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->number_of_members : 0U;
}
uint32_t xx_ntfs_get_bytes_per_sector(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->bytes_per_sector : 0U;
}
uint32_t xx_ntfs_get_bytes_per_cluster(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->bytes_per_cluster : 0U;
}
uint32_t xx_ntfs_get_file_record_size(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->file_record_size : 0U;
}
uint64_t xx_ntfs_get_mft_cluster(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->mft_cluster : 0U;
}
uint64_t xx_ntfs_get_volume_size(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->volume_size : 0U;
}
int64_t xx_ntfs_get_volume_end(const xx_ntfs *ntfs) {
    return ntfs ? ntfs->volume_end : -1;
}
