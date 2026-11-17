/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_byteset.c
 * @brief Byte-set bitmap, strategy choice and table building, the portable
 *        reference search and run-time dispatch.
 */

#include "platforms/xx_byteset_platform.h"
#include "xxfclib/global/xx_global.h"

/* ------------------------------------------------------------ reference */

size_t xx_byteset_find_scalar(const xx_byteset *set, const uint8_t *data, size_t size, size_t from)
{
    for (; from < size; ++from)
        if (xx_byteset_member(set, data[from])) return from;
    return size;
}

/* ---------------------------------------------------------------- bitmap */

static unsigned xx_byteset_popcount16(unsigned value)
{
    unsigned count = 0;
    value &= 0xffffU;
    while (value) {
        value &= value - 1U;
        ++count;
    }
    return count;
}

/* Row h of the 16 x 16 membership matrix: bit l set when (h << 4 | l) is a member. */
static unsigned xx_byteset_row(const xx_byteset *set, unsigned h)
{
    return (unsigned)(set->bits[h >> 2] >> ((h & 3U) * 16U)) & 0xffffU;
}

void xx_byteset_clear(xx_byteset *set)
{
    unsigned i;
    if (!set) return;
    for (i = 0; i < 4; ++i) set->bits[i] = 0;
    for (i = 0; i < 16; ++i) set->table_low[i] = set->table_high[i] = 0;
    for (i = 0; i < 4; ++i) set->values[i] = 0;
    set->strategy = XX_BYTESET_STRATEGY_EMPTY;
    set->reserved[0] = set->reserved[1] = set->reserved[2] = 0;
}

void xx_byteset_add(xx_byteset *set, uint8_t value)
{
    if (!set) return;
    set->bits[value >> 6] |= (uint64_t)1 << (value & 63U);
    set->strategy = XX_BYTESET_STRATEGY_UNPREPARED;
}

size_t xx_byteset_count(const xx_byteset *set)
{
    size_t count = 0;
    unsigned h;
    if (!set) return 0;
    for (h = 0; h < 16; ++h) count += xx_byteset_popcount16(xx_byteset_row(set, h));
    return count;
}

bool xx_byteset_contains(const xx_byteset *set, uint8_t value)
{
    return set && xx_byteset_member(set, value);
}

/* ------------------------------------------------------- table building */

/* Whether xx_byteset_find would run a pshufb kernel now: the AVX2 backend
 * always can, the SSE2 backend only with SSSE3 (xx_byteset_sse2_pshufb).
 * Only the choice between LOOKUP and EQ3 for three members depends on it;
 * both are exact on every backend, so the set stays valid on any CPU. */
static bool xx_byteset_pshufb_usable(void)
{
#ifdef XX_BYTESET_X86
    return xx_is_avx2_enabled() || (xx_is_sse2_enabled() && xx_byteset_sse2_pshufb());
#else
    return false;
#endif
}

/* One shufti bucket: every byte whose low nibble is in low and whose high
 * nibble is in high (bit masks over 0..15). */
typedef struct xx_byteset_bucket {
    unsigned low, high;
} xx_byteset_bucket;

/* Rows of the bytes the buckets accept. */
static void xx_byteset_accepted(const xx_byteset_bucket *buckets, unsigned count, unsigned accepted[16])
{
    unsigned h, j;
    for (h = 0; h < 16; ++h) {
        accepted[h] = 0;
        for (j = 0; j < count; ++j)
            if (buckets[j].high & (1U << h)) accepted[h] |= buckets[j].low;
    }
}

/* Partition of the members into buckets of lines with equal contents: rows
 * (by_rows, a bucket is one low-nibble mask times the high nibbles whose row
 * equals it) or columns. Returns the bucket count, at most 16. */
