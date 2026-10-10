/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_die_engine_internal.h"
#include "xxfclib/data/xx_data.h"
#include "../data/xx_data_search_internal.h"

/* Large searches retain adjacent and one-byte-gap anchors in the same pass.
 * Both indexes belong to one immutable engine buffer and each holds at most
 * 512 KiB on a 64-bit build. Dense anchors revert to the native search. */
#define LITERAL_INDEX_MIN_WINDOW ((size_t)1024 * 1024)
#define LITERAL_INDEX_MAX_POSITIONS ((size_t)65536)
#define LITERAL_INDEX_BATCH_POSITIONS ((size_t)128)
#define LITERAL_STREAM_PREFIX_BYTES ((size_t)8)

typedef struct {
    unsigned char *pStorage;
    cd_i64 *pPositions;
    unsigned char (*pPrefixes)[8];
    unsigned char *pPrefixSizes;
    size_t nCapacity;
    size_t nCount;
    cd_i64 nCursor;
    int bComplete;
    int bDisabled;
} DieLiteralStreamIndex;

typedef struct {
    xx_io_device *pOwnerDevice;
    cd_i64 nOwnerSize;
    cd_i64 nOffset;
    cd_i64 nSize;
    unsigned char sAnchor[2];
    DieLiteralStreamIndex primary;
    int bWindowValid;
    int bActive;
    int bPreviousNegative;
    int bAuxForPrimary;

    cd_i64 nAuxOffset;
    cd_i64 nAuxSize;
    unsigned char sAuxAnchor[2];
    DieLiteralStreamIndex auxiliary;
    int bAuxValid;
} DieLiteralStreamCache;

struct DieLiteralSearchCache {
    const unsigned char *pOwnerData;
    cd_i64 nOwnerSize;
    const unsigned char *pData;
    size_t nSize;
    unsigned char sAnchor[2];
    size_t *pPositions;
    size_t nCount;
    size_t nCursor;
    int bPreviousNegative;
    int bActive;
    int bComplete;
    int bDisabled;
    int bAuxForPrimary;

    const unsigned char *pAuxData;
    cd_i64 nAuxOffset;
    size_t nAuxSize;
    unsigned char sAuxAnchor[2];
    size_t *pAuxPositions;
    size_t nAuxCount;
    size_t nAuxCursor;
    int bAuxComplete;
    int bAuxDisabled;
    DieLiteralStreamCache *pStream;
};

static cd_i64 literal_find_native(const unsigned char *pData, size_t nSize, const unsigned char *pNeedle, size_t nNeedleSize)
{
    return xx_data_find_bytes_buffer_optimize(pData, nSize, 0, pNeedle, nNeedleSize, NULL);
}

static cd_i64 literal_find_native_after(const unsigned char *pData, size_t nSize, const unsigned char *pNeedle, size_t nNeedleSize, size_t nCursor)
{
    cd_i64 nFound;

    if (nCursor > nSize || nNeedleSize > nSize - nCursor) {
        return -1;
    }
    nFound = literal_find_native(pData + nCursor, nSize - nCursor, pNeedle, nNeedleSize);
    return nFound < 0 ? -1 : (cd_i64)nCursor + nFound;
}

static void literal_cache_set_owner(DieLiteralSearchCache *pCache, const unsigned char *pData, cd_i64 nSize)
{
    if (pCache->pOwnerData != pData || pCache->nOwnerSize != nSize) {
        pCache->pOwnerData = pData;
        pCache->nOwnerSize = nSize;
        pCache->pData = NULL;
        pCache->pAuxData = NULL;
        pCache->nAuxCount = 0;
        pCache->nAuxCursor = 0;
        pCache->bAuxComplete = 0;
        pCache->bAuxDisabled = 0;
        pCache->bAuxForPrimary = 0;
    }
}

static void literal_aux_begin(DieLiteralSearchCache *pCache, const unsigned char *pData, size_t nSize, cd_i64 nOffset, const unsigned char *pNeedle)
{
    if (pCache->pAuxData != pData || pCache->nAuxSize != nSize || pCache->sAuxAnchor[0] != pNeedle[0] || pCache->sAuxAnchor[1] != pNeedle[1] || pCache->bAuxDisabled) {
        pCache->pAuxData = pData;
        pCache->nAuxOffset = nOffset;
        pCache->nAuxSize = nSize;
        pCache->sAuxAnchor[0] = pNeedle[0];
        pCache->sAuxAnchor[1] = pNeedle[1];
        pCache->nAuxCount = 0;
        pCache->nAuxCursor = 0;
        pCache->bAuxComplete = 0;
        pCache->bAuxDisabled = 0;
    }
    pCache->bAuxForPrimary = 1;
}

