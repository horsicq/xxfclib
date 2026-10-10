/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient ByteKiller, ANC, ACE, GRAC,
 * McDisk and JEK wire layouts at commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834.
 * See LICENSE.ancient in this directory.
 */
#ifndef XX_BYTEKILLER_NATIVE_H
#define XX_BYTEKILLER_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "xxfclib/data/xx_data.h"

#define BK_MAX_PACKED (16U * 1024U * 1024U)
#define BK_MAX_RAW (64U * 1024U * 1024U)
#define BK_FOURCC(a, b, c, d) (((uint32_t)(a) << 24U) | ((uint32_t)(b) << 16U) | ((uint32_t)(c) << 8U) | (uint32_t)(d))

typedef enum bk_variant {
    BK_STANDARD,
    BK_PRO,
    BK_ACE,
    BK_ANC,
    BK_GRAC,
    BK_MD10,
    BK_MD11,
    BK_JEK
} bk_variant;
typedef struct bk_context {
    bk_variant variant;
    size_t packed_size, raw_size, stream_start, stream_end;
} bk_context;
typedef struct bk_bits {
    const uint8_t *data;
    size_t start, at;
    uint32_t control;
    unsigned available;
} bk_bits;

static bool bk_get32(const uint8_t *data, size_t size, size_t at, uint32_t *value)
{
    if (!data || at > size || size - at < 4U) return false;
    *value = xx_data_get_u32(data + at, 4, 0, true);
    return true;
}
static bool bk_parse_native(const uint8_t *data, size_t size, bk_context *ctx)
{
    uint32_t head, footer, value, checksum = 0U, word;
    size_t i;
    if (!data || !ctx || size < 12U || size > BK_MAX_PACKED + 16U || !bk_get32(data, size, 0U, &head) || !bk_get32(data, size, size - 4U, &footer)) return false;
    ctx->variant = BK_STANDARD;
    if (head == BK_FOURCC('A', 'C', 'E', '!')) ctx->variant = BK_ACE;
    else if (head == BK_FOURCC('F', 'V', 'L', '0')) ctx->variant = BK_ANC;
    else if (head == BK_FOURCC('G', 'R', '2', '0')) ctx->variant = BK_GRAC;
    else if (head == BK_FOURCC('M', 'D', '1', '0')) ctx->variant = BK_MD10;
    else if (head == BK_FOURCC('M', 'D', '1', '1')) ctx->variant = BK_MD11;
    else if (footer == BK_FOURCC('J', 'E', 'K', '!')) ctx->variant = BK_JEK;
    else if (footer == BK_FOURCC('d', 'a', 't', 'a')) ctx->variant = BK_PRO;
    ctx->packed_size = size;
    switch (ctx->variant) {
        case BK_STANDARD:
        case BK_PRO:
            if (!bk_get32(data, size, 0U, &value) || !value || (value & 3U) || value > BK_MAX_PACKED || value > size - 12U || !bk_get32(data, size, 4U, &value))
                return false;
            ctx->raw_size = value;
            ctx->stream_start = 12U;
            ctx->packed_size = (size_t)head + 12U;
            ctx->stream_end = ctx->packed_size;
            if (ctx->variant == BK_PRO && (ctx->packed_size > size - 4U || xx_data_get_u32(data + ctx->packed_size, 4, 0, true) != BK_FOURCC('d', 'a', 't', 'a')))
                return false;
            break;
        case BK_ACE:
            if (!bk_get32(data, size, 4U, &value) || !value || (value & 3U) || value > BK_MAX_PACKED || value > size - 12U) return false;
            ctx->packed_size = (size_t)value + 12U;
            ctx->stream_start = 16U;
            ctx->stream_end = ctx->packed_size;
            if (!bk_get32(data, size, 8U, &value)) return false;
            ctx->raw_size = value;
            break;
        case BK_ANC:
            if (!bk_get32(data, size, 4U, &value) || !value || (value & 3U) || value > BK_MAX_PACKED || value > size - 8U) return false;
            ctx->packed_size = (size_t)value + 8U;
            if (ctx->packed_size < 16U) return false;
            ctx->stream_start = 8U;
            ctx->stream_end = ctx->packed_size - 8U;
            if (!bk_get32(data, size, ctx->packed_size - 4U, &value)) return false;
            ctx->raw_size = value;
            break;
        case BK_GRAC:
            if (size < 16U) return false;
            ctx->stream_start = 8U;
            ctx->stream_end = size - 8U;
            ctx->raw_size = footer;
            break;
        case BK_MD10:
            ctx->stream_start = 4U;
            ctx->stream_end = size - 8U;
            ctx->raw_size = footer;
            break;
        case BK_MD11:
            if (size < 16U || !bk_get32(data, size, 4U, &value)) return false;
            value ^= BK_FOURCC('M', 'D', '1', '1');
            if ((value & 0xFFFFU) != (value >> 16U) || (value & 0xFFFFU) != footer) return false;
            ctx->raw_size = value & 0xFFFFU;
            ctx->stream_start = 8U;
            ctx->stream_end = size - 8U;
            break;
        case BK_JEK:
            if (size < 16U || !bk_get32(data, size, size - 8U, &value)) return false;
            ctx->raw_size = value;
            ctx->stream_start = 0U;
            ctx->stream_end = size - 12U;
            break;
    }
    if (!ctx->raw_size || ctx->raw_size > BK_MAX_RAW || ctx->stream_end < ctx->stream_start || (ctx->stream_end - ctx->stream_start) & 3U) return false;
    for (i = ctx->stream_start; i < ctx->stream_end; i += 4U) checksum ^= xx_data_get_u32(data + i, 4, 0, true);
    if (!bk_get32(data, size, ctx->variant == BK_STANDARD || ctx->variant == BK_PRO ? 8U : ctx->variant == BK_ACE ? 12U : ctx->stream_end, &word) || word != checksum)
        return false;
    return true;
}

