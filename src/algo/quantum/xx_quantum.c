/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/quantum/xx_quantum.h"
#include <string.h>

#define Q_MAX_MODEL 64

typedef struct q_model_s {
    uint16_t symbols[Q_MAX_MODEL];
    uint16_t cumulative[Q_MAX_MODEL + 1];
    uint8_t count;
    uint8_t reorder_after;
    bool old_variant;
} q_model;

typedef struct q_bits_s {
    const uint8_t *data;
    size_t size, bit;
} q_bits;

typedef struct q_range_s {
    q_bits bits;
    uint32_t low, high, code;
} q_range;

typedef struct q_state_s {
    q_model selector, literal[4], position[3], length;
} q_state;

static const uint32_t q_pos_base[42] = {0,     1,     2,     3,     4,     6,     8,      12,     16,     24,     32,     48,     64,      96,
                                        128,   192,   256,   384,   512,   768,   1024,   1536,   2048,   3072,   4096,   6144,   8192,    12288,
                                        16384, 24576, 32768, 49152, 65536, 98304, 131072, 196608, 262144, 393216, 524288, 786432, 1048576, 1572864};
static const uint8_t q_pos_extra[42] = {0, 0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,
                                        9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19};
static const uint16_t q_len_base[27] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 18, 22, 26, 30, 38, 46, 54, 62, 78, 94, 110, 126, 158, 190, 222, 254};
static const uint8_t q_len_extra[27] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t q_old_len_base[29] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256};
static const uint8_t q_old_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

static void q_model_init(q_model *m, unsigned count, unsigned first)
{
    unsigned i;
    xx_rt_memset(m, 0, sizeof(*m));
    m->count = (uint8_t)count;
    m->reorder_after = 4;
    for (i = 0; i < count; ++i) m->symbols[i] = (uint16_t)(first + i);
    for (i = 0; i <= count; ++i) m->cumulative[i] = (uint16_t)(count - i);
}

static void q_model_init_old(q_model *m, unsigned count, unsigned first, bool weighted)
{
    unsigned i, weight = 8U;
    q_model_init(m, count, first);
    m->old_variant = true;
    if (!weighted) return;
    m->cumulative[count] = 0U;
    for (i = count; i-- > 0U;) {
        m->cumulative[i] = (uint16_t)(m->cumulative[i + 1U] + weight);
        weight = weight / 2U + 1U;
    }
}

static void q_model_rescale(q_model *m)
{
    int i;
    for (i = (int)m->count - 1; i >= 0; --i) {
        unsigned v = m->cumulative[i] >> 1U;
        if (v <= m->cumulative[i + 1]) v = m->cumulative[i + 1] + 1U;
        m->cumulative[i] = (uint16_t)v;
    }
}

static void q_model_reorder(q_model *m)
{
    uint16_t frequencies[Q_MAX_MODEL];
    unsigned i, j;
    for (i = 0; i < m->count; ++i) frequencies[i] = (uint16_t)(((m->cumulative[i] - m->cumulative[i + 1]) + 1U) >> 1U);
    /* Quantum swaps at each comparison.  Picking one maximum per slot yields
     * the same weights but a different order for equal-weight symbols. */
    for (i = 0; i + 1U < m->count; ++i) {
        for (j = i + 1U; j < m->count; ++j) {
            if (frequencies[i] < frequencies[j]) {
                uint16_t weight = frequencies[i];
                uint16_t symbol = m->symbols[i];
                frequencies[i] = frequencies[j];
                frequencies[j] = weight;
                m->symbols[i] = m->symbols[j];
                m->symbols[j] = symbol;
            }
        }
    }
    m->cumulative[m->count] = 0;
    for (i = m->count; i-- > 0;) m->cumulative[i] = (uint16_t)(m->cumulative[i + 1] + frequencies[i]);
    m->reorder_after = 50;
}

static void q_model_update(q_model *m, unsigned slot)
{
    unsigned i;
    for (i = 0; i <= slot; ++i) m->cumulative[i] = (uint16_t)(m->cumulative[i] + 8U);
    if (m->cumulative[0] > 3800U) {
        if (m->old_variant || --m->reorder_after) q_model_rescale(m);
        else q_model_reorder(m);
    }
}