static void literal_aux_record(DieLiteralSearchCache *pCache, const struct XXDataLiteralDualBatch *pBatch)
{
    size_t i;
    size_t nCovered = pCache->nAuxCursor;

    for (i = 0; i < pBatch->skip_count; ++i) {
        size_t nPosition = pBatch->skip[i];
        /* A primary hit can resume inside an already inspected SIMD batch.
         * Auxiliary coverage advances independently, keeping positions unique. */
        if (nPosition < nCovered) continue;
        if (pCache->nAuxCount == LITERAL_INDEX_MAX_POSITIONS) {
            pCache->bAuxDisabled = 1;
            pCache->bAuxComplete = 0;
            return;
        }
        if (!pCache->pAuxPositions) {
            pCache->pAuxPositions = (size_t *)x_malloc(LITERAL_INDEX_MAX_POSITIONS * sizeof(size_t));
        }
        if (!pCache->pAuxPositions) {
            pCache->bAuxDisabled = 1;
            pCache->bAuxComplete = 0;
            return;
        }
        pCache->pAuxPositions[pCache->nAuxCount++] = nPosition;
    }
    if (pBatch->next > pCache->nAuxCursor) {
        pCache->nAuxCursor = pBatch->next;
        if (pCache->nAuxCursor > pCache->nAuxSize - 2) {
            pCache->nAuxCursor = pCache->nAuxSize - 2;
        }
    }
    if (pCache->nAuxCursor == pCache->nAuxSize - 2) {
        pCache->bAuxComplete = 1;
    }
}

/* Reuse only proved raw-anchor coverage. The native head/tail include all
 * starts outside it, including full needles crossing either byte boundary. */
static int literal_aux_find(DieLiteralSearchCache *pCache, cd_i64 nOffset, size_t nSize, const unsigned char *pNeedle, size_t nNeedleSize, cd_i64 *pFound)
{
    cd_i64 nEnd = nOffset + (cd_i64)nSize;
    cd_i64 nLastStart = nEnd - (cd_i64)nNeedleSize;
    cd_i64 nAuxEnd;
    cd_i64 nTailStart;
    size_t nStart;
    size_t nLow;
    size_t nHigh;
    size_t i;
    cd_i64 nFound;

    if (!pCache->bAuxComplete || pCache->bAuxDisabled || nNeedleSize < 3 || pCache->sAuxAnchor[0] != pNeedle[0] || pCache->sAuxAnchor[1] != pNeedle[2]) return 0;
    nAuxEnd = pCache->nAuxOffset + (cd_i64)pCache->nAuxSize;
    if (nOffset > nAuxEnd - 3 || nLastStart < pCache->nAuxOffset) return 0;

    if (nOffset < pCache->nAuxOffset) {
        size_t nHeadSize = (size_t)(pCache->nAuxOffset - nOffset);
        size_t nOverlap = nSize - nHeadSize;
        if (nOverlap > nNeedleSize - 1) nOverlap = nNeedleSize - 1;
        nHeadSize += nOverlap;
        nFound = literal_find_native(pCache->pOwnerData + nOffset, nHeadSize, pNeedle, nNeedleSize);
        if (nFound >= 0) {
            *pFound = nOffset + nFound;
            return 1;
        }
    }
    nStart = nOffset > pCache->nAuxOffset ? (size_t)(nOffset - pCache->nAuxOffset) : 0;
    nLow = 0;
    nHigh = pCache->nAuxCount;
    while (nLow < nHigh) {
        size_t nMiddle = nLow + (nHigh - nLow) / 2;
        if (pCache->pAuxPositions[nMiddle] < nStart) nLow = nMiddle + 1;
        else nHigh = nMiddle;
    }
    for (i = nLow; i < pCache->nAuxCount; ++i) {
        cd_i64 nPosition = pCache->nAuxOffset + (cd_i64)pCache->pAuxPositions[i];
        if (nPosition > nLastStart) break;
        if (!x_memcmp(pCache->pOwnerData + nPosition, pNeedle, nNeedleSize)) {
            *pFound = nPosition;
            return 1;
        }
    }
    nTailStart = nAuxEnd - 2;
    if (nTailStart < nOffset) nTailStart = nOffset;
    if (nTailStart <= nLastStart) {
        nFound = literal_find_native(pCache->pOwnerData + nTailStart, (size_t)(nEnd - nTailStart), pNeedle, nNeedleSize);
        *pFound = nFound < 0 ? -1 : nTailStart + nFound;
    } else {
        *pFound = -1;
    }
    return 1;
}

static void literal_cache_set_window(DieLiteralSearchCache *pCache, const unsigned char *pData, size_t nSize, const unsigned char *pNeedle)
{
    pCache->pData = pData;
    pCache->nSize = nSize;
    pCache->sAnchor[0] = pNeedle[0];
    pCache->sAnchor[1] = pNeedle[1];
    pCache->nCount = 0;
    pCache->nCursor = 0;
    pCache->bPreviousNegative = 0;
    pCache->bActive = 0;
    pCache->bComplete = 0;
    pCache->bDisabled = 0;
    pCache->bAuxForPrimary = 0;
}

