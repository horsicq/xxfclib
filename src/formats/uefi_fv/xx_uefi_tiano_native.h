/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Original bounded implementation of the EFI 1.1 / Tiano static-Huffman
 * compression format. The bitstream layout was checked against EDK2
 * BaseTools/Source/C/Common/Decompress.c (BSD-2-Clause-Patent). This code does
 * not copy that implementation's table builder or unchecked lookahead.
 */
#ifndef XX_UEFI_TIANO_NATIVE_H
#define XX_UEFI_TIANO_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XX_TIANO_NC 510U
#define XX_TIANO_NT 19U
#define XX_TIANO_NP 31U

typedef struct xx_tiano_bits_s {
    const uint8_t *data;
    size_t size;
    size_t bit;
} xx_tiano_bits;

typedef struct xx_tiano_huff_s {
    uint16_t symbol[XX_TIANO_NC];
    uint16_t count[17];
    uint16_t first_index[17];
    uint32_t first_code[17];
    uint16_t constant;
    bool is_constant;
} xx_tiano_huff;

static bool xx_tiano_read(xx_tiano_bits *bits, unsigned count, uint32_t *value) {
    uint32_t result = 0;
    unsigned i;
    if (!bits || !value || count > 31U ||
        bits->bit > bits->size * 8U ||
        count > bits->size * 8U - bits->bit) return false;
    for (i = 0; i < count; ++i) {
        size_t pos = bits->bit++;
        result = (result << 1) |
                 ((bits->data[pos >> 3] >> (7U - (unsigned)(pos & 7U))) & 1U);
    }
    *value = result;
    return true;
}

static bool xx_tiano_huff_make(xx_tiano_huff *h, const uint8_t *length,
                               unsigned symbols) {
    uint32_t code = 0;
    unsigned i, n, out = 0;
    if (!h || !length || symbols > XX_TIANO_NC) return false;
    memset(h, 0, sizeof(*h));
    for (i = 0; i < symbols; ++i) {
        if (length[i] > 16U) return false;
        if (length[i] != 0U) ++h->count[length[i]];
    }
    for (n = 1; n <= 16U; ++n) {
        code = (code + h->count[n - 1U]) << 1;
        h->first_code[n] = code;
        h->first_index[n] = (uint16_t)out;
        out += h->count[n];
        if (code + h->count[n] > (1U << n)) return false;
    }
    if (code + h->count[16] != 65536U) return false;
    out = 0;
    for (n = 1; n <= 16U; ++n)
        for (i = 0; i < symbols; ++i)
            if (length[i] == n) h->symbol[out++] = (uint16_t)i;
    return true;
}

static bool xx_tiano_decode_symbol(xx_tiano_bits *bits,
                                   const xx_tiano_huff *h, uint32_t *symbol) {
    uint32_t code = 0, bit;
    unsigned n;
    if (!h || !symbol) return false;
    if (h->is_constant) {
        *symbol = h->constant;
        return true;
    }
    for (n = 1; n <= 16U; ++n) {
        if (!xx_tiano_read(bits, 1U, &bit)) return false;
        code = (code << 1) | bit;
        if (code >= h->first_code[n] &&
            code - h->first_code[n] < h->count[n]) {
            *symbol = h->symbol[h->first_index[n] +
                                (unsigned)(code - h->first_code[n])];
            return true;
        }
    }
    return false;
}

static bool xx_tiano_read_pt(xx_tiano_bits *bits, xx_tiano_huff *h,
                             unsigned symbols, unsigned nbits, bool special) {
    uint8_t length[XX_TIANO_NP] = {0};
    uint32_t count, value;
    unsigned i = 0;
    if (!xx_tiano_read(bits, nbits, &count) || count > symbols) return false;
    if (count == 0U) {
        if (!xx_tiano_read(bits, nbits, &value) || value >= symbols)
            return false;
        memset(h, 0, sizeof(*h));
        h->is_constant = true;
        h->constant = (uint16_t)value;
        return true;
    }
    while (i < count) {
        if (!xx_tiano_read(bits, 3U, &value)) return false;
        if (value == 7U) {
            uint32_t bit;
            do {
                if (!xx_tiano_read(bits, 1U, &bit)) return false;
                if (bit && ++value > 16U) return false;
            } while (bit);
        }
        length[i++] = (uint8_t)value;
        if (special && i == 3U) {
            if (!xx_tiano_read(bits, 2U, &value) || value > symbols - i)
                return false;
            i += (unsigned)value;
        }
    }
    return xx_tiano_huff_make(h, length, symbols);
}

