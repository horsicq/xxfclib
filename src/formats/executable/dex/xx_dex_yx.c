/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <xxfclib/formats/executable/dex/xx_dex_yx.h>

#include "xxfclib/algo/sha/xx_sha.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/global/xx_global.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
     defined(__x86_64__)) && !defined(_M_ARM64EC)
#define XX_DEX_YX_X86 1
#include <immintrin.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_DEX_YX_X86)
#define XX_DEX_YX_TARGET_SSE2 __attribute__((target("sse2")))
#define XX_DEX_YX_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_DEX_YX_TARGET_SSE2
#define XX_DEX_YX_TARGET_AVX2
#endif

#define XX_DEX_YX_MAX_ITEMS ((size_t)1000000)

/* A string is a view into the scanned data when its bytes are valid UTF-8
   (the common case, no copy), otherwise into the decoded MUTF-8 buffer. */
typedef struct { size_t offset; uint32_t length; bool decoded; } xx_dex_yx_text;
typedef struct { const uint8_t *bytes; size_t length; } xx_dex_yx_view;
typedef struct {
    uint32_t shorty, return_type;
    uint32_t parameters_start, parameters_count;
} xx_dex_yx_proto;
typedef struct { uint32_t class_type, type, name; } xx_dex_yx_field;
typedef struct { uint32_t class_type, proto, name; } xx_dex_yx_method;
typedef struct {
    uint32_t class_type, access_flags, superclass, source_file;
    bool has_superclass, has_source_file;
} xx_dex_yx_class;

/* Collections are parsed on first access; each stage depends only on the
   counts of the stages it lists as prerequisites, as in the eager order
   strings, types, protos, fields, methods, class_defs. */
enum {
    XX_DEX_YX_STRINGS = 1u << 0,
    XX_DEX_YX_TYPES = 1u << 1,
    XX_DEX_YX_PROTOS = 1u << 2,
    XX_DEX_YX_FIELDS = 1u << 3,
    XX_DEX_YX_METHODS = 1u << 4,
    XX_DEX_YX_CLASSES = 1u << 5
};

struct xx_dex_yx_result {
    const uint8_t *data;
    size_t size;
    bool is_dex;
    uint32_t version;
    uint32_t checksum, file_size, header_size, endian_tag, link_size, link_off;
    uint32_t map_off, string_ids_size, string_ids_off, type_ids_size,
             type_ids_off, proto_ids_size, proto_ids_off, field_ids_size,
             field_ids_off, method_ids_size, method_ids_off, class_defs_size,
             class_defs_off, data_size, data_off, container_size, header_offset;
    bool has_container_fields, has_map_list;
    uint32_t map_declared_size;
    char header_signature[41];
    /* Lazy state: stages parsed, stages that failed (allocation). */
    unsigned ready, failed;
    xx_dex_yx_text *strings;
    size_t strings_count;
    uint8_t *decoded;
    size_t decoded_used, decoded_capacity;
    uint32_t *types;
    size_t types_count;
    xx_dex_yx_proto *protos;
    size_t protos_count;
    uint16_t *parameters;
    size_t parameters_used, parameters_capacity;
    xx_dex_yx_field *fields;
    size_t fields_count;
    xx_dex_yx_method *methods;
    size_t methods_count;
    xx_dex_yx_class *classes;
    size_t classes_count;
    /* map_list items are read in place: map_off + 4 + index * 12. */
    size_t map_items_count;
};

/* Shared by every non-DEX input. All of its collections are empty and no
   stage runs for it, so it is never written. */
static const xx_dex_yx_result xx_dex_yx_not_dex = {0};

static bool xx_dex_yx_span(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static uint16_t xx_dex_yx_u16(const uint8_t *data, size_t at)
{
    return (uint16_t)data[at] | (uint16_t)((uint16_t)data[at + 1] << 8);
}

static uint32_t xx_dex_yx_u32(const uint8_t *data, size_t at)
{
    return (uint32_t)data[at] | ((uint32_t)data[at + 1] << 8) |
           ((uint32_t)data[at + 2] << 16) | ((uint32_t)data[at + 3] << 24);
}

static void xx_dex_yx_hex_lower(const uint8_t *data, size_t length, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    out[length * 2] = '\0';
}

static bool xx_dex_yx_magic(const uint8_t *d, size_t size)
{
    return size >= 112 && memcmp(d, "dex\n0", 5) == 0 && d[7] == 0 &&
           ((d[5] == '3' && d[6] >= '5' && d[6] <= '9') ||
            (d[5] == '4' && d[6] >= '0' && d[6] <= '1'));
}

static bool xx_dex_yx_parse_header(xx_dex_yx_result *r)
{
    const uint8_t *d = r->data;
    if (!xx_dex_yx_magic(d, r->size)) return false;
    r->version = (uint32_t)(d[5] - '0') * 10 + (uint32_t)(d[6] - '0');
    if (r->version == 41 && r->size < 120) return false;
    r->checksum = xx_dex_yx_u32(d, 8);
    xx_dex_yx_hex_lower(d + 12, 20, r->header_signature);
    r->file_size = xx_dex_yx_u32(d, 32);
    r->header_size = xx_dex_yx_u32(d, 36);
    r->endian_tag = xx_dex_yx_u32(d, 40);
    r->link_size = xx_dex_yx_u32(d, 44);
    r->link_off = xx_dex_yx_u32(d, 48);
    r->map_off = xx_dex_yx_u32(d, 52);
    r->string_ids_size = xx_dex_yx_u32(d, 56);
    r->string_ids_off = xx_dex_yx_u32(d, 60);
    r->type_ids_size = xx_dex_yx_u32(d, 64);
    r->type_ids_off = xx_dex_yx_u32(d, 68);
    r->proto_ids_size = xx_dex_yx_u32(d, 72);
    r->proto_ids_off = xx_dex_yx_u32(d, 76);
    r->field_ids_size = xx_dex_yx_u32(d, 80);
    r->field_ids_off = xx_dex_yx_u32(d, 84);
    r->method_ids_size = xx_dex_yx_u32(d, 88);
    r->method_ids_off = xx_dex_yx_u32(d, 92);
    r->class_defs_size = xx_dex_yx_u32(d, 96);
    r->class_defs_off = xx_dex_yx_u32(d, 100);
    r->data_size = xx_dex_yx_u32(d, 104);
    r->data_off = xx_dex_yx_u32(d, 108);
    if (r->version == 41) {
        r->has_container_fields = true;
        r->container_size = xx_dex_yx_u32(d, 112);
        r->header_offset = xx_dex_yx_u32(d, 116);
    }
    /* YARA-X compares offsets against data.len() truncated to u32. */
    uint32_t file_length = (uint32_t)r->size;
    if (r->file_size > file_length || r->header_size != 112 ||
        (r->endian_tag != 0x12345678 && r->endian_tag != 0x78563412) ||
        r->link_off > file_length || r->map_off > file_length ||
        r->string_ids_off > file_length || r->type_ids_off > file_length ||
        r->proto_ids_off > file_length || r->field_ids_off > file_length ||
        r->method_ids_off > file_length || r->class_defs_off > file_length ||
        r->data_off > file_length || r->type_ids_size > UINT16_MAX ||
        r->proto_ids_size > UINT16_MAX)
        return false;
    r->is_dex = true;
    return true;
}

static bool xx_dex_yx_uleb128(const uint8_t *data, size_t size, size_t *at,
                        uint64_t *value)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 10; ++i) {
        if (*at >= size) return false;
        uint8_t byte = data[(*at)++];
        result |= (uint64_t)(byte & 0x7f) << (i * 7);
        if (!(byte & 0x80)) { *value = result; return true; }
    }
    return false;
}

