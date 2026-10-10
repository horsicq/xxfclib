/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent Shrink arithmetic/LZ decoder. Wire facts: IFF-CDAF client
 * documents a 504-symbol binary frequency model, distance-driven symbol
 * activation, 32-bit interval, and classes of repeat/match tokens. */
#ifndef XX_SHRINK_CDAF_CODEC_H
#define XX_SHRINK_CDAF_CODEC_H
typedef struct sc_decoder {
    const uint8_t *p;
    uint32_t n, at, range, value;
    uint16_t weight[1008];
    bool failed;
} sc_decoder;
static void sc_normalize(sc_decoder *d)
{
    while (d->range < 0x01000000U && !d->failed) {
        if (d->at >= d->n) {
            d->failed = true;
            break;
        }
        d->value = (d->value << 8U) | d->p[d->at++];
        d->range <<= 8U;
    }
}
static uint32_t sc_fraction(sc_decoder *d, uint32_t weight)
{
    uint32_t total = d->weight[1], q, r, high = d->range >> 16U, low = d->range & 65535U;
    if (!total) {
        d->failed = true;
        return 0;
    }
    q = (weight << 16U) / total;
    r = (((weight << 16U) % total) << 16U) / total;
    return ((low * q) >> 16U) + ((high * (r & 65535U)) >> 16U) + (q & 65535U) * high;
}
static void sc_rescale(sc_decoder *d)
{
    unsigned i;
    for (i = 504U; i < 1008U; ++i)
        if (d->weight[i]) d->weight[i] = (uint16_t)(d->weight[i] / 2U + 1U);
    for (i = 503U; i; --i) d->weight[i] = (uint16_t)(d->weight[i * 2U] + d->weight[i * 2U + 1U]);
}
static void sc_increment(sc_decoder *d, unsigned symbol, unsigned amount)
{
    unsigned node = 504U + symbol;
    if (node >= 1008U) {
        d->failed = true;
        return;
    }
    while (node) {
        d->weight[node] = (uint16_t)(d->weight[node] + amount);
        node >>= 1U;
    }
    if (d->weight[1] >= 8192U) sc_rescale(d);
}
static uint32_t sc_raw(sc_decoder *d, unsigned n)
{
    uint32_t v = 0;
    while (n--) {
        d->range >>= 1U;
        v <<= 1U;
        if (d->value >= d->range) {
            ++v;
            d->value -= d->range;
        }
        sc_normalize(d);
        if (d->failed) return 0;
    }
    return v;
}
static uint32_t sc_base(unsigned n)
{
    return n < 15U ? ((4U << n) - 4U) : 0U;
}
static bool sc_decode(ac_blob *b, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t n, unsigned method)
{
    sc_decoder d;
    uint8_t *ring;
    uint32_t window, at = 0, next = 0;
    unsigned i;
    if (method < 1U || method > 7U || packed < 4U) return false;
    xx_mem_zero(&d, sizeof(d));
    d.p = in;
    d.n = packed;
    d.at = 4;
    d.value = xx_data_get_u32(in, 4, 0, true);
    d.range = 0x80000000U;
    for (i = 0; i < 261U; ++i) d.weight[504U + i] = (uint16_t)((i < 32U || i > 126U) ? 1 : 3);
    d.weight[897] = 1;
    for (i = 503U; i; --i) d.weight[i] = (uint16_t)(d.weight[i * 2U] + d.weight[i * 2U + 1U]);
    window = 1U << (10U + method);
    ring = ac_alloc(b, window);
    if (!ring) return false;
    xx_mem_zero(ring, window);
    while (at < n) {
        uint32_t distance = 0, length = 1;
        unsigned node = 1, symbol, cumulative = 0;
        uint32_t lower = 0;
        if (!ac_poll(b)) goto fail;
        while (next <= at) {
            uint32_t power = 4;
            unsigned exponent = 0, k, group;
            if (next < 48U) sc_increment(&d, next + 261U, 1);
            while (power < next + 4U) {
                power *= 2U;
                ++exponent;
            }
            if (power == next + 4U && exponent < 14U) {
                if (exponent < 13U) {
                    sc_increment(&d, 394U + 2U * exponent, 1);
                    sc_increment(&d, 395U + 2U * exponent, 1);
                }
                for (group = 0; group < 3U; ++group) {
                    sc_increment(&d, 420U + 28U * group + 2U * exponent, 1);
                    sc_increment(&d, 421U + 28U * group + 2U * exponent, 1);
                }
                if (exponent < 7U)
                    for (k = 0; k < 4U; ++k) sc_increment(&d, 309U + 4U * exponent + k, 1);
                for (k = 0; k < 4U; ++k) sc_increment(&d, 337U + 14U * k + exponent, 1);
            }
            ++next;
            if (next >= 48U) next = next == 48U ? 60U : (next + 3U) * 2U - 4U;
            if (d.failed || next > AC_MAX_BYTES * 2U) goto fail;
        }
        while (node < 504U) {
            unsigned left = node * 2U;
            uint32_t split = sc_fraction(&d, cumulative + d.weight[left]);
            if (d.failed) goto fail;
            if (d.value >= split) {
                cumulative += d.weight[left];
                lower = split;
                node = left + 1U;
            } else node = left;
        }
        if (!d.weight[node]) goto fail;
        d.value -= lower;
        d.range = sc_fraction(&d, d.weight[node]);
        symbol = node - 504U;
        sc_increment(&d, symbol, 3U + (d.weight[1] >> 10U));
        sc_normalize(&d);
        if (symbol < 256U) {
            out[at] = ring[at & (window - 1U)] = (uint8_t)symbol;
            ++at;
            continue;
        }
        if (symbol < 261U) {
            unsigned cls = symbol - 256U;
            distance = 1;
            length = cls < 4U ? 2U + sc_raw(&d, cls + 2U) + sc_base(cls) : sc_raw(&d, 16);
        } else if (symbol < 309U) {
            distance = symbol - 259U;
            length = 2;
        } else if (symbol < 337U) {
            unsigned cls = symbol - 309U;
            distance = (sc_raw(&d, cls / 4U) << 2U) + (cls & 3U) + sc_base(cls / 4U) + 3U;
            length = 3;
        } else if (symbol < 393U) {
            unsigned cls = symbol - 337U, l = cls / 14U, o = cls % 14U;
            length = sc_raw(&d, l + 2U) + sc_base(l) + 8U;
            distance = sc_raw(&d, o + 2U) + sc_base(o) + length;
        } else if (symbol == 393U) {
            length = sc_raw(&d, 16);
            distance = sc_raw(&d, 16);
        } else {
            unsigned group = symbol < 420U ? 0U : symbol < 448U ? 1U : symbol < 476U ? 2U : 3U;
            unsigned cls = symbol - (group ? 420U + (group - 1U) * 28U : 394U);
            length = group + 4U;
            distance = (sc_raw(&d, 1U + cls / 2U) << 1U) + (cls & 1U) + sc_base(cls / 2U) + length;
        }
        if (d.failed || !length || !distance || distance > window || length > n - at) goto fail;
        while (length--) {
            uint8_t value = ring[(at - distance) & (window - 1U)];
            out[at] = ring[at & (window - 1U)] = value;
            ++at;
        }
    }
    ac_release(b, ring, window);
    return !d.failed && ac_poll(b);
fail:
    ac_release(b, ring, window);
    return false;
}
#endif