static cd_i64 literal_find_indexed(DieLiteralSearchCache *pCache, const unsigned char *pData, size_t nSize, const unsigned char *pNeedle, size_t nNeedleSize,
                                   cd_i64 nWindowOffset)
{
    size_t i;
    struct XXDataLiteralDualBatch sBatch;
    size_t *sPositions = sBatch.adjacent;
    cd_i64 nFound;

    if (pCache->pData != pData || pCache->nSize != nSize || pCache->sAnchor[0] != pNeedle[0] || pCache->sAnchor[1] != pNeedle[1]) {
        literal_cache_set_window(pCache, pData, nSize, pNeedle);
    }

    /* Fuse the first pass only when there are enough legal needle starts and
     * the native SIMD filter already represents this two-byte prefix. Other
     * searches retain the lazy second-query path. */
    if (pCache->bDisabled || (!pCache->bActive && !pCache->bPreviousNegative &&
                              (nSize - nNeedleSize < LITERAL_INDEX_MIN_WINDOW - 1 || !xx_data_can_fuse_literal_prefix(pNeedle, nNeedleSize)))) {
        nFound = literal_find_native(pData, nSize, pNeedle, nNeedleSize);
        if (nFound < 0) {
            pCache->bPreviousNegative = 1;
        }
        return nFound;
    }

    /* Positions are ordered, include overlapping anchors, and are relative
     * to this exact window. A complete needle must fit within the window. */
    for (i = 0; i < pCache->nCount; ++i) {
        size_t nPosition = pCache->pPositions[i];
        if (nNeedleSize <= nSize - nPosition && !x_memcmp(pData + nPosition, pNeedle, nNeedleSize)) {
            return (cd_i64)nPosition;
        }
    }
    if (pCache->bComplete) {
        return -1;
    }

    /* A new auxiliary index must cover starts from zero. The primary cursor
     * may already have advanced before a CPU-feature change enables fusion. */
    if (!pCache->bAuxForPrimary && pCache->nCursor == 0 && nSize - nNeedleSize >= LITERAL_INDEX_MIN_WINDOW - 1 && xx_data_can_fuse_literal_prefix(pNeedle, nNeedleSize)) {
        literal_aux_begin(pCache, pData, nSize, nWindowOffset, pNeedle);
    }

    while (pCache->nCursor <= nSize && 2 <= nSize - pCache->nCursor) {
        size_t nNext = pCache->nCursor;
        size_t nCount;
        int bAny;
        int bDual = pCache->bAuxForPrimary && !pCache->bAuxDisabled && !pCache->bAuxComplete;

        if (bDual) {
            bAny = xx_data_collect_literal_dual_buffer(pData, nSize, pCache->nCursor, pCache->sAnchor, &sBatch) ? 1 : 0;
            nCount = sBatch.adjacent_count;
            nNext = sBatch.next;
            if (!pCache->bActive && nCount && nNeedleSize <= nSize - sPositions[0] && !x_memcmp(pData + sPositions[0], pNeedle, nNeedleSize)) {
                pCache->nCursor = 0;
                return (cd_i64)sPositions[0];
            }
            literal_aux_record(pCache, &sBatch);
        } else {
            nCount = xx_data_collect_prefixes_buffer(pData, nSize, pCache->nCursor, pCache->sAnchor, sPositions, LITERAL_INDEX_BATCH_POSITIONS, &nNext);
            bAny = nCount != 0;
        }

        if (!bAny) {
            pCache->nCursor = nNext;
            pCache->bActive = 1;
            pCache->bComplete = 1;
            return -1;
        }
        for (i = 0; i < nCount; ++i) {
            size_t nPosition = sPositions[i];
            int bMatch = nNeedleSize <= nSize - nPosition && !x_memcmp(pData + nPosition, pNeedle, nNeedleSize);

            /* An initial positive needs no primary index allocation.
             * Keep the cursor at zero so a later query can still start fresh. */
            if (!pCache->bActive && bMatch) {
                pCache->nCursor = 0;
                return (cd_i64)nPosition;
            }
            if (!pCache->bActive) {
                if (!pCache->pPositions) {
                    pCache->pPositions = (size_t *)x_malloc(LITERAL_INDEX_MAX_POSITIONS * sizeof(size_t));
                }
                if (!pCache->pPositions) {
                    pCache->bDisabled = 1;
                    return literal_find_native_after(pData, nSize, pNeedle, nNeedleSize, nPosition + 1);
                }
                pCache->bActive = 1;
            }
            if (pCache->nCount == LITERAL_INDEX_MAX_POSITIONS) {
                /* Every preceding anchor was checked for this query. On
                 * overflow continue after this one, avoiding a repeated pass.
                 * Future queries use the native finder from the beginning. */
                pCache->bDisabled = 1;
                return bMatch ? (cd_i64)nPosition : literal_find_native_after(pData, nSize, pNeedle, nNeedleSize, nPosition + 1);
            }
            pCache->pPositions[pCache->nCount++] = nPosition;
            pCache->nCursor = nPosition + 1;
            if (bMatch) {
                return (cd_i64)nPosition;
            }
        }
        /* The collector also covered any gap after the last saved anchor. */
        pCache->nCursor = nNext;
    }
    pCache->bActive = 1;
    pCache->bComplete = 1;
    return -1;
}

/* File-backed searches borrow bounded xxio windows. No pointer to a borrowed
 * window survives a reader call: candidate batches and their first eight
 * bytes are copied before verification can refill the device cache. */
static int literal_stream_stopped(DieEngine *pEngine)
{
    return pEngine->bStop || die_file_read_failed(&pEngine->file);
}

