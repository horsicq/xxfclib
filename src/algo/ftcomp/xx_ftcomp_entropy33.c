/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * FTCOMP fT33 block entropy decoder, ported from the 0x00665af0 variant.
 */
#include "xx_ftcomp_entropy33.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define XX_FTCOMP_SYMBOLS 0x1c4U
#define XX_FTCOMP_LEAF_END 0x710U
#define XX_FTCOMP_INTERNAL 0x710U
#define XX_FTCOMP_NODE_WORDS (0x1c60U / 2U)
#define XX_FTCOMP_LUT_SIZE 512U
#define XX_FTCOMP_TOKEN_SLACK 16U

static const uint16_t ft33_weights_header[257] = {
    1024, 600, 300, 260, 230, 212, 192, 172, 148, 132, 120, 108, 
    92, 84, 80, 76, 72, 68, 64, 60, 56, 52, 48, 44, 
    40, 36, 32, 28, 24, 22, 20, 19, 18, 17, 16, 15, 
    14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9, 
    9, 8, 8, 8, 7, 7, 7, 6, 6, 6, 6, 5, 
    5, 5, 5, 4, 4, 4, 4, 4, 4, 3, 3, 3, 
    3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1, 1, 4, 15
};
static const uint16_t ft33_weights_extra[258] = {
    40, 39, 39, 38, 38, 37, 37, 36, 36, 35, 35, 34, 
    34, 33, 33, 32, 32, 31, 31, 30, 30, 29, 29, 28, 
    28, 27, 26, 25, 24, 24, 23, 23, 22, 22, 21, 21, 
    20, 20, 19, 19, 19, 18, 18, 18, 17, 17, 17, 17, 
    16, 16, 16, 16, 16, 16, 16, 15, 15, 15, 15, 15, 
    15, 15, 15, 14, 14, 14, 14, 14, 14, 13, 13, 13, 
    13, 13, 13, 12, 12, 12, 12, 12, 11, 11, 11, 11, 
    11, 10, 10, 10, 10, 10, 10, 9, 9, 9, 9, 9, 
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 
    9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 
    8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
    5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 0, 0, 255, 120
};

typedef struct xx_ftcomp_huff_s {
    uint16_t node[XX_FTCOMP_NODE_WORDS];
    uint16_t lut[XX_FTCOMP_LUT_SIZE];
    uint8_t lutlen[XX_FTCOMP_LUT_SIZE];
    uint32_t root;
} xx_ftcomp_huff;

typedef struct xx_ftcomp_bits_s {
    const uint8_t *data;
    size_t size;
    size_t at;
    uint16_t accumulator;
    int32_t held;
} xx_ftcomp_bits;


static void xx_ftcomp_bits_init(xx_ftcomp_bits *bits, const uint8_t *data,
                                size_t size, size_t at) {
    bits->data = data;
    bits->size = size;
    bits->at = at;
    bits->accumulator = 0U;
    bits->held = 0;
}

/* MSB-first into a sixteen-bit accumulator.  Reading past the end feeds zero
 * bytes, as the reference does against its own zero-filled buffer; a stream
 * that actually needs them fails the length checks instead. */
static void xx_ftcomp_bits_fill(xx_ftcomp_bits *bits, int32_t need) {
    while (bits->held < need) {
        uint32_t byte = (bits->at < bits->size) ? bits->data[bits->at] : 0U;
        ++bits->at;
        bits->accumulator =
            (uint16_t)(bits->accumulator |
                       (uint16_t)(byte << ((8 - bits->held) & 31)));
        bits->held += 8;
    }
}

static void xx_ftcomp_bits_drop(xx_ftcomp_bits *bits, int32_t count) {
    bits->accumulator = (uint16_t)(bits->accumulator << count);
    bits->held -= count;
}

static uint32_t xx_ftcomp_bits_one(xx_ftcomp_bits *bits) {
    uint32_t value;
    xx_ftcomp_bits_fill(bits, 1);
    value = (uint32_t)(bits->accumulator >> 15);
    xx_ftcomp_bits_drop(bits, 1);
    return value;
}