static unsigned q_get_bit(q_bits *b)
{
    size_t byte = b->bit >> 3U;
    unsigned value = byte < b->size ? (b->data[byte] >> (7U - (b->bit & 7U))) & 1U : 0U;
    ++b->bit;
    return value;
}

static uint32_t q_get_bits(q_bits *b, unsigned count)
{
    uint32_t value = 0;
    while (count--) value = (value << 1U) | q_get_bit(b);
    return value;
}

static void q_range_init(q_range *r, const uint8_t *data, size_t size)
{
    r->bits.data = data;
    r->bits.size = size;
    r->bits.bit = 0;
    r->low = 0;
    r->high = 0xffffU;
    r->code = q_get_bits(&r->bits, 16);
}

static int q_decode_symbol(q_range *r, q_model *m)
{
    uint32_t range = ((r->high - r->low) & 0xffffU) + 1U;
    uint32_t freq = (uint32_t)((((uint64_t)(r->code - r->low + 1U) * m->cumulative[0] - 1U) / range) & 0xffffU);
    unsigned i = 1, slot;
    while (i <= m->count && m->cumulative[i] > freq) ++i;
    if (i > m->count) return -1;
    slot = i - 1U;
    range = r->high - r->low + 1U;
    r->high = (uint32_t)((r->low + (uint64_t)m->cumulative[slot] * range / m->cumulative[0] - 1U) & 0xffffU);
    r->low = (uint32_t)((r->low + (uint64_t)m->cumulative[slot + 1] * range / m->cumulative[0]) & 0xffffU);
    for (;;) {
        if ((r->low & 0x8000U) != (r->high & 0x8000U)) {
            if ((r->low & 0x4000U) && !(r->high & 0x4000U)) {
                r->code ^= 0x4000U;
                r->low &= 0x3fffU;
                r->high |= 0x4000U;
            } else break;
        }
        r->low = (r->low << 1U) & 0xffffU;
        r->high = ((r->high << 1U) | 1U) & 0xffffU;
        r->code = ((r->code << 1U) | q_get_bit(&r->bits)) & 0xffffU;
    }
    i = m->symbols[slot];
    q_model_update(m, slot);
    return (int)i;
}

static void q_state_init(q_state *s, unsigned order)
{
    unsigned i, slots = 20U + 2U * (order - 10U);
    q_model_init(&s->selector, 7, 0);
    for (i = 0; i < 4; ++i) q_model_init(&s->literal[i], 64, 64U * i);
    q_model_init(&s->position[0], slots < 24U ? slots : 24U, 0);
    q_model_init(&s->position[1], slots < 36U ? slots : 36U, 0);
    q_model_init(&s->position[2], slots < 42U ? slots : 42U, 0);
    q_model_init(&s->length, 27, 0);
}

static void q_state_init_old(q_state *s, unsigned order)
{
    unsigned i, slots = order * 2U;
    q_model_init_old(&s->selector, 5U, 0U, false);
    for (i = 0U; i < 4U; ++i) q_model_init_old(&s->literal[i], 64U, 64U * i, false);
    q_model_init_old(&s->position[0], slots < 24U ? slots : 24U, 0U, true);
    q_model_init_old(&s->position[1], slots < 36U ? slots : 36U, 0U, true);
    q_model_init_old(&s->position[2], slots, 0U, true);
    q_model_init_old(&s->length, 29U, 0U, true);
}