static const unsigned char *literal_stream_window(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, unsigned char sBridge[3], size_t *pnSize)
{
    const unsigned char *pData;
    size_t nCapacity = die_file_buffer_size(&pEngine->file);
    size_t nRequest;

    *pnSize = 0;
    if (nSize <= 0 || !nCapacity || literal_stream_stopped(pEngine)) return NULL;
    nRequest = (cd_u64)nSize > (cd_u64)nCapacity ? nCapacity : (size_t)nSize;
    *pnSize = nRequest;
    pData = die_file_window(&pEngine->file, nOffset, pnSize);
    if (!pData || !*pnSize || *pnSize > nRequest || literal_stream_stopped(pEngine)) {
        *pnSize = 0;
        return NULL;
    }
    /* Three bytes are the dual anchor's semantic lookahead, even when the
     * captured file capacity is one/two bytes. read_at splits physical I/O
     * at that capacity; this fixed bridge never grows with file or needle. */
    if (*pnSize < 3 && nSize > (cd_i64)*pnSize) {
        size_t nBridge = nSize < 3 ? (size_t)nSize : 3;
        if (!die_file_read_at(&pEngine->file, nOffset, sBridge, nBridge) || literal_stream_stopped(pEngine)) {
            *pnSize = 0;
            return NULL;
        }
        *pnSize = nBridge;
        pData = sBridge;
    }
    return pData;
}

/* The caller has proved that the whole needle fits its logical query. Even
 * a needle larger than the file-buffer capacity is compared piecewise. */
static int literal_stream_compare(DieEngine *pEngine, cd_i64 nOffset, const unsigned char *pNeedle, size_t nNeedleSize)
{
    size_t nDone = 0;
    size_t nCapacity = die_file_buffer_size(&pEngine->file);
    if (!nCapacity) return -1;
    while (nDone < nNeedleSize) {
        const unsigned char *pData;
        size_t nRequest = nNeedleSize - nDone;
        size_t nGot;

        if (literal_stream_stopped(pEngine)) return -1;
        if (nRequest > nCapacity) nRequest = nCapacity;
        nGot = nRequest;
        pData = die_file_window(&pEngine->file, nOffset + (cd_i64)nDone, &nGot);
        if (!pData || !nGot || nGot > nRequest || literal_stream_stopped(pEngine)) return -1;
        if (x_memcmp(pData, pNeedle + nDone, nGot)) return 0;
        nDone += nGot;
    }
    return 1;
}

static unsigned int literal_stream_byte_weight(unsigned char nByte)
{
    if (nByte == 0) return 255;
    if (nByte == 255) return 220;
    if (nByte == ' ') return 180;
    if ((nByte >= 'a' && nByte <= 'z') || (nByte >= 'A' && nByte <= 'Z')) return 100;
    if (nByte >= '0' && nByte <= '9') return 120;
    return 50;
}

/* A fixed two-byte filter avoids allocating needle-sized overlap storage.
 * It is used when a needle does not comfortably fit the available view. */
static cd_i64 literal_stream_pair_find(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, size_t nNeedleSize)
{
    size_t nAnchor = 0;
    unsigned int nWeight = 511;
    size_t i;
    cd_i64 nCursor = 0;
    cd_i64 nLastStart = nSize - (cd_i64)nNeedleSize;
    cd_i64 nAnchorSize = nLastStart + 2;

    for (i = 0; i + 1 < nNeedleSize; ++i) {
        unsigned int nCurrent;
        if ((i & 4095) == 0 && literal_stream_stopped(pEngine)) return -1;
        nCurrent = literal_stream_byte_weight(pNeedle[i]) + literal_stream_byte_weight(pNeedle[i + 1]);
        if (nCurrent < nWeight) {
            nWeight = nCurrent;
            nAnchor = i;
        }
    }
    while (nCursor <= nAnchorSize - 2) {
        unsigned char sBridge[3];
        size_t nViewSize;
        const unsigned char *pData = literal_stream_window(pEngine, nOffset + (cd_i64)nAnchor + nCursor, nAnchorSize - nCursor, sBridge, &nViewSize);
        cd_i64 nFound;
        int nMatch;

        if (!pData || nViewSize < 2) return -1;
        nFound = literal_find_native(pData, nViewSize, pNeedle + nAnchor, 2);
        if (literal_stream_stopped(pEngine)) return -1;
        if (nFound < 0) {
            nCursor += (cd_i64)nViewSize - 1;
            continue;
        }
        nCursor += nFound;
        nMatch = literal_stream_compare(pEngine, nOffset + nCursor, pNeedle, nNeedleSize);
        if (nMatch < 0) return -1;
        if (nMatch) return nOffset + nCursor;
        ++nCursor;
    }
    return -1;
}

static cd_i64 literal_stream_native(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, size_t nNeedleSize)
{
    cd_i64 nCursor = 0;
    cd_i64 nLastStart = nSize - (cd_i64)nNeedleSize;

    while (nCursor <= nLastStart) {
        unsigned char sBridge[3];
        size_t nViewSize;
        const unsigned char *pData = literal_stream_window(pEngine, nOffset + nCursor, nSize - nCursor, sBridge, &nViewSize);
        cd_i64 nFound;

        if (!pData) return -1;
        if (nNeedleSize >= 2 && nNeedleSize > nViewSize / 2) {
            return literal_stream_pair_find(pEngine, nOffset + nCursor, nSize - nCursor, pNeedle, nNeedleSize);
        }
        if (nNeedleSize > nViewSize) return -1;
        nFound = literal_find_native(pData, nViewSize, pNeedle, nNeedleSize);
        if (literal_stream_stopped(pEngine)) return -1;
        if (nFound >= 0) return nOffset + nCursor + nFound;
        nCursor += (cd_i64)(nViewSize - nNeedleSize + 1);
    }
    return -1;
}