static bool bk_word(bk_bits *bits, uint32_t *word)
{
    if (bits->at < bits->start + 4U) return false;
    bits->at -= 4U;
    *word = xx_data_get_u32(bits->data + bits->at, 4, 0, true);
    return true;
}
static bool bk_bit(bk_bits *bits, uint32_t *value)
{
    if (!bits->available) {
        if (!bk_word(bits, &bits->control)) return false;
        bits->available = 32U;
    }
    *value = bits->control & 1U;
    bits->control >>= 1U;
    --bits->available;
    return true;
}
static bool bk_bits_be(bk_bits *bits, unsigned count, uint32_t *value)
{
    uint32_t result = 0U, bit;
    unsigned i;
    if (count > 12U) return false;
    for (i = 0U; i < count; ++i) {
        if (!bk_bit(bits, &bit)) return false;
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}
static bool bk_copy(uint8_t *out, size_t *at, size_t size, uint32_t distance, uint32_t count, xx_pd_struct *pd)
{
    uint32_t i;
    if (!distance || distance > size - *at || count > *at) return false;
    for (i = 0U; i < count; ++i) {
        if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        out[*at - 1U] = out[*at + distance - 1U];
        --*at;
    }
    return true;
}
static bool bk_decode_native(const uint8_t *data, size_t size, const bk_context *ctx, uint8_t *out, size_t capacity, xx_pd_struct *pd)
{
    bk_bits bits;
    size_t at;
    uint32_t anchor;
    unsigned highest;
    if (!data || !ctx || !out || ctx->raw_size > capacity || ctx->packed_size > size || xx_pd_is_stopped(pd)) return false;
    bits.data = data;
    bits.start = ctx->stream_start;
    bits.at = ctx->stream_end;
    bits.control = 0U;
    bits.available = 0U;
    if (!bk_word(&bits, &anchor)) return false;
    for (highest = 32U; highest > 0U; --highest)
        if (anchor & (UINT32_C(1) << (highest - 1U))) break;
    if (highest) {
        bits.available = highest - 1U;
        bits.control = anchor & ((UINT32_C(1) << bits.available) - 1U);
    }
    at = ctx->raw_size;
    while (at) {
        uint32_t first, second, third, kind, count, distance, value, i;
        if ((at & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        if (!bk_bit(&bits, &first) || !bk_bit(&bits, &second)) return false;
        if (!first) kind = second;
        else {
            if (!bk_bit(&bits, &third)) return false;
            kind = 2U + (second << 1U) + third;
        }
        switch (kind) {
            case 0U:
                if (!bk_bits_be(&bits, 3U, &count)) return false;
                ++count;
                if (count > at) return false;
                for (i = 0U; i < count; ++i) {
                    if (!bk_bits_be(&bits, 8U, &value)) return false;
                    out[--at] = (uint8_t)value;
                }
                break;
            case 1U:
            case 2U:
            case 3U:
                if (!bk_bits_be(&bits, kind == 1U ? 8U : kind == 2U ? 9U : 10U, &distance) || !bk_copy(out, &at, ctx->raw_size, distance, kind + 1U, pd)) return false;
                break;
            case 4U:
                if (!bk_bits_be(&bits, 8U, &count) || !bk_bits_be(&bits, 12U, &distance) || !bk_copy(out, &at, ctx->raw_size, distance, count + 1U, pd)) return false;
                break;
            case 5U:
                if (!bk_bits_be(&bits, 8U, &count)) return false;
                count += 9U;
                if (count > at) return false;
                for (i = 0U; i < count; ++i) {
                    if (!bk_bits_be(&bits, 8U, &value)) return false;
                    out[--at] = (uint8_t)value;
                }
                break;
            default: return false;
        }
    }
    return bits.available == 0U && bits.at == bits.start && !xx_pd_is_stopped(pd);
}
#endif
