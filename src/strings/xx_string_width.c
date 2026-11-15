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
#include <xxfclib/strings/xx_string_width.h>

#include <stddef.h>

typedef struct {
    uint32_t first;
    uint32_t last;
    uint8_t width;
} xx_string_width_range;

#include "xx_string_width_data.inc"

XXFC_API unsigned xx_string_scalar_display_width(uint32_t scalar)
{
    size_t first = 0;
    size_t last = sizeof(xx_string_width_ranges) /
                  sizeof(xx_string_width_ranges[0]);
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        const xx_string_width_range *range = &xx_string_width_ranges[middle];
        if (scalar < range->first) last = middle;
        else if (scalar > range->last) first = middle + 1;
        else return range->width;
    }
    return 1;
}
