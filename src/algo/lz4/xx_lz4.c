/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Independently written LZ4 frame and block decoder. The implementation follows
 * the public LZ4 frame and block format descriptions and has no dependency on
 * liblz4 or XArchive.
 */

#include "xxfclib/algo/lz4/xx_lz4.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include "xxfclib/data/xx_data.h"

#define XX_LZ4_FRAME_MAGIC UINT32_C(0x184D2204)
#define XX_LZ4_SKIP_MAGIC UINT32_C(0x184D2A50)

#define XXH32_PRIME1 UINT32_C(2654435761)
#define XXH32_PRIME2 UINT32_C(2246822519)
#define XXH32_PRIME3 UINT32_C(3266489917)
#define XXH32_PRIME4 UINT32_C(668265263)
#define XXH32_PRIME5 UINT32_C(374761393)

static uint32_t xx_lz4_rotl32(uint32_t value, unsigned count)
{
    return (value << count) | (value >> (32U - count));
}

static uint32_t xx_lz4_xxh_round(uint32_t state, uint32_t input)
{
    state += input * XXH32_PRIME2;
    state = xx_lz4_rotl32(state, 13);
    return state * XXH32_PRIME1;
}

static uint32_t xx_lz4_xxh32(const uint8_t *data, size_t size)
{
    const uint8_t *cursor = data;
    const uint8_t *end = data + size;
    uint32_t hash;

    if (size >= 16U) {
        const uint8_t *limit = end - 16U;
        uint32_t lane1 = XXH32_PRIME1 + XXH32_PRIME2;
        uint32_t lane2 = XXH32_PRIME2;
        uint32_t lane3 = 0;
        uint32_t lane4 = 0U - XXH32_PRIME1;
        do {
            lane1 = xx_lz4_xxh_round(lane1, xx_data_get_u32(cursor, 4, 0, false));
            lane2 = xx_lz4_xxh_round(lane2, xx_data_get_u32(cursor + 4, 4, 0, false));
            lane3 = xx_lz4_xxh_round(lane3, xx_data_get_u32(cursor + 8, 4, 0, false));
            lane4 = xx_lz4_xxh_round(lane4, xx_data_get_u32(cursor + 12, 4, 0, false));
            cursor += 16;
        } while (cursor <= limit);
        hash = xx_lz4_rotl32(lane1, 1) + xx_lz4_rotl32(lane2, 7) + xx_lz4_rotl32(lane3, 12) + xx_lz4_rotl32(lane4, 18);
    } else {
        hash = XXH32_PRIME5;
    }

    hash += (uint32_t)size;
    while ((size_t)(end - cursor) >= 4U) {
        hash += xx_data_get_u32(cursor, 4, 0, false) * XXH32_PRIME3;
        hash = xx_lz4_rotl32(hash, 17) * XXH32_PRIME4;
        cursor += 4;
    }
    while (cursor < end) {
        hash += (uint32_t)(*cursor++) * XXH32_PRIME5;
        hash = xx_lz4_rotl32(hash, 11) * XXH32_PRIME1;
    }
    hash ^= hash >> 15;
    hash *= XXH32_PRIME2;
    hash ^= hash >> 13;
    hash *= XXH32_PRIME3;
    hash ^= hash >> 16;
    return hash;
}

static bool xx_lz4_extended_length(const uint8_t **cursor, const uint8_t *end, size_t *length)
{
    uint8_t value;
    do {
        if (*cursor == end) return false;
        value = *(*cursor)++;
        if (*length > SIZE_MAX - value) return false;
        *length += value;
    } while (value == UINT8_C(255));
    return true;
}

