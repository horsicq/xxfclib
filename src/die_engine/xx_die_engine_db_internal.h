/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_DIE_ENGINE_DB_INTERNAL_H
#define XX_DIE_ENGINE_DB_INTERNAL_H

#include <stddef.h>

static unsigned char db_ascii_fold(unsigned char c)
{
    if (c >= (unsigned char)'A' && c <= (unsigned char)'Z') return (unsigned char)(c - 'A' + 'a');
    return c;
}

static int db_ascii_span_equal(const char *pLeft, size_t nLeft, const char *pRight, size_t nRight)
{
    size_t i;
    if (!pLeft || !pRight || nLeft != nRight) return 0;
    for (i = 0; i < nLeft; ++i) {
        if (db_ascii_fold((unsigned char)pLeft[i]) != db_ascii_fold((unsigned char)pRight[i])) return 0;
    }
    return 1;
}

static int db_is_tag_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static void db_trim_span(const char **ppText, size_t *pSize)
{
    const char *pText = *ppText;
    size_t nSize = *pSize;
    while (nSize && db_is_tag_space(*pText)) {
        ++pText;
        --nSize;
    }
    while (nSize && db_is_tag_space(pText[nSize - 1])) --nSize;
    *ppText = pText;
    *pSize = nSize;
}

#endif /* XX_DIE_ENGINE_DB_INTERNAL_H */
