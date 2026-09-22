/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HA (Harri Hirvola) methods 1 "ASC" and 2 "HSC".
 *
 * Ported from XArchive's Algos/xhadecoder.cpp, which was itself written from
 * the reference decompressor (HSC at VA 0x004bb180.., ASC at VA 0x004bd290..)
 * rather than from HA's own GPL sources.  This is a straight transliteration:
 * same tables, same update order, same integer widths.  The only structural
 * change is that QVector members became fixed arrays inside one heap-allocated
 * state struct (no global or static mutable state), and that running out of
 * output capacity is reported as a failure instead of being discovered
 * afterwards by comparing sizes - which is what the C++ wrapper did, so the
 * two agree on every stream.
 *
 * Several things in here look wrong and are load-bearing; they are commented
 * individually.  Do not "clean them up".
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/ha/xx_ha.h"

/* ------------------------------------------------------------------ arith */
/* Witten-Neal-Cleary decoder with 16-bit registers, shared by both methods. */

typedef struct ha_arith {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint16_t high;
    uint16_t low;
    uint16_t code;
    uint16_t buffer;
} ha_arith;

static uint32_t ha_arith_next_byte(ha_arith *a)
{
    /* Past the end the reference feeds zeroes rather than failing. */
    if (a->pos >= a->size) return 0U;
    return a->data[a->pos++];
}

static void ha_arith_init(ha_arith *a, const uint8_t *data, size_t size)
{
    a->data = data;
    a->size = size;
    a->pos = 0U;
    a->high = 0xffffU;
    a->low = 0U;
    a->code = 0U;
    a->buffer = 0U;
    a->code = (uint16_t)((ha_arith_next_byte(a) << 8) | ha_arith_next_byte(a));
}

static void ha_arith_shift_in(ha_arith *a)
{
    a->buffer = (uint16_t)(a->buffer << 1);
    if ((a->buffer & 0xffU) == 0U) {
        if (a->pos >= a->size) {
            a->buffer = 0x100U;
        } else {
            a->buffer = (uint16_t)((a->data[a->pos] * 2U) | 1U);
            ++a->pos;
        }
    }
    a->code = (uint16_t)(a->code | ((a->buffer >> 8) & 1U));
}

static uint32_t ha_arith_target(const ha_arith *a, uint32_t total)
{
    uint32_t range;
    if (total == 0U) return 0U;
    range = (uint32_t)(a->high - a->low) + 1U;
    return (uint32_t)(((((uint64_t)(uint16_t)(a->code - a->low) + 1U) *
                        (uint64_t)total) - 1U) / (uint64_t)range);
}

static void ha_arith_update(ha_arith *a, uint32_t low, uint32_t high,
                            uint32_t total)
{
    uint32_t range;
    if (total == 0U) return;
    range = (uint32_t)(a->high - a->low) + 1U;
    a->high = (uint16_t)(((range * high) / total) + a->low - 1U);
    a->low = (uint16_t)(a->low + ((range * low) / total));
    while (((a->high ^ a->low) & 0x8000U) == 0U) {
        a->low = (uint16_t)(a->low << 1);
        a->high = (uint16_t)((a->high << 1) | 1U);
        a->code = (uint16_t)(a->code << 1);
        ha_arith_shift_in(a);
    }
    /* underflow expansion: XOR of the top bit, not a plain shift */
    while ((a->low & 0x4000U) && !(a->high & 0x4000U)) {
        a->low = (uint16_t)((a->low & 0x3fffU) << 1);
        a->high = (uint16_t)((a->high << 1) | 0x8001U);
        a->code = (uint16_t)((a->code << 1) ^ 0x8000U);
        ha_arith_shift_in(a);
    }
}

/* ------------------------------------------------------------------- sink */

typedef struct ha_sink {
    uint8_t *data;
    size_t capacity;
    size_t pos;
} ha_sink;

static bool ha_sink_put(ha_sink *s, uint8_t value)
{
    /* Running out of capacity is a failure, never a truncation. */
    if (s->pos >= s->capacity) return false;
    s->data[s->pos++] = value;
    return true;
}

/* ================================================================== HSC == */

#define HA_CONTEXTS     10000
#define HA_POOL         0x7ff8
#define HA_HASH_SIZE    0x4000
#define HA_NIL          0xffff
#define HA_ESCAPE       0x100
#define HA_MAX_TOTAL    7999
#define HA_ORDER4_BUDGET 0x9c4

typedef struct ha_hsc {
    ha_arith arith;

    uint8_t order[HA_CONTEXTS];
    uint8_t low_count[HA_CONTEXTS];
    uint8_t novel[HA_CONTEXTS];
    uint16_t total[HA_CONTEXTS];
    uint8_t rescale[HA_CONTEXTS];
    uint8_t context_bytes[HA_CONTEXTS * 4];
    uint16_t hash_next[HA_CONTEXTS];
    uint16_t lru_prev[HA_CONTEXTS];
    uint16_t lru_next[HA_CONTEXTS];

    uint16_t freq[HA_POOL];
    uint8_t symbol[HA_POOL];
    uint16_t symbol_next[HA_POOL];

    uint16_t buckets[HA_HASH_SIZE];
    uint16_t hash[HA_HASH_SIZE];

    int32_t max_order;
    uint8_t increment[5];
    int32_t order4_budget;
    int32_t run_count;
    int32_t pool_scan;
    int32_t lru_head;
    int32_t lru_tail;
    int32_t free_symbol;

    uint8_t context[4];
    uint8_t excluded[0x100];
    uint8_t excluded_list[0x100];
    int32_t excluded_count;

    /* Both arrays are indexed up to and including escape_depth by
     * ha_hsc_reclaim_symbols(); see the comment there.  Eight slots is more
     * than the five escape levels the model can ever reach. */
    int32_t escape_context[8];
    int32_t escape_node[8];
    int32_t escape_depth;

    int32_t context_hash[6];
    int32_t scan_order;

    bool failed;
} ha_hsc;

