/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_time.c
 * @brief gmtime and "C" locale strftime as the Universal CRT computes them,
 *        plus UCRT's errno message table and Win32 error mapping.
 *
 * gmtime accepts UCRT's range: from -43200 (half a day before the epoch, the
 * room UCRT leaves for local time zones) to 32536850399 (3001-01-19
 * 07:59:59 UTC); outside it UCRT returns NULL with EINVAL.
 */

#include "xxfclib/crt/xx_crt.h"

#include <stdint.h>

#define XX_CRT_TM_SEC 0
#define XX_CRT_TM_MIN 1
#define XX_CRT_TM_HOUR 2
#define XX_CRT_TM_MDAY 3
#define XX_CRT_TM_MON 4
#define XX_CRT_TM_YEAR 5
#define XX_CRT_TM_WDAY 6
#define XX_CRT_TM_YDAY 7
#define XX_CRT_TM_ISDST 8

static int xx_crt_is_leap(long long nYear)
{
    return (nYear % 4 == 0 && nYear % 100 != 0) || nYear % 400 == 0;
}

int xx_crt_gmtime(long long nTime, int pFields[9])
{
    static const int s_days_before[2][13] = {{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
                                             {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}};
    long long nDays;
    long long nSeconds;
    long long nYear = 1970;
    int nLeap;
    int nMonth;
    if (nTime < -43200 || nTime > 32536850399LL) return XX_CRT_EINVAL;
    nDays = nTime / 86400;
    nSeconds = nTime % 86400;
    if (nSeconds < 0) {
        nSeconds += 86400;
        --nDays;
    }
    pFields[XX_CRT_TM_SEC] = (int)(nSeconds % 60);
    pFields[XX_CRT_TM_MIN] = (int)((nSeconds / 60) % 60);
    pFields[XX_CRT_TM_HOUR] = (int)(nSeconds / 3600);
    /* 1970-01-01 was a Thursday. UCRT counts the weekday from the day number
     * truncated toward zero, so the half day it accepts before the epoch is
     * a Thursday as well (the calendar date is still 1969-12-31). */
    pFields[XX_CRT_TM_WDAY] = (int)(((nTime / 86400) % 7 + 11) % 7);
    while (nDays < 0) {
        --nYear;
        nDays += xx_crt_is_leap(nYear) ? 366 : 365;
    }
    for (;;) {
        int nLength = xx_crt_is_leap(nYear) ? 366 : 365;
        if (nDays < nLength) break;
        nDays -= nLength;
        ++nYear;
    }
    nLeap = xx_crt_is_leap(nYear);
    pFields[XX_CRT_TM_YDAY] = (int)nDays;
    for (nMonth = 0; nMonth < 12 && nDays >= s_days_before[nLeap][nMonth + 1]; ++nMonth) {
    }
    pFields[XX_CRT_TM_MON] = nMonth;
    pFields[XX_CRT_TM_MDAY] = (int)(nDays - s_days_before[nLeap][nMonth]) + 1;
    pFields[XX_CRT_TM_YEAR] = (int)(nYear - 1900);
    pFields[XX_CRT_TM_ISDST] = 0;
    return 0;
}

typedef struct xx_crt_strftime_out_s {
    char *pBuffer;
    size_t nSize;
    size_t nUsed;
    int bOverflow;
} xx_crt_strftime_out;

static void xx_crt_strftime_put(xx_crt_strftime_out *pOut, const char *pText)
{
    while (*pText) {
        if (pOut->nUsed + 1 >= pOut->nSize) {
            pOut->bOverflow = 1;
            return;
        }
        pOut->pBuffer[pOut->nUsed++] = *pText++;
    }
}

static void xx_crt_strftime_number(xx_crt_strftime_out *pOut, long nValue, int nWidth, char nPad)
{
    char sText[24];
    char sDigits[24];
    int nDigits = 0;
    int nLength = 0;
    int bNegative = nValue < 0;
    unsigned long nMagnitude = bNegative ? 0ul - (unsigned long)nValue : (unsigned long)nValue;
    do {
        sDigits[nDigits++] = (char)('0' + nMagnitude % 10);
        nMagnitude /= 10;
    } while (nMagnitude);
    if (bNegative) sText[nLength++] = '-';
    while (nDigits + nLength < nWidth) sText[nLength++] = nPad;
    while (nDigits) sText[nLength++] = sDigits[--nDigits];
    sText[nLength] = 0;
    xx_crt_strftime_put(pOut, sText);
}

/* ISO 8601 week number (1..53) and the year it belongs to. */
static int xx_crt_iso_week(const int pFields[9], int *pnYear)
{
    int nYear = pFields[XX_CRT_TM_YEAR] + 1900;
    int nWeekday = (pFields[XX_CRT_TM_WDAY] + 6) % 7; /* Monday = 0 */
    int nWeek = (pFields[XX_CRT_TM_YDAY] - nWeekday + 10) / 7;
    if (nWeek < 1) {
        /* the last week of the previous year: 53 when that year starts on
         * a Thursday, or on a Wednesday and is a leap year */
        int nPrevious = nYear - 1;
        int nJan1 = ((pFields[XX_CRT_TM_WDAY] - pFields[XX_CRT_TM_YDAY]) % 7 + 7) % 7; /* weekday of this Jan 1, Sunday = 0 */
        int nPreviousJan1 = ((nJan1 - (xx_crt_is_leap(nPrevious) ? 366 : 365)) % 7 + 7) % 7;
        nWeek = (nPreviousJan1 == 4 || (nPreviousJan1 == 3 && xx_crt_is_leap(nPrevious))) ? 53 : 52;
        nYear = nPrevious;
    } else if (nWeek == 53) {
        int nJan1 = ((pFields[XX_CRT_TM_WDAY] - pFields[XX_CRT_TM_YDAY]) % 7 + 7) % 7;
        if (!(nJan1 == 4 || (nJan1 == 3 && xx_crt_is_leap(nYear)))) {
            nWeek = 1;
            ++nYear;
        }
    }
    *pnYear = nYear;
    return nWeek;
}

size_t xx_crt_strftime(char *pBuffer, size_t nSize, const char *pFormat, const int pFields[9])
{
    return xx_crt_strftime_zone(pBuffer, nSize, pFormat, pFields, NULL, 0);
}

size_t xx_crt_strftime_zone(char *pBuffer, size_t nSize, const char *pFormat, const int pFields[9], const char *pZoneName, long nUtcOffsetSeconds)
{
    static const char *const s_days[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const s_months[12] = {"January", "February", "March", "April", "May", "June",
                                             "July", "August", "September", "October", "November", "December"};
    xx_crt_strftime_out out;
    const char *p = pFormat;
    if (!pBuffer || nSize == 0) return 0;
    out.pBuffer = pBuffer;
    out.nSize = nSize;
    out.nUsed = 0;
    out.bOverflow = 0;
    while (*p && !out.bOverflow) {
        char nConversion;
        int bAlternate = 0;
        if (*p != '%') {
            char sOne[2];
            sOne[0] = *p++;
            sOne[1] = 0;
            xx_crt_strftime_put(&out, sOne);
            continue;
        }
        ++p;
        if (*p == '#') {
            bAlternate = 1;
            ++p;
        }
        nConversion = *p;
        if (!nConversion) break;
        ++p;
        switch (nConversion) {
            case 'a': {
                char sShort[4];
                const char *pDay = s_days[pFields[XX_CRT_TM_WDAY] % 7];
                sShort[0] = pDay[0];
                sShort[1] = pDay[1];
                sShort[2] = pDay[2];
                sShort[3] = 0;
                xx_crt_strftime_put(&out, sShort);
                break;
            }
            case 'A': xx_crt_strftime_put(&out, s_days[pFields[XX_CRT_TM_WDAY] % 7]); break;
            case 'b':
            case 'h': {
                char sShort[4];
                const char *pMonth = s_months[pFields[XX_CRT_TM_MON] % 12];
                sShort[0] = pMonth[0];
                sShort[1] = pMonth[1];
                sShort[2] = pMonth[2];
                sShort[3] = 0;
                xx_crt_strftime_put(&out, sShort);
                break;
            }
            case 'B': xx_crt_strftime_put(&out, s_months[pFields[XX_CRT_TM_MON] % 12]); break;
            case 'c': {
                /* "Thu Jan  1 00:00:00 1970"; with #: "Thursday, January 01, 1970 00:00:00" */
                char sText[96];
                xx_crt_strftime(sText, sizeof(sText), bAlternate ? "%A, %B %d, %Y %H:%M:%S" : "%a %b %e %H:%M:%S %Y", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'C': xx_crt_strftime_number(&out, (pFields[XX_CRT_TM_YEAR] + 1900) / 100, bAlternate ? 1 : 2, '0'); break;
            case 'd': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_MDAY], bAlternate ? 1 : 2, '0'); break;
            case 'D': {
                char sText[32];
                xx_crt_strftime(sText, sizeof(sText), bAlternate ? "%#m/%#d/%#y" : "%m/%d/%y", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'e': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_MDAY], bAlternate ? 1 : 2, bAlternate ? '0' : ' '); break;
            case 'F': {
                char sText[32];
                xx_crt_strftime(sText, sizeof(sText), bAlternate ? "%Y-%#m-%#d" : "%Y-%m-%d", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'H': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_HOUR], bAlternate ? 1 : 2, '0'); break;
            case 'I': {
                int nHour = pFields[XX_CRT_TM_HOUR] % 12;
                xx_crt_strftime_number(&out, nHour == 0 ? 12 : nHour, bAlternate ? 1 : 2, '0');
                break;
            }
            case 'j': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_YDAY] + 1, bAlternate ? 1 : 3, '0'); break;
            case 'm': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_MON] + 1, bAlternate ? 1 : 2, '0'); break;
            case 'M': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_MIN], bAlternate ? 1 : 2, '0'); break;
            case 'n': xx_crt_strftime_put(&out, "\n"); break;
            case 'p': xx_crt_strftime_put(&out, pFields[XX_CRT_TM_HOUR] < 12 ? "AM" : "PM"); break;
            case 'r': {
                char sText[32];
                xx_crt_strftime(sText, sizeof(sText), bAlternate ? "%#I:%#M:%#S %p" : "%I:%M:%S %p", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'R': {
                char sText[32];
                xx_crt_strftime(sText, sizeof(sText), bAlternate ? "%#H:%#M" : "%H:%M", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'S': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_SEC], bAlternate ? 1 : 2, '0'); break;
            case 't': xx_crt_strftime_put(&out, "\t"); break;
            case 'T': {
                char sText[32];
                xx_crt_strftime(sText, sizeof(sText), bAlternate ? "%#H:%#M:%#S" : "%H:%M:%S", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'X': {
                char sText[32];
                xx_crt_strftime(sText, sizeof(sText), "%H:%M:%S", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'U': xx_crt_strftime_number(&out, (pFields[XX_CRT_TM_YDAY] + 7 - pFields[XX_CRT_TM_WDAY]) / 7, bAlternate ? 1 : 2, '0'); break;
            case 'W':
                xx_crt_strftime_number(&out, (pFields[XX_CRT_TM_YDAY] + 7 - (pFields[XX_CRT_TM_WDAY] + 6) % 7) / 7, bAlternate ? 1 : 2, '0');
                break;
            case 'V':
            case 'G':
            case 'g': {
                int nIsoYear;
                int nIsoWeek = xx_crt_iso_week(pFields, &nIsoYear);
                if (nConversion == 'V') xx_crt_strftime_number(&out, nIsoWeek, bAlternate ? 1 : 2, '0');
                else if (nConversion == 'G') xx_crt_strftime_number(&out, nIsoYear, bAlternate ? 1 : 4, '0');
                else xx_crt_strftime_number(&out, nIsoYear % 100, 2, '0'); /* UCRT: # does not shorten %g */
                break;
            }
            case 'u': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_WDAY] == 0 ? 7 : pFields[XX_CRT_TM_WDAY], 1, '0'); break;
            case 'w': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_WDAY], 1, '0'); break;
            case 'x': {
                /* with #: the long date "Thursday, January 01, 1970" */
                char sText[64];
                xx_crt_strftime(sText, sizeof(sText), bAlternate ? "%A, %B %d, %Y" : "%m/%d/%y", pFields);
                xx_crt_strftime_put(&out, sText);
                break;
            }
            case 'y': xx_crt_strftime_number(&out, (pFields[XX_CRT_TM_YEAR] + 1900) % 100, bAlternate ? 1 : 2, '0'); break;
            case 'Y': xx_crt_strftime_number(&out, pFields[XX_CRT_TM_YEAR] + 1900, bAlternate ? 1 : 1, '0'); break;
            case 'z':
                if (pZoneName) {
                    long nMinutes = (nUtcOffsetSeconds < 0 ? -nUtcOffsetSeconds : nUtcOffsetSeconds) / 60;
                    xx_crt_strftime_put(&out, nUtcOffsetSeconds < 0 ? "-" : "+");
                    xx_crt_strftime_number(&out, nMinutes / 60 * 100 + nMinutes % 60, 4, '0');
                }
                break;
            case 'Z':
                if (pZoneName) xx_crt_strftime_put(&out, pZoneName);
                break;
            case '%': xx_crt_strftime_put(&out, "%"); break;
            default: break;
        }
    }
    if (out.bOverflow) {
        pBuffer[0] = 0;
        return 0;
    }
    pBuffer[out.nUsed] = 0;
    return out.nUsed;
}

