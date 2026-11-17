/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt.h
 * @brief The portable cores of xxfclib's standard-name C runtime.
 *
 * A program that links no Microsoft C runtime at all (no ucrt, vcruntime or
 * msvcrt, neither as DLLs nor as the static libucrt/libvcruntime/libcmt)
 * still calls strtod, snprintf, qsort, isalpha, log2 and friends by their
 * standard names. On Windows those names are defined by the units in
 * src/crt/platforms (the standard-name layer, the stdio streams, the program
 * and DLL entry points and the thread-local storage directory); they are
 * compiled only into a CRT-free link. Everything that is not tied to the
 * operating system lives in the functions declared here, under xx_crt_
 * names, so that a differential test linked against the real C runtime can
 * call both side by side.
 *
 * The results follow Microsoft's Universal CRT, which is what a program
 * built with MSVC observes: printf's float conversions are exact with
 * round-half-even on exact ties (the "standard rounding" UCRT selects at
 * startup), %p prints 16 upper-case digits, strtod is correctly rounded,
 * qsort permutes equal elements exactly like UCRT's quicksort, and the
 * character classes are those of the "C" locale. errno values are returned
 * through an out parameter instead of being stored, because errno itself
 * belongs to the platform layer.
 */

#ifndef XXFCLIB_XX_CRT_H
#define XXFCLIB_XX_CRT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "xxfclib/xxfc_defs.h"
#include "xxfclib/crt/xx_ms_log2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* errno values of the Universal CRT used by the cores. */
#define XX_CRT_EINVAL 22
#define XX_CRT_EDOM 33
#define XX_CRT_ERANGE 34
#define XX_CRT_EILSEQ 42

/* ---------------------------------------------------------------- printf -- */

/* Receives formatted output in pieces; returns 0, or nonzero to stop. */
typedef int (*xx_crt_write_fn)(void *pContext, const char *pData, size_t nSize);

/* Formats with the conversions of UCRT's printf family (flags - + space # 0,
 * width and precision including '*', the length modifiers hh h l ll L j z t
 * I I32 I64 w, and the conversions d i o u x X c C s S p e E f F g G a A %).
 * Returns the number of characters produced, or -1 when a conversion fails
 * (a wide character outside the "C" locale) or the sink stops. */
XXFC_API int xx_crt_vformat(xx_crt_write_fn fnWrite, void *pContext, const char *pFormat, va_list args);

/* snprintf: writes at most nSize - 1 characters plus a terminator and
 * returns the length the complete output needs (-1 on a conversion error). */
XXFC_API int xx_crt_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, va_list args);
XXFC_API int xx_crt_snprintf(char *pBuffer, size_t nSize, const char *pFormat, ...);

/* The legacy _vsnprintf shape: stores at most nCount characters and never a
 * terminator; returns the length of the complete output (-1 on error). */
XXFC_API int xx_crt_vsnprintf_raw(char *pBuffer, size_t nCount, const char *pFormat, va_list args);

/* One floating-point conversion ('e', 'E', 'f', 'F', 'g', 'G', 'a' or 'A')
 * of nValue, sign and digits only (no width), as UCRT spells it. nPrecision
 * < 0 selects the default. Returns the length; at most nBufferSize - 1
 * characters are stored, followed by a terminator. */
XXFC_API int xx_crt_format_double(double nValue, char nConversion, int nPrecision, int bAlt, int bPlus, int bSpace, char *pBuffer, size_t nBufferSize);

/* ----------------------------------------------------------------- scanf -- */

/* sscanf over a NUL-terminated string with UCRT's conversions. bSecure
 * selects the sscanf_s convention (a buffer size follows %s, %c and %[).
 * Returns the number of assigned fields, or -1 (EOF) when the input ends
 * before the first conversion. */
XXFC_API int xx_crt_vsscanf(const char *pInput, const char *pFormat, int bSecure, va_list args);

/* ---------------------------------------------------------- conversions -- */

/* strtod with UCRT's syntax (decimal, hexadecimal, inf/infinity,
 * nan/nan(chars)) and a correctly rounded result. *pnErrno receives
 * XX_CRT_ERANGE when UCRT would set it and is left alone otherwise. */
XXFC_API double xx_crt_strtod(const char *pString, char **ppEnd, int *pnErrno);

/* strtof: the same syntax, converted to binary32 directly (one rounding),
 * as UCRT's strtof and scanf's %f do. ERANGE as for strtod. */
XXFC_API float xx_crt_strtof(const char *pString, char **ppEnd, int *pnErrno);

/* strtod and strtof on at most nLength characters (the text need not be
 * terminated there). */
XXFC_API double xx_crt_strtod_n(const char *pString, size_t nLength, char **ppEnd, int *pnErrno);
XXFC_API float xx_crt_strtof_n(const char *pString, size_t nLength, char **ppEnd, int *pnErrno);

/* strtol, strtoul, strtoll and strtoull with UCRT's saturation and errno
 * (the long variants use the width of the target's long). */
XXFC_API long long xx_crt_strtoll(const char *pString, char **ppEnd, int nBase, int *pnErrno);
XXFC_API unsigned long long xx_crt_strtoull(const char *pString, char **ppEnd, int nBase, int *pnErrno);
XXFC_API long xx_crt_strtol(const char *pString, char **ppEnd, int nBase, int *pnErrno);
XXFC_API unsigned long xx_crt_strtoul(const char *pString, char **ppEnd, int nBase, int *pnErrno);

/* ---------------------------------------------------------------- qsort -- */

/* UCRT's qsort: the same comparisons in the same order, so elements that
 * compare equal end up in the same order. Element exchange is vectorised. */