static void ha_hsc_init(ha_hsc *m, const uint8_t *data, size_t size)
{
    int32_t i;
    int32_t x;

    xx_rt_memset(m, 0, sizeof(*m));
    ha_arith_init(&m->arith, data, size);

    /* HA_NIL is 0xffff and "unused order" is 0xff, so these four fills are
     * byte fills; they go through xx_rt_memset rather than through a loop the
     * compiler would happily turn back into a CRT memset call. */
    xx_rt_memset(m->order, 0xff, sizeof(m->order));
    xx_rt_memset(m->hash_next, 0xff, sizeof(m->hash_next));
    xx_rt_memset(m->symbol_next, 0xff, sizeof(m->symbol_next));
    xx_rt_memset(m->buckets, 0xff, sizeof(m->buckets));
    for (i = 0; i < HA_CONTEXTS; ++i) {
        /* lru_prev[0] deliberately wraps to 0xffff and lru_next[last] to
         * 10000: the head/tail guards keep both out of reach. */
        m->lru_next[i] = (uint16_t)(i + 1);
        m->lru_prev[i] = (uint16_t)(i - 1);
    }

    m->max_order = 4;
    for (i = 0; i < 5; ++i) m->increment[i] = (uint8_t)((i == 0) ? 0x10 : 0x0f);
    m->order4_budget = HA_ORDER4_BUDGET;
    m->run_count = 0;
    m->pool_scan = 0;

    m->lru_head = 0;
    m->lru_tail = HA_CONTEXTS - 1;
    for (i = HA_CONTEXTS; i < (HA_POOL - 1); ++i) {
        m->symbol_next[i] = (uint16_t)(i + 1);
    }
    m->symbol_next[HA_POOL - 1] = (uint16_t)HA_NIL;
    m->free_symbol = HA_CONTEXTS;

    m->excluded_count = 0;
    m->escape_depth = 0;
    m->scan_order = 0;
    m->failed = false;

    /* MINSTD (Schrage) seeded at 10.  The mixing table is part of the format,
     * not an implementation detail: it must be reproduced bit for bit.  Every
     * intermediate here fits in int32 by construction of Schrage's method, but
     * the arithmetic is done in 64 bits so that no signed overflow is possible
     * even for a hypothetical bad seed. */
    x = 10;
    for (i = 0; i < HA_HASH_SIZE; ++i) {
        int64_t t = ((int64_t)(x % 127773) * 16807) +
                    ((int64_t)(x / 127773) * -2836);
        x = (int32_t)t;
        if (x < 1) x += 0x7fffffff;
        m->hash[i] = (uint16_t)(x & 0x3fff);
    }
}

static int32_t ha_hsc_hash_of(const ha_hsc *m, const uint8_t *bytes,
                              int32_t order)
{
    int32_t h = 0;
    if (order > 0) h = m->hash[bytes[0]];
    if (order > 1) h = m->hash[(bytes[1] + h) & 0x3fff];
    if (order > 2) h = m->hash[(bytes[2] + h) & 0x3fff];
    if (order > 3) h = m->hash[(bytes[3] + h) & 0x3fff];
    return h;
}

static int32_t ha_hsc_find_next(ha_hsc *m)
{
    int32_t order;
    for (order = m->scan_order - 1; order >= 0; --order) {
        int32_t n = m->buckets[m->context_hash[order]];
        while (n != HA_NIL) {
            if (m->order[n] == (uint8_t)order) {
                if (order == 0) {
                    m->scan_order = order;
                    return n;
                }
                {
                    const uint8_t *p = m->context_bytes + (n * 4);
                    bool match = false;
                    if (order == 1) {
                        match = (m->context[0] == p[0]);
                    } else if (order == 2) {
                        match = (m->context[1] == p[1]) &&
                                (m->context[0] == p[0]);
                    } else if (order == 3) {
                        match = (m->context[2] == p[2]) &&
                                (m->context[1] == p[1]) &&
                                (m->context[0] == p[0]);
                    } else if (order == 4) {
                        match = (m->context[3] == p[3]) &&
                                (m->context[2] == p[2]) &&
                                (m->context[1] == p[1]) &&
                                (m->context[0] == p[0]);
                    }
                    if (match) {
                        m->scan_order = order;
                        return n;
                    }
                }
            }
            n = m->hash_next[n];
        }
    }
    return HA_NIL;
}

static int32_t ha_hsc_find_deepest(ha_hsc *m)
{
    m->context_hash[0] = 0;
    m->context_hash[1] = m->hash[m->context[0]];
    m->context_hash[2] = m->hash[(m->context[1] + m->context_hash[1]) & 0x3fff];
    m->context_hash[3] = m->hash[(m->context[2] + m->context_hash[2]) & 0x3fff];
    m->context_hash[4] = m->hash[(m->context[3] + m->context_hash[3]) & 0x3fff];
    m->escape_depth = 0;
    while (m->excluded_count) {
        --m->excluded_count;
        m->excluded[m->excluded_list[m->excluded_count]] = 0;
    }
    m->scan_order = 5;
    return ha_hsc_find_next(m);
}

/* order is 0..4 for every context that can reach the coder (find_next only
 * returns nodes whose order it matched), so this never clamps; it exists so a
 * recycled 0xff slot can never index past the five increment counters. */
static int32_t ha_hsc_inc_slot(const ha_hsc *m, int32_t context)
{
    const int32_t order = m->order[context];
    return (order > 4) ? 4 : order;
}