static bool xx_lz4_decode_block(const uint8_t *source, size_t source_size, uint8_t *destination, size_t destination_capacity, size_t history_size, size_t *out_written)
{
    const uint8_t *input = source;
    const uint8_t *input_end = source + source_size;
    size_t output = history_size;
    const size_t output_limit = destination_capacity;

    if (out_written) *out_written = 0;
    if (history_size > destination_capacity) return false;

    while (input < input_end) {
        uint8_t token = *input++;
        size_t literal_length = (size_t)(token >> 4);
        size_t match_length;
        size_t distance;
        size_t match;

        if (literal_length == 15U && !xx_lz4_extended_length(&input, input_end, &literal_length)) {
            return false;
        }
        if (literal_length > (size_t)(input_end - input) || literal_length > output_limit - output) {
            return false;
        }
        for (size_t index = 0; index < literal_length; ++index) {
            destination[output + index] = input[index];
        }
        input += literal_length;
        output += literal_length;

        if (input == input_end) {
            if (out_written) *out_written = output - history_size;
            return true;
        }
        if ((size_t)(input_end - input) < 2U) return false;
        distance = (size_t)input[0] | ((size_t)input[1] << 8);
        input += 2;
        if (distance == 0U || distance > output) return false;

        match_length = (size_t)(token & 15U);
        if (match_length == 15U && !xx_lz4_extended_length(&input, input_end, &match_length)) {
            return false;
        }
        if (match_length > SIZE_MAX - 4U) return false;
        match_length += 4U;
        if (match_length > output_limit - output) return false;
        match = output - distance;
        for (size_t index = 0; index < match_length; ++index) {
            destination[output++] = destination[match++];
        }
    }

    if (out_written) *out_written = output - history_size;
    return true;
}

static bool xx_lz4_block_limit(uint8_t descriptor, size_t *limit)
{
    switch ((descriptor >> 4) & 7U) {
        case 4: *limit = 64U * 1024U; return true;
        case 5: *limit = 256U * 1024U; return true;
        case 6: *limit = 1024U * 1024U; return true;
        case 7: *limit = 4U * 1024U * 1024U; return true;
        default: return false;
    }
}

static bool xx_lz4_decompress_frames_impl(const void *source, size_t source_size, void *destination, size_t destination_size, bool exact, size_t *out_written, bool *cut,
                                          size_t *checked);

bool xx_lz4_decompress_memory(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written)
{
    return xx_lz4_decompress_frames_impl(source, source_size, destination, destination_size, true, out_written, NULL, NULL);
}

bool xx_lz4_decompress_frames(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written)
{
    return xx_lz4_decompress_frames_impl(source, source_size, destination, destination_capacity, false, out_written, NULL, NULL);
}

bool xx_lz4_decompress_memory_prefix(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written, bool *cut, size_t *checked)
{
    return cut && xx_lz4_decompress_frames_impl(source, source_size, destination, destination_size, false, out_written, cut, checked);
}

bool xx_lz4_decompress_block(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written)
{
    /* A raw block starts with no history: the internal decoder carries a
     * history_size because the frame path chains blocks through a shared
     * window, which a standalone block never does. */
    if (out_written) *out_written = 0;
    if (!source || !destination) return false;
    return xx_lz4_decode_block((const uint8_t *)source, source_size, (uint8_t *)destination, destination_capacity, 0U, out_written);
}

/* With cut, the input may end inside a frame: the call then fails with
 * *cut set and *out_written the bytes of the whole blocks before that point
 * (and of what there is of a cut uncompressed block), which the checksums
 * still to come cannot check. *checked (optional): the bytes from the start
 * that block and content checksums vouch for. */