static uint32_t xx_ftcomp_bits_take(xx_ftcomp_bits *bits, int32_t count) {
    uint32_t value;
    xx_ftcomp_bits_fill(bits, count);
    value = (uint32_t)(bits->accumulator >> (16 - count));
    xx_ftcomp_bits_drop(bits, count);
    return value;
}


static void xx_ftcomp_sort(uint16_t *lut, const uint16_t *node, int32_t low,
                           int32_t high) {
    int32_t stack[66];
    int32_t depth = 2;
    stack[0] = low;
    stack[1] = high;
    while (depth != 0) {
        int32_t right = stack[depth - 1];
        int32_t left;
        depth -= 2;
        left = stack[depth];
        for (;;) {
            int32_t a = left, b = right, keep = right, next;
            if (b - a < 0x11) {
                int32_t probe = a;
                for (;;) {
                    int32_t hold = probe;
                    int32_t scan = a;
                    probe = hold + 1;
                    next = b;
                    if (probe > b) break;
                    while (scan < probe && node[lut[scan]] < node[lut[probe]])
                        ++scan;
                    if (scan <= hold) {
                        int32_t k = hold;
                        for (;;) {
                            uint16_t swap = lut[k];
                            lut[k] = lut[k + 1];
                            lut[k + 1] = swap;
                            --k;
                            if (k == scan - 1) break;
                        }
                    }
                }
            } else {
                int32_t pivot = (a + b) >> 1, i = a, j = b;
                for (;;) {
                    while (node[lut[i]] < node[lut[pivot]]) ++i;
                    while (node[lut[j]] > node[lut[pivot]]) --j;
                    if (i <= j) {
                        uint16_t swap = lut[i];
                        int32_t moved = j;
                        lut[i] = lut[j];
                        lut[j] = swap;
                        if (pivot != i) {
                            moved = pivot;
                            if (pivot == j) moved = i;
                        }
                        ++i;
                        --j;
                        pivot = moved;
                    }
                    if (i > j) break;
                }
                if (j - a < b - i) {
                    keep = j;
                    next = a;
                    if (i < b && depth + 2 <= 64) {
                        stack[depth] = i;
                        stack[depth + 1] = b;
                        depth += 2;
                    }
                } else {
                    next = i;
                    if (a < j && depth + 2 <= 64) {
                        stack[depth] = a;
                        stack[depth + 1] = j;
                        depth += 2;
                    }
                }
            }
            left = next;
            right = keep;
            if (left >= right) break;
        }
    }
}