static int32_t ha_hsc_escape_frequency(const ha_hsc *m, int32_t low_count,
                                       int32_t context)
{
    int32_t novel;
    int32_t value;
    if (m->total[context] == 1) {
        return (m->increment[ha_hsc_inc_slot(m, context)] < 0x10) ? 1 : 2;
    }
    novel = m->novel[context];
    if (novel == 0xff) return 1;
    value = low_count;
    if ((novel != 0) && (m->total[context] <= ((novel + 1) * 2))) {
        value = (int32_t)(((uint32_t)low_count * (uint32_t)((novel + 1) * 2)) /
                          (uint32_t)m->total[context]);
        if ((novel + 1) == (int32_t)m->total[context]) value += ((novel + 1) >> 1);
    }
    if (value == 0) value = 1;
    return value;
}

/* Push a symbol onto the exclusion set.  The set can never hold more than the
 * 256 distinct byte values, so the guard below is unreachable on any stream;
 * it exists so a corrupted model can never scribble past the array. */
static void ha_hsc_exclude(ha_hsc *m, uint8_t symbol)
{
    if (m->excluded_count >= 0x100) {
        m->failed = true;
        return;
    }
    m->excluded_list[m->excluded_count] = symbol;
    ++m->excluded_count;
    m->excluded[symbol] = 1;
}

static int32_t ha_hsc_decode_no_exclusion(ha_hsc *m, int32_t context)
{
    int32_t escape = ha_hsc_escape_frequency(m, m->low_count[context], context);
    int32_t total = m->total[context];
    int32_t shift = 0;
    uint32_t target;
    int32_t accumulated = 0;
    int32_t hit = 0;
    int32_t n;

    if (m->run_count >= 5) {
        shift = ((total < 5) && (m->run_count == 10)) ? 2 : 1;
    }
    total = total << shift;

    target = ha_arith_target(&m->arith, (uint32_t)(total + escape));
    n = context;
    while (n != HA_NIL) {
        if ((int32_t)(target >> shift) < (accumulated + m->freq[n])) {
            hit = m->freq[n] << shift;
            break;
        }
        accumulated += m->freq[n];
        n = m->symbol_next[n];
    }
    accumulated = accumulated << shift;

    m->escape_depth = 1;
    if (n == HA_NIL) {
        int32_t last = context;
        int32_t k;
        ha_arith_update(&m->arith, (uint32_t)total, (uint32_t)(total + escape),
                        (uint32_t)(total + escape));
        if ((m->total[context] == 1) && (m->increment[ha_hsc_inc_slot(m, context)] < 0x20)) {
            ++m->increment[ha_hsc_inc_slot(m, context)];
        }
        for (k = context; k != HA_NIL; k = m->symbol_next[k]) {
            last = k;
            ha_hsc_exclude(m, m->symbol[k]);
        }
        m->escape_context[0] = context | 0x8000;
        m->escape_node[0] = last;
        m->run_count = 0;
        return HA_ESCAPE;
    }
    ha_arith_update(&m->arith, (uint32_t)accumulated,
                    (uint32_t)(accumulated + hit), (uint32_t)(total + escape));
    if ((m->total[context] == 1) && (m->increment[ha_hsc_inc_slot(m, context)] != 0)) {
        --m->increment[ha_hsc_inc_slot(m, context)];
    }
    m->escape_context[0] = context;
    m->escape_node[0] = n;
    if (m->run_count < 10) ++m->run_count;
    return m->symbol[n];
}

static int32_t ha_hsc_decode_with_exclusion(ha_hsc *m, int32_t context)
{
    int32_t total = 0;
    int32_t low_count = 0;
    int32_t escape;
    uint32_t target;
    int32_t accumulated = 0;
    int32_t hit = 0;
    int32_t found = HA_NIL;
    int32_t n;

    for (n = context; n != HA_NIL; n = m->symbol_next[n]) {
        if (m->excluded[m->symbol[n]] == 0) {
            total += m->freq[n];
            if (m->freq[n] < 3) ++low_count;
        }
    }
    escape = ha_hsc_escape_frequency(m, low_count, context);
    target = ha_arith_target(&m->arith, (uint32_t)(total + escape));

    for (n = context; n != HA_NIL; n = m->symbol_next[n]) {
        if (m->excluded[m->symbol[n]] == 0) {
            if ((int32_t)target < (accumulated + m->freq[n])) {
                hit = m->freq[n];
                found = n;
                break;
            }
            accumulated += m->freq[n];
        }
    }

    if (found == HA_NIL) {
        /* "last" starts at 0 here, not at the context, and it tracks every
         * node in the chain including excluded ones - the new symbol is
         * appended after the physically last slot.  Deliberate. */
        int32_t last = 0;
        int32_t k;
        ha_arith_update(&m->arith, (uint32_t)total, (uint32_t)(total + escape),
                        (uint32_t)(total + escape));
        if ((m->total[context] == 1) && (m->increment[ha_hsc_inc_slot(m, context)] < 0x20)) {
            ++m->increment[ha_hsc_inc_slot(m, context)];
        }
        for (k = context; k != HA_NIL; k = m->symbol_next[k]) {
            last = k;
            if (m->excluded[m->symbol[k]] == 0) ha_hsc_exclude(m, m->symbol[k]);
        }
        if (m->escape_depth >= 8) {
            m->failed = true;
            return HA_ESCAPE;
        }
        m->escape_context[m->escape_depth] = context | 0x8000;
        m->escape_node[m->escape_depth] = last;
        ++m->escape_depth;
        return HA_ESCAPE;
    }
    ha_arith_update(&m->arith, (uint32_t)accumulated,
                    (uint32_t)(accumulated + hit), (uint32_t)(total + escape));
    if ((m->total[context] == 1) && (m->increment[ha_hsc_inc_slot(m, context)] != 0)) {
        --m->increment[ha_hsc_inc_slot(m, context)];
    }
    if (m->escape_depth >= 8) {
        m->failed = true;
        return HA_ESCAPE;
    }
    m->escape_node[m->escape_depth] = found;
    m->escape_context[m->escape_depth] = context;
    ++m->escape_depth;
    /* no cap on run_count in this path, unlike the no-exclusion one */
    ++m->run_count;
    return m->symbol[found];
}