static void literal_stream_index_reset(DieLiteralStreamIndex *pIndex)
{
    pIndex->nCount = 0;
    pIndex->nCursor = 0;
    pIndex->bComplete = 0;
    pIndex->bDisabled = 0;
}

/* These arrays hold anchor offsets and fixed fingerprints, not file-copy
 * buffers. Their entry budget follows the captured file capacity (one entry
 * per capacity byte, capped at 65536); the 64KiB default preserves its budget.
 * A different owner can have a different budget, so release stale storage. */
static void literal_stream_index_set_capacity(DieLiteralStreamIndex *pIndex, size_t nCapacity)
{
    if (!nCapacity) nCapacity = 1;
    if (nCapacity > LITERAL_INDEX_MAX_POSITIONS) nCapacity = LITERAL_INDEX_MAX_POSITIONS;
    if (pIndex->nCapacity != nCapacity) {
        x_free(pIndex->pStorage);
        pIndex->pStorage = NULL;
        pIndex->pPositions = NULL;
        pIndex->pPrefixes = NULL;
        pIndex->pPrefixSizes = NULL;
        pIndex->nCapacity = nCapacity;
        literal_stream_index_reset(pIndex);
    }
}

static void literal_stream_abort(DieLiteralStreamCache *pCache)
{
    literal_stream_index_reset(&pCache->primary);
    literal_stream_index_reset(&pCache->auxiliary);
    pCache->bWindowValid = 0;
    pCache->bAuxValid = 0;
    pCache->bActive = 0;
    pCache->bPreviousNegative = 0;
    pCache->bAuxForPrimary = 0;
}

static int literal_stream_append(DieLiteralStreamIndex *pIndex, cd_i64 nPosition, const unsigned char sPrefix[8], unsigned char nPrefixSize)
{
    if (pIndex->nCount == pIndex->nCapacity) {
        pIndex->bDisabled = 1;
        pIndex->bComplete = 0;
        return 0;
    }
    if (!pIndex->pStorage) {
        size_t nPositionsBytes = pIndex->nCapacity * sizeof(cd_i64);
        size_t nPrefixesBytes = pIndex->nCapacity * LITERAL_STREAM_PREFIX_BYTES;

        pIndex->pStorage = (unsigned char *)x_malloc(nPositionsBytes + nPrefixesBytes + pIndex->nCapacity);
        if (!pIndex->pStorage) {
            pIndex->bDisabled = 1;
            return 0;
        }
        pIndex->pPositions = (cd_i64 *)pIndex->pStorage;
        pIndex->pPrefixes = (unsigned char(*)[8])(pIndex->pStorage + nPositionsBytes);
        pIndex->pPrefixSizes = pIndex->pStorage + nPositionsBytes + nPrefixesBytes;
    }
    pIndex->pPositions[pIndex->nCount] = nPosition;
    pIndex->pPrefixSizes[pIndex->nCount] = nPrefixSize;
    x_memcpy(pIndex->pPrefixes[pIndex->nCount], sPrefix, nPrefixSize);
    ++pIndex->nCount;
    return 1;
}

static int literal_stream_entry_match(DieEngine *pEngine, const DieLiteralStreamIndex *pIndex, size_t nEntry, cd_i64 nPosition, const unsigned char *pNeedle,
                                      size_t nNeedleSize)
{
    size_t nSaved = pIndex->pPrefixSizes[nEntry];
    size_t nCompare = nSaved < nNeedleSize ? nSaved : nNeedleSize;

    if (literal_stream_stopped(pEngine)) return -1;
    if (x_memcmp(pIndex->pPrefixes[nEntry], pNeedle, nCompare)) return 0;
    if (nNeedleSize <= nSaved) return 1;
    return literal_stream_compare(pEngine, nPosition + (cd_i64)nSaved, pNeedle + nSaved, nNeedleSize - nSaved);
}

/* Capture bytes before another reader call can invalidate the borrowed view.
 * Positions at unproved chunk-edge starts are removed from both channels. */
static size_t literal_stream_capture(const unsigned char *pData, size_t nViewSize, size_t nSafeStarts, size_t *pPositions, size_t nCount, unsigned char sPrefixes[128][8],
                                     unsigned char sSizes[128])
{
    size_t i;
    size_t nKept = 0;
    for (i = 0; i < nCount; ++i) {
        size_t nPosition = pPositions[i];
        size_t nSaved;
        if (nPosition >= nSafeStarts) continue;
        nSaved = nViewSize - nPosition;
        if (nSaved > LITERAL_STREAM_PREFIX_BYTES) nSaved = LITERAL_STREAM_PREFIX_BYTES;
        pPositions[nKept] = nPosition;
        sSizes[nKept] = (unsigned char)nSaved;
        x_memcpy(sPrefixes[nKept], pData + nPosition, nSaved);
        ++nKept;
    }
    return nKept;
}

