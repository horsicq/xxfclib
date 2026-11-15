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
#include <xxfclib/strings/xx_string_casefold.h>

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t source;
    uint32_t lower;
} xx_string_lower_mapping;

#include "xx_string_casefold_data.inc"
#include "xx_string_casefold_props.inc"

static bool xx_string_in_ranges(uint32_t scalar, const uint32_t *ranges,
                                      size_t length)
{
    size_t first = 0, last = length / 2;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        uint32_t lower = ranges[middle * 2];
        uint32_t upper = ranges[middle * 2 + 1];
        if (scalar < lower) last = middle;
        else if (scalar > upper) first = middle + 1;
        else return true;
    }
    return false;
}

static bool xx_string_is_cased(uint32_t scalar)
{
    return xx_string_in_ranges(scalar, xx_string_cased_ranges,
                                     sizeof(xx_string_cased_ranges) /
                                     sizeof(xx_string_cased_ranges[0]));
}

static bool xx_string_is_case_ignorable(uint32_t scalar)
{
    return xx_string_in_ranges(scalar, xx_string_case_ignorable_ranges,
                                     sizeof(xx_string_case_ignorable_ranges) /
                                     sizeof(xx_string_case_ignorable_ranges[0]));
}

static uint32_t xx_string_lower_scalar(uint32_t scalar)
{
    size_t first = 0;
    size_t last = sizeof(xx_string_lower_mappings) / sizeof(xx_string_lower_mappings[0]);
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (xx_string_lower_mappings[middle].source < scalar) first = middle + 1;
        else last = middle;
    }
    return first < sizeof(xx_string_lower_mappings) /
                   sizeof(xx_string_lower_mappings[0]) &&
           xx_string_lower_mappings[first].source == scalar ?
           xx_string_lower_mappings[first].lower : scalar;
}

/* Decode one scalar with Rust String::from_utf8_lossy's error consumption:
 * a malformed prefix becomes one replacement character. */
static size_t xx_string_decode_utf8_lossy(const uint8_t *bytes, size_t length,
                                        uint32_t *scalar)
{
    uint8_t lead = bytes[0];
    unsigned width;
    uint32_t value;
    if (lead < 0x80) { *scalar = lead; return 1; }
    if (lead >= 0xc2 && lead <= 0xdf) {
        width = 2; value = lead & 0x1fu;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        width = 3; value = lead & 0x0fu;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        width = 4; value = lead & 0x07u;
    } else {
        *scalar = 0xfffdu;
        return 1;
    }
    for (unsigned i = 1; i < width; ++i) {
        if (i >= length || bytes[i] < 0x80 || bytes[i] > 0xbf ||
            (i == 1 && ((lead == 0xe0 && bytes[i] < 0xa0) ||
                        (lead == 0xed && bytes[i] > 0x9f) ||
                        (lead == 0xf0 && bytes[i] < 0x90) ||
                        (lead == 0xf4 && bytes[i] > 0x8f)))) {
            *scalar = 0xfffdu;
            return i;
        }
        value = (value << 6) | (bytes[i] & 0x3fu);
    }
    *scalar = value;
    return width;
}

XXFC_API bool xx_string_utf8_lossy(const uint8_t *bytes, size_t length, char *out,
                        size_t capacity, size_t *written)
{
    static const char replacement[] = "\xef\xbf\xbd";
    size_t used = 0;
    if (!written || (!bytes && length)) return false;
    for (size_t i = 0; i < length;) {
        uint32_t scalar;
        size_t input_width = xx_string_decode_utf8_lossy(bytes + i,
                                                       length - i, &scalar);
        size_t output_width = scalar == 0xfffdu ? 3 : input_width;
        if (output_width > SIZE_MAX - used) return false;
        if (out && output_width <= capacity && used <= capacity - output_width)
            memcpy(out + used,
                   scalar == 0xfffdu ? replacement : (const char *)(bytes + i),
                   output_width);
        used += output_width;
        i += input_width;
    }
    *written = used;
    if (out && used > capacity) return false;
    if (out && used < capacity) out[used] = '\0';
    return true;
}

