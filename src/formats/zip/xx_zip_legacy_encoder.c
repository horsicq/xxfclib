/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Original encoders implementing PKWARE APPNOTE sections 5.1..5.3 and the
 * public DCL fixed-code format. No external codec or C runtime is required.
 */
#include "xx_zip_legacy_encoder.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
#include <stdio.h>

#define ZIP_LEGACY_IO_SIZE 32768U
#define ZIP_LEGACY_HASH_SIZE 16384U
#define ZIP_LEGACY_WINDOW_SIZE 8192U
#define ZIP_LEGACY_NONE UINT32_MAX

typedef struct zip_legacy_bits {
    xx_io_device *destination;
    uint8_t output[ZIP_LEGACY_IO_SIZE];
    size_t used;
    int64_t written;
    uint32_t bits;
    unsigned count;
} zip_legacy_bits;

typedef struct zip_legacy_matcher {
    uint32_t head[ZIP_LEGACY_HASH_SIZE];
    uint32_t previous[ZIP_LEGACY_WINDOW_SIZE];
} zip_legacy_matcher;

static bool zip_legacy_flush(zip_legacy_bits *writer)
{
    size_t at = 0U;
    while (at < writer->used) {
        ssize_t count = xx_io_write(writer->destination, writer->output + at, writer->used - at);
        if (count <= 0 || (size_t)count > writer->used - at) return false;
        at += (size_t)count;
    }
    if ((uint64_t)writer->used > (uint64_t)(INT64_MAX - writer->written)) return false;
    writer->written += (int64_t)writer->used;
    writer->used = 0U;
    return true;
}

static bool zip_legacy_byte(zip_legacy_bits *writer, uint8_t value)
{
    writer->output[writer->used++] = value;
    return writer->used < sizeof(writer->output) || zip_legacy_flush(writer);
}

static bool zip_legacy_put(zip_legacy_bits *writer, unsigned value, unsigned count)
{
    writer->bits |= (uint32_t)value << writer->count;
    writer->count += count;
    while (writer->count >= 8U) {
        if (!zip_legacy_byte(writer, (uint8_t)writer->bits)) return false;
        writer->bits >>= 8U;
        writer->count -= 8U;
    }
    return true;
}

static bool zip_legacy_finish(zip_legacy_bits *writer)
{
    if (writer->count && !zip_legacy_byte(writer, (uint8_t)writer->bits)) return false;
    writer->bits = 0U;
    writer->count = 0U;
    return zip_legacy_flush(writer);
}

static unsigned zip_legacy_reverse(unsigned value, unsigned count)
{
    unsigned result = 0U;
    while (count--) {
        result = (result << 1U) | (value & 1U);
        value >>= 1U;
    }
    return result;
}

static bool zip_legacy_cancelled(xx_pd_struct *progress, size_t at)
{
    /* Match and dictionary probes have their own fixed bounds. */
    (void)at;
    return progress && xx_pd_is_stopped(progress);
}

static unsigned zip_legacy_hash(const uint8_t *input)
{
    return (((unsigned)input[0] * 251U + input[1]) * 251U + input[2]) & (ZIP_LEGACY_HASH_SIZE - 1U);
}

static void zip_legacy_insert(zip_legacy_matcher *matcher, const uint8_t *input, size_t size, size_t at)
{
    unsigned hash;
    if (size - at < 3U) return;
    hash = zip_legacy_hash(input + at);
    matcher->previous[at & (ZIP_LEGACY_WINDOW_SIZE - 1U)] = matcher->head[hash];
    matcher->head[hash] = (uint32_t)at;
}

static unsigned zip_legacy_find(zip_legacy_matcher *matcher, const uint8_t *input, size_t size, size_t at, unsigned window, unsigned maximum, unsigned probes,
                                unsigned *distance)
{
    uint32_t candidate;
    unsigned best = 0U;
    if (size - at < 3U) return 0U;
    if (maximum > size - at) maximum = (unsigned)(size - at);
    candidate = matcher->head[zip_legacy_hash(input + at)];
    while (candidate != ZIP_LEGACY_NONE && candidate < at && at - candidate <= window && probes--) {
        unsigned length = 0U;
        uint32_t next;
        while (length < maximum && input[candidate + length] == input[at + length]) ++length;
        if (length > best) {
            best = length;
            *distance = (unsigned)(at - candidate);
            if (length == maximum) break;
        }
        next = matcher->previous[candidate & (ZIP_LEGACY_WINDOW_SIZE - 1U)];
        if (next >= candidate) break;
        candidate = next;
    }
    return best >= 3U ? best : 0U;
}