/* Order -1: a flat model over the not-yet-excluded bytes plus one slot that
 * ends the stream (returns 0x100). */
static int32_t ha_hsc_decode_fallback(ha_hsc *m)
{
    int32_t total = 0x101 - m->excluded_count;
    uint32_t target = ha_arith_target(&m->arith, (uint32_t)total);
    int32_t accumulated = 0;
    int32_t i = 0;
    while (i < 0x100) {
        if (m->excluded[i] == 0) {
            if ((int32_t)target < (accumulated + 1)) break;
            ++accumulated;
        }
        ++i;
    }
    ha_arith_update(&m->arith, (uint32_t)accumulated, (uint32_t)(accumulated + 1),
                    (uint32_t)total);
    return i;
}

static void ha_hsc_touch(ha_hsc *m, int32_t context)
{
    if (context == m->lru_head) return;
    if (context == m->lru_tail) {
        m->lru_tail = m->lru_prev[context];
    } else {
        m->lru_prev[m->lru_next[context]] = m->lru_prev[context];
        m->lru_next[m->lru_prev[context]] = m->lru_next[context];
    }
    m->lru_prev[m->lru_head] = (uint16_t)context;
    m->lru_next[context] = (uint16_t)m->lru_head;
    m->lru_head = context;
}

/* The symbol pool is full: find a context that is not on the current escape
 * path and merge its rarest symbols away.  This is part of the bitstream
 * contract - the encoder does exactly the same thing at the same moment - not
 * housekeeping that can be reordered. */
static void ha_hsc_reclaim_symbols(ha_hsc *m)
{
    int32_t guard = 0;
    for (;;) {
        int32_t i;
        int32_t c;
        int32_t lowest;
        int32_t previous;
        int32_t n;

        do {
            ++m->pool_scan;
            if (m->pool_scan == HA_CONTEXTS) m->pool_scan = 0;
            if (++guard > (HA_CONTEXTS * 4)) {
                /* Cannot happen on a well-formed stream; refuse rather than
                 * spin forever on a corrupted one. */
                m->failed = true;
                return;
            }
        } while (m->symbol_next[m->pool_scan] == HA_NIL);

        /* Note the inclusive bound: slot [escape_depth] is read as well as
         * [0 .. escape_depth-1].  The reference does this and the extra slot
         * holds a stale entry from an earlier, deeper escape (zero on the
         * first call), which at worst protects one more context from being
         * harvested.  Kept as-is. */
        i = 0;
        while (i <= m->escape_depth) {
            if ((m->escape_context[i] & 0x7fff) == m->pool_scan) break;
            ++i;
        }
        if (i <= m->escape_depth) continue;

        c = m->pool_scan;
        lowest = m->freq[c];
        for (n = m->symbol_next[c]; n != HA_NIL; n = m->symbol_next[n]) {
            if (m->freq[n] < lowest) lowest = m->freq[n];
        }
        ++lowest;

        if (m->freq[c] < lowest) {
            int32_t after;
            n = m->symbol_next[c];
            while ((m->freq[n] < lowest) && (m->symbol_next[n] != HA_NIL)) {
                n = m->symbol_next[n];
            }
            m->freq[c] = m->freq[n];
            m->symbol[c] = m->symbol[n];
            after = m->symbol_next[n];
            m->symbol_next[n] = (uint16_t)m->free_symbol;
            m->free_symbol = m->symbol_next[c];
            m->symbol_next[c] = (uint16_t)after;
            if (after == HA_NIL) {
                m->novel[c] = 0;
                m->total[c] = m->freq[c];
                m->low_count[c] = (uint8_t)((m->freq[c] < 3) ? 1 : 0);
                return;
            }
        }

        m->freq[c] = (uint16_t)(m->freq[c] / lowest);
        m->total[c] = m->freq[c];
        m->low_count[c] = (uint8_t)((m->freq[c] < 3) ? 1 : 0);
        m->novel[c] = 0;
        previous = c;
        n = m->symbol_next[c];
        while (n != HA_NIL) {
            /* the drop test is on the ORIGINAL frequency, before dividing, and
             * every surviving symbol counts as novel again */
            if (m->freq[n] < lowest) {
                m->symbol_next[previous] = m->symbol_next[n];
                m->symbol_next[n] = (uint16_t)m->free_symbol;
                m->free_symbol = n;
                n = m->symbol_next[previous];
                continue;
            }
            m->novel[c] = (uint8_t)(m->novel[c] + 1);
            m->freq[n] = (uint16_t)(m->freq[n] / lowest);
            m->total[c] = (uint16_t)(m->total[c] + m->freq[n]);
            if (m->freq[n] < 3) m->low_count[c] = (uint8_t)(m->low_count[c] + 1);
            previous = n;
            n = m->symbol_next[n];
        }
        return;
    }
}

static void ha_hsc_update_model(ha_hsc *m, int32_t symbol)
{
    while (m->escape_depth) {
        int32_t node;
        int32_t context;
        int32_t total;
        int32_t divisor;

        --m->escape_depth;
        node = m->escape_node[m->escape_depth];
        context = m->escape_context[m->escape_depth];
        if ((context & 0x8000) == 0) {
            ++m->freq[node];
            if (m->freq[node] == 3) --m->low_count[context];
        } else {
            context &= 0x7fff;
            if (m->free_symbol == HA_NIL) {
                ha_hsc_reclaim_symbols(m);
                if (m->failed) return;
                if (m->free_symbol == HA_NIL) {
                    m->failed = true;
                    return;
                }
            }
            m->symbol_next[node] = (uint16_t)m->free_symbol;
            node = m->symbol_next[node];
            m->free_symbol = m->symbol_next[m->free_symbol];
            m->symbol_next[node] = (uint16_t)HA_NIL;
            m->freq[node] = 1;
            m->symbol[node] = (uint8_t)symbol;
            m->novel[context] = (uint8_t)(m->novel[context] + 1);
            m->low_count[context] = (uint8_t)(m->low_count[context] + 1);
        }
        ++m->total[context];
        total = m->total[context];
        divisor = m->novel[context] + 1;
        if ((m->freq[node] * 2) < (total / divisor)) {
            /* deliberately wraps from 0 to 255 */
            m->rescale[context] = (uint8_t)(m->rescale[context] - 1);
        } else if (m->rescale[context] < 4) {
            ++m->rescale[context];
        }
        if ((m->rescale[context] == 0) || (m->total[context] > HA_MAX_TOTAL)) {
            int32_t n;
            m->rescale[context] = (uint8_t)(m->rescale[context] + 1);
            m->low_count[context] = 0;
            m->total[context] = 0;
            for (n = context; n != HA_NIL; n = m->symbol_next[n]) {
                if (m->freq[n] < 2) {
                    ++m->total[context];
                    ++m->low_count[context];
                } else {
                    m->freq[n] = (uint16_t)(m->freq[n] >> 1);
                    m->total[context] = (uint16_t)(m->total[context] + m->freq[n]);
                    if (m->freq[n] < 3) ++m->low_count[context];
                }
            }
        }
    }
}