const char *xx_crt_strerror_text(int nError)
{
    static const char *const s_messages[] = {"No error",
                                             "Operation not permitted",
                                             "No such file or directory",
                                             "No such process",
                                             "Interrupted function call",
                                             "Input/output error",
                                             "No such device or address",
                                             "Arg list too long",
                                             "Exec format error",
                                             "Bad file descriptor",
                                             "No child processes",
                                             "Resource temporarily unavailable",
                                             "Not enough space",
                                             "Permission denied",
                                             "Bad address",
                                             "Unknown error",
                                             "Resource device",
                                             "File exists",
                                             "Improper link",
                                             "No such device",
                                             "Not a directory",
                                             "Is a directory",
                                             "Invalid argument",
                                             "Too many open files in system",
                                             "Too many open files",
                                             "Inappropriate I/O control operation",
                                             "Unknown error",
                                             "File too large",
                                             "No space left on device",
                                             "Invalid seek",
                                             "Read-only file system",
                                             "Too many links",
                                             "Broken pipe",
                                             "Domain error",
                                             "Result too large",
                                             "Unknown error",
                                             "Resource deadlock avoided",
                                             "Unknown error",
                                             "Filename too long",
                                             "No locks available",
                                             "Function not implemented",
                                             "Directory not empty",
                                             "Illegal byte sequence"};
    /* The POSIX supplement, errno 100..140 (131 has no text). */
    static const char *const s_posix[] = {"address in use", "address not available", "address family not supported",
                                          "connection already in progress", "bad message", "operation canceled", "connection aborted",
                                          "connection refused", "connection reset", "destination address required", "host unreachable",
                                          "identifier removed", "operation in progress", "already connected", "too many symbolic link levels",
                                          "message size", "network down", "network reset", "network unreachable", "no buffer space",
                                          "no message available", "no link", "no message", "no protocol option", "no stream resources",
                                          "not a stream", "not connected", "state not recoverable", "not a socket", "not supported",
                                          "operation not supported", "Unknown error", "value too large", "owner dead", "protocol error",
                                          "protocol not supported", "wrong protocol type", "stream timeout", "timed out", "text file busy",
                                          "operation would block"};
    if (nError >= 100 && nError < 100 + (int)(sizeof(s_posix) / sizeof(s_posix[0]))) return s_posix[nError - 100];
    if (nError < 0 || nError >= (int)(sizeof(s_messages) / sizeof(s_messages[0]))) return "Unknown error";
    return s_messages[nError];
}