static unsigned xx_byteset_partition(const unsigned rows[16], bool by_rows, xx_byteset_bucket buckets[16])
{
    unsigned lines[16], count = 0, i, j;
    for (i = 0; i < 16; ++i) {
        lines[i] = 0;
        if (by_rows) lines[i] = rows[i];
        else
            for (j = 0; j < 16; ++j)
                if (rows[j] & (1U << i)) lines[i] |= 1U << j;
    }
    for (i = 0; i < 16; ++i) {
        if (!lines[i]) continue;
        for (j = 0; j < count; ++j)
            if ((by_rows ? buckets[j].low : buckets[j].high) == lines[i]) break;
        if (j == count) {
            buckets[count].low = by_rows ? lines[i] : 0;
            buckets[count].high = by_rows ? 0 : lines[i];
            ++count;
        }
        if (by_rows) buckets[j].high |= 1U << i;
        else buckets[j].low |= 1U << i;
    }
    return count;
}

/* Greedy overlapping cover of the members by at most eight rectangles that
 * hold members only: each candidate is a nibble mask M (a row, a column, or
 * the intersection of two) with every line that contains M. Returns true
 * when every member is covered. */
static bool xx_byteset_cover(const unsigned rows[16], xx_byteset_bucket buckets[8], unsigned *count)
{
    xx_byteset_bucket candidates[2 * (16 + 120)];
    unsigned uncovered[16], columns[16], ncand = 0, h, l, i, j, used = 0;
    int pass;
    for (l = 0; l < 16; ++l) {
        columns[l] = 0;
        for (h = 0; h < 16; ++h)
            if (rows[h] & (1U << l)) columns[l] |= 1U << h;
    }
    for (pass = 0; pass < 2; ++pass) {
        const unsigned *lines = pass == 0 ? rows : columns;
        for (i = 0; i < 16; ++i) {
            for (j = i; j < 16; ++j) {
                unsigned mask = lines[i] & lines[j], across = 0, k;
                if (!mask) continue;
                if (j != i && (mask == lines[i] || mask == lines[j])) continue; /* already a single line */
                for (k = 0; k < 16; ++k)
                    if ((lines[k] & mask) == mask) across |= 1U << k;
                candidates[ncand].low = pass == 0 ? mask : across;
                candidates[ncand].high = pass == 0 ? across : mask;
                ++ncand;
            }
        }
    }
    for (h = 0; h < 16; ++h) uncovered[h] = rows[h];
    while (used < 8) {
        unsigned best = 0, best_gain = 0, gain, left = 0;
        for (h = 0; h < 16; ++h) left |= uncovered[h];
        if (!left) break;
        for (i = 0; i < ncand; ++i) {
            gain = 0;
            for (h = 0; h < 16; ++h)
                if (candidates[i].high & (1U << h)) gain += xx_byteset_popcount16(uncovered[h] & candidates[i].low);
            if (gain > best_gain) {
                best_gain = gain;
                best = i;
            }
        }
        if (!best_gain) break;
        buckets[used++] = candidates[best];
        for (h = 0; h < 16; ++h)
            if (candidates[best].high & (1U << h)) uncovered[h] &= ~candidates[best].low;
    }
    *count = used;
    for (h = 0; h < 16; ++h)
        if (uncovered[h]) return false;
    return true;
}

/* Merge the closest buckets of a partition until eight remain; the result
 * accepts a superset of the members. Returns the number of extra values. */
static unsigned xx_byteset_merge(xx_byteset_bucket *buckets, unsigned count)
{
    unsigned extra = 0;
    while (count > 8) {
        unsigned bi = 0, bj = 1, best = ~0U, i, j;
        for (i = 0; i < count; ++i) {
            for (j = i + 1; j < count; ++j) {
                unsigned before = xx_byteset_popcount16(buckets[i].low) * xx_byteset_popcount16(buckets[i].high) +
                                  xx_byteset_popcount16(buckets[j].low) * xx_byteset_popcount16(buckets[j].high);
                unsigned after = xx_byteset_popcount16(buckets[i].low | buckets[j].low) *
                                 xx_byteset_popcount16(buckets[i].high | buckets[j].high);
                if (after - before < best) {
                    best = after - before;
                    bi = i;
                    bj = j;
                }
            }
        }
        buckets[bi].low |= buckets[bj].low;
        buckets[bi].high |= buckets[bj].high;
        buckets[bj] = buckets[--count];
        extra += best;
    }
    return extra;
}