static bool xx_lz4_decompress_frames_impl(const void *source, size_t source_size, void *destination, size_t destination_size, bool exact, size_t *out_written, bool *cut,
                                          size_t *checked)
{
    const uint8_t *input = (const uint8_t *)source;
    const uint8_t *end;
    uint8_t *output = (uint8_t *)destination;
    uint8_t empty_output = 0;
    size_t output_position = 0;
    bool saw_frame = false;

    if (out_written) *out_written = 0;
    if (cut) *cut = false;
    if (checked) *checked = 0U;
    if ((!input && source_size != 0U) || (!output && destination_size != 0U)) {
        return false;
    }
    if (!output) output = &empty_output;
    end = input + source_size;
/* The input ends here, inside a frame. */
#define XX_LZ4_ENDED()                                   \
    do {                                                 \
        if (!cut) return false;                          \
        *cut = true;                                     \
        if (out_written) *out_written = output_position; \
        return false;                                    \
    } while (0)

    while (input < end) {
        uint32_t magic;
        const uint8_t *descriptor_start;
        uint8_t flags;
        uint8_t block_descriptor;
        size_t block_limit;
        size_t frame_output_start;
        uint64_t content_size = 0;
        bool has_content_size;
        bool independent_blocks;
        bool block_checksum;
        bool content_checksum;

        if ((size_t)(end - input) < 4U) XX_LZ4_ENDED();
        magic = xx_data_get_u32(input, 4, 0, false);
        input += 4;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_LZ4_SKIP_MAGIC) {
            uint32_t skip_size;
            if ((size_t)(end - input) < 4U) XX_LZ4_ENDED();
            skip_size = xx_data_get_u32(input, 4, 0, false);
            input += 4;
            if ((size_t)(end - input) < (size_t)skip_size) XX_LZ4_ENDED();
            input += skip_size;
            continue;
        }
        if (magic != XX_LZ4_FRAME_MAGIC) return false;
        if ((size_t)(end - input) < 3U) XX_LZ4_ENDED();

        descriptor_start = input;
        flags = *input++;
        block_descriptor = *input++;
        if ((flags >> 6) != 1U || (flags & 2U) != 0U || (block_descriptor & UINT8_C(0x8F)) != 0U || !xx_lz4_block_limit(block_descriptor, &block_limit)) {
            return false;
        }
        independent_blocks = (flags & UINT8_C(0x20)) != 0U;
        block_checksum = (flags & UINT8_C(0x10)) != 0U;
        has_content_size = (flags & UINT8_C(0x08)) != 0U;
        content_checksum = (flags & UINT8_C(0x04)) != 0U;

        if (has_content_size) {
            if ((size_t)(end - input) < 8U) XX_LZ4_ENDED();
            content_size = xx_data_get_u64(input, 8, 0, false);
            input += 8;
        }
        if ((flags & 1U) != 0U) {
            /* Dictionary IDs require an external dictionary, which this API
             * intentionally does not accept. */
            return false;
        }
        if (input == end) XX_LZ4_ENDED();
        if (*input != (uint8_t)(xx_lz4_xxh32(descriptor_start, (size_t)(input - descriptor_start)) >> 8)) {
            return false;
        }
        ++input;

        frame_output_start = output_position;
        for (;;) {
            uint32_t stored_size;
            size_t block_size;
            bool uncompressed;
            const uint8_t *block_data;
            size_t block_output = 0;
            size_t history_size;

            if ((size_t)(end - input) < 4U) XX_LZ4_ENDED();
            stored_size = xx_data_get_u32(input, 4, 0, false);
            input += 4;
            if (stored_size == 0U) break;
            uncompressed = (stored_size & UINT32_C(0x80000000)) != 0U;
            block_size = (size_t)(stored_size & UINT32_C(0x7FFFFFFF));
            if (block_size == 0U || block_size > block_limit) {
                return false;
            }
            if ((size_t)(end - input) < block_size) {
                /* An uncompressed block is its data: what there is of it is
                 * kept, unless a checksum was to check it. */
                if (cut && uncompressed && !block_checksum && (size_t)(end - input) <= destination_size - output_position) {
                    while (input < end) output[output_position++] = *input++;
                }
                XX_LZ4_ENDED();
            }
            block_data = input;
            input += block_size;
            if (block_checksum) {
                if ((size_t)(end - input) < 4U) XX_LZ4_ENDED();
                if (xx_data_get_u32(input, 4, 0, false) != xx_lz4_xxh32(block_data, block_size)) {
                    return false;
                }
                input += 4;
            }

            if (uncompressed) {
                if (block_size > destination_size - output_position) return false;
                for (size_t index = 0; index < block_size; ++index) {
                    output[output_position + index] = block_data[index];
                }
                block_output = block_size;
            } else {
                uint8_t *block_destination;
                size_t block_capacity;
                if (independent_blocks) {
                    history_size = 0;
                    block_destination = output + output_position;
                    block_capacity = destination_size - output_position;
                } else {
                    history_size = output_position - frame_output_start;
                    block_destination = output + frame_output_start;
                    block_capacity = destination_size - frame_output_start;
                }
                if (!xx_lz4_decode_block(block_data, block_size, block_destination, block_capacity, history_size, &block_output) || block_output > block_limit) {
                    return false;
                }
            }
            /* Its checksum vouches for its data, decoded after the bytes
             * before it. */
            if (block_checksum && checked && *checked == output_position) *checked = output_position + block_output;
            output_position += block_output;
        }

        if (content_checksum) {
            if ((size_t)(end - input) < 4U) XX_LZ4_ENDED();
            if (xx_data_get_u32(input, 4, 0, false) != xx_lz4_xxh32(output + frame_output_start, output_position - frame_output_start)) {
                return false;
            }
            input += 4;
            if (checked && *checked >= frame_output_start) *checked = output_position;
        }
        if (has_content_size && (content_size > SIZE_MAX || (size_t)content_size != output_position - frame_output_start)) {
            return false;
        }
        saw_frame = true;
    }

