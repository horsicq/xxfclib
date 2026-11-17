/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_literal_internal.h"
#include "xxfclib/global/xx_global.h"

#include <stdlib.h>

/* Small sets (up to 64 literals) use Teddy's low/high nibble membership
 * masks (SSSE3/AVX2 pshufb) over up to four leading bytes with eight buckets.
 * Literals are packed into buckets greedily by the expected confirmation
 * work, literals shorter than the screened window accept any byte past their
 * end, and a candidate is confirmed with one masked 8-byte compare per item
 * of its buckets, visited in insertion order through a 64-bit item mask
 * (crowded buckets look their lengths up in the exact table instead).
 *
 * Large sets screen every start without branches, 64 starts per block, with
 * one lookup per input pair in a 64 Ki-entry table holding 16 bucket bits for
 * the pair at offset 0 and 16 for the pair at offset 2 (see the pair screen
 * below). Passing starts take their candidate lengths from a two-byte length
 * table, lengths >= 4 pass a four-byte fingerprint screen, and an exact
 * (length, bytes) hash table confirms; duplicates are chained by increasing
 * index and chains of several lengths are merged by index. On x86 a large
 * set may instead screen with one lookup per two starts (the gram screen
 * below) when its model cost is lower; candidates take their lengths from
 * the gram roles and the two-byte length table and meet the same
 * confirmation. A large set whose single-bucket nibble screen is very
 * selective feeds the same confirmation from the SIMD filter instead. Every
 * path reports starts in increasing order and, per start, literals in
 * insertion order.
 */

