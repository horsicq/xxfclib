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
#include "xxfclib/algo/winzipjpeg/xx_winzipjpeg.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/memory/xx_memory.h"
#include "../lzma/xx_lzma_internal.h"

// ZIP method 96: WinZip JPEG recompression, implemented from the official
// specification "JPEG Compression - Method 96" (WinZip Computing, 2008) and
// the log-domain binary arithmetic coder of the expired U.S. patent 4,791,403
// that the specification incorporates by reference (section 6).
//
// Known corrections to the published formulas, byte-verified against real
// WinZip-produced streams (each marked SPEC-ERRATUM below at the site):
//  - AVG (5.6.2.2) sums Bw[x], not Bw[k], and the DC coefficient never
//    participates as an x.
//  - BDR (5.6.2.3) and DC prediction (5.6.7.1) use (Bn[x] + Bc[x]) /
//    (Bw[x] + Bc[x]); the printed minus sign is wrong.
//  - Prediction refinement (5.6.7.2) sums abs(Bn[x] - Bc[x]) without the
//    inner absolute values.
//  - DC sign contexts (5.6.7.3.2) compare the neighbour DC values against the
//    predicted DC, not against zero.
//  - Binarization (5.6.4): the unary magnitude bins are indexed by the count
//    of preceding one bits capped at (cap - 1), and the remainder bits are
//    coded most-significant first with one bin per bit position.

const int64_t WZJPEG_MAX_METADATA_SIZE = 16 * 1024 * 1024;  // spec 4.1.1
static const size_t WZJPEG_MAX_WORKING_MEMORY = 64U * 1024U * 1024U;
const int32_t WZJPEG_OUTPUT_FLUSH_SIZE = 0x40000;

#include "xx_winzipjpeg_tables.inc"

// Probability table of the arithmetic coder, reproduced in section 6 of the
// official specification (parameters kavg = 5, kmax = 11; index 48 is the
// appended fixed-statistics entry).
static const uint16_t g_wzjpegLogP[49] = {
    1024, 895, 795, 706, 628, 559, 493, 437, 379, 331, 287, 247, 212, 186, 158, 143, 127, 110, 98, 84, 72, 65, 59, 53,   48,
    45,   42,  40,  37,  35,  33,  30,  28,  26,  23,  21,  19,  17,  15,  13,  11,  9,   7,   5,  4,  3,  2,  1,  1024,
};

static const uint16_t g_wzjpegLogQP[49] = {
    0,    272,  502,  726,  941,  1150, 1371, 1578, 1819, 2044, 2278, 2521, 2765, 2971, 3227, 3382, 3566, 3788, 3965, 4200, 4435, 4590,  4737,  4899, 5050,
    5147, 5250, 5325, 5441, 5527, 5617, 5758, 5863, 5976, 6157, 6295, 6447, 6616, 6806, 7024, 7278, 7585, 7972, 8495, 8884, 9309, 10065, 11689, 0,
};

static const uint16_t g_wzjpegNMaxLP[49] = {
    16384, 16110, 15105, 14826, 14444, 13975, 13804, 13547, 13265, 13240, 12915, 12844, 12720, 12648, 12482, 12441, 12319,
    12320, 12250, 12180, 12168, 12155, 12154, 12084, 12096, 12105, 12096, 12080, 12062, 12075, 12078, 12060, 12068, 12090,
    12075, 12075, 12103, 12121, 12150, 12181, 12221, 12294, 12411, 12615, 13120, 13113, 14574, 21860, 0,
};

static const uint8_t g_wzjpegHalfI[49] = {
    8, 8, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 8, 9, 10, 10, 10, 10, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 6, 5, 5, 4, 4, 3, 3, 3, 3, 2, 2, 1, 0, 0,
};

static const uint8_t g_wzjpegDblI[49] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 10, 10, 10, 10, 10, 9, 8, 7, 6, 6, 5, 4, 3, 3, 3, 2, 1, 0,
};

// Standard JPEG zigzag facts.
static const uint8_t g_wzjpegZigZag[8][8] = {
    {0, 1, 5, 6, 14, 15, 27, 28},     {2, 4, 7, 13, 16, 26, 29, 42},    {3, 8, 12, 17, 25, 30, 41, 43},   {9, 11, 18, 24, 31, 40, 44, 53},
    {10, 19, 23, 32, 39, 45, 52, 54}, {20, 22, 33, 38, 46, 51, 55, 60}, {21, 34, 37, 47, 50, 56, 59, 61}, {35, 36, 48, 49, 57, 58, 62, 63},
};

static const uint8_t g_wzjpegRow[64] = {
    0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3,
    4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 4, 5, 6, 7, 7, 6, 5, 6, 7, 7,
};

static const uint8_t g_wzjpegColumn[64] = {
    0, 1, 0, 0, 1, 2, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4,
    3, 2, 1, 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 5, 6, 7, 7, 6, 7,
};

/* Reader bounded to the ZIP entry's compressed payload. */
typedef struct WZJPEG_INPUT {
    const uint8_t *data;
    size_t size;
    size_t position;
    bool bReadError;
} WZJPEG_INPUT;
static int32_t wzjpegInputReadByte(WZJPEG_INPUT *pInput)
{
    if (pInput->position == pInput->size) {
        pInput->bReadError = true;
        return -1;
    }
    return pInput->data[pInput->position++];
}

static bool wzjpegInputReadFull(WZJPEG_INPUT *pInput, uint8_t *pBuffer, int64_t nSize)
{
    for (int64_t i = 0; i < nSize; i++) {
        const int32_t nByte = wzjpegInputReadByte(pInput);
        if (nByte < 0) return false;
        pBuffer[i] = (uint8_t)nByte;
    }
    return true;
}

static bool wzjpegInputSkip(WZJPEG_INPUT *pInput, int64_t nSize)
{
    for (int64_t i = 0; i < nSize; i++) {
        if (wzjpegInputReadByte(pInput) < 0) return false;
    }
    return true;
}

// One adaptive probability bin of the arithmetic coder.
typedef struct WZJPEG_BIN {
    int32_t nIndex;  // index into the probability table
    int32_t nDLRM;   // distance from lr to the next adaptation point
    uint8_t nMPS;    // current most-probable-symbol value
    uint8_t nK;      // LPS occurrence count
} WZJPEG_BIN;

static void wzjpegBinInit(WZJPEG_BIN *pBin)
{
    pBin->nIndex = 0;
    pBin->nDLRM = g_wzjpegNMaxLP[0];
    pBin->nMPS = 0;
    pBin->nK = 0;
}

static void wzjpegBinInitFixed(WZJPEG_BIN *pBin)
{
    pBin->nIndex = 48;  // appended fixed-statistics entry
    pBin->nDLRM = g_wzjpegNMaxLP[0];
    pBin->nMPS = 0;
    pBin->nK = 0;
}

// Log-domain binary arithmetic decoder (U.S. patent 4,791,403 with the two
// WinZip modifications to QSMALLER/QBIGGER shown in the specification).
typedef struct WZJPEG_BAC {
    WZJPEG_INPUT *pInput;
    bool bEndOfData;
    uint8_t nCurrentByte;
    uint8_t nLastByte;
    uint32_t nX;   // finite-precision window on the code stream
    int32_t nLR;   // minus log2 of the range, 10 fraction bits
    int32_t nLRM;  // lr bound before the next adaptation check
    int32_t nLX;   // minus log2 of x
} WZJPEG_BAC;

static uint8_t wzjpegBacByteIn(WZJPEG_BAC *pBac)
{
    pBac->nLastByte = pBac->nCurrentByte;
    const int32_t nByte = wzjpegInputReadByte(pBac->pInput);
    if (nByte < 0) {
        pBac->bEndOfData = true;
        pBac->nCurrentByte = 0;
    } else {
        pBac->nCurrentByte = (uint8_t)nByte;
    }
    return pBac->nCurrentByte;
}

static int32_t wzjpegBacLogX(uint32_t nX)
{
    const uint32_t nHighBits = nX >> 12;
    if (nHighBits == 0) return 0x2000;

    int32_t nWhole = 0;
    if (nHighBits < 512) {
        // characteristic: 8 - floor(log2(highbits))
        uint32_t nValue = nHighBits;
        int32_t nLog = 0;
        while (nValue > 1) {
            nValue >>= 1;
            nLog++;
        }
        nWhole = 8 - nLog;
    }

    const int32_t nShift = 8 - nWhole;
    int32_t nNegFraction = 0;
    if (nShift >= 0) nNegFraction = g_wzjpegLogTable[(nX >> nShift) & 0xfff];
    else nNegFraction = g_wzjpegLogTable[(nX << (-nShift)) & 0xfff];

    return (nWhole << 10) - nNegFraction;
}

static uint32_t wzjpegBacAntilogX(int32_t nLR)
{
    const int32_t nWhole = nLR >> 10;
    const uint32_t nFraction = (uint32_t)(nLR & 0x3ff);
    const int32_t nShift = 7 - nWhole;
    if (nShift >= 0) return (uint32_t)g_wzjpegAntilogTable[nFraction] << nShift;
    return (uint32_t)g_wzjpegAntilogTable[nFraction] >> (-nShift);
}

