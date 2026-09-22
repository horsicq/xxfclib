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

#ifndef XX_CRC_INTERNAL_H
#define XX_CRC_INTERNAL_H

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reflection helper */
static inline uint64_t xx_crc_reflect(uint64_t val, uint8_t width) {
    uint64_t res = 0;
    for (uint8_t i = 0; i < width; ++i) {
        if ((val >> i) & 1ULL) {
            res |= (1ULL << (width - 1 - i));
        }
    }
    return res;
}

static inline uint64_t xx_crc_mask(uint8_t width) {
    if (width >= 64) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }
    return (1ULL << width) - 1ULL;
}

/* Fast submodule dispatchers with precomputed tables */
bool xx_crc8_has_fast(xx_crc_type_t type);
uint8_t xx_crc8_fast(xx_crc_type_t type, const void *data, size_t size);

bool xx_crc16_has_fast(xx_crc_type_t type);
uint16_t xx_crc16_fast(xx_crc_type_t type, const void *data, size_t size);

bool xx_crc32_has_fast(xx_crc_type_t type);
uint32_t xx_crc32_fast(xx_crc_type_t type, const void *data, size_t size);

bool xx_crc64_has_fast(xx_crc_type_t type);
uint64_t xx_crc64_fast(xx_crc_type_t type, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XX_CRC_INTERNAL_H */