static bool xx_dex_yx_is_cont(uint8_t ch) { return (ch & 0xc0) == 0x80; }

/* ---------------------------------------------------- ASCII run search */

/* SIMD level for the ASCII runs: 0 scalar, 1 SSE2, 2 AVX2. Read once per
   stage or call; the xxfclib feature flags are process-wide and immutable
   after start-up. */
static int xx_dex_yx_simd_level(void)
{
#ifdef XX_DEX_YX_X86
    if (xx_is_avx2_enabled()) return 2;
    if (xx_is_sse2_enabled()) return 1;
#endif
    return 0;
}

#ifdef XX_DEX_YX_X86
static unsigned xx_dex_yx_ctz32(uint32_t value)
{
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, value);
    return (unsigned)index;
#else
    return (unsigned)__builtin_ctz(value);
#endif
}
#endif

/* Length of the leading run of s[0..length) whose bytes are below 0x80,
   and with no_zero also not 0. */
static size_t xx_dex_yx_ascii_run_scalar(const uint8_t *s, size_t length,
                                      bool no_zero)
{
    const uint64_t high = UINT64_C(0x8080808080808080);
    const uint64_t ones = UINT64_C(0x0101010101010101);
    size_t i = 0;
    for (; length - i >= 8; i += 8) {
        uint64_t word;
        memcpy(&word, s + i, 8);
        uint64_t bad = word & high;
        if (no_zero) bad |= (word - ones) & ~word & high;
        if (bad) break;
    }
    for (; i < length; ++i)
        if (s[i] >= 0x80 || (no_zero && !s[i])) break;
    return i;
}

#ifdef XX_DEX_YX_X86
/* readable >= length bytes at s may be loaded; vectors never cross
   s + readable, the bytes past length are masked off. */
XX_DEX_YX_TARGET_SSE2
static size_t xx_dex_yx_ascii_run_sse2(const uint8_t *s, size_t length,
                                    size_t readable, bool no_zero)
{
    const __m128i zero = _mm_setzero_si128();
    size_t i = 0;
    while (i < length && readable - i >= 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(s + i));
        uint32_t mask = (uint32_t)_mm_movemask_epi8(v);
        if (no_zero)
            mask |= (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(v, zero));
        size_t left = length - i;
        if (left < 16) mask &= (1u << left) - 1u;
        if (mask) return i + xx_dex_yx_ctz32(mask);
        if (left <= 16) return length;
        i += 16;
    }
    return i + xx_dex_yx_ascii_run_scalar(s + i, length - i, no_zero);
}

XX_DEX_YX_TARGET_AVX2
static size_t xx_dex_yx_ascii_run_avx2(const uint8_t *s, size_t length,
                                    size_t readable, bool no_zero)
{
    const __m256i zero = _mm256_setzero_si256();
    size_t i = 0;
    while (i < length && readable - i >= 32) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(const void *)(s + i));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(v);
        if (no_zero)
            mask |= (uint32_t)_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(v, zero));
        size_t left = length - i;
        if (left < 32) mask &= (1u << left) - 1u;
        if (mask) { _mm256_zeroupper(); return i + xx_dex_yx_ctz32(mask); }
        if (left <= 32) { _mm256_zeroupper(); return length; }
        i += 32;
    }
    _mm256_zeroupper();
    if (i >= length) return length;
    return i + xx_dex_yx_ascii_run_sse2(s + i, length - i, readable - i,
                                     no_zero);
}
#endif

static size_t xx_dex_yx_ascii_run(const uint8_t *s, size_t length,
                               size_t readable, int simd, bool no_zero)
{
#ifdef XX_DEX_YX_X86
    if (simd == 2) return xx_dex_yx_ascii_run_avx2(s, length, readable, no_zero);
    if (simd == 1) return xx_dex_yx_ascii_run_sse2(s, length, readable, no_zero);
#else
    (void)readable;
    (void)simd;
#endif
    return xx_dex_yx_ascii_run_scalar(s, length, no_zero);
}

/* ------------------------------------------------- UTF-8 and MUTF-8 */

/* Width of the multi-byte UTF-8 sequence at s (s[0] >= 0x80) within left
   bytes, 0 when it is malformed. */
