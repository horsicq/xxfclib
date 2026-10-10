/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * AIN table grammar: src/algo/ain/xx_ain.c and XArchive/Algos/xaindecoder.cpp.
 * Directory profile requires the end escape, complete records, bounded output
 * and cancellation. Unlike a range decoder it cannot accept a partial prefix.
 */
#ifndef XX_ESP_DIRECTORY_H
#define XX_ESP_DIRECTORY_H
#include "../common/xx_carrier_helpers.h"
typedef struct esp_directory_bits {
    const uint8_t *p;
    size_t size, pos;
    uint64_t bits;
    unsigned count;
} esp_directory_bits;
typedef struct esp_directory_tree {
    uint16_t table[256], tree[1024];
} esp_directory_tree;
typedef struct esp_directory_build {
    int count[17], head[17], next[272], order[272], node;
    unsigned used, position;
    bool bad;
} esp_directory_build;
static bool esp_directory_fill(esp_directory_bits *b, unsigned n) {
    while (b->count < n) {
        if (b->pos == b->size)
            return false;
        b->bits |= (uint64_t)b->p[b->pos++] << b->count;
        b->count += 8;
    }
    return true;
}
static int64_t esp_directory_get(esp_directory_bits *b, unsigned n) {
    uint64_t v;
    if (!n)
        return 0;
    if (n > 16 || !esp_directory_fill(b, n))
        return -1;
    v = b->bits & (((uint64_t)1 << n) - 1);
    b->bits >>= n;
    b->count -= n;
    return (int64_t)v;
}
static void esp_directory_walk(esp_directory_build *s, esp_directory_tree *t, unsigned depth, unsigned node,
                               uint32_t prefix) {
    int child, value;
    if (s->bad || depth > 16 || node >= 1024) {
        s->bad = true;
        return;
    }
    if (--s->count[depth] < 0) {
        child = s->node;
        if (child + 1 >= 1024) {
            s->bad = true;
            return;
        }
        s->node += 2;
        t->tree[node] = (uint16_t)child;
        if (depth == 8)
            t->table[prefix >> 8] = (uint16_t)(child | 0x8000U);
        esp_directory_walk(s, t, depth + 1, (unsigned)child, (prefix >> 1) & 65535U);
        esp_directory_walk(s, t, depth + 1, (unsigned)child + 1, ((prefix >> 1) | 0x8000U) & 65535U);
        return;
    }
    if (s->position >= s->used) {
        s->bad = true;
        return;
    }
    value = s->order[s->position++];
    t->tree[node] = (uint16_t)(-value);
    if (depth < 9) {
        unsigned at = depth ? prefix >> (16 - depth) : 0, step = 1U << depth;
        uint16_t entry = (uint16_t)(value | (depth << 10));
        while (at < 256) {
            t->table[at] = entry;
            at += step;
        }
    }
}
static bool esp_directory_build_tree(const int *length, unsigned n, esp_directory_tree *t) {
    esp_directory_build s;
    uint32_t total = 0;
    unsigned i, d;
    xx_rt_memset(&s, 0, sizeof(s));
    xx_rt_memset(t, 0, sizeof(*t));
    for (d = 0; d <= 16; ++d)
        s.head[d] = -1;
    for (i = 0; i < n; ++i) {
        if (length[i] < 0 || length[i] > 16)
            return false;
        ++s.count[length[i]];
        s.next[i] = s.head[length[i]];
        s.head[length[i]] = (int)i;
    }
    for (d = 1; d <= 16; ++d) {
        total += (uint32_t)s.count[d] << (16 - d);
    }
    if (total != 65536U)
        return false;
    for (d = 1; d <= 16; ++d) {
        int p = s.head[d];
        while (p >= 0) {
            s.order[s.used++] = p;
            p = s.next[p];
        }
    }
    s.count[0] = 0;
    esp_directory_walk(&s, t, 0, 0, 0);
    return !s.bad;
}
static int esp_directory_symbol(esp_directory_bits *b, const esp_directory_tree *t) {
    uint16_t e;
    unsigned node, guard;
    if (!esp_directory_fill(b, 8))
        return -1;
    e = t->table[b->bits & 255U];
    if (e < 0x8000U) {
        unsigned n = e >> 10;
        if (!n)
            return -1;
        b->bits >>= n;
        b->count -= n;
        return e & 1023U;
    }
    node = e & 1023U;
    b->bits >>= 8;
    b->count -= 8;
    if (!esp_directory_fill(b, 16))
        return -1;
    for (guard = 0; guard < 16; ++guard) {
        unsigned child = node + (unsigned)(b->bits & 1U);
        int v;
        if (child >= 1024)
            return -1;
        v = (int)(int16_t)t->tree[child];
        b->bits >>= 1;
        --b->count;
        if (v <= 0)
            return -v;
        node = (unsigned)v;
        if (!esp_directory_fill(b, 1))
            return -1;
    }
    return -1;
}
static bool esp_directory_table(esp_directory_bits *b, unsigned symbols, esp_directory_tree *t) {
    int pre[19] = {0}, lengths[272] = {0};
    esp_directory_tree pt;
    int64_t v;
    unsigned i, count, at = 0;
    v = esp_directory_get(b, 5);
    if (v < 0 || v > 19)
        return false;
    count = 19U - (unsigned)v;
    for (i = 0; i < count; ++i) {
        v = esp_directory_get(b, 3);
        if (v < 0)
            return false;
        pre[i] = (int)v;
        if (pre[i] == 7)
            for (;;) {
                v = esp_directory_get(b, 1);
                if (v < 0)
                    return false;
                if (!v)
                    break;
                if (++pre[i] > 16)
                    return false;
            }
    }
    if (!esp_directory_build_tree(pre, 19, &pt)) {
        return false;
    }
    v = esp_directory_get(b, 9);
    if (v < 0 || (uint64_t)v > symbols)
        return false;
    count = symbols - (unsigned)v;
    while (at < count) {
        int sym = esp_directory_symbol(b, &pt);
        if (sym < 0)
            return false;
        if (sym >= 3) {
            if (sym > 18)
                return false;
            lengths[at++] = sym - 2;
        } else {
            unsigned run = 1;
            if (sym == 1) {
                v = esp_directory_get(b, 4);
                if (v < 0)
                    return false;
                run = (unsigned)v + 3;
            } else if (sym == 2) {
                v = esp_directory_get(b, 9);
                if (v < 0)
                    return false;
                run = (unsigned)v + 20;
            }
            if (run > count - at) {
                return false;
            }
            while (run--)
                lengths[at++] = 0;
        }
    }
    return esp_directory_build_tree(lengths, symbols, t);
}
static bool esp_directory_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t capacity,
                                 size_t *written, xx_pd_struct *pd) {
    esp_directory_bits b;
    esp_directory_tree main_tree, length_tree;
    uint8_t window[32768];
    size_t out = 0;
    unsigned wp = 0, blocks = 0;
    *written = 0;
    xx_rt_memset(&b, 0, sizeof(b));
    xx_rt_memset(window, 0, sizeof(window));
    b.p = input;
    b.size = input_size;
    if (esp_directory_get(&b, 1) < 0 || !esp_directory_table(&b, 272, &main_tree) ||
        !esp_directory_table(&b, 254, &length_tree))
        return false;
    for (;;) {
        int sym;
        if (carrier_stop(pd))
            return false;
        sym = esp_directory_symbol(&b, &main_tree);
        if (sym < 0)
            return false;
        if (sym < 256) {
            if (out == capacity)
                return false;
            window[wp] = (uint8_t)sym;
            wp = (wp + 1) & 32767U;
            output[out++] = (uint8_t)sym;
        } else {
            unsigned distance = (unsigned)(sym - 256), rp, length;
            int ls;
            if (distance > 1) {
                int64_t extra = esp_directory_get(&b, distance - 1);
                if (extra < 0)
                    return false;
                if (extra == 16383) {
                    int64_t more = esp_directory_get(&b, 1);
                    if (more == 1) {
                        *written = out;
                        return true;
                    }
                    if (more != 0 || ++blocks > 1024 || !esp_directory_table(&b, 272, &main_tree) ||
                        !esp_directory_table(&b, 254, &length_tree))
                        return false;
                    continue;
                }
                distance = (unsigned)extra | (1U << (distance - 1));
            }
            rp = (wp - (distance + 1)) & 32767U;
            ls = esp_directory_symbol(&b, &length_tree);
            if (ls < 0)
                return false;
            length = (unsigned)ls + 3;
            if (length > capacity - out) {
                return false;
            }
            while (length--) {
                uint8_t c = window[rp];
                window[wp] = c;
                wp = (wp + 1) & 32767U;
                rp = (rp + 1) & 32767U;
                output[out++] = c;
            }
        }
    }
}
#endif