static void ha_hsc_add_context(ha_hsc *m, int32_t order, int32_t symbol)
{
    int32_t c = m->lru_tail;
    int32_t h;

    /* the reference writes lru_prev[head] before reading lru_prev[c]; with
     * 10000 slots head != tail always, so the order is harmless - but keep it */
    m->lru_prev[m->lru_head] = (uint16_t)c;
    m->lru_tail = m->lru_prev[c];
    m->lru_next[c] = (uint16_t)m->lru_head;
    m->lru_head = c;

    if (m->order[c] != 0xffU) {
        if (m->order[c] == 4) {
            --m->order4_budget;
            /* after 2500 order-4 recycles the model permanently drops to 3 */
            if (m->order4_budget == 0) m->max_order = 3;
        }
        h = ha_hsc_hash_of(m, m->context_bytes + (c * 4), m->order[c]);
        if (m->buckets[h] == c) {
            m->buckets[h] = m->hash_next[c];
        } else {
            int32_t p = m->buckets[h];
            while ((p != HA_NIL) && (m->hash_next[p] != c)) p = m->hash_next[p];
            if (p != HA_NIL) m->hash_next[p] = m->hash_next[c];
        }
        if (m->symbol_next[c] != HA_NIL) {
            int32_t n = m->symbol_next[c];
            while (m->symbol_next[n] != HA_NIL) n = m->symbol_next[n];
            m->symbol_next[n] = (uint16_t)m->free_symbol;
            m->free_symbol = m->symbol_next[c];
        }
    }

    m->symbol_next[c] = (uint16_t)HA_NIL;
    m->low_count[c] = 1;
    m->total[c] = 1;
    m->freq[c] = 1;
    m->symbol[c] = (uint8_t)symbol;
    m->rescale[c] = 4;
    m->novel[c] = 0;
    m->order[c] = (uint8_t)order;
    m->context_bytes[c * 4 + 0] = m->context[0];
    m->context_bytes[c * 4 + 1] = m->context[1];
    m->context_bytes[c * 4 + 2] = m->context[2];
    m->context_bytes[c * 4 + 3] = m->context[3];
    h = ha_hsc_hash_of(m, m->context, order);
    m->hash_next[c] = m->buckets[h];
    m->buckets[h] = (uint16_t)c;
}

static bool ha_hsc_run(ha_hsc *m, ha_sink *sink)
{
    while (sink->pos < sink->capacity) {
        int32_t context = ha_hsc_find_deepest(m);
        const int32_t order = (context == HA_NIL) ? 0 : (m->order[context] + 1);
        int32_t max_order = m->max_order + 1;
        int32_t symbol = 0;

        while (context != HA_NIL) {
            symbol = (m->excluded_count == 0) ? ha_hsc_decode_no_exclusion(m, context)
                                              : ha_hsc_decode_with_exclusion(m, context);
            if (m->failed) return false;
            if (symbol != HA_ESCAPE) {
                ha_hsc_touch(m, context);
                break;
            }
            context = ha_hsc_find_next(m);
        }
        if (context == HA_NIL) symbol = ha_hsc_decode_fallback(m);
        /* 0x100 out of the order -1 model is the end-of-stream marker.  It can
         * only appear where the member header said more bytes were coming, so
         * it is a short stream: fail. */
        if (symbol == HA_ESCAPE) return false;

        ha_hsc_update_model(m, symbol);
        if (m->failed) return false;

        /* one new context per order above the deepest that matched */
        while (order < max_order) {
            --max_order;
            ha_hsc_add_context(m, max_order, symbol);
        }

        if (!ha_sink_put(sink, (uint8_t)symbol)) return false;
        m->context[3] = m->context[2];
        m->context[2] = m->context[1];
        m->context[1] = m->context[0];
        m->context[0] = (uint8_t)symbol;
    }
    return true;
}

/* ================================================================== ASC == */

#define ASC_WSIZE   31200 /* 0x79e0 */
#define ASC_MINLEN  3
#define ASC_SLCODES 16
#define ASC_LLLEN   16
#define ASC_LTCODES 64
#define ASC_CTCODES 256
#define ASC_PTCODES 16
#define ASC_LTSTEP  8
#define ASC_CTSTEP  1
#define ASC_PTSTEP  24
#define ASC_TTSTEP  40
#define ASC_LTMAX   6000
#define ASC_CTMAX   1000
#define ASC_PTMAX   6000
#define ASC_TTMAX   6000
#define ASC_CPLEN   8
#define ASC_LPLEN   4
/* code SLCODES-1 is reserved for the longest match: 15 + 48 * 16 = 783 */
#define ASC_MAXLEN  ((ASC_SLCODES - 1) + ((ASC_LTCODES - ASC_SLCODES) * ASC_LLLEN))