int xx_crt_errno_from_oserror(unsigned long nError)
{
    static const struct {
        unsigned long nOs;
        int nErrno;
    } s_map[] = {{1, 22},   {2, 2},    {3, 2},   {4, 24},   {5, 13},   {6, 9},    {7, 12},   {8, 12},  {9, 12},  {10, 7},
                 {11, 8},   {12, 22},  {13, 22}, {15, 2},   {16, 13},  {17, 18},  {18, 2},   {33, 13}, {53, 2},  {65, 13},
                 {67, 2},   {80, 17},  {82, 13}, {83, 13},  {87, 22},  {89, 11},  {108, 13}, {109, 32}, {112, 28}, {114, 9},
                 {128, 10}, {129, 10}, {130, 9}, {131, 22}, {132, 13}, {145, 41}, {158, 13}, {161, 2}, {164, 11}, {167, 13},
                 {183, 17}, {206, 2},  {215, 11}, {1113, 42}, {1816, 12}};
    size_t i;
    for (i = 0; i < sizeof(s_map) / sizeof(s_map[0]); ++i) {
        if (s_map[i].nOs == nError) return s_map[i].nErrno;
    }
    if (nError >= 19 && nError <= 36) return 13;   /* write protect .. sharing buffer exceeded: EACCES */
    if (nError >= 188 && nError <= 202) return 8;  /* invalid exe formats: ENOEXEC */
    return 22;                                     /* EINVAL */
}