/* Shrink uses explicit width-change markers, so it can keep a full
 * dictionary without partial clears. The dictionary hash has <= 1/2 load. */
typedef struct zip_shrink_slot {
    uint32_t key;
    uint16_t code;
} zip_shrink_slot;

static bool zip_shrink_code(zip_legacy_bits *writer, unsigned code, unsigned *width)
{
    while (code >= (1U << *width)) {
        if (*width >= 13U || !zip_legacy_put(writer, 256U, *width) || !zip_legacy_put(writer, 1U, *width)) return false;
        ++*width;
    }
    return zip_legacy_put(writer, code, *width);
}

static bool zip_shrink_encode(const uint8_t *input, size_t size, zip_legacy_bits *writer, xx_pd_struct *progress)
{
    zip_shrink_slot *slots;
    unsigned current, width = 9U, next_code = 257U;
    size_t at;
    bool ok = true;
    if (!size) return true;
    slots = (zip_shrink_slot *)xx_mem_alloc(sizeof(*slots) * ZIP_LEGACY_HASH_SIZE);
    if (!slots) return false;
    xx_mem_zero(slots, sizeof(*slots) * ZIP_LEGACY_HASH_SIZE);
    current = input[0];
    for (at = 1U; at < size; ++at) {
        uint32_t key = ((uint32_t)current << 8U) | input[at];
        unsigned slot = (unsigned)((key * 2654435761U) >> 18U);
        while (slots[slot].code && slots[slot].key != key) slot = (slot + 1U) & (ZIP_LEGACY_HASH_SIZE - 1U);
        if (slots[slot].code) {
            current = slots[slot].code;
        } else {
            if (!zip_shrink_code(writer, current, &width)) {
                ok = false;
                break;
            }
            if (next_code < 8192U) {
                slots[slot].key = key;
                slots[slot].code = (uint16_t)next_code++;
            }
            current = input[at];
        }
        if ((at & 4095U) == 0U && zip_legacy_cancelled(progress, at)) {
            ok = false;
            break;
        }
    }
    if (ok) ok = zip_shrink_code(writer, current, &width);
    xx_mem_free(slots);
    return ok;
}

static bool zip_reduce_match(zip_legacy_bits *writer, unsigned factor, unsigned distance, unsigned length)
{
    unsigned mask = (1U << (8U - factor)) - 1U;
    unsigned value = length - 3U;
    unsigned command = ((distance - 1U) >> 8U) << (8U - factor);
    command |= value < mask ? value : mask;
    /* Zero is the escaped literal, never a match command. */
    if (command == 0U) return false;
    return zip_legacy_put(writer, 144U, 8U) && zip_legacy_put(writer, command, 8U) && (value < mask || zip_legacy_put(writer, value - mask, 8U)) &&
           zip_legacy_put(writer, (distance - 1U) & 255U, 8U);
}

/* These are the DCL fixed length/distance tree code lengths, expanded from
 * the format's run descriptions by the same canonical construction as ZIP. */
