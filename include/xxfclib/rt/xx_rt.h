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

/**
 * @file xx_rt.h
 * @brief The CRT-compatible runtime surface used throughout xxfclib.
 *
 * Every wrapped standard-library function is reachable through an `xx_rt_`
 * prefixed stub declared here. Implementations are grouped with the subsystem
 * they serve: process, I/O and formatting live in src/rt, runtime memory
 * primitives in src/memory, sorting in src/sort, and narrow/wide string
 * primitives in src/strings.
 * Within each, anything that differs by operating system sits in that
 * subsystem's platforms/ directory, so no file outside platforms/ carries a
 * platform conditional. A translation unit that uses this surface can be built
 * for a target with no C runtime at all: on Windows the implementations use
 * Win32 or are hand-rolled, and hosted builds may forward to libc.
 *
 * That is what makes a freestanding link possible. xxfclib's shared target
 * builds with /NODEFAULTLIB (see XXFC_NO_CRT in CMakeLists.txt), so anything
 * it pulls in must resolve without the CRT.
 *
 * Three things cannot be wrapped in a function because they are macros that
 * must expand in the caller's frame, and are exposed as macros instead: the
 * varargs helpers (XX_RT_VA_*), the non-local jump helpers (XX_RT_SETJMP,
 * XX_RT_LONGJMP) and XX_RT_OFFSETOF.
 *
 * Rule for new code: never call a standard function directly, always call
 * the xx_rt_ stub.
 *
 * @par Provenance
 * Adapted from the `utils.h` / `utils.c` runtime layer of cdie
 * (github.com/horsicq, MIT), where it served the same purpose. The `x_`
 * prefix was renamed to `xx_rt_` on import. Memory operations now use shared
 * CPU backends; the exact-round-trip double formatters in xx_rt_fp.c that a
 * JavaScript Number.toString depends on remain unchanged.
 */

#ifndef XXFCLIB_XX_RT_H
#define XXFCLIB_XX_RT_H

/* Both are compiler-provided, not library functions: <stddef.h> only defines
 * types and macros, and va_list cannot be wrapped.                         */
#include <stddef.h> /* size_t, NULL, offsetof */
#include <stdarg.h> /* va_list                */
#include "xxfclib/xxfc_defs.h"
#include "xxfclib/sort/xx_sort.h"

/* ------------------------------------------------------------- macros --- */

#define XX_RT_VA_LIST va_list
#define XX_RT_VA_START(ap, last) va_start(ap, last)
#define XX_RT_VA_END(ap) va_end(ap)
#define XX_RT_VA_COPY(dst, src) va_copy(dst, src)

/* C99 spells neither "never returns" nor "checks like printf", so both are
 * expressed through the GNU attributes where the compiler understands them
 * and expand to nothing everywhere else. Nothing depends on them.         */
#if defined(__GNUC__)
#define XX_RT_NORETURN __attribute__((noreturn))
#define XX_RT_PRINTF_LIKE(nFormat, nFirst) __attribute__((format(printf, nFormat, nFirst)))
#else
#define XX_RT_NORETURN
#define XX_RT_PRINTF_LIKE(nFormat, nFirst)
#endif

#define XX_RT_OFFSETOF(type, member) offsetof(type, member)

/* setjmp/longjmp are deliberately absent: they live in the C runtime, and a
 * CRT-free build cannot use them. The parser unwinds with an error flag.   */

/* fseek() origins, so callers do not need <stdio.h>. */
#define XX_RT_SEEK_SET 0
#define XX_RT_SEEK_CUR 1
#define XX_RT_SEEK_END 2

/* ------------------------------------------------------------ strings --- */

XXFC_API size_t xx_rt_strlen(const char *pString);
XXFC_API int xx_rt_strcmp(const char *pLeft, const char *pRight);
XXFC_API int xx_rt_strncmp(const char *pLeft, const char *pRight, size_t nSize);
XXFC_API char *xx_rt_strncpy(char *pDestination, const char *pSource, size_t nSize);
XXFC_API char *xx_rt_strchr(const char *pString, int nChar);
XXFC_API char *xx_rt_strrchr(const char *pString, int nChar);
XXFC_API char *xx_rt_strstr(const char *pHaystack, const char *pNeedle);
XXFC_API size_t xx_rt_wcslen(const wchar_t *pString);
XXFC_API int xx_rt_wcscmp(const wchar_t *pLeft, const wchar_t *pRight);
XXFC_API int xx_rt_wcsncmp(const wchar_t *pLeft, const wchar_t *pRight, size_t nSize);
XXFC_API wchar_t *xx_rt_wcschr(const wchar_t *pString, wchar_t nChar);

