/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LiveMaker "vf" resource archive.  xx_livemaker.h carries the field table.
 *
 * The index layout, the TpRandom name/offset keystream, the entry flags and
 * the chunk reshuffle with its TpScramble generator are ported from GARbro's
 * ArcFormats/LiveMaker/ArcVF.cs (MIT, Copyright (C) 2016-2019 by morkt).
 * The index walk, the member-name handling (cp932 escaping, duplicate
 * renaming, unsafe-name refusal) and the bounded zlib output follow this
 * library's ypf reader (src/formats/ypf/xx_ypf.c, MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/livemaker/xx_livemaker.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef LIVEMAKER
#define XX_LIVEMAKER_FILE_TYPE XX_FILE_TYPE_LIVEMAKER
#else
#define XX_LIVEMAKER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LM_HEADER_SIZE 0x0A
#define LM_MAX_NAME 0x100U
/* GARbro's IsSaneCount bound. */
#define LM_MAX_COUNT 0x40000U
#define LM_NAME_KEY 0x75D6EE39U
#define LM_SCRAMBLE_KEY 0xF8EAU
#define LM_NAME_BUFFER (3U * LM_MAX_NAME + 2U + 12U + 1U)
#define LM_POLL_MASK 0x3ffU
/* zlib cannot expand by more than about 1032:1. */
#define LM_MAX_RATIO 1100U
/* A scrambled member is reshuffled in memory. */
#define LM_MAX_SCRAMBLED (64U * 1024U * 1024U)
#define LM_MAX_CHUNKS (1U << 22U)

#define LM_FLAG_ZLIB 0U
#define LM_FLAG_STORED 1U
#define LM_FLAG_SCRAMBLED 2U
#define LM_FLAG_SCRAMBLED_ZLIB 3U

typedef struct lm_member_s {
    int64_t name_offset; /**< Absolute offset of the encrypted name. */
    int64_t data_offset; /**< Absolute offset of the stored bytes. */
    int64_t size;
    uint32_t key_state; /**< TpRandom state before the first name byte. */
    uint16_t name_length;
    uint8_t flags;
    bool renamed;
} lm_member;

typedef struct lm_key_s {
    uint64_t hash;
    uint32_t index;
} lm_key;

typedef struct lm_layout_s {
    int64_t origin; /**< Absolute offset of "vf". */
    int64_t size;   /**< Bytes from origin to EOF. */
    uint32_t count;
    int64_t format_size;
} lm_layout;

/* Sequential buffered reader over [origin, origin + size). */
typedef struct lm_reader_s {
    xx_io_device *device;
    int64_t origin;
    int64_t size;
    int64_t pos;   /**< Next byte, relative to origin. */
    int64_t start; /**< Buffer start, relative to origin. */
    size_t length;
    uint8_t *buffer;
    size_t capacity;
} lm_reader;

typedef struct lm_stream_s {
    lm_member *items;
    size_t count;
    size_t index;
    char *name;
} lm_stream;

/* ---- generators (GARbro ArcVF.cs, MIT) ---------------------------------- */

static uint32_t lm_rand(uint32_t *current)
{
    *current += *current << 2U;
    *current += LM_NAME_KEY;
    return *current;
}

typedef struct lm_scramble_s {
    uint32_t state[5];
} lm_scramble;

static uint32_t lm_scramble_next(lm_scramble *s)
{
    uint64_t v = (uint64_t)2111111111U * s->state[3] + (uint64_t)1492U * s->state[2] + (uint64_t)1776U * s->state[1] + (uint64_t)5115U * s->state[0] + s->state[4];
    s->state[3] = s->state[2];
    s->state[2] = s->state[1];
    s->state[1] = s->state[0];
    s->state[4] = (uint32_t)(v >> 32U);
    s->state[0] = (uint32_t)v;
    return s->state[0];
}

static void lm_scramble_init(lm_scramble *s, uint32_t seed)
{
    uint32_t hash = seed != 0U ? seed : 0xFFFFFFFFU;
    int index;
    for (index = 0; index < 5; ++index) {
        hash ^= hash << 13U;
        hash ^= hash >> 17U;
        hash ^= hash << 5U;
        s->state[index] = hash;
    }
    for (index = 0; index < 19; ++index) (void)lm_scramble_next(s);
}

/* GARbro's GetInt32(0, last): (int)(GetUInt32() / 2^32 * (last + 1)).  The
 * product of a 32-bit and a 31-bit integer is exact here (as in the
 * engine's extended-precision original); a 53-bit double would round it up
 * to the next integer only within 2^-23 of a boundary. */
