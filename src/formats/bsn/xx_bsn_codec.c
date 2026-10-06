/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * C port of XArchive/Algos/xbsndecoder.cpp, retaining its solid-history semantics.
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/rt/xx_rt.h"
#define BSN_NC (510)          // literal/length alphabet (256 + 254 lengths)
#define BSN_CBIT (9)          // width of the literal-table symbol count
#define BSN_NT (19)           // pre-table alphabet
#define BSN_TBIT (5)          // width of the pre-table symbol count
#define BSN_NP (16)           // position alphabet -> 15-bit distances
#define BSN_PBIT (5)          // width of the position-table symbol count
#define BSN_THRESHOLD (3)     // shortest encodable match
#define BSN_PT_SPECIAL (3)    // pre-table index carrying the 2-bit zero run
#define BSN_MAX_CODE_LENGTH (16)
#define BSN_WINDOW_SIZE (32768)
#define BSN_WINDOW_MASK (BSN_WINDOW_SIZE - 1)
// Member sizes are u32 in the container, so this ceiling is the format's own
// limit rather than a policy choice; it only keeps the allocation below from
// being driven past what the header could ever legitimately declare.
#define BSN_MAX_OUTPUT (0xffffffffLL)
#define BSN_CANCEL_CHECK_INTERVAL (0x10000)

typedef struct BsnBitReader { const uint8_t *data;size_t size;uint64_t bit_position; } BsnBitReader;
static bool bsnReadBits(BsnBitReader *r,int count,uint32_t *value) {
 uint32_t result=0;int i;
 if(!value || count<0 || count>24 || r->bit_position>(uint64_t)r->size*8 || (uint64_t)count>(uint64_t)r->size*8-r->bit_position)return false;
 for(i=0;i<count;++i){result=(result<<1)|((r->data[r->bit_position>>3]>>(7-(r->bit_position&7)))&1);++r->bit_position;}
 *value=result;return true;
}
typedef struct BsnTree
{
    uint32_t nCount[BSN_MAX_CODE_LENGTH + 1];
    uint32_t nFirstCode[BSN_MAX_CODE_LENGTH + 1];
    int32_t nFirstIndex[BSN_MAX_CODE_LENGTH + 1];
    uint16_t vSymbols[BSN_NC];
    int32_t symbol_count;
    int32_t nMaxLength;
    bool bConstant;
    uint32_t nConstantSymbol;
} BsnTree;

void bsnResetTree(BsnTree *pTree)
{
    for (int32_t i = 0; i <= BSN_MAX_CODE_LENGTH; ++i) {
        pTree->nCount[i] = 0;
        pTree->nFirstCode[i] = 0;
        pTree->nFirstIndex[i] = 0;
    }
    pTree->symbol_count=0;
    pTree->nMaxLength = 0;
    pTree->bConstant = false;
    pTree->nConstantSymbol = 0;
}

// Every table this format emits is Kraft-complete (verified over 1292 blocks
// of the reference corpus).  Rejecting incomplete and over-subscribed tables
// therefore costs nothing on genuine input and turns a mis-parse into an
// immediate failure instead of a plausible-looking wrong plaintext.
bool bsnBuildTree(const uint8_t *vLengths, int32_t length_count, BsnTree *pTree)
{
    bsnResetTree(pTree);

    int32_t nMaxLength = 0;
    for (int32_t i = 0; i < length_count; ++i) {
        const uint8_t nLength = vLengths[i];
        if (nLength > BSN_MAX_CODE_LENGTH) return false;
        if (nLength > 0) {
            ++pTree->nCount[nLength];
            if (nLength > nMaxLength) nMaxLength = nLength;
        }
    }
    if (nMaxLength == 0) return false;
    pTree->nMaxLength = nMaxLength;

    int64_t nLeft = 1;
    uint32_t nCode = 0;
    int32_t nIndex = 0;
    for (int32_t nLength = 1; nLength <= nMaxLength; ++nLength) {
        nLeft = (nLeft << 1) - pTree->nCount[nLength];
        if (nLeft < 0) return false;
        pTree->nFirstCode[nLength] = nCode;
        pTree->nFirstIndex[nLength] = nIndex;
        nCode = (nCode + pTree->nCount[nLength]) << 1;
        nIndex += (int32_t)(pTree->nCount[nLength]);
    }
    if (nLeft != 0) return false;

    if(nIndex>BSN_NC) {return false; } pTree->symbol_count=nIndex;
    int32_t vNext[BSN_MAX_CODE_LENGTH + 1]={0};
    for (int32_t nLength = 1; nLength <= nMaxLength; ++nLength) {
        vNext[nLength] = pTree->nFirstIndex[nLength];
    }
    for (int32_t i = 0; i < length_count; ++i) {
        const uint8_t nLength = vLengths[i];
        if (nLength > 0) pTree->vSymbols[vNext[nLength]++] = (uint16_t)(i);
    }
    return true;
}

