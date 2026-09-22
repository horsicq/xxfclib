/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Two LZW dialects that the existing xxfclib LZW entry points cannot express:
 *
 *   K-BOOM  - a trie-with-recycling LZW on fixed 13-bit codes, ported from
 *             XArchive Algos/xkboomdecoder.cpp.
 *   NewWave - the "LZWD" member codec, ported from XArchive
 *             Algos/xnewwavelzwdecoder.cpp (which is what
 *             HANDLE_METHOD_LZWD_LZW actually dispatches to).
 *
 * Neither container is length-less, so there is no scan entry point here.
 * See the header for the per-codec faithfulness notes.
 */

#include "xxfclib/algo/lzwvariants/xx_lzwvariants.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

/* ------------------------------------------------------------------ K-BOOM */

#define KBOOM_NIL 0x1fffU
#define KBOOM_CAPACITY 0x2000U
#define KBOOM_LAST_SLOT 0x1ffeU
#define KBOOM_FIRST_SLOT 0x0100U
#define KBOOM_SCRATCH_TOP 100U  /* phrases are written down from here */
#define KBOOM_MAX_PHRASE 100U
#define KBOOM_MAX_EXTEND 4U     /* trie bytes added per token */
#define KBOOM_SCRATCH_SIZE 256U
#define KBOOM_WALK_GUARD 0x4000U

typedef struct kboom_reader_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t accumulator;
    uint32_t count;
} kboom_reader;

/* 13-bit codes off the TOP of a 32-bit accumulator, refilled a little-endian
 * 16-bit word at a time.  The word is ADDED, not OR-ed: that is what the
 * reference does and the two differ if the accumulator's low bits are ever
 * non-zero, so it is kept verbatim. */
static bool kboom_reader_next(kboom_reader *reader, uint32_t *code) {
    uint32_t word;

    if (!reader || !code) return false;
    if (reader->count < 13U) {
        if ((reader->position + 2U) > reader->size) return false;
        word = (uint32_t)reader->data[reader->position] |
               ((uint32_t)reader->data[reader->position + 1U] << 8U);
        reader->position += 2U;
        /* count <= 12 here, so the shift is 4..16 and always well defined. */
        reader->accumulator =
            (uint32_t)(reader->accumulator + (word << (16U - reader->count)));
        reader->count += 16U;
    }
    *code = (reader->accumulator >> 19U) & 0x1fffU;
    reader->accumulator = (uint32_t)(reader->accumulator << 13U);
    reader->count -= 13U;
    return true;
}

typedef struct kboom_trie_s {
    uint16_t *symbol;
    uint16_t *child;
    uint16_t *parent;
    uint16_t *next;
    uint16_t *previous;
} kboom_trie;

/* Unlink a recycled slot from its parent's sibling list.  Verbatim from the
 * reference, including the "no parent, no head fix-up" case. */
static bool kboom_unlink(kboom_trie *trie, uint32_t node) {
    uint16_t after;
    uint16_t before;

    if (node >= KBOOM_CAPACITY) return false;
    after = trie->next[node];
    before = trie->previous[node];
    if (before == (uint16_t)KBOOM_NIL) {
        if (trie->parent[node] != (uint16_t)KBOOM_NIL) {
            if (trie->parent[node] >= (uint16_t)KBOOM_CAPACITY) return false;
            trie->child[trie->parent[node]] = after;
        }
    } else {
        if (before >= (uint16_t)KBOOM_CAPACITY) return false;
        trie->next[before] = after;
    }
    if (after != (uint16_t)KBOOM_NIL) {
        if (after >= (uint16_t)KBOOM_CAPACITY) return false;
        trie->previous[after] = before;
    }
    return true;
}