static size_t xx_dex_yx_utf8_sequence(const uint8_t *s, size_t left)
{
    uint8_t a = s[0];
    if (a >= 0xc2 && a <= 0xdf)
        return left >= 2 && xx_dex_yx_is_cont(s[1]) ? 2 : 0;
    if (a >= 0xe0 && a <= 0xef)
        return left >= 3 && xx_dex_yx_is_cont(s[1]) && xx_dex_yx_is_cont(s[2]) &&
               !(a == 0xe0 && s[1] < 0xa0) &&
               !(a == 0xed && s[1] >= 0xa0) ? 3 : 0;
    if (a >= 0xf0 && a <= 0xf4)
        return left >= 4 && xx_dex_yx_is_cont(s[1]) && xx_dex_yx_is_cont(s[2]) &&
               xx_dex_yx_is_cont(s[3]) && !(a == 0xf0 && s[1] < 0x90) &&
               !(a == 0xf4 && s[1] >= 0x90) ? 4 : 0;
    return 0;
}

static bool xx_dex_yx_utf8_valid(const uint8_t *s, size_t length,
                              size_t readable, int simd)
{
    size_t i = 0;
    while (i < length) {
        i += xx_dex_yx_ascii_run(s + i, length - i, readable - i, simd, false);
        /* Stay scalar across a run of multi-byte sequences. */
        while (i < length && s[i] >= 0x80) {
            size_t width = xx_dex_yx_utf8_sequence(s + i, length - i);
            if (!width) return false;
            i += width;
        }
    }
    return true;
}

static bool xx_dex_yx_valid_utf8(const uint8_t *data, size_t length)
{
    if (!data && length) return false;
    return xx_dex_yx_utf8_valid(data, length, length, xx_dex_yx_simd_level());
}

/* simd_cesu8::mutf8::decode semantics for input that is not valid UTF-8.
   text has room for length bytes: no sequence grows when decoded. */
static bool xx_dex_yx_decode_mutf8(const uint8_t *data, size_t length,
                            size_t readable, int simd, uint8_t *text,
                            size_t *out_length)
{
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        size_t run = xx_dex_yx_ascii_run(data + i, length - i, readable - i,
                                      simd, true);
        if (run) {
            memcpy(text + used, data + i, run);
            used += run;
            i += run;
            if (i >= length) break;
        }
        uint8_t a = data[i];
        if (a == 0xc0 && i + 1 < length && data[i + 1] == 0x80) {
            text[used++] = 0;
            i += 2;
        } else if (a >= 0xc2 && a <= 0xdf && i + 1 < length &&
                   xx_dex_yx_is_cont(data[i + 1])) {
            text[used++] = a;
            text[used++] = data[i + 1];
            i += 2;
        } else if (a >= 0xe0 && a <= 0xef && i + 2 < length &&
                   xx_dex_yx_is_cont(data[i + 1]) && xx_dex_yx_is_cont(data[i + 2]) &&
                   !(a == 0xe0 && data[i + 1] < 0xa0)) {
            uint32_t code = ((uint32_t)(a & 15) << 12) |
                            ((uint32_t)(data[i + 1] & 63) << 6) |
                            (uint32_t)(data[i + 2] & 63);
            if (code >= 0xd800 && code <= 0xdbff) {
                if (i + 5 >= length || data[i + 3] != 0xed ||
                    data[i + 4] < 0xb0 || data[i + 4] > 0xbf ||
                    !xx_dex_yx_is_cont(data[i + 5])) return false;
                /* Low ten bits of the low surrogate (ED B0..BF xx). */
                uint32_t low = ((uint32_t)(data[i + 4] & 15) << 6) |
                               (uint32_t)(data[i + 5] & 63);
                code = 0x10000 + ((code - 0xd800) << 10) + low;
                text[used++] = (uint8_t)(0xf0 | (code >> 18));
                text[used++] = (uint8_t)(0x80 | ((code >> 12) & 63));
                text[used++] = (uint8_t)(0x80 | ((code >> 6) & 63));
                text[used++] = (uint8_t)(0x80 | (code & 63));
                i += 6;
            } else {
                if (code >= 0xdc00 && code <= 0xdfff) return false;
                text[used++] = a;
                text[used++] = data[i + 1];
                text[used++] = data[i + 2];
                i += 3;
            }
        } else {
            /* NUL, C1, F0-FF, lone continuation or truncated sequence. */
            return false;
        }
    }
    *out_length = used;
    return true;
}

/* ------------------------------------------------------- collections */

static size_t xx_dex_yx_record_count(const xx_dex_yx_result *r, uint32_t offset,
                              uint32_t declared, size_t width)
{
    if (!offset || offset > r->size) return 0;
    size_t available = (r->size - offset) / width;
    size_t count = declared;
    if (count > available) count = available;
    if (count > XX_DEX_YX_MAX_ITEMS) count = XX_DEX_YX_MAX_ITEMS;
    return count;
}

static bool xx_dex_yx_alloc_array(void **out, size_t count, size_t item_size)
{
    *out = NULL;
    if (!count) return true;
    if (count > SIZE_MAX / item_size) return false;
    *out = malloc(count * item_size);
    return *out != NULL;
}

/* Grows a buffer of item_size items to hold at least needed items. */
static bool xx_dex_yx_reserve(void **buffer, size_t *capacity, size_t needed,
                       size_t item_size, size_t minimum)
{
    if (needed <= *capacity) return true;
    size_t grown = *capacity > SIZE_MAX / 2 ? SIZE_MAX : *capacity * 2;
    if (grown < needed) grown = needed;
    if (grown < minimum) grown = minimum;
    if (grown > SIZE_MAX / item_size) return false;
    void *items = realloc(*buffer, grown * item_size);
    if (!items) return false;
    *buffer = items;
    *capacity = grown;
    return true;
}

static xx_dex_yx_view xx_dex_yx_string_view(const xx_dex_yx_result *r, size_t index)
{
    xx_dex_yx_view view = {NULL, 0};
    if (index >= r->strings_count) return view;
    const xx_dex_yx_text *text = &r->strings[index];
    view.bytes = (text->decoded ? r->decoded : r->data) + text->offset;
    view.length = text->length;
    return view;
}

