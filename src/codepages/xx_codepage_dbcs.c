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
#include "xx_codepage_internal.h"
static const xx_codepage_pair *lookup(const xx_codepage_pair *table, size_t count, uint32_t key)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (table[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    return lo < count && table[lo].key == key ? table + lo : NULL;
}
xx_codepage_status xx_codepage_dbcs_decode(const xx_codepage *cp, const unsigned char *src, size_t n, size_t *used, uint32_t *scalar)
{
    const xx_codepage_dbcs *map = (const xx_codepage_dbcs *)cp->context;
    const xx_codepage_pair *pair;
    uint32_t single = map->singles[src[0]];
    if (single != UINT32_MAX) {
        *scalar = single;
        *used = 1;
        return XX_CODEPAGE_OK;
    }
    if (!map->leads[src[0]]) return XX_CODEPAGE_INVALID;
    if (n < 2) return XX_CODEPAGE_INCOMPLETE;
    pair = lookup(map->forward, map->forward_count, ((uint32_t)src[0] << 8) | src[1]);
    if (!pair) return XX_CODEPAGE_INVALID;
    *scalar = pair->value;
    *used = 2;
    return XX_CODEPAGE_OK;
}
xx_codepage_status xx_codepage_dbcs_encode(const xx_codepage *cp, uint32_t scalar, unsigned char *out, size_t n, size_t *used)
{
    const xx_codepage_dbcs *map = (const xx_codepage_dbcs *)cp->context;
    const xx_codepage_pair *pair = lookup(map->reverse, map->reverse_count, scalar);
    size_t count;
    if (!pair) return XX_CODEPAGE_UNMAPPABLE;
    count = pair->value > 255 ? 2 : 1;
    if (n < count) return XX_CODEPAGE_BUFFER_TOO_SMALL;
    if (count == 2) *out++ = (unsigned char)(pair->value >> 8);
    *out = (unsigned char)pair->value;
    *used = count;
    return XX_CODEPAGE_OK;
}

int xx_codepage_sjis_lead_byte(unsigned char byte)
{
    return (byte >= 0x81 && byte <= 0x9f) || (byte >= 0xe0 && byte <= 0xfc);
}
int xx_codepage_sjis_trail_byte(unsigned char byte)
{
    return (byte >= 0x40 && byte <= 0x7e) || (byte >= 0x80 && byte <= 0xfc);
}
