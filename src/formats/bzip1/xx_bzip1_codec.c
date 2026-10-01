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
/* C port of XArchive/Algos/xbzip1decoder.cpp. */
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define BZIP1_SMALL_B (26)
#define BZIP1_R_INITIAL (1U << (BZIP1_SMALL_B - 1))
#define BZIP1_R_FLOOR (1U << (BZIP1_SMALL_B - 2))

#define BZIP1_MAX_MODEL_SYMBOLS (256)

// The coder's flush is always inside the file, so a real stream never needs a
// bit past EOF.  A truncated one does, and this cap stops it spinning; the
// trailing CRC turns the truncation into a clean failure either way.
#define BZIP1_MAX_PAD_BITS (64)

#define BZIP1_SPOT_BASIS_STEP (8000)

// Every stream's final block ends with this one extra byte, which marks
// end-of-input and is never written out.  A wrong value means the block was
// mis-decoded, so it is checked.
#define BZIP1_RLE_SENTINEL (42U)

// Values getMTFVal() can return outside the 1..255 move-to-front range.
#define BZIP1_SYM_RUNA (257)
#define BZIP1_SYM_RUNB (258)
#define BZIP1_SYM_EOB (259)

#define BZIP1_MODEL_COUNT (8)
#define BZIP1_OUTPUT_FLUSH_SIZE (0x10000)

typedef struct Bzip1Model {
    uint32_t nTotFreq;
    uint32_t nNumSymbols;
    uint32_t nIncValue;
    uint32_t nNoExceed;
    uint32_t freq[BZIP1_MAX_MODEL_SYMBOLS + 2];
} Bzip1Model;

void bzip1InitModel(Bzip1Model *pModel, uint32_t nNumSymbols, uint32_t nIncValue, uint32_t nNoExceed)
{
    const uint32_t nInitial = (nIncValue == 0) ? 1U : nIncValue;

    pModel->nNumSymbols = nNumSymbols;
    pModel->nIncValue = nIncValue;
    pModel->nNoExceed = nNoExceed;
    pModel->nTotFreq = nNumSymbols * nInitial;

    for (uint32_t i = 0; i <= nNumSymbols + 1; i++) {
        pModel->freq[i] = nInitial;
    }
    pModel->freq[0] = 0;
    pModel->freq[nNumSymbols + 1] = 0;
}

void bzip1UpdateModel(Bzip1Model *pModel, uint32_t nSymbol)
{
    pModel->nTotFreq += pModel->nIncValue;
    pModel->freq[nSymbol] += pModel->nIncValue;

    if (pModel->nTotFreq > pModel->nNoExceed) {
        pModel->nTotFreq = 0;
        for (uint32_t i = 1; i <= pModel->nNumSymbols; i++) {
            // The +1 before the shift is what keeps every count at 1 or above,
            // so no symbol can ever be scaled out of the alphabet.
            pModel->freq[i] = (pModel->freq[i] + 1) >> 1;
            pModel->nTotFreq += pModel->freq[i];
        }
    }
}

