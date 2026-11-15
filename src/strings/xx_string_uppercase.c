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
#include <xxfclib/strings/xx_string_uppercase.h>

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t source, mapped[3];
    unsigned count;
} xx_string_upper_mapping;

typedef struct {
    uint32_t first, last;
} xx_string_case_range;

#include "xx_string_uppercase_data.inc"
#include "xx_string_case_properties.inc"

/* Width of the well-formed UTF-8 scalar at bytes, or 0 for a malformed
 * byte, which bstr's char_indices reports as U+FFFD with its raw bytes. */
static size_t xx_string_upper_decode(const uint8_t *bytes, size_t length,
                                     uint32_t *scalar)
{
    uint8_t lead = bytes[0];
    size_t width;
    uint32_t value;
    if (lead < 0x80) {
        *scalar = lead;
        return 1;
    }
    if (lead >= 0xc2 && lead <= 0xdf) {
        width = 2;
        value = lead & 0x1fu;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        width = 3;
        value = lead & 0x0fu;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        width = 4;
        value = lead & 0x07u;
    } else {
        return 0;
    }
    if (length < width) return 0;
    for (size_t i = 1; i < width; ++i) {
        if ((bytes[i] & 0xc0u) != 0x80u) return 0;
        value = (value << 6) | (bytes[i] & 0x3fu);
    }
    if ((width == 3 && value < 0x800u) || (width == 4 && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu)
        return 0;
    *scalar = value;
    return width;
}

static size_t xx_string_upper_encode(uint8_t *out, uint32_t scalar)
{
    if (scalar < 0x80u) {
        out[0] = (uint8_t)scalar;
        return 1;
    }
    if (scalar < 0x800u) {
        out[0] = (uint8_t)(0xc0u | (scalar >> 6));
        out[1] = (uint8_t)(0x80u | (scalar & 0x3fu));
        return 2;
    }
    if (scalar < 0x10000u) {
        out[0] = (uint8_t)(0xe0u | (scalar >> 12));
        out[1] = (uint8_t)(0x80u | ((scalar >> 6) & 0x3fu));
        out[2] = (uint8_t)(0x80u | (scalar & 0x3fu));
        return 3;
    }
    out[0] = (uint8_t)(0xf0u | (scalar >> 18));
    out[1] = (uint8_t)(0x80u | ((scalar >> 12) & 0x3fu));
    out[2] = (uint8_t)(0x80u | ((scalar >> 6) & 0x3fu));
    out[3] = (uint8_t)(0x80u | (scalar & 0x3fu));
    return 4;
}

static const xx_string_upper_mapping *xx_string_upper_find(uint32_t scalar)
{
    size_t first = 0;
    size_t last = sizeof(xx_string_upper_mappings) /
                  sizeof(xx_string_upper_mappings[0]);
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (xx_string_upper_mappings[middle].source < scalar) first = middle + 1;
        else last = middle;
    }
    return first < sizeof(xx_string_upper_mappings) /
                   sizeof(xx_string_upper_mappings[0]) &&
           xx_string_upper_mappings[first].source == scalar ?
           &xx_string_upper_mappings[first] : NULL;
}

XXFC_API bool xx_string_to_upper_alloc(const uint8_t *bytes, size_t length,
                                       char **out, size_t *out_length)
{
    uint8_t *upper;
    size_t used = 0, capacity;
    if (!out || !out_length || (!bytes && length)) return false;
    *out = NULL;
    *out_length = 0;
    /* The Unicode 17 mappings grow a scalar's UTF-8 form at most threefold
     * (U+0390 -> U+0399 U+0308 U+0301); the check below keeps any other
     * table safe. */
    if (length > (SIZE_MAX - 13) / 3) return false;
    capacity = length * 3 + 13;
    upper = (uint8_t *)malloc(capacity);
    if (!upper) return false;
    for (size_t i = 0; i < length;) {
        uint32_t scalar;
        size_t width = xx_string_upper_decode(bytes + i, length - i, &scalar);
        const xx_string_upper_mapping *mapping;
        if (capacity - used < 13) {
            uint8_t *grown;
            if (capacity > SIZE_MAX / 2) { free(upper); return false; }
            grown = (uint8_t *)realloc(upper, capacity * 2);
            if (!grown) { free(upper); return false; }
            upper = grown;
            capacity *= 2;
        }
        if (!width) {
            upper[used++] = bytes[i++];
            continue;
        }
        mapping = xx_string_upper_find(scalar);
        if (mapping) {
            for (unsigned c = 0; c < mapping->count; ++c)
                used += xx_string_upper_encode(upper + used, mapping->mapped[c]);
        } else {
            memcpy(upper + used, bytes + i, width);
            used += width;
        }
        i += width;
    }
    upper[used] = '\0';
    *out = (char *)upper;
    *out_length = used;
    return true;
}

XXFC_API bool xx_string_has_case_property(const uint8_t *bytes, size_t length,
                                          bool uppercase)
{
    const xx_string_case_range *ranges = uppercase ?
        xx_string_uppercase_property : xx_string_lowercase_property;
    size_t count = uppercase ?
        sizeof(xx_string_uppercase_property) /
        sizeof(xx_string_uppercase_property[0]) :
        sizeof(xx_string_lowercase_property) /
        sizeof(xx_string_lowercase_property[0]);
    if (!bytes && length) return false;
    for (size_t i = 0; i < length;) {
        uint32_t scalar;
        size_t width = xx_string_upper_decode(bytes + i, length - i, &scalar);
        size_t first = 0, last = count;
        if (!width) {
            ++i;
            continue;
        }
        while (first < last) {
            size_t middle = first + (last - first) / 2;
            if (ranges[middle].last < scalar) first = middle + 1;
            else last = middle;
        }
        if (first < count && ranges[first].first <= scalar) return true;
        i += width;
    }
    return false;
}
