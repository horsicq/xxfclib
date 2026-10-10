/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * NScripter NS2 resource archive (*.ns2).  xx_ns2.h carries the field table
 * and the member-name rules.  Written from the format's structure.  The
 * window, streaming copy and member-name handling follow this library's
 * sar_ns reader (src/formats/sar_ns/xx_sar_ns.c, MIT); the acceptance rules
 * (terminating 'e' exactly at base - 1, sizes covering the data area to EOF)
 * follow XArchive's games/xnscripter.cpp (MIT), and GARbro's
 * ArcFormats/NScripter/ArcNS2.cs (MIT, morkt) was the reference for the
 * layout.  No code is taken from either.
 *
 * The container has no magic, so the index walk is the validator: every
 * check below that a real archive always passes is also what keeps the
 * "u32 base + quoted name table" shape from matching unrelated files.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ns2/xx_ns2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef NS2
#define XX_NS2_FILE_TYPE XX_FILE_TYPE_NS2
#else
#define XX_NS2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NS2_HEADER_SIZE 4
#define NS2_SIZE_FIELD 4
/* Real names are short relative paths; 1024 bytes bounds both the per-entry
 * scan and the name buffers. */
#define NS2_MAX_NAME 1024
/* '"', one name byte, '"', the u32 size. */
#define NS2_MIN_ENTRY (1 + 1 + 1 + NS2_SIZE_FIELD)
#define NS2_MAX_ENTRY (1 + NS2_MAX_NAME + 1 + NS2_SIZE_FIELD)
/* The smallest index: one entry and the 'e' terminator. */
#define NS2_MIN_INDEX (NS2_MIN_ENTRY + 1)
/* The index is at most 16 MiB and holds at most 65535 entries. */
#define NS2_MAX_INDEX (16LL * 1024 * 1024)
#define NS2_MAX_MEMBERS 65535U
/* A converted name: at most "%XX" per raw byte, then "%_" and up to five
 * digits of entry index, then the terminator. */
#define NS2_NAME_BUFFER (3 * NS2_MAX_NAME + 2 + 5 + 1)
#define NS2_POLL_MASK 0x3ffU

typedef struct ns2_member_s {
    int64_t name_offset; /**< Absolute offset of the name's first byte. */
    int64_t data_offset; /**< Absolute offset of the data. */
    int64_t size;
    uint32_t name_length;
    bool renamed; /**< Duplicate name: "%_<index>" is inserted. */
} ns2_member;

typedef struct ns2_key_s {
    uint64_t hash; /**< Of the converted name, ASCII folded to lower case. */
    uint32_t index;
} ns2_key;

typedef struct ns2_layout_s {
    int64_t archive_size; /**< From base_address to EOF. */
    int64_t data_base;    /**< The base field. */
} ns2_layout;

typedef struct ns2_window_s {
    xx_io_device *device;
    int64_t origin; /**< Absolute offset of index byte 0. */
    int64_t size;   /**< Index size: base - 4. */
    int64_t start;  /**< Index-relative offset of buffer[0]. */
    size_t length;
    uint8_t *buffer;
    size_t capacity; /**< At least NS2_MAX_ENTRY (see ns2_capacity). */
} ns2_window;

typedef struct ns2_stream_s {
    ns2_member *items;
    size_t count;
    size_t index;
    char *name; /**< NS2_NAME_BUFFER bytes: the current member's name. */
} ns2_stream;

