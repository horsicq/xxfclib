/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient LZCBDecompressor.cpp,
 * RangeDecoder.cpp and FrequencyTree.hpp at commit
 * 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_LZCB_NATIVE_H
#define XX_XPK_LZCB_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct xpk_lzcb_bits {
    const uint8_t *data;
    size_t size, at;
    unsigned pad, left;
    uint32_t word;
} xpk_lzcb_bits;
typedef struct xpk_lzcb_range {
    uint16_t low, high, stream;
} xpk_lzcb_range;
typedef struct xpk_lzcb_freq {
    uint16_t freq[258];
    uint16_t total, threshold;
    bool used;
} xpk_lzcb_freq;
typedef struct xpk_lzcb_context {
    xpk_lzcb_bits bits;
    xpk_lzcb_range range;
    xpk_lzcb_freq base, repeat, literal_count, distance;
    xpk_lzcb_freq literals[256];
    xx_pd_struct *pd;
} xpk_lzcb_context;
static bool xpk_lzcb_bit(xpk_lzcb_bits *bits, uint32_t *value)
{
    if (!bits->left) {
        unsigned i;
        bits->word = 0U;
        for (i = 0U; i < 4U; ++i) {
            uint8_t byte;
            if (bits->at < bits->size) byte = bits->data[bits->at++];
            else if (bits->pad) {
                byte = 0U;
                --bits->pad;
            } else return false;
            bits->word = (bits->word << 8U) | byte;
        }
        bits->left = 32U;
    }
    --bits->left;
    *value = (bits->word >> bits->left) & 1U;
    return true;
}
static bool xpk_lzcb_read(xpk_lzcb_bits *bits, unsigned count, uint32_t *value)
{
    uint32_t result = 0U, bit;
    unsigned i;
    if (count > 32U) return false;
    for (i = 0U; i < count; ++i) {
        if (!xpk_lzcb_bit(bits, &bit)) return false;
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}
static bool xpk_lzcb_value(const xpk_lzcb_range *r, uint16_t total, uint16_t *value)
{
    uint32_t interval, numerator;
    if (!total || r->high < r->low || r->stream < r->low || r->stream > r->high) return false;
    interval = (uint32_t)r->high - r->low + 1U;
    numerator = ((uint32_t)r->stream - r->low + 1U) * total - 1U;
    *value = (uint16_t)(numerator / interval);
    return *value < total;
}
static bool xpk_lzcb_scale(xpk_lzcb_context *ctx, uint16_t low, uint16_t high, uint16_t total)
{
    uint32_t interval;
    xpk_lzcb_range *r = &ctx->range;
    if (!total || low >= high || high > total || r->high < r->low) return false;
    interval = (uint32_t)r->high - r->low + 1U;
    r->high = (uint16_t)((interval * high) / total + r->low - 1U);
    r->low = (uint16_t)((interval * low) / total + r->low);
    if (r->high < r->low) return false;
    for (;;) {
        uint16_t decr;
        uint32_t bit;
        if (r->high < 0x8000U) decr = 0U;
        else if (r->low >= 0x8000U) decr = 0x8000U;
        else if (r->low >= 0x4000U && r->high < 0xc000U) decr = 0x4000U;
        else break;
        if (!xpk_lzcb_bit(&ctx->bits, &bit)) return false;
        r->low = (uint16_t)((r->low - decr) << 1U);
        r->high = (uint16_t)(((r->high - decr) << 1U) | 1U);
        r->stream = (uint16_t)(((r->stream - decr) << 1U) | bit);
    }
    return true;
}
static bool xpk_lzcb_fixed(xpk_lzcb_context *ctx, uint16_t total, uint16_t *value)
{
    if (!xpk_lzcb_value(&ctx->range, total, value)) return false;
    return xpk_lzcb_scale(ctx, *value, (uint16_t)(*value + 1U), total);
}
static bool xpk_lzcb_frequency(xpk_lzcb_context *ctx, xpk_lzcb_freq *model, unsigned max_symbol, unsigned fresh, uint16_t *symbol);
static bool xpk_lzcb_fresh(xpk_lzcb_context *ctx, unsigned kind, uint16_t *value)
{
    if (kind == 0U) return xpk_lzcb_fixed(ctx, 256U, value);
    if (kind == 1U) return xpk_lzcb_fixed(ctx, 257U, value);
    return xpk_lzcb_frequency(ctx, &ctx->base, 256U, 0U, value);
}
static bool xpk_lzcb_frequency(xpk_lzcb_context *ctx, xpk_lzcb_freq *model, unsigned max_symbol, unsigned fresh, uint16_t *symbol)
{
    uint16_t value, old_total, low = 0U, old_freq = 0U;
    unsigned i;
    if (!model->used) {
        model->threshold = 1U;
        model->used = true;
    }
    old_total = (uint16_t)(model->threshold + model->total);
    if (!xpk_lzcb_value(&ctx->range, old_total, &value)) return false;
    if (value >= model->threshold) {
        uint16_t remain = (uint16_t)(value - model->threshold);
        for (i = 0U; i <= max_symbol; ++i) {
            if (remain < (uint16_t)(low + model->freq[i])) break;
            low = (uint16_t)(low + model->freq[i]);
        }
        if (i > max_symbol || !model->freq[i]) return false;
        old_freq = model->freq[i];
        if (!xpk_lzcb_scale(ctx, (uint16_t)(model->threshold + low), (uint16_t)(model->threshold + low + old_freq), old_total)) return false;
        if (old_freq == 1U && model->threshold > 1U) --model->threshold;
        *symbol = (uint16_t)i;
    } else {
        if (!xpk_lzcb_scale(ctx, 0U, model->threshold, old_total) || !xpk_lzcb_fresh(ctx, fresh, symbol)) return false;
        if (!*symbol && model->freq[0]) *symbol = (uint16_t)max_symbol;
        if (*symbol > max_symbol) return false;
        ++model->threshold;
    }
    ++model->freq[*symbol];
    ++model->total;
    if ((uint32_t)model->threshold + model->total >= 0x3ffdU) {
        model->total = 0U;
        for (i = 0U; i <= max_symbol; ++i) {
            model->freq[i] >>= 1U;
            model->total = (uint16_t)(model->total + model->freq[i]);
        }
        model->threshold = (uint16_t)((model->threshold >> 1U) + 1U);
    }
    return true;
}
/* Version 23=LZCB. Fixed-size models bound stack use to ~135 KiB. */
static bool xpk_lzcb_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_lzcb_context ctx;
    uint16_t symbol, ch;
    size_t produced = 0U;
    bool last_literal = true;
    uint32_t bits;
    if (!packed || size < 2U || !output || !wanted || xx_pd_is_stopped(pd)) return false;
    memset(&ctx, 0, sizeof(ctx));
    ctx.bits.data = packed;
    ctx.bits.size = size;
    ctx.bits.pad = 7U;
    ctx.pd = pd;
    if (!xpk_lzcb_read(&ctx.bits, 16U, &bits)) return false;
    ctx.range.low = 0U;
    ctx.range.high = 0xffffU;
    ctx.range.stream = (uint16_t)bits;
    if (!xpk_lzcb_frequency(&ctx, &ctx.base, 256U, 0U, &ch)) return false;
    output[produced++] = (uint8_t)ch;
    while (produced < wanted) {
        uint32_t count, distance, tmp;
        size_t i;
        if (xx_pd_is_stopped(pd) || !xpk_lzcb_frequency(&ctx, &ctx.repeat, 257U, 1U, &symbol)) return false;
        count = symbol;
        if (count) {
            if (count == 256U) {
                do {
                    if (!xpk_lzcb_fixed(&ctx, 256U, &symbol)) return false;
                    tmp = symbol;
                    if (count > UINT32_MAX - tmp) return false;
                    count += tmp;
                } while (tmp == 255U);
            }
            count += last_literal ? 5U : 4U;
            if (count > wanted - produced) return false;
            if (!xpk_lzcb_frequency(&ctx, &ctx.distance, 256U, 0U, &symbol)) return false;
            distance = (uint32_t)symbol << 8U;
            if (!xpk_lzcb_fixed(&ctx, 256U, &symbol)) return false;
            distance |= symbol;
            if (!distance || distance > produced) return false;
            for (i = 0U; i < count; ++i) {
                if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                output[produced + i] = output[produced + i - distance];
            }
            produced += count;
            ch = output[produced - 1U];
            last_literal = false;
        } else {
            do {
                if (!xpk_lzcb_frequency(&ctx, &ctx.literal_count, 257U, 1U, &symbol)) return false;
                count = symbol;
                if (!count || count > wanted - produced) return false;
                for (i = 0U; i < count; ++i) {
                    if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                    if (!xpk_lzcb_frequency(&ctx, &ctx.literals[ch], 256U, 2U, &symbol)) return false;
                    ch = (uint8_t)symbol;
                    output[produced++] = (uint8_t)ch;
                }
            } while (count == 256U);
            last_literal = true;
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