static void wzjpegBacRenorm(WZJPEG_BAC *pBac)
{
    while (pBac->nLR > 0x1fff) {
        if ((pBac->nCurrentByte == 0xff) && (pBac->nLastByte == 0xff)) {
            pBac->nX += wzjpegBacByteIn(pBac);  // stuffed carry byte
        }
        pBac->nX = (pBac->nX << 8) | wzjpegBacByteIn(pBac);
        pBac->nLR -= 0x2000;
        pBac->nLRM -= 0x2000;
    }
    pBac->nLX = wzjpegBacLogX(pBac->nX);
}

static void wzjpegBacLRMBig(WZJPEG_BAC *pBac)
{
    if (pBac->nLRM > 0x7ff) wzjpegBacRenorm(pBac);
}

static void wzjpegBacInit(WZJPEG_BAC *pBac, WZJPEG_INPUT *pInput)
{
    pBac->pInput = pInput;
    pBac->bEndOfData = false;
    pBac->nCurrentByte = 0;
    pBac->nLastByte = 0;

    const uint8_t nByte1 = wzjpegBacByteIn(pBac);
    const uint8_t nByte2 = wzjpegBacByteIn(pBac);
    pBac->nX = ((uint32_t)nByte1 << 8) | nByte2;

    pBac->nLR = 0x1001;
    pBac->nLRM = pBac->nLR;
    pBac->nLX = wzjpegBacLogX(pBac->nX);

    if (pBac->nX == 0xffff) wzjpegBacByteIn(pBac);  // skip stuffed byte
}

static void wzjpegBacFlush(WZJPEG_BAC *pBac)
{
    wzjpegBacRenorm(pBac);
    if ((pBac->nCurrentByte == 0xff) && (pBac->nLastByte == 0xff)) wzjpegBacByteIn(pBac);
}

// QSMALLER with the WinZip modification: the index saturates at 47 instead of
// backing up from the sentinel.
static void wzjpegBacQSmaller(WZJPEG_BIN *pBin)
{
    if (pBin->nIndex >= 47) return;
    pBin->nIndex++;
    if (pBin->nK <= 1) {  // kmin1
        pBin->nIndex += g_wzjpegHalfI[pBin->nIndex];
        if (pBin->nK <= 0) {  // kmin2
            pBin->nIndex += g_wzjpegHalfI[pBin->nIndex];
        }
    }
}

static void wzjpegBacUpdateMPS(WZJPEG_BAC *pBac, WZJPEG_BIN *pBin)
{
    if (pBin->nK <= 5) wzjpegBacQSmaller(pBin);  // kmin
    pBin->nK = 0;
    pBac->nLRM = pBac->nLR + g_wzjpegNMaxLP[pBin->nIndex];
    wzjpegBacLRMBig(pBac);
}

static void wzjpegBacIncrIndex(int32_t *pnIndex, int32_t *pnIncrSaved)
{
    if (*pnIndex > 0) (*pnIndex)--;
    else (*pnIncrSaved)++;
}

static void wzjpegBacDblIndex(int32_t *pnIndex, int32_t *pnIncrSaved)
{
    if (*pnIndex > 0) *pnIndex -= g_wzjpegDblI[*pnIndex];
    else *pnIncrSaved += g_wzjpegDblI[*pnIndex];
}

// QBIGGER with the WinZip modification: the fixed-statistics entry (index 48)
// never adapts.
static void wzjpegBacQBigger(WZJPEG_BAC *pBac, WZJPEG_BIN *pBin)
{
    if (pBin->nIndex >= 48) return;

    int32_t nDLRM = pBac->nLRM - pBac->nLR;
    int32_t nIncrSaved = 0;

    if (nDLRM >= g_wzjpegNMaxLP[pBin->nIndex] / 2) {
        nDLRM = g_wzjpegNMaxLP[pBin->nIndex] - nDLRM;
        if (nDLRM <= g_wzjpegNMaxLP[pBin->nIndex] / 4) wzjpegBacDblIndex(&pBin->nIndex, &nIncrSaved);
        wzjpegBacDblIndex(&pBin->nIndex, &nIncrSaved);
    } else {
        if (nDLRM >= g_wzjpegNMaxLP[pBin->nIndex] / 4) wzjpegBacIncrIndex(&pBin->nIndex, &nIncrSaved);
        wzjpegBacIncrIndex(&pBin->nIndex, &nIncrSaved);
    }

    if (pBin->nIndex <= 0) {
        pBin->nIndex = nIncrSaved;
        pBin->nMPS = pBin->nMPS ^ 1;
    }

    pBac->nLRM = pBac->nLR + nDLRM;
}

static void wzjpegBacUpdateLPS(WZJPEG_BAC *pBac, WZJPEG_BIN *pBin)
{
    pBac->nLR += g_wzjpegLogQP[pBin->nIndex];
    pBac->nLRM += g_wzjpegLogQP[pBin->nIndex];

    if (pBin->nK >= 11) {  // kmax
        wzjpegBacQBigger(pBac, pBin);
        pBin->nK = 0;
        pBac->nLRM = pBac->nLR + g_wzjpegNMaxLP[pBin->nIndex];
    } else {
        if (pBac->nLRM < pBac->nLR) pBac->nLRM = pBac->nLR;
    }
}

static int32_t wzjpegBacDecodeBit(WZJPEG_BAC *pBac, WZJPEG_BIN *pBin)
{
    pBac->nLRM = pBac->nLR + pBin->nDLRM;
    wzjpegBacLRMBig(pBac);

    pBac->nLR += g_wzjpegLogP[pBin->nIndex];

    int32_t nBit = pBin->nMPS;

    int32_t nLRT = pBac->nLRM;
    if (pBac->nLX < nLRT) nLRT = pBac->nLX;

    if (pBac->nLR >= nLRT) {
        if (pBac->nLR < pBac->nLX) {
            wzjpegBacUpdateMPS(pBac, pBin);
        } else {
            wzjpegBacRenorm(pBac);
            if (pBac->nLR < pBac->nLX) {
                if (pBac->nLR >= pBac->nLRM) wzjpegBacUpdateMPS(pBac, pBin);
            } else {
                nBit ^= 1;
                pBin->nK++;
                pBac->nX -= wzjpegBacAntilogX(pBac->nLR);
                pBac->nLX = wzjpegBacLogX(pBac->nX);
                wzjpegBacUpdateLPS(pBac, pBin);
            }
        }
    }

    pBin->nDLRM = pBac->nLRM - pBac->nLR;

    return nBit;
}

// JPEG metadata state, persistent across bundles.
typedef struct WZJPEG_HUFFCODE {
    uint16_t nCode;
    uint8_t nLength;  // 0 = value has no code in this table
} WZJPEG_HUFFCODE;

typedef struct WZJPEG_COMPONENT {
    int32_t nIdentifier;
    int32_t nHorizontalFactor;
    int32_t nVerticalFactor;
    int32_t nQuantIndex;
} WZJPEG_COMPONENT;

typedef struct WZJPEG_SCANCOMPONENT {
    int32_t nComponentIndex;
    int32_t nDCTable;
    int32_t nACTable;
} WZJPEG_SCANCOMPONENT;

typedef struct WZJPEG_METADATA {
    uint16_t quantTables[4][64];
    WZJPEG_HUFFCODE huffCodes[2][4][256];  // [class][index][value]
    int32_t nBits;
    int32_t nHeight;
    int32_t nWidth;
    int32_t nNumComponents;
    WZJPEG_COMPONENT components[4];
    int32_t nMaxHorizontalFactor;
    int32_t nMaxVerticalFactor;
    int32_t nHorizontalMCUs;
    int32_t nVerticalMCUs;
    int32_t nNumScanComponents;
    WZJPEG_SCANCOMPONENT scanComponents[4];
    int32_t nRestartInterval;
} WZJPEG_METADATA;

typedef enum WZJPEG_PARSE_RESULT {
    WZJPEG_PARSE_FAILED = 0,
    WZJPEG_PARSE_FOUND_SOS,
    WZJPEG_PARSE_FOUND_EOI
} WZJPEG_PARSE_RESULT;

static int32_t wzjpegParseUInt16BE(const uint8_t *pData)
{
    return ((int32_t)pData[0] << 8) | pData[1];
}

// Find the next marker byte, skipping 0xff fill bytes. Returns -1 on failure.
static int64_t wzjpegFindNextMarker(const uint8_t *pData, int64_t nSize, int64_t nOffset)
{
    if (nOffset >= nSize) return -1;
    if (pData[nOffset] != 0xff) return -1;
    while (pData[nOffset] == 0xff) {
        nOffset++;
        if (nOffset >= nSize) return -1;
    }
    return nOffset;
}

// Parse and bound-check a marker segment size.
static int32_t wzjpegParseSegmentSize(const uint8_t *pData, int64_t nSize, int64_t nOffset)
{
    if (nOffset + 2 > nSize) return 0;
    const int32_t nSegmentSize = wzjpegParseUInt16BE(pData + nOffset);
    if (nSegmentSize < 2) return 0;
    if (nOffset + nSegmentSize > nSize) return 0;
    return nSegmentSize;
}