static int literal_stream_aux_find(DieEngine *pEngine, DieLiteralStreamCache *pCache, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, size_t nNeedleSize,
                                   cd_i64 *pFound)
{
    cd_i64 nEnd = nOffset + nSize;
    cd_i64 nLastStart = nEnd - (cd_i64)nNeedleSize;
    cd_i64 nAuxEnd;
    cd_i64 nStart;
    cd_i64 nTail;
    size_t nLow = 0;
    size_t nHigh;
    size_t i;

    if (!pCache->bAuxValid || !pCache->auxiliary.bComplete || pCache->auxiliary.bDisabled || nNeedleSize < 3 || pCache->sAuxAnchor[0] != pNeedle[0] ||
        pCache->sAuxAnchor[1] != pNeedle[2])
        return 0;
    nAuxEnd = pCache->nAuxOffset + pCache->nAuxSize;
    if (nOffset > nAuxEnd - 3 || nLastStart < pCache->nAuxOffset) return 0;
    if (nOffset < pCache->nAuxOffset) {
        cd_i64 nHeadSize = pCache->nAuxOffset - nOffset;
        cd_i64 nOverlap = nSize - nHeadSize;
        cd_i64 nFound;

        if ((cd_u64)nOverlap > (cd_u64)nNeedleSize - 1) nOverlap = (cd_i64)nNeedleSize - 1;
        nFound = literal_stream_native(pEngine, nOffset, nHeadSize + nOverlap, pNeedle, nNeedleSize);
        if (literal_stream_stopped(pEngine)) {
            *pFound = -1;
            return 1;
        }
        if (nFound >= 0) {
            *pFound = nFound;
            return 1;
        }
    }
    nStart = nOffset > pCache->nAuxOffset ? nOffset - pCache->nAuxOffset : 0;
    nHigh = pCache->auxiliary.nCount;
    while (nLow < nHigh) {
        size_t nMiddle = nLow + (nHigh - nLow) / 2;
        if (pCache->auxiliary.pPositions[nMiddle] < nStart) nLow = nMiddle + 1;
        else nHigh = nMiddle;
    }
    for (i = nLow; i < pCache->auxiliary.nCount; ++i) {
        cd_i64 nPosition = pCache->nAuxOffset + pCache->auxiliary.pPositions[i];
        int nMatch;
        if (nPosition > nLastStart) break;
        nMatch = literal_stream_entry_match(pEngine, &pCache->auxiliary, i, nPosition, pNeedle, nNeedleSize);
        if (nMatch < 0) {
            *pFound = -1;
            return 1;
        }
        if (nMatch) {
            *pFound = nPosition;
            return 1;
        }
    }
    nTail = nAuxEnd - 2;
    if (nTail < nOffset) nTail = nOffset;
    *pFound = nTail <= nLastStart ? literal_stream_native(pEngine, nTail, nEnd - nTail, pNeedle, nNeedleSize) : -1;
    return 1;
}