static void xx_byteset_reset_tables(xx_byteset *set)
{
    unsigned i;
    for (i = 0; i < 16; ++i) set->table_low[i] = set->table_high[i] = 0;
    for (i = 0; i < 4; ++i) set->values[i] = 0;
}

/* Fill the shufti tables; exact asks for an exact cover, otherwise the
 * smallest superset found. Returns false when no exact cover was found. */
static bool xx_byteset_build_shufti(xx_byteset *set, bool exact)
{
    xx_byteset_bucket by_rows[16], by_columns[16], cover[8];
    const xx_byteset_bucket *chosen;
    unsigned rows[16], accepted[16], nrows, ncolumns, ncover = 0, count, h, j, extra_rows, extra_columns;
    for (h = 0; h < 16; ++h) rows[h] = xx_byteset_row(set, h);
    nrows = xx_byteset_partition(rows, true, by_rows);
    ncolumns = xx_byteset_partition(rows, false, by_columns);
    if (nrows <= 8) {
        chosen = by_rows;
        count = nrows;
    } else if (ncolumns <= 8) {
        chosen = by_columns;
        count = ncolumns;
    } else if (xx_byteset_cover(rows, cover, &ncover)) {
        chosen = cover;
        count = ncover;
    } else {
        if (exact) return false;
        extra_rows = xx_byteset_merge(by_rows, nrows);
        extra_columns = xx_byteset_merge(by_columns, ncolumns);
        chosen = extra_rows <= extra_columns ? by_rows : by_columns;
        count = 8;
    }
    xx_byteset_reset_tables(set);
    for (j = 0; j < count; ++j) {
        for (h = 0; h < 16; ++h) {
            if (chosen[j].low & (1U << h)) set->table_low[h] |= (uint8_t)(1U << j);
            if (chosen[j].high & (1U << h)) set->table_high[h] |= (uint8_t)(1U << j);
        }
    }
    /* The vector test of byte b is table_low[b & 15] & table_high[b >> 4];
     * check it for all 256 values: no member may be rejected, and an exact
     * strategy may accept nothing else. */
    xx_byteset_accepted(chosen, count, accepted);
    for (h = 0; h < 16; ++h) {
        unsigned l, test = 0;
        for (l = 0; l < 16; ++l)
            if (set->table_low[l] & set->table_high[h]) test |= 1U << l;
        if (test != accepted[h] || (rows[h] & ~test) != 0) return false;
        if (exact && test != rows[h]) return false;
    }
    return true;
}

/* Truffle rows: bit (h & 7) of table_low[l] (h < 8) or table_high[l]
 * (h >= 8) is set when (h << 4 | l) is a member. Exact for every set. */
static void xx_byteset_build_truffle(xx_byteset *set)
{
    unsigned h, l;
    xx_byteset_reset_tables(set);
    for (h = 0; h < 16; ++h) {
        unsigned row = xx_byteset_row(set, h);
        for (l = 0; l < 16; ++l) {
            if (!(row & (1U << l))) continue;
            if (h < 8) set->table_low[l] |= (uint8_t)(1U << h);
            else set->table_high[l] |= (uint8_t)(1U << (h - 8));
        }
    }
}

/* LOOKUP: no two members share a low nibble. table_low[l] is the member with
 * low nibble l, or l ^ 1 when there is none: its low nibble is not l, so no
 * byte b with b & 15 == l equals it. A byte is a member exactly when
 * table_low[b & 15] == b. */
static bool xx_byteset_build_lookup(xx_byteset *set)
{
    unsigned b, l, used = 0;
    xx_byteset_reset_tables(set);
    for (l = 0; l < 16; ++l) set->table_low[l] = (uint8_t)(l ^ 1U);
    for (b = 0; b < 256; ++b) {
        if (!xx_byteset_member(set, (uint8_t)b)) continue;
        l = b & 15U;
        if (used & (1U << l)) return false;
        used |= 1U << l;
        set->table_low[l] = (uint8_t)b;
    }
    return true;
}