bool xx_lzwvariants_kboom_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written) {
    kboom_reader reader;
    kboom_trie trie;
    uint16_t *block = NULL;
    uint8_t scratch[KBOOM_SCRATCH_SIZE];
    size_t produced = 0U;
    uint32_t free_slot = KBOOM_FIRST_SLOT;
    uint32_t cursor = KBOOM_FIRST_SLOT;
    uint32_t previous_code = KBOOM_NIL;
    uint32_t previous_length = 0U;
    uint32_t index;
    bool result = false;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    if (output_size == 0U) return true;

    block = (uint16_t *)xx_mem_alloc((size_t)KBOOM_CAPACITY * 5U *
                                     sizeof(uint16_t));
    if (!block) return false;
    trie.symbol = block;
    trie.child = block + KBOOM_CAPACITY;
    trie.parent = block + (size_t)KBOOM_CAPACITY * 2U;
    trie.next = block + (size_t)KBOOM_CAPACITY * 3U;
    trie.previous = block + (size_t)KBOOM_CAPACITY * 4U;
    for (index = 0U; index < KBOOM_CAPACITY * 5U; ++index)
        block[index] = (uint16_t)KBOOM_NIL;
    for (index = 0U; index < 256U; ++index) trie.symbol[index] = (uint16_t)index;
    /* xx_rt_memset, not a zeroing loop: an optimiser is allowed to turn the
     * loop into a CRT memset call, which the freestanding build cannot link. */
    xx_rt_memset(scratch, 0, sizeof(scratch));

    reader.data = input;
    reader.size = input_size;
    reader.position = 0U;
    reader.accumulator = 0U;
    reader.count = 0U;

    while (produced < output_size) {
        uint32_t code = 0U;
        uint32_t length = 0U;
        uint32_t node;
        uint32_t guard = KBOOM_WALK_GUARD;
        uint32_t start;
        uint32_t emit;

        if (!kboom_reader_next(&reader, &code)) break;

        /* Walk the trie to the root, laying the phrase down backwards from
         * scratch[KBOOM_SCRATCH_TOP]. */
        node = code;
        while (node != KBOOM_NIL) {
            if ((node >= KBOOM_CAPACITY) || (length > KBOOM_MAX_PHRASE) ||
                (--guard == 0U))
                goto done;
            scratch[KBOOM_SCRATCH_TOP - length] = (uint8_t)trie.symbol[node];
            ++length;
            node = trie.parent[node];
        }
        start = (KBOOM_SCRATCH_TOP + 1U) - length;

        /* The reference appends the whole phrase and truncates afterwards.
         * Dropping the overrun bytes here is the same thing: the trie update
         * below reads from scratch, never from the output. */
        for (emit = 0U; emit < length; ++emit) {
            if (produced >= output_size) break;
            output[produced++] = scratch[start + emit];
        }

        if (previous_code != KBOOM_NIL) {
            bool must_allocate = false;
            uint32_t current = previous_code;
            uint32_t step;

            for (step = 0U;
                 (step < length) && (step < KBOOM_MAX_EXTEND) &&
                 ((previous_length + step) < KBOOM_MAX_PHRASE);
                 ++step) {
                uint8_t value;
                uint32_t fresh;
                uint16_t head;

                if ((start + step) > 0xffU) goto done;
                value = scratch[start + step];

                if (!must_allocate) {
                    uint32_t match = trie.child[current];
                    uint32_t walk = KBOOM_CAPACITY;
                    while (match != KBOOM_NIL) {
                        if ((match >= KBOOM_CAPACITY) || (walk-- == 0U))
                            goto done;
                        if (trie.symbol[match] == value) break;
                        match = trie.next[match];
                    }
                    if (match != KBOOM_NIL) {
                        current = match;
                        continue;
                    }
                    must_allocate = true;
                }

                if (free_slot > KBOOM_LAST_SLOT) {
                    /* Table full: sweep for a childless node to reuse,
                     * skipping the node currently being extended. */
                    uint32_t sweep = 0U;
                    for (;;) {
                        if (cursor > KBOOM_LAST_SLOT) cursor = KBOOM_FIRST_SLOT;
                        if ((cursor != current) &&
                            (trie.child[cursor] == (uint16_t)KBOOM_NIL))
                            break;
                        ++cursor;
                        if (++sweep > KBOOM_CAPACITY) goto done;
                    }
                    fresh = cursor;
                    cursor = fresh + 1U;
                    if (!kboom_unlink(&trie, fresh)) goto done;
                } else {
                    fresh = free_slot;
                    ++free_slot;
                }

                if ((fresh >= KBOOM_CAPACITY) || (current >= KBOOM_CAPACITY))
                    goto done;
                trie.symbol[fresh] = value;
                trie.parent[fresh] = (uint16_t)current;
                trie.child[fresh] = (uint16_t)KBOOM_NIL;
                trie.previous[fresh] = (uint16_t)KBOOM_NIL;
                head = trie.child[current];
                trie.next[fresh] = head;
                if (head != (uint16_t)KBOOM_NIL) {
                    if (head >= (uint16_t)KBOOM_CAPACITY) goto done;
                    trie.previous[head] = (uint16_t)fresh;
                }
                trie.child[current] = (uint16_t)fresh;
                current = fresh;
            }
        }

        previous_code = code;
        previous_length = length;
    }

    result = (produced == output_size);
done:
    xx_mem_free(block);
    if (!result) return false;
    if (written) *written = produced;
    return true;
}

/* ----------------------------------------------------------------- NewWave */

#define NEWWAVE_CLEAR 0x100U
#define NEWWAVE_END 0x101U
#define NEWWAVE_FIRST_SLOT 0x102U
#define NEWWAVE_MAX_BITS 12U
#define NEWWAVE_CAPACITY (1U << NEWWAVE_MAX_BITS) /* 0x1000 */
/* Worst-case phrase: one KwKwK byte + the guard's 0x1000 walk steps + the
 * root byte. */
#define NEWWAVE_STACK_SIZE (NEWWAVE_CAPACITY + 4U)

typedef struct newwave_reader_s {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_position;
    uint32_t width;
} newwave_reader;

/* MSB-first.  A code that merely RUNS PAST the final byte is still readable -
 * the encoder pads the tail with zero bits - but one that STARTS past it is
 * not.  Deliberate; do not tighten. */