static bool xx_dex_yx_parse_strings(xx_dex_yx_result *r)
{
    size_t n = xx_dex_yx_record_count(r, r->string_ids_off,
                                r->string_ids_size, 4);
    int simd = xx_dex_yx_simd_level();
    if (!xx_dex_yx_alloc_array((void **)&r->strings, n, sizeof(*r->strings)))
        return false;
    for (size_t i = 0; i < n; ++i) {
        size_t at = xx_dex_yx_u32(r->data, r->string_ids_off + i * 4);
        uint64_t length;
        if (at < 112 || !xx_dex_yx_uleb128(r->data, r->size, &at, &length) ||
            length > 65536 || length > r->size ||
            !xx_dex_yx_span(r->size, at, (size_t)length)) continue;
        const uint8_t *bytes = r->data + at;
        size_t readable = r->size - at;
        xx_dex_yx_text *item = &r->strings[r->strings_count];
        /* simd_cesu8::mutf8::decode first accepts any valid UTF-8 slice.
         * This includes literal NUL and four-byte scalars even though
         * canonical MUTF-8 encodes them differently. */
        if (xx_dex_yx_utf8_valid(bytes, (size_t)length, readable, simd)) {
            item->offset = at;
            item->length = (uint32_t)length;
            item->decoded = false;
            ++r->strings_count;
            continue;
        }
        if (!xx_dex_yx_reserve((void **)&r->decoded, &r->decoded_capacity,
                        r->decoded_used + (size_t)length, 1, 4096))
            return false;
        size_t decoded_length;
        if (!xx_dex_yx_decode_mutf8(bytes, (size_t)length, readable, simd,
                             r->decoded + r->decoded_used, &decoded_length))
            continue;
        item->offset = r->decoded_used;
        item->length = (uint32_t)decoded_length;
        item->decoded = true;
        r->decoded_used += decoded_length;
        ++r->strings_count;
    }
    return true;
}

static bool xx_dex_yx_parse_types(xx_dex_yx_result *r)
{
    size_t n = xx_dex_yx_record_count(r, r->type_ids_off, r->type_ids_size, 4);
    if (!xx_dex_yx_alloc_array((void **)&r->types, n, sizeof(*r->types)))
        return false;
    for (size_t i = 0; i < n; ++i) {
        uint32_t index = xx_dex_yx_u32(r->data, r->type_ids_off + i * 4);
        if (index < r->strings_count) r->types[r->types_count++] = index;
    }
    return true;
}

/* Appends the valid entries of the type_list at offset to the shared
   parameter pool. */
static bool xx_dex_yx_parse_parameters(xx_dex_yx_result *r, uint32_t offset,
                                 xx_dex_yx_proto *proto)
{
    proto->parameters_start = (uint32_t)r->parameters_used;
    proto->parameters_count = 0;
    if (!offset || !xx_dex_yx_span(r->size, offset, 4)) return true;
    uint32_t declared = xx_dex_yx_u32(r->data, offset);
    if (declared > 255) return true;
    size_t n = (r->size - offset - 4) / 2;
    if (n > declared) n = declared;
    if (!n) return true;
    if (!xx_dex_yx_reserve((void **)&r->parameters, &r->parameters_capacity,
                    r->parameters_used + n, sizeof(*r->parameters), 1024))
        return false;
    for (size_t i = 0; i < n; ++i) {
        uint16_t index = xx_dex_yx_u16(r->data, offset + 4 + i * 2);
        if (index < r->types_count)
            r->parameters[r->parameters_used++] = index;
    }
    proto->parameters_count =
        (uint32_t)(r->parameters_used - proto->parameters_start);
    return true;
}

static bool xx_dex_yx_parse_protos(xx_dex_yx_result *r)
{
    size_t n = xx_dex_yx_record_count(r, r->proto_ids_off,
                                r->proto_ids_size, 12);
    if (!xx_dex_yx_alloc_array((void **)&r->protos, n, sizeof(*r->protos)))
        return false;
    for (size_t i = 0; i < n; ++i) {
        size_t at = r->proto_ids_off + i * 12;
        uint32_t shorty = xx_dex_yx_u32(r->data, at);
        uint32_t return_type = xx_dex_yx_u32(r->data, at + 4);
        uint32_t parameters_off = xx_dex_yx_u32(r->data, at + 8);
        if (shorty >= r->strings_count || return_type >= r->types_count)
            continue;
        xx_dex_yx_proto *item = &r->protos[r->protos_count];
        item->shorty = shorty;
        item->return_type = return_type;
        if (!xx_dex_yx_parse_parameters(r, parameters_off, item)) return false;
        ++r->protos_count;
    }
    return true;
}

static bool xx_dex_yx_parse_fields(xx_dex_yx_result *r)
{
    size_t n = xx_dex_yx_record_count(r, r->field_ids_off,
                                r->field_ids_size, 8);
    if (!xx_dex_yx_alloc_array((void **)&r->fields, n, sizeof(*r->fields)))
        return false;
    for (size_t i = 0; i < n; ++i) {
        size_t at = r->field_ids_off + i * 8;
        uint16_t class_type = xx_dex_yx_u16(r->data, at);
        uint16_t type = xx_dex_yx_u16(r->data, at + 2);
        uint32_t name = xx_dex_yx_u32(r->data, at + 4);
        if (class_type >= r->types_count || type >= r->types_count ||
            name >= r->strings_count) continue;
        r->fields[r->fields_count++] =
            (xx_dex_yx_field){class_type, type, name};
    }
    return true;
}

