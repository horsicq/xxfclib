/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * NScripter NSA resource archive ("arc.nsa").  xx_nsa.h carries the field
 * table, the codecs and the member-name rules.  Written from the format's
 * structure.  The index walk, window and member-name handling follow this
 * library's sar_ns reader (src/formats/sar_ns/xx_sar_ns.c, MIT), the
 * acceptance rules follow XArchive's games/xnscripter.cpp (MIT), and
 * GARbro's ArcFormats/NScripter/ArcNSA.cs (MIT, morkt) was the reference for
 * the layout, the zero-prefixed variant and the codec semantics.  The LZSS
 * and SPB decoders below are written from the bit-level description in
 * xx_nsa.h; no decoder code is taken from any of those sources.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nsa/xx_nsa.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef NSA
#define XX_NSA_FILE_TYPE XX_FILE_TYPE_NSA
#else
#define XX_NSA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NSA_HEADER_SIZE 6
#define NSA_FIXED_SIZE 13
#define NSA_MAX_NAME 1024
#define NSA_MIN_ENTRY (1 + 1 + NSA_FIXED_SIZE)
#define NSA_MAX_ENTRY (NSA_MAX_NAME + 1 + NSA_FIXED_SIZE)
#define NSA_NAME_BUFFER (3 * NSA_MAX_NAME + 2 + 5 + 1)
#define NSA_POLL_MASK 0x3ffU

#define NSA_CODEC_STORED 0U
#define NSA_CODEC_SPB 1U
#define NSA_CODEC_LZSS 2U
#define NSA_CODEC_NBZ 4U

/* SPB: each side at most 16384 pixels and the BMP at most 512 MiB. */
#define NSA_SPB_MAX_SIDE 16384U
#define NSA_SPB_MAX_OUTPUT (512LL * 1024 * 1024)
/* NBZ: the declared output size is capped at 1 GiB. */
#define NSA_NBZ_MAX_OUTPUT (1024LL * 1024 * 1024)

typedef struct nsa_member_s {
    int64_t entry_offset; /**< Absolute offset of the entry (its name). */
    int64_t data_offset;  /**< Absolute offset of the data. */
    int64_t packed;
    int64_t unpacked; /**< The table's unpacked-size field. */
    uint32_t name_length;
    uint8_t codec; /**< Resolved: 0, 1, 2 or 4 (".nbz" names are 4). */
    bool renamed;
} nsa_member;

typedef struct nsa_key_s {
    uint64_t hash;
    uint32_t index;
} nsa_key;

typedef struct nsa_layout_s {
    int64_t header;    /**< Absolute offset of the count field. */
    int64_t format_size; /**< From base_address to EOF. */
    int64_t data_base; /**< The base field (relative to header). */
    int64_t data_size; /**< From the data area to EOF. */
    uint32_t count;
} nsa_layout;

typedef struct nsa_window_s {
    xx_io_device *device;
    int64_t origin;
    int64_t size;
    int64_t start;
    size_t length;
    uint8_t *buffer;
    size_t capacity;
    uint8_t entry[NSA_MAX_ENTRY];
} nsa_window;

typedef struct nsa_stream_s {
    nsa_member *items;
    size_t count;
    size_t index;
    char *name;
} nsa_stream;

/* ---- I/O --------------------------------------------------------------- */