// Parse the recognized markers of one bundle's metadata (spec 5.2), updating
// the persistent state. Stops at SOS or EOI.
static WZJPEG_PARSE_RESULT wzjpegParseMetadata(WZJPEG_METADATA *pMeta, const uint8_t *pData, int64_t nSize)
{
    int64_t nOffset = 0;

    for (;;) {
        nOffset = wzjpegFindNextMarker(pData, nSize, nOffset);
        if (nOffset < 0) return WZJPEG_PARSE_FAILED;

        const uint8_t nMarker = pData[nOffset];
        nOffset++;

        if (nMarker == 0xd8) {  // SOI
            continue;
        } else if (nMarker == 0xd9) {  // EOI
            return WZJPEG_PARSE_FOUND_EOI;
        } else if (nMarker == 0xc4) {  // DHT
            const int32_t nSegmentSize = wzjpegParseSegmentSize(pData, nSize, nOffset);
            if (!nSegmentSize) return WZJPEG_PARSE_FAILED;
            const int64_t nNext = nOffset + nSegmentSize;
            int64_t nPos = nOffset + 2;

            while (nPos + 17 <= nNext) {
                const int32_t nClass = pData[nPos] >> 4;
                const int32_t nIndex = pData[nPos] & 0x0f;
                nPos++;
                if ((nClass != 0) && (nClass != 1)) return WZJPEG_PARSE_FAILED;
                if (nIndex >= 4) return WZJPEG_PARSE_FAILED;

                int32_t nCodesPerLength[16];
                int32_t nTotalCodes = 0;
                for (int32_t i = 0; i < 16; i++) {
                    nCodesPerLength[i] = pData[nPos + i];
                    nTotalCodes += nCodesPerLength[i];
                }
                nPos += 16;
                if (nPos + nTotalCodes > nNext) return WZJPEG_PARSE_FAILED;

                // Canonical code assignment (ITU T.81 Annex C).
                xx_mem_zero(pMeta->huffCodes[nClass][nIndex], sizeof(pMeta->huffCodes[nClass][nIndex]));
                uint32_t nCode = 0;
                for (int32_t i = 0; i < 16; i++) {
                    for (int32_t j = 0; j < nCodesPerLength[i]; j++) {
                        const int32_t nValue = pData[nPos];
                        nPos++;
                        pMeta->huffCodes[nClass][nIndex][nValue].nCode = (uint16_t)nCode;
                        pMeta->huffCodes[nClass][nIndex][nValue].nLength = (uint8_t)(i + 1);
                        nCode++;
                    }
                    nCode <<= 1;
                }
            }

            nOffset = nNext;
        } else if (nMarker == 0xdb) {  // DQT
            const int32_t nSegmentSize = wzjpegParseSegmentSize(pData, nSize, nOffset);
            if (!nSegmentSize) return WZJPEG_PARSE_FAILED;
            const int64_t nNext = nOffset + nSegmentSize;
            int64_t nPos = nOffset + 2;

            while (nPos + 1 <= nNext) {
                const int32_t nPrecision = pData[nPos] >> 4;
                const int32_t nIndex = pData[nPos] & 0x0f;
                nPos++;
                if (nIndex >= 4) return WZJPEG_PARSE_FAILED;

                if (nPrecision == 0) {
                    if (nPos + 64 > nNext) return WZJPEG_PARSE_FAILED;
                    for (int32_t i = 0; i < 64; i++) pMeta->quantTables[nIndex][i] = pData[nPos + i];
                    nPos += 64;
                } else if (nPrecision == 1) {
                    if (nPos + 128 > nNext) return WZJPEG_PARSE_FAILED;
                    for (int32_t i = 0; i < 64; i++) pMeta->quantTables[nIndex][i] = (uint16_t)wzjpegParseUInt16BE(pData + nPos + 2 * i);
                    nPos += 128;
                } else {
                    return WZJPEG_PARSE_FAILED;
                }
            }

            nOffset = nNext;
        } else if (nMarker == 0xdd) {  // DRI
            const int32_t nSegmentSize = wzjpegParseSegmentSize(pData, nSize, nOffset);
            if (!nSegmentSize || (nSegmentSize < 4)) return WZJPEG_PARSE_FAILED;
            pMeta->nRestartInterval = wzjpegParseUInt16BE(pData + nOffset + 2);
            nOffset += nSegmentSize;
        } else if ((nMarker == 0xc0) || (nMarker == 0xc1)) {  // SOF0/SOF1
            const int32_t nSegmentSize = wzjpegParseSegmentSize(pData, nSize, nOffset);
            if (!nSegmentSize || (nSegmentSize < 8)) return WZJPEG_PARSE_FAILED;

            pMeta->nBits = pData[nOffset + 2];
            if (pMeta->nBits != 8 && pMeta->nBits != 12) return WZJPEG_PARSE_FAILED;
            pMeta->nHeight = wzjpegParseUInt16BE(pData + nOffset + 3);
            pMeta->nWidth = wzjpegParseUInt16BE(pData + nOffset + 5);
            pMeta->nNumComponents = pData[nOffset + 7];
            if ((pMeta->nNumComponents < 1) || (pMeta->nNumComponents > 4)) return WZJPEG_PARSE_FAILED;
            if (nSegmentSize < 8 + pMeta->nNumComponents * 3) return WZJPEG_PARSE_FAILED;
            if ((pMeta->nWidth == 0) || (pMeta->nHeight == 0)) return WZJPEG_PARSE_FAILED;

            pMeta->nMaxHorizontalFactor = 1;
            pMeta->nMaxVerticalFactor = 1;

            for (int32_t i = 0; i < pMeta->nNumComponents; i++) {
                pMeta->components[i].nIdentifier = pData[nOffset + 8 + i * 3];
                pMeta->components[i].nHorizontalFactor = pData[nOffset + 9 + i * 3] >> 4;
                pMeta->components[i].nVerticalFactor = pData[nOffset + 9 + i * 3] & 0x0f;
                pMeta->components[i].nQuantIndex = pData[nOffset + 10 + i * 3];
                if (pMeta->components[i].nQuantIndex >= 4) return WZJPEG_PARSE_FAILED;
                if ((pMeta->components[i].nHorizontalFactor < 1) || (pMeta->components[i].nHorizontalFactor > 4)) return WZJPEG_PARSE_FAILED;
                if ((pMeta->components[i].nVerticalFactor < 1) || (pMeta->components[i].nVerticalFactor > 4)) return WZJPEG_PARSE_FAILED;

                if (pMeta->components[i].nHorizontalFactor > pMeta->nMaxHorizontalFactor) pMeta->nMaxHorizontalFactor = pMeta->components[i].nHorizontalFactor;
                if (pMeta->components[i].nVerticalFactor > pMeta->nMaxVerticalFactor) pMeta->nMaxVerticalFactor = pMeta->components[i].nVerticalFactor;
            }

            // Single-component frames are stored as if they used 1x1 sampling
            // regardless of the declared factors.
            if (pMeta->nNumComponents == 1) {
                pMeta->components[0].nHorizontalFactor = 1;
                pMeta->components[0].nVerticalFactor = 1;
                pMeta->nMaxHorizontalFactor = 1;
                pMeta->nMaxVerticalFactor = 1;
            }

            const int32_t nMCUWidth = pMeta->nMaxHorizontalFactor * 8;
            const int32_t nMCUHeight = pMeta->nMaxVerticalFactor * 8;
            pMeta->nHorizontalMCUs = (pMeta->nWidth + nMCUWidth - 1) / nMCUWidth;
            pMeta->nVerticalMCUs = (pMeta->nHeight + nMCUHeight - 1) / nMCUHeight;

            nOffset += nSegmentSize;
        } else if (nMarker == 0xda) {  // SOS
            const int32_t nSegmentSize = wzjpegParseSegmentSize(pData, nSize, nOffset);
            if (!nSegmentSize || (nSegmentSize < 6)) return WZJPEG_PARSE_FAILED;

            pMeta->nNumScanComponents = pData[nOffset + 2];
            if ((pMeta->nNumScanComponents < 1) || (pMeta->nNumScanComponents > 4)) return WZJPEG_PARSE_FAILED;
            if (nSegmentSize < 6 + pMeta->nNumScanComponents * 2) return WZJPEG_PARSE_FAILED;

            for (int32_t i = 0; i < pMeta->nNumScanComponents; i++) {
                const int32_t nIdentifier = pData[nOffset + 3 + i * 2];
                int32_t nComponentIndex = -1;
                for (int32_t j = 0; j < pMeta->nNumComponents; j++) {
                    if (pMeta->components[j].nIdentifier == nIdentifier) {
                        nComponentIndex = j;
                        break;
                    }
                }
                if (nComponentIndex < 0) return WZJPEG_PARSE_FAILED;
                for (int32_t coefficient = 0; coefficient < 64; ++coefficient) {
                    if (!pMeta->quantTables[pMeta->components[nComponentIndex].nQuantIndex][coefficient]) return WZJPEG_PARSE_FAILED;
                }
                for (int32_t previous = 0; previous < i; ++previous) {
                    if (pMeta->scanComponents[previous].nComponentIndex == nComponentIndex) return WZJPEG_PARSE_FAILED;
                }

                pMeta->scanComponents[i].nComponentIndex = nComponentIndex;
                pMeta->scanComponents[i].nDCTable = pData[nOffset + 4 + i * 2] >> 4;
                pMeta->scanComponents[i].nACTable = pData[nOffset + 4 + i * 2] & 0x0f;
                if ((pMeta->scanComponents[i].nDCTable >= 4) || (pMeta->scanComponents[i].nACTable >= 4)) return WZJPEG_PARSE_FAILED;
            }

            // Only full-spectrum sequential scans exist in method 96 streams.
            if (pData[nOffset + 3 + pMeta->nNumScanComponents * 2] != 0) return WZJPEG_PARSE_FAILED;
            if (pData[nOffset + 4 + pMeta->nNumScanComponents * 2] != 63) return WZJPEG_PARSE_FAILED;
            if (pData[nOffset + 5 + pMeta->nNumScanComponents * 2] != 0) return WZJPEG_PARSE_FAILED;

            return WZJPEG_PARSE_FOUND_SOS;
        } else {
            const int32_t nSegmentSize = wzjpegParseSegmentSize(pData, nSize, nOffset);
            if (!nSegmentSize) return WZJPEG_PARSE_FAILED;
            nOffset += nSegmentSize;
        }
    }
}

