/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Parameterized version of the library's MIT adaptive-Huffman model.
 * CP/M CRLZH and Zoom/PCompress framing facts determine the alphabets,
 * terminal symbols and dictionary conventions; no upstream implementation
 * is incorporated. */
#ifndef XX_LEGACY_HUFFMAN_H
#define XX_LEGACY_HUFFMAN_H
typedef struct ac_tree {
    int frequency[634], parent[951], child[633];
    unsigned chars, total, root;
} ac_tree;
static void ac_tree_init(ac_tree *t, unsigned chars)
{
    unsigned i, j = 0;
    xx_mem_zero(t, sizeof(*t));
    t->chars = chars;
    t->total = chars * 2U - 1U;
    t->root = t->total - 1U;
    for (i = 0; i < chars; ++i) {
        t->frequency[i] = 1;
        t->child[i] = (int)(t->total + i);
        t->parent[t->total + i] = (int)i;
    }
    for (i = chars; i < t->total; ++i, j += 2U) {
        t->frequency[i] = t->frequency[j] + t->frequency[j + 1U];
        t->child[i] = (int)j;
        t->parent[j] = t->parent[j + 1U] = (int)i;
    }
    t->frequency[t->total] = 0xFFFF;
}
static void ac_tree_rebuild(ac_tree *t)
{
    unsigned i, j = 0, k;
    for (i = 0; i < t->total; ++i)
        if (t->child[i] >= (int)t->total) {
            t->frequency[j] = (t->frequency[i] + 1) / 2;
            t->child[j++] = t->child[i];
        }
    for (i = 0, k = t->chars; k < t->total; i += 2U, ++k) {
        int weight = t->frequency[i] + t->frequency[i + 1U], at = (int)k - 1;
        while (at >= 0 && weight < t->frequency[at]) {
            --at;
        }
        ++at;
        for (j = k; j > (unsigned)at; --j) {
            t->frequency[j] = t->frequency[j - 1U];
            t->child[j] = t->child[j - 1U];
        }
        t->frequency[at] = weight;
        t->child[at] = (int)i;
    }
    for (i = 0; i < t->total; ++i) {
        int c = t->child[i];
        t->parent[c] = (int)i;
        if (c < (int)t->total) t->parent[c + 1] = (int)i;
    }
}
static void ac_tree_update(ac_tree *t, unsigned symbol, bool freeze)
{
    int node;
    if (t->frequency[t->root] == 0x8000) {
        if (freeze) return;
        ac_tree_rebuild(t);
    }
    node = t->parent[t->total + symbol];
    do {
        int weight = ++t->frequency[node], other = node + 1;
        if (weight > t->frequency[other]) {
            int a, c;
            while (weight > t->frequency[other + 1]) ++other;
            t->frequency[node] = t->frequency[other];
            t->frequency[other] = weight;
            a = t->child[node];
            c = t->child[other];
            t->child[other] = a;
            t->child[node] = c;
            t->parent[a] = other;
            if (a < (int)t->total) t->parent[a + 1] = other;
            t->parent[c] = node;
            if (c < (int)t->total) t->parent[c + 1] = node;
            node = other;
        }
        node = t->parent[node];
    } while (node);
}
/* mode=1 CRLZH1, 2 CRLZH2, 3 Zoom LH (EOF316, no initial dictionary),
 * 4 LH1, 5 LH1 frozen model (length-framed, no terminal symbol).
 * consumed excludes any trailing container checksum. */
static bool ac_adaptive(ac_blob *b, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t cap, uint32_t *written, uint32_t *consumed, unsigned mode)
{
    static const unsigned counts[6] = {1, 3, 8, 12, 24, 16};
    ac_tree tree;
    ac_bits bits;
    uint8_t widths[256], codes[256], ring[4096];
    unsigned i, j, k = 0, symbol = 0, width, position = 4036U;
    uint32_t at = 0;
    ac_tree_init(&tree, mode == 3U ? 317U : mode >= 4U ? 314U : 315U);
    xx_mem_zero(ring, sizeof(ring));
    xx_rt_memset(ring, ' ', 4036U);
    bits.p = in;
    bits.n = packed;
    bits.bit = 0;
    bits.failed = false;
    bits.lsb = false;
    for (width = 3U; width <= 8U; ++width)
        for (i = 0; i < counts[width - 3U]; ++i, ++symbol)
            for (j = 0; j < (1U << (8U - width)); ++j) {
                widths[k] = (uint8_t)width;
                codes[k++] = (uint8_t)symbol;
            }
    for (;;) {
        int node = tree.child[tree.root];
        uint32_t count = 1, distance = 0;
        unsigned token;
        if (!ac_poll(b)) {
            return false;
        }
        if (mode >= 4U && at == cap) break;
        while (node < (int)tree.total && !bits.failed) node = tree.child[node + ac_bits_get(&bits, 1)];
        if (bits.failed || node < (int)tree.total || node >= (int)(tree.total + tree.chars)) return false;
        token = (unsigned)node - tree.total;
        ac_tree_update(&tree, token, mode == 3U || mode == 5U);
        if (mode < 4U && token == (mode == 3U ? 316U : 256U)) break;
        if (token >= 256U) {
            unsigned value = ac_bits_get(&bits, 8), low = mode == 2U ? 5U : 6U, extra = widths[value] - (8U - low);
            distance = (uint32_t)codes[value] << low;
            while (extra--) value = (value << 1U) | ac_bits_get(&bits, 1);
            distance |= value & ((1U << low) - 1U);
            if (mode == 3U) {
                count = token - 255U;
                if (!distance || distance > at) return false;
            } else {
                ++distance;
                count = token - (mode >= 4U ? 253U : 254U);
            }
        }
        if (bits.failed || count > cap - at) return false;
        while (count--) {
            uint8_t c = token < 256U ? (uint8_t)token : (mode == 3U ? out[at - distance] : ring[(position - distance) & 4095U]);
            out[at++] = c;
            ring[position++ & 4095U] = c;
        }
    }
    *written = at;
    if (consumed) *consumed = (uint32_t)((bits.bit + 7U) / 8U);
    return ac_poll(b);
}
#endif