/* Members (or non-members) listed into values[], padded with the first. */
static unsigned xx_byteset_list(xx_byteset *set, bool members, unsigned limit)
{
    unsigned found = 0, b;
    xx_byteset_reset_tables(set);
    for (b = 0; b < 256; ++b) {
        if (xx_byteset_member(set, (uint8_t)b) != members) continue;
        if (found == limit) return limit + 1;
        set->values[found++] = (uint8_t)b;
    }
    for (b = found; b < 4; ++b) set->values[b] = set->values[0];
    return found;
}

/* One run of consecutive values with membership `members`: first value and
 * span in values[0..1]. */
static bool xx_byteset_build_range(xx_byteset *set, bool members)
{
    unsigned b = 0, first, last;
    xx_byteset_reset_tables(set);
    while (b < 256 && xx_byteset_member(set, (uint8_t)b) != members) ++b;
    if (b == 256) return false;
    first = b;
    while (b < 256 && xx_byteset_member(set, (uint8_t)b) == members) ++b;
    last = b - 1;
    while (b < 256 && xx_byteset_member(set, (uint8_t)b) != members) ++b;
    if (b != 256) return false;
    set->values[0] = (uint8_t)first;
    set->values[1] = (uint8_t)(last - first);
    return true;
}

bool xx_byteset_prepare_strategy(xx_byteset *set, int strategy)
{
    size_t count;
    bool ok;
    if (!set) return false;
    count = xx_byteset_count(set);
    set->strategy = XX_BYTESET_STRATEGY_UNPREPARED;
    switch (strategy) {
    case XX_BYTESET_STRATEGY_EMPTY: xx_byteset_reset_tables(set); ok = count == 0; break;
    case XX_BYTESET_STRATEGY_FULL: xx_byteset_reset_tables(set); ok = count == 256; break;
    case XX_BYTESET_STRATEGY_EQ1:
    case XX_BYTESET_STRATEGY_EQ2:
    case XX_BYTESET_STRATEGY_EQ3:
        ok = xx_byteset_list(set, true, 3) == (unsigned)(strategy - XX_BYTESET_STRATEGY_EQ1 + 1);
        break;
    case XX_BYTESET_STRATEGY_NE1:
    case XX_BYTESET_STRATEGY_NE2:
    case XX_BYTESET_STRATEGY_NE3:
        ok = xx_byteset_list(set, false, 3) == (unsigned)(strategy - XX_BYTESET_STRATEGY_NE1 + 1);
        break;
    case XX_BYTESET_STRATEGY_RANGE: ok = xx_byteset_build_range(set, true); break;
    case XX_BYTESET_STRATEGY_NOT_RANGE: ok = xx_byteset_build_range(set, false); break;
    case XX_BYTESET_STRATEGY_LOOKUP: ok = xx_byteset_build_lookup(set); break;
    case XX_BYTESET_STRATEGY_SHUFTI: ok = xx_byteset_build_shufti(set, true); break;
    case XX_BYTESET_STRATEGY_SHUFTI_FILTER: ok = xx_byteset_build_shufti(set, false); break;
    case XX_BYTESET_STRATEGY_TRUFFLE_ASCII: /* only table_low: no member from 0x80 */
        xx_byteset_build_truffle(set);
        ok = (set->bits[2] | set->bits[3]) == 0;
        break;
    case XX_BYTESET_STRATEGY_TRUFFLE: xx_byteset_build_truffle(set); ok = true; break;
    default: ok = false; break;
    }
    if (!ok) {
        xx_byteset_reset_tables(set);
        return false;
    }
    set->strategy = (uint8_t)strategy;
    return true;
}

/* The cheapest exact strategy that fits the set (operation counts in
 * platforms/xx_byteset_platform.h). */