static bool xx_tiano_read_c(xx_tiano_bits *bits, xx_tiano_huff *c,
                            const xx_tiano_huff *extra) {
    uint8_t length[XX_TIANO_NC] = {0};
    uint32_t count, value;
    unsigned i = 0;
    if (!xx_tiano_read(bits, 9U, &count) || count > XX_TIANO_NC)
        return false;
    if (count == 0U) {
        if (!xx_tiano_read(bits, 9U, &value) || value >= XX_TIANO_NC)
            return false;
        memset(c, 0, sizeof(*c));
        c->is_constant = true;
        c->constant = (uint16_t)value;
        return true;
    }
    while (i < count) {
        if (!xx_tiano_decode_symbol(bits, extra, &value)) return false;
        if (value <= 2U) {
            uint32_t run = 1U;
            if (value == 1U) {
                if (!xx_tiano_read(bits, 4U, &run)) return false;
                run += 3U;
            } else if (value == 2U) {
                if (!xx_tiano_read(bits, 9U, &run)) return false;
                run += 20U;
            }
            if (run > count - i) return false;
            i += (unsigned)run;
        } else {
            if (value - 2U > 16U) return false;
            length[i++] = (uint8_t)(value - 2U);
        }
    }
    return xx_tiano_huff_make(c, length, XX_TIANO_NC);
}

/* Version 1 is EFI standard (PBIT=4); version 2 is GUID-defined Tiano
 * (PBIT=5). The caller has already imposed the format memory/output budget.
 * The cancellation callback is optional and runs at least every 4096 symbols. */
static bool xx_uefi_tiano_decompress_memory(const uint8_t *source,
                                            size_t source_size,
                                            uint8_t *output,
                                            size_t output_size,
                                            unsigned version,
                                            bool (*cancelled)(void *),
                                            void *cancel_ctx) {
    xx_tiano_bits bits;
    xx_tiano_huff extra, c, pos;
    uint32_t packed, original, remaining = 0;
    size_t out = 0, next_poll = 0;
    if (!source || !output || source_size < 8U || source_size > UINT32_MAX ||
        output_size > UINT32_MAX || (version != 1U && version != 2U) ||
        source_size > SIZE_MAX / 8U) return false;
    packed = (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
             ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
    original = (uint32_t)source[4] | ((uint32_t)source[5] << 8) |
               ((uint32_t)source[6] << 16) | ((uint32_t)source[7] << 24);
    if ((size_t)packed != source_size - 8U ||
        (size_t)original != output_size) return false;
    if (output_size == 0U) return true;
    bits.data = source + 8U;
    bits.size = (size_t)packed;
    bits.bit = 0;
    memset(&extra, 0, sizeof(extra));
    memset(&c, 0, sizeof(c));
    memset(&pos, 0, sizeof(pos));
    while (out < output_size) {
        uint32_t symbol;
        if (cancelled && out >= next_poll) {
            if (cancelled(cancel_ctx)) return false;
            next_poll = out + 4096U;
        }
        if (remaining == 0U) {
            if (!xx_tiano_read(&bits, 16U, &remaining) || remaining == 0U ||
                !xx_tiano_read_pt(&bits, &extra, XX_TIANO_NT, 5U, true) ||
                !xx_tiano_read_c(&bits, &c, &extra) ||
                !xx_tiano_read_pt(&bits, &pos, XX_TIANO_NP,
                                  version == 1U ? 4U : 5U, false))
                return false;
        }
        --remaining;
        if (!xx_tiano_decode_symbol(&bits, &c, &symbol)) return false;
        if (symbol < 256U) {
            output[out++] = (uint8_t)symbol;
        } else {
            uint32_t p, lo, length = symbol - 253U;
            size_t distance, i;
            if (length > output_size - out ||
                !xx_tiano_decode_symbol(&bits, &pos, &p) || p > 30U)
                return false;
            if (p > 1U) {
                if (!xx_tiano_read(&bits, p - 1U, &lo)) return false;
                distance = ((size_t)1U << (p - 1U)) + (size_t)lo;
            } else {
                distance = (size_t)p;
            }
            if (distance >= out) return false;
            for (i = 0; i < length; ++i) {
                output[out] = output[out - distance - 1U];
                ++out;
            }
        }
    }
    return !cancelled || !cancelled(cancel_ctx);
}

#endif /* XX_UEFI_TIANO_NATIVE_H */