typedef struct Bzip1Decoder {
 const uint8_t *input;size_t input_size,input_position;
 uint8_t *output;size_t output_size,output_capacity;xx_pd_struct *pd;
 uint32_t m_nBitBuffer;int m_nBitsLive,m_nPadBits;bool m_bFailed;
 uint32_t m_nBigR,m_nBigD;
 Bzip1Model m_byteModel,m_models[BZIP1_MODEL_COUNT];
 int m_nBlockLimit,m_nLast,m_nOrigPtr;
 char *m_baMtfBlock,*m_baBlock;int32_t *m_vecNext;
} Bzip1Decoder;
static bool bz1_nextInputByte(Bzip1Decoder *ctx, uint8_t *pnByte);
static bool bz1_readBit(Bzip1Decoder *ctx, uint32_t *pnBit);
static bool bz1_readRawByte(Bzip1Decoder *ctx, uint32_t *pnByte);
static bool bz1_decodeSymbol(Bzip1Decoder *ctx, Bzip1Model *pModel, uint32_t *pnSymbol);
static bool bz1_decodeByte(Bzip1Decoder *ctx, uint32_t *pnByte);
static bool bz1_decodeUInt32(Bzip1Decoder *ctx, uint32_t *pnValue);
static void bz1_initStructuredModels(Bzip1Decoder *ctx);
static bool bz1_decodeMTFValue(Bzip1Decoder *ctx, int32_t *pnValue);
static bool bz1_decodeBlockSymbols(Bzip1Decoder *ctx, bool *pbLastBlock);
static bool bz1_undoTransform(Bzip1Decoder *ctx);
static void bz1_applySpotTransform(Bzip1Decoder *ctx);
static bool bz1_emitByte(Bzip1Decoder *ctx, uint8_t nByte);
static bool bz1_flushOutput(Bzip1Decoder *ctx);
static bool bz1_unRleAndEmit(Bzip1Decoder *ctx, bool bLastBlock);
static bool bz1_run(Bzip1Decoder *ctx);
static bool bz1_nextInputByte(Bzip1Decoder *ctx, uint8_t *pnByte) {
    if (ctx->input_position < ctx->input_size) { *pnByte=ctx->input[ctx->input_position++];return true; }
    if (ctx->m_nPadBits >= BZIP1_MAX_PAD_BITS) { ctx->m_bFailed=true;return false; }
    ctx->m_nPadBits+=8; *pnByte=0;return true;
}


static bool bz1_readBit(Bzip1Decoder *ctx, uint32_t *pnBit)
{
    if (ctx->m_bFailed) return false;

    if (ctx->m_nBitsLive == 0) {
        uint8_t nByte = 0;
        if (!bz1_nextInputByte(ctx, &nByte)) return false;
        ctx->m_nBitBuffer = nByte;
        ctx->m_nBitsLive = 8;
    }

    ctx->m_nBitsLive--;
    *pnBit = (ctx->m_nBitBuffer >> ctx->m_nBitsLive) & 1U;

    return true;
}

static bool bz1_readRawByte(Bzip1Decoder *ctx, uint32_t *pnByte)
{
    uint32_t nValue = 0;

    for (int32_t i = 0; i < 8; i++) {
        uint32_t nBit = 0;
        if (!bz1_readBit(ctx, &nBit)) return false;
        nValue = (nValue << 1) | nBit;
    }

    *pnByte = nValue;

    return true;
}

static bool bz1_decodeSymbol(Bzip1Decoder *ctx, Bzip1Model *pModel, uint32_t *pnSymbol)
{
    if (ctx->m_bFailed) return false;

    const uint32_t nTotal = pModel->nTotFreq;
    if (nTotal == 0) {
        ctx->m_bFailed = true;
        return false;
    }

    const uint32_t nScale = ctx->m_nBigR / nTotal;
    if (nScale == 0) {
        ctx->m_bFailed = true;
        return false;
    }

    const uint32_t nTarget = ((nTotal-1 < ctx->m_nBigD/nScale) ? nTotal-1 : ctx->m_nBigD/nScale);

    uint32_t nSymbol = 0;
    uint32_t nHigh = 0;
    while (nHigh <= nTarget) {
        nSymbol++;
        if (nSymbol > pModel->nNumSymbols) {
            ctx->m_bFailed = true;
            return false;
        }
        nHigh += pModel->freq[nSymbol];
    }
    const uint32_t nLow = nHigh - pModel->freq[nSymbol];

    const uint32_t nScaledLow = nScale * nLow;
    ctx->m_nBigD -= nScaledLow;

    // The top interval keeps the rounding remainder R - scale*low, which is the
    // whole reason this coder needs no division on the encode side either.
    if (nHigh < nTotal) {
        ctx->m_nBigR = nScale * (nHigh - nLow);
    } else {
        ctx->m_nBigR -= nScaledLow;
    }

    if (ctx->m_nBigR == 0) {
        ctx->m_bFailed = true;
        return false;
    }

    while (ctx->m_nBigR <= BZIP1_R_FLOOR) {
        uint32_t nBit = 0;
        if (!bz1_readBit(ctx, &nBit)) return false;
        ctx->m_nBigR <<= 1;
        ctx->m_nBigD = (ctx->m_nBigD << 1) + nBit;
    }

    bzip1UpdateModel(pModel, nSymbol);
    *pnSymbol = nSymbol;

    return true;
}