static cd_i64 literal_stream_indexed(DieEngine *pEngine, DieLiteralStreamCache *pCache, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, size_t nNeedleSize)
{
    size_t i;
    cd_i64 nFound;
    int bFuse = nSize - (cd_i64)nNeedleSize >= (cd_i64)LITERAL_INDEX_MIN_WINDOW - 1 && xx_data_can_fuse_literal_prefix(pNeedle, nNeedleSize);

    if (!pCache->bWindowValid || pCache->nOffset != nOffset || pCache->nSize != nSize || pCache->sAnchor[0] != pNeedle[0] || pCache->sAnchor[1] != pNeedle[1]) {
        literal_stream_index_reset(&pCache->primary);
        pCache->nOffset = nOffset;
        pCache->nSize = nSize;
        pCache->sAnchor[0] = pNeedle[0];
        pCache->sAnchor[1] = pNeedle[1];
        pCache->bWindowValid = 1;
        pCache->bActive = 0;
        pCache->bPreviousNegative = 0;
        pCache->bAuxForPrimary = 0;
    }
    if (pCache->primary.bDisabled || (!pCache->bActive && !pCache->bPreviousNegative && !bFuse)) {
        nFound = literal_stream_native(pEngine, nOffset, nSize, pNeedle, nNeedleSize);
        if (nFound < 0 && !literal_stream_stopped(pEngine)) pCache->bPreviousNegative = 1;
        return nFound;
    }
    for (i = 0; i < pCache->primary.nCount; ++i) {
        cd_i64 nPosition = pCache->primary.pPositions[i];
        int nMatch;
        if ((cd_i64)nNeedleSize > nSize - nPosition) break;
        nMatch = literal_stream_entry_match(pEngine, &pCache->primary, i, nOffset + nPosition, pNeedle, nNeedleSize);
        if (nMatch < 0) return -1;
        if (nMatch) return nOffset + nPosition;
    }
    if (pCache->primary.bComplete) return -1;
    if (!pCache->bAuxForPrimary && pCache->primary.nCursor == 0 && bFuse) {
        if (!pCache->bAuxValid || pCache->nAuxOffset != nOffset || pCache->nAuxSize != nSize || pCache->sAuxAnchor[0] != pNeedle[0] ||
            pCache->sAuxAnchor[1] != pNeedle[1] || pCache->auxiliary.bDisabled) {
            literal_stream_index_reset(&pCache->auxiliary);
            pCache->nAuxOffset = nOffset;
            pCache->nAuxSize = nSize;
            pCache->sAuxAnchor[0] = pNeedle[0];
            pCache->sAuxAnchor[1] = pNeedle[1];
            pCache->bAuxValid = 1;
        }
        pCache->bAuxForPrimary = 1;
    }
    while (pCache->primary.nCursor <= nSize - 2) {
        /* Fixed SIMD-collector ABI scratch and eight-byte fingerprints are
         * algorithm metadata; no bulk file-copy allocation occurs here. */
        struct XXDataLiteralDualBatch sBatch;
        unsigned char sPrimaryPrefixes[128][8], sAuxPrefixes[128][8];
        unsigned char sPrimarySizes[128], sAuxSizes[128];
        unsigned char sBridge[3];
        cd_i64 nBase = pCache->primary.nCursor;
        size_t nViewSize, nSafeStarts, nNext, nCount, nAuxCount = 0;
        int bDual = pCache->bAuxForPrimary && !pCache->auxiliary.bDisabled && !pCache->auxiliary.bComplete;
        const unsigned char *pData = literal_stream_window(pEngine, nOffset + nBase, nSize - nBase, sBridge, &nViewSize);

        if (!pData || nViewSize < 2) return -1;
        nSafeStarts = nViewSize - ((bDual && (cd_i64)nViewSize < nSize - nBase) ? 2 : 1);
        if (bDual) {
            xx_data_collect_literal_dual_buffer(pData, nViewSize, 0, pCache->sAnchor, &sBatch);
            nCount = sBatch.adjacent_count;
            nAuxCount = sBatch.skip_count;
            nNext = sBatch.next;
        } else {
            nCount = xx_data_collect_prefixes_buffer(pData, nViewSize, 0, pCache->sAnchor, sBatch.adjacent, LITERAL_INDEX_BATCH_POSITIONS, &nNext);
        }
        if (literal_stream_stopped(pEngine)) return -1;
        if (nNext > nSafeStarts) nNext = nSafeStarts;
        if (!nNext) return -1;
        nCount = literal_stream_capture(pData, nViewSize, nSafeStarts, sBatch.adjacent, nCount, sPrimaryPrefixes, sPrimarySizes);
        if (bDual) nAuxCount = literal_stream_capture(pData, nViewSize, nSafeStarts, sBatch.skip, nAuxCount, sAuxPrefixes, sAuxSizes);
        /* Preserve the allocation-free first positive behavior. */
        if (!pCache->bActive && nCount) {
            cd_i64 nPosition = nBase + (cd_i64)sBatch.adjacent[0];
            size_t nSaved = sPrimarySizes[0];
            int nMatch = 0;
            if ((cd_i64)nNeedleSize <= nSize - nPosition) {
                size_t nCompare = nSaved < nNeedleSize ? nSaved : nNeedleSize;
                if (!x_memcmp(sPrimaryPrefixes[0], pNeedle, nCompare)) {
                    nMatch = nNeedleSize <= nSaved ? 1 : literal_stream_compare(pEngine, nOffset + nPosition + (cd_i64)nSaved, pNeedle + nSaved, nNeedleSize - nSaved);
                }
            }
            if (nMatch < 0) return -1;
            if (nMatch) {
                pCache->primary.nCursor = 0;
                return nOffset + nPosition;
            }
        }
        if (bDual) {
            cd_i64 nCovered = pCache->auxiliary.nCursor;
            for (i = 0; i < nAuxCount; ++i) {
                cd_i64 nPosition = nBase + (cd_i64)sBatch.skip[i];
                if (nPosition < nCovered) continue;
                if (!literal_stream_append(&pCache->auxiliary, nPosition, sAuxPrefixes[i], sAuxSizes[i])) break;
            }
            if (!pCache->auxiliary.bDisabled) {
                cd_i64 nCoveredEnd = nBase + (cd_i64)nNext;
                if (nCoveredEnd > pCache->nAuxSize - 2) nCoveredEnd = pCache->nAuxSize - 2;
                if (nCoveredEnd > pCache->auxiliary.nCursor) pCache->auxiliary.nCursor = nCoveredEnd;
                pCache->auxiliary.bComplete = pCache->auxiliary.nCursor == pCache->nAuxSize - 2;
            }
        }
        for (i = 0; i < nCount; ++i) {
            cd_i64 nPosition = nBase + (cd_i64)sBatch.adjacent[i];
            size_t nSaved = sPrimarySizes[i];
            int nMatch = 0;
            if (literal_stream_stopped(pEngine)) return -1;
            if ((cd_i64)nNeedleSize <= nSize - nPosition) {
                size_t nCompare = nSaved < nNeedleSize ? nSaved : nNeedleSize;
                if (!x_memcmp(sPrimaryPrefixes[i], pNeedle, nCompare)) {
                    nMatch = nNeedleSize <= nSaved ? 1 : literal_stream_compare(pEngine, nOffset + nPosition + (cd_i64)nSaved, pNeedle + nSaved, nNeedleSize - nSaved);
                }
            }
            if (nMatch < 0) return -1;
            if (!pCache->bActive && nMatch) {
                pCache->primary.nCursor = 0;
                return nOffset + nPosition;
            }
            if (!literal_stream_append(&pCache->primary, nPosition, sPrimaryPrefixes[i], sPrimarySizes[i])) {
                if (nMatch) return nOffset + nPosition;
                return nSize - nPosition - 1 >= (cd_i64)nNeedleSize ? literal_stream_native(pEngine, nOffset + nPosition + 1, nSize - nPosition - 1, pNeedle, nNeedleSize)
                                                                    : -1;
            }
            pCache->bActive = 1;
            pCache->primary.nCursor = nPosition + 1;
            if (nMatch) return nOffset + nPosition;
        }
        pCache->primary.nCursor = nBase + (cd_i64)nNext;
    }
    if (literal_stream_stopped(pEngine)) return -1;
    pCache->bActive = 1;
    pCache->primary.bComplete = 1;
    return -1;
}