static uint32_t lm_scramble_pick(lm_scramble *s, uint32_t last)
{
    return (uint32_t)(((uint64_t)lm_scramble_next(s) * ((uint64_t)last + 1U)) >> 32U);
}

/* seq[chunk] = output position of input chunk `chunk`, as GARbro's
 * RandomSequence; the ordered-list removal runs on a Fenwick tree. */
static bool lm_sequence(uint32_t count, uint32_t seed, uint32_t *seq, uint32_t *tree, xx_pd_struct *pd)
{
    lm_scramble generator;
    uint32_t index, left = count, top = 1U;
    for (index = 1U; index <= count; ++index) tree[index] = 0U;
    for (index = 1U; index <= count; ++index) {
        uint32_t parent;
        tree[index] += 1U;
        parent = index + (index & (0U - index));
        if (parent <= count) tree[parent] += tree[index];
    }
    while ((top << 1U) <= count && top < 0x80000000U) top <<= 1U;
    lm_scramble_init(&generator, seed);
    for (index = 0U; left > 0U; ++index) {
        uint32_t rank = left > 1U ? lm_scramble_pick(&generator, left - 2U) : 0U;
        uint32_t at = 0U, step, chosen;
        if ((index & 0xffffU) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        /* Smallest position whose prefix sum exceeds rank. */
        for (step = top; step != 0U; step >>= 1U)
            if (at + step <= count && tree[at + step] <= rank) {
                at += step;
                rank -= tree[at];
            }
        chosen = at + 1U;
        if (chosen > count) return false;
        seq[chosen - 1U] = left > 1U ? index : count - 1U;
        for (at = chosen; at <= count; at += at & (0U - at)) tree[at] -= 1U;
        --left;
    }
    return true;
}

/* ---- I/O --------------------------------------------------------------- */

static size_t lm_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static bool lm_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool lm_write_all(xx_io_device *destination, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    if (!destination) return true;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool lm_copy_range(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t capacity = lm_capacity();
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (remaining > 0) {
        size_t chunk = remaining > (int64_t)capacity ? capacity : (size_t)remaining;
        if ((pd && xx_pd_is_stopped(pd)) || !lm_read_at(source, offset + (size - remaining), buffer, chunk) || !lm_write_all(destination, buffer, chunk)) {
            ok = false;
            break;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* Next `want` (<= LM_MAX_NAME + 8) bytes, or NULL past the end. */
static const uint8_t *lm_take(lm_reader *reader, size_t want)
{
    const uint8_t *view;
    if (reader->pos < 0 || reader->pos > reader->size || (uint64_t)want > (uint64_t)(reader->size - reader->pos)) return NULL;
    if (reader->pos < reader->start || reader->pos + (int64_t)want > reader->start + (int64_t)reader->length) {
        int64_t chunk = reader->size - reader->pos;
        if ((uint64_t)chunk > reader->capacity) chunk = (int64_t)reader->capacity;
        reader->length = 0U;
        if (!lm_read_at(reader->device, reader->origin + reader->pos, reader->buffer, (size_t)chunk)) return NULL;
        reader->start = reader->pos;
        reader->length = (size_t)chunk;
    }
    view = reader->buffer + (reader->pos - reader->start);
    reader->pos += (int64_t)want;
    return view;
}

/* ---- member names (as in the ypf reader) -------------------------------- */

static bool lm_is_sjis_lead(uint8_t c)
{
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool lm_is_sjis_trail(uint8_t c)
{
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t lm_put_escape(char *out, uint8_t c)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

static size_t lm_convert_name(const uint8_t *raw, size_t length, char *out)
{
    size_t at = 0U;
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        if (lm_is_sjis_lead(c) && index + 1U < length && lm_is_sjis_trail(raw[index + 1U])) {
            at += lm_put_escape(out + at, c);
            at += lm_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c == (uint8_t)'%') at += lm_put_escape(out + at, c);
        else if (c == (uint8_t)'\\') out[at++] = '/';
        else out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

static uint64_t lm_name_hash(const char *name, size_t length)
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

static void lm_insert_suffix(char *name, size_t length, uint32_t index)
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
    if (length + suffix_length >= LM_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at) name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool lm_reserved_component(const char *segment, size_t length)
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

static bool lm_safe_name(const char *name)
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
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || lm_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- index walk -------------------------------------------------------- */

static bool lm_read_header(Abstractformat *format, lm_layout *layout)
{
    uint8_t header[LM_HEADER_SIZE];
    int64_t total, size;
    uint32_t count;
    if (!format || !format->device || !layout || format->base_address < 0) return false;
    xx_mem_zero(layout, sizeof(*layout));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* Header, one 1-byte name, two offsets, one flag. */
    if (size < LM_HEADER_SIZE + 5 + 16 + 1 || !lm_read_at(format->device, format->base_address, header, sizeof(header)) || header[0] != (uint8_t)'v' ||
        header[1] != (uint8_t)'f' || header[2] != (uint8_t)'f' || header[3] != 0U)
        return false;
    count = xx_data_get_u32(header + 6, 4, 0, false);
    if (count == 0U || count >= LM_MAX_COUNT || (int64_t)count * (5 + 8 + 1) + 8 > size - LM_HEADER_SIZE) return false;
    layout->origin = format->base_address;
    layout->size = size;
    layout->count = count;
    return true;
}

/* Walk the index.  With items == NULL the pass only validates.  On success
 * sets layout->format_size. */
static bool lm_walk(Abstractformat *format, lm_layout *layout, lm_member *items, lm_key *keys, char *name, xx_pd_struct *pd)
{
    lm_reader reader;
    uint32_t index, current = 0U;
    int64_t previous = 0, index_end;
    bool ok = false;
    uint8_t plain[LM_MAX_NAME];

    xx_mem_zero(&reader, sizeof(reader));
    reader.device = format->device;
    reader.origin = layout->origin;
    reader.size = layout->size;
    reader.pos = LM_HEADER_SIZE;
    /* A small first window keeps the rejection of garbage cheap. */
    reader.capacity = items ? lm_capacity() : 4096U;
    reader.buffer = (uint8_t *)xx_mem_alloc(reader.capacity);
    if (!reader.buffer) return false;

    for (index = 0U; index < layout->count; ++index) {
        const uint8_t *view;
        uint32_t length, k;
        uint32_t state = current;
        if ((index & LM_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (!(view = lm_take(&reader, 4U))) goto done;
        length = xx_data_get_u32(view, 4, 0, false);
        if (length == 0U || length > LM_MAX_NAME) goto done;
        if (!(view = lm_take(&reader, length))) goto done;
        for (k = 0U; k < length; ++k) {
            uint8_t c = (uint8_t)(view[k] ^ (uint8_t)lm_rand(&current));
            if (c < 0x20U || c == 0x7fU) goto done;
            plain[k] = c;
        }
        if (items) {
            size_t converted = lm_convert_name(plain, length, name);
            items[index].name_offset = layout->origin + reader.pos - (int64_t)length;
            items[index].name_length = (uint16_t)length;
            items[index].key_state = state;
            items[index].renamed = false;
            keys[index].hash = lm_name_hash(name, converted);
            keys[index].index = index;
        }
    }
    index_end = reader.pos + 8 * ((int64_t)layout->count + 1) + (int64_t)layout->count;
    if (index_end > layout->size) goto done;
    current = 0U;
    for (index = 0U; index <= layout->count; ++index) {
        const uint8_t *view = lm_take(&reader, 8U);
        uint64_t mask, raw;
        int64_t offset;
        if (!view) goto done;
        mask = (uint64_t)(int64_t)(int32_t)lm_rand(&current);
        raw = xx_data_get_u64(view, 8, 0, false) ^ mask;
        if (raw > (uint64_t)layout->size) goto done;
        offset = (int64_t)raw;
        if (index == 0U ? offset < index_end : offset < previous) goto done;
        if (items && index > 0U) {
            items[index - 1U].data_offset = layout->origin + previous;
            items[index - 1U].size = offset - previous;
        }
        previous = offset;
    }
    for (index = 0U; index < layout->count; ++index) {
        const uint8_t *view = lm_take(&reader, 1U);
        if (!view || view[0] > LM_FLAG_SCRAMBLED_ZLIB) goto done;
        if (items) items[index].flags = view[0];
    }
    layout->format_size = previous;
    ok = true;
done:
    xx_mem_free(reader.buffer);
    return ok;
}

static bool lm_resolve(Abstractformat *format, lm_layout *layout, xx_pd_struct *pd)
{
    return lm_read_header(format, layout) && lm_walk(format, layout, NULL, NULL, NULL, pd);
}

static int lm_compare_keys(const void *left, const void *right)
{
    const lm_key *a = (const lm_key *)left;
    const lm_key *b = (const lm_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static void lm_mark_duplicates(lm_member *items, lm_key *keys, size_t count)
{
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), lm_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash && keys[index].index < count) items[keys[index].index].renamed = true;
}

static void lm_stream_free(void *opaque)
{
    lm_stream *stream = (lm_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool lm_open_stream(Abstractformat *format, lm_stream **result, xx_pd_struct *pd)
{
    lm_layout layout;
    lm_member *items = NULL;
    lm_key *keys = NULL;
    char *name = NULL;
    lm_stream *stream = NULL;
    if (!result || !lm_resolve(format, &layout, pd)) return false;
    /* count < 0x40000: at most about 12 MiB of bookkeeping. */
    items = (lm_member *)xx_mem_calloc(layout.count, sizeof(*items));
    keys = (lm_key *)xx_mem_alloc((size_t)layout.count * sizeof(*keys));
    name = (char *)xx_mem_alloc(LM_NAME_BUFFER);
    stream = (lm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!items || !keys || !name || !stream || !lm_walk(format, &layout, items, keys, name, pd)) goto fail;
    lm_mark_duplicates(items, keys, layout.count);
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

static bool lm_load_name(Abstractformat *format, lm_stream *stream, size_t index)
{
    const lm_member *member = &stream->items[index];
    uint8_t raw[LM_MAX_NAME];
    uint32_t current = member->key_state;
    size_t length, k;
    if (member->name_length == 0U || member->name_length > LM_MAX_NAME || !lm_read_at(format->device, member->name_offset, raw, member->name_length)) return false;
    for (k = 0U; k < member->name_length; ++k) raw[k] ^= (uint8_t)lm_rand(&current);
    length = lm_convert_name(raw, member->name_length, stream->name);
    if (member->renamed) lm_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

/* ---- member data -------------------------------------------------------- */

/* A write-only device that forwards to `target` (or discards) and fails as
 * soon as more than `limit` bytes arrive. */
typedef struct lm_limit_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} lm_limit;

static ssize_t lm_limit_write(xx_io_device *self, const void *buffer, size_t size)
{
    lm_limit *limit = (lm_limit *)self;
    if (size > (SIZE_MAX >> 1) || (uint64_t)size > limit->limit - limit->written) return -1;
    if (!lm_write_all(limit->target, (const uint8_t *)buffer, size)) return -1;
    limit->written += (uint64_t)size;
    return (ssize_t)size;
}

static void lm_limit_init(lm_limit *limit, xx_io_device *destination, int64_t packed)
{
    xx_mem_zero(limit, sizeof(*limit));
    limit->device.write = lm_limit_write;
    limit->target = destination;
    limit->limit = (uint64_t)packed * LM_MAX_RATIO + 1024U;
}

static bool lm_unpack_zlib_device(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t header[2];
    lm_limit limit;
    if (size < 2 || !lm_read_at(source, offset, header, sizeof(header)) || !xx_zlib_stream_header_is_valid(header, sizeof(header))) return false;
    lm_limit_init(&limit, destination, size);
    return xx_deflate_unpack_device(source, offset + 2, size - 2, &limit.device, false, pd);
}

static bool lm_unpack_scrambled(xx_io_device *source, const lm_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t header[8];
    uint8_t *input = NULL, *output = NULL;
    uint32_t *seq = NULL, *tree = NULL;
    uint32_t chunk, count, index;
    size_t length, done = 0U;
    bool ok = false;
    /* GARbro: a member of 8 bytes or less yields nothing. */
    if (member->size <= 8) return true;
    if (member->size - 8 > (int64_t)LM_MAX_SCRAMBLED || !lm_read_at(source, member->data_offset, header, sizeof(header))) return false;
    chunk = xx_data_get_u32(header, 4, 0, false);
    length = (size_t)(member->size - 8);
    if (chunk == 0U || chunk > 0x7fffffffU) return false;
    count = (uint32_t)((length - 1U) / chunk + 1U);
    if (count > LM_MAX_CHUNKS) return false;
    input = (uint8_t *)xx_mem_alloc(length);
    output = (uint8_t *)xx_mem_alloc(length);
    seq = (uint32_t *)xx_mem_alloc((size_t)count * sizeof(*seq));
    tree = (uint32_t *)xx_mem_alloc(((size_t)count + 1U) * sizeof(*tree));
    if (!input || !output || !seq || !tree || !lm_read_at(source, member->data_offset + 8, input, length) ||
        !lm_sequence(count, xx_data_get_u32(header + 4, 4, 0, false) ^ LM_SCRAMBLE_KEY, seq, tree, pd))
        goto done;
    for (index = 0U; index < count; ++index) {
        size_t position = (size_t)seq[index] * chunk, piece;
        if (seq[index] >= count || position >= length) goto done;
        piece = length - position < chunk ? length - position : chunk;
        if (piece > length - done) goto done;
        xx_rt_memcpy(output + done, input + position, piece);
        done += piece;
    }
    if (done != length) goto done;
    if (member->flags == LM_FLAG_SCRAMBLED) {
        ok = lm_write_all(destination, output, length);
    } else {
        lm_limit limit;
        if (length < 2U || !xx_zlib_stream_header_is_valid(output, 2U)) goto done;
        lm_limit_init(&limit, destination, (int64_t)length);
        ok = xx_deflate_unpack_memory_to_device(output + 2, length - 2U, &limit.device, false, pd);
    }
done:
    if (input) xx_mem_free(input);
    if (output) xx_mem_free(output);
    if (seq) xx_mem_free(seq);
    if (tree) xx_mem_free(tree);
    return ok;
}

static bool lm_unpack_member(xx_io_device *source, const lm_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    switch (member->flags) {
        case LM_FLAG_ZLIB: return lm_unpack_zlib_device(source, member->data_offset, member->size, destination, pd);
        case LM_FLAG_STORED: return lm_copy_range(source, member->data_offset, member->size, destination, pd);
        case LM_FLAG_SCRAMBLED:
        case LM_FLAG_SCRAMBLED_ZLIB: return lm_unpack_scrambled(source, member, destination, pd);
        default: return false;
    }
}

/* ---- records ----------------------------------------------------------- */

static bool lm_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *lm_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lm_set_record(Abstractformat *format, xx_archive_record *record, lm_stream *stream, size_t index)
{
    const lm_member *member = &stream->items[index];
    const bool zlib = member->flags == LM_FLAG_ZLIB || member->flags == LM_FLAG_SCRAMBLED_ZLIB;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!lm_load_name(format, stream, index)) return false;
    record->header_offset = member->name_offset - 4;
    record->header_size = 4 + (int64_t)member->name_length;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    if (!zlib) {
        int64_t plain = member->flags == LM_FLAG_STORED ? member->size : (member->size > 8 ? member->size - 8 : 0);
        if (!xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)plain)) return false;
    }
    return xx_archive_record_set_original_name(record, stream->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, zlib ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_livemaker_init(xx_livemaker *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LIVEMAKER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_livemaker_check_is_valid;
    archive->format.handle_base_info = xx_livemaker_handle_base_info;
    archive->format.get_format_size = xx_livemaker_get_format_size;
    archive->format.get_number_of_archive_records = xx_livemaker_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_livemaker_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_livemaker_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_livemaker_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_livemaker_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_livemaker_free_archive_records_reading;
}

xx_livemaker *xx_livemaker_create(xx_io_device *device, int64_t base_address)
{
    xx_livemaker *archive = (xx_livemaker *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_livemaker_init(archive, device, base_address);
    return archive;
}

void xx_livemaker_destroy(xx_livemaker *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_livemaker_free(xx_livemaker *archive)
{
    if (!archive) return;
    xx_livemaker_destroy(archive);
    xx_mem_free(archive);
}

bool xx_livemaker_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    lm_layout layout;
    return lm_resolve(format, &layout, pd);
}

bool xx_livemaker_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    lm_layout layout;
    xx_livemaker *archive;
    if (!lm_resolve(format, &layout, pd)) return false;
    archive = (xx_livemaker *)format;
    archive->number_of_records = layout.count;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_livemaker_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_livemaker_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_livemaker_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_livemaker_handle_base_info(format, pd)) ? ((xx_livemaker *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_livemaker_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    lm_stream *stream;
    xx_archive_record_state *state;
    if (!lm_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lm_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lm_stream_free;
    state->total_records = stream->count;
    if (!lm_copy_options(&state->options, options) || !lm_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_livemaker_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_livemaker_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    lm_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (lm_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = lm_set_record(format, &state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_livemaker_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    lm_stream *stream;
    const lm_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (lm_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->size < 0 || member->data_offset < 0) return false;
    path_option = lm_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) /* No destination: decode into nothing, which verifies the member. */
        return lm_unpack_member(format->device, member, NULL, pd);
    if (!lm_safe_name(stream->name)) return false;
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
        created = destination != NULL;
        if (!destination) goto done;
        result = lm_unpack_member(format->device, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_livemaker_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