static bool bz1_decodeByte(Bzip1Decoder *ctx, uint32_t *pnByte)
{
    uint32_t nSymbol = 0;
    if (!bz1_decodeSymbol(ctx, &ctx->m_byteModel, &nSymbol)) return false;
    *pnByte = nSymbol - 1;
    return true;
}

static bool bz1_decodeUInt32(Bzip1Decoder *ctx, uint32_t *pnValue)
{
    uint32_t nValue = 0;

    for (int32_t i = 0; i < 4; i++) {
        uint32_t nByte = 0;
        if (!bz1_decodeByte(ctx, &nByte)) return false;
        nValue = (nValue << 8) | (nByte & 0xffU);
    }

    *pnValue = nValue;

    return true;
}

/*--
   Fenwick's structured model over the move-to-front alphabet: a first-level
   model of 11 symbols (RUNA, RUNB, the literal 1, six escapes naming a
   power-of-two bucket, and EOB) with one sub-model per bucket.  Rarer buckets
   get a smaller increment so they adapt more slowly.  Every one of these
   numbers is part of the bit stream definition.
--*/
static void bz1_initStructuredModels(Bzip1Decoder *ctx)
{
    bzip1InitModel(&ctx->m_models[0], 11, 12, 1000);   // first level
    bzip1InitModel(&ctx->m_models[1], 2, 4, 1000);     // 2..3
    bzip1InitModel(&ctx->m_models[2], 4, 3, 1000);     // 4..7
    bzip1InitModel(&ctx->m_models[3], 8, 3, 1000);     // 8..15
    bzip1InitModel(&ctx->m_models[4], 16, 3, 1000);    // 16..31
    bzip1InitModel(&ctx->m_models[5], 32, 3, 1000);    // 32..63
    bzip1InitModel(&ctx->m_models[6], 64, 2, 1000);    // 64..127
    bzip1InitModel(&ctx->m_models[7], 128, 1, 1000);   // 128..255
}

static bool bz1_decodeMTFValue(Bzip1Decoder *ctx, int32_t *pnValue)
{
    uint32_t nSymbol = 0;
    if (!bz1_decodeSymbol(ctx, &ctx->m_models[0], &nSymbol)) return false;

    if (nSymbol == 1) {
        *pnValue = BZIP1_SYM_RUNA;
        return true;
    }
    if (nSymbol == 2) {
        *pnValue = BZIP1_SYM_RUNB;
        return true;
    }
    if (nSymbol == 3) {
        *pnValue = 1;
        return true;
    }
    if (nSymbol == 11) {
        *pnValue = BZIP1_SYM_EOB;
        return true;
    }

    // Symbols 4..10 are escapes into the 2-3, 4-7, ... 128-255 buckets.  Both
    // the sub-model index and the bucket's base exponent are the escape minus
    // three: escape 4 selects model 1 and base 2^1 = 2, escape 10 selects model
    // 7 and base 2^7 = 128.  The sub-model then codes 1..bucketSize, so the
    // value is base + offset - 1.
    const int32_t nModelIndex = (int32_t)(nSymbol) - 3;
    if ((nModelIndex < 1) || (nModelIndex >= BZIP1_MODEL_COUNT)) {
        ctx->m_bFailed = true;
        return false;
    }

    uint32_t nOffset = 0;
    if (!bz1_decodeSymbol(ctx, &ctx->m_models[nModelIndex], &nOffset)) return false;

    *pnValue = (int32_t)((1U << (nSymbol - 3)) + nOffset - 1);

    return true;
}

