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
/* TLSH constants and algorithm adapted from YARA-X v1.21.0.
 * The YARA-X Authors' BSD-3-Clause notice is in LICENSE.YARA-X. */

#include "xxfclib/algo/tlsh/xx_tlsh.h"

#include <string.h>

static const uint8_t xx_v_table[256] = {
    1, 87, 49, 12, 176, 178, 102, 166, 121, 193, 6, 84,
    249, 230, 44, 163, 14, 197, 213, 181, 161, 85, 218, 80,
    64, 239, 24, 226, 236, 142, 38, 200, 110, 177, 104, 103,
    141, 253, 255, 50, 77, 101, 81, 18, 45, 96, 31, 222,
    25, 107, 190, 70, 86, 237, 240, 34, 72, 242, 20, 214,
    244, 227, 149, 235, 97, 234, 57, 22, 60, 250, 82, 175,
    208, 5, 127, 199, 111, 62, 135, 248, 174, 169, 211, 58,
    66, 154, 106, 195, 245, 171, 17, 187, 182, 179, 0, 243,
    132, 56, 148, 75, 128, 133, 158, 100, 130, 126, 91, 13,
    153, 246, 216, 219, 119, 68, 223, 78, 83, 88, 201, 99,
    122, 11, 92, 32, 136, 114, 52, 10, 138, 30, 48, 183,
    156, 35, 61, 26, 143, 74, 251, 94, 129, 162, 63, 152,
    170, 7, 115, 167, 241, 206, 3, 150, 55, 59, 151, 220,
    90, 53, 23, 131, 125, 173, 15, 238, 79, 95, 89, 16,
    105, 137, 225, 224, 217, 160, 37, 123, 118, 73, 2, 157,
    46, 116, 9, 145, 134, 228, 207, 212, 202, 215, 69, 229,
    27, 188, 67, 124, 168, 252, 42, 4, 29, 108, 21, 247,
    19, 205, 39, 203, 233, 40, 186, 147, 198, 192, 155, 33,
    164, 191, 98, 204, 165, 180, 117, 76, 140, 36, 210, 172,
    41, 54, 159, 8, 185, 232, 113, 196, 231, 47, 146, 120,
    51, 65, 28, 144, 254, 221, 93, 189, 194, 139, 112, 43,
    71, 109, 184, 209,
};

static const uint32_t xx_topval[170] = {
    1, 2, 3, 5, 7, 11, 17, 25, 38, 57, 86, 129,
    194, 291, 437, 656, 854, 1110, 1443, 1876, 2439, 3171, 3475, 3823,
    4205, 4626, 5088, 5597, 6157, 6772, 7450, 8195, 9014, 9916, 10907, 11998,
    13198, 14518, 15970, 17567, 19323, 21256, 23382, 25720, 28292, 31121, 34233, 37656,
    41422, 45564, 50121, 55133, 60646, 66711, 73382, 80721, 88793, 97672, 107439, 118183,
    130002, 143002, 157302, 173032, 190335, 209369, 230306, 253337, 278670, 306538, 337191, 370911,
    408002, 448802, 493682, 543050, 597356, 657091, 722800, 795081, 874589, 962048, 1058252, 1164078,
    1280486, 1408534, 1549388, 1704327, 1874759, 2062236, 2268459, 2495305, 2744836, 3019320, 3321252, 3653374,
    4018711, 4420582, 4862641, 5348905, 5883796, 6472176, 7119394, 7831333, 8614467, 9475909, 10423501, 11465851,
    12612437, 13873681, 15261050, 16787154, 18465870, 20312458, 22343706, 24578077, 27035886, 29739474, 32713425, 35984770,
    39583245, 43541573, 47895730, 52685306, 57953837, 63749221, 70124148, 77136564, 84850228, 93335252, 102668779, 112935659,
    124229227, 136652151, 150317384, 165349128, 181884040, 200072456, 220079703, 242087671, 266296456, 292926096, 322218735, 354440623,
    389884688, 428873168, 471760495, 518936559, 570830240, 627913311, 690704607, 759775136, 835752671, 919327967, 1011260767, 1112386880,
    1223623232, 1345985727, 1480584256, 1628642751, 1791507135, 1970657856, 2167723648, 2384496256, 2622945920, 2885240448, 3173764736, 3491141248,
    3840255616, 4224281216,
};

static uint8_t xx_tlsh_pearson(uint8_t salt, uint8_t a, uint8_t b,
                               uint8_t c)
{
    uint8_t h = xx_v_table[salt];
    h = xx_v_table[h ^ a];
    h = xx_v_table[h ^ b];
    return xx_v_table[h ^ c];
}

static void xx_tlsh_swap_u32(uint32_t *left, uint32_t *right)
{
    uint32_t value = *left;
    *left = *right;
    *right = value;
}

static size_t xx_tlsh_partition(uint32_t buckets[128], size_t low,
                                size_t high)
{
    size_t result, pivot;
    uint32_t value;
    if (low == high) return low;
    if (low + 1 == high) {
        if (buckets[low] > buckets[high])
            xx_tlsh_swap_u32(&buckets[low], &buckets[high]);
        return low;
    }
    result = low;
    pivot = (low + high) >> 1;
    value = buckets[pivot];
    xx_tlsh_swap_u32(&buckets[pivot], &buckets[high]);
    for (size_t i = low; i < high; ++i) {
        if (buckets[i] < value) {
            xx_tlsh_swap_u32(&buckets[i], &buckets[result]);
            ++result;
        }
    }
    buckets[high] = buckets[result];
    buckets[result] = value;
    return result;
}

