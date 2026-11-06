/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/die_engine/die_engine_db.h"
#include "xx_die_engine_db_internal.h"
#include "xx_die_engine_compat.h"

static int db_next_filter_tag(const char **ppCursor, const char **ppTag, size_t *pTagSize)
{
    const char *pCursor;
    const char *pStart;
    size_t nSize;

    if (!ppCursor || !(pCursor = *ppCursor)) return 0;
    while (*pCursor == ';' || db_is_tag_space(*pCursor)) ++pCursor;
    if (!*pCursor) {
        *ppCursor = pCursor;
        return 0;
    }
    pStart = pCursor;
    while (*pCursor && *pCursor != ';') ++pCursor;
    nSize = (size_t)(pCursor - pStart);
    db_trim_span(&pStart, &nSize);
    *ppCursor = *pCursor ? pCursor + 1 : pCursor;
    if (!nSize) return db_next_filter_tag(ppCursor, ppTag, pTagSize);
    *ppTag = pStart;
    *pTagSize = nSize;
    return 1;
}

static int db_signature_has_tag_span(const DBSignature *pSignature, const char *pTag, size_t nTagSize)
{
    const char *pCursor;
    if (!pSignature || !pSignature->pTags || !pTag || !nTagSize) return 0;
    pCursor = pSignature->pTags;
    while (*pCursor) {
        const char *pStart;
        size_t nSize;
        while (*pCursor == ';' || *pCursor == ',') ++pCursor;
        pStart = pCursor;
        while (*pCursor && *pCursor != ';' && *pCursor != ',') ++pCursor;
        nSize = (size_t)(pCursor - pStart);
        db_trim_span(&pStart, &nSize);
        if (db_ascii_span_equal(pStart, nSize, pTag, nTagSize)) return 1;
    }
    return 0;
}

int db_signature_has_tag(const DBSignature *pSignature, const char *pTag)
{
    size_t nTagSize;
    if (!pTag) return 0;
    nTagSize = xx_rt_strlen(pTag);
    db_trim_span(&pTag, &nTagSize);
    return db_signature_has_tag_span(pSignature, pTag, nTagSize);
}

int db_signature_matches_tag_filters(const DBSignature *pSignature,
                                     const char *pIncludeTags,
                                     const char *pExcludeTags)
{
    const char *pCursor;
    const char *pTag;
    size_t nTagSize;
    int bHasInclude = 0;
    int bIncludeMatch = 0;

    pCursor = pExcludeTags;
    while (db_next_filter_tag(&pCursor, &pTag, &nTagSize)) {
        if (db_signature_has_tag_span(pSignature, pTag, nTagSize)) return 0;
    }

    pCursor = pIncludeTags;
    while (db_next_filter_tag(&pCursor, &pTag, &nTagSize)) {
        bHasInclude = 1;
        if (db_signature_has_tag_span(pSignature, pTag, nTagSize)) bIncludeMatch = 1;
    }
    return !bHasInclude || bIncludeMatch;
}

typedef struct db_used_tag {
    char *pText;
    size_t nSize;
} db_used_tag;

static int db_used_tag_compare(const void *pLeft, const void *pRight)
{
    const db_used_tag *pTagLeft = (const db_used_tag *)pLeft;
    const db_used_tag *pTagRight = (const db_used_tag *)pRight;
    int nResult = cd_stricmp_ascii(pTagLeft->pText, pTagRight->pText);

    /* Case-insensitive duplicates are removed before sorting. Keep a complete
     * ordering here so the result remains deterministic if that ever changes. */
    return nResult ? nResult : x_strcmp(pTagLeft->pText, pTagRight->pText);
}

static void db_used_tags_free(db_used_tag *pTags, size_t nCount)
{
    size_t i;
    for (i = 0; i < nCount; ++i) xx_rt_free(pTags[i].pText);
    xx_rt_free(pTags);
}

