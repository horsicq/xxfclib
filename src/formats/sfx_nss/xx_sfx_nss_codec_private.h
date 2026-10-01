/* Norton Secret Stuff's bounded LZW and 0x90 run-length decoder.
 * Derived from the 16-bit self-extractor's decode routine. */
#ifndef XX_SFX_NSS_CODEC_PRIVATE_H
#define XX_SFX_NSS_CODEC_PRIVATE_H

#include "xxfclib/memory/xx_memory.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NSS_CODEC_MAX_CODE 7935U
#define NSS_CODEC_HISTORY 500U

typedef struct nss_codec_context_s {
    int16_t prefix[NSS_CODEC_MAX_CODE + 1U];
    uint8_t suffix[NSS_CODEC_MAX_CODE + 1U];
    uint8_t use_count[NSS_CODEC_MAX_CODE + 1U];
    uint8_t stack[NSS_CODEC_MAX_CODE + 1U];
    uint8_t history[NSS_CODEC_HISTORY];
} nss_codec_context;

typedef struct nss_codec_bits_s {
    const uint8_t *data;
    size_t size;
    size_t byte_at;
    unsigned bit_at;
} nss_codec_bits;

static bool nss_codec_read_bits(nss_codec_bits *bits, unsigned count,
                                unsigned *value) {
    unsigned result = 0U, index;
    if (!bits || !value || count > 13U) return false;
    for (index = 0U; index < count; ++index) {
        if (bits->byte_at >= bits->size) return false;
        result |= (unsigned)((bits->data[bits->byte_at] >> bits->bit_at) &
                             1U) << index;
        if (++bits->bit_at == 8U) {
            bits->bit_at = 0U;
            ++bits->byte_at;
        }
    }
    *value = result;
    return true;
}

static bool nss_codec_emit(uint8_t value, uint8_t *output,
                           size_t output_size, size_t *output_at,
                           bool *escape, uint8_t *previous) {
    size_t repeat;
    if (*escape) {
        *escape = false;
        if (!value) {
            if (*output_at == output_size) return false;
            output[(*output_at)++] = 0x90U;
            return true;
        }
        repeat = (size_t)value - 1U;
        if (repeat > output_size - *output_at) return false;
        memset(output + *output_at, *previous, repeat);
        *output_at += repeat;
        return true;
    }
    if (value == 0x90U) {
        *escape = true;
        return true;
    }
    if (*output_at == output_size) return false;
    output[(*output_at)++] = value;
    *previous = value;
    return true;
}

/* `input` begins just after the decrypted eight-byte SYMANTEC verifier.
 * `output_size` is the original size from the member header. A malformed
 * stream never writes outside the caller's output buffer. */
static bool nss_decode_lzw_rle(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size) {
    nss_codec_context *ctx;
    nss_codec_bits bits;
    size_t output_at = 0U, history_at = 0U, token_count = 0U;
    unsigned history_hits = 0U, width = 1U, limit = 2U;
    unsigned next_code = 0U, replacement = 1U;
    int32_t previous_code = 0;
    uint8_t previous_byte = 0U;
    bool high_mode = true, escape = false, success = false;
    if ((!input && input_size) || (!output && output_size) ||
        !output_size || !input_size || input_size > SIZE_MAX / 8U)
        return false;
    ctx = (nss_codec_context *)xx_mem_calloc(1U, sizeof(*ctx));
    if (!ctx) return false;
    bits.data = input;
    bits.size = input_size;
    bits.byte_at = 0U;
    bits.bit_at = 0U;
    for (;;) {
        unsigned raw_code, selector = 0U;
        int32_t code, node;
        size_t depth = 0U, index;
        uint8_t first;
        bool has_bits;
        if (high_mode) {
            has_bits = nss_codec_read_bits(&bits, 1U, &selector);
            if (!has_bits) {
                success = output_at == output_size && !escape;
                break;
            }
            if (!nss_codec_read_bits(&bits, selector ? width : 8U,
                                     &raw_code)) break;
            code = selector ? (int32_t)raw_code : ~(int32_t)raw_code;
        } else {
            has_bits = nss_codec_read_bits(&bits, width, &raw_code);
            if (!has_bits) {
                success = output_at == output_size && !escape;
                break;
            }
            code = (int32_t)raw_code - 256;
        }
        if (!code) {
            success = output_at == output_size && !escape;
            break;
        }
        if (++token_count > input_size * 8U || code < -256 ||
            code > (int32_t)next_code) break;
        node = code;
        while (node > 0) {
            unsigned slot = (unsigned)node;
            if (slot > next_code || depth == NSS_CODEC_MAX_CODE ||
                ctx->prefix[slot] == (int16_t)slot) break;
            ctx->stack[depth++] = ctx->suffix[slot];
            ctx->use_count[slot] = 4U;
            node = ctx->prefix[slot];
        }
        if (node > 0 || node < -256 || depth > NSS_CODEC_MAX_CODE) break;
        first = (uint8_t)~node;
        ctx->stack[depth++] = first;
        for (index = depth; index > 0U; --index) {
            if (!nss_codec_emit(ctx->stack[index - 1U], output,
                                output_size, &output_at, &escape,
                                &previous_byte)) goto done;
        }
        if (previous_code) {
            unsigned slot;
            if (next_code < NSS_CODEC_MAX_CODE) {
                slot = ++next_code;
            } else {
                unsigned best = 257U, scanned, start = replacement;
                slot = 0U;
                for (scanned = 0U; scanned < NSS_CODEC_MAX_CODE;
                     ++scanned) {
                    unsigned seen;
                    if (++replacement > NSS_CODEC_MAX_CODE)
                        replacement = 1U;
                    seen = ctx->use_count[replacement];
                    if (seen < best) {
                        best = seen;
                        slot = replacement;
                    }
                    --ctx->use_count[replacement];
                    if (!ctx->use_count[replacement] ||
                        replacement == start) break;
                }
                if (!slot) break;
            }
            ctx->prefix[slot] = (int16_t)previous_code;
            ctx->suffix[slot] = first;
            ctx->use_count[slot] = 2U;
            if (next_code >= limit) {
                if (++width > 13U) break;
                limit = (1U << width) - (high_mode ? 0U : 256U);
            }
        }
        previous_code = code;
        history_hits -= ctx->history[history_at];
        ctx->history[history_at] = code > 0 ? 1U : 0U;
        history_hits += ctx->history[history_at];
        if (++history_at == NSS_CODEC_HISTORY) history_at = 0U;
        if ((history_hits < 375U) != high_mode) {
            high_mode = history_hits < 375U;
            limit = (1U << width) - (high_mode ? 0U : 256U);
            /* Low mode stores dictionary references with a +256 bias.
             * Raise the width before reading its first code if that bias
             * makes the current dictionary exceed the old bit range. */
            if (next_code >= limit) {
                if (++width > 13U) break;
                limit = (1U << width) - (high_mode ? 0U : 256U);
            }
        }
    }
done:
    xx_mem_free(ctx);
    return success;
}

#endif /* XX_SFX_NSS_CODEC_PRIVATE_H */