bool bsnDecodeSymbol(BsnBitReader *pReader, const BsnTree *tree,
                     uint32_t *pSymbol)
{
    if (!pSymbol) return false;
    if (tree->bConstant) {
        *pSymbol = tree->nConstantSymbol;
        return true;
    }
    uint32_t nCode = 0;
    for (int32_t nLength = 1; nLength <= tree->nMaxLength; ++nLength) {
        uint32_t nBit = 0;
        if (!bsnReadBits(pReader, 1, &nBit)) return false;
        nCode = (nCode << 1) | nBit;
        if (tree->nCount[nLength] &&
            (nCode >= tree->nFirstCode[nLength]) &&
            (nCode - tree->nFirstCode[nLength] < tree->nCount[nLength])) {
            *pSymbol = tree->vSymbols[tree->nFirstIndex[nLength] + (int32_t)(nCode - tree->nFirstCode[nLength])];
            return true;
        }
    }
    return false;
}

// Pre-table / position-table reader.  The 3-bit length escape at value 7 is
// extended by counting following 1-bits AND consuming the terminating 0 bit
// (LHA's fillbuf(c - 3)).  Dropping that consume yields Kraft-invalid tables
// on about two thirds of the members while the short ones still decode
// perfectly - a very convincing partial success, so it is called out here.
bool bsnReadPtLen(BsnBitReader *pReader, int32_t nAlphabet, int32_t nCountBits,
                  int32_t nSpecialIndex, BsnTree *pTree)
{
    bsnResetTree(pTree);

    uint32_t nNumber = 0;
    if (!bsnReadBits(pReader, nCountBits, &nNumber)) return false;
    if (nNumber == 0) {
        uint32_t nSymbol = 0;
        if (!bsnReadBits(pReader, nCountBits, &nSymbol) ||
            (nSymbol >= (uint32_t)(nAlphabet))) {
            return false;
        }
        pTree->bConstant = true;
        pTree->nConstantSymbol = nSymbol;
        return true;
    }
    if (nNumber > (uint32_t)(nAlphabet)) return false;

    uint8_t vLengths[BSN_NC]={0};
    int32_t i = 0;
    while (i < (int32_t)(nNumber)) {
        uint32_t nLength = 0;
        if (!bsnReadBits(pReader, 3, &nLength)) return false;
        if (nLength == 7) {
            for (;;) {
                uint32_t nBit = 0;
                if (!bsnReadBits(pReader, 1, &nBit)) return false;
                if (!nBit) break;
                ++nLength;
                if (nLength > (uint32_t)(BSN_MAX_CODE_LENGTH)) return false;
            }
        }
        vLengths[i++] = (uint8_t)(nLength);
        if (i == nSpecialIndex) {
            uint32_t nZeroRun = 0;
            if (!bsnReadBits(pReader, 2, &nZeroRun)) return false;
            while ((nZeroRun > 0) && (i < nAlphabet)) {
                vLengths[i++] = 0;
                --nZeroRun;
            }
        }
    }
    return bsnBuildTree(vLengths, nAlphabet, pTree);
}

bool bsnReadCLen(BsnBitReader *pReader, const BsnTree *preTree, BsnTree *pTree)
{
    bsnResetTree(pTree);

    uint32_t nNumber = 0;
    if (!bsnReadBits(pReader, BSN_CBIT, &nNumber)) return false;
    if (nNumber == 0) {
        uint32_t nSymbol = 0;
        if (!bsnReadBits(pReader, BSN_CBIT, &nSymbol) ||
            (nSymbol >= (uint32_t)(BSN_NC))) {
            return false;
        }
        pTree->bConstant = true;
        pTree->nConstantSymbol = nSymbol;
        return true;
    }
    if (nNumber > (uint32_t)(BSN_NC)) return false;

    uint8_t vLengths[BSN_NC]={0};
    int32_t i = 0;
    while (i < (int32_t)(nNumber)) {
        uint32_t nSymbol = 0;
        if (!bsnDecodeSymbol(pReader, preTree, &nSymbol)) return false;
        if (nSymbol <= 2) {
            uint32_t nZeroRun = 0;
            if (nSymbol == 0) {
                nZeroRun = 1;
            } else if (nSymbol == 1) {
                if (!bsnReadBits(pReader, 4, &nZeroRun)) return false;
                nZeroRun += 3;
            } else {
                if (!bsnReadBits(pReader, BSN_CBIT, &nZeroRun)) return false;
                nZeroRun += 20;
            }
            while ((nZeroRun > 0) && (i < (int32_t)(nNumber))) {
                vLengths[i++] = 0;
                --nZeroRun;
            }
        } else {
            if ((nSymbol - 2) > (uint32_t)(BSN_MAX_CODE_LENGTH)) return false;
            vLengths[i++] = (uint8_t)(nSymbol - 2);
        }
    }
    return bsnBuildTree(vLengths, BSN_NC, pTree);
}