static int db_collect_used_tags(db_used_tag **ppTags, size_t *pCount,
                                size_t *pCapacity, const char *pText)
{
    const char *pCursor = pText;

    if (!pCursor) return 1;
    while (*pCursor) {
        const char *pStart;
        size_t nSize;
        size_t i;

        while (*pCursor == ';' || *pCursor == ',' || db_is_tag_space(*pCursor)) ++pCursor;
        pStart = pCursor;
        while (*pCursor && *pCursor != ';' && *pCursor != ',') ++pCursor;
        nSize = (size_t)(pCursor - pStart);
        db_trim_span(&pStart, &nSize);
        if (!nSize) continue;

        for (i = 0; i < *pCount; ++i) {
            if (db_ascii_span_equal((*ppTags)[i].pText, (*ppTags)[i].nSize,
                                    pStart, nSize)) break;
        }
        if (i != *pCount) continue;

        if (*pCount == *pCapacity) {
            size_t nNewCapacity;
            db_used_tag *pNewTags;
            if (*pCapacity) {
                if (*pCapacity > SIZE_MAX / 2U) return 0;
                nNewCapacity = *pCapacity * 2U;
            } else {
                nNewCapacity = 16U;
            }
            if (nNewCapacity > SIZE_MAX / sizeof(**ppTags)) return 0;
            pNewTags = (db_used_tag *)xx_rt_realloc(
                *ppTags, nNewCapacity * sizeof(**ppTags));
            if (!pNewTags) return 0;
            *ppTags = pNewTags;
            *pCapacity = nNewCapacity;
        }
        if (nSize == SIZE_MAX) return 0;
        (*ppTags)[*pCount].pText = (char *)xx_rt_malloc(nSize + 1U);
        if (!(*ppTags)[*pCount].pText) return 0;
        x_memcpy((*ppTags)[*pCount].pText, pStart, nSize);
        (*ppTags)[*pCount].pText[nSize] = '\0';
        (*ppTags)[*pCount].nSize = nSize;
        ++*pCount;
    }
    return 1;
}

char *db_get_used_tags(const DBase *pDb)
{
    db_used_tag *pTags = NULL;
    size_t nCount = 0;
    size_t nCapacity = 0;
    size_t nResultSize = 0;
    size_t nPosition = 0;
    size_t i;
    char *pResult;

    if (pDb && pDb->pRecords && pDb->nCount > 0) {
        for (i = 0; i < (size_t)pDb->nCount; ++i) {
            if (!db_collect_used_tags(&pTags, &nCount, &nCapacity,
                                      pDb->pRecords[i].pTags)) {
                db_used_tags_free(pTags, nCount);
                return NULL;
            }
        }
    }

    if (nCount > 1U) {
        x_qsort(pTags, nCount, sizeof(*pTags), db_used_tag_compare);
    }
    for (i = 0; i < nCount; ++i) {
        size_t nSeparatorSize = i ? 1U : 0U;
        if (nResultSize > SIZE_MAX - nSeparatorSize ||
            pTags[i].nSize > SIZE_MAX - nResultSize - nSeparatorSize) {
            db_used_tags_free(pTags, nCount);
            return NULL;
        }
        nResultSize += pTags[i].nSize + nSeparatorSize;
    }

    if (nResultSize == SIZE_MAX) {
        db_used_tags_free(pTags, nCount);
        return NULL;
    }
    pResult = (char *)xx_rt_malloc(nResultSize + 1U);
    if (!pResult) {
        db_used_tags_free(pTags, nCount);
        return NULL;
    }
    for (i = 0; i < nCount; ++i) {
        if (i) pResult[nPosition++] = ';';
        x_memcpy(pResult + nPosition, pTags[i].pText, pTags[i].nSize);
        nPosition += pTags[i].nSize;
    }
    pResult[nPosition] = '\0';
    db_used_tags_free(pTags, nCount);
    return pResult;
}


int db_count_for_type(DBase *pDb, XFileType fileType)
{
    int nResult = 0;
    int i = 0;

    for (i = 0; i < pDb->nCount; i++) {
        if ((x_strcmp(pDb->pRecords[i].pName, "_init") != 0) && xft_check(pDb->pRecords[i].fileType, fileType)) {
            nResult++;
        }
    }

    return nResult;
}