static bool bz1_decodeBlockSymbols(Bzip1Decoder *ctx, bool *pbLastBlock)
{
    // The signed origPtr comes first and it is sent through the fixed byte
    // model, BEFORE the structured models are (re)initialised: the sign bit is
    // the only end-of-stream marker the format has.
    uint32_t nRawOrigPtr = 0;
    if (!bz1_decodeUInt32(ctx, &nRawOrigPtr)) return false;

    // Negated rather than sign-extended, so the magnitude is taken on the
    // unsigned value: negating 0x80000000 as a signed int would be undefined.
    *pbLastBlock = ((nRawOrigPtr & 0x80000000U) != 0);
    const uint32_t nMagnitude = (*pbLastBlock) ? ((~nRawOrigPtr) + 1U) : nRawOrigPtr;
    if ((nMagnitude == 0) || (nMagnitude > (uint32_t)(ctx->m_nBlockLimit))) {
        ctx->m_bFailed = true;
        return false;
    }
    ctx->m_nOrigPtr = (int32_t)(nMagnitude) - 1;

    bz1_initStructuredModels(ctx);

    uint8_t mtfTable[256];
    for (int32_t i = 0; i < 256; i++) {
        mtfTable[i] = (uint8_t)(i);
    }

    // The buffer stays at its full block size for the life of the decoder;
    // nCount is the only cursor, so no resize() invalidates pBlock mid-block.
    int32_t nCount = 0;
    char *pBlock = ctx->m_baMtfBlock;

    int32_t nSymbol = 0;
    if (!bz1_decodeMTFValue(ctx, &nSymbol)) return false;

    while (nSymbol != BZIP1_SYM_EOB) {
        if ((ctx->pd && xx_pd_is_stopped(ctx->pd))) {
            ctx->m_bFailed = true;
            return false;
        }

        if ((nSymbol == BZIP1_SYM_RUNA) || (nSymbol == BZIP1_SYM_RUNB)) {
            // Bijective base-2 run length: each step doubles, adds the RUNA/RUNB
            // bit and then adds one, so no run length has two encodings.
            int64_t nRunLength = 0;
            do {
                nRunLength = (nRunLength << 1) + ((nSymbol == BZIP1_SYM_RUNA) ? 1 : 0) + 1;
                if (nRunLength > ctx->m_nBlockLimit) {
                    ctx->m_bFailed = true;
                    return false;
                }
                if (!bz1_decodeMTFValue(ctx, &nSymbol)) return false;
            } while ((nSymbol == BZIP1_SYM_RUNA) || (nSymbol == BZIP1_SYM_RUNB));

            if (nRunLength > (int64_t)(ctx->m_nBlockLimit - nCount)) {
                ctx->m_bFailed = true;
                return false;
            }

            const uint8_t nRunByte = mtfTable[0];
            for (int64_t i = 0; i < nRunLength; i++) {
                pBlock[nCount] = (char)(nRunByte);
                nCount++;
            }
            continue;
        }

        if ((nSymbol < 1) || (nSymbol > 255)) {
            ctx->m_bFailed = true;
            return false;
        }
        if (nCount >= ctx->m_nBlockLimit) {
            ctx->m_bFailed = true;
            return false;
        }

        const uint8_t nByte = mtfTable[nSymbol];
        for (int32_t j = nSymbol; j > 0; j--) {
            mtfTable[j] = mtfTable[j - 1];
        }
        mtfTable[0] = nByte;

        pBlock[nCount] = (char)(nByte);
        nCount++;

        if (!bz1_decodeMTFValue(ctx, &nSymbol)) return false;
    }

    ctx->m_nLast = nCount - 1;

    // Every real block has at least the RLE end marker in it, and origPtr has to
    // address a byte that exists; both are cheap fail-closed checks on a stream
    // that is otherwise fully entropy-coded.
    if ((ctx->m_nLast < 0) || (ctx->m_nOrigPtr < 0) || (ctx->m_nOrigPtr > ctx->m_nLast)) {
        ctx->m_bFailed = true;
        return false;
    }

    return true;
}

static bool bz1_undoTransform(Bzip1Decoder *ctx)
{
    int32_t counts[256];
    for (int32_t i = 0; i < 256; i++) {
        counts[i] = 0;
    }

    const int32_t nSize = ctx->m_nLast + 1;
    const unsigned char *pMtf = (const unsigned char *)ctx->m_baMtfBlock;
    int32_t *pNext = ctx->m_vecNext;

    for (int32_t i = 0; i < nSize; i++) {
        const unsigned char nByte = pMtf[i];
        pNext[i] = counts[nByte];
        counts[nByte]++;
    }

    int32_t nSum = 0;
    for (int32_t nChar = 0; nChar < 256; nChar++) {
        nSum += counts[nChar];
        counts[nChar] = nSum - counts[nChar];
    }

    char *pOut = ctx->m_baBlock;
    int32_t nIndex = ctx->m_nOrigPtr;
    for (int32_t j = ctx->m_nLast; j >= 0; j--) {
        if ((nIndex < 0) || (nIndex >= nSize)) {
            ctx->m_bFailed = true;
            return false;
        }
        const unsigned char nByte = pMtf[nIndex];
        pOut[j] = (char)(nByte);
        nIndex = pNext[nIndex] + counts[nByte];
    }

    return true;
}

