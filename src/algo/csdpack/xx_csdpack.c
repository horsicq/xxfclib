/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM OS/2 service-pack ("CSD"/FixPak) compressed members.
 *
 * Header: six bytes, one of six magics ('S' x 'S' plus a word); each selects
 * a 32-bit key.  The rest of the file is scrambled: the key is a shift
 * register stepped once per byte (s = (s >> 20) | (((s >> 3) ^ s) << 12)),
 * six times for the header, and every payload byte is XORed with the low
 * byte of the register after its own step.  Bits are read MSB first.
 *
 * Bit 0 of the stream picks the model size: 0 = 9,600-byte window and 5,568
 * long phrases, 1 = 24,960-byte window and 10,688 long phrases (plus, every
 * 500 symbols, a flag bit whose 0 value resets the model and inserts up to
 * 2,047 raw literal bytes).
 *
 * Symbols come from an adaptive Huffman tree over 33 leaves (65 nodes) whose
 * weights count the last 512 symbols only (sliding window: the symbol 512
 * steps back is decremented).  0 = end, 1 = model reset, 2..7 = literal by
 * move-to-front rank class, 8..12 = long phrase by rank class, 13..28 =
 * LZ copy (4 distance classes x 4 length classes), 29..32 = two-byte phrase
 * by rank class.  Each class is followed by a fixed number of raw bits.
 *
 * Phrases are learned from the output: every copy becomes a new phrase, and
 * every other token concatenated with the previous token's bytes (at most
 * 255) becomes one unless it repeats the previous candidate.  Long phrases
 * are allocated round-robin skipping slots with a nonzero age (use bumps the
 * age by 3, the allocator decrements it); two-byte phrases replace the slot
 * under the short-rank cursor.  Literal bytes, long phrases and two-byte
 * phrases each keep a rank list in classes; a hit in class k > 0 swaps the
 * entry with a victim chosen by a per-class cursor that skips entries with a
 * nonzero score (score = 2k on promotion, +1 up to 10 on hits in class 0).
 *
 * Integer widths follow the reference implementation: 16-bit indices and
 * cursors (the long allocator starts at 0xFFFF and wraps to 0), 8-bit
 * lengths, scores and ages.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/csdpack/xx_csdpack.h"

#define CSD_LEAVES 33U
#define CSD_NODES 65U
#define CSD_ROOT 0x40U
#define CSD_LEAF 0x4000U
#define CSD_NIL 0xFFFFU
#define CSD_RING 0x200U
#define CSD_SHORT 0xB8U
#define CSD_MAX_LONG 0x29C0U
#define CSD_MAX_TOTAL (CSD_MAX_LONG + CSD_SHORT)
#define CSD_SCORE_LONG 0x9C0U
#define CSD_MAX_WINDOW 0x6180U
#define CSD_SLACK 0x2000U
#define CSD_MAX_PHRASE 255U
#define CSD_BLOCK 500U
/* The tree walks are bounded by its 65 nodes; this only stops a broken tree. */
#define CSD_UPDATE_GUARD 0x100000U

/* Reset storms: a crafted stream can request a model reset every few bits,
 * and each reset rewrites the whole phrase table.  Real members reset rarely;
 * beyond this many resets per produced byte the stream is refused. */
#define CSD_RESET_ALLOWANCE 64U
#define CSD_RESET_BYTES_SHIFT 8U

typedef enum csd_kind_e { CSD_LITERAL = 0, CSD_COPY = 1, CSD_LONG = 2, CSD_PAIR = 3 } csd_kind;