#undef XX_LZ4_ENDED
    if (!saw_frame) return false;
    /* `exact` is the historical contract: the caller knew the output
     * length up front and a mismatch means corruption. When the length
     * is not knowable -- an LZ4 frame need not carry a content size, and
     * the lz4 CLI omits it -- the destination is a capacity instead and
     * the real length is reported back. */
    if (exact && output_position != destination_size) return false;
    if (out_written) *out_written = output_position;
    return true;
}

/* Native bounded LZ4 encoder. Matches have a 64 KiB history; frame blocks
 * are independent and <=4 MiB. No external codec dependency. */
#include "../xx_pack_memory_internal.h"
#include "xxfclib/rt/xx_rt.h"
#define XX_LZ4_ENC_BLOCK (4U * 1024U * 1024U)
static uint32_t xx_lz4_encode_hash(const uint8_t *p)
{
    return (xx_data_get_u32(p, 4, 0, false) * UINT32_C(2654435761)) >> 16;
}
static void xx_lz4_encode_length(uint8_t *out, size_t *position, size_t length)
{
    while (length >= 255U) {
        out[(*position)++] = 255;
        length -= 255U;
    }
    out[(*position)++] = (uint8_t)length;
}
bool xx_lz4_compress_block(const void *source, size_t size, void *destination, size_t capacity, size_t *written)
{
    static const uint8_t empty = 0;
    if (written) *written = 0;
    if ((!source && size) || !destination || size > UINT32_MAX - 1U || size > SIZE_MAX - 16U || size / 255U > SIZE_MAX - size - 16U) return false;
    const uint8_t *src = source ? source : &empty;
    uint8_t *out = destination;
    uint32_t *table = xx_mem_alloc(65536U * sizeof(*table));
    if (!table) return false;
    for (size_t i = 0; i < 65536U; i++) table[i] = UINT32_MAX;
    size_t position = 0, anchor = 0, output = 0;
    bool ok = true;
    while (position <= size && size - position >= 12U) {
        uint32_t hash = xx_lz4_encode_hash(src + position), candidate = table[hash];
        table[hash] = (uint32_t)position;
        if (candidate == UINT32_MAX || position - candidate > 65535U || xx_rt_memcmp(src + candidate, src + position, 4) != 0) {
            position++;
            continue;
        }
        size_t length = 4, limit = size - position - 5U;
        while (length < limit && src[candidate + length] == src[position + length]) length++;
        size_t literals = position - anchor, match = length - 4U;
        size_t need = 1U + literals + 2U + (literals >= 15U ? (literals - 15U) / 255U + 1U : 0U) + (match >= 15U ? (match - 15U) / 255U + 1U : 0U);
        if (need > capacity - output) {
            ok = false;
            break;
        }
        out[output++] = (uint8_t)(((literals < 15U ? literals : 15U) << 4) | (match < 15U ? match : 15U));
        if (literals >= 15U) xx_lz4_encode_length(out, &output, literals - 15U);
        if (literals) xx_rt_memcpy(out + output, src + anchor, literals);
        output += literals;
        size_t distance = position - candidate;
        out[output++] = (uint8_t)distance;
        out[output++] = (uint8_t)(distance >> 8);
        if (match >= 15U) xx_lz4_encode_length(out, &output, match - 15U);
        position += length;
        anchor = position;
        if (position >= 2U && size - position >= 2U) table[xx_lz4_encode_hash(src + position - 2U)] = (uint32_t)(position - 2U);
    }
    if (ok) {
        size_t literals = size - anchor, need = 1U + literals + (literals >= 15U ? (literals - 15U) / 255U + 1U : 0U);
        if (need > capacity - output) ok = false;
        else {
            out[output++] = (uint8_t)((literals < 15U ? literals : 15U) << 4);
            if (literals >= 15U) xx_lz4_encode_length(out, &output, literals - 15U);
            if (literals) xx_rt_memcpy(out + output, src + anchor, literals);
            output += literals;
        }
    }
    xx_mem_free(table);
    if (ok && written) *written = output;
    return ok;
}
size_t xx_lz4_compress_bound(size_t size)
{
    size_t blocks = size / XX_LZ4_ENC_BLOCK + (size % XX_LZ4_ENC_BLOCK != 0U);
    if (size > SIZE_MAX - 23U || blocks > (SIZE_MAX - size - 23U) / 4U) return 0U;
    return size + blocks * 4U + 23U;
}
bool xx_lz4_compress_memory(const void *source, size_t size, void *destination, size_t capacity, size_t *written)
{
    static const uint8_t empty = 0;
    if (written) *written = 0;
    if ((!source && size) || !destination || !xx_lz4_compress_bound(size) || capacity < 23U) return false;
    const uint8_t *src = source ? source : &empty;
    uint8_t *out = destination;
    size_t block_cap = XX_LZ4_ENC_BLOCK + XX_LZ4_ENC_BLOCK / 255U + 16U;
    uint8_t *block = size ? xx_mem_alloc(block_cap) : NULL;
    if (size && !block) return false;
    xx_data_set_u32(out, capacity, 0, XX_LZ4_FRAME_MAGIC, false);
    out[4] = 0x6c;
    out[5] = 0x70;
    xx_data_set_u64(out, capacity, 6, (uint64_t)size, false);
    out[14] = (uint8_t)(xx_lz4_xxh32(out + 4, 10) >> 8);
    size_t output = 15U, position = 0;
    bool ok = true;
    while (position < size) {
        size_t amount = size - position;
        if (amount > XX_LZ4_ENC_BLOCK) amount = XX_LZ4_ENC_BLOCK;
        size_t n = 0;
        if (!xx_lz4_compress_block(src + position, amount, block, block_cap, &n)) {
            ok = false;
            break;
        }
        bool compressed = n < amount;
        size_t payload = compressed ? n : amount;
        if (capacity - output < 12U || payload > capacity - output - 12U) {
            ok = false;
            break;
        }
        xx_data_set_u32(out, capacity, output, (uint32_t)payload | (compressed ? 0U : UINT32_C(0x80000000)), false);
        output += 4U;
        xx_rt_memcpy(out + output, compressed ? block : src + position, payload);
        output += payload;
        position += amount;
    }
    if (ok) {
        xx_data_set_u32(out, capacity, output, 0, false);
        output += 4U;
        xx_data_set_u32(out, capacity, output, xx_lz4_xxh32(src, size), false);
        output += 4U;
        if (written) *written = output;
    }
    xx_mem_free(block);
    return ok;
}
bool xx_lz4_pack_device(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    return xx_codec_pack_memory_device(source, offset, size, destination, pd, xx_lz4_compress_bound, xx_lz4_compress_memory);
}
