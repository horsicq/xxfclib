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

#ifndef CDIE_XPNG_H
#define CDIE_XPNG_H

#include "../../die_engine/die_engine_bin.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The pieces of a PNG that the database asks about: the mandatory IHDR, plus
 * the optional pHYs and bKGD chunks that XPNG::getInfo appends to the
 * description string.                                                      */
typedef struct {
    int bValid;

    cd_u32 nWidth;
    cd_u32 nHeight;
    cd_u8 nBitDepth;
    cd_u8 nColorType;
    cd_u8 nCompression;
    cd_u8 nFilter;
    cd_u8 nInterlace;

    int bHasPhys;
    cd_u32 nPixelsPerUnitX;
    cd_u32 nPixelsPerUnitY;
    cd_u8 nUnitSpecifier;

    /* 0 none, 1 grayscale, 2 truecolour, 3 palette index. */
    int nBkgdType;
    cd_u16 nBkgdGray;
    cd_u16 nBkgdRed;
    cd_u16 nBkgdGreen;
    cd_u16 nBkgdBlue;
    cd_u8 nBkgdPaletteIndex;
} XPNG;

int xpng_parse(DieFile *pFile, XPNG *pPng);

/* The bracketed text after "PNG" in the result line, for example
 * "723x464, 8 bits, RGB, pHYs: 3780x3780 meter". The caller owns the
 * returned string; an invalid or dimensionless image gives "".            */
char *xpng_info_string(XPNG *pPng);

#ifdef __cplusplus
}
#endif

#endif