/* Locale-independent ASCII folding. Bytes outside A..Z are unchanged. */
XXFC_API int xx_rt_ascii_tolower(int nChar);

/* Memory operations live in src/memory/xx_memory_rt.c, with CPU backends in
 * src/memory/platforms. Allocators remain in the OS platform files; on Windows
 * xx_rt_malloc(0) returns a one-byte block, while xx_memory_platform_alloc(0)
 * returns NULL. */
/* ------------------------------------------------------------- memory --- */

XXFC_API void *xx_rt_memcpy(void *pDestination, const void *pSource, size_t nSize);
XXFC_API void *xx_rt_memmove(void *pDestination, const void *pSource, size_t nSize);
XXFC_API void *xx_rt_memset(void *pDestination, int nValue, size_t nSize);
XXFC_API int xx_rt_memcmp(const void *pLeft, const void *pRight, size_t nSize);
XXFC_API void *xx_rt_memchr(const void *pMemory, int nChar, size_t nSize);

XXFC_API void *xx_rt_malloc(size_t nSize);
XXFC_API void *xx_rt_calloc(size_t nCount, size_t nSize);
XXFC_API void *xx_rt_realloc(void *pPtr, size_t nSize);
XXFC_API void xx_rt_free(void *pPtr);

/* ------------------------------------------------------------ process --- */

XXFC_API XX_RT_NORETURN void xx_rt_exit(int nCode);
XXFC_API char *xx_rt_getenv(const char *pName);
/* Compatibility names; sorting implementations live in src/sort. */
typedef xx_sort_compare_fn xx_rt_compare_fn;
typedef xx_sort_compare_context_fn xx_rt_compare_context_fn;
XXFC_API void xx_rt_qsort(void *pBase, size_t nCount, size_t nSize, xx_rt_compare_fn fnCompare);
XXFC_API void xx_rt_qsort_context(void *pBase, size_t nCount, size_t nSize, xx_rt_compare_context_fn fnCompare, void *pContext);

/* ---------------------------------------------------------- conversion --- */

/* The end pointer stays const: unlike the C library these never hand back a
 * writable view of the input, so the qualifier is carried through.        */
XXFC_API double xx_rt_strtod(const char *pString, const char **ppEnd);
XXFC_API long xx_rt_strtol(const char *pString, const char **ppEnd, int nBase);
XXFC_API unsigned long long xx_rt_strtoull(const char *pString, const char **ppEnd, int nBase);

XXFC_API int xx_rt_rand(void);
#define XX_RT_RAND_MAX 32767

/* ---------------------------------------------------------------- I/O --- */

/* Streams are opaque handles so that <stdio.h> stays inside xx_rt.c. */
XXFC_API void *xx_rt_stdout(void);
XXFC_API void *xx_rt_stderr(void);

XXFC_API XX_RT_PRINTF_LIKE(1, 2) int xx_rt_printf(const char *pFormat, ...);
XXFC_API XX_RT_PRINTF_LIKE(2, 3) int xx_rt_fprintf(void *pStream, const char *pFormat, ...);
XXFC_API XX_RT_PRINTF_LIKE(3, 4) int xx_rt_snprintf(char *pBuffer, size_t nSize, const char *pFormat, ...);
XXFC_API XX_RT_PRINTF_LIKE(3, 0) int xx_rt_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, XX_RT_VA_LIST args);
XXFC_API int xx_rt_fflush(void *pStream);

XXFC_API void *xx_rt_fopen(const char *pFileName, const char *pMode);
XXFC_API int xx_rt_fclose(void *pFile);
XXFC_API int xx_rt_remove(const char *pFileName);

#if defined(_WIN32)
/* UTF-8 <-> UTF-16 at the Win32 boundary. Only defined on Windows; every
 * path in the program is UTF-8, but the file and directory APIs are the wide
 * ones so that non-ANSI paths open. The result is a heap-allocated WCHAR* /
 * char* (freed with xx_rt_free); the header keeps them opaque to avoid pulling
 * in <windows.h>. */