/* Implicit binary cumulative-frequency tree over 2 * count 16-bit counters;
 * leaves live at [count .. 2*count-1], the running total at [1]. */
typedef struct asc_tree {
    int32_t count;
    uint16_t node[ASC_CTCODES * 2];
} asc_tree;

static void asc_tree_rebuild(asc_tree *t)
{
    int32_t i;
    for (i = t->count - 1; i > 0; --i) {
        t->node[i] = (uint16_t)(t->node[i * 2] + t->node[i * 2 + 1]);
    }
}

static void asc_tree_init(asc_tree *t, int32_t count, uint16_t leaf)
{
    int32_t i;
    t->count = count;
    xx_rt_memset(t->node, 0, (size_t)(count * 2) * sizeof(t->node[0]));
    if (leaf != 0) {
        for (i = count; i < (count * 2); ++i) t->node[i] = leaf;
    }
    asc_tree_rebuild(t);
}

static uint16_t asc_tree_total(const asc_tree *t)
{
    return t->node[1];
}

static uint16_t asc_tree_freq(const asc_tree *t, int32_t index)
{
    if ((index < 0) || (index >= t->count)) return 0;
    return t->node[t->count + index];
}

/* halve every leaf above 1, then recompute the interior */
static void asc_tree_rescale(asc_tree *t)
{
    int32_t i;
    for (i = (t->count * 2) - 1; i >= t->count; --i) {
        if (t->node[i] > 1) t->node[i] = (uint16_t)(t->node[i] >> 1);
    }
    asc_tree_rebuild(t);
}

static void asc_tree_add(asc_tree *t, int32_t index, uint16_t step,
                         uint16_t max_total)
{
    int32_t i;
    if ((index < 0) || (index >= t->count)) return;
    i = t->count + index;
    while (i != 0) {
        t->node[i] = (uint16_t)(t->node[i] + step);
        i >>= 1;
    }
    if (t->node[1] >= max_total) asc_tree_rescale(t);
}

static void asc_tree_remove(asc_tree *t, int32_t index)
{
    int32_t i;
    uint16_t value;
    if ((index < 0) || (index >= t->count)) return;
    i = t->count + index;
    value = t->node[i];
    while (i != 0) {
        t->node[i] = (uint16_t)(t->node[i] - value);
        i >>= 1;
    }
}

/* Descend from node 2; returns the leaf index, *low gets its cumulative low. */
static int32_t asc_tree_find(const asc_tree *t, uint32_t target, uint32_t *low)
{
    int32_t node = 2;
    uint32_t cum = 0;
    for (;;) {
        if ((cum + t->node[node]) <= target) {
            cum += t->node[node];
            ++node;
        }
        if (node > (t->count - 1)) break;
        node <<= 1;
        if (node >= (t->count * 2)) return -1; /* cannot happen on a sane tree */
    }
    if (node >= (t->count * 2)) return -1;
    *low = cum;
    return node - t->count;
}

typedef struct ha_asc {
    ha_arith arith;
    uint8_t window[ASC_WSIZE];
    int32_t window_pos;
    asc_tree length;
    asc_tree new_length;
    asc_tree position;
    asc_tree chars;
    asc_tree new_chars;
    uint16_t literal_weight[4];
    uint16_t match_weight[4];
    int32_t context;
    int32_t out_size;
    uint16_t pos_limit;
    int32_t pos_code;
    uint16_t char_escape;
    uint16_t length_escape;
} ha_asc;

static void ha_asc_init(ha_asc *m, const uint8_t *data, size_t size)
{
    int32_t i;

    xx_rt_memset(m, 0, sizeof(*m));
    ha_arith_init(&m->arith, data, size);

    /* The window starts as 31200 zero bytes and the reference lets a match
     * read the part of it that has not been written yet, which yields those
     * zeroes.  That is legal in HA - the encoder never emits such a match, but
     * the decoder does not police it - so we match the behaviour instead of
     * rejecting the stream. */
    m->window_pos = 0;

    asc_tree_init(&m->length, ASC_LTCODES, 0);
    asc_tree_init(&m->new_length, ASC_LTCODES, 1);
    asc_tree_init(&m->position, ASC_PTCODES, 0);
    asc_tree_init(&m->chars, ASC_CTCODES, 0);
    asc_tree_init(&m->new_chars, ASC_CTCODES, 1);
    asc_tree_add(&m->position, 0, ASC_PTSTEP, ASC_PTMAX);

    for (i = 0; i < 4; ++i) {
        m->literal_weight[i] = ASC_TTSTEP;
        m->match_weight[i] = ASC_TTSTEP;
    }
    m->context = 0;
    m->out_size = 0;
    m->pos_limit = 1;
    m->pos_code = 1;
    m->char_escape = 1;
    m->length_escape = ASC_LTSTEP;
}

static void ha_asc_context_rescale(ha_asc *m, int32_t context)
{
    uint16_t value = (uint16_t)(m->literal_weight[context] >> 1);
    m->literal_weight[context] = (uint16_t)(value ? value : 1);
    value = (uint16_t)(m->match_weight[context] >> 1);
    m->match_weight[context] = (uint16_t)(value ? value : 1);
}

static bool ha_asc_put_literal(ha_asc *m, ha_sink *sink, uint8_t value)
{
    m->window[m->window_pos] = value;
    if (!ha_sink_put(sink, value)) return false;
    ++m->window_pos;
    if (m->window_pos == ASC_WSIZE) m->window_pos = 0;
    return true;
}

static bool ha_asc_put_match(ha_asc *m, ha_sink *sink, int32_t length,
                             int32_t position)
{
    int32_t source;
    int32_t i;
    if (position < m->window_pos) {
        source = m->window_pos - position - 1;
    } else {
        source = m->window_pos + ASC_WSIZE - position - 1;
    }
    if ((source < 0) || (source >= ASC_WSIZE)) return false;
    for (i = 0; i < length; ++i) {
        const uint8_t value = m->window[source];
        m->window[m->window_pos] = value;
        if (!ha_sink_put(sink, value)) return false;
        ++m->window_pos;
        if (m->window_pos == ASC_WSIZE) m->window_pos = 0;
        ++source;
        if (source == ASC_WSIZE) source = 0;
    }
    return true;
}

