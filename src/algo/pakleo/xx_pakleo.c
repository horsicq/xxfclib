/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PAKLEO "LEOLZW" codec, ported from XArchive/Algos/xpakleodecoder.cpp.
 *
 * Deliberately kept from the reference, do not "fix":
 *
 *  - The code width moves ONLY when the explicit 0x101 WIDEN code appears in
 *    the stream.  It is never bumped implicitly when the free-code counter
 *    crosses a power of two.  A textbook LZW reader desynchronises at the
 *    first 512-code boundary; that is exactly why this codec cannot ride the
 *    shared LZW decoder.
 *  - The WIDEN codes are swallowed in an inner loop, so any number of them may
 *    stack up between two data codes, and the width saturates at 15.
 *  - A clear code (0x102) resets the free-code counter to 0x103 and the width
 *    to 9 but does NOT wipe the prefix/suffix table.  Entries above the free
 *    counter keep stale contents; they are unreachable because any code >=
 *    the free counter is taken as the KwKwK case instead.  Keep both halves of
 *    that pair together.
 *  - After a clear, the very next code is read at 9 bits and must be a plain
 *    literal (<= 0xff); 0x100, 0x102 or anything wider ends the stream.
 *  - The new table entry is added AFTER the phrase has been emitted, with
 *    prefix = the previous code and suffix = the FIRST byte of the phrase the
 *    current code expands to.
 *
 * The reference builds the plaintext in a growing QByteArray and only then
 * compares its length with the declared uncompressed size, so producing more
 * than that size always ends in failure.  This port therefore treats the first
 * byte that would exceed output_size as an immediate failure, which is
 * output-equivalent: the reference's length can only grow, so once it has
 * overshot, its final `size == nUncompressedSize` test can never succeed.
 *
 * One intentional difference: the reference refuses nUncompressedSize above
 * XPAKLEODecoder::MAX_UNCOMPRESSED_SIZE (0x40000000).  That is a container
 * sanity limit, not part of the codec, and this library caps decoded size
 * against the caller's output_size only.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/pakleo/xx_pakleo.h"

#define LEO_CAPACITY 0x8000
#define LEO_FIRST_FREE 0x103
#define LEO_CODE_EOF 0x100
#define LEO_CODE_WIDEN 0x101
#define LEO_CODE_CLEAR 0x102
#define LEO_MAX_WIDTH 15

typedef struct leo_bits {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint8_t current;
    int count;
} leo_bits;

/* Refill one byte, hand out bits MSB first.  -1 means the stream is exhausted;
 * bits already taken from the partial code are dropped, as in the reference. */
static int leo_get(leo_bits *reader, int width)
{
    int value = 0;
    int i;

    for (i = 0; i < width; ++i) {
        if (reader->count == 0) {
            if (reader->pos >= reader->size) return -1;
            reader->current = reader->data[reader->pos];
            reader->pos++;
            reader->count = 8;
        }
        value = (value << 1) | ((reader->current >> 7) & 1);
        reader->current = (uint8_t)((reader->current << 1) & 0xff);
        reader->count--;
    }

    return value;
}

bool xx_pakleo_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written)
{
    leo_bits reader;
    uint8_t *block;
    uint16_t *prefix;
    uint8_t *suffix;
    uint8_t *stack;
    size_t produced = 0U;
    size_t block_size;
    bool done = false;
    bool overflow = false;
    bool ok;
    int i;

    if (written) *written = 0U;
    if (!output) return false;
    if (!input && (input_size != 0U)) return false;

    reader.data = input;
    reader.size = input_size;
    reader.pos = 0U;
    reader.current = 0U;
    reader.count = 0;

    block_size = (size_t)LEO_CAPACITY * sizeof(uint16_t) +
                 (size_t)LEO_CAPACITY + (size_t)LEO_CAPACITY;
    block = (uint8_t *)xx_mem_alloc(block_size);
    if (!block) return false;
    xx_rt_memset(block, 0, block_size);

    prefix = (uint16_t *)(void *)block;
    suffix = block + (size_t)LEO_CAPACITY * sizeof(uint16_t);
    stack = suffix + (size_t)LEO_CAPACITY;

    for (i = 0; i < 256; ++i) suffix[i] = (uint8_t)i;

    while (!done) {
        int code;
        int first;
        int previous;
        int free_code;
        int width;
        bool restart = false;

        /* Entry point, and where a clear code comes back to. */
        code = leo_get(&reader, 9);
        if ((code < 0) || (code == LEO_CODE_EOF) || (code > 0xff)) break;

        if (produced >= output_size) {
            /* The reference appends this literal past the declared size and
             * then fails its final length test; fail now instead. */
            overflow = true;
            break;
        }
        output[produced] = (uint8_t)code;
        produced++;

        first = code;
        previous = code;
        free_code = LEO_FIRST_FREE;
        width = 9;

        while (!restart) {
            int current = -1;
            int stack_size = 0;
            int c;
            bool bad = false;

            for (;;) { /* swallow every 0x101 */
                if (produced >= output_size) {
                    done = true;
                    break;
                }
                current = leo_get(&reader, width);
                if ((current < 0) || (current == LEO_CODE_EOF)) {
                    done = true;
                    break;
                }
                if (current != LEO_CODE_WIDEN) break;
                if (width < LEO_MAX_WIDTH) ++width;
            }
            if (done) break;

            if (current == LEO_CODE_CLEAR) {
                restart = true;
                break;
            }

            c = current;
            if (current >= free_code) { /* KwKwK */
                stack[stack_size++] = (uint8_t)first;
                c = previous;
            }

            while (c > 0xff) {
                if ((c >= LEO_CAPACITY) || (stack_size >= LEO_CAPACITY)) {
                    bad = true;
                    break;
                }
                stack[stack_size++] = suffix[c];
                c = (int)prefix[c];
            }
            if (bad) {
                done = true;
                break;
            }
            if (stack_size >= LEO_CAPACITY) {
                done = true;
                break;
            }
            stack[stack_size++] = (uint8_t)c;
            first = c;

            for (i = stack_size - 1; i >= 0; --i) {
                if (produced >= output_size) {
                    /* The reference would append past the declared size and
                     * then fail its final length test; fail now instead. */
                    overflow = true;
                    break;
                }
                output[produced] = stack[i];
                produced++;
            }
            if (overflow) {
                done = true;
                break;
            }

            if (free_code < LEO_CAPACITY) {
                prefix[free_code] = (uint16_t)previous;
                suffix[free_code] = (uint8_t)first;
                ++free_code;
            }
            previous = current;
        }
    }

    xx_mem_free(block);

    ok = !overflow && (produced == output_size);
    if (ok && written) *written = produced;

    return ok;
}
