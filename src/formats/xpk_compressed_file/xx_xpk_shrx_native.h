/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient SHRXDecompressor.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_SHRX_NATIVE_H
#define XX_XPK_SHRX_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

typedef struct xpk_shrx_state {
    uint32_t ar[999], vlen, vnext, shift;
    bool valid;
} xpk_shrx_state;
typedef struct xpk_shrx_context {
    const uint8_t *packed;
    size_t size, at;
    xpk_shrx_state state;
    uint32_t stream;
    bool shr3;
    xx_pd_struct *pd;
} xpk_shrx_context;
static bool xpk_shrx_byte(xpk_shrx_context *ctx, uint32_t *value)
{
    if (ctx->at >= ctx->size) return false;
    *value = ctx->packed[ctx->at++];
    return true;
}
static void xpk_shrx_resum(xpk_shrx_state *state)
{
    uint32_t i;
    for (i = 498U; i; --i) state->ar[i] = state->ar[i * 2U] + state->ar[i * 2U + 1U];
}
static void xpk_shrx_init(xpk_shrx_state *state)
{
    uint32_t i;
    for (i = 0U; i < 499U; ++i) state->ar[i] = 0U;
    for (i = 0U; i < 256U; ++i) state->ar[i + 499U] = (i < 32U || i > 126U) ? 1U : 3U;
    for (i = 755U; i < 999U; ++i) state->ar[i] = 0U;
    xpk_shrx_resum(state);
}
static void xpk_shrx_update(xpk_shrx_state *state, uint32_t index, uint32_t increment)
{
    uint32_t i;
    if (index >= 499U) return;
    index += 499U;
    while (index) {
        state->ar[index] += increment;
        index >>= 1U;
    }
    if (state->ar[1] >= 0x2000U) {
        for (i = 499U; i < 998U; ++i)
            if (state->ar[i]) state->ar[i] = (state->ar[i] >> 1U) + 1U;
        xpk_shrx_resum(state);
    }
}
static void xpk_shrx_upgrade(xpk_shrx_state *state)
{
    static const uint32_t updates1[6] = {358U, 359U, 386U, 387U, 414U, 415U};
    static const uint32_t updates2[4] = {442U, 456U, 470U, 484U};
    uint32_t vvalue, bits, compare, i;
    if (state->vnext >= 65532U) {
        state->vnext = UINT32_MAX;
        return;
    }
    if (!state->vlen) {
        state->vnext = 1U;
        return;
    }
    vvalue = state->vnext - 1U;
    if (vvalue < 48U) xpk_shrx_update(state, vvalue + 256U, 1U);
    bits = 0U;
    compare = 4U;
    while (vvalue >= compare) {
        vvalue -= compare;
        compare <<= 1U;
        ++bits;
    }
    if (bits >= 14U) {
        state->vnext = UINT32_MAX;
        return;
    }
    if (!vvalue) {
        if (bits < 7U)
            for (i = 304U; i <= 307U; ++i) xpk_shrx_update(state, (bits << 2U) + i, 1U);
        if (bits < 13U)
            for (i = 332U; i <= 333U; ++i) xpk_shrx_update(state, (bits << 1U) + i, 1U);
        for (i = 0U; i < 6U; ++i) xpk_shrx_update(state, (bits << 1U) + updates1[i], 1U);
        for (i = 0U; i < 4U; ++i) xpk_shrx_update(state, bits + updates2[i], 1U);
    }
    if (state->vnext < 49U) ++state->vnext;
    else if (state->vnext == 49U) state->vnext = 61U;
    else state->vnext = (state->vnext << 1U) + 3U;
}
static bool xpk_shrx_scale(bool shr3, uint32_t a, uint32_t b, uint32_t mult, uint32_t *result)
{
    uint32_t first, second;
    if (!b) return false;
    if (shr3) {
        first = 0x10000U / b;
        second = ((0x10000U % b) << 16U) / b;
    } else {
        first = (a << 16U) / b;
        second = (((a << 16U) % b) << 16U) / b;
    }
    *result = (((mult & 0xffffU) * first) >> 16U) + (((mult >> 16U) * second) >> 16U) + (mult >> 16U) * first;
    return true;
}
static bool xpk_shrx_refill(xpk_shrx_context *ctx)
{
    while (ctx->state.shift < 0x1000000U) {
        uint32_t byte;
        if (!ctx->state.shift || !xpk_shrx_byte(ctx, &byte)) return false;
        ctx->stream = (ctx->stream << 8U) | byte;
        ctx->state.shift <<= 8U;
    }
    return true;
}
static bool xpk_shrx_symbol(xpk_shrx_context *ctx, uint32_t *symbol)
{
    xpk_shrx_state *s = &ctx->state;
    uint32_t value, threshold, index, result = 0U, raw = 0U, new_value = 0U, addition;
    unsigned corrections = 0U;
    if (!(s->shift >> 16U) || !s->ar[1]) return false;
    value = (ctx->stream / (s->shift >> 16U)) & 0xffffU;
    threshold = (s->ar[1] * value) >> 16U;
    index = 1U;
    do {
        uint32_t tmp;
        index <<= 1U;
        if (index + 1U >= 999U) return false;
        tmp = s->ar[index] + result;
        if (threshold >= tmp) {
            result = tmp;
            ++index;
        }
    } while (index < 499U);
    if (ctx->shr3) {
        if (!xpk_shrx_scale(true, 0U, s->ar[1], s->shift, &raw)) return false;
        new_value = raw * result;
    } else if (!xpk_shrx_scale(false, result, s->ar[1], s->shift, &new_value)) return false;
    if (new_value > ctx->stream) {
        while (new_value > ctx->stream) {
            if (++corrections > 999U || !index) return false;
            if (--index < 499U) index += 499U;
            if (index >= 999U || result < s->ar[index]) return false;
            result -= s->ar[index];
            if (ctx->shr3) new_value = raw * result;
            else if (!xpk_shrx_scale(false, result, s->ar[1], s->shift, &new_value)) return false;
        }
    } else {
        result += s->ar[index];
        while (result < s->ar[1]) {
            uint32_t compare;
            if (++corrections > 999U) return false;
            if (ctx->shr3) compare = raw * result;
            else if (!xpk_shrx_scale(false, result, s->ar[1], s->shift, &compare)) return false;
            if (ctx->stream < compare) break;
            if (++index >= 998U) index -= 499U;
            if (index >= 999U) return false;
            result += s->ar[index];
            new_value = compare;
        }
    }
    if (index < 499U || index >= 998U || new_value > ctx->stream) return false;
    ctx->stream -= new_value;
    if (ctx->shr3) s->shift = raw * s->ar[index];
    else if (!xpk_shrx_scale(false, s->ar[index], s->ar[1], s->shift, &s->shift)) return false;
    addition = (s->ar[1] >> 10U) + 3U;
    index -= 499U;
    xpk_shrx_update(s, index, addition);
    if (!xpk_shrx_refill(ctx)) return false;
    *symbol = index;
    return true;
}
static bool xpk_shrx_code(xpk_shrx_context *ctx, uint32_t bits, uint32_t *value)
{
    uint32_t result = 0U;
    if (bits > 16U) return false;
    while (bits--) {
        result <<= 1U;
        ctx->state.shift >>= 1U;
        if (ctx->stream >= ctx->state.shift) {
            ++result;
            ctx->stream -= ctx->state.shift;
        }
        if (!xpk_shrx_refill(ctx)) return false;
    }
    *value = result;
    return true;
}
static uint32_t xpk_shrx_distance_addition(uint32_t i)
{
    return ((1U << (i + 2U)) - 1U) & ~3U;
}
/* Version 32=SHR3; 33=SHRI. The outer dispatcher keeps the model state and
 * supplies up to the final 65535 prior plaintext bytes in chronological order.
 * Initialize state.valid=false before chunk 1. A failed chunk leaves it intact.
 */
