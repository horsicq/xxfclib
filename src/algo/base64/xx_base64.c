/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_base64.c
 * @brief Buffer base64 coding with a caller-supplied alphabet.
 */

#include "xxfclib/algo/base64/xx_base64.h"

#include <string.h>

size_t xx_base64_encoded_size(size_t length, bool pad)
{
    size_t groups = length / 3U;
    unsigned remainder = (unsigned)(length % 3U);
    size_t size;
    if (groups > (SIZE_MAX - 4U) / 4U) return 0U;
    size = groups * 4U;
    if (remainder) size += pad ? 4U : (size_t)remainder + 1U;
    return size;
}

bool xx_base64_encode_buffer(const uint8_t *in, size_t length,
                             const char alphabet[64], bool pad,
                             char *out, size_t out_size, size_t *out_length)
{
    size_t written = 0U, i = 0U;
    if (!alphabet || !out || (length && !in)) return false;
    if (length / 3U > (SIZE_MAX - 4U) / 4U) return false;
    if (out_size < xx_base64_encoded_size(length, pad)) return false;
    while (i + 3U <= length) {
        uint32_t bits = ((uint32_t)in[i] << 16) |
                        ((uint32_t)in[i + 1U] << 8) | in[i + 2U];
        out[written++] = alphabet[(bits >> 18) & 63U];
        out[written++] = alphabet[(bits >> 12) & 63U];
        out[written++] = alphabet[(bits >> 6) & 63U];
        out[written++] = alphabet[bits & 63U];
        i += 3U;
    }
    if (i < length) {
        uint32_t bits = (uint32_t)in[i] << 16;
        if (i + 1U < length) bits |= (uint32_t)in[i + 1U] << 8;
        out[written++] = alphabet[(bits >> 18) & 63U];
        out[written++] = alphabet[(bits >> 12) & 63U];
        if (i + 1U < length)
            out[written++] = alphabet[(bits >> 6) & 63U];
        if (pad)
            while (written % 4U) out[written++] = '=';
    }
    if (out_length) *out_length = written;
    return true;
}

bool xx_base64_decode_buffer(const char *in, size_t length,
                             const char alphabet[64],
                             uint8_t *out, size_t out_capacity,
                             size_t *out_length)
{
    int8_t reverse[256];
    size_t i = 0U, used = 0U, needed;
    if (!in || !alphabet || !out) return false;
    memset(reverse, -1, sizeof(reverse));
    for (size_t symbol = 0; symbol < 64U; ++symbol)
        reverse[(uint8_t)alphabet[symbol]] = (int8_t)symbol;
    if (length % 4U == 1U) return false;
    needed = (length / 4U) * 3U;
    if (length % 4U == 2U) needed += 1U;
    else if (length % 4U == 3U) needed += 2U;
    if (needed > out_capacity) return false;
    while (i + 4U <= length) {
        int a = reverse[(uint8_t)in[i]], b = reverse[(uint8_t)in[i + 1U]];
        int c = reverse[(uint8_t)in[i + 2U]], d = reverse[(uint8_t)in[i + 3U]];
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        out[used++] = (uint8_t)((a << 2) | (b >> 4));
        out[used++] = (uint8_t)((b << 4) | (c >> 2));
        out[used++] = (uint8_t)((c << 6) | d);
        i += 4U;
    }
    if (length - i == 2U) {
        int a = reverse[(uint8_t)in[i]], b = reverse[(uint8_t)in[i + 1U]];
        if (a < 0 || b < 0 || (b & 15)) return false;
        out[used++] = (uint8_t)((a << 2) | (b >> 4));
    } else if (length - i == 3U) {
        int a = reverse[(uint8_t)in[i]], b = reverse[(uint8_t)in[i + 1U]];
        int c = reverse[(uint8_t)in[i + 2U]];
        if (a < 0 || b < 0 || c < 0 || (c & 3)) return false;
        out[used++] = (uint8_t)((a << 2) | (b >> 4));
        out[used++] = (uint8_t)((b << 4) | (c >> 2));
    }
    if (out_length) *out_length = used;
    return true;
}