/* Two-level "seen / not yet seen" model: a hit in the seen tree codes the byte
 * directly, an escape codes it from the not-yet-seen tree, which then loses the
 * symbol and bumps its still-unseen neighbours. */
static int32_t ha_asc_decode_char(ha_asc *m)
{
    uint32_t low = 0;
    int32_t value = 0;
    const uint32_t seen = asc_tree_total(&m->chars);
    const uint32_t total = seen + m->char_escape;
    uint32_t target = ha_arith_target(&m->arith, total);

    if (target < seen) {
        value = asc_tree_find(&m->chars, target, &low);
        if (value < 0) return -1;
        ha_arith_update(&m->arith, low, low + asc_tree_freq(&m->chars, value),
                        total);
    } else {
        uint32_t new_total;
        int32_t first;
        int32_t bound;
        int32_t i;
        ha_arith_update(&m->arith, seen, total, total);
        new_total = asc_tree_total(&m->new_chars);
        if (new_total == 0) return -1;
        target = ha_arith_target(&m->arith, new_total);
        value = asc_tree_find(&m->new_chars, target, &low);
        if (value < 0) return -1;
        ha_arith_update(&m->arith, low,
                        low + asc_tree_freq(&m->new_chars, value), new_total);
        asc_tree_remove(&m->new_chars, value);
        if (asc_tree_total(&m->new_chars) == 0) {
            m->char_escape = 0;
        } else {
            m->char_escape = (uint16_t)(m->char_escape + 1);
        }
        /* NOTE: the upper bound is exclusive here.  HA's encoder is inclusive -
         * a real asymmetry in the original, not a transcription slip.  Making
         * the two agree desynchronises the coder. */
        first = (value < ASC_CPLEN) ? 0 : (value - ASC_CPLEN);
        bound = ((value + ASC_CPLEN) > (ASC_CTCODES - 2)) ? (ASC_CTCODES - 1)
                                                          : (value + ASC_CPLEN);
        for (i = first; i < bound; ++i) {
            if (asc_tree_freq(&m->new_chars, i) != 0) {
                asc_tree_add(&m->new_chars, i, ASC_CTSTEP, ASC_CTMAX);
            }
        }
    }

    asc_tree_add(&m->chars, value, ASC_CTSTEP, ASC_CTMAX);
    if (asc_tree_freq(&m->chars, value) == (3 * ASC_CTSTEP)) {
        m->char_escape = (uint16_t)((m->char_escape < 2) ? 1
                                                         : (m->char_escape - 1));
    }

    return value;
}

static int32_t ha_asc_decode_length(ha_asc *m)
{
    uint32_t low = 0;
    int32_t code = 0;
    int32_t length;
    const uint32_t seen = asc_tree_total(&m->length);
    const uint32_t total = seen + m->length_escape;
    uint32_t target = ha_arith_target(&m->arith, total);

    if (target < seen) {
        code = asc_tree_find(&m->length, target, &low);
        if (code < 0) return -1;
        ha_arith_update(&m->arith, low, low + asc_tree_freq(&m->length, code),
                        total);
    } else {
        uint32_t new_total;
        int32_t first;
        int32_t bound;
        int32_t i;
        ha_arith_update(&m->arith, seen, total, total);
        new_total = asc_tree_total(&m->new_length);
        if (new_total == 0) return -1;
        target = ha_arith_target(&m->arith, new_total);
        code = asc_tree_find(&m->new_length, target, &low);
        if (code < 0) return -1;
        ha_arith_update(&m->arith, low,
                        low + asc_tree_freq(&m->new_length, code), new_total);
        asc_tree_remove(&m->new_length, code);
        if (asc_tree_total(&m->new_length) == 0) {
            m->length_escape = 0;
        } else {
            m->length_escape = (uint16_t)(m->length_escape + ASC_LTSTEP);
        }
        /* exclusive upper bound again - same deliberate asymmetry */
        first = (code < ASC_LPLEN) ? 0 : (code - ASC_LPLEN);
        bound = ((code + ASC_LPLEN) > (ASC_LTCODES - 2)) ? (ASC_LTCODES - 1)
                                                         : (code + ASC_LPLEN);
        for (i = first; i < bound; ++i) {
            if (asc_tree_freq(&m->new_length, i) != 0) {
                asc_tree_add(&m->new_length, i, 1, ASC_LTMAX);
            }
        }
    }

    asc_tree_add(&m->length, code, ASC_LTSTEP, ASC_LTMAX);
    if (asc_tree_freq(&m->length, code) == (3 * ASC_LTSTEP)) {
        m->length_escape = (uint16_t)((m->length_escape < (ASC_LTSTEP + 1))
                                          ? 1
                                          : (m->length_escape - ASC_LTSTEP));
    }

    length = code;
    if (code == (ASC_SLCODES - 1)) {
        length = ASC_MAXLEN;
    } else if (code > (ASC_SLCODES - 1)) {
        const uint32_t extra = ha_arith_target(&m->arith, ASC_LLLEN);
        if (extra >= (uint32_t)ASC_LLLEN) return -1;
        ha_arith_update(&m->arith, extra, extra + 1, ASC_LLLEN);
        length = ((code - ASC_SLCODES) * ASC_LLLEN) + (int32_t)extra +
                 (ASC_SLCODES - 1);
    }

    return length + ASC_MINLEN;
}

/* 16 buckets that go live as the output grows; bucket k covers [2^(k-1), 2^k)
 * with uniform extra bits, and the top live bucket is clamped to the output. */