static bool xx_ftcomp_build(xx_ftcomp_huff *huff) {
    uint16_t *node = huff->node;
    uint16_t *lut = huff->lut;
    int32_t index = 0, count = 0, ones = 0, last_zero = 0;
    int32_t free_node, head = 0, alive, slot;
    uint32_t pattern;

    while (index < XX_FTCOMP_LEAF_END) {
        node[index + 1] = 0U;
        if (node[index] != 0U) {
            if (node[index] == 1U) {
                /* Weight-one symbols stay at the front in symbol order, so
                 * sorting only the tail leaves the whole list sorted. */
                lut[count++] = lut[ones];
                lut[ones++] = (uint16_t)index;
            } else {
                lut[count++] = (uint16_t)index;
            }
        } else {
            last_zero = index;
        }
        index += 4;
    }
    if (count == 0) return false;
    if (count == 1) {
        lut[1] = lut[ones];
        count = 2;
        lut[ones++] = (uint16_t)last_zero;
        node[last_zero] = 1U;
    }
    free_node = XX_FTCOMP_INTERNAL;
    xx_ftcomp_sort(lut, node, ones, count - 1);
    alive = count;
    while (alive != 2) {
        int32_t bound = head + 2, insert = (count + head + 2) >> 1;
        int32_t top = count, moved;
        uint32_t weight;
        uint16_t first, second;
        --alive;
        first = lut[head];
        second = lut[head + 1];
        ++head;
        weight = (uint32_t)node[first] + (uint32_t)node[second];
        if (bound < count) {
            for (;;) {
                if ((uint32_t)node[lut[insert]] < weight) {
                    bound = insert + 1;
                    insert = top;
                }
                top = insert;
                insert = (top + bound) >> 1;
                if (bound >= top) break;
            }
            insert = (top + bound) >> 1;
        }
        moved = insert - head - 1;
        if (insert < 1 || insert > count ||
            free_node + 3 >= XX_FTCOMP_NODE_WORDS)
            return false;
        if (moved > 0) {
            int32_t k;
            for (k = 0; k < moved; ++k) lut[head + k] = lut[head + k + 1];
        }
        lut[insert - 1] = (uint16_t)free_node;
        node[free_node] = (uint16_t)weight;
        node[free_node + 1] = 0U;
        node[free_node + 2] = first;
        node[free_node + 3] = second;
        node[first + 1] = (uint16_t)free_node;
        node[second + 1] = (uint16_t)free_node;
        free_node += 4;
    }
    if (free_node + 3 >= XX_FTCOMP_NODE_WORDS) return false;
    {
        uint16_t first = lut[head];
        uint16_t second = lut[head + 1];
        node[free_node] =
            (uint16_t)((uint32_t)node[first] + (uint32_t)node[second]);
        node[free_node + 1] = 0U;
        node[free_node + 2] = first;
        node[free_node + 3] = second;
        node[first + 1] = (uint16_t)free_node;
        node[second + 1] = (uint16_t)free_node;
    }
    huff->root = (uint32_t)free_node;
    pattern = 0U;
    for (slot = 0; slot < XX_FTCOMP_LUT_SIZE; ++slot) {
        uint32_t walk = pattern, current = huff->root, reached = 0U;
        int32_t used = 0;
        for (;;) {
            reached = current + (((walk & 0x8000U) == 0U) ? 1U : 0U);
            walk <<= 1;
            ++used;
            if (reached + 2U >= (uint32_t)XX_FTCOMP_NODE_WORDS) return false;
            current = node[reached + 2U];
            if (current < (uint32_t)XX_FTCOMP_LEAF_END) break;
            if (used >= 9) break;
        }
        huff->lut[slot] = node[reached + 2U];
        huff->lutlen[slot] = (uint8_t)used;
        pattern += 0x80U;
    }
    return true;
}

static bool xx_ftcomp_build_static(xx_ftcomp_huff *huff,
                                   const uint16_t *weights, size_t count) {
    size_t index;
    xx_mem_zero(huff, sizeof(*huff));
    for (index = 0U; index < count; ++index)
        huff->node[index * 4U] = weights[index];
    return xx_ftcomp_build(huff);
}

static uint32_t xx_ftcomp_decode_symbol(xx_ftcomp_bits *bits,
                                        const xx_ftcomp_huff *huff) {
    uint32_t top, value;
    xx_ftcomp_bits_fill(bits, 9);
    top = (uint32_t)(bits->accumulator >> 7);
    value = huff->lut[top];
    xx_ftcomp_bits_drop(bits, (int32_t)huff->lutlen[top]);
    while (value >= (uint32_t)XX_FTCOMP_LEAF_END) {
        uint32_t bit = xx_ftcomp_bits_one(bits);
        if (value + 3U >= (uint32_t)XX_FTCOMP_NODE_WORDS) return 0U;
        value = huff->node[value + ((bit == 0U) ? 1U : 0U) + 2U];
    }
    return value;
}

static uint32_t ft33_class(uint32_t symbol) {
    if (symbol < 0x100U) return 0U;
    if (symbol < 0x194U) return 1U;
    if (symbol < 0x1b4U) return 0U;
    return 1U;
}

static uint32_t ft33_recent(uint32_t symbol, uint32_t *current,
                            uint32_t *previous) {
    uint32_t value, low, high;
    if (symbol == 0x100U) return *current;
    value = *previous;
    if (symbol != 0x101U) {
        value = symbol;
        low = *current < *previous ? *current : *previous;
        high = *current < *previous ? *previous : *current;
        if (low <= value) ++value;
        if (high <= value) ++value;
    }
    *previous = *current;
    *current = value;
    return value;
}

static uint32_t ft33_token_fields(uint8_t token) {
    if (token < 0x40U) return 1U;
    if (token < 0x80U) return 2U;
    if (token < 0x90U) return 3U;
    if (token < 0x92U) return 1U;
    return 0U;
}

