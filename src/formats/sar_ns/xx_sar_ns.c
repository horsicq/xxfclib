/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * NScripter SAR resource archive ("arc.sar").  xx_sar_ns.h carries the field
 * table and the member-name rules.  Written from the format's structure; the
 * acceptance rules (index ends exactly at base, members ascending, data area
 * covered to EOF) follow XArchive's games/xnscripter.cpp (MIT), and GARbro's
 * ArcFormats/NScripter/ArcSAR.cs (MIT, morkt) was the reference for the
 * layout.  No code is taken from either.
 *
 * The container has no magic, so the index walk is the validator: every
 * check below that a real archive always passes is also what keeps this
 * "count + base + name table" shape from matching unrelated files.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sar_ns/xx_sar_ns.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as SAR_NS is registered there. */
#ifdef SAR_NS
#define XX_SAR_NS_FILE_TYPE XX_FILE_TYPE_SAR_NS
#else
#define XX_SAR_NS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SAR_HEADER_SIZE 6
#define SAR_FIXED_SIZE 8
/* Real names are short relative paths; 1024 bytes is far beyond any of them
 * and bounds both the probe's per-entry scan and the name buffers. */
#define SAR_MAX_NAME 1024
/* One name byte, its NUL and the two u32 fields. */
#define SAR_MIN_ENTRY (1 + 1 + SAR_FIXED_SIZE)
#define SAR_MAX_ENTRY (SAR_MAX_NAME + 1 + SAR_FIXED_SIZE)
/* The index is walked through a window of this size, never loaded whole. */
#define SAR_WINDOW 65536
/* Extraction streams through a buffer of this size, never a whole member. */
#define SAR_COPY_CHUNK 65536U
/* A converted name: at most "%XX" per raw byte, then "%_" and up to five
 * digits of entry index, then the terminator. */
#define SAR_NAME_BUFFER (3 * SAR_MAX_NAME + 2 + 5 + 1)
/* How often the walk polls for a stop request. */
#define SAR_POLL_MASK 0x3ffU

typedef struct sar_member_s {
    int64_t entry_offset; /**< Absolute offset of the entry (its name). */
    int64_t data_offset;  /**< Absolute offset of the data. */
    int64_t size;
    uint32_t name_length;
    bool renamed; /**< Duplicate name: "%_<index>" is inserted. */
} sar_member;

typedef struct sar_key_s {
    uint64_t hash; /**< Of the converted name, ASCII folded to lower case. */
    uint32_t index;
} sar_key;

typedef struct sar_layout_s {
    int64_t archive_size; /**< From base_address to EOF. */
    int64_t data_base;    /**< The base field. */
    uint32_t count;
} sar_layout;

typedef struct sar_window_s {
    xx_io_device *device;
    int64_t origin; /**< Absolute offset of index byte 0. */
    int64_t size;   /**< Index size: base - 6. */
    int64_t start;  /**< Index-relative offset of buffer[0]. */
    size_t length;
    uint8_t *buffer;
} sar_window;

typedef struct sar_stream_s {
    sar_member *items;
    size_t count;
    size_t index;
    char *name; /**< SAR_NAME_BUFFER bytes: the current member's name. */
} sar_stream;

static uint16_t sar_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t sar_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool sar_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Stream `size` bytes at `offset` into `destination` (or just read them
 * through when it is NULL) in fixed chunks, so a member as large as the file
 * never becomes an allocation of that size. */