static bool q_decode_run(q_state *s, q_range *r, size_t plain_size, uint8_t *output, size_t *out_pos, bool zero_history)
{
    size_t start = *out_pos;
    while (*out_pos - start < plain_size) {
        int selector = q_decode_symbol(r, &s->selector);
        if (selector < 0 || selector > 6) return false;
        if (selector < 4) {
            int literal = q_decode_symbol(r, &s->literal[selector]);
            if (literal < 0) return false;
            output[(*out_pos)++] = (uint8_t)literal;
        } else {
            size_t length;
            int pm, pos_slot;
            uint64_t offset;
            if (selector == 4) {
                length = 3;
                pm = 0;
            } else if (selector == 5) {
                length = 4;
                pm = 1;
            } else {
                int ls = q_decode_symbol(r, &s->length);
                if (ls < 0 || ls >= 27) return false;
                length = (size_t)q_len_base[ls] + q_get_bits(&r->bits, q_len_extra[ls]) + 5U;
                pm = 2;
            }
            pos_slot = q_decode_symbol(r, &s->position[pm]);
            if (pos_slot < 0 || pos_slot >= 42) return false;
            offset = (uint64_t)q_pos_base[pos_slot] + q_get_bits(&r->bits, q_pos_extra[pos_slot]) + 1U;
            if ((!zero_history && offset > *out_pos) || length > plain_size - (*out_pos - start)) return false;
            while (length--) {
                output[*out_pos] = offset > *out_pos ? 0U : output[*out_pos - (size_t)offset];
                ++*out_pos;
            }
        }
    }
    return *out_pos - start == plain_size;
}

static bool q_decode_block(q_state *s, const uint8_t *data, size_t data_size, size_t plain_size, uint8_t *output, size_t *out_pos)
{
    q_range r;
    q_range_init(&r, data, data_size);
    return q_decode_run(s, &r, plain_size, output, out_pos, false);
}

bool xx_quantum_archive_decode(const uint8_t *data, size_t data_size, const size_t *member_sizes, size_t member_count, unsigned window_bits, uint8_t *output,
                               size_t output_size, size_t *written)
{
    q_state state;
    q_range range;
    size_t member, pos = 0U;
    if (written) *written = 0U;
    if (!data || data_size < 2U || !member_sizes || !member_count || !output || window_bits < 10U || window_bits > 21U || data_size > SIZE_MAX / 8U) return false;
    q_state_init(&state, window_bits);
    q_range_init(&range, data, data_size);
    for (member = 0U; member < member_count; ++member) {
        size_t start = pos, cursor;
        uint16_t checksum = 0U;
        uint32_t stored;
        if (member_sizes[member] > output_size - pos || !q_decode_run(&state, &range, member_sizes[member], output, &pos, true)) return false;
        for (cursor = start; cursor < pos; ++cursor) {
            checksum = (uint16_t)((((uint16_t)(checksum ^ output[cursor])) << 1U) | (checksum >> 15U));
        }
        stored = q_get_bits(&range.bits, 16U);
        if (range.bits.bit > data_size * 8U || stored != checksum) return false;
    }
    if (pos != output_size) return false;
    if (written) *written = pos;
    return true;
}

static bool q_decode_uniform_once(q_range *r, unsigned bits, uint32_t *value)
{
    uint32_t count = 1U << bits;
    uint32_t span = ((r->high - r->low) & 0xffffU) + 1U;
    uint32_t slot = (uint32_t)((((uint64_t)(r->code - r->low + 1U) * count - 1U) / span) & 0xffffU);
    if (slot >= count) return false;
    span = r->high - r->low + 1U;
    r->high = (uint32_t)((r->low + (uint64_t)(slot + 1U) * span / count - 1U) & 0xffffU);
    r->low = (uint32_t)((r->low + (uint64_t)slot * span / count) & 0xffffU);
    for (;;) {
        if ((r->low & 0x8000U) != (r->high & 0x8000U)) {
            if ((r->low & 0x4000U) && !(r->high & 0x4000U)) {
                r->code ^= 0x4000U;
                r->low &= 0x3fffU;
                r->high |= 0x4000U;
            } else break;
        }
        r->low = (r->low << 1U) & 0xffffU;
        r->high = ((r->high << 1U) | 1U) & 0xffffU;
        r->code = ((r->code << 1U) | q_get_bit(&r->bits)) & 0xffffU;
    }
    *value = slot;
    return true;
}

static bool q_decode_uniform(q_range *r, unsigned bits, uint32_t *value)
{
    uint32_t high = 0U, low = 0U;
    if (bits > 12U) {
        if (!q_decode_uniform_once(r, 12U, &high)) return false;
        bits -= 12U;
    }
    if (bits && !q_decode_uniform_once(r, bits, &low)) return false;
    *value = (high << bits) | low;
    return true;
}

