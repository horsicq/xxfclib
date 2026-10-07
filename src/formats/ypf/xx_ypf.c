/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * YU-RIS YPF resource archive.  xx_ypf.h carries the field table.
 *
 * The layout, the three name-length swap tables, the version rules for the
 * table and the extra entry bytes, and the key guess are ported from GARbro's
 * ArcFormats/YuRis/ArcYPF.cs (MIT, Copyright (C) 2014-2018 by morkt).  The
 * index window and the member-name handling (cp932 escaping, duplicate
 * renaming, unsafe-name refusal) follow this library's nsa reader
 * (src/formats/nsa/xx_nsa.c, MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ypf/xx_ypf.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef YPF
#define XX_YPF_FILE_TYPE XX_FILE_TYPE_YPF
#else
#define XX_YPF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define YPF_HEADER_SIZE 0x20
#define YPF_FIXED_SIZE 0x12 /* type, flag, unpacked, packed, offset, sum */
#define YPF_MAX_NAME 255U
#define YPF_MAX_EXTRA 8U
#define YPF_MAX_ENTRY (5U + YPF_MAX_NAME + YPF_FIXED_SIZE + YPF_MAX_EXTRA)
#define YPF_MIN_ENTRY 0x17
/* GARbro's IsSaneCount bound. */
#define YPF_MAX_COUNT 0x40000U
#define YPF_NAME_BUFFER (3U * YPF_MAX_NAME + 2U + 12U + 1U)
#define YPF_POLL_MASK 0x3ffU
/* zlib cannot expand by more than about 1032:1. */
#define YPF_MAX_RATIO 1100U

typedef struct ypf_member_s {
    int64_t name_offset; /**< Absolute offset of the obfuscated name. */
    int64_t data_offset; /**< Absolute offset of the stored bytes. */
    int64_t packed;
    int64_t unpacked;
    uint8_t name_length;
    bool zlib;
    bool renamed;
} ypf_member;

typedef struct ypf_key_s {
    uint64_t hash;
    uint32_t index;
} ypf_key;

typedef struct ypf_layout_s {
    int64_t origin; /**< Absolute offset of "YPF\0". */
    int64_t size;   /**< Bytes from origin to EOF. */
    int64_t index_limit; /**< Bytes the index may occupy. */
    uint32_t version;
    uint32_t count;
    uint32_t extra;
    const uint8_t *table; /**< Resolved swap table (may be empty). */
    size_t table_size;
    uint8_t key;
    int64_t format_size;
} ypf_layout;

typedef struct ypf_window_s {
    xx_io_device *device;
    int64_t origin;
    int64_t size;
    int64_t start;
    size_t length;
    uint8_t *buffer;
    size_t capacity;
} ypf_window;

typedef struct ypf_stream_s {
    ypf_member *items;
    size_t count;
    size_t index;
    uint8_t key;
    char *name;
} ypf_stream;

/* ---- swap tables (GARbro ArcYPF.cs, MIT) -------------------------------- */

static const uint8_t ypf_table00[] = {
    0x03, 0x48, 0x06, 0x35, 0x0C, 0x10, 0x11, 0x19, 0x1C, 0x1E,
    0x09, 0x0B, 0x0D, 0x13, 0x15, 0x1B, 0x20, 0x23, 0x26, 0x29,
    0x2C, 0x2F, 0x2E, 0x32,
};
static const uint8_t ypf_table04[] = {
    0x0C, 0x10, 0x11, 0x19, 0x1C, 0x1E, 0x09, 0x0B, 0x0D, 0x13,
    0x15, 0x1B, 0x20, 0x23, 0x26, 0x29, 0x2C, 0x2F, 0x2E, 0x32,
};
static const uint8_t ypf_table10[] = {
    0x09, 0x0B, 0x0D, 0x13, 0x15, 0x1B, 0x20,
    0x23, 0x26, 0x29, 0x2C, 0x2F, 0x2E, 0x32,
};

typedef struct ypf_table_s {
    const uint8_t *bytes;
    size_t size;
} ypf_table;

static const ypf_table ypf_tables[4] = {
    {ypf_table00, sizeof(ypf_table00)},
    {ypf_table04, sizeof(ypf_table04)},
    {ypf_table10, sizeof(ypf_table10)},
    {NULL, 0U},
};

/* GARbro's GuessSwapTable, without its per-game scheme lookup. */
static size_t ypf_guess_table(uint32_t version) {
    if (version < 0x100U) return 1U;
    if (version >= 0x12cU && version < 0x196U) return 2U;
    return 0U;
}

static uint32_t ypf_extra_size(uint32_t version) {
    if (version >= 0x1d9U) return 4U;
    if (version == 0xdeU) return 8U;
    return 0U;
}

static uint8_t ypf_decode_length(const uint8_t *table, size_t size,
                                 uint8_t value) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (table[index] == value)
            return (index & 1U) ? table[index - 1U] : table[index + 1U];
    return value;
}