static size_t nsa_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static uint16_t nsa_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t nsa_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool nsa_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool nsa_write_all(xx_io_device *destination, const uint8_t *data,
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

static bool nsa_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    const size_t capacity = nsa_capacity();
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
            !nsa_read_at(source, offset + (size - remaining), buffer, chunk) ||
            !nsa_write_all(destination, buffer, chunk)) {
            ok = false;
            break;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* MSB-first bit reader over [pos, end) of a device, buffered. */
typedef struct nsa_bits_s {
    xx_io_device *device;
    int64_t pos;
    int64_t end;
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    size_t at;
    uint32_t acc;
    unsigned count;
    bool eof;
} nsa_bits;

static bool nsa_bits_open(nsa_bits *bits, xx_io_device *device, int64_t offset,
                          int64_t size) {
    xx_mem_zero(bits, sizeof(*bits));
    bits->device = device;
    bits->pos = offset;
    bits->end = offset + size;
    bits->capacity = nsa_capacity();
    bits->buffer = (uint8_t *)xx_mem_alloc(bits->capacity);
    return bits->buffer != NULL;
}

static void nsa_bits_close(nsa_bits *bits) {
    if (bits->buffer) xx_mem_free(bits->buffer);
    bits->buffer = NULL;
}

/* Up to 16 bits; on running out of input sets eof and returns 0. */
static uint32_t nsa_bits_get(nsa_bits *bits, unsigned width) {
    uint32_t value;
    while (bits->count < width) {
        if (bits->at == bits->length) {
            int64_t left = bits->end - bits->pos;
            size_t chunk;
            if (bits->eof || left <= 0) {
                bits->eof = true;
                return 0U;
            }
            chunk = left > (int64_t)bits->capacity ? bits->capacity
                                                   : (size_t)left;
            if (!nsa_read_at(bits->device, bits->pos, bits->buffer, chunk)) {
                bits->eof = true;
                return 0U;
            }
            bits->pos += (int64_t)chunk;
            bits->length = chunk;
            bits->at = 0U;
        }
        bits->acc = (bits->acc << 8U) | bits->buffer[bits->at++];
        bits->count += 8U;
    }
    value = (bits->acc >> (bits->count - width)) & ((1U << width) - 1U);
    bits->count -= width;
    return value;
}

/* ---- member names (as in sar_ns) ---------------------------------------- */

static bool nsa_is_sjis_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool nsa_is_sjis_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t nsa_put_escape(char *out, uint8_t c) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

static size_t nsa_convert_name(const uint8_t *raw, size_t length, char *out) {
    size_t at = 0U;
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        if (nsa_is_sjis_lead(c) && index + 1U < length &&
            nsa_is_sjis_trail(raw[index + 1U])) {
            at += nsa_put_escape(out + at, c);
            at += nsa_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c == (uint8_t)'%')
            at += nsa_put_escape(out + at, c);
        else if (c == (uint8_t)'\\')
            out[at++] = '/';
        else
            out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

static uint64_t nsa_name_hash(const char *name, size_t length) {
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

static void nsa_insert_suffix(char *name, size_t length, uint32_t index) {
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
    if (length + suffix_length >= NSA_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool nsa_reserved_component(const char *segment, size_t length) {
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

static bool nsa_safe_name(const char *name) {
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
                nsa_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* A raw name ending in ".nbz" (ASCII case-insensitive). */
static bool nsa_is_nbz_name(const uint8_t *raw, size_t length) {
    static const char ext[] = ".nbz";
    size_t index;
    if (length < 4U) return false;
    for (index = 0U; index < 4U; ++index) {
        uint8_t c = raw[length - 4U + index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
            c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        if (c != (uint8_t)ext[index]) return false;
    }
    return true;
}

/* ---- index walk -------------------------------------------------------- */

static bool nsa_read_header(Abstractformat *format, nsa_layout *layout) {
    uint8_t header[2 + NSA_HEADER_SIZE];
    int64_t total, size, base, index_size, header_at;
    uint32_t count;
    size_t prefix = 0U;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)sizeof(header) + NSA_MIN_ENTRY ||
        !nsa_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    /* GARbro also opens archives whose header follows two zero bytes. */
    if (header[0] == 0U && header[1] == 0U) prefix = 2U;
    count = (uint32_t)nsa_be16(header + prefix);
    base = (int64_t)nsa_be32(header + prefix + 2U);
    header_at = format->base_address + (int64_t)prefix;
    size -= (int64_t)prefix;
    if (count == 0U || base > size) return false;
    index_size = base - NSA_HEADER_SIZE;
    if (index_size < (int64_t)count * NSA_MIN_ENTRY ||
        index_size > (int64_t)count * NSA_MAX_ENTRY)
        return false;
    layout->header = header_at;
    layout->format_size = total - format->base_address;
    layout->data_base = base;
    layout->data_size = size - base;
    layout->count = count;
    return true;
}

static const uint8_t *nsa_window_view(nsa_window *window, int64_t pos,
                                      size_t *avail) {
    int64_t want = window->size - pos;
    if (pos < 0 || want <= 0) return NULL;
    if (want > NSA_MAX_ENTRY) want = NSA_MAX_ENTRY;
    if ((uint64_t)want > window->capacity) {
        if (!nsa_read_at(window->device, window->origin + pos, window->entry,
                         (size_t)want))
            return NULL;
        *avail = (size_t)want;
        return window->entry;
    }
    if (pos < window->start ||
        pos + want > window->start + (int64_t)window->length) {
        int64_t chunk = window->size - pos;
        if ((uint64_t)chunk > window->capacity)
            chunk = (int64_t)window->capacity;
        window->length = 0U;
        if (!nsa_read_at(window->device, window->origin + pos, window->buffer,
                         (size_t)chunk))
            return NULL;
        window->start = pos;
        window->length = (size_t)chunk;
    }
    *avail = (size_t)(window->start + (int64_t)window->length - pos);
    return window->buffer + (pos - window->start);
}

typedef struct nsa_entry_s {
    uint32_t name_length;
    uint8_t codec;
    uint32_t offset;
    uint32_t packed;
    uint32_t unpacked;
} nsa_entry;

/* Parse one entry.  Returns its length, 0 if malformed: an empty or
 * over-long name, a control byte in the name, a codec outside 0/1/2/4, an
 * LZSS member claiming more output than its bits can encode, or a name or
 * fixed part running past the end of the index. */
static size_t nsa_parse_entry(const uint8_t *view, size_t avail,
                              nsa_entry *entry) {
    size_t at;
    for (at = 0U; at < avail && at <= NSA_MAX_NAME; ++at) {
        uint8_t c = view[at];
        if (c == 0U) break;
        if (c < 0x20U || c == 0x7fU) return 0U;
    }
    if (at == 0U || at > NSA_MAX_NAME || at >= avail ||
        avail - at - 1U < NSA_FIXED_SIZE)
        return 0U;
    entry->name_length = (uint32_t)at;
    entry->codec = view[at + 1U];
    entry->offset = nsa_be32(view + at + 2U);
    entry->packed = nsa_be32(view + at + 6U);
    entry->unpacked = nsa_be32(view + at + 10U);
    if (entry->codec != NSA_CODEC_STORED && entry->codec != NSA_CODEC_SPB &&
        entry->codec != NSA_CODEC_LZSS && entry->codec != NSA_CODEC_NBZ)
        return 0U;
    /* The cheapest LZSS code is 13 bits for 17 bytes. */
    if (entry->codec == NSA_CODEC_LZSS &&
        (uint64_t)entry->unpacked >
            ((uint64_t)entry->packed * 8U / 13U + 1U) * 17U)
        return 0U;
    if (nsa_is_nbz_name(view, at)) entry->codec = NSA_CODEC_NBZ;
    return at + 1U + NSA_FIXED_SIZE;
}

/* Members come in non-decreasing offset order and stay inside the data area
 * (as XArchive requires); a member may share data with an earlier one, as a
 * de-duplicating writer produces. */
static bool nsa_member_fits(const nsa_entry *entry, int64_t previous_offset,
                            int64_t data_size) {
    return (int64_t)entry->offset >= previous_offset &&
           (int64_t)entry->offset <= data_size &&
           (int64_t)entry->packed <= data_size - (int64_t)entry->offset;
}

static bool nsa_walk(Abstractformat *format, const nsa_layout *layout,
                     nsa_member *items, nsa_key *keys, char *name,
                     xx_pd_struct *pd) {
    nsa_window window;
    int64_t index_size = layout->data_base - NSA_HEADER_SIZE;
    int64_t pos = 0, previous_offset = 0, max_end = 0;
    uint32_t index;
    bool ok = false;

    /* Cheap gate before anything is allocated: entry 0 from one small read. */
    {
        uint8_t first[NSA_MAX_ENTRY];
        size_t avail = index_size < (int64_t)NSA_MAX_ENTRY
                           ? (size_t)index_size
                           : (size_t)NSA_MAX_ENTRY;
        nsa_entry entry;
        if (!nsa_read_at(format->device, layout->header + NSA_HEADER_SIZE,
                         first, avail) ||
            nsa_parse_entry(first, avail, &entry) == 0U ||
            !nsa_member_fits(&entry, 0, layout->data_size))
            return false;
    }

    xx_mem_zero(&window, sizeof(window));
    window.device = format->device;
    window.origin = layout->header + NSA_HEADER_SIZE;
    window.size = index_size;
    window.capacity = nsa_capacity();
    window.buffer = (uint8_t *)xx_mem_alloc(window.capacity);
    if (!window.buffer) return false;

    for (index = 0U; index < layout->count; ++index) {
        const uint8_t *view;
        size_t avail = 0U, length;
        nsa_entry entry;
        if ((index & NSA_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            goto done;
        view = nsa_window_view(&window, pos, &avail);
        if (!view) goto done;
        length = nsa_parse_entry(view, avail, &entry);
        if (length == 0U ||
            !nsa_member_fits(&entry, previous_offset, layout->data_size))
            goto done;
        if (items) {
            size_t converted = nsa_convert_name(view, entry.name_length, name);
            items[index].entry_offset = window.origin + pos;
            items[index].data_offset =
                layout->header + layout->data_base + (int64_t)entry.offset;
            items[index].packed = (int64_t)entry.packed;
            items[index].unpacked = entry.codec == NSA_CODEC_STORED
                                        ? (int64_t)entry.packed
                                        : (int64_t)entry.unpacked;
            items[index].name_length = entry.name_length;
            items[index].codec = entry.codec;
            items[index].renamed = false;
            keys[index].hash = nsa_name_hash(name, converted);
            keys[index].index = index;
        }
        previous_offset = (int64_t)entry.offset;
        if ((int64_t)entry.offset + (int64_t)entry.packed > max_end)
            max_end = (int64_t)entry.offset + (int64_t)entry.packed;
        pos += (int64_t)length;
    }
    /* The entries must end exactly at base and the data area must end
     * exactly at EOF: with no magic, this extent proof is the recognition. */
    ok = pos == index_size && max_end == layout->data_size;
done:
    xx_mem_free(window.buffer);
    return ok;
}

static int nsa_compare_keys(const void *left, const void *right) {
    const nsa_key *a = (const nsa_key *)left;
    const nsa_key *b = (const nsa_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static void nsa_mark_duplicates(nsa_member *items, nsa_key *keys,
                                size_t count) {
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), nsa_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            keys[index].index < count)
            items[keys[index].index].renamed = true;
}

static void nsa_stream_free(void *opaque) {
    nsa_stream *stream = (nsa_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool nsa_open_stream(Abstractformat *format, nsa_stream **result,
                            xx_pd_struct *pd) {
    nsa_layout layout;
    nsa_member *items = NULL;
    nsa_key *keys = NULL;
    char *name = NULL;
    nsa_stream *stream = NULL;
    if (!result || !nsa_read_header(format, &layout)) return false;
    /* At most 65535 entries: about 3 MiB of bookkeeping, never more. */
    items = (nsa_member *)xx_mem_calloc(layout.count, sizeof(*items));
    keys = (nsa_key *)xx_mem_alloc((size_t)layout.count * sizeof(*keys));
    name = (char *)xx_mem_alloc(NSA_NAME_BUFFER);
    stream = (nsa_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!items || !keys || !name || !stream ||
        !nsa_walk(format, &layout, items, keys, name, pd))
        goto fail;
    nsa_mark_duplicates(items, keys, layout.count);
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

static bool nsa_load_name(Abstractformat *format, nsa_stream *stream,
                          size_t index) {
    const nsa_member *member = &stream->items[index];
    uint8_t raw[NSA_MAX_NAME];
    size_t length;
    if (member->name_length == 0U || member->name_length > NSA_MAX_NAME ||
        !nsa_read_at(format->device, member->entry_offset, raw,
                     member->name_length))
        return false;
    length = nsa_convert_name(raw, member->name_length, stream->name);
    if (member->renamed)
        nsa_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

/* ---- codecs ------------------------------------------------------------ */

/* SPB output size from the 4-byte width/height prefix, or -1. */
static int64_t nsa_spb_size(uint32_t width, uint32_t height) {
    int64_t stride, total;
    if (width == 0U || height == 0U || width > NSA_SPB_MAX_SIDE ||
        height > NSA_SPB_MAX_SIDE)
        return -1;
    stride = ((int64_t)width * 3 + 3) & ~(int64_t)3;
    total = 54 + stride * (int64_t)height;
    return total > NSA_SPB_MAX_OUTPUT ? -1 : total;
}

/* The size a member unpacks to, as far as it can be told without decoding;
 * -1 when the member cannot be unpacked. */
static int64_t nsa_output_size(xx_io_device *device, const nsa_member *member) {
    uint8_t prefix[4];
    if (member->codec == NSA_CODEC_STORED) return member->packed;
    if (member->codec == NSA_CODEC_LZSS) return member->unpacked;
    if (member->packed < 4 ||
        !nsa_read_at(device, member->data_offset, prefix, sizeof(prefix)))
        return -1;
    if (member->codec == NSA_CODEC_NBZ) return (int64_t)nsa_be32(prefix);
    return nsa_spb_size(nsa_be16(prefix), nsa_be16(prefix + 2U));
}

static bool nsa_unpack_lzss(xx_io_device *source, const nsa_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t ring[256];
    unsigned cursor = 239U;
    int64_t produced = 0;
    const int64_t size = member->unpacked;
    nsa_bits bits;
    uint8_t *out;
    size_t out_length = 0U, out_capacity;
    bool ok = true;
    if (size < 0) return false;
    if (size == 0) return true;
    if (!nsa_bits_open(&bits, source, member->data_offset, member->packed))
        return false;
    out_capacity = bits.capacity;
    out = (uint8_t *)xx_mem_alloc(out_capacity);
    if (!out) {
        nsa_bits_close(&bits);
        return false;
    }
    xx_rt_memset(ring, 0, sizeof(ring));
    while (ok && produced < size) {
        uint32_t flag = nsa_bits_get(&bits, 1U);
        uint32_t source_pos, length, k;
        if (bits.eof) {
            ok = false;
            break;
        }
        if (flag) {
            source_pos = nsa_bits_get(&bits, 8U);
            if (bits.eof) {
                ok = false;
                break;
            }
            /* A literal is a one-byte copy of itself. */
            ring[cursor] = (uint8_t)source_pos;
            out[out_length++] = (uint8_t)source_pos;
            cursor = (cursor + 1U) & 255U;
            ++produced;
        } else {
            source_pos = nsa_bits_get(&bits, 8U);
            length = nsa_bits_get(&bits, 4U) + 2U;
            if (bits.eof) {
                ok = false;
                break;
            }
            for (k = 0U; k < length && produced < size; ++k) {
                uint8_t c = ring[(source_pos + k) & 255U];
                ring[cursor] = c;
                cursor = (cursor + 1U) & 255U;
                out[out_length++] = c;
                ++produced;
                if (out_length == out_capacity) {
                    if (!nsa_write_all(destination, out, out_length)) {
                        ok = false;
                        break;
                    }
                    out_length = 0U;
                }
            }
        }
        if (ok && out_length == out_capacity) {
            if (!nsa_write_all(destination, out, out_length)) ok = false;
            out_length = 0U;
            if (pd && xx_pd_is_stopped(pd)) ok = false;
        }
    }
    if (ok && out_length) ok = nsa_write_all(destination, out, out_length);
    xx_mem_free(out);
    nsa_bits_close(&bits);
    return ok;
}

static void nsa_put_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static bool nsa_unpack_spb(xx_io_device *source, const nsa_member *member,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t prefix[4];
    uint32_t width, height, plane_index;
    uint64_t pixels, groups, min_bits;
    int64_t total, stride;
    uint8_t *image = NULL, *plane = NULL;
    nsa_bits bits;
    bool ok = false;
    if (member->packed < 4 ||
        !nsa_read_at(source, member->data_offset, prefix, sizeof(prefix)))
        return false;
    width = nsa_be16(prefix);
    height = nsa_be16(prefix + 2U);
    total = nsa_spb_size(width, height);
    if (total < 0) return false;
    pixels = (uint64_t)width * height;
    /* Every plane costs at least 8 bits plus 3 per further group of four,
     * so a tiny member cannot demand a huge image. */
    groups = (pixels - 1U + 3U) / 4U;
    min_bits = 32U + 3U * (8U + 3U * groups);
    if ((uint64_t)member->packed * 8U < min_bits) return false;
    stride = ((int64_t)width * 3 + 3) & ~(int64_t)3;
    image = (uint8_t *)xx_mem_calloc(1U, (size_t)total);
    plane = (uint8_t *)xx_mem_alloc((size_t)pixels + 4U);
    if (!image || !plane ||
        !nsa_bits_open(&bits, source, member->data_offset + 4,
                       member->packed - 4)) {
        if (image) xx_mem_free(image);
        if (plane) xx_mem_free(plane);
        return false;
    }
    image[0] = (uint8_t)'B';
    image[1] = (uint8_t)'M';
    nsa_put_le32(image + 2, (uint32_t)total);
    image[10] = 54U;
    image[14] = 40U;
    nsa_put_le32(image + 18, width);
    nsa_put_le32(image + 22, height);
    image[26] = 1U;
    image[28] = 24U;
    for (plane_index = 0U; plane_index < 3U; ++plane_index) {
        uint64_t count = 0U, source_at = 0U;
        uint32_t y;
        uint8_t current = (uint8_t)nsa_bits_get(&bits, 8U);
        if (bits.eof) goto done;
        plane[count++] = current;
        while (count < pixels) {
            uint32_t code = nsa_bits_get(&bits, 3U), width_bits, j;
            if (bits.eof) goto done;
            if ((count & 0xffffU) == 0U && pd && xx_pd_is_stopped(pd))
                goto done;
            if (code == 0U) {
                plane[count] = plane[count + 1U] = plane[count + 2U] =
                    plane[count + 3U] = current;
                count += 4U;
                continue;
            }
            width_bits = code == 7U ? nsa_bits_get(&bits, 1U) + 1U : code + 2U;
            for (j = 0U; j < 4U; ++j) {
                if (width_bits == 8U) {
                    current = (uint8_t)nsa_bits_get(&bits, 8U);
                } else {
                    uint32_t delta = nsa_bits_get(&bits, width_bits);
                    if (delta & 1U)
                        current = (uint8_t)(current + (delta >> 1U) + 1U);
                    else
                        current = (uint8_t)(current - (delta >> 1U));
                }
                plane[count + j] = current;
            }
            if (bits.eof) goto done;
            count += 4U;
        }
        /* Row y of the picture (top first) is BMP row height-1-y; odd rows
         * were scanned right to left. */
        for (y = 0U; y < height; ++y) {
            uint8_t *row = image + 54 + stride * (int64_t)(height - 1U - y);
            uint32_t x;
            if (y & 1U) {
                for (x = width; x > 0U; --x)
                    row[(size_t)(x - 1U) * 3U + plane_index] =
                        plane[source_at++];
            } else {
                for (x = 0U; x < width; ++x)
                    row[(size_t)x * 3U + plane_index] = plane[source_at++];
            }
        }
    }
    ok = nsa_write_all(destination, image, (size_t)total);
done:
    nsa_bits_close(&bits);
    xx_mem_free(image);
    xx_mem_free(plane);
    return ok;
}

/* A write-only device that forwards to `target` (or discards) and fails as
 * soon as more than `limit` bytes arrive: the NBZ output-size cap. */
typedef struct nsa_limit_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} nsa_limit;

static ssize_t nsa_limit_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    nsa_limit *limit = (nsa_limit *)self;
    if (size > (SIZE_MAX >> 1) || (uint64_t)size > limit->limit - limit->written)
        return -1;
    if (!nsa_write_all(limit->target, (const uint8_t *)buffer, size))
        return -1;
    limit->written += (uint64_t)size;
    return (ssize_t)size;
}

static bool nsa_unpack_nbz(xx_io_device *source, const nsa_member *member,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t prefix[4];
    nsa_limit limit;
    int64_t expected;
    if (member->packed < 4 ||
        !nsa_read_at(source, member->data_offset, prefix, sizeof(prefix)))
        return false;
    expected = (int64_t)nsa_be32(prefix);
    if (expected > NSA_NBZ_MAX_OUTPUT) return false;
    xx_mem_zero(&limit, sizeof(limit));
    limit.device.write = nsa_limit_write;
    limit.target = destination;
    limit.limit = (uint64_t)expected;
    if (member->packed == 4) return expected == 0;
    return xx_bzip2_unpack_device(source, member->data_offset + 4,
                                  member->packed - 4, &limit.device, pd) &&
           limit.written == (uint64_t)expected;
}

static bool nsa_unpack_member(xx_io_device *source, const nsa_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    switch (member->codec) {
    case NSA_CODEC_STORED:
        return nsa_copy_range(source, member->data_offset, member->packed,
                              destination, pd);
    case NSA_CODEC_SPB:
        return nsa_unpack_spb(source, member, destination, pd);
    case NSA_CODEC_LZSS:
        return nsa_unpack_lzss(source, member, destination, pd);
    case NSA_CODEC_NBZ:
        return nsa_unpack_nbz(source, member, destination, pd);
    default:
        return false;
    }
}

/* ---- records ----------------------------------------------------------- */

static bool nsa_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *nsa_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool nsa_set_record(Abstractformat *format, xx_archive_record *record,
                           nsa_stream *stream, size_t index) {
    const nsa_member *member = &stream->items[index];
    int64_t output = nsa_output_size(format->device, member);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!nsa_load_name(format, stream, index)) return false;
    record->header_offset = member->entry_offset;
    record->header_size = (int64_t)member->name_length + 1 + NSA_FIXED_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)(output < 0 ? member->unpacked : output)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          (uint64_t)member->codec) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_nsa_init(xx_nsa *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_NSA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "nsa");
    archive->format.check_is_valid = xx_nsa_check_is_valid;
    archive->format.handle_base_info = xx_nsa_handle_base_info;
    archive->format.get_format_size = xx_nsa_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_nsa_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_nsa_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_nsa_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_nsa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_nsa_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_nsa_free_archive_records_reading;
    archive->data_base = -1;
}

xx_nsa *xx_nsa_create(xx_io_device *device, int64_t base_address) {
    xx_nsa *archive = (xx_nsa *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_nsa_init(archive, device, base_address);
    return archive;
}

void xx_nsa_destroy(xx_nsa *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_nsa_free(xx_nsa *archive) {
    if (!archive) return;
    xx_nsa_destroy(archive);
    xx_mem_free(archive);
}

bool xx_nsa_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    nsa_layout layout;
    return nsa_read_header(format, &layout) &&
           nsa_walk(format, &layout, NULL, NULL, NULL, pd);
}

bool xx_nsa_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    nsa_layout layout;
    xx_nsa *archive;
    if (!nsa_read_header(format, &layout) ||
        !nsa_walk(format, &layout, NULL, NULL, NULL, pd))
        return false;
    archive = (xx_nsa *)format;
    archive->number_of_records = layout.count;
    archive->data_base = layout.header + layout.data_base;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_nsa_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_nsa_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_nsa_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_nsa_handle_base_info(format, pd))
               ? ((xx_nsa *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_nsa_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    nsa_stream *stream;
    xx_archive_record_state *state;
    if (!nsa_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        nsa_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = nsa_stream_free;
    state->total_records = stream->count;
    if (!nsa_copy_options(&state->options, options) ||
        !nsa_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_nsa_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_nsa_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    nsa_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (nsa_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = nsa_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_nsa_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    nsa_stream *stream;
    const nsa_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (nsa_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->packed < 0 || member->data_offset < 0) return false;
    path_option = nsa_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode into nothing, which verifies the member. */
        return nsa_unpack_member(format->device, member, NULL, pd);
    if (!nsa_safe_name(stream->name)) return false;
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
        result = nsa_unpack_member(format->device, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_nsa_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
