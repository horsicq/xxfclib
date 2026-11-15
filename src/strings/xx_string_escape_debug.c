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
#include <xxfclib/strings/xx_string_escape_debug.h>

#include <stddef.h>

#include "xx_string_escape_debug_data.inc"

XXFC_API bool xx_string_scalar_debug_escaped(uint32_t scalar)
{
    size_t lo = 0, hi = sizeof(xx_string_debug_escaped_ranges) /
                        sizeof(xx_string_debug_escaped_ranges[0]);
    if (scalar < 0x80) return scalar < 0x20 || scalar == 0x7f;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (scalar < xx_string_debug_escaped_ranges[mid][0]) hi = mid;
        else if (scalar > xx_string_debug_escaped_ranges[mid][1]) lo = mid + 1;
        else return true;
    }
    return false;
}