static int32_t ha_asc_decode_position(ha_asc *m)
{
    uint32_t low = 0;
    uint32_t total;
    uint32_t target;
    int32_t code;
    int32_t position;

    while (m->pos_limit < m->out_size) {
        if (m->pos_code < ASC_PTCODES) {
            asc_tree_add(&m->position, m->pos_code, ASC_PTSTEP, ASC_PTMAX);
        }
        ++m->pos_code;
        m->pos_limit = (uint16_t)(m->pos_limit << 1);
        if (m->pos_limit == 0) break;
    }

    total = asc_tree_total(&m->position);
    if (total == 0) return -1;
    target = ha_arith_target(&m->arith, total);
    code = asc_tree_find(&m->position, target, &low);
    if (code < 0) return -1;
    ha_arith_update(&m->arith, low, low + asc_tree_freq(&m->position, code),
                    total);
    asc_tree_add(&m->position, code, ASC_PTSTEP, ASC_PTMAX);

    position = code;
    if (code > 1) {
        const int32_t base = (int32_t)((uint16_t)(1 << code) >> 1);
        uint32_t range = (uint32_t)base;
        uint32_t extra;
        /* the top live bucket is short: it only spans what has been produced */
        if (base == (int32_t)(m->pos_limit >> 1)) {
            range = (uint32_t)(uint16_t)(m->out_size - (m->pos_limit >> 1));
        }
        if (range == 0) return -1;
        extra = ha_arith_target(&m->arith, range);
        if (extra >= range) return -1;
        ha_arith_update(&m->arith, extra, extra + 1, range);
        position = (int32_t)extra + base;
    }

    if ((position < 0) || (position >= ASC_WSIZE)) return -1;

    return position;
}

static bool ha_asc_run(ha_asc *m, ha_sink *sink)
{
    while (sink->pos < sink->capacity) {
        const uint32_t sum = (uint32_t)m->literal_weight[m->context] +
                             m->match_weight[m->context];
        const uint32_t target = ha_arith_target(&m->arith, sum + 1);

        if (target < m->literal_weight[m->context]) {
            int32_t value;
            ha_arith_update(&m->arith, 0, m->literal_weight[m->context], sum + 1);
            m->literal_weight[m->context] =
                (uint16_t)(m->literal_weight[m->context] + ASC_TTSTEP);
            /* the overflow test is on the sum BEFORE the bump - deliberate */
            if (sum > (uint32_t)(ASC_TTMAX - 1)) ha_asc_context_rescale(m, m->context);
            m->context = (m->context * 2) & 3;

            value = ha_asc_decode_char(m);
            if (value < 0) return false;
            if (!ha_asc_put_literal(m, sink, (uint8_t)value)) return false;
            if (m->out_size < ASC_WSIZE) ++m->out_size;
            continue;
        }

        /* the one slot above the two weights ends the stream */
        if (sum <= target) return false;

        ha_arith_update(&m->arith, m->literal_weight[m->context], sum, sum + 1);
        m->match_weight[m->context] =
            (uint16_t)(m->match_weight[m->context] + ASC_TTSTEP);
        if (sum > (uint32_t)(ASC_TTMAX - 1)) ha_asc_context_rescale(m, m->context);
        m->context = ((m->context * 2) & 3) | 1;

        {
            /* position first, then length - the order is part of the stream */
            const int32_t position = ha_asc_decode_position(m);
            int32_t length;
            if (position < 0) return false;
            length = ha_asc_decode_length(m);
            if (length < 0) return false;

            if (m->out_size < ASC_WSIZE) {
                m->out_size += length;
                if (m->out_size > ASC_WSIZE) m->out_size = ASC_WSIZE;
            }

            if (!ha_asc_put_match(m, sink, length, position)) return false;
        }
    }
    return true;
}

/* ============================================================== entries == */

static bool ha_check_args(const uint8_t *input, size_t input_size,
                          const uint8_t *output, size_t output_size,
                          size_t *written)
{
    if (!written) return false;
    *written = 0U;
    if (!input || (input_size == 0U)) return false;
    if (!output || (output_size == 0U)) return false;
    return true;
}

bool xx_ha_asc_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written)
{
    ha_asc *m;
    ha_sink sink;
    bool ok;

    if (!ha_check_args(input, input_size, output, output_size, written)) {
        return false;
    }

    m = (ha_asc *)xx_mem_alloc(sizeof(ha_asc));
    if (!m) return false;
    ha_asc_init(m, input, input_size);

    sink.data = output;
    sink.capacity = output_size;
    sink.pos = 0U;

    ok = ha_asc_run(m, &sink);
    xx_mem_free(m);

    /* Never report success on a short decode: the member header gave the exact
     * plaintext length, so anything else is a broken stream. */
    if (!ok || (sink.pos != output_size)) {
        *written = 0U;
        return false;
    }
    *written = sink.pos;
    return true;
}

bool xx_ha_hsc_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written)
{
    ha_hsc *m;
    ha_sink sink;
    bool ok;

    if (!ha_check_args(input, input_size, output, output_size, written)) {
        return false;
    }

    m = (ha_hsc *)xx_mem_alloc(sizeof(ha_hsc));
    if (!m) return false;
    ha_hsc_init(m, input, input_size);

    sink.data = output;
    sink.capacity = output_size;
    sink.pos = 0U;

    ok = ha_hsc_run(m, &sink);
    xx_mem_free(m);

    if (!ok || (sink.pos != output_size)) {
        *written = 0U;
        return false;
    }
    *written = sink.pos;
    return true;
}

bool xx_ha_decode_memory_method(unsigned method, const uint8_t *input,
                                size_t input_size, uint8_t *output,
                                size_t output_size, size_t *written)
{
    if (method == 1U) {
        return xx_ha_asc_decode_memory(input, input_size, output, output_size,
                                       written);
    }
    if (method == 2U) {
        return xx_ha_hsc_decode_memory(input, input_size, output, output_size,
                                       written);
    }
    if (written) *written = 0U;
    return false;
}