typedef struct csd_state_s {
    const uint8_t *input;
    size_t input_size;
    size_t position;
    uint32_t key;
    unsigned bit_buffer;
    unsigned bits_left;

    uint8_t *output;
    size_t max_output;
    size_t produced;
    size_t resets;

    /* Adaptive Huffman tree. */
    uint16_t freq[CSD_NODES];
    uint16_t left[CSD_NODES];
    uint16_t right[CSD_NODES];
    uint16_t parent[CSD_NODES];
    uint16_t prev[CSD_NODES];
    uint16_t next[CSD_NODES];
    uint16_t leaf_node[CSD_LEAVES];
    uint8_t ring[CSD_RING];
    uint16_t ring_index;
    uint16_t ring_full;

    /* Model geometry. */
    uint16_t window_size;
    uint16_t max_long;
    uint16_t total;
    uint16_t long_base[5];
    uint8_t long_bits[5];
    uint16_t distance_base[4];
    uint8_t distance_bits[4];

    /* Phrases: index 0..max_long-1 long, max_long..total-1 two-byte. */
    uint8_t first[CSD_MAX_TOTAL];
    uint8_t suffix_length[CSD_MAX_TOTAL];
    uint8_t age[CSD_MAX_TOTAL];
    uint8_t suffix[CSD_MAX_TOTAL][CSD_MAX_PHRASE - 1U];
    uint16_t rank_phrase[CSD_MAX_TOTAL];
    uint16_t phrase_rank[CSD_MAX_TOTAL];
    uint8_t score_long[CSD_SCORE_LONG];
    uint16_t cursor_long[4];
    uint16_t last_long;
    uint8_t score_short[CSD_SHORT];
    uint16_t cursor_short[4];
    uint8_t score_literal[0x80];
    uint16_t cursor_literal[5];
    uint8_t rank_byte[256];
    uint8_t byte_rank[256];

    /* Candidate phrase. */
    uint8_t candidate[CSD_MAX_PHRASE + 1U];
    uint8_t candidate_length;
    uint8_t previous_length;

    uint16_t write;
    uint8_t window[CSD_MAX_WINDOW + CSD_SLACK + 1U];
} csd_state;