static cd_i64 literal_stream_find(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, size_t nNeedleSize)
{
    DieLiteralStreamCache *pCache;
    cd_i64 nFound;

    if (literal_stream_stopped(pEngine)) return -1;
    if (nNeedleSize < 4 || nSize < (cd_i64)LITERAL_INDEX_MIN_WINDOW) {
        return literal_stream_native(pEngine, nOffset, nSize, pNeedle, nNeedleSize);
    }
    if (!pEngine->pLiteralSearchCache) pEngine->pLiteralSearchCache = (DieLiteralSearchCache *)x_calloc(1, sizeof(DieLiteralSearchCache));
    if (!pEngine->pLiteralSearchCache) {
        return literal_stream_native(pEngine, nOffset, nSize, pNeedle, nNeedleSize);
    }
    if (!pEngine->pLiteralSearchCache->pStream) pEngine->pLiteralSearchCache->pStream = (DieLiteralStreamCache *)x_calloc(1, sizeof(DieLiteralStreamCache));
    pCache = pEngine->pLiteralSearchCache->pStream;
    if (!pCache) return literal_stream_native(pEngine, nOffset, nSize, pNeedle, nNeedleSize);
    if (pCache->pOwnerDevice != pEngine->file.pDevice || pCache->nOwnerSize != pEngine->file.nSize) {
        size_t nCapacity = die_file_buffer_size(&pEngine->file);
        literal_stream_abort(pCache);
        literal_stream_index_set_capacity(&pCache->primary, nCapacity);
        literal_stream_index_set_capacity(&pCache->auxiliary, nCapacity);
        pCache->pOwnerDevice = pEngine->file.pDevice;
        pCache->nOwnerSize = pEngine->file.nSize;
    }
    if (!literal_stream_aux_find(pEngine, pCache, nOffset, nSize, pNeedle, nNeedleSize, &nFound)) {
        nFound = literal_stream_indexed(pEngine, pCache, nOffset, nSize, pNeedle, nNeedleSize);
    }
    if (literal_stream_stopped(pEngine)) literal_stream_abort(pCache);
    return nFound;
}

cd_i64 die_engine_literal_find(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, cd_i64 nNeedleSize)
{
    const unsigned char *pData;
    const unsigned char *pWhole;
    cd_i64 nFound;

    if (!pEngine) {
        return -1;
    }
    if ((!pEngine->file.pData && !pEngine->file.pDevice) || !pNeedle) {
        return -1;
    }
    if (nNeedleSize <= 0 || !die_range_clamp(&pEngine->file, nOffset, &nSize) || nNeedleSize > nSize) {
        return -1;
    }
    pWhole = die_file_whole(&pEngine->file, die_file_search_access(&pEngine->file, nSize));
    if (!pWhole) {
        if ((cd_u64)nNeedleSize > (cd_u64)SIZE_MAX) return -1;
        return literal_stream_find(pEngine, nOffset, nSize, pNeedle, (size_t)nNeedleSize);
    }
    if (nNeedleSize < 4 || nSize < (cd_i64)LITERAL_INDEX_MIN_WINDOW || (cd_u64)nSize > (cd_u64)SIZE_MAX || (cd_u64)nNeedleSize > (cd_u64)SIZE_MAX) {
        return die_find_bytes(&pEngine->file, nOffset, nSize, pNeedle, nNeedleSize);
    }
    if (!pEngine->pLiteralSearchCache) {
        pEngine->pLiteralSearchCache = (DieLiteralSearchCache *)x_calloc(1, sizeof(DieLiteralSearchCache));
    }
    if (!pEngine->pLiteralSearchCache) {
        return die_find_bytes(&pEngine->file, nOffset, nSize, pNeedle, nNeedleSize);
    }
    literal_cache_set_owner(pEngine->pLiteralSearchCache, pWhole, pEngine->file.nSize);
    if (literal_aux_find(pEngine->pLiteralSearchCache, nOffset, (size_t)nSize, pNeedle, (size_t)nNeedleSize, &nFound)) {
        return nFound;
    }
    pData = pWhole + nOffset;
    nFound = literal_find_indexed(pEngine->pLiteralSearchCache, pData, (size_t)nSize, pNeedle, (size_t)nNeedleSize, nOffset);
    return nFound < 0 ? -1 : nOffset + nFound;
}

void die_engine_literal_cache_free(DieEngine *pEngine)
{
    if (pEngine && pEngine->pLiteralSearchCache) {
        if (pEngine->pLiteralSearchCache->pStream) {
            x_free(pEngine->pLiteralSearchCache->pStream->primary.pStorage);
            x_free(pEngine->pLiteralSearchCache->pStream->auxiliary.pStorage);
            x_free(pEngine->pLiteralSearchCache->pStream);
        }
        x_free(pEngine->pLiteralSearchCache->pAuxPositions);
        x_free(pEngine->pLiteralSearchCache->pPositions);
        x_free(pEngine->pLiteralSearchCache);
        pEngine->pLiteralSearchCache = NULL;
    }
}
