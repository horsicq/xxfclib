/* Private byte adapters for the legacy DIE format shims. All returned strings
 * retain raw bytes; Latin-1 conversion remains the individual caller's rule. */
#ifndef DIE_LEGACY_FORMAT_IO_H
#define DIE_LEGACY_FORMAT_IO_H

#include "../die_engine/xx_die_engine_bin.h"

/* File-copy work uses the size captured at open, even when the global value
 * changes later. Selected decoded payloads remain complete semantic objects. */
static inline size_t xio_chunk_size(DieFile *pFile, size_t nRemaining)
{
    size_t nCapacity = die_file_buffer_size(pFile);
    return nRemaining < nCapacity ? nRemaining : nCapacity;
}

static inline int xio_match(DieFile *pFile, cd_i64 nOffset,
                             const void *pBytes, size_t nSize)
{
    const unsigned char *pExpected = (const unsigned char *)pBytes;
    if (!pFile || !pBytes || nOffset < 0 || nOffset > pFile->nSize ||
        (cd_u64)nSize > (cd_u64)(pFile->nSize - nOffset)) return 0;
    if (pFile->pData) return x_memcmp(pFile->pData + nOffset, pBytes, nSize) == 0;
    while (nSize) {
        size_t nChunk = xio_chunk_size(pFile, nSize);
        const unsigned char *pView;
        if (!nChunk) return 0;
        pView = die_file_window(pFile, nOffset, &nChunk);
        if (!pView || !nChunk || x_memcmp(pView, pExpected, nChunk)) return 0;
        nOffset += (cd_i64)nChunk;
        pExpected += nChunk;
        nSize -= nChunk;
    }
    return 1;
}

/* Each borrowed window is consumed before requesting the next one. */
static inline int xio_append(DieFile *pFile, cd_i64 nOffset,
                              size_t nSize, CDBuf *pOut)
{
    if (!pFile || nOffset < 0 || nOffset > pFile->nSize ||
        (cd_u64)nSize > (cd_u64)(pFile->nSize - nOffset)) return 0;
    while (nSize) {
        size_t nChunk = xio_chunk_size(pFile, nSize);
        if (!nChunk) return 0;
        const unsigned char *pBytes = die_file_window(pFile, nOffset, &nChunk);
        if (!pBytes || !nChunk) return 0;
        cdbuf_append(pOut, pBytes, nChunk);
        nOffset += (cd_i64)nChunk;
        nSize -= nChunk;
    }
    return 1;
}

static inline char *xio_raw_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMax)
{
    CDBuf sText;
    cd_i64 nSize = nMax;
    cdbuf_init(&sText);
    if (!pFile || !die_range_clamp(pFile, nOffset, &nSize)) return cd_strdup("");
    while (nSize > 0) {
        size_t nChunk = (cd_u64)nSize > SIZE_MAX ? SIZE_MAX : (size_t)nSize;
        size_t i;
        nChunk = xio_chunk_size(pFile, nChunk);
        if (!nChunk) { cdbuf_free(&sText); return cd_strdup(""); }
        const unsigned char *pBytes = die_file_window(pFile, nOffset, &nChunk);
        if (!pBytes || !nChunk) { cdbuf_free(&sText); return cd_strdup(""); }
        for (i = 0; i < nChunk && pBytes[i]; ++i) {}
        cdbuf_append(&sText, pBytes, i);
        if (i != nChunk) break;
        nOffset += (cd_i64)nChunk;
        nSize -= (cd_i64)nChunk;
    }
    return cdbuf_detach(&sText, NULL);
}

/* The caller owns only this selected payload, never the whole input file. */
static inline unsigned char *xio_read_owned(DieFile *pFile, cd_i64 nOffset,
                                             size_t nSize)
{
    unsigned char *pBytes;
    size_t nDone = 0;
    if (!pFile || nOffset < 0 || nOffset > pFile->nSize ||
        (cd_u64)nSize > (cd_u64)(pFile->nSize - nOffset)) return NULL;
    pBytes = (unsigned char *)cd_malloc(nSize ? nSize : 1);
    if (!pBytes) return NULL;
    while (nDone < nSize) {
        size_t nChunk = xio_chunk_size(pFile, nSize - nDone);
        if (!nChunk || !die_file_read_at(pFile, nOffset + (cd_i64)nDone,
                                       pBytes + nDone, nChunk)) {
            cd_free(pBytes);
            return NULL;
        }
        nDone += nChunk;
    }
    return pBytes;
}
#endif