static const uint8_t k_literal_base[6] = {0U, 8U, 16U, 32U, 64U, 128U};
static const uint8_t k_literal_bits[6] = {3U, 3U, 4U, 5U, 6U, 7U};
static const uint8_t k_literal_class[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
static const uint8_t k_short_base[5] = {0U, 8U, 24U, 56U, 184U};
static const uint8_t k_short_bits[4] = {3U, 4U, 5U, 7U};
static const uint8_t k_length_base[4] = {0U, 2U, 6U, 22U};
static const uint8_t k_length_bits[4] = {1U, 2U, 4U, 7U};

typedef struct csd_magic_s {
    uint8_t bytes[6];
    uint32_t key;
} csd_magic;

static const csd_magic k_magics[6] = {
    {{0x53U, 0x45U, 0x23U, 0x53U, 0x7AU, 0x8AU}, 0x0457U},
    {{0x53U, 0x45U, 0x59U, 0x53U, 0x73U, 0x34U}, 0x04D2U},
    {{0x53U, 0x48U, 0x8FU, 0x53U, 0xAFU, 0x61U}, 0x08AEU},
    {{0x53U, 0x4DU, 0x53U, 0x53U, 0xFFU, 0x56U}, 0x0D05U},
    {{0x53U, 0x56U, 0x13U, 0x53U, 0x43U, 0x06U}, 0x15B3U},
    {{0x53U, 0x58U, 0x6FU, 0x53U, 0xA4U, 0xD9U}, 0x1A0AU},
};

uint32_t xx_csdpack_header_key(const uint8_t *header, size_t size)
{
    size_t index;

    if (!header || size < XX_CSDPACK_HEADER_SIZE) return 0U;
    for (index = 0U; index < sizeof(k_magics) / sizeof(k_magics[0]); ++index) {
        if (!xx_rt_memcmp(header, k_magics[index].bytes, XX_CSDPACK_HEADER_SIZE)) return k_magics[index].key;
    }
    return 0U;
}

/* ---------------------------------------------------------------- input -- */

static uint32_t csd_step(uint32_t s)
{
    return (s >> 20) | (((s >> 3) ^ s) << 12);
}

static bool csd_bit(csd_state *s, unsigned *bit)
{
    if (s->bits_left == 0U) {
        if (s->position >= s->input_size) return false;
        s->key = csd_step(s->key);
        s->bit_buffer = (unsigned)(s->input[s->position] ^ (uint8_t)s->key);
        ++s->position;
        s->bits_left = 8U;
    }
    *bit = (s->bit_buffer >> 7) & 1U;
    s->bit_buffer = (s->bit_buffer << 1) & 0xFFU;
    --s->bits_left;
    return true;
}

static bool csd_bits(csd_state *s, unsigned count, unsigned *value)
{
    unsigned bit;

    *value = 0U;
    while (count--) {
        if (!csd_bit(s, &bit)) return false;
        *value = (*value << 1) | bit;
    }
    return true;
}

/* --------------------------------------------------------------- output -- */

/* Emit window[window_size..write) and slide the last window_size bytes down. */
static bool csd_flush(csd_state *s)
{
    size_t count = (size_t)s->write - s->window_size;

    if (count > s->max_output - s->produced) return false;
    if (s->output) xx_rt_memcpy(s->output + s->produced, s->window + s->window_size, count);
    s->produced += count;
    xx_rt_memmove(s->window, s->window + count, s->window_size);
    s->write = s->window_size;
    return true;
}

static bool csd_room(csd_state *s, unsigned length)
{
    if ((unsigned)s->window_size + CSD_SLACK < (unsigned)s->write + length) return csd_flush(s);
    return true;
}

/* -------------------------------------------------------------- huffman -- */

static void csd_relink(csd_state *s, uint16_t child, uint16_t node)
{
    if (child < CSD_LEAF) {
        s->parent[child] = node;
    } else {
        s->leaf_node[child - CSD_LEAF] = node;
    }
}

static void csd_swap_nodes(csd_state *s, uint16_t a, uint16_t b, uint16_t weight)
{
    uint16_t child_a;
    uint16_t child_b;

    s->freq[a] = s->freq[b];
    s->freq[b] = weight;
    child_a = s->left[a];
    child_b = s->left[b];
    csd_relink(s, child_a, b);
    csd_relink(s, child_b, a);
    s->left[a] = child_b;
    s->left[b] = child_a;
    child_a = s->right[a];
    child_b = s->right[b];
    csd_relink(s, child_a, b);
    csd_relink(s, child_b, a);
    s->right[a] = child_b;
    s->right[b] = child_a;
}

static void csd_tree_init(csd_state *s)
{
    unsigned i;
    unsigned j;
    uint16_t tail = 0U;

    for (i = 0U; i < CSD_NODES; ++i) s->parent[i] = 0U;
    for (i = 0U; i < CSD_LEAVES; ++i) {
        s->freq[i] = 1U;
        s->left[i] = (uint16_t)(CSD_LEAF + i);
        s->right[i] = (uint16_t)(CSD_LEAF + i);
        s->parent[i] = CSD_NIL;
        s->leaf_node[i] = (uint16_t)i;
        s->prev[i] = i == 0U ? CSD_NIL : (uint16_t)(i - 1U);
        s->next[i] = i == CSD_LEAVES - 1U ? CSD_NIL : (uint16_t)(i + 1U);
    }
    for (i = CSD_LEAVES; i < CSD_NODES; ++i) {
        uint16_t low = 1000U;
        uint16_t high = 1000U;
        uint16_t cursor;

        for (j = 0U; j < CSD_NODES - 1U; ++j) {
            if (s->freq[j] < low && s->parent[j] == CSD_NIL) {
                low = s->freq[j];
                s->left[i] = (uint16_t)j;
            }
        }
        s->parent[s->left[i]] = (uint16_t)i;
        for (j = 0U; j < CSD_NODES - 1U; ++j) {
            if (s->freq[j] < high && s->parent[j] == CSD_NIL) {
                high = s->freq[j];
                s->right[i] = (uint16_t)j;
            }
        }
        s->parent[s->right[i]] = (uint16_t)i;
        s->freq[i] = (uint16_t)(low + high);
        s->parent[i] = CSD_NIL;
        /* Insert into the weight-ordered list after the last node <= weight. */
        cursor = 0U;
        while (cursor != CSD_NIL && s->freq[cursor] <= s->freq[i]) {
            tail = cursor;
            cursor = s->next[cursor];
        }
        if (cursor == CSD_NIL) {
            s->next[i] = CSD_NIL;
            s->prev[i] = tail;
            s->next[tail] = (uint16_t)i;
        } else {
            s->next[i] = cursor;
            s->prev[i] = s->prev[cursor];
            if (s->prev[i] != CSD_NIL) {
                s->next[s->prev[i]] = (uint16_t)i;
            }
            s->prev[cursor] = (uint16_t)i;
        }
    }
}

static void csd_tree_update(csd_state *s, unsigned symbol)
{
    uint16_t node = s->leaf_node[symbol];
    unsigned guard = 0U;

    do {
        uint16_t weight;
        uint16_t scan;
        uint16_t target = node;

        weight = ++s->freq[node];
        scan = s->next[node];
        while (scan != CSD_NIL && s->freq[scan] < weight && guard++ < CSD_UPDATE_GUARD) {
            target = scan;
            scan = s->next[scan];
        }
        if (target != node) csd_swap_nodes(s, node, target, weight);
        node = s->parent[target];
    } while (node != CSD_NIL && node < CSD_NODES && guard++ < CSD_UPDATE_GUARD);

    if (s->ring_full == 1U) {
        node = s->leaf_node[s->ring[s->ring_index]];
        do {
            uint16_t weight;
            uint16_t scan;
            uint16_t target = node;

            weight = --s->freq[node];
            scan = s->prev[node];
            while (scan != CSD_NIL && weight < s->freq[scan] && guard++ < CSD_UPDATE_GUARD) {
                target = scan;
                scan = s->prev[scan];
            }
            if (target != node) csd_swap_nodes(s, node, target, weight);
            node = s->parent[target];
        } while (node != CSD_NIL && node < CSD_NODES && guard++ < CSD_UPDATE_GUARD);
    }
}

static bool csd_symbol(csd_state *s, unsigned *symbol)
{
    uint16_t node = CSD_ROOT;
    unsigned depth = 0U;
    unsigned bit;

    ++s->ring_index;
    if (s->ring_index == CSD_RING) {
        s->ring_index = 0U;
        s->ring_full = 1U;
    }
    do {
        if (!csd_bit(s, &bit)) return false;
        node = bit ? s->right[node] : s->left[node];
        if (node >= CSD_NODES || ++depth > CSD_NODES) return false;
    } while (s->left[node] < CSD_LEAF);
    *symbol = (unsigned)(s->left[node] - CSD_LEAF);
    if (*symbol >= CSD_LEAVES) return false;
    csd_tree_update(s, *symbol);
    s->ring[s->ring_index] = (uint8_t)*symbol;
    return true;
}

/* ---------------------------------------------------------------- model -- */

static void csd_reset_model(csd_state *s)
{
    unsigned i;

    s->previous_length = 0U;
    s->candidate_length = 0U;
    s->last_long = CSD_NIL;
    for (i = 0U; i < 4U; ++i) s->cursor_long[i] = (uint16_t)(s->long_base[i] - 1U);
    for (i = 0U; i < s->total; ++i) {
        s->first[i] = 0U;
        s->suffix_length[i] = 0U;
        s->age[i] = 0U;
        s->rank_phrase[i] = (uint16_t)i;
        s->phrase_rank[i] = (uint16_t)(i < s->max_long ? i : i - s->max_long);
    }
    xx_mem_zero(s->score_long, sizeof(s->score_long));
    s->ring_full = 0U;
    s->ring_index = CSD_NIL;
    s->ring[0] = 0U;
    csd_tree_init(s);
}

static void csd_reset_short(csd_state *s)
{
    unsigned i;

    for (i = 0U; i < 4U; ++i) s->cursor_short[i] = (uint16_t)(k_short_base[i] - 1U);
    xx_mem_zero(s->score_short, sizeof(s->score_short));
}

static void csd_reset_literals(csd_state *s)
{
    unsigned i;

    for (i = 0U; i < 5U; ++i) s->cursor_literal[i] = (uint16_t)(k_literal_base[i] - 1U);
    for (i = 0U; i < 256U; ++i) {
        s->rank_byte[i] = (uint8_t)i;
        s->byte_rank[i] = (uint8_t)i;
    }
    xx_mem_zero(s->score_literal, sizeof(s->score_literal));
}

static void csd_adapt_literal(csd_state *s, uint8_t value)
{
    uint8_t rank = s->byte_rank[value];
    unsigned klass = k_literal_class[rank];
    unsigned c;
    uint16_t victim_rank;
    uint8_t victim;

    if (klass == 0U) {
        if (++s->score_literal[rank] > 10U) s->score_literal[rank] = 10U;
        return;
    }
    if (klass < 5U) s->score_literal[rank] = (uint8_t)(klass * 2U);
    c = klass - 1U;
    for (;;) {
        ++s->cursor_literal[c];
        if (s->cursor_literal[c] == k_literal_base[c + 1U]) s->cursor_literal[c] = k_literal_base[c];
        if (s->score_literal[s->cursor_literal[c]] == 0U) break;
        --s->score_literal[s->cursor_literal[c]];
    }
    victim_rank = s->cursor_literal[c];
    victim = s->rank_byte[victim_rank];
    s->rank_byte[victim_rank] = value;
    s->rank_byte[rank] = victim;
    s->byte_rank[value] = (uint8_t)victim_rank;
    s->byte_rank[victim] = rank;
}

static void csd_adapt_short(csd_state *s, uint16_t phrase)
{
    uint16_t rank = s->phrase_rank[phrase];
    unsigned klass;
    unsigned c;
    uint16_t victim;

    if (rank >= CSD_SHORT) return;
    klass = rank < 8U ? 0U : rank < 24U ? 1U : rank < 56U ? 2U : 3U;
    if (klass == 0U) {
        if (++s->score_short[rank] > 10U) s->score_short[rank] = 10U;
        return;
    }
    s->score_short[rank] = (uint8_t)(klass * 2U);
    c = klass - 1U;
    for (;;) {
        ++s->cursor_short[c];
        if (s->cursor_short[c] == k_short_base[c + 1U]) s->cursor_short[c] = k_short_base[c];
        if (s->score_short[s->cursor_short[c]] == 0U) break;
        --s->score_short[s->cursor_short[c]];
    }
    victim = s->rank_phrase[s->cursor_short[c] + s->max_long];
    s->rank_phrase[s->cursor_short[c] + s->max_long] = phrase;
    s->rank_phrase[rank + s->max_long] = victim;
    s->phrase_rank[phrase] = s->cursor_short[c];
    s->phrase_rank[victim] = rank;
}

static unsigned csd_long_class(const csd_state *s, uint16_t rank)
{
    if (rank < s->long_base[1]) return 0U;
    if (rank < s->long_base[2]) return 1U;
    if (rank < s->long_base[3]) return 2U;
    if (rank < s->long_base[4]) return 3U;
    return 4U;
}

static void csd_adapt_long(csd_state *s, uint16_t phrase)
{
    uint16_t rank;
    unsigned klass;
    unsigned c;
    uint16_t victim;

    if (phrase >= s->max_long) return;
    rank = s->phrase_rank[phrase];
    if (rank >= s->max_long) return;
    klass = csd_long_class(s, rank);
    if (klass == 0U) {
        if (++s->score_long[rank] > 10U) s->score_long[rank] = 10U;
        return;
    }
    if (klass < 4U) s->score_long[rank] = (uint8_t)(klass * 2U);
    c = klass - 1U;
    for (;;) {
        ++s->cursor_long[c];
        if (s->cursor_long[c] == s->long_base[c + 1U]) s->cursor_long[c] = s->long_base[c];
        if (s->score_long[s->cursor_long[c]] == 0U) break;
        --s->score_long[s->cursor_long[c]];
    }
    victim = s->rank_phrase[s->cursor_long[c]];
    s->rank_phrase[s->cursor_long[c]] = phrase;
    s->rank_phrase[rank] = victim;
    s->phrase_rank[phrase] = s->cursor_long[c];
    s->phrase_rank[victim] = rank;
}

/* Store the candidate as a phrase: two-byte candidates replace the slot under
 * the short cursor, longer ones go to the next long slot whose age is zero. */
static void csd_learn(csd_state *s, csd_kind kind, uint16_t decoded)
{
    uint16_t slot;

    if (s->candidate_length == 2U) {
        for (;;) {
            ++s->cursor_short[3];
            if (s->cursor_short[3] == k_short_base[4]) s->cursor_short[3] = k_short_base[3];
            if (s->score_short[s->cursor_short[3]] == 0U) break;
            --s->score_short[s->cursor_short[3]];
        }
        slot = s->rank_phrase[s->cursor_short[3] + s->max_long];
    } else {
        slot = s->last_long;
        do {
            for (;;) {
                slot = (uint16_t)(slot + 1U);
                if (slot == s->max_long) slot = 0U;
                if (s->age[slot] == 0U) break;
                --s->age[slot];
            }
        } while (kind == CSD_LONG && slot == decoded);
    }
    s->first[slot] = s->candidate[0];
    s->suffix_length[slot] = (uint8_t)(s->candidate_length - 1U);
    if (s->candidate_length > 1U) xx_rt_memcpy(s->suffix[slot], s->candidate + 1, (size_t)s->candidate_length - 1U);
    if (s->candidate_length != 2U) s->last_long = slot;
}

/* ----------------------------------------------------------------- main -- */

static bool csd_reset_allowed(csd_state *s)
{
    ++s->resets;
    return s->resets <= CSD_RESET_ALLOWANCE + (s->produced >> CSD_RESET_BYTES_SHIFT);
}

static bool csd_run(csd_state *s, bool large)
{
    unsigned counter = CSD_BLOCK - 1U;
    unsigned value;

    for (;;) {
        unsigned symbol;
        unsigned length;
        uint16_t distance = 0U;
        uint16_t phrase = 0U;
        uint8_t literal = 0U;
        csd_kind kind;
        unsigned i;

        if (++counter == CSD_BLOCK) {
            if (!large) {
                counter = 0U;
            } else {
                if (!csd_bit(s, &value)) return false;
                if (value) {
                    counter = 0U;
                } else {
                    unsigned count;

                    if (!csd_reset_allowed(s)) return false;
                    csd_reset_model(s);
                    csd_reset_short(s);
                    if (!csd_bits(s, 11U, &count)) return false;
                    for (i = 0U; i < count; ++i) {
                        if (!csd_room(s, 1U) || !csd_bits(s, 8U, &value)) return false;
                        s->window[s->write++] = (uint8_t)value;
                        csd_adapt_literal(s, (uint8_t)value);
                    }
                    counter = CSD_BLOCK - 1U;
                    continue;
                }
            }
        }
        if (!csd_symbol(s, &symbol)) return false;
        if (symbol == 0U) break;
        if (symbol == 1U) {
            if (!csd_reset_allowed(s)) return false;
            csd_reset_model(s);
            csd_reset_short(s);
            counter = CSD_BLOCK - 1U;
            continue;
        }
        if (symbol < 8U) {
            unsigned klass = symbol - 2U;

            if (!csd_bits(s, k_literal_bits[klass], &value)) return false;
            literal = s->rank_byte[(value + k_literal_base[klass]) & 0xFFU];
            length = 1U;
            kind = CSD_LITERAL;
        } else if (symbol < 13U) {
            unsigned klass = symbol - 8U;
            unsigned rank;

            if (!csd_bits(s, s->long_bits[klass], &value)) return false;
            rank = value + s->long_base[klass];
            if (rank >= s->max_long) return false;
            phrase = s->rank_phrase[rank];
            length = (unsigned)s->suffix_length[phrase] + 1U;
            kind = CSD_LONG;
        } else if (symbol < 29U) {
            unsigned klass = (symbol - 13U) >> 2;
            unsigned lclass = (symbol - 13U) & 3U;

            if (!csd_bits(s, s->distance_bits[klass], &value)) return false;
            distance = (uint16_t)(value + s->distance_base[klass] + 1U);
            if (!csd_bits(s, k_length_bits[lclass], &value)) return false;
            length = (value + k_length_base[lclass] + 4U) & 0xFFU;
            kind = CSD_COPY;
        } else {
            unsigned klass = symbol - 29U;

            if (!csd_bits(s, k_short_bits[klass], &value)) return false;
            phrase = s->rank_phrase[s->max_long + value + k_short_base[klass]];
            length = 2U;
            kind = CSD_PAIR;
        }
        if (length == 0U || !csd_room(s, length)) return false;

        if (kind == CSD_LITERAL) {
            s->window[s->write] = literal;
        } else if (kind == CSD_COPY) {
            if (distance > s->write) return false;
            for (i = 0U; i < length; ++i) s->window[s->write + i] = s->window[s->write + i - distance];
        } else {
            unsigned suffix = s->suffix_length[phrase];

            if (s->age[phrase] < 0x1EU) s->age[phrase] = (uint8_t)(s->age[phrase] + 3U);
            s->window[s->write] = s->first[phrase];
            if (suffix) xx_rt_memcpy(s->window + s->write + 1, s->suffix[phrase], suffix);
            if (kind == CSD_PAIR) csd_adapt_short(s, phrase);
        }
        for (i = 0U; i < length; ++i) csd_adapt_literal(s, s->window[s->write + i]);

        if (kind == CSD_COPY) {
            s->candidate_length = (uint8_t)length;
            xx_rt_memcpy(s->candidate, s->window + s->write, length);
            csd_learn(s, kind, phrase);
        } else if (s->previous_length == 0U || s->previous_length == 0xFFU) {
            s->candidate_length = 0U;
        } else {
            unsigned combined = length + s->previous_length;
            const uint8_t *start = s->window + s->write - s->previous_length;

            if (combined > CSD_MAX_PHRASE) combined = CSD_MAX_PHRASE;
            if (s->candidate_length == combined && !xx_rt_memcmp(s->candidate, start, combined)) {
                s->candidate_length = 0U;
            } else {
                s->candidate_length = (uint8_t)combined;
                xx_rt_memcpy(s->candidate, start, combined);
                csd_learn(s, kind, phrase);
            }
        }
        s->write = (uint16_t)(s->write + length);
        s->previous_length = (uint8_t)length;
        if (kind == CSD_LONG) {
            csd_adapt_long(s, phrase);
        } else if (kind == CSD_COPY) {
            csd_adapt_long(s, s->last_long);
        }
    }
    if (s->write != s->window_size && !csd_flush(s)) return false;
    return true;
}

bool xx_csdpack_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t max_output, size_t *consumed, size_t *produced)
{
    csd_state *s;
    uint32_t key;
    unsigned bit;
    unsigned i;
    bool large;
    bool ok;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    key = xx_csdpack_header_key(input, input_size);
    if (!key) return false;
    s = (csd_state *)xx_mem_calloc(1U, sizeof(*s));
    if (!s) return false;
    for (i = 0U; i < XX_CSDPACK_HEADER_SIZE; ++i) key = csd_step(key);
    s->input = input;
    s->input_size = input_size;
    s->position = XX_CSDPACK_HEADER_SIZE;
    s->key = key;
    s->output = output;
    s->max_output = max_output;

    if (!csd_bit(s, &bit)) {
        xx_mem_free(s);
        return false;
    }
    large = bit != 0U;
    s->long_base[0] = 0U;
    s->long_base[1] = 0x40U;
    s->long_base[2] = 0xC0U;
    s->long_base[3] = 0x1C0U;
    s->long_bits[0] = 6U;
    s->long_bits[1] = 7U;
    s->long_bits[2] = 8U;
    s->distance_base[0] = 0U;
    s->distance_base[1] = 0x80U;
    s->distance_base[2] = 0x180U;
    s->distance_bits[0] = 7U;
    s->distance_bits[1] = 8U;
    if (!large) {
        s->window_size = 0x2580U;
        s->max_long = 0x15C0U;
        s->long_base[4] = 0x5C0U;
        s->long_bits[3] = 10U;
        s->long_bits[4] = 12U;
        s->distance_base[3] = 0x580U;
        s->distance_bits[2] = 10U;
        s->distance_bits[3] = 13U;
    } else {
        s->window_size = 0x6180U;
        s->max_long = 0x29C0U;
        s->long_base[4] = 0x9C0U;
        s->long_bits[3] = 11U;
        s->long_bits[4] = 13U;
        s->distance_base[3] = 0x2180U;
        s->distance_bits[2] = 13U;
        s->distance_bits[3] = 14U;
    }
    s->total = (uint16_t)(s->max_long + CSD_SHORT);
    csd_reset_model(s);
    csd_reset_literals(s);
    csd_reset_short(s);
    s->write = s->window_size;

    ok = csd_run(s, large);
    if (ok) {
        if (consumed) *consumed = s->position;
        if (produced) *produced = s->produced;
    }
    xx_mem_free(s);
    return ok;
}
