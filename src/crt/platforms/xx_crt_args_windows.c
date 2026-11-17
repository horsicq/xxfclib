/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_args_windows.c
 * @brief Splits a Windows command line into argv exactly as the Universal
 *        CRT's startup code does.
 *
 * The program name (argv[0]) ends at the first space or tab outside double
 * quotes; quotes toggle and are dropped, backslashes are literal. The other
 * arguments follow the documented rules: 2n backslashes before a quote give
 * n backslashes and toggle quoting, 2n+1 give n backslashes and a literal
 * quote, other backslashes are literal, and inside quotes a doubled quote
 * is one literal quote. The result is one allocation (argv array, strings
 * after it), freed with free().
 *
 * The narrow parser keeps a double-byte character together, as UCRT does
 * with the process's multibyte code page: the trail byte of a lead byte is
 * copied without being looked at, so a trail byte 0x5C (for example in
 * CP932) is not taken for a backslash.
 *
 * No standard names here, so the differential test can compare this parser
 * with UCRT's by starting processes.
 */

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>

#include "xx_crt_args_windows.h"

/* Whether c starts a two-unit character whose second unit must be copied
 * unseen: never in UTF-16 here, a DBCS lead byte in a narrow code page. */
static int xx_crt_args_lead_w(unsigned nCodePage, wchar_t c)
{
    (void)nCodePage;
    (void)c;
    return 0;
}

static int xx_crt_args_lead_a(unsigned nCodePage, char c)
{
    return ((unsigned char)c & 0x80) && IsDBCSLeadByteEx(nCodePage, (BYTE)c);
}

#define XX_CRT_ARGS_IMPL(name, CHAR, LEAD)                                                                             \
    static size_t name##_pass(const CHAR *pCommand, CHAR **ppArgv, CHAR *pText, int *pnArgc, unsigned nCodePage)       \
    {                                                                                                                  \
        const CHAR *p = pCommand;                                                                                      \
        size_t nChars = 0;                                                                                             \
        int nArgc = 0;                                                                                                 \
        int bQuoted = 0;                                                                                               \
        CHAR c;                                                                                                        \
        /* program name */                                                                                             \
        if (ppArgv) ppArgv[nArgc] = pText + nChars;                                                                    \
        ++nArgc;                                                                                                       \
        do {                                                                                                           \
            if (*p == '"') {                                                                                           \
                bQuoted = !bQuoted;                                                                                    \
                c = *p++;                                                                                              \
                continue;                                                                                              \
            }                                                                                                          \
            if (pText) pText[nChars] = *p;                                                                             \
            ++nChars;                                                                                                  \
            c = *p++;                                                                                                  \
            if (LEAD(nCodePage, c) && *p) {                                                                                       \
                if (pText) pText[nChars] = *p;                                                                         \
                ++nChars;                                                                                              \
                ++p;                                                                                                   \
            }                                                                                                          \
        } while (c != 0 && (bQuoted || (c != ' ' && c != '\t')));                                                      \
        if (c == 0) {                                                                                                  \
            --p;                                                                                                       \
        } else if (pText) {                                                                                            \
            pText[nChars - 1] = 0;                                                                                     \
        }                                                                                                              \
        bQuoted = 0;                                                                                                   \
        for (;;) {                                                                                                     \
            if (*p) {                                                                                                  \
                while (*p == ' ' || *p == '\t') ++p;                                                                   \
            }                                                                                                          \
            if (*p == 0) break;                                                                                        \
            if (ppArgv) ppArgv[nArgc] = pText + nChars;                                                                \
            ++nArgc;                                                                                                   \
            for (;;) {                                                                                                 \
                int bCopy = 1;                                                                                         \
                size_t nSlashes = 0;                                                                                   \
                if (LEAD(nCodePage, *p) && p[1]) {                                                                                \
                    if (pText) {                                                                                       \
                        pText[nChars] = p[0];                                                                          \
                        pText[nChars + 1] = p[1];                                                                      \
                    }                                                                                                  \
                    nChars += 2;                                                                                       \
                    p += 2;                                                                                            \
                    continue;                                                                                          \
                }                                                                                                      \
                while (*p == '\\') {                                                                                   \
                    ++p;                                                                                               \
                    ++nSlashes;                                                                                        \
                }                                                                                                      \
                if (*p == '"') {                                                                                       \
                    if (nSlashes % 2 == 0) {                                                                           \
                        if (bQuoted && p[1] == '"') {                                                                  \
                            ++p;                                                                                       \
                        } else {                                                                                       \
                            bCopy = 0;                                                                                 \
                            bQuoted = !bQuoted;                                                                        \
                        }                                                                                              \
                    }                                                                                                  \
                    nSlashes /= 2;                                                                                     \
                }                                                                                                      \
                while (nSlashes--) {                                                                                   \
                    if (pText) pText[nChars] = '\\';                                                                   \
                    ++nChars;                                                                                          \
                }                                                                                                      \
                if (*p == 0 || (!bQuoted && (*p == ' ' || *p == '\t'))) break;                                         \
                if (bCopy) {                                                                                           \
                    if (pText) pText[nChars] = *p;                                                                     \
                    ++nChars;                                                                                          \
                }                                                                                                      \
                ++p;                                                                                                   \
            }                                                                                                          \
            if (pText) pText[nChars] = 0;                                                                              \
            ++nChars;                                                                                                  \
        }                                                                                                              \
        if (ppArgv) ppArgv[nArgc] = NULL;                                                                              \
        *pnArgc = nArgc;                                                                                               \
        return nChars;                                                                                                 \
    }                                                                                                                  \
    static CHAR **name##_cp(const CHAR *pCommand, int *pnArgc, unsigned nCodePage)                                     \
    {                                                                                                                  \
        static const CHAR s_empty[1] = {0};                                                                            \
        int nArgc = 0;                                                                                                 \
        size_t nChars;                                                                                                 \
        CHAR **ppArgv;                                                                                                 \
        if (!pCommand) pCommand = s_empty;                                                                             \
        nChars = name##_pass(pCommand, NULL, NULL, &nArgc, nCodePage);                                                 \
        ppArgv = (CHAR **)malloc(((size_t)nArgc + 1) * sizeof(CHAR *) + (nChars + 1) * sizeof(CHAR));                  \
        if (!ppArgv) return NULL;                                                                                      \
        name##_pass(pCommand, ppArgv, (CHAR *)(ppArgv + nArgc + 1), &nArgc, nCodePage);                                \
        *pnArgc = nArgc;                                                                                               \
        return ppArgv;                                                                                                 \
    }

XX_CRT_ARGS_IMPL(xx_crt_parse_w, wchar_t, xx_crt_args_lead_w)
XX_CRT_ARGS_IMPL(xx_crt_parse_a, char, xx_crt_args_lead_a)

wchar_t **xx_crt_parse_command_line_w(const wchar_t *pCommand, int *pnArgc)
{
    return xx_crt_parse_w_cp(pCommand, pnArgc, 0);
}

char **xx_crt_parse_command_line_a(const char *pCommand, int *pnArgc)
{
    return xx_crt_parse_a_cp(pCommand, pnArgc, CP_ACP);
}

char **xx_crt_parse_command_line_a_cp(const char *pCommand, int *pnArgc, unsigned nCodePage)
{
    return xx_crt_parse_a_cp(pCommand, pnArgc, nCodePage);
}

#endif /* _WIN32 */