static bool xx_dex_yx_parse_methods(xx_dex_yx_result *r)
{
    size_t n = xx_dex_yx_record_count(r, r->method_ids_off,
                                r->method_ids_size, 8);
    if (!xx_dex_yx_alloc_array((void **)&r->methods, n, sizeof(*r->methods)))
        return false;
    for (size_t i = 0; i < n; ++i) {
        size_t at = r->method_ids_off + i * 8;
        uint16_t class_type = xx_dex_yx_u16(r->data, at);
        uint16_t proto = xx_dex_yx_u16(r->data, at + 2);
        uint32_t name = xx_dex_yx_u32(r->data, at + 4);
        if (class_type >= r->types_count || proto >= r->protos_count ||
            name >= r->strings_count) continue;
        r->methods[r->methods_count++] =
            (xx_dex_yx_method){class_type, proto, name};
    }
    return true;
}

static bool xx_dex_yx_parse_classes(xx_dex_yx_result *r)
{
    size_t n = xx_dex_yx_record_count(r, r->class_defs_off,
                                r->class_defs_size, 32);
    if (!xx_dex_yx_alloc_array((void **)&r->classes, n, sizeof(*r->classes)))
        return false;
    for (size_t i = 0; i < n; ++i) {
        size_t at = r->class_defs_off + i * 32;
        uint32_t class_type = xx_dex_yx_u32(r->data, at);
        uint32_t access_flags = xx_dex_yx_u32(r->data, at + 4);
        uint32_t superclass = xx_dex_yx_u32(r->data, at + 8);
        uint32_t source_file = xx_dex_yx_u32(r->data, at + 16);
        if (class_type >= r->types_count) continue;
        xx_dex_yx_class *item = &r->classes[r->classes_count++];
        item->class_type = class_type;
        item->access_flags = access_flags;
        item->superclass = 0;
        item->source_file = 0;
        item->has_superclass = false;
        item->has_source_file = false;
        if (superclass != UINT32_MAX && superclass < r->types_count) {
            item->has_superclass = true;
            item->superclass = superclass;
        }
        if (source_file != UINT32_MAX && source_file < r->strings_count) {
            item->has_source_file = true;
            item->source_file = source_file;
        }
    }
    return true;
}

static void xx_dex_yx_parse_map(xx_dex_yx_result *r)
{
    if (!xx_dex_yx_span(r->size, r->map_off, 4)) return;
    r->has_map_list = true;
    r->map_declared_size = xx_dex_yx_u32(r->data, r->map_off);
    size_t n = (r->size - r->map_off - 4) / 12;
    if (n > r->map_declared_size) n = r->map_declared_size;
    r->map_items_count = n;
}

/* Parses stage (and its prerequisites) on first use. The result belongs to
   one scan of one scanner, so the cache is filled without locking; the
   const of the public accessors refers to the observable values. */
static bool xx_dex_yx_need(const xx_dex_yx_result *result, unsigned stage)
{
    if (!result->is_dex || (result->ready & stage)) return true;
    if (result->failed & stage) return false;
    xx_dex_yx_result *r = (xx_dex_yx_result *)result;
    bool ok;
    switch (stage) {
    case XX_DEX_YX_STRINGS:
        ok = xx_dex_yx_parse_strings(r);
        break;
    case XX_DEX_YX_TYPES:
        ok = xx_dex_yx_need(r, XX_DEX_YX_STRINGS) && xx_dex_yx_parse_types(r);
        break;
    case XX_DEX_YX_PROTOS:
        ok = xx_dex_yx_need(r, XX_DEX_YX_TYPES) && xx_dex_yx_parse_protos(r);
        break;
    case XX_DEX_YX_FIELDS:
        ok = xx_dex_yx_need(r, XX_DEX_YX_TYPES) && xx_dex_yx_parse_fields(r);
        break;
    case XX_DEX_YX_METHODS:
        ok = xx_dex_yx_need(r, XX_DEX_YX_PROTOS) && xx_dex_yx_parse_methods(r);
        break;
    case XX_DEX_YX_CLASSES:
        ok = xx_dex_yx_need(r, XX_DEX_YX_TYPES) && xx_dex_yx_parse_classes(r);
        break;
    default:
        ok = false;
        break;
    }
    if (ok) r->ready |= stage;
    else r->failed |= stage;
    return ok;
}

xx_dex_yx_result *xx_dex_yx_parse(const uint8_t *data, size_t size)
{
    if (!data && size) return NULL;
    /* Non-DEX input is rejected on the magic bytes without allocating. */
    if (!data || !xx_dex_yx_magic(data, size))
        return (xx_dex_yx_result *)&xx_dex_yx_not_dex;
    xx_dex_yx_result *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->data = data;
    r->size = size;
    if (!xx_dex_yx_parse_header(r)) {
        free(r);
        return (xx_dex_yx_result *)&xx_dex_yx_not_dex;
    }
    xx_dex_yx_parse_map(r);
    return r;
}

void xx_dex_yx_destroy(xx_dex_yx_result *r)
{
    if (!r || r == &xx_dex_yx_not_dex) return;
    free(r->strings);
    free(r->decoded);
    free(r->types);
    free(r->protos);
    free(r->parameters);
    free(r->fields);
    free(r->methods);
    free(r->classes);
    free(r);
}

typedef struct { const char *name; uint32_t value; } xx_dex_yx_named_value;