void xx_byteset_prepare(xx_byteset *set)
{
    size_t count;
    int strategy;
    if (!set) return;
    count = xx_byteset_count(set);
    if (count == 0) strategy = XX_BYTESET_STRATEGY_EMPTY;
    else if (count == 256) strategy = XX_BYTESET_STRATEGY_FULL;
    else if (count <= 2) strategy = XX_BYTESET_STRATEGY_EQ1 + (int)count - 1;
    else if (count >= 253) strategy = XX_BYTESET_STRATEGY_NE1 + (int)(256 - count) - 1;
    else if (xx_byteset_prepare_strategy(set, XX_BYTESET_STRATEGY_RANGE)) return;
    else if (xx_byteset_prepare_strategy(set, XX_BYTESET_STRATEGY_NOT_RANGE)) return;
    /* LOOKUP needs pshufb; without it three members stay with EQ3. */
    else if (count <= 16 && (count > 3 || xx_byteset_pshufb_usable()) &&
             xx_byteset_prepare_strategy(set, XX_BYTESET_STRATEGY_LOOKUP))
        return;
    else if (count == 3) strategy = XX_BYTESET_STRATEGY_EQ3;
    else if (xx_byteset_prepare_strategy(set, XX_BYTESET_STRATEGY_TRUFFLE_ASCII)) return;
    else if (xx_byteset_prepare_strategy(set, XX_BYTESET_STRATEGY_SHUFTI)) return;
    else strategy = XX_BYTESET_STRATEGY_TRUFFLE; /* exact for every set; see NOTES on SHUFTI_FILTER */
    if (!xx_byteset_prepare_strategy(set, strategy)) set->strategy = XX_BYTESET_STRATEGY_UNPREPARED;
}

/* ------------------------------------------------------------- dispatch */

bool xx_byteset_backend_available(int backend)
{
    if (backend == XX_BYTESET_BACKEND_SCALAR) return true;
#ifdef XX_BYTESET_X86
    if (backend == XX_BYTESET_BACKEND_SSE2) return xx_is_sse2_enabled();
    if (backend == XX_BYTESET_BACKEND_AVX2) return xx_is_avx2_enabled();
#endif
    return false;
}

/* The backends take from < size, data != NULL and a strategy other than
 * EMPTY and FULL, which never touch the data. */
static size_t xx_byteset_run(int backend, const xx_byteset *set, const uint8_t *data, size_t size, size_t from)
{
    if (!set || !data || from >= size || set->strategy == XX_BYTESET_STRATEGY_EMPTY) return size;
    if (set->strategy == XX_BYTESET_STRATEGY_FULL) return from;
#ifdef XX_BYTESET_X86
    if (backend == XX_BYTESET_BACKEND_AVX2) return xx_byteset_find_avx2(set, data, size, from);
    if (backend == XX_BYTESET_BACKEND_SSE2) return xx_byteset_find_sse2(set, data, size, from);
#else
    (void)backend;
#endif
    return xx_byteset_find_scalar(set, data, size, from);
}

size_t xx_byteset_find(const xx_byteset *set, const void *data, size_t size, size_t from)
{
    int backend = XX_BYTESET_BACKEND_SCALAR;
    if (!set || !data || from >= size || set->strategy == XX_BYTESET_STRATEGY_EMPTY) return size;
    if (set->strategy == XX_BYTESET_STRATEGY_FULL) return from;
#ifdef XX_BYTESET_X86
    if (xx_is_avx2_enabled()) return xx_byteset_find_avx2(set, (const uint8_t *)data, size, from);
    if (xx_is_sse2_enabled()) backend = XX_BYTESET_BACKEND_SSE2;
#endif
    return xx_byteset_run(backend, set, (const uint8_t *)data, size, from);
}

bool xx_byteset_find_backend(int backend, const xx_byteset *set, const void *data, size_t size,
                             size_t from, size_t *found)
{
    if (!found || !xx_byteset_backend_available(backend)) return false;
    *found = xx_byteset_run(backend, set, (const uint8_t *)data, size, from);
    return true;
}