bool xx_bsn_lz_decode(const uint8_t *packed,size_t packed_size,
 const uint8_t *history,size_t history_size,uint8_t *output,size_t nUncompressedSize,xx_pd_struct *pPdStruct) {
 if(!output || !packed || nUncompressedSize>1073741824U)return false;
 if(!nUncompressedSize)return packed_size==0;
    // The dictionary starts as LHA's 0x20 fill and is then overwritten, at its
    // END, by the tail of the preceding plaintext.  With w_pos parked at 0 a
    // distance d resolves to window[SIZE - d], which is history[n - d] for
    // d <= n and stays 0x20 beyond it - exactly the solid semantics the
    // corpus was validated against.
    uint8_t baWindow[BSN_WINDOW_SIZE];
    xx_rt_memset(baWindow, ' ', sizeof(baWindow));
    const int32_t nHistorySize = history_size < BSN_WINDOW_SIZE ? (int32_t)history_size : BSN_WINDOW_SIZE;
    if (nHistorySize > 0) {
        xx_rt_memcpy(baWindow + (BSN_WINDOW_SIZE - nHistorySize),
               history + (history_size - nHistorySize),
               (size_t)(nHistorySize));
    }
    uint8_t *pWindow = baWindow;
    int32_t nWindowPosition = 0;

    size_t produced=0;

    BsnBitReader reader={packed,packed_size,0};
    BsnTree preTree = {0};
    BsnTree literalTree = {0};
    BsnTree positionTree = {0};
    bsnResetTree(&preTree);
    bsnResetTree(&literalTree);
    bsnResetTree(&positionTree);

    uint32_t nBlockRemaining = 0;
    size_t nNextCancelCheck = BSN_CANCEL_CHECK_INTERVAL;
    while (produced < nUncompressedSize) {
        if (produced >= nNextCancelCheck) {
            if ((pPdStruct && xx_pd_is_stopped(pPdStruct))) return false;
            nNextCancelCheck = produced + BSN_CANCEL_CHECK_INTERVAL;
        }
        if (nBlockRemaining == 0) {
            // A member may hold several blocks; each one restates all three
            // tables in-stream, immediately after the 16-bit symbol count.
            if (!bsnReadBits(&reader, 16, &nBlockRemaining) ||
                (nBlockRemaining == 0)) {
                return false;
            }
            if (!bsnReadPtLen(&reader, BSN_NT, BSN_TBIT, BSN_PT_SPECIAL,
                              &preTree) ||
                !bsnReadCLen(&reader, &preTree, &literalTree) ||
                !bsnReadPtLen(&reader, BSN_NP, BSN_PBIT, -1, &positionTree)) {
                return false;
            }
        }
        --nBlockRemaining;

        uint32_t nSymbol = 0;
        if (!bsnDecodeSymbol(&reader, &literalTree, &nSymbol)) return false;
        if (nSymbol < 256) {
            output[produced++]=(uint8_t)nSymbol;
            pWindow[nWindowPosition] = (uint8_t)(nSymbol);
            nWindowPosition = (nWindowPosition + 1) & BSN_WINDOW_MASK;
            continue;
        }

        const int32_t nMatchLength =
            (int32_t)(nSymbol) - 256 + BSN_THRESHOLD;
        uint32_t nPositionSymbol = 0;
        if (!bsnDecodeSymbol(&reader, &positionTree, &nPositionSymbol) ||
            (nPositionSymbol >= (uint32_t)(BSN_NP))) {
            return false;
        }
        uint32_t nDistance = 0;
        if (nPositionSymbol > 0) {
            uint32_t nExtra = 0;
            if (!bsnReadBits(&reader, (int32_t)(nPositionSymbol) - 1, &nExtra)) {
                return false;
            }
            nDistance = (1U << (nPositionSymbol - 1)) + nExtra;
        }
        ++nDistance;
        // A genuine stream never overruns the declared member size, and a
        // distance can never exceed the dictionary.  Both are hard errors:
        // silently clamping either one would emit wrong bytes.
        if ((nDistance > (uint32_t)(BSN_WINDOW_SIZE)) ||
            (produced + (size_t)(nMatchLength) > nUncompressedSize)) {
            return false;
        }

        int32_t nCopyPosition =
            (nWindowPosition - (int32_t)(nDistance)) & BSN_WINDOW_MASK;
        for (int32_t i = 0; i < nMatchLength; ++i) {
            const uint8_t nByte = pWindow[nCopyPosition];
            nCopyPosition = (nCopyPosition + 1) & BSN_WINDOW_MASK;
            output[produced++]=nByte;
            pWindow[nWindowPosition] = nByte;
            nWindowPosition = (nWindowPosition + 1) & BSN_WINDOW_MASK;
        }
    }

    if ((produced != nUncompressedSize) ||
        (((reader.bit_position+7)/8) > packed_size) ||
        (pPdStruct && xx_pd_is_stopped(pPdStruct))) {
        return false;
    }

    return true;
}