static bool newwave_reader_next(newwave_reader *reader, uint32_t *code) {
    uint32_t value = 0U;
    uint32_t index;

    if (!reader || !code) return false;
    if (reader->bit_position >= reader->bit_size) return false;
    for (index = 0U; index < reader->width; ++index) {
        size_t bit = reader->bit_position + (size_t)index;
        uint32_t set = 0U;
        if (bit < reader->bit_size)
            set = (uint32_t)((reader->data[bit >> 3U] >> (7U - (bit & 7U))) &
                             1U);
        value = (value << 1U) | set;
    }
    reader->bit_position += reader->width;
    *code = value;
    return true;
}

bool xx_lzwvariants_newwave_decode_memory(const uint8_t *input,
                                          size_t input_size, uint8_t *output,
                                          size_t output_size,
                                          size_t *written) {
    newwave_reader reader;
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL;
    uint8_t *stack = NULL;
    size_t produced = 0U;
    uint32_t free_slot = NEWWAVE_FIRST_SLOT;
    int32_t previous = -1;
    uint8_t previous_first = 0U;
    uint32_t index;
    bool result = false;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    if (input_size == 0U) return output_size == 0U;
    if (output_size == 0U) return true;
    if (input_size > SIZE_MAX / 8U) return false;

    prefix = (uint16_t *)xx_mem_alloc((size_t)NEWWAVE_CAPACITY *
                                      sizeof(uint16_t));
    suffix = (uint8_t *)xx_mem_alloc((size_t)NEWWAVE_CAPACITY);
    stack = (uint8_t *)xx_mem_alloc((size_t)NEWWAVE_STACK_SIZE);
    if (!prefix || !suffix || !stack) goto done;
    /* xx_rt_memset, not zeroing loops: an optimiser is allowed to turn those
     * into CRT memset calls, which the freestanding build cannot link. */
    xx_rt_memset(prefix, 0, (size_t)NEWWAVE_CAPACITY * sizeof(uint16_t));
    xx_rt_memset(suffix, 0, (size_t)NEWWAVE_CAPACITY);
    for (index = 0U; index < 256U; ++index) suffix[index] = (uint8_t)index;

    reader.data = input;
    reader.bit_size = input_size * 8U;
    reader.bit_position = 0U;
    reader.width = 9U;

    while (produced < output_size) {
        uint32_t code = 0U;
        uint32_t current;
        size_t depth = 0U;
        uint32_t guard = 0U;

        if (!newwave_reader_next(&reader, &code)) break;
        if (code == NEWWAVE_END) break;
        if (code == NEWWAVE_CLEAR) {
            free_slot = NEWWAVE_FIRST_SLOT;
            reader.width = 9U;
            previous = -1;
            /* Deliberate: an explicit CLEAR skips the width test below. */
            continue;
        }

        if (previous < 0) {
            /* The first code of a group is always a literal: the table is
             * empty. */
            if (code > 0xffU) goto done;
            if (produced >= output_size) goto done;
            output[produced++] = (uint8_t)code;
            previous = (int32_t)code;
            previous_first = (uint8_t)code;
        } else {
            current = code;
            if (code >= free_slot) {
                /* KwKwK: only the one slot about to be created may be
                 * named. */
                if (code > free_slot) goto done;
                stack[depth++] = previous_first;
                current = (uint32_t)previous;
            }
            while (current > 0xffU) {
                if ((current >= NEWWAVE_CAPACITY) ||
                    (++guard > NEWWAVE_CAPACITY) ||
                    (depth >= NEWWAVE_STACK_SIZE))
                    goto done;
                stack[depth++] = suffix[current];
                current = prefix[current];
            }
            if (depth >= NEWWAVE_STACK_SIZE) goto done;
            stack[depth++] = (uint8_t)current;

            /* Unlike K-BOOM, an overrunning phrase is fatal here: the
             * reference keeps the overrun and then fails its exact-size
             * test, so the outcome is the same. */
            if (depth > (output_size - produced)) goto done;
            while (depth != 0U) output[produced++] = stack[--depth];

            if (free_slot < NEWWAVE_CAPACITY) {
                prefix[free_slot] = (uint16_t)previous;
                suffix[free_slot] = (uint8_t)current;
                ++free_slot;
            }
            previous = (int32_t)code;
            previous_first = (uint8_t)current;
        }

        /* Early width change - and, at the top width, the self-restart that
         * takes the place of a thirteenth bit.  The explicit CLEAR the
         * encoder writes next is then read at nine bits and does nothing.
         * Deliberate: a table-full stall here desynchronises the stream. */
        if ((free_slot + 1U) >= (1U << reader.width)) {
            if (reader.width < NEWWAVE_MAX_BITS) {
                ++reader.width;
            } else {
                free_slot = NEWWAVE_FIRST_SLOT;
                reader.width = 9U;
                previous = -1;
            }
        }
    }

    result = (produced == output_size);
done:
    if (prefix) xx_mem_free(prefix);
    if (suffix) xx_mem_free(suffix);
    if (stack) xx_mem_free(stack);
    if (!result) return false;
    if (written) *written = produced;
    return true;
}
