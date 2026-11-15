/* Copyright (c) 2024-2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* Stateless character codecs. Callbacks consume/produce one Unicode scalar.
 * On error consumed/written is zero and the output scalar/buffer is unchanged.
 * Truncated input is distinguished from malformed input. Descriptors are
 * immutable and may be shared between threads; applications may supply their
 * own descriptor and callbacks. Use xx_codepage_decode/encode to validate
 * arguments before dispatching a callback. Callbacks receive nonempty input
 * and valid Unicode scalars respectively. UTF-16/32 use explicit byte order;
 * BOMs are returned as U+FEFF, and no platform locale is consulted. No allocation or platform runtime is needed. */
#ifndef XX_CODEPAGE_H
#define XX_CODEPAGE_H
#include <stddef.h>
#include <stdint.h>
#include "xxfclib/xxfc_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_codepage_status {
    XX_CODEPAGE_OK = 0,
    XX_CODEPAGE_INVALID,
    XX_CODEPAGE_INCOMPLETE,
    XX_CODEPAGE_UNMAPPABLE,
    XX_CODEPAGE_BUFFER_TOO_SMALL
} xx_codepage_status;
typedef struct xx_codepage xx_codepage;
struct xx_codepage {
    const char *name;
    unsigned int max_bytes;
    const void *context;
    xx_codepage_status (*decode)(const xx_codepage *, const unsigned char *, size_t, size_t *consumed, uint32_t *scalar);
    xx_codepage_status (*encode)(const xx_codepage *, uint32_t scalar, unsigned char *, size_t, size_t *written);
};
XXFC_API size_t xx_codepage_count(void);
XXFC_API const xx_codepage *xx_codepage_at(size_t index);
/* Case insensitive; hyphens, underscores and spaces are ignored. */
XXFC_API const xx_codepage *xx_codepage_find(const char *name);
/* Windows codepage numbers (for example 437, 932, 1252, 10000, 20866,
 * 28591 or 65001). Unsupported numbers return NULL; zero never means locale. */
XXFC_API const xx_codepage *xx_codepage_by_number(unsigned int number);
/* Decode one byte, using the caller's replacement for undefined values. */
XXFC_API uint32_t xx_codepage_decode_byte(const xx_codepage *, unsigned char, uint32_t replacement);
/* Convert a single-byte character into UTF-8. Returns its byte count, or
 * zero for an undefined byte or insufficient space; no terminator is added. */
XXFC_API size_t xx_codepage_byte_to_utf8(const xx_codepage *, unsigned char, unsigned char *, size_t);
/* CP932/Shift-JIS byte structure, including unassigned pairs. Readers that
 * escape raw names use these to keep a trail byte 0x5c inside its character.
 * Use decode to check whether a syntactically valid pair is assigned. */
XXFC_API int xx_codepage_sjis_lead_byte(unsigned char);
XXFC_API int xx_codepage_sjis_trail_byte(unsigned char);
/* Strict, allocation-free conversion without a terminator. On failure, used
 * and written describe the successfully converted prefix; a partial UTF-8
 * character is never written. Input and output must not overlap. */
XXFC_API xx_codepage_status xx_codepage_to_utf8(const xx_codepage *, const unsigned char *, size_t, unsigned char *, size_t, size_t *used,
                                                size_t *written);
XXFC_API xx_codepage_status xx_codepage_decode(const xx_codepage *, const unsigned char *, size_t, size_t *, uint32_t *);
XXFC_API xx_codepage_status xx_codepage_encode(const xx_codepage *, uint32_t, unsigned char *, size_t, size_t *);
/* Decode a complete buffer with WHATWG replacement semantics, matching
 * encoding_rs 0.8.42 decode_without_bom_handling. Labels use WHATWG aliases
 * (case insensitive, surrounding ASCII whitespace ignored), plus CP932,
 * CP936, CP949 and CP950. BOMs remain U+FEFF; malformed sequences emit U+FFFD
 * and set optional had_errors. Stateful ISO-2022-JP and Big5 combining
 * expansions are supported. No allocation, locale, or platform API is used.
 * Pass NULL output and zero capacity to obtain the exact required byte count.
 * written is required and receives the required count (saturating at SIZE_MAX
 * if unrepresentable); a small buffer
 * receives a complete UTF-8 prefix and returns BUFFER_TOO_SMALL. No terminator
 * is added. Unsupported labels return UNMAPPABLE. Input/output must not overlap.
 * Available when XXFC_CODEPAGE_ALL is enabled. */
XXFC_API xx_codepage_status xx_codepage_decode_whatwg(const char *label, const unsigned char *input, size_t input_size,
                                                    unsigned char *output, size_t capacity, size_t *written, int *had_errors);
#ifdef __cplusplus
}
#endif
#endif
