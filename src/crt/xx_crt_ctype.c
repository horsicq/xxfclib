/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_ctype.c
 * @brief The "C" locale character classes and case mappings of the
 *        Universal CRT, plus its ASCII case-insensitive comparisons.
 *
 * The class masks are those of UCRT's _pctype table in the "C" locale:
 * only ASCII has classes, bytes 0x80..0xFF and EOF have none, and any
 * argument outside [-1, 255] answers 0. _strnicmp and _stricmp return the
 * difference of the first differing folded bytes, as UCRT does.
 */

#include "xxfclib/crt/xx_crt.h"

#define XX_CRT_U 0x01  /* _UPPER */
#define XX_CRT_L 0x02  /* _LOWER */
#define XX_CRT_D 0x04  /* _DIGIT */
#define XX_CRT_S 0x08  /* _SPACE */
#define XX_CRT_P 0x10  /* _PUNCT */
#define XX_CRT_C 0x20  /* _CONTROL */
#define XX_CRT_B 0x40  /* _BLANK */
#define XX_CRT_X 0x80  /* _HEX */

static const unsigned short s_xx_crt_ctype[128] = {
    /* 0x00 */ XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C,
    /* 0x08 */ XX_CRT_C, XX_CRT_S | XX_CRT_C, XX_CRT_S | XX_CRT_C, XX_CRT_S | XX_CRT_C, XX_CRT_S | XX_CRT_C, XX_CRT_S | XX_CRT_C, XX_CRT_C, XX_CRT_C,
    /* 0x10 */ XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C,
    /* 0x18 */ XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C, XX_CRT_C,
    /* 0x20 */ XX_CRT_S | XX_CRT_B, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P,
    /* 0x28 */ XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P,
    /* 0x30 */ XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X,
    /* 0x38 */ XX_CRT_D | XX_CRT_X, XX_CRT_D | XX_CRT_X, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P,
    /* 0x40 */ XX_CRT_P, XX_CRT_U | XX_CRT_X, XX_CRT_U | XX_CRT_X, XX_CRT_U | XX_CRT_X, XX_CRT_U | XX_CRT_X, XX_CRT_U | XX_CRT_X, XX_CRT_U | XX_CRT_X, XX_CRT_U,
    /* 0x48 */ XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U,
    /* 0x50 */ XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_U,
    /* 0x58 */ XX_CRT_U, XX_CRT_U, XX_CRT_U, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P,
    /* 0x60 */ XX_CRT_P, XX_CRT_L | XX_CRT_X, XX_CRT_L | XX_CRT_X, XX_CRT_L | XX_CRT_X, XX_CRT_L | XX_CRT_X, XX_CRT_L | XX_CRT_X, XX_CRT_L | XX_CRT_X, XX_CRT_L,
    /* 0x68 */ XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L,
    /* 0x70 */ XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_L,
    /* 0x78 */ XX_CRT_L, XX_CRT_L, XX_CRT_L, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_P, XX_CRT_C};

int xx_crt_ctype(int nChar)
{
    if (nChar < 0 || nChar > 127) return 0;
    return s_xx_crt_ctype[nChar];
}

int xx_crt_tolower(int nChar)
{
    return (nChar >= 'A' && nChar <= 'Z') ? nChar - 'A' + 'a' : nChar;
}

int xx_crt_toupper(int nChar)
{
    return (nChar >= 'a' && nChar <= 'z') ? nChar - 'a' + 'A' : nChar;
}

int xx_crt_strnicmp(const char *pLeft, const char *pRight, size_t nCount)
{
    const unsigned char *a = (const unsigned char *)pLeft;
    const unsigned char *b = (const unsigned char *)pRight;
    int nA = 0, nB = 0;
    while (nCount--) {
        nA = xx_crt_tolower(*a++);
        nB = xx_crt_tolower(*b++);
        if (nA != nB || nA == 0) break;
    }
    return nA - nB;
}

int xx_crt_stricmp(const char *pLeft, const char *pRight)
{
    const unsigned char *a = (const unsigned char *)pLeft;
    const unsigned char *b = (const unsigned char *)pRight;
    int nA, nB;
    do {
        nA = xx_crt_tolower(*a++);
        nB = xx_crt_tolower(*b++);
    } while (nA == nB && nA != 0);
    return nA - nB;
}