static const uint8_t zip_dcl_length_runs[] = {2, 35, 36, 53, 38, 23};
static const uint8_t zip_dcl_distance_runs[] = {2, 20, 53, 230, 247, 151, 248};
static const uint16_t zip_dcl_length_base[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
static const uint8_t zip_dcl_length_extra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

typedef struct zip_dcl_codes {
    uint8_t width[64];
    uint16_t bits[64];
} zip_dcl_codes;

static void zip_dcl_build(zip_dcl_codes *codes, const uint8_t *runs, size_t size)
{
    unsigned counts[14] = {0U};
    unsigned next[14] = {0U};
    unsigned at = 0U, symbol, width, code = 0U;
    size_t run;
    for (run = 0U; run < size; ++run) {
        unsigned count = (runs[run] >> 4U) + 1U;
        while (count--) {
            width = runs[run] & 15U;
            codes->width[at++] = (uint8_t)width;
            ++counts[width];
        }
    }
    for (width = 1U; width < 14U; ++width) {
        code = (code + counts[width - 1U]) << 1U;
        next[width] = code;
    }
    for (symbol = 0U; symbol < at; ++symbol) {
        width = codes->width[symbol];
        codes->bits[symbol] = (uint16_t)zip_legacy_reverse((~next[width]++) & ((1U << width) - 1U), width);
    }
}

static bool zip_dcl_code(zip_legacy_bits *writer, const zip_dcl_codes *codes, unsigned symbol)
{
    return zip_legacy_put(writer, codes->bits[symbol], codes->width[symbol]);
}

static bool zip_lz_encode(const uint8_t *input, size_t size, uint16_t method, int level, zip_legacy_bits *writer, xx_pd_struct *progress)
{
    zip_legacy_matcher *matcher;
    zip_dcl_codes lengths, distances;
    unsigned factor = method >= 2U && method <= 5U ? method - 1U : 0U;
    unsigned window = factor ? (256U << factor) : method == 6U ? 8192U : 4096U;
    unsigned maximum = factor ? ((1U << (8U - factor)) - 1U) + 258U : method == 6U ? 320U : 518U;
    unsigned probes = level <= 1 ? 16U : level >= 8 ? 256U : 64U;
    size_t at = 0U;
    bool ok = true;
    matcher = (zip_legacy_matcher *)xx_mem_alloc(sizeof(*matcher));
    if (!matcher) return false;
    /* UINT32_MAX is the unused position sentinel. */
    {
        size_t index;
        for (index = 0U; index < ZIP_LEGACY_HASH_SIZE; ++index) matcher->head[index] = ZIP_LEGACY_NONE;
        for (index = 0U; index < ZIP_LEGACY_WINDOW_SIZE; ++index) matcher->previous[index] = ZIP_LEGACY_NONE;
    }
    if (factor) {
        unsigned symbol;
        /* Empty follower sets: the second-stage encoded bytes stay raw. */
        for (symbol = 0U; symbol < 256U && ok; ++symbol) ok = zip_legacy_put(writer, 0U, 6U);
    } else if (method == 6U) {
        unsigned tree, run;
        for (tree = 0U; tree < 2U && ok; ++tree) {
            ok = zip_legacy_put(writer, 3U, 8U);
            for (run = 0U; run < 4U && ok; ++run) ok = zip_legacy_put(writer, 0xF5U, 8U);
        }
    } else {
        zip_dcl_build(&lengths, zip_dcl_length_runs, sizeof(zip_dcl_length_runs));
        zip_dcl_build(&distances, zip_dcl_distance_runs, sizeof(zip_dcl_distance_runs));
        ok = zip_legacy_put(writer, 0U, 8U) && zip_legacy_put(writer, 6U, 8U);
    }
    while (at < size && ok) {
        unsigned distance = 0U;
        unsigned length;
        size_t end;
        if (zip_legacy_cancelled(progress, at)) {
            ok = false;
            break;
        }
        length = zip_legacy_find(matcher, input, size, at, window, maximum, probes, &distance);
        if (factor && length == 3U && distance <= 256U) length = 0U;
        if (length) {
            if (factor) {
                ok = zip_reduce_match(writer, factor, distance, length);
            } else if (method == 6U) {
                unsigned symbol = length - 2U;
                if (symbol > 63U) symbol = 63U;
                ok = zip_legacy_put(writer, 0U, 1U) && zip_legacy_put(writer, (distance - 1U) & 127U, 7U) &&
                     zip_legacy_put(writer, zip_legacy_reverse((~((distance - 1U) >> 7U)) & 63U, 6U), 6U) &&
                     zip_legacy_put(writer, zip_legacy_reverse((~symbol) & 63U, 6U), 6U) && (symbol != 63U || zip_legacy_put(writer, length - 65U, 8U));
            } else {
                unsigned symbol = 0U;
                while (symbol < 16U && (length < zip_dcl_length_base[symbol] || length - zip_dcl_length_base[symbol] >= (1U << zip_dcl_length_extra[symbol]))) ++symbol;
                ok = symbol < 16U && zip_legacy_put(writer, 1U, 1U) && zip_dcl_code(writer, &lengths, symbol) &&
                     zip_legacy_put(writer, length - zip_dcl_length_base[symbol], zip_dcl_length_extra[symbol]) &&
                     zip_dcl_code(writer, &distances, (distance - 1U) >> 6U) && zip_legacy_put(writer, (distance - 1U) & 63U, 6U);
            }
        } else {
            uint8_t value = input[at];
            length = 1U;
            if (factor) {
                ok = zip_legacy_put(writer, value, 8U) && (value != 144U || zip_legacy_put(writer, 0U, 8U));
            } else {
                ok = zip_legacy_put(writer, method == 6U ? 1U : 0U, 1U) && zip_legacy_put(writer, value, 8U);
            }
        }
        end = at + length;
        while (at < end) zip_legacy_insert(matcher, input, size, at++);
    }
    if (ok && method == 10U) ok = zip_legacy_put(writer, 1U, 1U) && zip_dcl_code(writer, &lengths, 15U) && zip_legacy_put(writer, 255U, 8U);
    xx_mem_free(matcher);
    return ok;
}

bool xx_zip_legacy_pack_source(xx_io_device *source, const char *source_path, uint16_t method, int level, int64_t *uncompressed_size, int64_t *compressed_size,
                               uint32_t *crc32, xx_io_device *destination, xx_pd_struct *progress)
{
    xx_io_device *owned = NULL;
    zip_legacy_bits *writer = NULL;
    uint8_t *input = NULL;
    int64_t total;
    size_t at = 0U;
    uint32_t crc = 0U;
    bool ok = false;
    if (uncompressed_size) *uncompressed_size = 0;
    if (compressed_size) *compressed_size = 0;
    if (crc32) *crc32 = 0U;
    if (!destination || !uncompressed_size || !compressed_size || !crc32 || !((method >= 1U && method <= 6U) || method == 10U) || zip_legacy_cancelled(progress, 0U))
        return false;
    if (!source) {
        if (!source_path) return false;
        owned = xx_io_file_open(source_path, "rb");
        source = owned;
    }
    if (!source || source == destination) goto cleanup;
    total = xx_io_size(source);
    if (total < 0 || (uint64_t)total >= UINT32_MAX || (uint64_t)total > SIZE_MAX || (method == 10U && (uint64_t)total > 64U * 1024U * 1024U) ||
        xx_io_seek64(source, 0, SEEK_SET) != 0)
        goto cleanup;
    input = (uint8_t *)xx_mem_alloc(total ? (size_t)total : 1U);
    writer = (zip_legacy_bits *)xx_mem_alloc(sizeof(*writer));
    if (!input || !writer) goto cleanup;
    xx_mem_zero(writer, sizeof(*writer));
    writer->destination = destination;
    while (at < (size_t)total) {
        size_t request = (size_t)total - at;
        ssize_t count;
        if (request > ZIP_LEGACY_IO_SIZE) request = ZIP_LEGACY_IO_SIZE;
        if (zip_legacy_cancelled(progress, at)) goto cleanup;
        count = xx_io_read(source, input + at, request);
        if (count <= 0 || (size_t)count > request) goto cleanup;
        crc = xx_crc32_calc(crc, input + at, (size_t)count);
        at += (size_t)count;
    }
    if (!(method == 1U ? zip_shrink_encode(input, at, writer, progress) : zip_lz_encode(input, at, method, level, writer, progress)) || !zip_legacy_finish(writer) ||
        zip_legacy_cancelled(progress, at) || (method == 10U && (uint64_t)writer->written > 64U * 1024U * 1024U))
        goto cleanup;
    *uncompressed_size = total;
    *compressed_size = writer->written;
    *crc32 = crc;
    ok = true;
cleanup:
    xx_mem_free(input);
    xx_mem_free(writer);
    if (owned) xx_io_close(owned);
    return ok;
}