XXFC_API void xx_crt_qsort(void *pBase, size_t nCount, size_t nSize, int (*fnCompare)(const void *, const void *));
XXFC_API void xx_crt_qsort_s(void *pBase, size_t nCount, size_t nSize, int (*fnCompare)(void *, const void *, const void *), void *pContext);

/* ---------------------------------------------------------------- ctype -- */

/* The "C" locale class mask of UCRT's ctype table (_UPPER 0x1, _LOWER 0x2,
 * _DIGIT 0x4, _SPACE 0x8, _PUNCT 0x10, _CONTROL 0x20, _BLANK 0x40, _HEX 0x80,
 * _ALPHA 0x100) for nChar in [-1, 255]; 0 outside that range. */
XXFC_API int xx_crt_ctype(int nChar);
XXFC_API int xx_crt_tolower(int nChar);
XXFC_API int xx_crt_toupper(int nChar);

/* --------------------------------------------------------------- strings -- */

XXFC_API int xx_crt_strnicmp(const char *pLeft, const char *pRight, size_t nCount);
XXFC_API int xx_crt_stricmp(const char *pLeft, const char *pRight);
XXFC_API size_t xx_crt_strlen(const char *pString);
XXFC_API char *xx_crt_strchr(const char *pString, int nChar);
XXFC_API int xx_crt_memcmp(const void *pLeft, const void *pRight, size_t nSize);
XXFC_API void *xx_crt_memchr(const void *pMemory, int nChar, size_t nSize);
XXFC_API void *xx_crt_memcpy(void *pDestination, const void *pSource, size_t nSize);
XXFC_API void *xx_crt_memmove(void *pDestination, const void *pSource, size_t nSize);
XXFC_API void *xx_crt_memset(void *pDestination, int nValue, size_t nSize);

/* ------------------------------------------------------------------ math -- */

/* _dclass/_dsign/_fdclass/_fdsign of <math.h>: FP_NAN 2, FP_INFINITE 1,
 * FP_ZERO 0, FP_NORMAL -1, FP_SUBNORMAL -2; the sign as UCRT returns it. */
XXFC_API short xx_crt_dclass(double nValue);
XXFC_API int xx_crt_dsign(double nValue);
XXFC_API short xx_crt_fdclass(float nValue);
XXFC_API int xx_crt_fdsign(float nValue);

XXFC_API double xx_crt_floor(double nValue);
XXFC_API double xx_crt_ceil(double nValue);
XXFC_API double xx_crt_trunc(double nValue);
XXFC_API double xx_crt_fabs(double nValue);
XXFC_API double xx_crt_sqrt(double nValue);

/* The same correctly rounded square root computed with integer operations
 * only (bit by bit); xx_crt_sqrt uses it where no hardware square root is
 * selected. Exposed so that a test can compare it everywhere. */
XXFC_API double xx_crt_sqrt_portable(double nValue);

/* The errno value UCRT's math functions report for an argument, or 0:
 * log2: XX_CRT_ERANGE for +-0, XX_CRT_EDOM for a negative argument (-inf
 * included); sqrt: XX_CRT_EDOM for a negative nonzero argument. NaN
 * arguments report nothing. The value functions themselves never touch
 * errno; the standard-name layer stores these. */
XXFC_API int xx_crt_log2_errno(double nValue);
XXFC_API int xx_crt_sqrt_errno(double nValue);

/* Base-2 logarithm, correctly rounded (round to nearest even) for every
 * finite positive argument, with UCRT's special values: log2(1) = +0,
 * log2(+-0) = -inf, log2(+inf) = +inf, a NaN argument comes back quiet and
 * a negative one gives -nan(ind). UCRT's own log2 is not correctly rounded
 * (0.06% of random doubles and 0.5% of the arguments in [0.5, 2) differ in
 * the last bit). The standard-name log2 of the CRT-free layer is therefore
 * NOT this function but xx_ms_log2 (xx_ms_log2.h), which reproduces UCRT's
 * log2 bit for bit; use xx_crt_log2 where accuracy matters and xx_ms_log2
 * where the result must equal what a program built with MSVC computes. */
XXFC_API double xx_crt_log2(double nValue);

/* ------------------------------------------------------------------ time -- */

/* gmtime for seconds since 1970 with UCRT's accepted range
 * [0, 32535215999] (the end of the year 3000): the broken-down time in the
 * nine ints of struct tm (sec, min, hour, mday, mon, year-1900, wday, yday,
 * isdst). Returns 0, or XX_CRT_EINVAL out of range. */
XXFC_API int xx_crt_gmtime(long long nTime, int pFields[9]);

/* strftime for the "C" locale over the nine struct tm fields above, with
 * every conversion UCRT accepts (a A b B c C d D e F g G h H I j m M n p r R
 * S t T u U V w W x X y Y z Z %) and the # flag. %z and %Z produce nothing
 * here: the time zone belongs to the platform. */
XXFC_API size_t xx_crt_strftime(char *pBuffer, size_t nSize, const char *pFormat, const int pFields[9]);

/* The same with a time zone for %Z (pZoneName, may be NULL) and %z
 * (nUtcOffsetSeconds east of UTC, printed as +hhmm or -hhmm). */
XXFC_API size_t xx_crt_strftime_zone(char *pBuffer, size_t nSize, const char *pFormat, const int pFields[9], const char *pZoneName,
                                     long nUtcOffsetSeconds);

/* ------------------------------------------------------- errno messages -- */

/* UCRT's strerror text for an errno value ("Unknown error" otherwise). */
XXFC_API const char *xx_crt_strerror_text(int nError);

/* UCRT's mapping of a Win32 error code to errno (_get_errno_from_oserr). */
XXFC_API int xx_crt_errno_from_oserror(unsigned long nError);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_CRT_H */