/*--
   The "spot" transform: bzip 0.21 decrements one byte in every ~8000 at a
   fixed, data-independent set of positions (the compressor incremented them
   before sorting).  It carries no information and cannot be skipped - leaving
   it out corrupts roughly one byte per 8 KB, which is exactly the kind of
   damage a CRC catches but an eyeball does not.
--*/
static void bz1_applySpotTransform(Bzip1Decoder *ctx)
{
    unsigned char *pBlock = (unsigned char *)ctx->m_baBlock;
    int32_t nPos = BZIP1_SPOT_BASIS_STEP;
    int32_t nDelta = 1;

    while (nPos < ctx->m_nLast) {
        pBlock[nPos] = (uint8_t)((pBlock[nPos] + 255U) & 0xffU);

        int32_t nNewDelta = 1;
        switch (nDelta) {
            case 1: nNewDelta = 4; break;
            case 2: nNewDelta = 6; break;
            case 3: nNewDelta = 1; break;
            case 4: nNewDelta = 5; break;
            case 5: nNewDelta = 9; break;
            case 6: nNewDelta = 7; break;
            case 7: nNewDelta = 3; break;
            case 8: nNewDelta = 8; break;
            case 9: nNewDelta = 2; break;
            default: nNewDelta = 1; break;
        }
        nDelta = nNewDelta;

        nPos += BZIP1_SPOT_BASIS_STEP + 17 * (nNewDelta - 5);
    }
}

static bool bz1_emitByte(Bzip1Decoder *ctx, uint8_t nByte) {
    if(ctx->output_size>=1073741824U) return false;
    if(ctx->output_size==ctx->output_capacity) {
        size_t capacity=ctx->output_capacity ? ctx->output_capacity*2 : 65536;
        void *new_output=xx_mem_realloc(ctx->output,capacity);
        if(!new_output)return false;ctx->output=new_output;ctx->output_capacity=capacity;
    }
    ctx->output[ctx->output_size++]=nByte;
    return true;
}


static bool bz1_flushOutput(Bzip1Decoder *ctx) { (void)ctx;return true; }


/*--
   Inverse RLE1: any run of four identical bytes is followed by one extra byte
   holding 0..255 further repeats.  On the final block the last byte is the
   sentinel and is not part of the data.
--*/
static bool bz1_unRleAndEmit(Bzip1Decoder *ctx, bool bLastBlock)
{
    const unsigned char *pBlock = (const unsigned char *)ctx->m_baBlock;
    const int32_t nLastToEmit = bLastBlock ? (ctx->m_nLast - 1) : ctx->m_nLast;

    int32_t nCount = 0;
    int32_t i = 0;
    int32_t nPrevious = 256;  // neither a byte value nor the end marker

    while (i <= nLastToEmit) {
        if ((ctx->pd && xx_pd_is_stopped(ctx->pd))) {
            ctx->m_bFailed = true;
            return false;
        }

        const int32_t nCurrent = pBlock[i];
        i++;

        if (!bz1_emitByte(ctx, (uint8_t)(nCurrent))) return false;

        if (nCurrent != nPrevious) {
            nCount = 1;
        } else {
            nCount++;
            if (nCount >= 4) {
                if (i > ctx->m_nLast) {
                    // The repeat count byte has to be inside the block.
                    ctx->m_bFailed = true;
                    return false;
                }
                const int32_t nRepeat = pBlock[i];
                for (int32_t j = 0; j < nRepeat; j++) {
                    if (!bz1_emitByte(ctx, (uint8_t)(nCurrent))) return false;
                }
                i++;
                nCount = 0;
            }
        }
        nPrevious = nCurrent;
    }

    if (bLastBlock && (pBlock[ctx->m_nLast] != BZIP1_RLE_SENTINEL)) {
        ctx->m_bFailed = true;
        return false;
    }

    return true;
}