static size_t xx_string_encode_utf8(char *out, uint32_t scalar)
{
    if (scalar < 0x80) {
        out[0] = (char)scalar;
        return 1;
    }
    if (scalar < 0x800) {
        out[0] = (char)(0xc0 | (scalar >> 6));
        out[1] = (char)(0x80 | (scalar & 0x3f));
        return 2;
    }
    if (scalar < 0x10000) {
        out[0] = (char)(0xe0 | (scalar >> 12));
        out[1] = (char)(0x80 | ((scalar >> 6) & 0x3f));
        out[2] = (char)(0x80 | (scalar & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (scalar >> 18));
    out[1] = (char)(0x80 | ((scalar >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((scalar >> 6) & 0x3f));
    out[3] = (char)(0x80 | (scalar & 0x3f));
    return 4;
}

/* Rust str::to_lowercase applies Unicode 17 scalar mappings and the Final
 * Sigma context rule. The input first receives from_utf8_lossy decoding. */
XXFC_API bool xx_string_to_lower_alloc(const uint8_t *bytes, size_t length,
                               char **out, size_t *out_length)
{
    uint32_t *scalars;
    char *lower;
    size_t count = 0, used = 0;
    if (length > SIZE_MAX / sizeof(uint32_t) ||
        length > (SIZE_MAX - 1) / 3) return false;
    scalars = (uint32_t *)malloc((length ? length : 1) * sizeof(uint32_t));
    lower = (char *)malloc(length * 3 + 1);
    if (!scalars || !lower) { free(scalars); free(lower); return false; }
    for (size_t i = 0; i < length;) {
        i += xx_string_decode_utf8_lossy(bytes + i, length - i,
                                       &scalars[count++]);
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t scalar = scalars[i];
        if (scalar == 0x03a3u) {
            bool before = false, after = false;
            for (size_t j = i; j > 0;) {
                --j;
                if (xx_string_is_case_ignorable(scalars[j])) continue;
                before = xx_string_is_cased(scalars[j]);
                break;
            }
            for (size_t j = i + 1; j < count; ++j) {
                if (xx_string_is_case_ignorable(scalars[j])) continue;
                after = xx_string_is_cased(scalars[j]);
                break;
            }
            scalar = before && !after ? 0x03c2u : 0x03c3u;
        } else if (scalar == 0x0130u) {
            lower[used++] = 'i';
            lower[used++] = (char)0xcc;
            lower[used++] = (char)0x87;
            continue;
        } else {
            scalar = xx_string_lower_scalar(scalar);
        }
        used += xx_string_encode_utf8(lower + used, scalar);
    }
    lower[used] = '\0';
    free(scalars);
    *out = lower;
    *out_length = used;
    return true;
}

static bool xx_string_is_whitespace(uint32_t scalar)
{
    return (scalar >= 9u && scalar <= 13u) || scalar == 0x20u ||
           scalar == 0x85u || scalar == 0xa0u || scalar == 0x1680u ||
           (scalar >= 0x2000u && scalar <= 0x200au) ||
           scalar == 0x2028u || scalar == 0x2029u ||
           scalar == 0x202fu || scalar == 0x205fu || scalar == 0x3000u;
}

XXFC_API bool xx_string_trim_whitespace_alloc(const uint8_t *bytes,
                                              size_t length, char **out,
                                              size_t *out_length)
{
    size_t first = SIZE_MAX, last = 0;
    char *copy;
    if (!out || !out_length || (!bytes && length)) return false;
    *out = NULL;
    *out_length = 0;
    for (size_t i = 0; i < length;) {
        uint32_t scalar;
        size_t width = xx_string_decode_utf8_lossy(bytes + i, length - i,
                                                   &scalar);
        if (!xx_string_is_whitespace(scalar)) {
            if (first == SIZE_MAX) first = i;
            last = i + width;
        }
        i += width;
    }
    if (first == SIZE_MAX) first = last;
    size_t trimmed = last - first;
    copy = (char *)malloc(trimmed + 1u);
    if (!copy) return false;
    if (trimmed) memcpy(copy, bytes + first, trimmed);
    copy[trimmed] = 0;
    *out = copy;
    *out_length = trimmed;
    return true;
}

/* Lowercase used by the streaming and trimming conversions: one scalar of
 * the lossy-decoded sequence, with U+0130 expanded. */
static size_t xx_string_encode_lower(char *out, uint32_t scalar)
{
    if (scalar == 0x0130u) {
        out[0] = 'i';
        out[1] = (char)0xcc;
        out[2] = (char)0x87;
        return 3;
    }
    return xx_string_encode_utf8(out, xx_string_lower_scalar(scalar));
}

XXFC_API bool xx_string_to_lower_stream(const uint8_t *bytes, size_t length,
                                        xx_string_sink_fn sink, void *opaque)
{
    char buffer[256];
    size_t used = 0, ascii = 0;
    bool upper = false, preceding_cased = false;
    if (!sink || (!bytes && length)) return false;
    for (; ascii < length && bytes[ascii] < 0x80; ++ascii)
        if (bytes[ascii] >= 'A' && bytes[ascii] <= 'Z') upper = true;
    if (ascii == length) {
        if (!upper) {
            if (length) sink(opaque, (const char *)bytes, length);
            return true;
        }
        for (size_t offset = 0; offset < length;) {
            size_t n = length - offset < sizeof(buffer) ?
                       length - offset : sizeof(buffer);
            for (size_t i = 0; i < n; ++i) {
                uint8_t ch = bytes[offset + i];
                buffer[i] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
            }
            sink(opaque, buffer, n);
            offset += n;
        }
        return true;
    }
    /* The preceding-cased state is the Final Sigma "before" context:
     * the casing of the last scalar that is not Case_Ignorable. */
    for (size_t at = 0; at < length;) {
        uint32_t scalar;
        size_t next = at + xx_string_decode_utf8_lossy(bytes + at,
                                                       length - at, &scalar);
        if (used > sizeof(buffer) - 4) {
            sink(opaque, buffer, used);
            used = 0;
        }
        if (scalar == 0x03a3u) {
            bool after = false;
            for (size_t j = next; j < length;) {
                uint32_t following;
                j += xx_string_decode_utf8_lossy(bytes + j, length - j,
                                                 &following);
                if (xx_string_is_case_ignorable(following)) continue;
                after = xx_string_is_cased(following);
                break;
            }
            used += xx_string_encode_utf8(buffer + used,
                preceding_cased && !after ? 0x03c2u : 0x03c3u);
        } else {
            used += xx_string_encode_lower(buffer + used, scalar);
        }
        if (!xx_string_is_case_ignorable(scalar))
            preceding_cased = xx_string_is_cased(scalar);
        at = next;
    }
    if (used) sink(opaque, buffer, used);
    return true;
}

XXFC_API bool xx_string_trim_lower_alloc(const uint8_t *bytes, size_t length,
                                         bool final_sigma, char **out,
                                         size_t *out_length)
{
    uint32_t *scalars;
    char *lower;
    size_t count = 0, first = 0, last, used = 0;
    if (!out || !out_length || (!bytes && length) ||
        length > SIZE_MAX / sizeof(uint32_t) ||
        length > (SIZE_MAX - 1) / 3) return false;
    *out = NULL;
    *out_length = 0;
    scalars = (uint32_t *)malloc((length ? length : 1) * sizeof(uint32_t));
    lower = (char *)malloc(length * 3 + 1);
    if (!scalars || !lower) { free(scalars); free(lower); return false; }
    for (size_t i = 0; i < length;)
        i += xx_string_decode_utf8_lossy(bytes + i, length - i,
                                         &scalars[count++]);
    last = count;
    while (first < last && xx_string_is_whitespace(scalars[first])) ++first;
    while (last > first && xx_string_is_whitespace(scalars[last - 1])) --last;
    for (size_t i = first; i < last; ++i) {
        uint32_t scalar = scalars[i];
        if (scalar == 0x03a3u && final_sigma) {
            bool before = false, after = false;
            for (size_t j = i; j > first;) {
                --j;
                if (xx_string_is_case_ignorable(scalars[j])) continue;
                before = xx_string_is_cased(scalars[j]);
                break;
            }
            for (size_t j = i + 1; j < last; ++j) {
                if (xx_string_is_case_ignorable(scalars[j])) continue;
                after = xx_string_is_cased(scalars[j]);
                break;
            }
            used += xx_string_encode_utf8(lower + used,
                                          before && !after ? 0x03c2u : 0x03c3u);
        } else {
            used += xx_string_encode_lower(lower + used, scalar);
        }
    }
    lower[used] = '\0';
    free(scalars);
    *out = lower;
    *out_length = used;
    return true;
}

XXFC_API uint32_t xx_string_scalar_to_lower(uint32_t scalar)
{
    return xx_string_lower_scalar(scalar);
}

XXFC_API size_t xx_string_lower_mapping_count(void)
{
    return sizeof(xx_string_lower_mappings) / sizeof(xx_string_lower_mappings[0]);
}

XXFC_API bool xx_string_lower_mapping_at(size_t index, uint32_t *source,
                                         uint32_t *lower)
{
    if (!source || !lower || index >= xx_string_lower_mapping_count())
        return false;
    *source = xx_string_lower_mappings[index].source;
    *lower = xx_string_lower_mappings[index].lower;
    return true;
}

XXFC_API size_t xx_string_scalar_lower_variants(uint32_t scalar,
                                                uint32_t variants[4])
{
    uint32_t lower = xx_string_lower_scalar(scalar);
    size_t count = 1;
    if (!variants) return 0;
    variants[0] = lower;
    for (size_t i = 0; i < xx_string_lower_mapping_count(); ++i) {
        if (xx_string_lower_mappings[i].lower == lower &&
            xx_string_lower_mappings[i].source != lower) {
            if (count == 4) return 0;
            variants[count++] = xx_string_lower_mappings[i].source;
        }
    }
    return count;
}
