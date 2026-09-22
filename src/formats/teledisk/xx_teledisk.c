/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Sydex TeleDisk image.  A "TD" signature stores the
 * track stream verbatim; "td" compresses it, with blocked 12-bit LZW up to
 * version 19 and Okumura's LZHUF at versions 20 and 21.  The single member
 * is the flat sector image the track records rebuild.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/teledisk/xx_teledisk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef TELEDISK
#define XX_TELEDISK_FILE_TYPE XX_FILE_TYPE_TELEDISK
#else
#define XX_TELEDISK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define TELEDISK_MAX_MEMBERS 65536U
#define TELEDISK_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct teledisk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} teledisk_member;

typedef struct teledisk_stream_s {
    teledisk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} teledisk_stream;

static uint16_t teledisk_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t teledisk_le32(const uint8_t *b) {
    return (uint32_t)teledisk_le16(b) | ((uint32_t)teledisk_le16(b + 2U) << 16U);
}

static bool teledisk_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction.  The helper only has to be CRT free. */
static char *teledisk_make_name(const char *prefix, int a, int b,
                           const char *suffix) {
    char buffer[64];
    size_t used = 0U;
    size_t index;
    char *result;
    for (index = 0U; prefix && prefix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[index];
    }
    if (a >= 0) {
        char digits[8];
        size_t count = 0U;
        int value = a;
        do {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 2U) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    if (b >= 0) {
        if (used >= sizeof(buffer) - 2U) return NULL;
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + (b % 10));
    }
    for (index = 0U; suffix && suffix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[index];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void teledisk_stream_free(void *opaque) {
    teledisk_stream *stream = (teledisk_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool teledisk_add_member(teledisk_stream *stream, const teledisk_member *member) {
    teledisk_member *grown;
    if (!stream || !member || stream->count >= TELEDISK_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (teledisk_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define TELEDISK_HEADER_SIZE 12
#define TELEDISK_MAX_TRACKS 65536
#define TELEDISK_MAX_SECTOR_SIZE 0x4000

/* TeleDisk's own CRC16: polynomial 0xA097, MSB first, init 0, no reflection
 * and no final xor.  It authenticates the file header, every track header and
 * every sector's payload, which is what lets the reader tell a real image from
 * a file that merely starts with the right two letters. */
static uint16_t teledisk_crc16(const uint8_t *data, size_t size,
                               uint16_t seed) {
    uint16_t value = seed;
    size_t index;
    unsigned bit;
    for (index = 0U; index < size; ++index) {
        value ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            value = (uint16_t)((value & 0x8000U) ? ((value << 1U) ^ 0xa097U)
                                                 : (value << 1U));
    }
    return value;
}

typedef struct teledisk_buffer_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
} teledisk_buffer;

static bool teledisk_push(teledisk_buffer *buffer, uint8_t value) {
    if (buffer->size == buffer->capacity) {
        size_t wanted = buffer->capacity ? buffer->capacity * 2U : 0x10000U;
        uint8_t *grown;
        if (wanted > TELEDISK_MAX_OUTPUT) wanted = TELEDISK_MAX_OUTPUT;
        if (wanted <= buffer->capacity) return false;
        grown = (uint8_t *)xx_mem_realloc(buffer->data, wanted);
        if (!grown) return false;
        buffer->data = grown;
        buffer->capacity = wanted;
    }
    buffer->data[buffer->size++] = value;
    return true;
}

/* Signature "td", versions 10..19: blocked 12-bit LSB-first LZW. */
static bool teledisk_expand_lzw(const uint8_t *input, size_t input_size,
                                teledisk_buffer *output) {
    uint16_t *prefix = (uint16_t *)xx_mem_alloc(4096U * sizeof(uint16_t));
    uint8_t *suffix = (uint8_t *)xx_mem_alloc(4096U);
    uint8_t *stack = (uint8_t *)xx_mem_alloc(4096U);
    size_t position = 0U;
    uint32_t accumulator = 0U;
    int32_t bit_count = 0, left = 0, next = 0, last = 0, previous = 0;
    int32_t stack_pointer = 0;
    bool ok = true;
    if (!prefix || !suffix || !stack) ok = false;
    while (ok) {
        int32_t code = 0;
        int32_t walk;
        if (left < 1) {
            if (input_size - position < 2U) break;
            left = (int32_t)input[position] |
                   ((int32_t)input[position + 1U] << 8);
            position += 2U;
            if (left > 0x3000) {
                ok = false;
                break;
            }
            for (code = 0; code < 256; ++code) {
                prefix[code] = 0U;
                suffix[code] = (uint8_t)code;
            }
            next = 0x100;
            while (bit_count <= 11) {
                if (position >= input_size) break;
                accumulator |= ((uint32_t)input[position++]) << bit_count;
                bit_count += 8;
            }
            if (bit_count <= 11) break;
            code = (int32_t)(accumulator & 0xfffU);
            accumulator >>= 12;
            bit_count -= 12;
            left -= 3;
            previous = code;
            last = code;
            stack_pointer = 0;
            if (!teledisk_push(output, (uint8_t)(code & 0xff))) {
                ok = false;
                break;
            }
            continue;
        }
        while (bit_count <= 11) {
            if (position >= input_size) break;
            accumulator |= ((uint32_t)input[position++]) << bit_count;
            bit_count += 8;
        }
        if (bit_count <= 11) break;
        code = (int32_t)(accumulator & 0xfffU);
        accumulator >>= 12;
        bit_count -= 12;
        left -= 3;
        walk = code;
        if (walk >= next) {
            if (stack_pointer > 0xfff) {
                ok = false;
                break;
            }
            stack[stack_pointer++] = (uint8_t)(last & 0xff);
            walk = previous;
        }
        while (walk > 0xff) {
            if (stack_pointer > 0xfff || walk > 0xfff) {
                ok = false;
                break;
            }
            stack[stack_pointer++] = suffix[walk];
            walk = (int32_t)prefix[walk];
        }
        if (!ok) break;
        if (walk < 0 || stack_pointer > 0xfff) {
            ok = false;
            break;
        }
        last = walk;
        stack[stack_pointer++] = (uint8_t)(walk & 0xff);
        while (stack_pointer > 0) {
            if (!teledisk_push(output, stack[--stack_pointer])) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
        if (next < 4096) {
            prefix[next] = (uint16_t)(previous & 0xffff);
            suffix[next] = (uint8_t)(last & 0xff);
            ++next;
        }
        previous = code;
    }
    if (prefix) xx_mem_free(prefix);
    if (suffix) xx_mem_free(suffix);
    if (stack) xx_mem_free(stack);
    return ok && output->size != 0U;
}

/* Signature "td", versions 20 and 21: Okumura's LZHUF - LZSS over an adaptive
 * Huffman tree - across the whole archive as ONE stream. */
#define TD_N 4096
#define TD_F 60
#define TD_N_CHAR (256 - 2 + TD_F)
#define TD_T (TD_N_CHAR * 2 - 1)
#define TD_R (TD_T - 1)
#define TD_MAX_FREQ 0x8000

typedef struct teledisk_lzhuf_s {
    const uint8_t *input;
    size_t input_size;
    size_t position;
    uint8_t bit_buffer;
    int32_t bit_count;
    bool eof;
    int32_t freq[TD_T + 1];
    int32_t parent[TD_T + TD_N_CHAR];
    int32_t son[TD_T];
    uint8_t d_code[256];
    uint8_t d_len[256];
    uint8_t text[TD_N];
    int32_t ring;
} teledisk_lzhuf;

static void teledisk_lzhuf_init(teledisk_lzhuf *state, const uint8_t *input,
                                size_t input_size) {
    static const int32_t counts[6] = {1, 3, 8, 12, 24, 16};
    int32_t index = 0, symbol = 0, length, i, j, k;
    xx_mem_zero(state, sizeof(*state));
    state->input = input;
    state->input_size = input_size;
    state->ring = TD_N - TD_F;
    for (length = 3; length <= 8; ++length) {
        const int32_t span = 1 << (8 - length);
        for (i = 0; i < counts[length - 3]; ++i) {
            for (j = 0; j < span; ++j) {
                if (index >= 256) break;
                state->d_code[index] = (uint8_t)symbol;
                state->d_len[index] = (uint8_t)length;
                ++index;
            }
            ++symbol;
        }
    }
    for (i = 0; i < TD_N_CHAR; ++i) {
        state->freq[i] = 1;
        state->son[i] = i + TD_T;
        state->parent[i + TD_T] = i;
    }
    i = 0;
    k = TD_N_CHAR;
    while (k <= TD_R) {
        state->freq[k] = state->freq[i] + state->freq[i + 1];
        state->son[k] = i;
        state->parent[i] = k;
        state->parent[i + 1] = k;
        i += 2;
        ++k;
    }
    state->freq[TD_T] = 0xffff;
    state->parent[TD_R] = 0;
    xx_rt_memset(state->text, 0x20, sizeof(state->text));
}

static void teledisk_lzhuf_reconst(teledisk_lzhuf *state) {
    int32_t i, j = 0, k, l, m;
    for (i = 0; i < TD_T; ++i) {
        if (state->son[i] >= TD_T) {
            state->freq[j] = (state->freq[i] + 1) / 2;
            state->son[j] = state->son[i];
            ++j;
        }
    }
    i = 0;
    for (k = TD_N_CHAR; k < TD_T; ++k) {
        const int32_t f = state->freq[i] + state->freq[i + 1];
        state->freq[k] = f;
        l = k - 1;
        while (l >= 0 && f < state->freq[l]) --l;
        ++l;
        for (m = k; m > l; --m) {
            state->freq[m] = state->freq[m - 1];
            state->son[m] = state->son[m - 1];
        }
        state->freq[l] = f;
        state->son[l] = i;
        i += 2;
    }
    for (i = 0; i < TD_T; ++i) {
        k = state->son[i];
        state->parent[k] = i;
        if (k < TD_T) state->parent[k + 1] = i;
    }
}

static void teledisk_lzhuf_update(teledisk_lzhuf *state, int32_t c) {
    if (state->freq[TD_R] == TD_MAX_FREQ) teledisk_lzhuf_reconst(state);
    c = state->parent[c + TD_T];
    do {
        int32_t k, l, i, j;
        state->freq[c]++;
        k = state->freq[c];
        l = c + 1;
        if (l < TD_T && k > state->freq[l]) {
            while (l + 1 < TD_T && k > state->freq[l + 1]) ++l;
            state->freq[c] = state->freq[l];
            state->freq[l] = k;
            i = state->son[c];
            state->parent[i] = l;
            if (i < TD_T) state->parent[i + 1] = l;
            j = state->son[l];
            state->son[l] = i;
            state->parent[j] = c;
            if (j < TD_T) state->parent[j + 1] = c;
            state->son[c] = j;
            c = l;
        }
        c = state->parent[c];
    } while (c != 0);
}

static int32_t teledisk_lzhuf_bit(teledisk_lzhuf *state) {
    if (state->bit_count == 0) {
        if (state->position >= state->input_size) {
            state->eof = true;
            return 0;
        }
        state->bit_buffer = state->input[state->position++];
        state->bit_count = 8;
    }
    state->bit_count--;
    return (state->bit_buffer >> state->bit_count) & 1;
}

static bool teledisk_expand_lzhuf(const uint8_t *input, size_t input_size,
                                  teledisk_buffer *output) {
    teledisk_lzhuf *state =
        (teledisk_lzhuf *)xx_mem_alloc(sizeof(teledisk_lzhuf));
    bool ok = true;
    if (!state) return false;
    teledisk_lzhuf_init(state, input, input_size);
    for (;;) {
        int32_t c = state->son[TD_R];
        int32_t byte_index = 0, high, extra, value, position, source, length, k;
        while (c < TD_T) {
            c += teledisk_lzhuf_bit(state);
            if (state->eof || c < 0 || c >= TD_T) goto done;
            c = state->son[c];
        }
        c -= TD_T;
        if (c < 0 || c >= TD_N_CHAR) goto done;
        teledisk_lzhuf_update(state, c);
        if (c < 256) {
            state->text[state->ring] = (uint8_t)c;
            state->ring = (state->ring + 1) & (TD_N - 1);
            if (!teledisk_push(output, (uint8_t)c)) {
                ok = false;
                goto done;
            }
            continue;
        }
        for (k = 0; k < 8; ++k) {
            byte_index = (byte_index << 1) | teledisk_lzhuf_bit(state);
            if (state->eof) goto done;
        }
        high = ((int32_t)state->d_code[byte_index]) << 6;
        extra = (int32_t)state->d_len[byte_index] - 2;
        value = byte_index;
        while (extra > 0) {
            --extra;
            value = ((value << 1) | teledisk_lzhuf_bit(state)) & 0xffff;
            if (state->eof) goto done;
        }
        position = high | (value & 0x3f);
        source = (state->ring - position - 1) & (TD_N - 1);
        length = c - 253;
        for (k = 0; k < length; ++k) {
            const uint8_t byte = state->text[source];
            source = (source + 1) & (TD_N - 1);
            state->text[state->ring] = byte;
            state->ring = (state->ring + 1) & (TD_N - 1);
            if (!teledisk_push(output, byte)) {
                ok = false;
                goto done;
            }
        }
    }
done:
    xx_mem_free(state);
    return ok && output->size != 0U;
}

/* Sector payload codings 0..2; a run must land exactly on the sector size. */
static bool teledisk_unpack_sector(const uint8_t *source, size_t source_size,
                                   uint8_t method, size_t sector_size,
                                   uint8_t *output) {
    size_t left = sector_size;
    size_t index = 0U;
    if (method == 0U) {
        if (source_size != sector_size) return false;
        if (sector_size != 0U) xx_mem_copy(output, source, sector_size);
        return true;
    }
    if (method == 1U) {
        if (source_size & 3U) return false;
        while (index < source_size) {
            size_t repeat = (size_t)source[index] |
                            ((size_t)source[index + 1U] << 8U);
            uint8_t a = source[index + 2U];
            uint8_t b = source[index + 3U];
            size_t step;
            index += 4U;
            if (repeat > left / 2U) return false;
            for (step = 0U; step < repeat; ++step) {
                output[sector_size - left] = a;
                output[sector_size - left + 1U] = b;
                left -= 2U;
            }
        }
        return left == 0U;
    }
    if (method == 2U) {
        size_t remaining = source_size;
        while (remaining > 0U) {
            size_t code, repeat, block, step;
            if (remaining < 2U) return false;
            code = source[index];
            repeat = source[index + 1U];
            if (code == 0U) {
                if (remaining - 2U < repeat || left < repeat) return false;
                if (repeat != 0U)
                    xx_mem_copy(output + (sector_size - left),
                                source + index + 2U, repeat);
                left -= repeat;
                index += 2U + repeat;
                remaining -= 2U + repeat;
            } else {
                block = code * 2U;
                if (remaining - 2U < block) return false;
                if (block != 0U && repeat > left / block) return false;
                for (step = 0U; step < repeat; ++step) {
                    xx_mem_copy(output + (sector_size - left),
                                source + index + 2U, block);
                    left -= block;
                }
                index += 2U + block;
                remaining -= 2U + block;
            }
        }
        return left == 0U;
    }
    return false;
}

/* Walk the decompressed stream: an optional comment block, then one track
 * header per physical track and one descriptor plus payload per sector.  A
 * sector count of 0xFF terminates.  Sectors go out in file order, padded to at
 * least 512 bytes, which is what the reference extractor produces. */
static bool teledisk_build(const uint8_t *plain, size_t plain_size,
                           bool has_comment, uint8_t *output,
                           size_t output_size, size_t *produced) {
    size_t position = 0U;
    size_t total = 0U;
    uint32_t tracks = 0U;
    uint8_t scratch[TELEDISK_MAX_SECTOR_SIZE];
    if (has_comment) {
        size_t comment_length;
        uint16_t stored, running;
        if (plain_size - position < 10U) return false;
        stored = teledisk_le16(plain + position);
        comment_length = teledisk_le16(plain + position + 2U);
        running = teledisk_crc16(plain + position + 2U, 8U, 0U);
        position += 10U;
        if (plain_size - position < comment_length) return false;
        if (teledisk_crc16(plain + position, comment_length, running) != stored)
            return false;
        position += comment_length;
    }
    while (tracks < TELEDISK_MAX_TRACKS) {
        uint8_t sectors;
        uint32_t index;
        ++tracks;
        if (plain_size - position < 4U) break;
        sectors = plain[position];
        if (sectors == 0xffU) break;
        if ((uint8_t)(teledisk_crc16(plain + position, 3U, 0U) & 0xffU) !=
            plain[position + 3U])
            break;
        position += 4U;
        if (sectors == 0U) continue;
        for (index = 0U; index < sectors; ++index) {
            uint8_t sector_number, size_code, flags, calculated;
            size_t sector_size, write_size;
            size_t descriptor = position;
            if (plain_size - position < 6U) return false;
            sector_number = plain[position + 2U];
            size_code = plain[position + 3U];
            flags = plain[position + 4U];
            if (size_code > 7U) return false;
            calculated = (uint8_t)(teledisk_crc16(plain + position, 5U, 0U) &
                                   0xffU);
            sector_size = (size_t)0x80U << size_code;
            xx_mem_zero(scratch, sizeof(scratch));
            position += 6U;
            if ((flags & 0x30U) == 0U) {
                size_t block_length;
                uint8_t method;
                if (plain_size - position < 3U) return false;
                block_length = teledisk_le16(plain + position);
                if (block_length == 0U) return false;
                --block_length;
                method = plain[position + 2U];
                position += 3U;
                if (plain_size - position < block_length) return false;
                if (!teledisk_unpack_sector(plain + position, block_length,
                                            method, sector_size, scratch))
                    return false;
                position += block_length;
                calculated = (uint8_t)(teledisk_crc16(scratch, sector_size,
                                                      0U) & 0xffU);
            }
            write_size = sector_size >= 0x200U ? sector_size : 0x200U;
            if (!(sectors == 0x13U && sector_number == 0x76U &&
                  (flags & 0x40U) != 0U)) {
                if (write_size > TELEDISK_MAX_OUTPUT - total) return false;
                if (output) {
                    if (total + write_size > output_size) return false;
                    xx_mem_copy(output + total, scratch, write_size);
                }
                total += write_size;
            }
            if (plain[descriptor + 5U] != calculated) return false;
        }
    }
    if (produced) *produced = total;
    return total != 0U;
}

static bool teledisk_expand(Abstractformat *format, int64_t base, int64_t size,
                            teledisk_buffer *plain, bool *has_comment) {
    uint8_t *raw = NULL;
    bool compressed, advanced, ok = false;
    uint8_t version;
    if (size < (int64_t)TELEDISK_HEADER_SIZE ||
        (uint64_t)size > TELEDISK_MAX_OUTPUT)
        return false;
    raw = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!raw) return false;
    if (!teledisk_read_at(format->device, base, raw, (size_t)size)) goto done;
    compressed = raw[0] == 't' && raw[1] == 'd';
    if (!compressed && !(raw[0] == 'T' && raw[1] == 'D')) goto done;
    version = raw[4];
    if (version < 10U || version > 21U || (raw[5] & 0x7fU) > 2U ||
        raw[6] > 6U || (raw[9] != 1U && raw[9] != 2U) ||
        teledisk_le16(raw + 10U) != teledisk_crc16(raw, 10U, 0U))
        goto done;
    *has_comment = (raw[7] & 0x80U) != 0U;
    advanced = compressed && version >= 20U;
    if (!compressed) {
        plain->data = (uint8_t *)xx_mem_alloc((size_t)size -
                                              TELEDISK_HEADER_SIZE + 1U);
        if (!plain->data) goto done;
        plain->size = (size_t)size - TELEDISK_HEADER_SIZE;
        plain->capacity = plain->size + 1U;
        if (plain->size != 0U)
            xx_mem_copy(plain->data, raw + TELEDISK_HEADER_SIZE, plain->size);
        ok = plain->size != 0U;
    } else if (advanced) {
        ok = teledisk_expand_lzhuf(raw + TELEDISK_HEADER_SIZE,
                                   (size_t)size - TELEDISK_HEADER_SIZE, plain);
    } else {
        ok = teledisk_expand_lzw(raw + TELEDISK_HEADER_SIZE,
                                 (size_t)size - TELEDISK_HEADER_SIZE, plain);
    }
done:
    xx_mem_free(raw);
    return ok;
}

static bool teledisk_parse(Abstractformat *format, teledisk_stream **result) {
    teledisk_buffer plain;
    teledisk_stream *stream;
    teledisk_member member;
    int64_t total, size;
    size_t measured = 0U;
    bool has_comment = false;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    xx_mem_zero(&plain, sizeof(plain));
    if (!teledisk_expand(format, format->base_address, size, &plain,
                         &has_comment)) {
        if (plain.data) xx_mem_free(plain.data);
        return false;
    }
    if (!teledisk_build(plain.data, plain.size, has_comment, NULL, 0U,
                        &measured) ||
        measured == 0U || measured > TELEDISK_MAX_OUTPUT) {
        xx_mem_free(plain.data);
        return false;
    }
    xx_mem_free(plain.data);
    stream = (teledisk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = teledisk_make_name("image", -1, -1, ".img");
    member.header_offset = format->base_address;
    member.header_size = TELEDISK_HEADER_SIZE;
    member.data_offset = format->base_address + TELEDISK_HEADER_SIZE;
    member.packed_size = size - TELEDISK_HEADER_SIZE;
    member.unpacked_size = (uint64_t)measured;
    member.method = 1U;
    member.decode = true;
    if (!member.name || !teledisk_add_member(stream, &member)) {
        if (member.name) xx_mem_free(member.name);
        teledisk_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
}

static bool teledisk_decode(Abstractformat *format,
                            const teledisk_member *member, uint8_t **plain_out,
                            size_t *plain_size) {
    teledisk_buffer plain;
    uint8_t *output;
    size_t produced = 0U;
    bool has_comment = false;
    if (member->unpacked_size == 0U ||
        member->unpacked_size > TELEDISK_MAX_OUTPUT)
        return false;
    xx_mem_zero(&plain, sizeof(plain));
    if (!teledisk_expand(format, member->header_offset,
                         member->packed_size + TELEDISK_HEADER_SIZE, &plain,
                         &has_comment)) {
        if (plain.data) xx_mem_free(plain.data);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!output ||
        !teledisk_build(plain.data, plain.size, has_comment, output,
                        (size_t)member->unpacked_size, &produced) ||
        produced != (size_t)member->unpacked_size) {
        if (output) xx_mem_free(output);
        xx_mem_free(plain.data);
        return false;
    }
    xx_mem_free(plain.data);
    *plain_out = output;
    *plain_size = produced;
    return true;
}

static bool teledisk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *teledisk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool teledisk_set_record(xx_archive_record *record,
                           const teledisk_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Stored members are copied verbatim; everything else goes to the format
 * codec above, which is the only place a size can grow. */
static bool teledisk_extract(Abstractformat *format, const teledisk_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return teledisk_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > TELEDISK_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !teledisk_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_teledisk_init(xx_teledisk *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TELEDISK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-teledisk");
    xx_format_set_extension(&archive->format, "td0");
    archive->format.check_is_valid = xx_teledisk_check_is_valid;
    archive->format.handle_base_info = xx_teledisk_handle_base_info;
    archive->format.get_format_size = xx_teledisk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_teledisk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_teledisk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_teledisk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_teledisk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_teledisk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_teledisk_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_teledisk *xx_teledisk_create(xx_io_device *device, int64_t base_address) {
    xx_teledisk *archive = (xx_teledisk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_teledisk_init(archive, device, base_address);
    return archive;
}

void xx_teledisk_destroy(xx_teledisk *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_teledisk_free(xx_teledisk *archive) {
    if (!archive) return;
    xx_teledisk_destroy(archive);
    xx_mem_free(archive);
}

bool xx_teledisk_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    teledisk_stream *stream;
    (void)pd;
    if (!teledisk_parse(format, &stream)) return false;
    teledisk_stream_free(stream);
    return true;
}

bool xx_teledisk_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    teledisk_stream *stream;
    xx_teledisk *archive;
    (void)pd;
    if (!format || !teledisk_parse(format, &stream)) return false;
    archive = (xx_teledisk *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    teledisk_stream_free(stream);
    return true;
}

int64_t xx_teledisk_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_teledisk_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_teledisk_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_teledisk_handle_base_info(format, pd))
               ? ((xx_teledisk *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_teledisk_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    teledisk_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!teledisk_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        teledisk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = teledisk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!teledisk_copy_options(&state->options, options) ||
        !teledisk_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_teledisk_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_teledisk_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    teledisk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (teledisk_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = teledisk_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_teledisk_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    teledisk_stream *stream;
    teledisk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (teledisk_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!teledisk_extract(format, member, &plain, &plain_size)) goto done;
    path_option = teledisk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_teledisk_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