/* ---- I/O --------------------------------------------------------------- */

static size_t ypf_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static bool ypf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool ypf_write_all(xx_io_device *destination, const uint8_t *data,
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

static bool ypf_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    const size_t capacity = ypf_capacity();
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (remaining > 0) {
        size_t chunk = remaining > (int64_t)capacity ? capacity
                                                     : (size_t)remaining;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !ypf_read_at(source, offset + (size - remaining), buffer, chunk) ||
            !ypf_write_all(destination, buffer, chunk)) {
            ok = false;
            break;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* A view of at least min(YPF_MAX_ENTRY, bytes left) bytes at pos. */
static const uint8_t *ypf_window_view(ypf_window *window, int64_t pos,
                                      size_t *avail) {
    int64_t want = window->size - pos;
    if (pos < 0 || want <= 0) return NULL;
    if (want > (int64_t)YPF_MAX_ENTRY) want = YPF_MAX_ENTRY;
    if (pos < window->start ||
        pos + want > window->start + (int64_t)window->length) {
        int64_t chunk = window->size - pos;
        if ((uint64_t)chunk > window->capacity)
            chunk = (int64_t)window->capacity;
        window->length = 0U;
        if (!ypf_read_at(window->device, window->origin + pos, window->buffer,
                         (size_t)chunk))
            return NULL;
        window->start = pos;
        window->length = (size_t)chunk;
    }
    *avail = (size_t)(window->start + (int64_t)window->length - pos);
    return window->buffer + (pos - window->start);
}

/* ---- member names (as in the nsa reader) -------------------------------- */

static bool ypf_is_sjis_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool ypf_is_sjis_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t ypf_put_escape(char *out, uint8_t c) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* raw is already de-XORed. */
static size_t ypf_convert_name(const uint8_t *raw, size_t length, char *out) {
    size_t at = 0U;
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        if (ypf_is_sjis_lead(c) && index + 1U < length &&
            ypf_is_sjis_trail(raw[index + 1U])) {
            at += ypf_put_escape(out + at, c);
            at += ypf_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c == (uint8_t)'%')
            at += ypf_put_escape(out + at, c);
        else if (c == (uint8_t)'\\')
            out[at++] = '/';
        else
            out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

static uint64_t ypf_name_hash(const char *name, size_t length) {
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

static void ypf_insert_suffix(char *name, size_t length, uint32_t index) {
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
    if (length + suffix_length >= YPF_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool ypf_reserved_component(const char *segment, size_t length) {
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

static bool ypf_safe_name(const char *name) {
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
                ypf_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- index walk -------------------------------------------------------- */

static bool ypf_read_header(Abstractformat *format, ypf_layout *layout) {
    uint8_t header[16];
    int64_t total, size, dir_size;
    uint32_t count;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    xx_mem_zero(layout, sizeof(*layout));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < YPF_HEADER_SIZE + YPF_MIN_ENTRY ||
        !ypf_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        header[0] != (uint8_t)'Y' || header[1] != (uint8_t)'P' ||
        header[2] != (uint8_t)'F' || header[3] != 0U)
        return false;
    count = xx_data_get_u32(header + 8, 4, 0, false);
    dir_size = (int64_t)xx_data_get_u32(header + 12, 4, 0, false);
    /* GARbro: a sane count and room for count minimal entries.  Writers
     * store 0x20 + index length here, so GARbro's "dir_size bytes after the
     * header" test rejects small archives whose data is shorter than 0x20
     * bytes; this reader only needs the field inside the file and walks at
     * most to EOF. */
    if (count == 0U || count >= YPF_MAX_COUNT ||
        dir_size < (int64_t)count * YPF_MIN_ENTRY || dir_size > size)
        return false;
    layout->origin = format->base_address;
    layout->size = size;
    layout->index_limit = dir_size < size - YPF_HEADER_SIZE
                              ? dir_size : size - YPF_HEADER_SIZE;
    layout->version = xx_data_get_u32(header + 4, 4, 0, false);
    layout->count = count;
    layout->extra = ypf_extra_size(layout->version);
    return true;
}

/* Walk the index with the table in layout.  With items == NULL the pass only
 * validates.  On success sets layout->key and layout->format_size. */
static bool ypf_walk(Abstractformat *format, ypf_layout *layout,
                     ypf_member *items, ypf_key *keys, char *name,
                     xx_pd_struct *pd) {
    ypf_window window;
    const int64_t fixed = 5 + YPF_FIXED_SIZE + (int64_t)layout->extra;
    int64_t pos = 0, remaining = layout->index_limit, max_end = 0;
    uint8_t key = 0U;
    uint32_t index;
    bool have_key = false, ok = false;
    uint8_t plain[YPF_MAX_NAME];

    xx_mem_zero(&window, sizeof(window));
    window.device = format->device;
    window.origin = layout->origin + YPF_HEADER_SIZE;
    window.size = layout->index_limit;
    /* A small first window keeps the cheap rejection of garbage cheap. */
    window.capacity = items ? ypf_capacity() : 4096U;
    window.buffer = (uint8_t *)xx_mem_alloc(window.capacity);
    if (!window.buffer) return false;

    for (index = 0U; index < layout->count; ++index) {
        const uint8_t *view;
        size_t avail = 0U, k;
        uint32_t name_length, unpacked, packed, offset;
        const uint8_t *tail;
        if ((index & YPF_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            goto done;
        if (remaining < fixed) goto done;
        remaining -= fixed;
        view = ypf_window_view(&window, pos, &avail);
        if (!view || avail < 5U) goto done;
        name_length = ypf_decode_length(layout->table, layout->table_size,
                                        (uint8_t)(view[4] ^ 0xffU));
        if (name_length == 0U || (int64_t)name_length > remaining ||
            avail < (size_t)fixed + name_length)
            goto done;
        remaining -= (int64_t)name_length;
        if (!have_key) {
            /* GARbro: the first name is assumed to end in ".xxx". */
            if (name_length < 4U) goto done;
            key = (uint8_t)(view[5U + name_length - 4U] ^ (uint8_t)'.');
            have_key = true;
        }
        for (k = 0U; k < name_length; ++k) {
            uint8_t c = (uint8_t)(view[5U + k] ^ key);
            if (c < 0x20U || c == 0x7fU) goto done;
            plain[k] = c;
        }
        tail = view + 5U + name_length;
        unpacked = xx_data_get_u32(tail + 2, 4, 0, false);
        packed = xx_data_get_u32(tail + 6, 4, 0, false);
        offset = xx_data_get_u32(tail + 10, 4, 0, false);
        if ((int64_t)offset > layout->size ||
            (int64_t)packed > layout->size - (int64_t)offset)
            goto done;
        if (tail[1] != 0U &&
            (packed < 2U ||
             (uint64_t)unpacked > (uint64_t)packed * YPF_MAX_RATIO + 64U))
            goto done;
        if (items) {
            size_t converted = ypf_convert_name(plain, name_length, name);
            items[index].name_offset = window.origin + pos + 5;
            items[index].data_offset = layout->origin + (int64_t)offset;
            items[index].packed = (int64_t)packed;
            items[index].zlib = tail[1] != 0U;
            items[index].unpacked =
                items[index].zlib ? (int64_t)unpacked : (int64_t)packed;
            items[index].name_length = (uint8_t)name_length;
            items[index].renamed = false;
            keys[index].hash = ypf_name_hash(name, converted);
            keys[index].index = index;
        }
        if ((int64_t)offset + (int64_t)packed > max_end)
            max_end = (int64_t)offset + (int64_t)packed;
        pos += fixed + (int64_t)name_length;
    }
    layout->key = key;
    layout->format_size =
        max_end > YPF_HEADER_SIZE + pos ? max_end : YPF_HEADER_SIZE + pos;
    ok = true;
done:
    xx_mem_free(window.buffer);
    return ok;
}

/* Choose the swap table: the version's guess first, then the others. */
static bool ypf_resolve(Abstractformat *format, ypf_layout *layout,
                        xx_pd_struct *pd) {
    size_t first, index;
    if (!ypf_read_header(format, layout)) return false;
    first = ypf_guess_table(layout->version);
    for (index = 0U; index < 4U; ++index) {
        size_t pick = index == 0U ? first : (index <= first ? index - 1U : index);
        layout->table = ypf_tables[pick].bytes;
        layout->table_size = ypf_tables[pick].size;
        if (ypf_walk(format, layout, NULL, NULL, NULL, pd)) return true;
        if (pd && xx_pd_is_stopped(pd)) return false;
    }
    return false;
}

static int ypf_compare_keys(const void *left, const void *right) {
    const ypf_key *a = (const ypf_key *)left;
    const ypf_key *b = (const ypf_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static void ypf_mark_duplicates(ypf_member *items, ypf_key *keys,
                                size_t count) {
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), ypf_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            keys[index].index < count)
            items[keys[index].index].renamed = true;
}

static void ypf_stream_free(void *opaque) {
    ypf_stream *stream = (ypf_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool ypf_open_stream(Abstractformat *format, ypf_stream **result,
                            xx_pd_struct *pd) {
    ypf_layout layout;
    ypf_member *items = NULL;
    ypf_key *keys = NULL;
    char *name = NULL;
    ypf_stream *stream = NULL;
    if (!result || !ypf_resolve(format, &layout, pd)) return false;
    /* count < 0x40000: at most about 12 MiB of bookkeeping. */
    items = (ypf_member *)xx_mem_calloc(layout.count, sizeof(*items));
    keys = (ypf_key *)xx_mem_alloc((size_t)layout.count * sizeof(*keys));
    name = (char *)xx_mem_alloc(YPF_NAME_BUFFER);
    stream = (ypf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!items || !keys || !name || !stream ||
        !ypf_walk(format, &layout, items, keys, name, pd))
        goto fail;
    ypf_mark_duplicates(items, keys, layout.count);
    xx_mem_free(keys);
    stream->items = items;
    stream->count = layout.count;
    stream->key = layout.key;
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

static bool ypf_load_name(Abstractformat *format, ypf_stream *stream,
                          size_t index) {
    const ypf_member *member = &stream->items[index];
    uint8_t raw[YPF_MAX_NAME];
    size_t length, k;
    if (member->name_length == 0U ||
        !ypf_read_at(format->device, member->name_offset, raw,
                     member->name_length))
        return false;
    for (k = 0U; k < member->name_length; ++k) raw[k] ^= stream->key;
    length = ypf_convert_name(raw, member->name_length, stream->name);
    if (member->renamed)
        ypf_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

/* ---- zlib members ------------------------------------------------------- */

/* A write-only device that forwards to `target` (or discards) and fails as
 * soon as more than `limit` bytes arrive. */
typedef struct ypf_limit_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} ypf_limit;

static ssize_t ypf_limit_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    ypf_limit *limit = (ypf_limit *)self;
    if (size > (SIZE_MAX >> 1) || (uint64_t)size > limit->limit - limit->written)
        return -1;
    if (!ypf_write_all(limit->target, (const uint8_t *)buffer, size))
        return -1;
    limit->written += (uint64_t)size;
    return (ssize_t)size;
}

static bool ypf_unpack_zlib(xx_io_device *source, const ypf_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t header[2];
    ypf_limit limit;
    if (member->packed < 2 ||
        !ypf_read_at(source, member->data_offset, header, sizeof(header)) ||
        !xx_zlib_stream_header_is_valid(header, sizeof(header)))
        return false;
    xx_mem_zero(&limit, sizeof(limit));
    limit.device.write = ypf_limit_write;
    limit.target = destination;
    limit.limit = (uint64_t)member->unpacked;
    return xx_deflate_unpack_device(source, member->data_offset + 2,
                                    member->packed - 2, &limit.device, false,
                                    pd) &&
           limit.written == (uint64_t)member->unpacked;
}

static bool ypf_unpack_member(xx_io_device *source, const ypf_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    if (member->zlib) return ypf_unpack_zlib(source, member, destination, pd);
    return ypf_copy_range(source, member->data_offset, member->packed,
                          destination, pd);
}

/* ---- records ----------------------------------------------------------- */

static bool ypf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ypf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ypf_set_record(Abstractformat *format, xx_archive_record *record,
                           ypf_stream *stream, size_t index) {
    const ypf_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!ypf_load_name(format, stream, index)) return false;
    record->header_offset = member->name_offset - 5;
    record->header_size = 5 + (int64_t)member->name_length + YPF_FIXED_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->zlib ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_ypf_init(xx_ypf *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_YPF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "ypf");
    archive->format.check_is_valid = xx_ypf_check_is_valid;
    archive->format.handle_base_info = xx_ypf_handle_base_info;
    archive->format.get_format_size = xx_ypf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ypf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ypf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ypf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ypf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ypf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ypf_free_archive_records_reading;
}

xx_ypf *xx_ypf_create(xx_io_device *device, int64_t base_address) {
    xx_ypf *archive = (xx_ypf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ypf_init(archive, device, base_address);
    return archive;
}

void xx_ypf_destroy(xx_ypf *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ypf_free(xx_ypf *archive) {
    if (!archive) return;
    xx_ypf_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ypf_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ypf_layout layout;
    return ypf_resolve(format, &layout, pd);
}

bool xx_ypf_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ypf_layout layout;
    xx_ypf *archive;
    if (!ypf_resolve(format, &layout, pd)) return false;
    archive = (xx_ypf *)format;
    archive->number_of_records = layout.count;
    archive->version = layout.version;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_ypf_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ypf_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ypf_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ypf_handle_base_info(format, pd))
               ? ((xx_ypf *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ypf_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ypf_stream *stream;
    xx_archive_record_state *state;
    if (!ypf_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ypf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ypf_stream_free;
    state->total_records = stream->count;
    if (!ypf_copy_options(&state->options, options) ||
        !ypf_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ypf_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ypf_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    ypf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ypf_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ypf_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_ypf_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    ypf_stream *stream;
    const ypf_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ypf_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->packed < 0 || member->data_offset < 0) return false;
    path_option = ypf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode into nothing, which verifies the member. */
        return ypf_unpack_member(format->device, member, NULL, pd);
    if (!ypf_safe_name(stream->name)) return false;
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
        result = ypf_unpack_member(format->device, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ypf_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