typedef struct ft33_decoder_s {
    xx_ftcomp_huff header, extra, table_a, table_b;
    xx_ftcomp_bits bits;
    uint16_t weights[XX_FTCOMP_SYMBOLS];
    uint8_t *out;
    size_t expected, capacity, at;
    uint32_t position, context, pending;
    uint32_t current_byte, previous_byte;
    uint32_t current_near, previous_near;
    uint32_t current_far, previous_far;
    uint32_t current_special, previous_special;
    uint32_t current_high, previous_high;
    bool special;
    uint16_t pairs[48], places[48];
    uint32_t pair_head, place_head;
    uint32_t last_symbol;
} ft33_decoder;

static bool ft33_put(ft33_decoder *d, uint8_t byte) {
    if (d->at >= d->capacity) return false;
    d->out[d->at++] = byte;
    return true;
}

static uint32_t ft33_symbol(ft33_decoder *d, const xx_ftcomp_huff *huff) {
    return xx_ftcomp_decode_symbol(&d->bits, huff) >> 2U;
}

static bool ft33_dynamic_tree(ft33_decoder *d, xx_ftcomp_huff *tree,
                              uint32_t first, uint32_t second) {
    uint32_t largest = 0U, scale, index;
    xx_mem_zero(tree, sizeof(*tree));
    for (index = 0U; index < XX_FTCOMP_SYMBOLS; ++index) {
        uint32_t weight = d->weights[index];
        uint32_t scaled = weight ? weight * (ft33_class(index) ? second : first) : 0U;
        tree->node[index * 4U] = (uint16_t)scaled;
        if (tree->node[index * 4U] > largest) largest = tree->node[index * 4U];
    }
    scale = largest < 0x100U ? 0U : 0xffffU / largest;
    if (scale) {
        for (index = 0U; index < XX_FTCOMP_SYMBOLS; ++index) {
            uint32_t before = tree->node[index * 4U];
            uint32_t after = (before * scale) >> 8U;
            tree->node[index * 4U] = (uint16_t)(before && !after ? 1U : after);
        }
    }
    return xx_ftcomp_build(tree);
}

static bool ft33_read_header(ft33_decoder *d, const uint8_t *input,
                             size_t input_size) {
    uint32_t filled = 0U;
    if (input_size < 4U ||
        !xx_ftcomp_build_static(&d->header, ft33_weights_header,
                                sizeof(ft33_weights_header) / sizeof(ft33_weights_header[0])) ||
        !xx_ftcomp_build_static(&d->extra, ft33_weights_extra,
                                sizeof(ft33_weights_extra) / sizeof(ft33_weights_extra[0])))
        return false;
    xx_ftcomp_bits_init(&d->bits, input, input_size, 4U);
    while (filled < XX_FTCOMP_SYMBOLS) {
        uint32_t symbol = ft33_symbol(d, &d->header);
        if (symbol == 0x100U) {
            uint32_t count = XX_FTCOMP_SYMBOLS - filled;
            if (count > 16U) count = 16U;
            while (count--) d->weights[filled++] = 0U;
        } else if (symbol < 0x100U) {
            d->weights[filled++] = (uint16_t)symbol;
        } else {
            return false;
        }
        if (d->bits.at > input_size + 4U) return false;
    }
    if (!ft33_dynamic_tree(d, &d->table_a, input[0], input[1])) return false;
    if (input[2] == input[1] && input[3] == input[0]) {
        d->table_b = d->table_a;
        return true;
    }
    if (input[2] || input[3])
        return ft33_dynamic_tree(d, &d->table_b, input[3], input[2]);
    d->table_b = d->table_a;
    return true;
}

static bool ft33_remember_pair(ft33_decoder *d, uint16_t pair) {
    if (d->pair_head == 0U) {
        uint32_t k;
        for (k = 0U; k < 15U; ++k) d->pairs[32U + k] = d->pairs[k];
        d->pair_head = 0x1fU;
    } else --d->pair_head;
    d->pairs[d->pair_head] = pair;
    return true;
}

static void ft33_remember_place(ft33_decoder *d, uint16_t place) {
    if (d->place_head == 0U) {
        uint32_t k;
        for (k = 0U; k < 16U; ++k) d->places[32U + k] = d->places[k];
        d->place_head = 0x1fU;
    } else --d->place_head;
    d->places[d->place_head] = place;
}