static const xx_dex_yx_named_value xx_dex_yx_constants[] = {
    {"ACC_PUBLIC", 0x1}, {"ACC_PRIVATE", 0x2},
    {"ACC_PROTECTED", 0x4}, {"ACC_STATIC", 0x8},
    {"ACC_FINAL", 0x10}, {"ACC_SYNCHRONIZED", 0x20},
    {"ACC_BRIDGE", 0x40}, {"ACC_VARARGS", 0x80},
    {"ACC_NATIVE", 0x100}, {"ACC_INTERFACE", 0x200},
    {"ACC_ABSTRACT", 0x400}, {"ACC_STRICT", 0x800},
    {"ACC_SYNTHETIC", 0x1000}, {"ACC_ANNOTATION", 0x2000},
    {"ACC_ENUM", 0x4000}, {"ACC_CONSTRUCTOR", 0x10000},
    {"ACC_DECLARED_SYNCHRONIZED", 0x20000},
    {"ACC_VOLATILE", 0x40}, {"ACC_TRANSIENT", 0x80},
    {"TYPE_HEADER_ITEM", 0x0000}, {"TYPE_STRING_ID_ITEM", 0x0001},
    {"TYPE_TYPE_ID_ITEM", 0x0002}, {"TYPE_PROTO_ID_ITEM", 0x0003},
    {"TYPE_FIELD_ID_ITEM", 0x0004}, {"TYPE_METHOD_ID_ITEM", 0x0005},
    {"TYPE_CLASS_DEF_ITEM", 0x0006}, {"TYPE_CALL_SITE_ID_ITEM", 0x0007},
    {"TYPE_METHOD_HANDLE_ITEM", 0x0008}, {"TYPE_MAP_LIST", 0x1000},
    {"TYPE_TYPE_LIST", 0x1001},
    {"TYPE_ANNOTATION_SET_REF_LIST", 0x1002},
    {"TYPE_ANNOTATION_SET_ITEM", 0x1003},
    {"TYPE_CLASS_DATA_ITEM", 0x2000}, {"TYPE_CODE_ITEM", 0x2001},
    {"TYPE_STRING_DATA_ITEM", 0x2002}, {"TYPE_DEBUG_INFO_ITEM", 0x2003},
    {"TYPE_ANNOTATION_ITEM", 0x2004},
    {"TYPE_ENCODED_ARRAY_ITEM", 0x2005},
    {"TYPE_ANNOTATIONS_DIRECTORY_ITEM", 0x2006},
    {"TYPE_HIDDENAPI_CLASS_DATA_ITEM", 0xf000}
};

bool xx_dex_yx_constant(const char *name, uint64_t *value)
{
    if (!name || !value) return false;
    for (size_t i = 0; i < sizeof(xx_dex_yx_constants) /
                           sizeof(xx_dex_yx_constants[0]); ++i)
        if (strcmp(name, xx_dex_yx_constants[i].name) == 0) {
            *value = xx_dex_yx_constants[i].value;
            return true;
        }
    return false;
}

static const char *const xx_dex_yx_numeric_paths[] = {
    "is_dex", "header.magic", "header.version", "header.checksum",
    "header.file_size", "header.header_size", "header.endian_tag",
    "header.link_size", "header.link_off", "header.data_size",
    "header.data_off", "header.container_size", "header.header_offset",
    "map_list.size"
};

bool xx_dex_yx_numeric_field_supported(const char *path)
{
    if (!path) return false;
    for (size_t i = 0; i < sizeof(xx_dex_yx_numeric_paths) /
                           sizeof(xx_dex_yx_numeric_paths[0]); ++i)
        if (strcmp(path, xx_dex_yx_numeric_paths[i]) == 0) return true;
    return false;
}

bool xx_dex_yx_numeric_field(const xx_dex_yx_result *r, const char *path,
                          uint64_t *value)
{
    if (!r || !path || !value) return false;
    if (strcmp(path, "is_dex") == 0) { *value = r->is_dex; return true; }
    if (!r->is_dex) return false;
#define XX_DEX_YX_FIELD(name, member) \
    if (strcmp(path, name) == 0) { *value = r->member; return true; }
    if (strcmp(path, "header.magic") == 0) {
        *value = 0x6465780a;
        return true;
    }
    XX_DEX_YX_FIELD("header.version", version)
    XX_DEX_YX_FIELD("header.checksum", checksum)
    XX_DEX_YX_FIELD("header.file_size", file_size)
    XX_DEX_YX_FIELD("header.header_size", header_size)
    XX_DEX_YX_FIELD("header.endian_tag", endian_tag)
    XX_DEX_YX_FIELD("header.link_size", link_size)
    XX_DEX_YX_FIELD("header.link_off", link_off)
    if (strcmp(path, "header.container_size") == 0 &&
        r->has_container_fields) {
        *value = r->container_size;
        return true;
    }
    if (strcmp(path, "header.header_offset") == 0 &&
        r->has_container_fields) {
        *value = r->header_offset;
        return true;
    }
    if (strcmp(path, "map_list.size") == 0 && r->has_map_list) {
        *value = r->map_declared_size;
        return true;
    }
#undef XX_DEX_YX_FIELD
    /* The upstream parser reads data_size/data_off but does not copy them
       into the exported protobuf header, so they remain undefined. */
    return false;
}

bool xx_dex_yx_string_field_supported(const char *path)
{
    return path && strcmp(path, "header.signature") == 0;
}

bool xx_dex_yx_string_field(const xx_dex_yx_result *r, const char *path,
                         const uint8_t **bytes, size_t *length)
{
    if (!r || !r->is_dex || !xx_dex_yx_string_field_supported(path) ||
        !bytes || !length) return false;
    *bytes = (const uint8_t *)r->header_signature;
    *length = 40;
    return true;
}

static const char *const xx_dex_yx_collections[] = {
    "strings", "types", "protos", "fields", "methods", "class_defs",
    "map_list.items"
};

bool xx_dex_yx_collection_supported(const char *collection)
{
    if (!collection) return false;
    for (size_t i = 0; i < sizeof(xx_dex_yx_collections) /
                           sizeof(xx_dex_yx_collections[0]); ++i)
        if (strcmp(collection, xx_dex_yx_collections[i]) == 0) return true;
    return false;
}

bool xx_dex_yx_collection_count(const xx_dex_yx_result *r,
                              const char *collection, uint64_t *count)
{
    if (!r || !collection || !count) return false;
    if (strcmp(collection, "strings") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_STRINGS)) return false;
        *count = r->strings_count;
    } else if (strcmp(collection, "types") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_TYPES)) return false;
        *count = r->types_count;
    } else if (strcmp(collection, "protos") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_PROTOS)) return false;
        *count = r->protos_count;
    } else if (strcmp(collection, "fields") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_FIELDS)) return false;
        *count = r->fields_count;
    } else if (strcmp(collection, "methods") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_METHODS)) return false;
        *count = r->methods_count;
    } else if (strcmp(collection, "class_defs") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_CLASSES)) return false;
        *count = r->classes_count;
    } else if (strcmp(collection, "map_list.items") == 0)
        /* Upstream's field traversal exposes a singleton undefined item
           when the optional map_list message is absent. */
        *count = r->has_map_list ? r->map_items_count : 1;
    else return false;
    return true;
}