static bool bz1_run(Bzip1Decoder *ctx)
{
    // The four header bytes are written raw, MSB-first, before the coder starts.
    uint32_t nMagic[4] = {0, 0, 0, 0};
    for (int32_t i = 0; i < 4; i++) {
        if (!bz1_readRawByte(ctx, &nMagic[i])) return false;
    }

    if ((nMagic[0] != (uint32_t)('B')) || (nMagic[1] != (uint32_t)('Z')) || (nMagic[2] != (uint32_t)('0')) || (nMagic[3] < (uint32_t)('1')) ||
        (nMagic[3] > (uint32_t)('9'))) {
        return false;
    }

    const int32_t nBlockSize100k = (int32_t)(nMagic[3] - (uint32_t)('0'));
    ctx->m_nBlockLimit = 100000 * nBlockSize100k;

    ctx->m_baMtfBlock=(char *)xx_mem_alloc((size_t)ctx->m_nBlockLimit);
    ctx->m_baBlock=(char *)xx_mem_alloc((size_t)ctx->m_nBlockLimit);
    ctx->m_vecNext=(int32_t *)xx_mem_alloc((size_t)ctx->m_nBlockLimit*sizeof(int32_t));
    if (!ctx->m_baMtfBlock || !ctx->m_baBlock || !ctx->m_vecNext) {
        return false;
    }

    // The coder is started once for the whole stream, not once per block.
    ctx->m_nBigR = BZIP1_R_INITIAL;
    ctx->m_nBigD = 0;
    for (int32_t i = 0; i < BZIP1_SMALL_B; i++) {
        uint32_t nBit = 0;
        if (!bz1_readBit(ctx, &nBit)) return false;
        ctx->m_nBigD = (ctx->m_nBigD << 1) + nBit;
    }

    bool bLastBlock = false;
    while (!bLastBlock) {
        if ((ctx->pd && xx_pd_is_stopped(ctx->pd))) return false;
        if (!bz1_decodeBlockSymbols(ctx, &bLastBlock)) return false;
        if (!bz1_undoTransform(ctx)) return false;
        bz1_applySpotTransform(ctx);
        if (!bz1_unRleAndEmit(ctx, bLastBlock)) return false;
    }

    uint32_t nStoredCrc = 0;
    if (!bz1_decodeUInt32(ctx, &nStoredCrc)) return false;

    // Fail closed.  The whole-file CRC is the only thing that certifies an
    // arithmetic decode - there is no other redundancy in the format - so a
    // mismatch returns false and never reports success.  Output is streamed, so
    // by this point some bytes may already have reached the output device; the
    // false return is the contract that tells the caller to discard them, the
    // same contract XBZIP2Decoder and every other streaming decoder here uses.
    if (nStoredCrc != xx_crc32(XX_CRC_TYPE_CRC32_BZIP2,
                               ctx->output, ctx->output_size)) return false;

    if (!bz1_flushOutput(ctx)) return false;

    return !ctx->m_bFailed;
}


bool xx_bzip1_decode(const uint8_t *input,size_t input_size,uint8_t **output,size_t *output_size,xx_pd_struct *pd) {
 Bzip1Decoder *ctx;bool ok;
 if(!output || !output_size || !input)return false;*output=NULL;*output_size=0;
 ctx=(Bzip1Decoder *)xx_mem_calloc(1,sizeof(*ctx));if(!ctx)return false;
 ctx->input=input;ctx->input_size=input_size;ctx->pd=pd;ctx->m_nBigR=BZIP1_R_INITIAL;
 bzip1InitModel(&ctx->m_byteModel,256,0,256);
 ok=bz1_run(ctx);
 if(ok){*output=ctx->output;*output_size=ctx->output_size;}else xx_mem_free(ctx->output);
 xx_mem_free(ctx->m_baMtfBlock);xx_mem_free(ctx->m_baBlock);xx_mem_free(ctx->m_vecNext);xx_mem_free(ctx);
 return ok;
}