static bool ft33_main_symbol(ft33_decoder *d) {
    uint32_t symbol = ft33_symbol(d, d->context ? &d->table_b : &d->table_a);
    d->last_symbol = symbol;
    size_t at = d->at;
    if (symbol >= XX_FTCOMP_SYMBOLS) return false;
    d->context = ft33_class(symbol);
    if (symbol < 0x100U) {
        ++d->position;
        return ft33_put(d, (uint8_t)symbol) &&
               (symbol != 0x9eU || ft33_put(d, 0xffU));
    }
    if (symbol < 0x194U) {
        uint8_t token = (uint8_t)(symbol - 0x100U);
        if (!ft33_put(d, 0x9eU) || !ft33_put(d, token)) return false;
        d->pending = ft33_token_fields(token);
        if (token == 0x90U || token == 0x91U) d->pending = 99U;
        if (d->pending) {
            if (d->pending < 3U) d->position += (token & 0x3fU) + 3U;
            d->special = token == 0x40U;
            ft33_remember_place(d, (uint16_t)(at + 1U));
        } else {
            d->position += 2U;
        }
        return true;
    }
    if (symbol < 0x1a4U) {
        uint32_t back = symbol - 0x192U;
        uint16_t pair;
        if (back > at || at - back + 1U >= at || d->capacity - at < 2U) return false;
        pair = (uint16_t)((uint32_t)d->out[at - back] |
                          ((uint32_t)d->out[at - back + 1U] << 8U));
        d->position += ((pair & 0xffU) != 0x9eU ? 1U : 0U) + 1U;
        if (!ft33_put(d, (uint8_t)pair) ||
            !ft33_put(d, (uint8_t)(pair >> 8U))) return false;
        return ft33_remember_pair(d, pair);
    }
    if (symbol < 0x1b4U) {
        uint32_t rank = symbol - 0x1a4U, k;
        uint16_t pair = d->pairs[d->pair_head + rank];
        if (d->capacity - at < 2U) return false;
        d->position += ((pair & 0xffU) != 0x9eU ? 1U : 0U) + 1U;
        if (!ft33_put(d, (uint8_t)pair) ||
            !ft33_put(d, (uint8_t)(pair >> 8U))) return false;
        for (k = rank; k > 0U; --k)
            d->pairs[d->pair_head + k] = d->pairs[d->pair_head + k - 1U];
        d->pairs[d->pair_head] = pair;
        return true;
    }
    {
        uint32_t rank = symbol - 0x1b4U, k;
        uint32_t source = d->places[d->place_head + rank];
        uint8_t token;
        uint32_t fields;
        size_t copied;
        if (source < 1U || source >= at) return false;
        token = d->out[source];
        fields = ft33_token_fields(token);
        if (!fields || source + fields >= at ||
            d->capacity - at < 2U + fields) return false;
        if (token < 0x80U)
            d->position += (token & 0x3fU) + 3U;
        else if (token < 0x88U)
            d->position += (uint32_t)d->out[source + 1U] + 6U;
        else if (token < 0x90U)
            d->position += (uint32_t)d->out[source + 1U] + 0x106U;
        else
            d->position += (uint32_t)d->out[source + 1U] + 3U;
        if (!ft33_put(d, 0x9eU) || !ft33_put(d, token)) return false;
        for (copied = 0U; copied < fields; ++copied)
            if (!ft33_put(d, d->out[source + 1U + copied])) return false;
        for (k = rank; k > 0U; --k)
            d->places[d->place_head + k] = d->places[d->place_head + k - 1U];
        d->places[d->place_head] = (uint16_t)(at + 1U);
        return true;
    }
}