static bool xpk_shrx_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, bool shr3, const uint8_t *previous, size_t previous_size,
                            xpk_shrx_state *state, xx_pd_struct *pd)
{
    xpk_shrx_context ctx;
    uint8_t version;
    size_t produced = 0U, history;
    uint32_t raw_size;
    if (!packed || !output || !state || !wanted || size < 6U || (previous_size && !previous) || xx_pd_is_stopped(pd)) return false;
    version = packed[0];
    if (version < 1U || version > 2U) return false;
    memset(&ctx, 0, sizeof(ctx));
    ctx.packed = packed;
    ctx.size = size;
    ctx.shr3 = shr3;
    ctx.pd = pd;
    if (shr3) ctx.at = 1U;
    else if (packed[2] & 0x80U) {
        raw_size = ~xx_data_get_u32(packed + 2U, 4, 0, true) + 1U;
        if (raw_size != wanted) return false;
        ctx.at = 6U;
    } else {
        raw_size = ((uint32_t)packed[2] << 8U) | packed[3];
        if (raw_size != wanted) return false;
        ctx.at = 4U;
    }
    if (version == 1U) {
        xpk_shrx_init(&ctx.state);
        xpk_shrx_update(&ctx.state, 498U, 1U);
        ctx.state.vlen = 0U;
        ctx.state.vnext = 0U;
        ctx.state.shift = 0x80000000U;
    } else {
        if (!state->valid) return false;
        ctx.state = *state;
    }
    if (ctx.at > size || size - ctx.at < 4U) return false;
    ctx.stream = xx_data_get_u32(packed + ctx.at, 4, 0, true);
    ctx.at += 4U;
    while (produced < wanted) {
        uint32_t code, count, distance, tmp, extra;
        size_t i;
        if (xx_pd_is_stopped(pd)) return false;
        while (ctx.state.vlen >= ctx.state.vnext) {
            uint32_t prior = ctx.state.vnext;
            xpk_shrx_upgrade(&ctx.state);
            if (ctx.state.vnext == prior || ctx.state.vnext < prior) return false;
        }
        if (!xpk_shrx_symbol(&ctx, &code)) return false;
        if (code < 256U) {
            output[produced++] = (uint8_t)code;
            if (ctx.state.vlen == UINT32_MAX) return false;
            ++ctx.state.vlen;
            continue;
        }
        if (code < 304U) {
            count = 2U;
            distance = code - 255U;
        } else if (code < 332U) {
            tmp = code - 304U;
            if (!xpk_shrx_code(&ctx, tmp >> 2U, &extra)) return false;
            distance = ((extra << 2U) | (tmp & 3U)) + xpk_shrx_distance_addition(tmp >> 2U) + 1U;
            count = 3U;
        } else if (code < 442U) {
            uint32_t base, run;
            if (code < 358U) {
                base = 332U;
                run = 4U;
            } else if (code < 386U) {
                base = 358U;
                run = 5U;
            } else if (code < 414U) {
                base = 386U;
                run = 6U;
            } else {
                base = 414U;
                run = 7U;
            }
            tmp = code - base;
            if (!xpk_shrx_code(&ctx, (tmp >> 1U) + 1U, &extra)) return false;
            distance = ((extra << 1U) | (tmp & 1U)) + xpk_shrx_distance_addition(tmp >> 1U) + 1U;
            count = run;
        } else if (code < 498U) {
            uint32_t d, m;
            tmp = code - 442U;
            d = tmp / 14U;
            m = tmp % 14U;
            if (!xpk_shrx_code(&ctx, d + 2U, &extra)) return false;
            count = extra + xpk_shrx_distance_addition(d) + 8U;
            if (!xpk_shrx_code(&ctx, m + 2U, &extra)) return false;
            distance = extra + xpk_shrx_distance_addition(m) + 1U;
        } else if (code == 498U) {
            if (!xpk_shrx_code(&ctx, 16U, &count) || !xpk_shrx_code(&ctx, 16U, &distance)) return false;
        } else return false;
        if (!count || count > wanted - produced || !distance || (size_t)distance > previous_size + produced || ctx.state.vlen > UINT32_MAX - count) return false;
        history = previous_size + produced;
        for (i = 0U; i < count; ++i) {
            size_t source;
            if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
            source = history + i - distance;
            if (source < previous_size) output[produced + i] = previous[source];
            else output[produced + i] = output[source - previous_size];
        }
        produced += count;
        ctx.state.vlen += count;
    }
    if (xx_pd_is_stopped(pd)) return false;
    ctx.state.valid = true;
    *state = ctx.state;
    return true;
}
#endif