static uint64_t xx_literal_native(const uint8_t *bytes, size_t length)
{
    uint8_t padded[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    xx_mem_copy(padded, bytes, length);
    return xx_literal_load64(padded);
}

static bool xx_literal_has_ssse3(void)
{
#if defined(XX_LITERAL_X86) && defined(_MSC_VER)
    int cpu[4];
    __cpuid(cpu, 0);
    if (cpu[0] < 1) return false;
    __cpuid(cpu, 1);
    return (cpu[2] & (1 << 9)) != 0;
#elif defined(XX_LITERAL_X86) && (defined(__GNUC__) || defined(__clang__))
    unsigned int a, b, c, d;
    return __get_cpuid(1, &a, &b, &c, &d) && (c & (1U << 9)) != 0;
#else
    return false;
#endif
}

xx_literal_set *xx_literal_set_create(void)
{
    return (xx_literal_set *)xx_mem_calloc(1, sizeof(xx_literal_set));
}

void xx_literal_set_free(xx_literal_set *set)
{
    if (set == NULL) return;
    xx_mem_free(set->items);
    xx_mem_free(set->slots);
    xx_mem_free(set->pair_screen);
    xx_mem_free(set->gram_screen);
    xx_mem_free(set->pair_lengths);
    xx_mem_free(set->prefix_bloom);
    xx_mem_free(set);
}

bool xx_literal_set_add(xx_literal_set *set, const uint8_t *bytes, size_t length)
{
    xx_literal_item *item;
    if (set == NULL || bytes == NULL || length == 0 || length > 8 || set->count >= UINT32_MAX) return false;
    if (set->count == set->capacity) {
        size_t capacity = set->capacity ? set->capacity * 2 : 16;
        xx_literal_item *items;
        if (capacity <= set->capacity || capacity > SIZE_MAX / sizeof(*items)) return false;
        items = (xx_literal_item *)xx_mem_realloc(set->items, capacity * sizeof(*items));
        if (items == NULL) return false;
        set->items = items;
        set->capacity = capacity;
    }
    item = &set->items[set->count++];
    xx_mem_zero(item->bytes, sizeof(item->bytes));
    xx_mem_copy(item->bytes, bytes, length);
    item->value = xx_literal_native(bytes, length);
    item->length = (uint8_t)length;
    item->next = XX_LITERAL_NONE;
    item->bucket = 0;
    set->finalized = false;
    return true;
}

size_t xx_literal_set_count(const xx_literal_set *set)
{
    return set ? set->count : 0;
}

/* ------------------------------------------------------------------ */
/* Bucket packing for the small-set filter.                            */

typedef struct xx_literal_group {
    uint16_t low[4];  /* low nibbles accepted at each screened byte */
    uint16_t high[4]; /* high nibbles accepted at each screened byte */
    uint64_t items;   /* member insertion indices */
} xx_literal_group;

/* Byte model of the packing estimate: separable high/low nibble weights in
 * which small values (zero) and ASCII letters dominate, as in DEX, ELF and
 * text data. Only bucket choice depends on it, never the results. */
static const double xx_literal_high_weight[16] = {0.30, 0.05, 0.08, 0.07, 0.06, 0.06, 0.12, 0.10,
                                                  0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02};
static const double xx_literal_low_weight[16] = {0.25, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05,
                                                 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05};

/* Weights of every 8-bit half of a 16-bit nibble mask. */
typedef struct xx_literal_model {
    double high[2][256];
    double low[2][256];
} xx_literal_model;

static void xx_literal_model_init(xx_literal_model *model)
{
    unsigned half, mask, bit;
    for (half = 0; half < 2; ++half) {
        for (mask = 0; mask < 256; ++mask) {
            double high = 0.0, low = 0.0;
            for (bit = 0; bit < 8; ++bit) {
                if ((mask >> bit) & 1U) {
                    high += xx_literal_high_weight[half * 8 + bit];
                    low += xx_literal_low_weight[half * 8 + bit];
                }
            }
            model->high[half][mask] = high;
            model->low[half][mask] = low;
        }
    }
}

/* Probability that a start passes the masks under the model. */
static double xx_literal_group_rate(const xx_literal_model *model, const uint16_t *low, const uint16_t *high,
                                    unsigned filter_bytes)
{
    double rate = 1.0;
    unsigned j;
    for (j = 0; j < filter_bytes; ++j)
        rate *= (model->low[0][low[j] & 255U] + model->low[1][low[j] >> 8]) *
                (model->high[0][high[j] & 255U] + model->high[1][high[j] >> 8]);
    return rate;
}

/* Expected confirmation work of a bucket per start: its pass rate times a
 * candidate's fixed cost plus one compare per member. */
#define XX_LITERAL_CANDIDATE_COST 40.0
#define XX_LITERAL_ITEM_COST 3.0

static unsigned xx_literal_popcount64(uint64_t bits)
{
    unsigned count = 0;
    while (bits != 0) { bits &= bits - 1; ++count; }
    return count;
}

/* Greedily merges the pair of groups whose union adds the least expected
 * work until eight remain; returns the expected work per start. */
static double xx_literal_merge_delta(const xx_literal_model *model, const xx_literal_group *groups,
                                     const double *costs, const unsigned *members, size_t i, size_t j,
                                     unsigned filter_bytes)
{
    uint16_t low[4], high[4];
    unsigned k;
    for (k = 0; k < filter_bytes; ++k) {
        low[k] = (uint16_t)(groups[i].low[k] | groups[j].low[k]);
        high[k] = (uint16_t)(groups[i].high[k] | groups[j].high[k]);
    }
    return xx_literal_group_rate(model, low, high, filter_bytes) *
           (XX_LITERAL_CANDIDATE_COST + XX_LITERAL_ITEM_COST * (members[i] + members[j])) - costs[i] - costs[j];
}

/* Greedily merges the pair of groups whose union adds the least expected
 * work until eight remain; returns the expected work per start. deltas is
 * scratch space for the pairwise merge costs (SMALL_MAX^2 entries). */
static double xx_literal_pack(const xx_literal_set *set, const xx_literal_model *model, unsigned filter_bytes,
                              xx_literal_group *groups, size_t *group_count, double *deltas)
{
    double costs[XX_LITERAL_SMALL_MAX];
    unsigned members[XX_LITERAL_SMALL_MAX];
    size_t count = 0, i, j;
    double total = 0.0;
#define XX_LITERAL_DELTA(i, j) deltas[(i) * XX_LITERAL_SMALL_MAX + (j)]
    for (i = 0; i < set->count; ++i) {
        const xx_literal_item *item = &set->items[i];
        xx_literal_group *group = &groups[count++];
        unsigned k;
        for (k = 0; k < filter_bytes; ++k) {
            if (k < item->length) {
                group->low[k] = (uint16_t)(1U << (item->bytes[k] & 15U));
                group->high[k] = (uint16_t)(1U << (item->bytes[k] >> 4));
            } else {
                group->low[k] = group->high[k] = 0xffffU;
            }
        }
        group->items = UINT64_C(1) << i;
        members[i] = 1;
        costs[i] = xx_literal_group_rate(model, group->low, group->high, filter_bytes) *
                   (XX_LITERAL_CANDIDATE_COST + XX_LITERAL_ITEM_COST);
    }
    if (count > 8) {
        for (i = 0; i < count; ++i)
            for (j = i + 1; j < count; ++j)
                XX_LITERAL_DELTA(i, j) = xx_literal_merge_delta(model, groups, costs, members, i, j, filter_bytes);
    }
    while (count > 8) {
        size_t best_a = 0, best_b = 1, last = count - 1;
        double best = XX_LITERAL_DELTA(0, 1);
        for (i = 0; i < count; ++i)
            for (j = i + 1; j < count; ++j)
                if (XX_LITERAL_DELTA(i, j) < best) { best = XX_LITERAL_DELTA(i, j); best_a = i; best_b = j; }
        for (j = 0; j < filter_bytes; ++j) {
            groups[best_a].low[j] |= groups[best_b].low[j];
            groups[best_a].high[j] |= groups[best_b].high[j];
        }
        groups[best_a].items |= groups[best_b].items;
        members[best_a] += members[best_b];
        costs[best_a] = xx_literal_group_rate(model, groups[best_a].low, groups[best_a].high, filter_bytes) *
                        (XX_LITERAL_CANDIDATE_COST + XX_LITERAL_ITEM_COST * members[best_a]);
        /* The last group takes best_b's place (best_a < best_b <= last). */
        if (best_b != last) {
            groups[best_b] = groups[last];
            costs[best_b] = costs[last];
            members[best_b] = members[last];
            for (i = 0; i < last; ++i) {
                if (i == best_b) continue;
                if (i < best_b) XX_LITERAL_DELTA(i, best_b) = i < last ? XX_LITERAL_DELTA(i, last) : 0.0;
                else XX_LITERAL_DELTA(best_b, i) = XX_LITERAL_DELTA(i, last);
            }
        }
        count = last;
        for (i = 0; i < count; ++i) {
            if (i == best_a) continue;
            if (i < best_a) XX_LITERAL_DELTA(i, best_a) = xx_literal_merge_delta(model, groups, costs, members, i, best_a, filter_bytes);
            else XX_LITERAL_DELTA(best_a, i) = xx_literal_merge_delta(model, groups, costs, members, best_a, i, filter_bytes);
        }
    }
#undef XX_LITERAL_DELTA
    for (i = 0; i < count; ++i) total += costs[i];
    *group_count = count;
    return total;
}

static bool xx_literal_prepare_small(xx_literal_set *set)
{
    xx_literal_group groups[XX_LITERAL_SMALL_MAX], best_groups[XX_LITERAL_SMALL_MAX];
    xx_literal_model model;
    size_t group_count = 0, best_count = 0, i, g;
    unsigned filter_bytes, best_bytes = 1, max_length = 1, j, v;
    double best_cost = 0.0;
    double *deltas = (double *)xx_mem_alloc(XX_LITERAL_SMALL_MAX * XX_LITERAL_SMALL_MAX * sizeof(double));
    if (deltas == NULL) return false;
    xx_literal_model_init(&model);
    for (i = 0; i < set->count; ++i)
        if (set->items[i].length > max_length) max_length = set->items[i].length;
    if (max_length > 4) max_length = 4;
    /* Per 16 starts the screen costs about 6 + 7 * filter_bytes vector
     * operations. */
    for (filter_bytes = 1; filter_bytes <= max_length; ++filter_bytes) {
        double cost = (6.0 + 7.0 * filter_bytes) / 16.0 + xx_literal_pack(set, &model, filter_bytes, groups, &group_count, deltas);
        if (filter_bytes == 1 || cost < best_cost) {
            best_cost = cost;
            best_bytes = filter_bytes;
            best_count = group_count;
            xx_mem_copy(best_groups, groups, group_count * sizeof(*groups));
        }
    }
    xx_mem_free(deltas);
    set->filter_bytes = (uint8_t)best_bytes;
    for (g = 0; g < best_count; ++g) {
        uint8_t bit = (uint8_t)(1U << g);
        for (j = 0; j < best_bytes; ++j) {
            for (v = 0; v < 16; ++v) {
                if ((best_groups[g].low[j] >> v) & 1U) set->low[j][v] |= bit;
                if ((best_groups[g].high[j] >> v) & 1U) set->high[j][v] |= bit;
            }
        }
        for (i = 0; i < set->count; ++i)
            if ((best_groups[g].items >> i) & 1U) set->items[i].bucket = bit;
    }
    /* Crowded bucket combinations confirm by exact lookups of their lengths
     * (a probe costs about six member compares). */
    for (v = 0; v < 256; ++v) {
        uint64_t items = 0, rest;
        uint8_t lengths = 0;
        unsigned distinct;
        for (g = 0; g < best_count; ++g)
            if ((v >> g) & 1U) items |= best_groups[g].items;
        set->bucket_items[v] = items;
        for (rest = items; rest != 0; rest &= rest - 1)
            lengths |= (uint8_t)(1U << (set->items[xx_literal_first_bit64(rest)].length - 1));
        distinct = xx_literal_popcount64(lengths);
        set->bucket_lengths[v] = xx_literal_popcount64(items) > 6U * distinct ? lengths : 0;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Pair screen of large sets.                                          */

/* A start passes when screen[pair at 0] & (screen[pair at 2] >> 16) != 0.
 * Bits 0 and 1 hold lengths 1 and 2 (exact, any bytes at offset 2), bits 2-5
 * colour length-3 literals by their first pair (offset 2: their third byte,
 * any fourth), bits 6-15 colour longer literals by their first pair (offset
 * 2: bytes 2 and 3). The colouring keeps the offset-2 pairs of a colour away
 * from frequent first pairs, under a byte model where zero and 0xff dominate
 * (runs of zeros, small little-endian integers): candidate rates on APK, ELF
 * and DEX data drop to near the exact four-byte prefix rate. */
#define XX_LITERAL_COLOURS_LEN3 4U
#define XX_LITERAL_COLOURS_LONG 10U

static double xx_literal_byte_weight(unsigned byte)
{
    return byte == 0 ? 0.25 : (byte == 255 ? 0.03 : 0.72 / 254.0);
}

typedef struct xx_literal_first {
    double weight;
    size_t begin, end; /* range of its (first, second) keys */
} xx_literal_first;

static int xx_literal_compare_first(const void *a, const void *b)
{
    const xx_literal_first *x = (const xx_literal_first *)a, *y = (const xx_literal_first *)b;
    if (x->weight != y->weight) return x->weight > y->weight ? -1 : 1;
    return x->begin < y->begin ? -1 : (x->begin > y->begin ? 1 : 0);
}

/* Colours already given to a second key: the offset-2 half of its screen
 * entry (a length-3 key, the third byte, fills all 256 fourth bytes alike). */
static unsigned xx_literal_second_colours(const xx_literal_set *set, unsigned second, bool long_class)
{
    uint8_t pair[2];
    pair[0] = (uint8_t)(long_class ? second >> 8 : second);
    pair[1] = (uint8_t)(long_class ? second & 255U : 0U);
    return long_class ? (set->pair_screen[xx_literal_load16(pair)] >> 22) & 0x3ffU
                      : (set->pair_screen[xx_literal_load16(pair)] >> 18) & 0xfU;
}

/* keys: (first << 16 | second) of one length class grouped by first pair;
 * second is a pair (long) or a byte (len3). Returns false on allocation
 * failure. */
static bool xx_literal_colour(xx_literal_set *set, const uint32_t *keys, size_t count, bool long_class)
{
    unsigned colours = long_class ? XX_LITERAL_COLOURS_LONG : XX_LITERAL_COLOURS_LEN3;
    unsigned base = long_class ? 6U : 2U;
    double used[XX_LITERAL_COLOURS_LONG], owners[XX_LITERAL_COLOURS_LONG];
    xx_literal_first *firsts;
    size_t first_count = 0, i, k;
    unsigned c;
    firsts = (xx_literal_first *)xx_mem_alloc((count ? count : 1) * sizeof(*firsts));
    if (firsts == NULL) return false;
    for (i = 0; i < count;) {
        size_t j = i;
        unsigned first = keys[i] >> 16;
        while (j < count && (keys[j] >> 16) == first) ++j;
        firsts[first_count].weight = xx_literal_byte_weight(first >> 8) * xx_literal_byte_weight(first & 255U);
        firsts[first_count].begin = i;
        firsts[first_count].end = j;
        ++first_count;
        i = j;
    }
    qsort(firsts, first_count, sizeof(*firsts), xx_literal_compare_first);
    for (c = 0; c < colours; ++c) used[c] = owners[c] = 0.0;
    for (i = 0; i < first_count; ++i) {
        const xx_literal_first *f = &firsts[i];
        unsigned best = 0, first = keys[f->begin] >> 16;
        double best_score = 0.0;
        uint8_t pair[2];
        for (c = 0; c < colours; ++c) {
            double shared = 0.0, fresh = 0.0, score;
            for (k = f->begin; k < f->end; ++k) {
                unsigned second = keys[k] & 0xffffU;
                double weight = long_class ? xx_literal_byte_weight(second >> 8) * xx_literal_byte_weight(second & 255U)
                                           : xx_literal_byte_weight(second);
                if ((xx_literal_second_colours(set, second, long_class) >> c) & 1U) shared += weight; else fresh += weight;
            }
            /* New collisions of this first pair with the colour's seconds, and
             * of the colour's earlier first pairs with the seconds it adds. */
            score = f->weight * (used[c] - shared) + fresh * owners[c];
            if (c == 0 || score < best_score) { best_score = score; best = c; }
        }
        owners[best] += f->weight;
        pair[0] = (uint8_t)(first >> 8);
        pair[1] = (uint8_t)(first & 255U);
        set->pair_screen[xx_literal_load16(pair)] |= 1U << (base + best);
        for (k = f->begin; k < f->end; ++k) {
            unsigned second = keys[k] & 0xffffU;
            if ((xx_literal_second_colours(set, second, long_class) >> best) & 1U) continue;
            if (long_class) {
                used[best] += xx_literal_byte_weight(second >> 8) * xx_literal_byte_weight(second & 255U);
                pair[0] = (uint8_t)(second >> 8);
                pair[1] = (uint8_t)(second & 255U);
                set->pair_screen[xx_literal_load16(pair)] |= (uint32_t)1U << (16U + base + best);
            } else {
                unsigned fourth;
                used[best] += xx_literal_byte_weight(second);
                pair[0] = (uint8_t)second;
                for (fourth = 0; fourth < 256; ++fourth) {
                    pair[1] = (uint8_t)fourth;
                    set->pair_screen[xx_literal_load16(pair)] |= (uint32_t)1U << (16U + base + best);
                }
            }
        }
    }
    xx_mem_free(firsts);
    return true;
}

/* Groups the (first << 16 | second) keys of one length class by first pair
 * (ascending; insertion order within a group) with a counting sort that
 * uses the still unused screen table as its counters. */
static size_t xx_literal_group_keys(xx_literal_set *set, uint32_t *keys, bool long_class)
{
    uint32_t *counts = set->pair_screen;
    size_t i, count = 0;
    uint32_t total = 0;
    xx_mem_set(counts, 0, 65536 * sizeof(*counts));
    for (i = 0; i < set->count; ++i) {
        const xx_literal_item *item = &set->items[i];
        if (long_class ? item->length >= 4 : item->length == 3) {
            ++counts[((unsigned)item->bytes[0] << 8) | item->bytes[1]];
            ++count;
        }
    }
    for (i = 0; i < 65536; ++i) {
        uint32_t here = counts[i];
        counts[i] = total;
        total += here;
    }
    for (i = 0; i < set->count; ++i) {
        const xx_literal_item *item = &set->items[i];
        unsigned first = ((unsigned)item->bytes[0] << 8) | item->bytes[1];
        if (long_class && item->length >= 4)
            keys[counts[first]++] = ((uint32_t)first << 16) | ((uint32_t)item->bytes[2] << 8) | item->bytes[3];
        else if (!long_class && item->length == 3)
            keys[counts[first]++] = ((uint32_t)first << 16) | item->bytes[2];
    }
    return count;
}

static bool xx_literal_prepare_screen(xx_literal_set *set)
{
    uint32_t *keys;
    size_t i, len3, longer;
    bool ok;
    uint8_t pair[2];
    unsigned second;
    set->pair_screen = (uint32_t *)xx_mem_alloc(65536 * sizeof(*set->pair_screen));
    keys = (uint32_t *)xx_mem_alloc((set->count ? set->count : 1) * sizeof(*keys));
    if (set->pair_screen == NULL || keys == NULL) {
        xx_mem_free(keys);
        return false;
    }
    /* Class keys in natural byte order, so the colouring is endian-neutral. */
    len3 = xx_literal_group_keys(set, keys, false);
    longer = xx_literal_group_keys(set, keys + len3, true);
    for (i = 0; i < 65536; ++i) set->pair_screen[i] = UINT32_C(3) << 16;
    for (i = 0; i < set->count; ++i) {
        const xx_literal_item *item = &set->items[i];
        if (item->length == 1) {
            pair[0] = item->bytes[0];
            for (second = 0; second < 256; ++second) {
                pair[1] = (uint8_t)second;
                set->pair_screen[xx_literal_load16(pair)] |= 1U;
            }
        } else if (item->length == 2) {
            set->pair_screen[xx_literal_load16(item->bytes)] |= 2U;
        }
    }
    ok = (len3 == 0 || xx_literal_colour(set, keys, len3, false)) &&
         (longer == 0 || xx_literal_colour(set, keys + len3, longer, true));
    xx_mem_free(keys);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Stride-2 gram screen of large sets.                                 */

/* The SSE2 scanner may look up only the pairs at even offsets of a block,
 * the grams G_j = (byte 2j, byte 2j + 1), one lookup per two starts, and
 * combine neighbours as c_j = screen[G_j] & (screen[G_j+1] >> 8):
 *   bits 0-7 of c_j pass the even start 2j: role A (bits 0-7 of an entry)
 *     holds a literal's bytes 0-1, role B (bits 8-15) its bytes 2-3;
 *   bits 16-23 pass the odd start 2j + 1: role C (bits 16-23) holds a
 *     literal's byte 0 as the gram's second byte, role D (bits 24-31) its
 *     bytes 1-2.
 * A role accepts any byte past the literal's end. Every parity has one bit
 * for length 1 (role B or D: any gram) and the even one a bit for length 2
 * (role B: any gram); the other classes are coloured by their first key like
 * the pair screen (even: lengths 3 and >= 4 by bytes 0-1; odd: length 2 and
 * lengths >= 3 by byte 0). Every literal matching at a start sets that
 * start's bit in its colour, so the screen passes a superset of the matching
 * starts, and the candidate's role bits give a superset of its lengths. */
#ifdef XX_LITERAL_X86
#define XX_LITERAL_KEY_PAIR 0U /* key = (byte 0 << 8) | byte 1 of a gram */
#define XX_LITERAL_KEY_HEAD 1U /* key = the gram's first byte, any second */
#define XX_LITERAL_KEY_TAIL 2U /* key = the gram's second byte, any first */

typedef struct xx_literal_gram_class {
    unsigned first_kind, second_kind;
    unsigned first_shift, second_shift; /* role fields of the two keys */
    unsigned base, colours;             /* role bits base .. base + colours - 1 */
} xx_literal_gram_class;

static double xx_literal_key_weight(unsigned kind, unsigned key)
{
    return kind == XX_LITERAL_KEY_PAIR ? xx_literal_byte_weight(key >> 8) * xx_literal_byte_weight(key & 255U)
                                       : xx_literal_byte_weight(key & 255U);
}

/* Native-order index of the gram (b0, b1). */
static unsigned xx_literal_gram_index(unsigned b0, unsigned b1)
{
    uint8_t pair[2];
    pair[0] = (uint8_t)b0;
    pair[1] = (uint8_t)b1;
    return xx_literal_load16(pair);
}

/* One gram of the key's grams (all of them get the same role bits). */
static unsigned xx_literal_key_gram(unsigned kind, unsigned key)
{
    if (kind == XX_LITERAL_KEY_PAIR) return xx_literal_gram_index(key >> 8, key & 255U);
    return kind == XX_LITERAL_KEY_HEAD ? xx_literal_gram_index(key, 0) : xx_literal_gram_index(0, key);
}

static void xx_literal_key_mark(uint32_t *screen, unsigned kind, unsigned key, uint32_t bit)
{
    unsigned other;
    if (kind == XX_LITERAL_KEY_PAIR) {
        screen[xx_literal_key_gram(kind, key)] |= bit;
        return;
    }
    for (other = 0; other < 256; ++other)
        screen[kind == XX_LITERAL_KEY_HEAD ? xx_literal_gram_index(key, other) : xx_literal_gram_index(other, key)] |= bit;
}

/* Greedy colouring of one class (keys: first << 16 | second, grouped by
 * first), as xx_literal_colour: heavy first keys first, each into the colour
 * that adds the least expected collisions. */
static bool xx_literal_colour_grams(uint32_t *screen, const uint32_t *keys, size_t count, const xx_literal_gram_class *cls)
{
    double used[8], owners[8];
    xx_literal_first *firsts;
    size_t first_count = 0, i, k;
    unsigned c;
    firsts = (xx_literal_first *)xx_mem_alloc((count ? count : 1) * sizeof(*firsts));
    if (firsts == NULL) return false;
    for (i = 0; i < count;) {
        size_t j = i;
        unsigned first = keys[i] >> 16;
        while (j < count && (keys[j] >> 16) == first) ++j;
        firsts[first_count].weight = xx_literal_key_weight(cls->first_kind, first);
        firsts[first_count].begin = i;
        firsts[first_count].end = j;
        ++first_count;
        i = j;
    }
    qsort(firsts, first_count, sizeof(*firsts), xx_literal_compare_first);
    for (c = 0; c < cls->colours; ++c) used[c] = owners[c] = 0.0;
    for (i = 0; i < first_count; ++i) {
        const xx_literal_first *f = &firsts[i];
        unsigned best = 0, first = keys[f->begin] >> 16;
        double best_score = 0.0;
        for (c = 0; c < cls->colours; ++c) {
            double shared = 0.0, fresh = 0.0, score;
            uint32_t bit = (uint32_t)1U << (cls->second_shift + cls->base + c);
            for (k = f->begin; k < f->end; ++k) {
                unsigned second = keys[k] & 0xffffU;
                double weight = xx_literal_key_weight(cls->second_kind, second);
                if ((screen[xx_literal_key_gram(cls->second_kind, second)] & bit) != 0) shared += weight; else fresh += weight;
            }
            score = f->weight * (used[c] - shared) + fresh * owners[c];
            if (c == 0 || score < best_score) { best_score = score; best = c; }
        }
        owners[best] += f->weight;
        xx_literal_key_mark(screen, cls->first_kind, first, (uint32_t)1U << (cls->first_shift + cls->base + best));
        for (k = f->begin; k < f->end; ++k) {
            unsigned second = keys[k] & 0xffffU;
            uint32_t bit = (uint32_t)1U << (cls->second_shift + cls->base + best);
            if ((screen[xx_literal_key_gram(cls->second_kind, second)] & bit) != 0) continue;
            used[best] += xx_literal_key_weight(cls->second_kind, second);
            xx_literal_key_mark(screen, cls->second_kind, second, bit);
        }
    }
    xx_mem_free(firsts);
    return true;
}

/* Coloured classes of the gram screen. */
#define XX_LITERAL_GRAM_EVEN3 0U /* length 3: bytes 0-1 / byte 2 */
#define XX_LITERAL_GRAM_EVEN4 1U /* length >= 4: bytes 0-1 / bytes 2-3 */
#define XX_LITERAL_GRAM_ODD2 2U  /* length 2: byte 0 / byte 1 */
#define XX_LITERAL_GRAM_ODD3 3U  /* length >= 3: byte 0 / bytes 1-2 */

static bool xx_literal_gram_key(const xx_literal_item *item, unsigned cls, uint32_t *key)
{
    const uint8_t *b = item->bytes;
    switch (cls) {
    case XX_LITERAL_GRAM_EVEN3:
        *key = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | b[2];
        return item->length == 3;
    case XX_LITERAL_GRAM_EVEN4:
        *key = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
        return item->length >= 4;
    case XX_LITERAL_GRAM_ODD2:
        *key = ((uint32_t)b[0] << 16) | b[1];
        return item->length == 2;
    default:
        *key = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
        return item->length >= 3;
    }
}

/* The keys of one class grouped by first key (ascending; insertion order
 * within a group) with a counting sort over counts (65536 entries). */
static size_t xx_literal_gram_keys(const xx_literal_set *set, unsigned cls, uint32_t *keys, uint32_t *counts)
{
    size_t i, count = 0;
    uint32_t total = 0, key;
    xx_mem_set(counts, 0, 65536 * sizeof(*counts));
    for (i = 0; i < set->count; ++i) {
        if (xx_literal_gram_key(&set->items[i], cls, &key)) {
            ++counts[key >> 16];
            ++count;
        }
    }
    for (i = 0; i < 65536; ++i) {
        uint32_t here = counts[i];
        counts[i] = total;
        total += here;
    }
    for (i = 0; i < set->count; ++i)
        if (xx_literal_gram_key(&set->items[i], cls, &key)) keys[counts[key >> 16]++] = key;
    return count;
}

/* Expected passing starts per position under the byte model (union bound
 * over role bits). roles = 2: 16-bit roles of the pair screen; roles = 4:
 * the gram screen, the mean of its two parities. */
static double xx_literal_screen_rate(const uint32_t *screen, unsigned roles)
{
    double weight[32], rate = 0.0;
    unsigned i, bit;
    for (bit = 0; bit < 32; ++bit) weight[bit] = 0.0;
    for (i = 0; i < 65536; ++i) {
        uint32_t entry = screen[i];
        uint16_t index = (uint16_t)i;
        uint8_t pair[2];
        double w;
        if (entry == 0) continue;
        memcpy(pair, &index, sizeof(pair));
        w = xx_literal_byte_weight(pair[0]) * xx_literal_byte_weight(pair[1]);
        while (entry != 0) {
            weight[xx_literal_first_bit(entry)] += w;
            entry &= entry - 1;
        }
    }
    if (roles == 2) {
        for (bit = 0; bit < 16; ++bit) rate += weight[bit] * weight[16 + bit];
    } else {
        for (bit = 0; bit < 8; ++bit) rate += 0.5 * (weight[bit] * weight[8 + bit] + weight[16 + bit] * weight[24 + bit]);
    }
    return rate;
}

/* Builds the gram screen and keeps it when its model cost (lookups per byte
 * about 0.6 of the pair screen's, a candidate about 20 times a pair-screen
 * byte) is lower. Returns false on allocation failure only. */
static bool xx_literal_prepare_grams(xx_literal_set *set)
{
    static const xx_literal_gram_class classes[4] = {
        {XX_LITERAL_KEY_PAIR, XX_LITERAL_KEY_HEAD, 0, 8, 0, 0},
        {XX_LITERAL_KEY_PAIR, XX_LITERAL_KEY_PAIR, 0, 8, 0, 0},
        {XX_LITERAL_KEY_TAIL, XX_LITERAL_KEY_HEAD, 16, 24, 0, 0},
        {XX_LITERAL_KEY_TAIL, XX_LITERAL_KEY_PAIR, 16, 24, 0, 0}};
    xx_literal_gram_class cls[4];
    uint32_t *screen, *keys, *counts;
    size_t sizes[4], i, offset = 0;
    unsigned even = 0, odd = 0, rest, parity, bits, c;
    uint32_t even1 = 0, even2 = 0, odd1 = 0;
    bool has1 = false, has2 = false, ok = true;
    for (i = 0; i < set->count; ++i) {
        if (set->items[i].length == 1) has1 = true;
        if (set->items[i].length == 2) has2 = true;
    }
    screen = (uint32_t *)xx_mem_calloc(65536, sizeof(*screen));
    keys = (uint32_t *)xx_mem_alloc((set->count ? set->count : 1) * 4 * sizeof(*keys));
    counts = (uint32_t *)xx_mem_alloc(65536 * sizeof(*counts));
    if (screen == NULL || keys == NULL || counts == NULL) {
        xx_mem_free(screen); xx_mem_free(keys); xx_mem_free(counts);
        return false;
    }
    for (c = 0; c < 4; ++c) {
        sizes[c] = xx_literal_gram_keys(set, c, keys + offset, counts);
        offset += sizes[c];
    }
    /* Role bits: the uncoloured classes first, the rest split by colours. */
    if (has1) { even1 = 1U << even++; odd1 = 1U << odd++; }
    if (has2) even2 = 1U << even++;
    for (c = 0; c < 4; ++c) cls[c] = classes[c];
    rest = 8 - even;
    cls[0].base = even;
    cls[0].colours = sizes[0] == 0 ? 0 : (sizes[1] == 0 ? rest : rest / 3);
    cls[1].base = even + cls[0].colours;
    cls[1].colours = sizes[1] == 0 ? 0 : rest - cls[0].colours;
    rest = 8 - odd;
    cls[2].base = odd;
    cls[2].colours = sizes[2] == 0 ? 0 : (sizes[3] == 0 ? rest : rest / 3);
    cls[3].base = odd + cls[2].colours;
    cls[3].colours = sizes[3] == 0 ? 0 : rest - cls[2].colours;
    for (i = 0; i < set->count; ++i) {
        const xx_literal_item *item = &set->items[i];
        if (item->length == 1) {
            xx_literal_key_mark(screen, XX_LITERAL_KEY_HEAD, item->bytes[0], even1);
            xx_literal_key_mark(screen, XX_LITERAL_KEY_TAIL, item->bytes[0], odd1 << 16);
        } else if (item->length == 2) {
            screen[xx_literal_gram_index(item->bytes[0], item->bytes[1])] |= even2;
        }
    }
    for (i = 0; i < 65536; ++i) screen[i] |= ((even1 | even2) << 8) | (odd1 << 24);
    for (c = 0, offset = 0; c < 4 && ok; ++c) {
        if (sizes[c] != 0) ok = xx_literal_colour_grams(screen, keys + offset, sizes[c], &cls[c]);
        offset += sizes[c];
    }
    xx_mem_free(keys);
    xx_mem_free(counts);
    if (!ok) {
        xx_mem_free(screen);
        return false;
    }
    for (parity = 0; parity < 2; ++parity) {
        for (bits = 0; bits < 256; ++bits) {
            unsigned lengths = 0;
            const xx_literal_gram_class *shorter = &cls[parity ? 2 : 0], *longer = &cls[parity ? 3 : 1];
            uint32_t shorter_bits = ((1U << shorter->colours) - 1U) << shorter->base;
            uint32_t longer_bits = ((1U << longer->colours) - 1U) << longer->base;
            if ((bits & (parity ? odd1 : even1)) != 0) lengths |= 1U;
            if (parity == 0 && (bits & even2) != 0) lengths |= 2U;
            if ((bits & shorter_bits) != 0) lengths |= parity ? 2U : 4U;
            if ((bits & longer_bits) != 0) lengths |= parity ? 0xfcU : 0xf8U;
            set->gram_lengths[parity][bits] = (uint8_t)lengths;
        }
    }
#ifdef XX_LITERAL_TEST_FORCE_GRAMS
    /* Test builds only: every large set takes the gram screen. */
    if (screen != NULL || xx_literal_screen_rate(screen, 4) < 0.0) {
#else
    if (0.6 + 20.0 * xx_literal_screen_rate(screen, 4) < 1.0 + 20.0 * xx_literal_screen_rate(set->pair_screen, 2)) {
#endif
        set->gram_screen = screen;
    } else {
        xx_mem_free(screen);
    }
    return true;
}
#endif

/* Large sets: a single-bucket nibble screen over the common prefix bytes is
 * used only when it rejects nearly all starts; otherwise the branch-free
 * pair-table blocks are cheaper per start. */
static void xx_literal_prepare_filter_large(xx_literal_set *set)
{
    size_t i, j;
    unsigned accepted_product = 256;
    set->filter_bytes = 4;
    for (i = 0; i < set->count; ++i)
        if (set->items[i].length < set->filter_bytes) set->filter_bytes = set->items[i].length;
    for (i = 0; i < set->count; ++i) {
        const xx_literal_item *item = &set->items[i];
        for (j = 0; j < set->filter_bytes; ++j) {
            set->low[j][item->bytes[j] & 15U] |= 1U;
            set->high[j][item->bytes[j] >> 4] |= 1U;
        }
    }
    for (j = 0; j < set->filter_bytes; ++j) {
        unsigned accepted = 0;
        for (i = 0; i < 256; ++i) {
            if ((set->low[j][i & 15] & set->high[j][i >> 4]) != 0) ++accepted;
        }
        accepted_product = accepted_product * accepted / 256U;
    }
    set->use_filter = accepted_product <= 4;
}

/* Exact (length, bytes) table of every set; reverse insertion makes each
 * duplicate chain increasing by index. */
static bool xx_literal_prepare_slots(xx_literal_set *set)
{
    size_t i, slots = 16;
    unsigned shift = 64 - 4;
    if (set->count > SIZE_MAX / 4) return false;
    while (slots < set->count * 2) {
        slots *= 2;
        --shift;
    }
    if (slots > SIZE_MAX / sizeof(*set->slots)) return false;
    set->slots = (xx_literal_slot *)xx_mem_alloc(slots * sizeof(*set->slots));
    if (set->slots == NULL) return false;
    for (i = 0; i < slots; ++i) {
        set->slots[i].value = 0;
        set->slots[i].first = XX_LITERAL_NONE;
        set->slots[i].length = 0;
        set->slots[i].duplicates = 0;
    }
    set->hash_mask = slots - 1;
    set->hash_shift = shift;
    for (i = set->count; i-- != 0;) {
        xx_literal_item *item = &set->items[i];
        size_t slot = xx_literal_slot_index(set, item->value, item->length);
        while (set->slots[slot].first != XX_LITERAL_NONE &&
               !(set->slots[slot].value == item->value && set->slots[slot].length == item->length))
            slot = (slot + 1) & set->hash_mask;
        item->next = set->slots[slot].first;
        set->slots[slot].value = item->value;
        set->slots[slot].length = (uint16_t)item->length;
        set->slots[slot].duplicates = (uint16_t)(item->next != XX_LITERAL_NONE);
        set->slots[slot].first = (uint32_t)i;
    }
    return true;
}

static bool xx_literal_prepare_large(xx_literal_set *set)
{
    size_t i;
    set->pair_lengths = (uint8_t *)xx_mem_calloc(65536, sizeof(*set->pair_lengths));
    set->prefix_bloom = (uint64_t *)xx_mem_calloc(65536, sizeof(*set->prefix_bloom));
    if (set->pair_lengths == NULL || set->prefix_bloom == NULL) return false;
    for (i = 0; i < set->count; ++i) {
        const xx_literal_item *item = &set->items[i];
        unsigned key = xx_literal_load16(item->bytes);
        if (item->length == 1) {
            unsigned second;
            set->single_bytes[item->bytes[0]] = 1;
            for (second = 0; second < 256; ++second) {
                uint8_t pair[2];
                pair[0] = item->bytes[0];
                pair[1] = (uint8_t)second;
                set->pair_lengths[xx_literal_load16(pair)] |= 1U;
            }
        } else {
            set->pair_lengths[key] |= (uint8_t)(1U << (item->length - 1));
        }
        if (item->length >= 4) {
            uint64_t bits = (UINT64_C(1) << (item->bytes[2] & 31U)) | (UINT64_C(1) << (32U + (item->bytes[3] & 31U)));
            set->prefix_bloom[key] |= bits;
        }
    }
    if (!xx_literal_prepare_screen(set)) return false;
    xx_literal_prepare_filter_large(set);
#ifdef XX_LITERAL_X86
    /* Only the SSE2 scanner of sets without a selective nibble screen uses it. */
    if (!set->use_filter && !xx_literal_prepare_grams(set)) return false;
#endif
    return true;
}

bool xx_literal_set_finalize(xx_literal_set *set)
{
    size_t i;
    if (set == NULL) return false;
    if (set->finalized) return true;
    xx_mem_free(set->slots);
    xx_mem_free(set->pair_screen);
    xx_mem_free(set->gram_screen);
    xx_mem_free(set->pair_lengths);
    xx_mem_free(set->prefix_bloom);
    set->slots = NULL;
    set->pair_screen = NULL;
    set->gram_screen = NULL;
    set->pair_lengths = NULL;
    set->prefix_bloom = NULL;
    xx_mem_zero(set->single_bytes, sizeof(set->single_bytes));
    xx_mem_zero(set->low, sizeof(set->low));
    xx_mem_zero(set->high, sizeof(set->high));
    xx_mem_set(set->bucket_items, 0, sizeof(set->bucket_items));
    xx_mem_set(set->bucket_lengths, 0, sizeof(set->bucket_lengths));
    set->small = set->count <= XX_LITERAL_SMALL_MAX;
    set->use_filter = false;
    set->filter_bytes = 1;
    set->ssse3 = xx_literal_has_ssse3();
#ifdef XX_LITERAL_X86
    /* Prepare the library's lazily detected CPU state before an immutable set
     * is published to concurrent scanning threads. */
    (void)xx_is_avx2_enabled();
    (void)xx_is_sse2_enabled();
#endif
    set->length_masks[0] = 0;
    for (i = 1; i <= 8; ++i) {
        static const uint8_t ones[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        set->length_masks[i] = xx_literal_native(ones, i);
    }
    for (i = 0; i < set->count; ++i) {
        xx_literal_item *item = &set->items[i];
        item->mask = set->length_masks[item->length];
        item->next = XX_LITERAL_NONE;
        item->bucket = 0;
    }
    if (!xx_literal_prepare_slots(set)) return false;
    if (set->small) {
        if (set->count != 0 && !xx_literal_prepare_small(set)) return false;
        set->use_filter = true;
    } else if (!xx_literal_prepare_large(set)) {
        return false;
    }
    set->finalized = true;
    return true;
}

static bool xx_literal_scan_valid(const xx_literal_set *set, const uint8_t *data, size_t size,
                                  size_t begin, size_t end, xx_literal_match_fn match)
{
    return set != NULL && set->finalized && match != NULL && begin <= end && end <= size && (begin == end || data != NULL);
}

bool xx_literal_set_scan_scalar(const xx_literal_set *set, const uint8_t *data, size_t size,
                                size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
    size_t position, i;
    if (!xx_literal_scan_valid(set, data, size, begin, end, match)) return false;
    for (position = begin; position < end; ++position) {
        for (i = 0; i < set->count; ++i) {
            const xx_literal_item *item = &set->items[i];
            size_t k;
            if (item->length > size - position) continue;
            for (k = 0; k < item->length && data[position + k] == item->bytes[k]; ++k) {}
            if (k == item->length && !match(context, i, position)) return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Confirmation.                                                       */

/* Exact lookups of every length in lengths (each <= the bytes behind word),
 * then the duplicate chains merged by insertion index. */
static XX_LITERAL_INLINE bool xx_literal_confirm_large(const xx_literal_set *set, uint64_t word, size_t position,
                                                       uint8_t lengths, xx_literal_match_fn match, void *context)
{
    uint32_t found[8];
    unsigned used = 0;
    bool duplicates = false;
    do {
        unsigned length = xx_literal_first_bit(lengths) + 1;
        uint64_t value = word & set->length_masks[length];
        size_t slot = xx_literal_slot_index(set, value, length);
        lengths &= (uint8_t)(lengths - 1);
        for (;;) {
            const xx_literal_slot *entry = &set->slots[slot];
            if (entry->first == XX_LITERAL_NONE) break;
            if (entry->value == value && entry->length == length) {
                found[used++] = entry->first;
                duplicates = duplicates || entry->duplicates != 0;
                break;
            }
            slot = (slot + 1) & set->hash_mask;
        }
    } while (lengths != 0);
    /* Single literals (the common case) need no item access. */
    if (used == 1 && !duplicates) return match(context, found[0], position);
    if (used == 1) {
        uint32_t index = found[0];
        do {
            if (!match(context, index, position)) return false;
            index = set->items[index].next;
        } while (index != XX_LITERAL_NONE);
        return true;
    }
    while (used != 0) {
        unsigned first = 0, j;
        uint32_t index;
        for (j = 1; j < used; ++j) if (found[j] < found[first]) first = j;
        index = found[first];
        if (!match(context, index, position)) return false;
        if (set->items[index].next != XX_LITERAL_NONE) found[first] = set->items[index].next;
        else found[first] = found[--used];
    }
    return true;
}

bool xx_literal_verify_large_fast(const xx_literal_set *set, const uint8_t *data, size_t position,
                                  uint8_t lengths, xx_literal_match_fn match, void *context)
{
    return xx_literal_confirm_large(set, xx_literal_load64(data + position), position, lengths, match, context);
}

/* Large-set lengths of the literals that may start at data (remaining >= 1). */
static XX_LITERAL_INLINE uint8_t xx_literal_lengths_at(const xx_literal_set *set, const uint8_t *data, size_t remaining)
{
    uint8_t lengths;
    if (remaining < 2) return set->single_bytes[data[0]];
    lengths = set->pair_lengths[xx_literal_load16(data)];
    if (remaining < 8) lengths &= (uint8_t)((1U << remaining) - 1U);
    if (remaining >= 4) lengths = xx_literal_screen(set, data, lengths);
    return lengths;
}

static bool xx_literal_verify_small_tail(const xx_literal_set *set, const uint8_t *data, size_t remaining,
                                         size_t position, uint8_t buckets, xx_literal_match_fn match, void *context)
{
    uint64_t items = set->bucket_items[buckets];
    uint64_t word = xx_literal_load_tail(data, remaining);
    while (items != 0) {
        unsigned i = xx_literal_first_bit64(items);
        const xx_literal_item *item = &set->items[i];
        items &= items - 1;
        if (item->length <= remaining && (word & item->mask) == item->value && !match(context, i, position)) return false;
    }
    return true;
}

bool xx_literal_verify(const xx_literal_set *set, const uint8_t *data, size_t size,
                       size_t position, uint8_t buckets, xx_literal_match_fn match, void *context)
{
    size_t remaining = size - position;
    if (set->small) {
        if (buckets == 0) return true;
        if (remaining >= 8) return xx_literal_verify_small_fast(set, data, position, buckets, match, context);
        return xx_literal_verify_small_tail(set, data + position, remaining, position, buckets, match, context);
    } else {
        uint8_t lengths = xx_literal_lengths_at(set, data + position, remaining);
        if (lengths == 0) return true;
        return xx_literal_confirm_large(set, remaining >= 8 ? xx_literal_load64(data + position) :
                                        xx_literal_load_tail(data + position, remaining),
                                        position, lengths, match, context);
    }
}

/* ------------------------------------------------------------------ */
/* Portable scanners.                                                  */

static bool xx_literal_scan_small_scalar(const xx_literal_set *set, const uint8_t *data, size_t size,
                                         size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
    size_t position;
    unsigned filter_bytes = set->filter_bytes, j;
    for (position = begin; position < end; ++position) {
        size_t remaining = size - position;
        uint8_t buckets = 255;
        if (remaining >= filter_bytes) {
            for (j = 0; j < filter_bytes; ++j) {
                uint8_t byte = data[position + j];
                buckets &= (uint8_t)(set->low[j][byte & 15U] & set->high[j][byte >> 4]);
            }
            if (buckets == 0) continue;
        }
        if (!xx_literal_verify(set, data, size, position, buckets, match, context)) return false;
    }
    return true;
}

bool xx_literal_scan_prepared(const xx_literal_set *set, const uint8_t *data, size_t size,
                              size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
    size_t position = begin;
    const uint32_t *screen;
    if (set->small) return xx_literal_scan_small_scalar(set, data, size, begin, end, match, context);
    screen = set->pair_screen;
    while (end - position >= XX_LITERAL_BLOCK && size - position >= XX_LITERAL_BLOCK + XX_LITERAL_BLOCK_SLACK) {
        const uint8_t *block = data + position;
        uint32_t pairs[XX_LITERAL_BLOCK + 2];
        uint64_t candidates = 0;
        unsigned i;
        for (i = 0; i < XX_LITERAL_BLOCK + 2; ++i) pairs[i] = screen[xx_literal_load16(block + i)];
        for (i = 0; i < XX_LITERAL_BLOCK; ++i)
            candidates |= (uint64_t)((pairs[i] & (pairs[i + 2] >> 16)) != 0) << i;
        while (candidates != 0) {
            unsigned lane = xx_literal_first_bit64(candidates);
            uint8_t found = xx_literal_screen_lengths(set, block + lane, pairs[lane] & (pairs[lane + 2] >> 16));
            candidates &= candidates - 1;
            if (found != 0 && !xx_literal_confirm_large(set, xx_literal_load64(block + lane), position + lane,
                                                        found, match, context)) return false;
        }
        position += XX_LITERAL_BLOCK;
    }
    for (; position < end; ++position) {
        size_t remaining = size - position;
        uint8_t lengths = xx_literal_lengths_at(set, data + position, remaining);
        if (lengths != 0 && !xx_literal_confirm_large(set, remaining >= 8 ? xx_literal_load64(data + position) :
                                                      xx_literal_load_tail(data + position, remaining),
                                                      position, lengths, match, context)) return false;
    }
    return true;
}

bool xx_literal_set_scan(const xx_literal_set *set, const uint8_t *data, size_t size,
                         size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
    if (!xx_literal_scan_valid(set, data, size, begin, end, match)) return false;
    if (begin == end || set->count == 0) return true;
#ifdef XX_LITERAL_X86
    if (set->use_filter) {
        if (xx_is_avx2_enabled()) return xx_literal_scan_avx2(set, data, size, begin, end, match, context);
        if (set->ssse3 && xx_is_sse2_enabled()) return xx_literal_scan_ssse3(set, data, size, begin, end, match, context);
    }
    if (!set->small && xx_is_sse2_enabled()) {
        if (set->gram_screen != NULL) return xx_literal_scan_grams_sse2(set, data, size, begin, end, match, context);
        return xx_literal_scan_blocks_sse2(set, data, size, begin, end, match, context);
    }
#endif
    return xx_literal_scan_prepared(set, data, size, begin, end, match, context);
}

const char *xx_literal_set_strategy(const xx_literal_set *set)
{
    if (set == NULL || !set->finalized) return "unprepared";
    return set->small ? "teddy" : (set->use_filter ? "prefix-hash+simd" : "prefix-hash");
}