static bool xx_dex_yx_text_out(xx_dex_yx_view item,
                            const uint8_t **bytes, size_t *length)
{
    if (!item.bytes || !bytes || !length) return false;
    *bytes = item.bytes;
    *length = item.length;
    return true;
}

static xx_dex_yx_view xx_dex_yx_type_text(const xx_dex_yx_result *r, uint32_t index)
{
    xx_dex_yx_view none = {NULL, 0};
    if (!r || index >= r->types_count) return none;
    return xx_dex_yx_string_view(r, r->types[index]);
}

bool xx_dex_yx_indexed_numeric_field_supported(const char *collection,
                                              const char *member)
{
    if (!collection || !member) return false;
    if (strcmp(collection, "protos") == 0)
        return strcmp(member, "parameters_count") == 0;
    if (strcmp(collection, "methods") == 0)
        return strcmp(member, "proto.parameters_count") == 0;
    if (strcmp(collection, "class_defs") == 0)
        return strcmp(member, "access_flags") == 0;
    if (strcmp(collection, "map_list.items") == 0)
        return strcmp(member, "type") == 0 ||
               strcmp(member, "unused") == 0 ||
               strcmp(member, "size") == 0 ||
               strcmp(member, "offset") == 0;
    return false;
}

bool xx_dex_yx_indexed_numeric_field(const xx_dex_yx_result *r,
                                   const char *collection, uint64_t index,
                                   const char *member, uint64_t *value)
{
    if (!r || !value ||
        !xx_dex_yx_indexed_numeric_field_supported(collection, member))
        return false;
    if (strcmp(collection, "protos") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_PROTOS) || index >= r->protos_count)
            return false;
        *value = r->protos[index].parameters_count;
    } else if (strcmp(collection, "methods") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_METHODS) || index >= r->methods_count)
            return false;
        uint32_t proto = r->methods[index].proto;
        if (proto >= r->protos_count) return false;
        *value = r->protos[proto].parameters_count;
    } else if (strcmp(collection, "class_defs") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_CLASSES) || index >= r->classes_count)
            return false;
        *value = r->classes[index].access_flags;
    } else if (strcmp(collection, "map_list.items") == 0) {
        if (index >= r->map_items_count) return false;
        size_t at = r->map_off + 4 + (size_t)index * 12;
        if (strcmp(member, "type") == 0) *value = xx_dex_yx_u16(r->data, at);
        else if (strcmp(member, "unused") == 0)
            *value = xx_dex_yx_u16(r->data, at + 2);
        else if (strcmp(member, "size") == 0)
            *value = xx_dex_yx_u32(r->data, at + 4);
        else *value = xx_dex_yx_u32(r->data, at + 8);
    } else return false;
    return true;
}

bool xx_dex_yx_indexed_string_field_supported(const char *collection,
                                             const char *member)
{
    if (!collection || !member) return false;
    if (strcmp(collection, "strings") == 0 ||
        strcmp(collection, "types") == 0)
        return strcmp(member, "value") == 0;
    if (strcmp(collection, "protos") == 0)
        return strcmp(member, "shorty") == 0 ||
               strcmp(member, "return_type") == 0;
    if (strcmp(collection, "fields") == 0)
        return strcmp(member, "class") == 0 ||
               strcmp(member, "type") == 0 ||
               strcmp(member, "name") == 0;
    if (strcmp(collection, "methods") == 0)
        return strcmp(member, "class") == 0 ||
               strcmp(member, "name") == 0 ||
               strcmp(member, "proto.shorty") == 0 ||
               strcmp(member, "proto.return_type") == 0;
    if (strcmp(collection, "class_defs") == 0)
        return strcmp(member, "class") == 0 ||
               strcmp(member, "superclass") == 0 ||
               strcmp(member, "source_file") == 0;
    return false;
}

bool xx_dex_yx_indexed_string_field(const xx_dex_yx_result *r,
                                  const char *collection, uint64_t index,
                                  const char *member,
                                  const uint8_t **bytes, size_t *length)
{
    if (!r || !bytes || !length ||
        !xx_dex_yx_indexed_string_field_supported(collection, member))
        return false;
    xx_dex_yx_view item = {NULL, 0};
    if (strcmp(collection, "strings") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_STRINGS) || index >= r->strings_count)
            return false;
        item = xx_dex_yx_string_view(r, (size_t)index);
    } else if (strcmp(collection, "types") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_TYPES) || index >= r->types_count)
            return false;
        item = xx_dex_yx_type_text(r, (uint32_t)index);
    } else if (strcmp(collection, "protos") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_PROTOS) || index >= r->protos_count)
            return false;
        const xx_dex_yx_proto *p = &r->protos[index];
        item = strcmp(member, "shorty") == 0 ?
               xx_dex_yx_string_view(r, p->shorty) :
               xx_dex_yx_type_text(r, p->return_type);
    } else if (strcmp(collection, "fields") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_FIELDS) || index >= r->fields_count)
            return false;
        const xx_dex_yx_field *f = &r->fields[index];
        if (strcmp(member, "class") == 0)
            item = xx_dex_yx_type_text(r, f->class_type);
        else if (strcmp(member, "type") == 0)
            item = xx_dex_yx_type_text(r, f->type);
        else item = xx_dex_yx_string_view(r, f->name);
    } else if (strcmp(collection, "methods") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_METHODS) || index >= r->methods_count)
            return false;
        const xx_dex_yx_method *m = &r->methods[index];
        if (strcmp(member, "class") == 0)
            item = xx_dex_yx_type_text(r, m->class_type);
        else if (strcmp(member, "name") == 0)
            item = xx_dex_yx_string_view(r, m->name);
        else {
            const xx_dex_yx_proto *p = &r->protos[m->proto];
            item = strcmp(member, "proto.shorty") == 0 ?
                   xx_dex_yx_string_view(r, p->shorty) :
                   xx_dex_yx_type_text(r, p->return_type);
        }
    } else if (strcmp(collection, "class_defs") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_CLASSES) || index >= r->classes_count)
            return false;
        const xx_dex_yx_class *c = &r->classes[index];
        if (strcmp(member, "class") == 0)
            item = xx_dex_yx_type_text(r, c->class_type);
        else if (strcmp(member, "superclass") == 0) {
            if (!c->has_superclass) return false;
            item = xx_dex_yx_type_text(r, c->superclass);
        } else {
            if (!c->has_source_file) return false;
            item = xx_dex_yx_string_view(r, c->source_file);
        }
    }
    return xx_dex_yx_text_out(item, bytes, length);
}