static bool q_decode_old_run(q_state *s, q_range *r, size_t plain_size, uint8_t *output, size_t *out_pos)
{
    size_t start = *out_pos;
    while (*out_pos - start < plain_size) {
        int raw = q_decode_symbol(r, &s->selector);
        int selector;
        if (raw < 0 || raw > 4) return false;
        selector = 4 - raw;
        if (selector < 4) {
            int literal = q_decode_symbol(r, &s->literal[selector]);
            if (literal < 0) return false;
            output[(*out_pos)++] = (uint8_t)(63 + 128 * selector - literal);
        } else {
            int raw_length = q_decode_symbol(r, &s->length);
            unsigned length_slot, position_slot, pm;
            uint32_t extra;
            uint64_t offset;
            size_t length;
            int raw_position;
            if (raw_length < 0 || raw_length >= 29) return false;
            length_slot = 28U - (unsigned)raw_length;
            if (!q_decode_uniform(r, q_old_len_extra[length_slot], &extra)) return false;
            length = (size_t)q_old_len_base[length_slot] + extra + 3U;
            pm = length == 3U ? 0U : length == 4U ? 1U : 2U;
            raw_position = q_decode_symbol(r, &s->position[pm]);
            if (raw_position < 0 || raw_position >= s->position[pm].count) return false;
            position_slot = s->position[pm].count - 1U - (unsigned)raw_position;
            if (position_slot >= 42U || !q_decode_uniform(r, q_pos_extra[position_slot], &extra)) return false;
            offset = (uint64_t)q_pos_base[position_slot] + extra + 1U;
            if (length > plain_size - (*out_pos - start)) return false;
            while (length--) {
                output[*out_pos] = offset > *out_pos ? 0U : output[*out_pos - (size_t)offset];
                ++*out_pos;
            }
        }
    }
    return *out_pos - start == plain_size;
}

bool xx_quantum_archive_decode_old(const uint8_t *data, size_t data_size, const size_t *member_sizes, const uint16_t *checksums, size_t member_count,
                                   unsigned window_bits, uint8_t *output, size_t output_size, size_t *written)
{
    q_state state;
    q_range range;
    size_t member, pos = 0U;
    if (written) *written = 0U;
    if (!data || data_size < 2U || !member_sizes || !checksums || !member_count || !output || window_bits < 10U || window_bits > 21U || data_size > SIZE_MAX / 8U)
        return false;
    q_state_init_old(&state, window_bits);
    q_range_init(&range, data, data_size);
    for (member = 0U; member < member_count; ++member) {
        size_t start = pos, cursor;
        uint16_t checksum = 0U;
        if (member_sizes[member] > output_size - pos || !q_decode_old_run(&state, &range, member_sizes[member], output, &pos)) return false;
        for (cursor = start; cursor < pos; ++cursor) {
            checksum = (uint16_t)(checksum + 1U);
            checksum = (uint16_t)((((uint16_t)checksum << 1U) | (checksum >> 15U)) ^ output[cursor]);
        }
        if (range.bits.bit > data_size * 8U || checksum != checksums[member]) return false;
    }
    if (pos != output_size) return false;
    if (written) *written = pos;
    return true;
}

bool xx_quantum_cab_decode(const uint8_t *const *blocks, const size_t *block_sizes, const size_t *plain_sizes, size_t block_count, unsigned window_bits, uint8_t *output,
                           size_t output_size, size_t *written)
{
    q_state state;
    size_t i, pos = 0;
    if (written) *written = 0;
    if (!blocks || !block_sizes || !plain_sizes || !block_count || !output || window_bits < 10U || window_bits > 21U) return false;
    q_state_init(&state, window_bits);
    for (i = 0; i < block_count; ++i) {
        if (!blocks[i] || !block_sizes[i] || !plain_sizes[i] || plain_sizes[i] > output_size - pos ||
            !q_decode_block(&state, blocks[i], block_sizes[i], plain_sizes[i], output, &pos))
            return false;
    }
    if (pos != output_size) return false;
    if (written) *written = pos;
    return true;
}