// One decoded 8x8 DCT block, coefficients in zigzag order.
typedef struct WZJPEG_BLOCK {
    int16_t c[64];
    uint8_t nEOB;
} WZJPEG_BLOCK;

static const WZJPEG_BLOCK g_wzjpegZeroBlock = {{0}, 0};

static int32_t wzjpegMin(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

static int32_t wzjpegAbs(int32_t x)
{
    return (x >= 0) ? x : -x;
}

static int32_t wzjpegSign(int32_t x)
{
    if (x > 0) return 1;
    if (x < 0) return -1;
    return 0;
}

// CAT (5.6.3): ceil(log2(abs(value) + 1)).
static int32_t wzjpegCategory(uint32_t nValue)
{
    int32_t nCategory = 0;
    while (nValue) {
        nValue >>= 1;
        nCategory++;
    }
    return nCategory;
}

static int32_t wzjpegRowOf(int32_t k)
{
    return g_wzjpegRow[k];
}

static int32_t wzjpegColumnOf(int32_t k)
{
    return g_wzjpegColumn[k];
}

static int32_t wzjpegZigZagAt(int32_t nRow, int32_t nColumn)
{
    return g_wzjpegZigZag[nRow][nColumn];
}

static bool wzjpegIsFirstRow(int32_t k)
{
    return wzjpegRowOf(k) == 0;
}

static bool wzjpegIsFirstColumn(int32_t k)
{
    return wzjpegColumnOf(k) == 0;
}

static bool wzjpegIsFirstRowOrColumn(int32_t k)
{
    return wzjpegIsFirstRow(k) || wzjpegIsFirstColumn(k);
}

static int32_t wzjpegLeftOf(int32_t k)
{
    return wzjpegZigZagAt(wzjpegRowOf(k), wzjpegColumnOf(k) - 1);
}

static int32_t wzjpegUpOf(int32_t k)
{
    return wzjpegZigZagAt(wzjpegRowOf(k) - 1, wzjpegColumnOf(k));
}

static int32_t wzjpegUpLeftOf(int32_t k)
{
    return wzjpegZigZagAt(wzjpegRowOf(k) - 1, wzjpegColumnOf(k) - 1);
}

static int32_t wzjpegRightOf(int32_t k)
{
    return wzjpegZigZagAt(wzjpegRowOf(k), wzjpegColumnOf(k) + 1);
}

static int32_t wzjpegDownOf(int32_t k)
{
    return wzjpegZigZagAt(wzjpegRowOf(k) + 1, wzjpegColumnOf(k));
}

// SUM (5.6.2.1): coefficients below and to the right of k. All such positions
// have a larger zigzag index than k, so at decode time (EOB downward) every
// contributing coefficient has already been decoded.
static int32_t wzjpegSum(int32_t k, const WZJPEG_BLOCK *pBlock)
{
    int32_t nSum = 0;
    const int32_t nRow = wzjpegRowOf(k);
    const int32_t nColumn = wzjpegColumnOf(k);
    for (int32_t i = 0; i < 64; i++) {
        if ((i != k) && (wzjpegRowOf(i) >= nRow) && (wzjpegColumnOf(i) >= nColumn)) nSum += wzjpegAbs(pBlock->c[i]);
    }
    return nSum;
}

// AVG (5.6.2.2). SPEC-ERRATUM: the summed neighbour term is Bw[x], not Bw[k],
// and the DC coefficient never participates.
static int32_t wzjpegAverage(int32_t k, const WZJPEG_BLOCK *pNorth, const WZJPEG_BLOCK *pWest, const uint16_t *pQuant)
{
    if ((k == 0) || (k == 1) || (k == 2)) {
        return (wzjpegAbs(pNorth->c[k]) + wzjpegAbs(pWest->c[k]) + 1) / 2;
    } else if (wzjpegIsFirstRow(k)) {
        const int32_t nLeft = wzjpegLeftOf(k);
        return (((int64_t)wzjpegAbs(pNorth->c[nLeft]) + wzjpegAbs(pWest->c[nLeft])) * pQuant[nLeft] / pQuant[k] + wzjpegAbs(pNorth->c[k]) + wzjpegAbs(pWest->c[k]) + 2) /
               (2 * 2);
    } else if (wzjpegIsFirstColumn(k)) {
        const int32_t nUp = wzjpegUpOf(k);
        return (((int64_t)wzjpegAbs(pNorth->c[nUp]) + wzjpegAbs(pWest->c[nUp])) * pQuant[nUp] / pQuant[k] + wzjpegAbs(pNorth->c[k]) + wzjpegAbs(pWest->c[k]) + 2) /
               (2 * 2);
    } else if (k == 4) {
        const int32_t nUp = wzjpegUpOf(k);
        const int32_t nLeft = wzjpegLeftOf(k);
        return (((int64_t)wzjpegAbs(pNorth->c[nUp]) + wzjpegAbs(pWest->c[nUp])) * pQuant[nUp] / pQuant[k] +
                ((int64_t)wzjpegAbs(pNorth->c[nLeft]) + wzjpegAbs(pWest->c[nLeft])) * pQuant[nLeft] / pQuant[k] + wzjpegAbs(pNorth->c[k]) + wzjpegAbs(pWest->c[k]) + 3) /
               (2 * 3);
    } else {
        const int32_t nUp = wzjpegUpOf(k);
        const int32_t nLeft = wzjpegLeftOf(k);
        const int32_t nUpLeft = wzjpegUpLeftOf(k);
        return (((int64_t)wzjpegAbs(pNorth->c[nUp]) + wzjpegAbs(pWest->c[nUp])) * pQuant[nUp] / pQuant[k] +
                ((int64_t)wzjpegAbs(pNorth->c[nLeft]) + wzjpegAbs(pWest->c[nLeft])) * pQuant[nLeft] / pQuant[k] +
                ((int64_t)wzjpegAbs(pNorth->c[nUpLeft]) + wzjpegAbs(pWest->c[nUpLeft])) * pQuant[nUpLeft] / pQuant[k] + wzjpegAbs(pNorth->c[k]) + wzjpegAbs(pWest->c[k]) +
                4) /
               (2 * 4);
    }
}

// BDR (5.6.2.3). SPEC-ERRATUM: the bracketed term is a sum, not a difference.
static int32_t wzjpegBDR(int32_t k, const WZJPEG_BLOCK *pCurrent, const WZJPEG_BLOCK *pNorth, const WZJPEG_BLOCK *pWest, const uint16_t *pQuant)
{
    if (wzjpegIsFirstRow(k)) {
        const int32_t nDown = wzjpegDownOf(k);
        return pNorth->c[k] - ((int64_t)pNorth->c[nDown] + pCurrent->c[nDown]) * pQuant[nDown] / pQuant[k];
    } else if (wzjpegIsFirstColumn(k)) {
        const int32_t nRight = wzjpegRightOf(k);
        return pWest->c[k] - ((int64_t)pWest->c[nRight] + pCurrent->c[nRight]) * pQuant[nRight] / pQuant[k];
    }
    return 0;
}

// The whole per-record decode session.
typedef struct WZJPEG_SESSION {
    WZJPEG_INPUT input;
    WZJPEG_BAC bac;
    WZJPEG_METADATA meta;

    // Arithmetic model bins, initialized per bundle (fixed bin per session).
    WZJPEG_BIN eobBins[4][13][63];
    WZJPEG_BIN zeroBins[4][62][3][6];
    WZJPEG_BIN pivotBins[4][63][5][7];
    WZJPEG_BIN acMagnitudeBins[4][3][9][9][9];
    WZJPEG_BIN acRemainderBins[4][3][7][13];
    WZJPEG_BIN acSignBins[4][27][3][2];
    WZJPEG_BIN dcMagnitudeBins[4][13][10];
    WZJPEG_BIN dcRemainderBins[4][13][14];
    WZJPEG_BIN dcSignBins[4][2][2][2];
    WZJPEG_BIN fixedBin;

    int32_t nSignContextForK[64];  // n for the 27 k that use a sign context

    // Slice geometry and buffers.
    int32_t nSliceValue;
    int32_t nSliceHeight;
    int32_t nCurrentHeight;
    int32_t nFinishedRows;
    bool bSlicesAvailable;
    WZJPEG_BLOCK *pBlocks[4];
    int64_t nBlocksPerComponent[4];

    // Huffman re-encoder state, persistent across the slices of a bundle.
    uint64_t nBitString;
    int32_t nBitLength;
    int32_t nHuffmanPredicted[4];
    int32_t nMCUCounter;
    int32_t nRestartMarkerIndex;

    // Output accumulator. Plain storage only: the whole session is zeroed
    // with memset at construction.
    uint8_t *destination;
    size_t destination_size;
    uint8_t *metadata;
    size_t metadata_size;
    char baOutputBuffer[0x40000 + 0x8000];
    int32_t nOutputSize;
    bool bWriteFailed;
    int64_t nTotalWritten;
} WZJPEG_SESSION;

static void wzjpegInitSignContextTable(WZJPEG_SESSION *pSession)
{
    // The 27 zigzag positions within the first or second rows or columns use
    // an adaptive sign context, numbered in ascending zigzag order (5.6.6.4).
    int32_t nNext = 0;
    for (int32_t k = 0; k < 64; k++) pSession->nSignContextForK[k] = 0;
    for (int32_t k = 1; k < 64; k++) {
        const bool bEligible = wzjpegIsFirstRowOrColumn(k) || (wzjpegRowOf(k) == 1) || (wzjpegColumnOf(k) == 1);
        if (bEligible) {
            pSession->nSignContextForK[k] = nNext;
            nNext++;
        }
    }
}

static void wzjpegInitBins(WZJPEG_BIN *pBins, int64_t nCount)
{
    for (int64_t i = 0; i < nCount; i++) wzjpegBinInit(&pBins[i]);
}

static void wzjpegInitAllBins(WZJPEG_SESSION *pSession)
{
    wzjpegInitBins(&pSession->eobBins[0][0][0], sizeof(pSession->eobBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->zeroBins[0][0][0][0], sizeof(pSession->zeroBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->pivotBins[0][0][0][0], sizeof(pSession->pivotBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->acMagnitudeBins[0][0][0][0][0], sizeof(pSession->acMagnitudeBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->acRemainderBins[0][0][0][0], sizeof(pSession->acRemainderBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->acSignBins[0][0][0][0], sizeof(pSession->acSignBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->dcMagnitudeBins[0][0][0], sizeof(pSession->dcMagnitudeBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->dcRemainderBins[0][0][0], sizeof(pSession->dcRemainderBins) / sizeof(WZJPEG_BIN));
    wzjpegInitBins(&pSession->dcSignBins[0][0][0][0], sizeof(pSession->dcSignBins) / sizeof(WZJPEG_BIN));
}

// Generalized Elias gamma binarization (5.6.4, with the reverse-engineered
// bin-mapping corrections noted at the top of this file).
static int32_t wzjpegDecodeBinarization(WZJPEG_BAC *pBac, WZJPEG_BIN *pMagnitudeBins, WZJPEG_BIN *pRemainderBins, int32_t nMaxBits, int32_t nCap)
{
    int32_t nOnes = 0;
    while (nOnes < nMaxBits) {
        int32_t nContext = nOnes;
        if (nContext >= nCap) nContext = nCap - 1;
        const int32_t nUnaryBit = wzjpegBacDecodeBit(pBac, &pMagnitudeBins[nContext]);
        if (nUnaryBit == 1) nOnes++;
        else break;
    }

    if (nOnes == 0) return 0;
    if (nOnes == 1) return 1;

    const int32_t nNumBits = nOnes - 1;
    int32_t nValue = 1 << nNumBits;
    for (int32_t i = nNumBits - 1; i >= 0; i--) {
        const int32_t nBit = wzjpegBacDecodeBit(pBac, &pRemainderBins[i]);
        nValue |= nBit << i;
    }
    return nValue;
}

static int32_t wzjpegDecodeACSign(WZJPEG_SESSION *pSession, int32_t nComp, int32_t k, int32_t nAbsValue, const WZJPEG_BLOCK *pCurrent, const WZJPEG_BLOCK *pNorth,
                                  const WZJPEG_BLOCK *pWest, const uint16_t *pQuant)
{
    // AC sign coding (5.6.6.4).
    int32_t nPredictedSign = 0;
    if (wzjpegIsFirstRowOrColumn(k)) {
        const int32_t nBdr = wzjpegBDR(k, pCurrent, pNorth, pWest, pQuant);
        if (nBdr == 0) return wzjpegBacDecodeBit(&pSession->bac, &pSession->fixedBin);
        nPredictedSign = (nBdr < 0) ? 1 : 0;
    } else if (k == 4) {
        const int32_t nSign1 = wzjpegSign(pNorth->c[k]);
        const int32_t nSign2 = wzjpegSign(pWest->c[k]);
        if (nSign1 + nSign2 == 0) return wzjpegBacDecodeBit(&pSession->bac, &pSession->fixedBin);
        nPredictedSign = (nSign1 + nSign2 < 0) ? 1 : 0;
    } else if (wzjpegRowOf(k) == 1) {
        if (pNorth->c[k] == 0) return wzjpegBacDecodeBit(&pSession->bac, &pSession->fixedBin);
        nPredictedSign = (pNorth->c[k] < 0) ? 1 : 0;
    } else if (wzjpegColumnOf(k) == 1) {
        if (pWest->c[k] == 0) return wzjpegBacDecodeBit(&pSession->bac, &pSession->fixedBin);
        nPredictedSign = (pWest->c[k] < 0) ? 1 : 0;
    } else {
        return wzjpegBacDecodeBit(&pSession->bac, &pSession->fixedBin);
    }

    const int32_t n = pSession->nSignContextForK[k];
    const int32_t nSignContext1 = wzjpegMin(wzjpegCategory((uint32_t)nAbsValue) / 2, 2);

    return wzjpegBacDecodeBit(&pSession->bac, &pSession->acSignBins[nComp][n][nSignContext1][nPredictedSign]);
}

static int32_t wzjpegDecodeACComponent(WZJPEG_SESSION *pSession, int32_t nComp, int32_t k, bool bCanBeZero, const WZJPEG_BLOCK *pCurrent, const WZJPEG_BLOCK *pNorth,
                                       const WZJPEG_BLOCK *pWest, const uint16_t *pQuant)
{
    if (!pNorth) pNorth = &g_wzjpegZeroBlock;
    if (!pWest) pWest = &g_wzjpegZeroBlock;

    int32_t nVal1 = 0;
    if (wzjpegIsFirstRowOrColumn(k)) nVal1 = wzjpegAbs(wzjpegBDR(k, pCurrent, pNorth, pWest, pQuant));
    else nVal1 = wzjpegAverage(k, pNorth, pWest, pQuant);

    const int32_t nVal2 = wzjpegSum(k, pCurrent);

    if (bCanBeZero) {
        // Zero/non-zero decision (5.6.6.1).
        const int32_t nZeroContext1 = wzjpegMin(wzjpegCategory((uint32_t)nVal1), 2);
        const int32_t nZeroContext2 = wzjpegMin(wzjpegCategory((uint32_t)nVal2), 5);

        const int32_t nNonZero = wzjpegBacDecodeBit(&pSession->bac, &pSession->zeroBins[nComp][k - 1][nZeroContext1][nZeroContext2]);
        if (!nNonZero) return 0;
    }

    int32_t nAbsValue = 0;

    // Pivot decision, abs >= 2 (5.6.6.2).
    const int32_t nPivotContext1 = wzjpegMin(wzjpegCategory((uint32_t)nVal1), 4);
    const int32_t nPivotContext2 = wzjpegMin(wzjpegCategory((uint32_t)nVal2), 6);

    const int32_t nPivot = wzjpegBacDecodeBit(&pSession->bac, &pSession->pivotBins[nComp][k - 1][nPivotContext1][nPivotContext2]);

    if (!nPivot) {
        nAbsValue = 1;
    } else {
        // Absolute value (5.6.6.3).
        int32_t nVal3 = 0;
        int32_t n = 0;
        if (wzjpegIsFirstRow(k)) {
            nVal3 = wzjpegColumnOf(k) - 1;
            n = 0;
        } else if (wzjpegIsFirstColumn(k)) {
            nVal3 = wzjpegRowOf(k) - 1;
            n = 1;
        } else {
            nVal3 = wzjpegCategory((uint32_t)(k - 4));
            n = 2;
        }

        const int32_t nMagnitudeContext1 = wzjpegMin(wzjpegCategory((uint32_t)nVal1), 8);
        const int32_t nMagnitudeContext2 = wzjpegMin(wzjpegCategory((uint32_t)nVal2), 8);

        nAbsValue = wzjpegDecodeBinarization(&pSession->bac, pSession->acMagnitudeBins[nComp][n][nMagnitudeContext1][nMagnitudeContext2],
                                             pSession->acRemainderBins[nComp][n][nVal3], 14, 9) +
                    2;
    }

    if (wzjpegDecodeACSign(pSession, nComp, k, nAbsValue, pCurrent, pNorth, pWest, pQuant)) return -nAbsValue;
    return nAbsValue;
}

static int32_t wzjpegDecodeDCComponent(WZJPEG_SESSION *pSession, int32_t nComp, const WZJPEG_BLOCK *pCurrent, const WZJPEG_BLOCK *pNorth, const WZJPEG_BLOCK *pWest,
                                       const uint16_t *pQuant)
{
    // DC prediction (5.6.7.1). SPEC-ERRATUM: the neighbour AC terms are added
    // to the current block's, not subtracted.
    int32_t nPredicted = 0;
    if (!pNorth && !pWest) {
        nPredicted = 0;
    } else if (!pNorth) {
        const int64_t t1 = (int64_t)pWest->c[0] * 10000 - (int64_t)11038 * pQuant[1] * (pWest->c[1] + pCurrent->c[1]) / pQuant[0];
        nPredicted = (int32_t)(((t1 < 0) ? (t1 - 5000) : (t1 + 5000)) / 10000);
    } else if (!pWest) {
        const int64_t t0 = (int64_t)pNorth->c[0] * 10000 - (int64_t)11038 * pQuant[2] * (pNorth->c[2] + pCurrent->c[2]) / pQuant[0];
        nPredicted = (int32_t)(((t0 < 0) ? (t0 - 5000) : (t0 + 5000)) / 10000);
    } else {
        const int64_t t0 = (int64_t)pNorth->c[0] * 10000 - (int64_t)11038 * pQuant[2] * (pNorth->c[2] + pCurrent->c[2]) / pQuant[0];
        const int32_t p0 = (int32_t)(((t0 < 0) ? (t0 - 5000) : (t0 + 5000)) / 10000);

        const int64_t t1 = (int64_t)pWest->c[0] * 10000 - (int64_t)11038 * pQuant[1] * (pWest->c[1] + pCurrent->c[1]) / pQuant[0];
        const int32_t p1 = (int32_t)(((t1 < 0) ? (t1 - 5000) : (t1 + 5000)) / 10000);

        // Prediction refinement (5.6.7.2). SPEC-ERRATUM: plain differences,
        // without the inner absolute values.
        int32_t d0 = 0;
        int32_t d1 = 0;
        for (int32_t i = 1; i < 8; i++) {
            d0 += wzjpegAbs(pNorth->c[wzjpegZigZagAt(i, 0)] - pCurrent->c[wzjpegZigZagAt(i, 0)]);
            d1 += wzjpegAbs(pWest->c[wzjpegZigZagAt(0, i)] - pCurrent->c[wzjpegZigZagAt(0, i)]);
        }

        if (d0 > d1) {
            const int64_t nWeight = (int64_t)1 << wzjpegMin(d0 - d1, 31);
            nPredicted = (int32_t)((nWeight * p1 + p0) / (1 + nWeight));
        } else {
            const int64_t nWeight = (int64_t)1 << wzjpegMin(d1 - d0, 31);
            nPredicted = (int32_t)((nWeight * p0 + p1) / (1 + nWeight));
        }
    }

    // DC residual absolute value (5.6.7.3.1).
    const int32_t nSum = wzjpegSum(0, pCurrent);
    const int32_t nValueContext = wzjpegMin(wzjpegCategory((uint32_t)nSum), 12);

    const int32_t nAbsValue =
        wzjpegDecodeBinarization(&pSession->bac, pSession->dcMagnitudeBins[nComp][nValueContext], pSession->dcRemainderBins[nComp][nValueContext], 15, 10);
    if (nAbsValue == 0) return nPredicted;

    // DC residual sign (5.6.7.3.2). SPEC-ERRATUM: the neighbour DC values are
    // compared against the predicted DC, not against zero.
    if (!pNorth) pNorth = &g_wzjpegZeroBlock;
    if (!pWest) pWest = &g_wzjpegZeroBlock;
    const int32_t nNorthSign = (pNorth->c[0] < nPredicted) ? 1 : 0;
    const int32_t nWestSign = (pWest->c[0] < nPredicted) ? 1 : 0;
    const int32_t nPredictedSign = (nPredicted < 0) ? 1 : 0;

    const int32_t nSign = wzjpegBacDecodeBit(&pSession->bac, &pSession->dcSignBins[nComp][nNorthSign][nWestSign][nPredictedSign]);

    if (nSign) return nPredicted - nAbsValue;
    return nPredicted + nAbsValue;
}

static void wzjpegDecodeBlock(WZJPEG_SESSION *pSession, int32_t nComp, WZJPEG_BLOCK *pCurrent, const WZJPEG_BLOCK *pNorth, const WZJPEG_BLOCK *pWest,
                              const uint16_t *pQuant)
{
    // EOB context (5.6.5.2).
    int32_t nAverage = 0;
    if (!pNorth && !pWest) nAverage = 0;
    else if (!pNorth) nAverage = wzjpegSum(0, pWest);
    else if (!pWest) nAverage = wzjpegSum(0, pNorth);
    else nAverage = (wzjpegSum(0, pNorth) + wzjpegSum(0, pWest) + 1) / 2;

    const int32_t nEOBContext = wzjpegMin(wzjpegCategory((uint32_t)nAverage), 12);

    // Binary-tree EOB decode (5.6.5.1): six bits, bins indexed by the partial
    // bit string.
    uint32_t nBitString = 1;
    for (int32_t i = 0; i < 6; i++) {
        nBitString = (nBitString << 1) | (uint32_t)wzjpegBacDecodeBit(&pSession->bac, &pSession->eobBins[nComp][nEOBContext][nBitString - 1]);
    }
    const int32_t nEOB = (int32_t)(nBitString & 0x3f);
    pCurrent->nEOB = (uint8_t)nEOB;

    for (int32_t k = nEOB + 1; k <= 63; k++) pCurrent->c[k] = 0;

    // AC coefficients from EOB down to AC1 (5.6.6); the coefficient at EOB is
    // non-zero by definition.
    for (int32_t k = nEOB; k >= 1; k--) {
        const int32_t nValue = wzjpegDecodeACComponent(pSession, nComp, k, k != nEOB, pCurrent, pNorth, pWest, pQuant);
        pCurrent->c[k] = (int16_t)nValue;
    }

    // DC (5.6.7).
    pCurrent->c[0] = (int16_t)wzjpegDecodeDCComponent(pSession, nComp, pCurrent, pNorth, pWest, pQuant);
}

// Output side: bit writer that reproduces the original entropy-coded scan,
// including 0xFF00 byte stuffing.
static bool wzjpegFlushOutput(WZJPEG_SESSION *pSession, bool bForce)
{
    if (pSession->bWriteFailed) return false;
    if (!bForce && (pSession->nOutputSize < WZJPEG_OUTPUT_FLUSH_SIZE)) return true;
    if (pSession->nOutputSize == 0) return true;

    const int32_t nSize = pSession->nOutputSize;
    if ((size_t)pSession->nTotalWritten > pSession->destination_size || (size_t)nSize > pSession->destination_size - (size_t)pSession->nTotalWritten) {
        pSession->bWriteFailed = true;
        return false;
    }
    xx_mem_copy(pSession->destination + (size_t)pSession->nTotalWritten, pSession->baOutputBuffer, (size_t)nSize);
    pSession->nTotalWritten += nSize;
    pSession->nOutputSize = 0;
    return true;
}

static void wzjpegAppendOutputByte(WZJPEG_SESSION *pSession, uint8_t nByte)
{
    if (pSession->nOutputSize >= (int32_t)sizeof(pSession->baOutputBuffer)) {
        if (!wzjpegFlushOutput(pSession, true)) return;
    }
    pSession->baOutputBuffer[pSession->nOutputSize] = (char)nByte;
    pSession->nOutputSize++;
}

static void wzjpegEmitScanByte(WZJPEG_SESSION *pSession, uint8_t nByte)
{
    wzjpegAppendOutputByte(pSession, nByte);
    if (nByte == 0xff) wzjpegAppendOutputByte(pSession, 0x00);  // byte stuffing
}

static void wzjpegPushBits(WZJPEG_SESSION *pSession, uint32_t nBits, int32_t nLength)
{
    if (nLength <= 0) return;
    pSession->nBitString |= (uint64_t)nBits << (64 - pSession->nBitLength - nLength);
    pSession->nBitLength += nLength;
    while (pSession->nBitLength >= 8) {
        const uint8_t nByte = (uint8_t)(pSession->nBitString >> 56);
        wzjpegEmitScanByte(pSession, nByte);
        pSession->nBitString <<= 8;
        pSession->nBitLength -= 8;
    }
}

static bool wzjpegPushHuffmanCode(WZJPEG_SESSION *pSession, const WZJPEG_HUFFCODE *pTable, int32_t nSymbol)
{
    if (pTable[nSymbol].nLength == 0) return false;  // symbol not in table: corrupt stream
    wzjpegPushBits(pSession, pTable[nSymbol].nCode, pTable[nSymbol].nLength);
    return true;
}

static bool wzjpegPushEncodedValue(WZJPEG_SESSION *pSession, const WZJPEG_HUFFCODE *pTable, int32_t nValue, int32_t nHighBits)
{
    int32_t nCategory = 0;
    uint32_t nBitString = 0;
    if (nValue >= 0) {
        nCategory = wzjpegCategory((uint32_t)nValue);
        const uint32_t nMask = ((uint32_t)1 << nCategory) - 1;
        nBitString = (uint32_t)nValue & nMask;
    } else {
        nCategory = wzjpegCategory((uint32_t)(-nValue));
        const uint32_t nMask = ((uint32_t)1 << nCategory) - 1;
        nBitString = ((uint32_t)nValue & nMask) - 1;
    }
    if (nCategory > 15) return false;  // not representable in a JPEG code

    if (!wzjpegPushHuffmanCode(pSession, pTable, nCategory | (nHighBits << 4))) return false;
    wzjpegPushBits(pSession, nBitString, nCategory);
    return true;
}

// Re-encode one slice's decoded blocks in MCU order, recreating restart
// markers and, at the end of the scan, the closing 1-bit padding.
static bool wzjpegEncodeSlice(WZJPEG_SESSION *pSession)
{
    WZJPEG_METADATA *pMeta = &pSession->meta;

    for (int32_t nMCURow = 0; nMCURow < pSession->nCurrentHeight; nMCURow++) {
        for (int32_t nMCUCol = 0; nMCUCol < pMeta->nHorizontalMCUs; nMCUCol++) {
            // Restart marker between MCUs at the defined interval (5.4.2),
            // never at the very end of the scan.
            if (pMeta->nRestartInterval && (pSession->nMCUCounter == pMeta->nRestartInterval)) {
                if (pSession->nBitLength) {
                    const int32_t nPad = 8 - pSession->nBitLength;
                    wzjpegPushBits(pSession, ((uint32_t)1 << nPad) - 1, nPad);
                }
                // The marker itself must not trigger byte stuffing.
                wzjpegAppendOutputByte(pSession, 0xff);
                wzjpegAppendOutputByte(pSession, (uint8_t)(0xd0 + pSession->nRestartMarkerIndex));
                pSession->nRestartMarkerIndex = (pSession->nRestartMarkerIndex + 1) & 7;
                pSession->nMCUCounter = 0;
                xx_mem_zero(pSession->nHuffmanPredicted, sizeof(pSession->nHuffmanPredicted));
            }

            for (int32_t nComp = 0; nComp < pMeta->nNumScanComponents; nComp++) {
                const WZJPEG_COMPONENT *pComponent = &pMeta->components[pMeta->scanComponents[nComp].nComponentIndex];
                const WZJPEG_HUFFCODE *pDCTable = pMeta->huffCodes[0][pMeta->scanComponents[nComp].nDCTable];
                const WZJPEG_HUFFCODE *pACTable = pMeta->huffCodes[1][pMeta->scanComponents[nComp].nACTable];
                const int32_t nHBlocks = pComponent->nHorizontalFactor;
                const int32_t nVBlocks = pComponent->nVerticalFactor;
                const int32_t nBlocksPerRow = pMeta->nHorizontalMCUs * nHBlocks;

                for (int32_t nY = 0; nY < nVBlocks; nY++) {
                    for (int32_t nX = 0; nX < nHBlocks; nX++) {
                        const int32_t nBlockX = nMCUCol * nHBlocks + nX;
                        const int32_t nBlockY = nMCURow * nVBlocks + nY;
                        const WZJPEG_BLOCK *pBlock = &pSession->pBlocks[nComp][nBlockX + (int64_t)nBlockY * nBlocksPerRow];

                        // DC difference.
                        const int32_t nDiff = pBlock->c[0] - pSession->nHuffmanPredicted[nComp];
                        if (!wzjpegPushEncodedValue(pSession, pDCTable, nDiff, 0)) return false;
                        pSession->nHuffmanPredicted[nComp] = pBlock->c[0];

                        // AC run-length coding.
                        int32_t nCoeff = 1;
                        while (nCoeff <= pBlock->nEOB) {
                            const int32_t nFirstCoeff = nCoeff;
                            const int32_t nEndRun = nCoeff + 15;
                            while ((nCoeff < 63) && (nCoeff < nEndRun) && (pBlock->c[nCoeff] == 0)) nCoeff++;

                            const int32_t nZeroes = nCoeff - nFirstCoeff;
                            const int32_t nValue = pBlock->c[nCoeff];
                            if (!wzjpegPushEncodedValue(pSession, pACTable, nValue, nZeroes)) return false;
                            nCoeff++;
                        }
                        if (pBlock->nEOB != 63) {
                            if (!wzjpegPushHuffmanCode(pSession, pACTable, 0x00)) return false;  // EOB
                        }
                    }
                }
            }

            pSession->nMCUCounter++;

            if (!wzjpegFlushOutput(pSession, false)) return false;
        }
    }

    if (!pSession->bSlicesAvailable) {
        // End of scan: pad the final partial byte with one bits (F.1.2.3).
        const int32_t nPad = (-pSession->nBitLength) & 7;
        if (nPad) wzjpegPushBits(pSession, ((uint32_t)1 << nPad) - 1, nPad);
    }

    return wzjpegFlushOutput(pSession, false);
}

// Decode one slice: per scan component, a fresh arithmetic-decoder segment
// over the component's blocks in cartesian order (5.4, 5.5).
static bool wzjpegDecodeSlice(WZJPEG_SESSION *pSession)
{
    WZJPEG_METADATA *pMeta = &pSession->meta;

    pSession->nCurrentHeight = pSession->nSliceHeight;
    if (pSession->nFinishedRows + pSession->nCurrentHeight >= pMeta->nVerticalMCUs) {
        pSession->nCurrentHeight = pMeta->nVerticalMCUs - pSession->nFinishedRows;
        pSession->bSlicesAvailable = false;
    }

    for (int32_t nComp = 0; nComp < pMeta->nNumScanComponents; nComp++) {
        wzjpegBacInit(&pSession->bac, &pSession->input);

        const WZJPEG_COMPONENT *pComponent = &pMeta->components[pMeta->scanComponents[nComp].nComponentIndex];
        const int32_t nHBlocks = pComponent->nHorizontalFactor;
        const int32_t nVBlocks = pComponent->nVerticalFactor;
        const int32_t nBlocksPerRow = pMeta->nHorizontalMCUs * nHBlocks;
        const uint16_t *pQuant = pMeta->quantTables[pComponent->nQuantIndex];

        const int32_t nRows = pSession->nCurrentHeight * nVBlocks;
        for (int32_t nY = 0; nY < nRows; nY++) {
            for (int32_t nX = 0; nX < nBlocksPerRow; nX++) {
                WZJPEG_BLOCK *pCurrent = &pSession->pBlocks[nComp][nX + (int64_t)nY * nBlocksPerRow];

                const WZJPEG_BLOCK *pNorth = NULL;
                if (nY != 0) pNorth = &pSession->pBlocks[nComp][nX + (int64_t)(nY - 1) * nBlocksPerRow];
                else if (pSession->nFinishedRows != 0) pNorth = &pSession->pBlocks[nComp][nX + (int64_t)(pSession->nSliceHeight * nVBlocks - 1) * nBlocksPerRow];

                const WZJPEG_BLOCK *pWest = NULL;
                if (nX != 0) pWest = &pSession->pBlocks[nComp][nX - 1 + (int64_t)nY * nBlocksPerRow];

                wzjpegDecodeBlock(pSession, nComp, pCurrent, pNorth, pWest, pQuant);

                if (pSession->bac.bEndOfData) return false;
            }
        }

        wzjpegBacFlush(&pSession->bac);
    }

    pSession->nFinishedRows += pSession->nCurrentHeight;

    return true;
}

static bool wzjpegWriteMetadata(WZJPEG_SESSION *pSession, const uint8_t *pData, int64_t nSize)
{
    for (int64_t i = 0; i < nSize; i++) {
        wzjpegAppendOutputByte(pSession, pData[i]);
        if (pSession->bWriteFailed) return false;
    }
    return wzjpegFlushOutput(pSession, false);
}

static void wzjpegFreeSliceBuffers(WZJPEG_SESSION *pSession)
{
    for (int32_t i = 0; i < 4; i++) {
        if (pSession->pBlocks[i]) {
            xx_mem_free(pSession->pBlocks[i]);
            pSession->pBlocks[i] = NULL;
        }
        pSession->nBlocksPerComponent[i] = 0;
    }
}

static bool wzjpegAllocateSliceBuffers(WZJPEG_SESSION *pSession)
{
    WZJPEG_METADATA *pMeta = &pSession->meta;
    size_t required = sizeof(*pSession) + pSession->metadata_size;
    size_t amounts[4] = {0, 0, 0, 0};
    wzjpegFreeSliceBuffers(pSession);
    if (required > WZJPEG_MAX_WORKING_MEMORY) return false;

    /* Preflight every component before allocating any coefficient buffer. A
     * malformed frame may request individually small buffers whose sum is huge. */
    for (int32_t nComp = 0; nComp < pMeta->nNumScanComponents; nComp++) {
        const WZJPEG_COMPONENT *pComponent = &pMeta->components[pMeta->scanComponents[nComp].nComponentIndex];
        const int64_t nBlocks = (int64_t)pMeta->nHorizontalMCUs * pSession->nSliceHeight * pComponent->nHorizontalFactor * pComponent->nVerticalFactor;
        if ((nBlocks <= 0) || (uint64_t)nBlocks > (WZJPEG_MAX_WORKING_MEMORY - required) / sizeof(WZJPEG_BLOCK)) return false;
        amounts[nComp] = (size_t)nBlocks * sizeof(WZJPEG_BLOCK);
        required += amounts[nComp];
    }

    for (int32_t nComp = 0; nComp < pMeta->nNumScanComponents; nComp++) {
        pSession->pBlocks[nComp] = (WZJPEG_BLOCK *)xx_mem_alloc(amounts[nComp]);
        if (!pSession->pBlocks[nComp]) return false;
        xx_mem_zero(pSession->pBlocks[nComp], amounts[nComp]);
        pSession->nBlocksPerComponent[nComp] = (int64_t)(amounts[nComp] / sizeof(WZJPEG_BLOCK));
    }

    return true;
}

static bool wzjpegProcessStream(WZJPEG_SESSION *pSession)
{
    // Properties Header (3.2).
    uint8_t baHeader[4];
    if (!wzjpegInputReadFull(&pSession->input, baHeader, 4)) return false;
    if (baHeader[0] < 4) return false;
    if (baHeader[1] != 0x10) return false;  // version 1.0
    if (baHeader[2] != 0x01) return false;  // compression method 1
    if (baHeader[3] & 0xe0) return false;
    if (baHeader[0] > 4) {
        if (!wzjpegInputSkip(&pSession->input, baHeader[0] - 4)) return false;
    }
    pSession->nSliceValue = baHeader[3] & 0x1f;

    bool bFirstBundle = true;

    for (;;) {
        // Bundle header (4.1.1).
        uint8_t baBundleHeader[8];
        if (!wzjpegInputReadFull(&pSession->input, baBundleHeader, 4)) return false;
        int64_t nUncompressedSize = (int64_t)baBundleHeader[0] | ((int64_t)baBundleHeader[1] << 8);
        int64_t nCompressedSize = (int64_t)baBundleHeader[2] | ((int64_t)baBundleHeader[3] << 8);
        if ((nUncompressedSize == 0xffff) && (nCompressedSize == 0xffff)) {
            if (!wzjpegInputReadFull(&pSession->input, baBundleHeader, 8)) return false;
            nUncompressedSize = (int64_t)baBundleHeader[0] | ((int64_t)baBundleHeader[1] << 8) | ((int64_t)baBundleHeader[2] << 16) | ((int64_t)baBundleHeader[3] << 24);
            nCompressedSize = (int64_t)baBundleHeader[4] | ((int64_t)baBundleHeader[5] << 8) | ((int64_t)baBundleHeader[6] << 16) | ((int64_t)baBundleHeader[7] << 24);
        }
        if ((nUncompressedSize <= 0) || (nUncompressedSize > WZJPEG_MAX_METADATA_SIZE)) return false;
        if ((uint64_t)nUncompressedSize > pSession->destination_size ||
            (uint64_t)pSession->nTotalWritten + (uint64_t)pSession->nOutputSize + (uint64_t)nUncompressedSize > pSession->destination_size)
            return false;
        if ((nCompressedSize < 0) || (nCompressedSize > WZJPEG_MAX_METADATA_SIZE)) return false;

        // Bundle metadata (4.1.2): LZMA with synthesized coder properties, or
        // stored verbatim when the compressed size field is zero.
        /* Previous scan coefficients are no longer needed. Release them before
         * allocating the next bundle's metadata and temporary LZMA buffers. */
        wzjpegFreeSliceBuffers(pSession);
        xx_mem_free(pSession->metadata);
        pSession->metadata = NULL;
        pSession->metadata_size = 0U;
        if ((uint64_t)nUncompressedSize > WZJPEG_MAX_WORKING_MEMORY - sizeof(*pSession)) return false;
        pSession->metadata = (uint8_t *)xx_mem_alloc((size_t)nUncompressedSize);
        if (!pSession->metadata) return false;
        pSession->metadata_size = (size_t)nUncompressedSize;
        if (nCompressedSize) {
            uint8_t *compressed;
            size_t written = 0;
            bool decoded;
            int64_t dictionary = (nUncompressedSize + 511) & ~(int64_t)511;
            uint8_t properties[5];
            size_t available = WZJPEG_MAX_WORKING_MEMORY - sizeof(*pSession) - pSession->metadata_size;
            size_t lzma_fixed = sizeof(lzma_decoder) + ((size_t)0x300U << 3U) * sizeof(lzma_prob);
            size_t lzma_output_buffer = xx_get_file_buffer_size();
            if (dictionary < 1024) dictionary = 1024;
            if (dictionary > 512 * 1024) dictionary = 512 * 1024;
            if ((uint64_t)nCompressedSize > available) return false;
            available -= (size_t)nCompressedSize;
            if (lzma_fixed > available) return false;
            available -= lzma_fixed;
            if ((uint64_t)dictionary > available) return false;
            available -= (size_t)dictionary;
            if (lzma_output_buffer > available) return false;
            compressed = (uint8_t *)xx_mem_alloc((size_t)nCompressedSize);
            if (!compressed) return false;
            if (!wzjpegInputReadFull(&pSession->input, compressed, nCompressedSize)) {
                xx_mem_free(compressed);
                return false;
            }
            properties[0] = 93;
            properties[1] = (uint8_t)dictionary;
            properties[2] = (uint8_t)(dictionary >> 8);
            properties[3] = (uint8_t)(dictionary >> 16);
            properties[4] = (uint8_t)(dictionary >> 24);
            decoded = xx_lzma_decompress_memory(compressed, (size_t)nCompressedSize, properties, sizeof(properties), nUncompressedSize, pSession->metadata,
                                                (size_t)nUncompressedSize, &written);
            xx_mem_free(compressed);
            if (!decoded || written != (size_t)nUncompressedSize) return false;
        } else if (!wzjpegInputReadFull(&pSession->input, pSession->metadata, nUncompressedSize)) {
            return false;
        }
        if (!wzjpegWriteMetadata(pSession, pSession->metadata, nUncompressedSize)) return false;

        // Data before the SOI marker of the first bundle is unknown metadata
        // and is not parsed (5.1).
        int64_t nParseOffset = 0;
        if (bFirstBundle) {
            int64_t nFound = -1;
            for (int64_t i = 0; i + 2 <= nUncompressedSize; i++) {
                if (((uint8_t)pSession->metadata[i] == 0xff) && ((uint8_t)pSession->metadata[i + 1] == 0xd8)) {
                    nFound = i;
                    break;
                }
            }
            if (nFound < 0) return false;
            nParseOffset = nFound;
            bFirstBundle = false;
        }

        const WZJPEG_PARSE_RESULT parseResult =
            wzjpegParseMetadata(&pSession->meta, (const uint8_t *)pSession->metadata + nParseOffset, nUncompressedSize - nParseOffset);
        if (parseResult == WZJPEG_PARSE_FAILED) return false;
        if (parseResult == WZJPEG_PARSE_FOUND_EOI) break;

        // A scan follows: prepare the models and the slice geometry.
        if ((pSession->meta.nHorizontalMCUs <= 0) || (pSession->meta.nVerticalMCUs <= 0)) return false;

        wzjpegInitAllBins(pSession);

        if (pSession->nSliceValue) {
            const int64_t nPow2Size = (int64_t)1 << (pSession->nSliceValue + 6);
            int64_t nDiv1 = nPow2Size / pSession->meta.nHorizontalMCUs;
            if (nDiv1 < 1) nDiv1 = 1;
            const int64_t nDiv2 = (pSession->meta.nVerticalMCUs + nDiv1 - 1) / nDiv1;
            pSession->nSliceHeight = (int32_t)((pSession->meta.nVerticalMCUs + nDiv2 - 1) / nDiv2);
        } else {
            pSession->nSliceHeight = pSession->meta.nVerticalMCUs;
        }
        if (pSession->nSliceHeight <= 0) return false;

        if (!wzjpegAllocateSliceBuffers(pSession)) return false;

        pSession->bSlicesAvailable = true;
        pSession->nFinishedRows = 0;

        pSession->nBitString = 0;
        pSession->nBitLength = 0;
        xx_mem_zero(pSession->nHuffmanPredicted, sizeof(pSession->nHuffmanPredicted));
        pSession->nMCUCounter = 0;
        pSession->nRestartMarkerIndex = 0;

        while (pSession->bSlicesAvailable) {
            if (!wzjpegDecodeSlice(pSession)) return false;
            if (!wzjpegEncodeSlice(pSession)) return false;
        }
    }

    return wzjpegFlushOutput(pSession, true);
}

bool xx_winzipjpeg_decompress_memory(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written)
{
    WZJPEG_SESSION *session;
    bool result;
    int32_t i;
    if (out_written) *out_written = 0;
    if (!source || !source_size || (!destination && destination_size) || destination_size > INT64_MAX || sizeof(*session) > WZJPEG_MAX_WORKING_MEMORY) return false;
    session = (WZJPEG_SESSION *)xx_mem_calloc(1, sizeof(*session));
    if (!session) return false;
    session->input.data = (const uint8_t *)source;
    session->input.size = source_size;
    session->destination = (uint8_t *)destination;
    session->destination_size = destination_size;
    wzjpegBinInitFixed(&session->fixedBin);
    wzjpegInitSignContextTable(session);
    result = wzjpegProcessStream(session);
    result =
        result && !session->input.bReadError && !session->bWriteFailed && (size_t)session->nTotalWritten == destination_size && session->input.position == source_size;
    if (result && out_written) *out_written = (size_t)session->nTotalWritten;
    for (i = 0; i < 4; ++i) xx_mem_free(session->pBlocks[i]);
    xx_mem_free(session->metadata);
    xx_mem_free(session);
    return result;
}