/* This follows YARA-X's TLSH quartile selection, including its path-dependent
 * zero quartile when the shortcut stack on one side is empty. */
static void xx_tlsh_quartiles(const uint32_t source[256], uint32_t *q1,
                              uint32_t *q2, uint32_t *q3)
{
    uint32_t buckets[128];
    size_t shortcuts_low[128], shortcuts_high[128];
    size_t low = 0, high = 127, spl = 0, sph = 0;
    memcpy(buckets, source, sizeof(buckets));
    for (;;) {
        size_t pivot = xx_tlsh_partition(buckets, low, high);
        if (pivot > 63) {
            high = pivot - 1;
            shortcuts_high[sph++] = pivot;
        } else if (pivot < 63) {
            low = pivot + 1;
            shortcuts_low[spl++] = pivot;
        } else {
            *q2 = buckets[63];
            break;
        }
    }
    shortcuts_low[spl] = 62;
    shortcuts_high[sph] = 64;
    *q1 = 0;
    low = 0;
    for (size_t i = 0; i < spl; ++i) {
        high = shortcuts_low[i];
        if (high > 31) {
            for (;;) {
                size_t pivot = xx_tlsh_partition(buckets, low, high);
                if (pivot > 31) high = pivot - 1;
                else if (pivot < 31) low = pivot + 1;
                else { *q1 = buckets[31]; break; }
            }
            break;
        }
        if (high < 31) low = high;
        else { *q1 = buckets[31]; break; }
    }
    *q3 = 0;
    high = 127;
    for (size_t i = 0; i < sph; ++i) {
        low = shortcuts_high[i];
        if (low < 95) {
            for (;;) {
                size_t pivot = xx_tlsh_partition(buckets, low, high);
                if (pivot < 95) low = pivot + 1;
                else if (pivot > 95) high = pivot - 1;
                else { *q3 = buckets[95]; break; }
            }
            break;
        }
        if (low == 95) { *q3 = buckets[95]; break; }
        high = low;
    }
}

static unsigned xx_tlsh_length_code(size_t length)
{
    size_t left = 0, right = sizeof(xx_topval) / sizeof(xx_topval[0]);
    while (left < right) {
        size_t middle = left + (right - left) / 2;
        if (length <= xx_topval[middle]) right = middle;
        else left = middle + 1;
    }
    return (unsigned)left;
}

static char xx_tlsh_hex(unsigned value)
{
    static const char digits[] = "0123456789ABCDEF";
    return digits[value & 15];
}

static void xx_tlsh_hex_byte(char *out, uint8_t value)
{
    out[0] = xx_tlsh_hex(value >> 4);
    out[1] = xx_tlsh_hex(value);
}

static void xx_tlsh_hex_reversed_byte(char *out, uint8_t value)
{
    out[0] = xx_tlsh_hex(value);
    out[1] = xx_tlsh_hex(value >> 4);
}

bool xx_tlsh_hash128(const uint8_t *data, size_t length, char out[73])
{
    uint32_t buckets[256] = {0};
    uint8_t window[5] = {0}, checksum = 0, codes[32];
    size_t j0 = 0, j1 = 4, j2 = 3, j3 = 2, j4 = 1, pos = 0;
    uint32_t q1, q2, q3;
    unsigned len_code;
    if (!out || (!data && length) || length < 50 ||
        length > xx_topval[169]) return false;
    for (size_t i = 0; i < length; ++i) {
        size_t old_j4;
        uint8_t r;
        window[j0] = data[i];
        if (i >= 4) {
            checksum = xx_tlsh_pearson(0, window[j0], window[j1],
                                        checksum);
            r = xx_tlsh_pearson(2, window[j0], window[j1], window[j2]);
            ++buckets[r];
            r = xx_tlsh_pearson(3, window[j0], window[j1], window[j3]);
            ++buckets[r];
            r = xx_tlsh_pearson(5, window[j0], window[j2], window[j3]);
            ++buckets[r];
            r = xx_tlsh_pearson(7, window[j0], window[j2], window[j4]);
            ++buckets[r];
            r = xx_tlsh_pearson(11, window[j0], window[j1], window[j4]);
            ++buckets[r];
            r = xx_tlsh_pearson(13, window[j0], window[j3], window[j4]);
            ++buckets[r];
        }
        old_j4 = j4;
        j4 = j3; j3 = j2; j2 = j1; j1 = j0; j0 = old_j4;
    }
    xx_tlsh_quartiles(buckets, &q1, &q2, &q3);
    if (!q3) return false;
    for (size_t i = 0; i < 32; ++i) {
        unsigned code = 0;
        for (size_t j = 0; j < 4; ++j) {
            uint32_t count = buckets[4 * i + j];
            unsigned part = count > q3 ? 3 : count > q2 ? 2 :
                            count > q1 ? 1 : 0;
            code |= part << (j * 2);
        }
        codes[i] = (uint8_t)code;
    }
    len_code = xx_tlsh_length_code(length);
    out[pos++] = 'T'; out[pos++] = '1';
    xx_tlsh_hex_reversed_byte(out + pos, checksum); pos += 2;
    xx_tlsh_hex_reversed_byte(out + pos, (uint8_t)len_code); pos += 2;
    xx_tlsh_hex_byte(out + pos,
        (uint8_t)((((unsigned)((double)q1 * 100.0 / (double)q3)) & 15) << 4 |
                  (((unsigned)((double)q2 * 100.0 / (double)q3)) & 15)));
    pos += 2;
    for (size_t i = 32; i > 0; --i) {
        xx_tlsh_hex_byte(out + pos, codes[i - 1]);
        pos += 2;
    }
    out[pos] = '\0';
    return pos == 72;
}
