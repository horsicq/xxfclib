/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded canonical prefix tables shared only by the new legacy readers. */
#ifndef XX_LEGACY_PREFIX_H
#define XX_LEGACY_PREFIX_H
typedef struct ac_prefix {
    uint16_t count[32], start[32], symbols[512];
    uint32_t first[32];
    unsigned max, single;
    bool singleton;
} ac_prefix;
static bool ac_prefix_build(ac_prefix *h, const uint8_t *lengths, unsigned n, unsigned max)
{
    unsigned i, k = 0, len;
    uint64_t code = 0;
    bool have = false;
    if (!n || n > 512U || max > 31U) {
        return false;
    }
    xx_mem_zero(h, sizeof(*h));
    h->max = max;
    for (i = 0; i < n; ++i) {
        if (lengths[i] > max) return false;
        if (lengths[i]) {
            ++h->count[lengths[i]];
            have = true;
        }
    }
    if (!have) return false;
    for (len = 1U; len <= max; ++len) {
        code = (code + h->count[len - 1U]) * 2U;
        if (code + h->count[len] > (UINT64_C(1) << len)) return false;
        h->first[len] = (uint32_t)code;
        h->start[len] = (uint16_t)k;
        for (i = 0; i < n; ++i)
            if (lengths[i] == len) h->symbols[k++] = (uint16_t)i;
    }
    return true;
}
static XXFC_MAYBE_UNUSED void ac_prefix_single(ac_prefix *h, unsigned symbol)
{
    xx_mem_zero(h, sizeof(*h));
    h->singleton = true;
    h->single = symbol;
}
static bool ac_prefix_read(ac_prefix *h, ac_bits *bits, unsigned *symbol)
{
    unsigned len;
    uint32_t code = 0;
    if (h->singleton) {
        *symbol = h->single;
        return true;
    }
    for (len = 1U; len <= h->max; ++len) {
        code = (code << 1U) | ac_bits_get(bits, 1U);
        if (bits->failed) return false;
        if (code >= h->first[len] && code - h->first[len] < h->count[len]) {
            *symbol = h->symbols[h->start[len] + code - h->first[len]];
            return true;
        }
    }
    return false;
}
#endif
