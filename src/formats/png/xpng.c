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

/* xpng.c - PNG chunk walker.
 *
 * Only what the database needs: IHDR for the dimensions and colour model,
 * pHYs for the physical resolution and bKGD for the background colour.
 * Everything else is skipped by length. Mirrors XPNG::getIHDR / getpHYs /
 * getbKGD and the string XPNG::getInfo builds from them.
 */

#include "../../formats/png/xpng.h"
#include "../../die_engine/die_engine_compat.h"

/* Colour type codes from the PNG specification. */
#define PNG_COLOR_GRAYSCALE 0
#define PNG_COLOR_RGB 2
#define PNG_COLOR_PALETTE 3
#define PNG_COLOR_GRAYSCALE_ALPHA 4
#define PNG_COLOR_RGBA 6

static int chunk_name_is(DieFile *pFile, cd_i64 nOffset, const char *pName)
{
    int i = 0;

    for (i = 0; i < 4; i++) {
        if ((cd_u8)xx_io_get_u8(pFile->pDevice, nOffset + i) != (cd_u8)pName[i]) {
            return 0;
        }
    }

    return 1;
}

int xpng_parse(DieFile *pFile, XPNG *pPng)
{
    cd_i64 nOffset = 8; /* past the 8 byte signature */

    x_memset(pPng, 0, sizeof(XPNG));

    if (pFile == NULL) {
        return 0;
    }

    /* 89 50 4E 47 0D 0A 1A 0A */
    if ((pFile->nSize < 8) || (xx_io_get_u8(pFile->pDevice, 0) != 0x89) || (xx_io_get_u8(pFile->pDevice, 1) != 0x50) || (xx_io_get_u8(pFile->pDevice, 2) != 0x4E) || (xx_io_get_u8(pFile->pDevice, 3) != 0x47)) {
        return 0;
    }

    /* length(4) + type(4) + data + crc(4) */
    while ((nOffset + 12) <= pFile->nSize) {
        cd_u32 nDataSize = xx_io_get_u32(pFile->pDevice, nOffset, true);
        cd_i64 nDataOffset = nOffset + 8;

        /* A length that runs past the end means the file is truncated or
         * this is not really a chunk header; stop rather than wrap. The
         * loop condition guarantees nDataOffset <= nSize - 4, so the
         * subtraction below stays positive.                               */
        if ((cd_i64)nDataSize > (pFile->nSize - nDataOffset)) {
            break;
        }

        if (chunk_name_is(pFile, nOffset + 4, "IHDR")) {
            if (nDataSize >= 13) {
                pPng->nWidth = xx_io_get_u32(pFile->pDevice, nDataOffset + 0, true);
                pPng->nHeight = xx_io_get_u32(pFile->pDevice, nDataOffset + 4, true);
                pPng->nBitDepth = xx_io_get_u8(pFile->pDevice, nDataOffset + 8);
                pPng->nColorType = xx_io_get_u8(pFile->pDevice, nDataOffset + 9);
                pPng->nCompression = xx_io_get_u8(pFile->pDevice, nDataOffset + 10);
                pPng->nFilter = xx_io_get_u8(pFile->pDevice, nDataOffset + 11);
                pPng->nInterlace = xx_io_get_u8(pFile->pDevice, nDataOffset + 12);
                pPng->bValid = 1;
            }
        } else if (chunk_name_is(pFile, nOffset + 4, "pHYs")) {
            if (nDataSize >= 9) {
                pPng->nPixelsPerUnitX = xx_io_get_u32(pFile->pDevice, nDataOffset + 0, true);
                pPng->nPixelsPerUnitY = xx_io_get_u32(pFile->pDevice, nDataOffset + 4, true);
                pPng->nUnitSpecifier = xx_io_get_u8(pFile->pDevice, nDataOffset + 8);
                pPng->bHasPhys = 1;
            }
        } else if (chunk_name_is(pFile, nOffset + 4, "bKGD")) {
            /* The chunk length alone tells which variant this is. */
            if (nDataSize == 1) {
                pPng->nBkgdPaletteIndex = xx_io_get_u8(pFile->pDevice, nDataOffset);
                pPng->nBkgdType = 3;
            } else if (nDataSize == 2) {
                pPng->nBkgdGray = xx_io_get_u16(pFile->pDevice, nDataOffset, true);
                pPng->nBkgdType = 1;
            } else if (nDataSize == 6) {
                pPng->nBkgdRed = xx_io_get_u16(pFile->pDevice, nDataOffset + 0, true);
                pPng->nBkgdGreen = xx_io_get_u16(pFile->pDevice, nDataOffset + 2, true);
                pPng->nBkgdBlue = xx_io_get_u16(pFile->pDevice, nDataOffset + 4, true);
                pPng->nBkgdType = 2;
            }
        } else if (chunk_name_is(pFile, nOffset + 4, "IEND")) {
            break;
        }

        nOffset += 12 + (cd_i64)nDataSize;
    }

    return pPng->bValid;
}

char *xpng_info_string(XPNG *pPng)
{
    CDBuf buf;
    const char *pSchema = NULL;

    cdbuf_init(&buf);

    if ((!pPng->bValid) || (pPng->nWidth == 0) || (pPng->nHeight == 0)) {
        return cdbuf_detach(&buf, NULL);
    }

    switch (pPng->nColorType) {
        case PNG_COLOR_GRAYSCALE: pSchema = "Grayscale"; break;
        case PNG_COLOR_RGB: pSchema = "RGB"; break;
        case PNG_COLOR_PALETTE: pSchema = "Palette"; break;
        case PNG_COLOR_GRAYSCALE_ALPHA: pSchema = "Grayscale+Alpha"; break;
        case PNG_COLOR_RGBA: pSchema = "RGBA"; break;
        default: pSchema = NULL; break;
    }

    cdbuf_appendf(&buf, "%ux%u, %u bits, ", (unsigned)pPng->nWidth, (unsigned)pPng->nHeight, (unsigned)pPng->nBitDepth);

    if (pSchema) {
        cdbuf_append_str(&buf, pSchema);
    } else {
        cdbuf_appendf(&buf, "Unknown(%u)", (unsigned)pPng->nColorType);
    }

    if (pPng->bHasPhys && (pPng->nPixelsPerUnitX || pPng->nPixelsPerUnitY)) {
        cdbuf_appendf(&buf, ", pHYs: %ux%u %s", (unsigned)pPng->nPixelsPerUnitX, (unsigned)pPng->nPixelsPerUnitY,
                      (pPng->nUnitSpecifier == 1) ? "meter" : "unknown");
    }

    if (pPng->nBkgdType == 1) {
        cdbuf_appendf(&buf, ", bKGD: gray=%u", (unsigned)pPng->nBkgdGray);
    } else if (pPng->nBkgdType == 2) {
        cdbuf_appendf(&buf, ", bKGD: rgb=(%u,%u,%u)", (unsigned)pPng->nBkgdRed, (unsigned)pPng->nBkgdGreen, (unsigned)pPng->nBkgdBlue);
    } else if (pPng->nBkgdType == 3) {
        cdbuf_appendf(&buf, ", bKGD: paletteIndex=%u", (unsigned)pPng->nBkgdPaletteIndex);
    }

    return cdbuf_detach(&buf, NULL);
}