static const xx_dex_yx_proto *xx_dex_yx_nested_proto(const xx_dex_yx_result *r,
                                                 const char *collection,
                                                 uint64_t index,
                                                 const char *nested)
{
    if (!r || !collection || !nested) return NULL;
    if (strcmp(collection, "protos") == 0 &&
        strcmp(nested, "parameters") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_PROTOS) || index >= r->protos_count)
            return NULL;
        return &r->protos[index];
    }
    if (strcmp(collection, "methods") == 0 &&
        strcmp(nested, "proto.parameters") == 0) {
        if (!xx_dex_yx_need(r, XX_DEX_YX_METHODS) || index >= r->methods_count)
            return NULL;
        uint32_t proto = r->methods[index].proto;
        return proto < r->protos_count ? &r->protos[proto] : NULL;
    }
    return NULL;
}

bool xx_dex_yx_nested_collection_count(const xx_dex_yx_result *r,
                                      const char *collection, uint64_t index,
                                      const char *nested, uint64_t *count)
{
    const xx_dex_yx_proto *p = xx_dex_yx_nested_proto(r, collection, index,
                                                 nested);
    if (!p || !count) return false;
    *count = p->parameters_count;
    return true;
}

bool xx_dex_yx_nested_indexed_string(const xx_dex_yx_result *r,
                                    const char *collection, uint64_t index,
                                    const char *nested, uint64_t nested_index,
                                    const uint8_t **bytes, size_t *length)
{
    const xx_dex_yx_proto *p = xx_dex_yx_nested_proto(r, collection, index,
                                                 nested);
    if (!p || !bytes || !length || nested_index >= p->parameters_count)
        return false;
    xx_dex_yx_view item = xx_dex_yx_type_text(
        r, r->parameters[p->parameters_start + (size_t)nested_index]);
    return xx_dex_yx_text_out(item, bytes, length);
}

static int xx_dex_yx_text_compare(xx_dex_yx_view item,
                                const uint8_t *bytes, size_t length)
{
    size_t common = item.length < length ? item.length : length;
    int comparison = common ? memcmp(item.bytes, bytes, common) : 0;
    if (comparison) return comparison;
    return item.length < length ? -1 : item.length > length ? 1 : 0;
}

bool xx_dex_yx_contains_string(const xx_dex_yx_result *r,
                             const uint8_t *bytes, size_t length,
                             bool *found)
{
    if (!r || !found || !xx_dex_yx_valid_utf8(bytes, length) ||
        !xx_dex_yx_need(r, XX_DEX_YX_STRINGS)) return false;
    *found = false;
    size_t low = 0, high = r->strings_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int comparison = xx_dex_yx_text_compare(xx_dex_yx_string_view(r, mid),
                                             bytes, length);
        if (comparison < 0) low = mid + 1;
        else if (comparison > 0) high = mid;
        else { *found = true; return true; }
    }
    return true;
}

bool xx_dex_yx_contains_method(const xx_dex_yx_result *r,
                             const uint8_t *bytes, size_t length,
                             bool *found)
{
    if (!r || !found || !xx_dex_yx_valid_utf8(bytes, length) ||
        !xx_dex_yx_need(r, XX_DEX_YX_METHODS)) return false;
    *found = false;
    for (size_t i = 0; i < r->methods_count; ++i) {
        xx_dex_yx_view item = xx_dex_yx_string_view(r, r->methods[i].name);
        if (xx_dex_yx_text_compare(item, bytes, length) == 0) {
            *found = true;
            break;
        }
    }
    return true;
}

bool xx_dex_yx_contains_class(const xx_dex_yx_result *r,
                            const uint8_t *bytes, size_t length,
                            bool *found)
{
    if (!r || !found || !xx_dex_yx_valid_utf8(bytes, length) ||
        !xx_dex_yx_need(r, XX_DEX_YX_CLASSES)) return false;
    *found = false;
    size_t low = 0, high = r->classes_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        xx_dex_yx_view item = xx_dex_yx_type_text(r, r->classes[mid].class_type);
        if (!item.bytes) return false;
        int comparison = xx_dex_yx_text_compare(item, bytes, length);
        if (comparison < 0) low = mid + 1;
        else if (comparison > 0) high = mid;
        else { *found = true; return true; }
    }
    return true;
}

bool xx_dex_yx_checksum(const xx_dex_yx_result *r, uint64_t *value)
{
    if (!r || !r->is_dex || !value || r->size < 12) return false;
    /* Adler-32 over the file after the 12-byte signature; the header
     * stores (b << 16) | a, exactly xx_adler32's layout. */
    *value = xx_adler32(r->data + 12, r->size - 12);
    return true;
}

bool xx_dex_yx_signature(const xx_dex_yx_result *r, char out[41])
{
    if (!r || !r->is_dex || !out || r->size < 32) return false;
    uint8_t digest[20];
    if (!xx_sha1_memory(r->data + 32, r->size - 32, digest)) return false;
    xx_dex_yx_hex_lower(digest, 20, out);
    return true;
}