static bool ft33_pending_field(ft33_decoder *d) {
    uint32_t symbol, value, raw = 0U, range = 0U;
    if (d->pending == 2U) {
        xx_ftcomp_bits_fill(&d->bits, 9);
        if (d->special) {
            range = 2U;
            raw = xx_ftcomp_bits_take(&d->bits, 2);
        } else if (!(d->bits.accumulator & 0x8000U) ||
                   d->position < 0x2101U) {
            range = 0U;
            raw = d->position < 0x2100U
                      ? xx_ftcomp_bits_take(&d->bits, 5) & 0x1fU
                      : xx_ftcomp_bits_take(&d->bits, 6) & 0x1fU;
        } else {
            range = 1U;
            raw = d->position < 0x6100U
                      ? xx_ftcomp_bits_take(&d->bits, 7) & 0x3fU
                      : d->position < 0xa100U
                            ? xx_ftcomp_bits_take(&d->bits, 8) & 0x7fU
                            : xx_ftcomp_bits_take(&d->bits, 9) & 0xffU;
        }
    }
    symbol = ft33_symbol(d, &d->extra);
    if (symbol > 0x101U) return false;
    if (d->pending == 1U) {
        value = ft33_recent(symbol, &d->current_byte, &d->previous_byte);
        d->pending = 0U;
        return value <= 0xffU && ft33_put(d, (uint8_t)value);
    }
    if (range == 2U) {
        value = ft33_recent(symbol, &d->current_special, &d->previous_special);
        value = raw + value * 4U + 0x100U;
    } else if (range == 0U) {
        value = ft33_recent(symbol, &d->current_near, &d->previous_near);
        value = raw + value * 0x20U + 0x100U;
    } else {
        value = ft33_recent(symbol, &d->current_far, &d->previous_far);
        value = raw + value * (d->position < 0x6100U ? 0x40U :
                               d->position < 0xa100U ? 0x80U : 0x100U) +
                0x2100U;
    }
    value &= 0xffffU;
    d->pending = 0U;
    return ft33_put(d, (uint8_t)value) &&
           ft33_put(d, (uint8_t)(value >> 8U));
}

static bool ft33_long_field(ft33_decoder *d) {
    uint32_t value, symbol;
    ++d->pending;
    if (d->pending == 5U) {
        value = xx_ftcomp_bits_take(&d->bits, 8);
    } else if (d->pending == 4U || d->pending == 100U) {
        symbol = ft33_symbol(d, &d->header);
        if (symbol > 0x100U) return false;
        value = symbol;
        if (d->pending == 100U) {
            d->position += value + 3U;
            d->pending = 0U;
        } else {
            uint8_t token;
            if (d->at < 2U) return false;
            token = d->out[d->at - 1U];
            d->position += value + (token < 0x88U ? 6U : 0x106U);
        }
    } else if (d->pending == 6U) {
        symbol = ft33_symbol(d, &d->extra);
        if (symbol > 0x101U) return false;
        value = ft33_recent(symbol, &d->current_high, &d->previous_high);
        d->pending = 0U;
    } else return false;
    return value <= 0xffU && ft33_put(d, (uint8_t)value);
}

bool xx_ftcomp_entropy33_decode(const uint8_t *input, size_t input_size,
                                uint8_t *tokens, size_t expected,
                                size_t capacity, uint32_t initial_position,
                                size_t *produced, size_t *consumed) {
    ft33_decoder *d;
    bool ok = false;
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input || !tokens || !produced || !consumed || input_size < 4U ||
        expected == 0U || expected > 0xffffU || capacity < expected)
        return false;
    d = (ft33_decoder *)xx_mem_calloc(1U, sizeof(*d));
    if (!d) return false;
    d->out = tokens;
    d->expected = expected;
    d->capacity = capacity - expected > XX_FTCOMP_TOKEN_SLACK
                      ? expected + XX_FTCOMP_TOKEN_SLACK : capacity;
    d->position = initial_position;
    d->pair_head = d->place_head = 0x20U;
    d->previous_byte = d->previous_near = d->previous_far = 1U;
    d->previous_special = d->previous_high = 1U;
    if (!ft33_read_header(d, input, input_size)) goto done;
    while (d->at < expected) {
        if ((d->pending == 0U ? !ft33_main_symbol(d) :
             d->pending < 3U ? !ft33_pending_field(d) :
                                !ft33_long_field(d)) ||
            d->bits.at > input_size + 4U) goto done;
    }
    *produced = d->at;
    *consumed = d->bits.at - (size_t)(d->bits.held >> 3);
    ok = *consumed != 0U && *consumed <= input_size &&
         *produced == expected;
done:
    xx_mem_free(d);
    return ok;
}