XXFC_API void *xx_rt_utf8_to_utf16(const char *pUtf8);
XXFC_API char *xx_rt_utf16_to_utf8(const void *pUtf16);
#endif
XXFC_API size_t xx_rt_fread(void *pBuffer, size_t nSize, size_t nCount, void *pFile);
/* File offsets are 64 bit on every target: `long` is 32 bit on 64-bit
 * Windows, which truncates the size of any file at or above 2 GiB. */
XXFC_API int xx_rt_fseek(void *pFile, long long nOffset, int nOrigin);
XXFC_API long long xx_rt_ftell(void *pFile);
XXFC_API void xx_rt_rewind(void *pFile);

/* ----------------------------------------------------------------- math -- */

XXFC_API double xx_rt_floor(double nValue);
XXFC_API double xx_rt_ceil(double nValue);
XXFC_API double xx_rt_fabs(double nValue);
XXFC_API double xx_rt_sqrt(double nValue);
XXFC_API double xx_rt_log(double nValue);
XXFC_API double xx_rt_log10(double nValue);
XXFC_API double xx_rt_exp(double nValue);
XXFC_API double xx_rt_sin(double nValue);
XXFC_API double xx_rt_cos(double nValue);
XXFC_API double xx_rt_tan(double nValue);
XXFC_API double xx_rt_pow(double nBase, double nExponent);
XXFC_API double xx_rt_fmod(double nValue, double nDivisor);

/* Quiet NaN and positive infinity, so that <math.h> macros stay internal. */
XXFC_API double xx_rt_nan(void);
XXFC_API double xx_rt_inf(void);
XXFC_API int xx_rt_isnan(double nValue);
XXFC_API int xx_rt_isinf(double nValue);
/* The sign bit, which is not the same question as nValue < 0: -0.0 prints
 * with its sign and compares equal to zero.                                */
XXFC_API int xx_rt_signbit(double nValue);

/* ------------------------------------------- double <-> decimal string --- */

/* ECMAScript Number::toString: the shortest decimal that reads back as the
 * same double, with the exponent rules of the specification. Returns the
 * number of characters written.                                            */
XXFC_API int xx_rt_dtoa_shortest(double nValue, char *pBuffer, size_t nBufferSize);
/* Rust `{:?}` layout of the same shortest digits: keeps a ".0" on integral
 * values and switches to the exponential form outside [1e-5, 1e16). */
XXFC_API int xx_rt_dtoa_debug(double nValue, char *pBuffer, size_t nBufferSize);

/* Number.prototype.toFixed: nDigits digits after the decimal point. */
XXFC_API int xx_rt_dtoa_fixed(double nValue, int nDigits, char *pBuffer, size_t nBufferSize);

/* Number.prototype.toPrecision: nDigits significant digits. */
XXFC_API int xx_rt_dtoa_precision(double nValue, int nDigits, char *pBuffer, size_t nBufferSize);

/* printf's %e, %f and %g of the magnitude of nValue - never a sign, the
 * caller owns that - under the C99 rules rather than the ECMAScript ones the
 * three entry points above follow. nConversion is one of 'e', 'E', 'f', 'F',
 * 'g' or 'G', nPrecision is the conversion precision or -1 for the default,
 * and bAlt is the # flag. Exists for the hand-written formatter in xx_rt.c,
 * whose platform has no printf to fall back on. Returns the number of
 * characters the conversion produces, snprintf-style.                       */
XXFC_API int xx_rt_dtoa_cformat(double nValue, char nConversion, int nPrecision, int bAlt, char *pBuffer, size_t nBufferSize);

/* ---------------------------------------------------------------- clock --- */

/* Milliseconds counted from an unspecified fixed point, non-decreasing for
 * the lifetime of the process. Only differences of two readings mean
 * anything; the profiling trace and the archive-scan budget are the callers.  */
XXFC_API long long xx_rt_clock_ms(void);

/* ---------------------------------------------------------- entry point --- */

/* The program entry point. On Windows the real entry is provided by
 * utils_entry.c, which builds argv from GetCommandLineA and calls this; the
 * C runtime is not involved. Elsewhere main() forwards to it.              */
XXFC_API int xx_rt_main(int nArgc, char *ppArgv[]);

#endif /* XXFCLIB_XX_RT_H */