static bool sar_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(SAR_COPY_CHUNK);
    if (!buffer) return false;
    while (ok && remaining > 0) {
        size_t chunk = remaining > (int64_t)SAR_COPY_CHUNK
                           ? (size_t)SAR_COPY_CHUNK
                           : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !sar_read_at(source, offset + (size - remaining), buffer, chunk)) {
            ok = false;
            break;
        }
        while (destination && written < chunk) {
            ssize_t amount = xx_io_write(destination, buffer + written,
                                         chunk - written);
            if (amount <= 0 || (size_t)amount > chunk - written) {
                ok = false;
                break;
            }
            written += (size_t)amount;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* ---- member names ------------------------------------------------------ */

static bool sar_is_sjis_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool sar_is_sjis_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t sar_put_escape(char *out, uint8_t c) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* Raw Shift-JIS name -> ASCII path (see xx_sar_ns.h).  A double-byte
 * character is escaped as a unit: its trail byte may be 0x5C or 0x7C, which
 * must never turn into a separator or a '|'.  `out` holds at least
 * 3 * length + 1 bytes; returns the converted length. */
static size_t sar_convert_name(const uint8_t *raw, size_t length, char *out) {
    size_t at = 0U;
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        if (sar_is_sjis_lead(c) && index + 1U < length &&
            sar_is_sjis_trail(raw[index + 1U])) {
            at += sar_put_escape(out + at, c);
            at += sar_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c == (uint8_t)'%')
            at += sar_put_escape(out + at, c);
        else if (c == (uint8_t)'\\')
            out[at++] = '/';
        else
            out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

/* 64-bit FNV-1a over the converted name with ASCII folded to lower case, so
 * names that a case-insensitive filesystem treats as one hash alike.  A
 * collision between different names only renames a member needlessly. */
static uint64_t sar_name_hash(const char *name, size_t length) {
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
 * it when there is none).  `name` has room for SAR_NAME_BUFFER bytes. */
static void sar_insert_suffix(char *name, size_t length, uint32_t index) {
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
    if (length + suffix_length >= SAR_NAME_BUFFER) return;
    tail = length - dot;
    /* Shift the extension (and the terminator) right, then fill the gap. */
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$,
 * CONOUT$) as the part of a component before its first '.', trailing spaces
 * ignored: "nul", "Con.txt" and "aux .dat" all open the device. */
static bool sar_reserved_component(const char *segment, size_t length) {
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
 * components, components ending in '.' or ' ' (this covers "." and "..", and
 * the names Windows would trim into a collision), device names, control
 * characters and the characters no Windows path may carry. */
static bool sar_safe_name(const char *name) {
    const char *segment;
    const char *at;
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
                sar_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- index walk -------------------------------------------------------- */

static bool sar_read_header(Abstractformat *format, sar_layout *layout) {
    uint8_t header[SAR_HEADER_SIZE];
    int64_t total, size, base, index_size;
    uint32_t count;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)SAR_HEADER_SIZE + SAR_MIN_ENTRY ||
        !sar_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    count = (uint32_t)sar_be16(header);
    base = (int64_t)sar_be32(header + 2U);
    if (count == 0U || base > size) return false;
    /* The index must hold `count` entries of 10..1033 bytes each. */
    index_size = base - SAR_HEADER_SIZE;
    if (index_size < (int64_t)count * SAR_MIN_ENTRY ||
        index_size > (int64_t)count * SAR_MAX_ENTRY)
        return false;
    layout->archive_size = size;
    layout->data_base = base;
    layout->count = count;
    return true;
}

/* Return a pointer to index byte `pos` with at least
 * min(SAR_MAX_ENTRY, size - pos) bytes behind it; *avail gets the real
 * count.  NULL at or past the end of the index, or on a read error. */
static const uint8_t *sar_window_view(sar_window *window, int64_t pos,
                                      size_t *avail) {
    int64_t want = window->size - pos;
    if (pos < 0 || want <= 0) return NULL;
    if (want > SAR_MAX_ENTRY) want = SAR_MAX_ENTRY;
    if (pos < window->start ||
        pos + want > window->start + (int64_t)window->length) {
        int64_t chunk = window->size - pos;
        if (chunk > SAR_WINDOW) chunk = SAR_WINDOW;
        window->length = 0U;
        if (!sar_read_at(window->device, window->origin + pos, window->buffer,
                         (size_t)chunk))
            return NULL;
        window->start = pos;
        window->length = (size_t)chunk;
    }
    *avail = (size_t)(window->start + (int64_t)window->length - pos);
    return window->buffer + (pos - window->start);
}

/* Parse one entry from `view` (which reaches the end of the index or holds
 * at least SAR_MAX_ENTRY bytes).  Returns the entry's length, 0 if it is
 * malformed: an empty or over-long name, a control byte in the name, or a
 * name or fixed part running past the end of the index. */
static size_t sar_parse_entry(const uint8_t *view, size_t avail,
                              uint32_t *name_length, uint32_t *offset,
                              uint32_t *size) {
    size_t at;
    for (at = 0U; at < avail && at <= SAR_MAX_NAME; ++at) {
        uint8_t c = view[at];
        if (c == 0U) break;
        /* Shift-JIS (and every other code page NScripter ports used) keeps
         * bytes below 0x20 out of file names; one here means the walk is
         * reading something that is not an index. */
        if (c < 0x20U || c == 0x7fU) return 0U;
    }
    if (at == 0U || at > SAR_MAX_NAME || at >= avail ||
        avail - at - 1U < SAR_FIXED_SIZE)
        return 0U;
    *name_length = (uint32_t)at;
    *offset = sar_be32(view + at + 1U);
    *size = sar_be32(view + at + 5U);
    return at + 1U + SAR_FIXED_SIZE;
}

/* Members come in ascending order, never overlap and stay inside the data
 * area.  Real writers lay them out back to back; allowing gaps but not
 * overlap keeps the total output bounded by the file size. */
static bool sar_member_fits(uint32_t offset, uint32_t size,
                            int64_t previous_end, int64_t data_size) {
    return (int64_t)offset >= previous_end && (int64_t)offset <= data_size &&
           (int64_t)size <= data_size - (int64_t)offset;
}

/* Walk the whole index.  With `items` NULL this is the probe and keeps
 * nothing; otherwise it fills items[] and keys[] (count entries each), using
 * `name` (SAR_NAME_BUFFER bytes) to hash every converted name. */
static bool sar_walk(Abstractformat *format, const sar_layout *layout,
                     sar_member *items, sar_key *keys, char *name,
                     xx_pd_struct *pd) {
    sar_window window;
    int64_t index_size = layout->data_base - SAR_HEADER_SIZE;
    int64_t data_size = layout->archive_size - layout->data_base;
    int64_t pos = 0, previous_end = 0;
    uint32_t index;
    bool ok = false;

    /* Cheap gate before anything is allocated: entry 0 from one small read.
     * The walk doubles as the magic-less detection probe, so garbage that
     * got past the header has to fall out here. */
    {
        uint8_t first[SAR_MAX_ENTRY];
        size_t avail = index_size < (int64_t)SAR_MAX_ENTRY
                           ? (size_t)index_size
                           : (size_t)SAR_MAX_ENTRY;
        uint32_t name_length, offset, size;
        if (!sar_read_at(format->device,
                         format->base_address + SAR_HEADER_SIZE, first,
                         avail) ||
            sar_parse_entry(first, avail, &name_length, &offset, &size) ==
                0U ||
            !sar_member_fits(offset, size, 0, data_size))
            return false;
    }

    xx_mem_zero(&window, sizeof(window));
    window.device = format->device;
    window.origin = format->base_address + SAR_HEADER_SIZE;
    window.size = index_size;
    window.buffer = (uint8_t *)xx_mem_alloc(SAR_WINDOW);
    if (!window.buffer) return false;

    for (index = 0U; index < layout->count; ++index) {
        const uint8_t *view;
        size_t avail = 0U, length;
        uint32_t name_length, offset, size;
        if ((index & SAR_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            goto done;
        view = sar_window_view(&window, pos, &avail);
        if (!view) goto done;
        length = sar_parse_entry(view, avail, &name_length, &offset, &size);
        if (length == 0U ||
            !sar_member_fits(offset, size, previous_end, data_size))
            goto done;
        if (items) {
            size_t converted = sar_convert_name(view, name_length, name);
            items[index].entry_offset = window.origin + pos;
            items[index].data_offset =
                format->base_address + layout->data_base + (int64_t)offset;
            items[index].size = (int64_t)size;
            items[index].name_length = name_length;
            items[index].renamed = false;
            keys[index].hash = sar_name_hash(name, converted);
            keys[index].index = index;
        }
        previous_end = (int64_t)offset + (int64_t)size;
        pos += (int64_t)length;
    }
    /* The entries must end exactly at base, and the last member exactly at
     * EOF: with no magic, this extent proof is the recognition. */
    ok = pos == index_size && previous_end == data_size;
done:
    xx_mem_free(window.buffer);
    return ok;
}

static int sar_compare_keys(const void *left, const void *right) {
    const sar_key *a = (const sar_key *)left;
    const sar_key *b = (const sar_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Sorting puts every group of equal (case-folded) names together, lowest
 * entry index first; that one keeps its name and the rest are renamed. */
static void sar_mark_duplicates(sar_member *items, sar_key *keys,
                                size_t count) {
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), sar_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            keys[index].index < count)
            items[keys[index].index].renamed = true;
}

static void sar_stream_free(void *opaque) {
    sar_stream *stream = (sar_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool sar_open_stream(Abstractformat *format, sar_stream **result,
                            xx_pd_struct *pd) {
    sar_layout layout;
    sar_member *items = NULL;
    sar_key *keys = NULL;
    char *name = NULL;
    sar_stream *stream = NULL;
    if (!result || !sar_read_header(format, &layout)) return false;
    /* At most 65535 entries: 2.5 MiB of bookkeeping, never more. */
    items = (sar_member *)xx_mem_calloc(layout.count, sizeof(*items));
    keys = (sar_key *)xx_mem_alloc((size_t)layout.count * sizeof(*keys));
    name = (char *)xx_mem_alloc(SAR_NAME_BUFFER);
    stream = (sar_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!items || !keys || !name || !stream ||
        !sar_walk(format, &layout, items, keys, name, pd))
        goto fail;
    sar_mark_duplicates(items, keys, layout.count);
    xx_mem_free(keys);
    stream->items = items;
    stream->count = layout.count;
    stream->name = name;
    *result = stream;
    return true;
fail:
    if (items) xx_mem_free(items);
    if (keys) xx_mem_free(keys);
    if (name) xx_mem_free(name);
    if (stream) xx_mem_free(stream);
    return false;
}

/* Re-read member `index`'s raw name and leave its converted (and, for a
 * duplicate, suffixed) form in stream->name. */
static bool sar_load_name(Abstractformat *format, sar_stream *stream,
                          size_t index) {
    const sar_member *member = &stream->items[index];
    uint8_t raw[SAR_MAX_NAME];
    size_t length;
    if (member->name_length == 0U || member->name_length > SAR_MAX_NAME ||
        !sar_read_at(format->device, member->entry_offset, raw,
                     member->name_length))
        return false;
    length = sar_convert_name(raw, member->name_length, stream->name);
    if (member->renamed) sar_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

static bool sar_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *sar_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sar_set_record(Abstractformat *format, xx_archive_record *record,
                           sar_stream *stream, size_t index) {
    const sar_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!sar_load_name(format, stream, index)) return false;
    record->header_offset = member->entry_offset;
    record->header_size = (int64_t)member->name_length + 1 + SAR_FIXED_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_sar_ns_init(xx_sar_ns *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_SAR_NS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "sar");
    archive->format.check_is_valid = xx_sar_ns_check_is_valid;
    archive->format.handle_base_info = xx_sar_ns_handle_base_info;
    archive->format.get_format_size = xx_sar_ns_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sar_ns_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sar_ns_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sar_ns_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sar_ns_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sar_ns_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sar_ns_free_archive_records_reading;
    archive->data_base = -1;
}

xx_sar_ns *xx_sar_ns_create(xx_io_device *device, int64_t base_address) {
    xx_sar_ns *archive = (xx_sar_ns *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sar_ns_init(archive, device, base_address);
    return archive;
}

void xx_sar_ns_destroy(xx_sar_ns *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sar_ns_free(xx_sar_ns *archive) {
    if (!archive) return;
    xx_sar_ns_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sar_ns_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    sar_layout layout;
    return sar_read_header(format, &layout) &&
           sar_walk(format, &layout, NULL, NULL, NULL, pd);
}

bool xx_sar_ns_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    sar_layout layout;
    xx_sar_ns *archive;
    if (!sar_read_header(format, &layout) ||
        !sar_walk(format, &layout, NULL, NULL, NULL, pd))
        return false;
    archive = (xx_sar_ns *)format;
    archive->number_of_records = layout.count;
    archive->data_base = layout.data_base;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sar_ns_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sar_ns_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sar_ns_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sar_ns_handle_base_info(format, pd))
               ? ((xx_sar_ns *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_sar_ns_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sar_stream *stream;
    xx_archive_record_state *state;
    if (!sar_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sar_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sar_stream_free;
    state->total_records = stream->count;
    if (!sar_copy_options(&state->options, options) ||
        !sar_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sar_ns_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sar_ns_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    sar_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sar_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = sar_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_sar_ns_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    sar_stream *stream;
    const sar_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sar_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->size < 0 || member->data_offset < 0) return false;
    path_option = sar_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the member through, which verifies it. */
        return sar_copy_range(format->device, member->data_offset,
                              member->size, NULL, pd);
    /* stream->name was built from the file by sar_load_name: refuse it
     * before anything is created when it could escape the output folder or
     * name a device. */
    if (!sar_safe_name(stream->name)) return false;
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
        created = destination != NULL;
        if (!destination) goto done;
        result = sar_copy_range(format->device, member->data_offset,
                                member->size, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sar_ns_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