static size_t ns2_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < NS2_MAX_ENTRY) n = NS2_MAX_ENTRY;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static ssize_t ns2_read_some(xx_io_device *device, void *buffer, size_t size, size_t capacity)
{
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static ssize_t ns2_write_some(xx_io_device *device, const void *buffer, size_t size, size_t capacity)
{
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_write(device, (const uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static bool ns2_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    const size_t capacity = ns2_capacity();
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = ns2_read_some(device, (uint8_t *)buffer + done, size - done, capacity);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Stream `size` bytes at `offset` into `destination` (or just read them
 * through when it is NULL) in fixed chunks. */
static bool ns2_copy_range(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t capacity = ns2_capacity();
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (ok && remaining > 0) {
        size_t chunk = remaining > (int64_t)capacity ? capacity : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || !ns2_read_at(source, offset + (size - remaining), buffer, chunk)) {
            ok = false;
            break;
        }
        while (destination && written < chunk) {
            ssize_t amount = ns2_write_some(destination, buffer + written, chunk - written, capacity);
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

static bool ns2_is_sjis_lead(uint8_t c)
{
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool ns2_is_sjis_trail(uint8_t c)
{
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t ns2_put_escape(char *out, uint8_t c)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* Raw Shift-JIS name -> ASCII path (see xx_ns2.h).  A double-byte character
 * is escaped as a unit, so a trail byte of 0x5C never becomes a separator.
 * `out` holds at least 3 * length + 1 bytes; returns the converted length. */
static size_t ns2_convert_name(const uint8_t *raw, size_t length, char *out)
{
    size_t at = 0U;
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        if (ns2_is_sjis_lead(c) && index + 1U < length && ns2_is_sjis_trail(raw[index + 1U])) {
            at += ns2_put_escape(out + at, c);
            at += ns2_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c == (uint8_t)'%') at += ns2_put_escape(out + at, c);
        else if (c == (uint8_t)'\\') out[at++] = '/';
        else out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

/* 64-bit FNV-1a with ASCII folded to lower case. */
static uint64_t ns2_name_hash(const char *name, size_t length)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* Insert "%_<index>" before the extension of the last component (or append
 * it).  `name` has room for NS2_NAME_BUFFER bytes. */
static void ns2_insert_suffix(char *name, size_t length, uint32_t index)
{
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
    if (length + suffix_length >= NS2_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at) name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* A Windows device name as the part of a component before its first '.',
 * trailing spaces ignored. */
static bool ns2_reserved_component(const char *segment, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ') --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') || (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length && xx_rt_memcmp(stem, devices[index], stem_length) == 0) return true;
    return false;
}

/* Refuse absolute paths, drive letters and streams, empty components,
 * components ending in '.' or ' ' (covers "." and ".."), device names,
 * control characters and characters no Windows path may carry. */
static bool ns2_safe_name(const char *name)
{
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\' || c == 0x7fU || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || ns2_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- index walk -------------------------------------------------------- */

static bool ns2_read_header(Abstractformat *format, ns2_layout *layout)
{
    uint8_t header[NS2_HEADER_SIZE + 2];
    int64_t total, size, base;
    if (!format || !format->device || !layout || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)NS2_HEADER_SIZE + NS2_MIN_INDEX || !ns2_read_at(format->device, format->base_address, header, sizeof(header))) return false;
    base = (int64_t)xx_data_get_u32(header, 4, 0, false);
    /* Cheap rejection: the index starts with a quote and a name byte. */
    if (header[4] != (uint8_t)'"' || header[5] < 0x20U || header[5] == 0x7fU || header[5] == (uint8_t)'"') return false;
    if (base < (int64_t)NS2_HEADER_SIZE + NS2_MIN_INDEX || base > size || base - NS2_HEADER_SIZE > NS2_MAX_INDEX) return false;
    layout->archive_size = size;
    layout->data_base = base;
    return true;
}

/* Return a pointer to index byte `pos` with at least
 * min(NS2_MAX_ENTRY, size - pos) bytes behind it; *avail gets the real
 * count.  NULL at or past the end of the index, or on a read error. */
static const uint8_t *ns2_window_view(ns2_window *window, int64_t pos, size_t *avail)
{
    int64_t want = window->size - pos;
    if (pos < 0 || want <= 0) return NULL;
    if (want > NS2_MAX_ENTRY) want = NS2_MAX_ENTRY;
    if (pos < window->start || pos + want > window->start + (int64_t)window->length) {
        int64_t chunk = window->size - pos;
        if ((uint64_t)chunk > window->capacity) chunk = (int64_t)window->capacity;
        window->length = 0U;
        if (!ns2_read_at(window->device, window->origin + pos, window->buffer, (size_t)chunk)) return NULL;
        window->start = pos;
        window->length = (size_t)chunk;
    }
    *avail = (size_t)(window->start + (int64_t)window->length - pos);
    return window->buffer + (pos - window->start);
}

/* Parse one entry from `view`, of which `avail` bytes may belong to it.
 * Returns the entry's length, 0 if it is malformed. */
static size_t ns2_parse_entry(const uint8_t *view, size_t avail, uint32_t *name_length, uint32_t *size)
{
    size_t at;
    if (avail < NS2_MIN_ENTRY || view[0] != (uint8_t)'"') return 0U;
    for (at = 1U; at < avail && at <= NS2_MAX_NAME + 1U; ++at) {
        uint8_t c = view[at];
        if (c == (uint8_t)'"') break;
        if (c < 0x20U || c == 0x7fU) return 0U;
    }
    if (at >= avail || view[at] != (uint8_t)'"' || at == 1U || at - 1U > NS2_MAX_NAME || avail - at - 1U < NS2_SIZE_FIELD) return 0U;
    *name_length = (uint32_t)(at - 1U);
    *size = xx_data_get_u32(view + at + 1U, 4, 0, false);
    return at + 1U + NS2_SIZE_FIELD;
}

/* Walk the whole index.  With `items` NULL this is the probe and only
 * counts; otherwise it fills items[] and keys[] (`limit` entries each),
 * using `name` (NS2_NAME_BUFFER bytes) to hash every converted name. */
static bool ns2_walk(Abstractformat *format, const ns2_layout *layout, ns2_member *items, ns2_key *keys, char *name, uint32_t limit, uint32_t *count_out,
                     xx_pd_struct *pd)
{
    ns2_window window;
    int64_t index_size = layout->data_base - NS2_HEADER_SIZE;
    int64_t data_size = layout->archive_size - layout->data_base;
    int64_t origin = format->base_address + NS2_HEADER_SIZE;
    int64_t pos = 0, running = 0;
    uint32_t count = 0U;
    bool ok = false;

    /* Cheap gate before anything is allocated: entry 0 from one small read. */
    {
        uint8_t first[NS2_MAX_ENTRY];
        size_t avail = index_size - 1 < (int64_t)NS2_MAX_ENTRY ? (size_t)(index_size - 1) : (size_t)NS2_MAX_ENTRY;
        uint32_t name_length, size;
        if (!ns2_read_at(format->device, origin, first, avail) || ns2_parse_entry(first, avail, &name_length, &size) == 0U || (int64_t)size > data_size) return false;
    }

    xx_mem_zero(&window, sizeof(window));
    window.device = format->device;
    window.origin = origin;
    window.size = index_size;
    window.capacity = ns2_capacity();
    window.buffer = (uint8_t *)xx_mem_alloc(window.capacity);
    if (!window.buffer) return false;

    while (pos < index_size - 1) {
        const uint8_t *view;
        size_t avail = 0U, length;
        uint32_t name_length, size;
        if ((count & NS2_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (count >= NS2_MAX_MEMBERS || (items && count >= limit)) goto done;
        view = ns2_window_view(&window, pos, &avail);
        if (!view) goto done;
        /* The entry may not reach the terminator byte. */
        if ((int64_t)avail > index_size - 1 - pos) avail = (size_t)(index_size - 1 - pos);
        length = ns2_parse_entry(view, avail, &name_length, &size);
        if (length == 0U || (int64_t)size > data_size - running) goto done;
        if (items) {
            size_t converted = ns2_convert_name(view + 1U, name_length, name);
            items[count].name_offset = origin + pos + 1;
            items[count].data_offset = format->base_address + layout->data_base + running;
            items[count].size = (int64_t)size;
            items[count].name_length = name_length;
            items[count].renamed = false;
            keys[count].hash = ns2_name_hash(name, converted);
            keys[count].index = count;
        }
        running += (int64_t)size;
        pos += (int64_t)length;
        ++count;
    }
    /* The entries end with 'e' exactly at base - 1 and the members fill the
     * data area exactly to EOF: with no magic, this is the recognition. */
    if (pos == index_size - 1 && count != 0U && running == data_size) {
        size_t avail = 0U;
        const uint8_t *view = ns2_window_view(&window, pos, &avail);
        ok = view && avail >= 1U && view[0] == (uint8_t)'e';
    }
    if (ok && count_out) *count_out = count;
done:
    xx_mem_free(window.buffer);
    return ok;
}

static int ns2_compare_keys(const void *left, const void *right)
{
    const ns2_key *a = (const ns2_key *)left;
    const ns2_key *b = (const ns2_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static void ns2_mark_duplicates(ns2_member *items, ns2_key *keys, size_t count)
{
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), ns2_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash && keys[index].index < count) items[keys[index].index].renamed = true;
}

static void ns2_stream_free(void *opaque)
{
    ns2_stream *stream = (ns2_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool ns2_open_stream(Abstractformat *format, ns2_stream **result, xx_pd_struct *pd)
{
    ns2_layout layout;
    ns2_member *items = NULL;
    ns2_key *keys = NULL;
    char *name = NULL;
    ns2_stream *stream = NULL;
    uint32_t count = 0U, filled = 0U;
    if (!result || !ns2_read_header(format, &layout) || !ns2_walk(format, &layout, NULL, NULL, NULL, 0U, &count, pd) || count == 0U || count > NS2_MAX_MEMBERS)
        return false;
    /* At most 65535 entries: about 3 MiB of bookkeeping. */
    items = (ns2_member *)xx_mem_calloc(count, sizeof(*items));
    keys = (ns2_key *)xx_mem_alloc((size_t)count * sizeof(*keys));
    name = (char *)xx_mem_alloc(NS2_NAME_BUFFER);
    stream = (ns2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!items || !keys || !name || !stream || !ns2_walk(format, &layout, items, keys, name, count, &filled, pd) || filled != count) goto fail;
    ns2_mark_duplicates(items, keys, count);
    xx_mem_free(keys);
    stream->items = items;
    stream->count = count;
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

static bool ns2_load_name(Abstractformat *format, ns2_stream *stream, size_t index)
{
    const ns2_member *member = &stream->items[index];
    uint8_t raw[NS2_MAX_NAME];
    size_t length;
    if (member->name_length == 0U || member->name_length > NS2_MAX_NAME || !ns2_read_at(format->device, member->name_offset, raw, member->name_length)) return false;
    length = ns2_convert_name(raw, member->name_length, stream->name);
    if (member->renamed) ns2_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

static bool ns2_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *ns2_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ns2_set_record(Abstractformat *format, xx_archive_record *record, ns2_stream *stream, size_t index)
{
    const ns2_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!ns2_load_name(format, stream, index)) return false;
    record->header_offset = member->name_offset - 1;
    record->header_size = (int64_t)member->name_length + 2 + NS2_SIZE_FIELD;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, stream->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_ns2_init(xx_ns2 *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_NS2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "ns2");
    archive->format.check_is_valid = xx_ns2_check_is_valid;
    archive->format.handle_base_info = xx_ns2_handle_base_info;
    archive->format.get_format_size = xx_ns2_get_format_size;
    archive->format.get_number_of_archive_records = xx_ns2_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_ns2_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_ns2_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_ns2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_ns2_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_ns2_free_archive_records_reading;
    archive->data_base = -1;
}

xx_ns2 *xx_ns2_create(xx_io_device *device, int64_t base_address)
{
    xx_ns2 *archive = (xx_ns2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ns2_init(archive, device, base_address);
    return archive;
}

void xx_ns2_destroy(xx_ns2 *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ns2_free(xx_ns2 *archive)
{
    if (!archive) return;
    xx_ns2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ns2_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    ns2_layout layout;
    return ns2_read_header(format, &layout) && ns2_walk(format, &layout, NULL, NULL, NULL, 0U, NULL, pd);
}

bool xx_ns2_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    ns2_layout layout;
    xx_ns2 *archive;
    uint32_t count = 0U;
    if (!ns2_read_header(format, &layout) || !ns2_walk(format, &layout, NULL, NULL, NULL, 0U, &count, pd)) return false;
    archive = (xx_ns2 *)format;
    archive->number_of_records = count;
    archive->data_base = layout.data_base;
    format->number_of_archive_records = count;
    format->format_size = layout.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_ns2_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ns2_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_ns2_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ns2_handle_base_info(format, pd)) ? ((xx_ns2 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ns2_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    ns2_stream *stream;
    xx_archive_record_state *state;
    if (!ns2_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ns2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ns2_stream_free;
    state->total_records = stream->count;
    if (!ns2_copy_options(&state->options, options) || !ns2_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ns2_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_ns2_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ns2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (ns2_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ns2_set_record(format, &state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_ns2_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ns2_stream *stream;
    const ns2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (ns2_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->size < 0 || member->data_offset < 0) return false;
    path_option = ns2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ns2_copy_range(format->device, member->data_offset, member->size, NULL, pd);
    if (!ns2_safe_name(stream->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->name)
                                                                                                  : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = ns2_copy_range(format->device, member->data_offset, member->size, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ns2_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
