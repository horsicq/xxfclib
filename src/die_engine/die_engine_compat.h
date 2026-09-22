/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file die_engine_compat.h
 * @brief The cdie runtime surface, expressed on xxfclib primitives.
 *
 * The scan engine and its format parsers arrived from cdie, where they sat on
 * cdie's own `x_*` runtime and `cd_*` containers. Both of those have xxfclib
 * counterparts, but they are NOT uniformly interchangeable, and cdie's entire
 * acceptance test is that it prints byte for byte what the reference `diec`
 * prints. So the mapping is made deliberately, one primitive at a time:
 *
 *   - Where the two implementations were verified byte-identical, this header
 *     simply aliases the cdie spelling onto xxfclib's function. One
 *     implementation, two names, no drift. That covers the whole string /
 *     memory / printf / math / clock family.
 *
 *   - Where the semantics genuinely differ, cdie's version is kept here and
 *     the difference is documented at the point of divergence. Silently
 *     adopting xxfclib's variant would change printed output.
 *
 * The three that differ, and why they are not aliased:
 *
 *   x_qsort     xxfclib's xx_rt_qsort is an in-place heapsort; cdie's is a
 *               median-of-three quicksort finishing with insertion sort.
 *               Heapsort is not stable, so it permutes equal keys differently.
 *               db.c's compare_signatures returns 0 for genuinely distinct
 *               signatures, and the note above it records that the comparator
 *               was hand-tuned against this exact partitioning -- retuning it
 *               moved script order *further* from diec. Signature order picks
 *               the winning signature, which picks the printed line.
 *
 *   cd_malloc   never returns NULL: it longjmp-free aborts through the OOM
 *               path instead, and cd_malloc(0) yields a one-byte block. The
 *               ~380 call sites are written against that contract and do not
 *               check. xx_mem_alloc returns NULL both on failure and for a
 *               zero size.
 *
 *   cdbuf_*     cdbuf_detach never returns NULL; xx_buf_detach returns NULL
 *               for a buffer whose sticky failure flag is set. The 48 detach
 *               sites here do not check. (That same mismatch already produced
 *               a latent defect in the migrated lexer, which pairs a non-zero
 *               size with a possibly-NULL pointer.)
 *
 * Everything in this header is private to src/die_engine/.
 */

#ifndef DIE_ENGINE_COMPAT_H
#define DIE_ENGINE_COMPAT_H

#include "xxfclib/rt/xx_rt.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------- scalars -- */

typedef int8_t cd_i8;
typedef uint8_t cd_u8;
typedef int16_t cd_i16;
typedef uint16_t cd_u16;
typedef int32_t cd_i32;
typedef uint32_t cd_u32;
typedef int64_t cd_i64;
typedef uint64_t cd_u64;

#define CD_TRUE 1
#define CD_FALSE 0

/* --------------------------------------------- runtime, aliased 1:1 ------ */

/* Verified byte-identical between cdie's utils.c and xxfclib's xx_rt_*: the
 * bodies differ only in the include line. Aliased rather than duplicated. */
#define x_strlen xx_rt_strlen
#define x_strcmp xx_rt_strcmp
#define x_strncmp xx_rt_strncmp
#define x_strncpy xx_rt_strncpy
#define x_strchr xx_rt_strchr
#define x_strrchr xx_rt_strrchr
#define x_strstr xx_rt_strstr
#define x_memcpy xx_rt_memcpy
#define x_memmove xx_rt_memmove
#define x_memset xx_rt_memset
#define x_memcmp xx_rt_memcmp
#define x_snprintf xx_rt_snprintf
#define x_vsnprintf xx_rt_vsnprintf
#define x_printf xx_rt_printf
#define x_fprintf xx_rt_fprintf
#define x_fflush xx_rt_fflush
#define x_stdout xx_rt_stdout
#define x_stderr xx_rt_stderr
#define x_getenv xx_rt_getenv
#define x_strtoull xx_rt_strtoull
#define x_strtol xx_rt_strtol
#define x_strtod xx_rt_strtod
#define x_clock_ms xx_rt_clock_ms
#define x_log xx_rt_log
#define x_pow xx_rt_pow
#define x_floor xx_rt_floor
#define x_ceil xx_rt_ceil
#define x_fabs xx_rt_fabs
#define x_sqrt xx_rt_sqrt
#define x_dtoa_fixed xx_rt_dtoa_fixed
#define x_dtoa_shortest xx_rt_dtoa_shortest
#define x_dtoa_precision xx_rt_dtoa_precision

#define x_malloc xx_rt_malloc
#define x_calloc xx_rt_calloc
#define x_realloc xx_rt_realloc
#define x_free xx_rt_free
#define x_exit xx_rt_exit
#define X_NORETURN XX_RT_NORETURN

#define x_fopen xx_rt_fopen
#define x_fclose xx_rt_fclose
#define x_fread xx_rt_fread
#define x_fseek xx_rt_fseek
#define x_ftell xx_rt_ftell
#define x_rewind xx_rt_rewind
#define X_SEEK_SET XX_RT_SEEK_SET
#define X_SEEK_CUR XX_RT_SEEK_CUR
#define X_SEEK_END XX_RT_SEEK_END

#define X_VA_LIST XX_RT_VA_LIST
#define X_VA_START XX_RT_VA_START
#define X_VA_END XX_RT_VA_END
#define X_VA_COPY XX_RT_VA_COPY
#define X_PRINTF_LIKE XX_RT_PRINTF_LIKE

/* cdie's src/global.h. Printed verbatim by the script API's DIE version
 * accessor, so it is a string the acceptance test compares. */
#ifndef X_APPLICATIONVERSION
#define X_APPLICATIONVERSION "4.0.0"
#endif

/* ------------------------------------------------------------ sorting  -- */

/**
 * @brief cdie's sort: median-of-three quicksort, insertion sort under 13.
 *
 * Deliberately NOT xx_rt_qsort. See the header comment: the signature
 * comparator is tuned to this partitioning, and changing it reorders the
 * signature database and therefore the printed result.
 */
void x_qsort(void *pBase, size_t nCount, size_t nSize,
             int (*fnCompare)(const void *, const void *));

/* --------------------------------------------------------- allocation  -- */

/* The soft-OOM window. Within it an allocation failure sets a sticky flag and
 * unwinds through the normal error paths instead of taking the process down;
 * outside it, failure is fatal. The engine opens one window per scan.
 *
 * NOTE this is a SECOND window, independent of the one the JS interpreter
 * keeps in src/js. A failure raised on the JS side is not visible to
 * cd_alloc_oom() here, and vice versa. In cdie both layers shared one window
 * because both allocated through cd_malloc. Splitting them is a behaviour
 * change confined to out-of-memory handling; it does not affect any
 * successful scan. */
int cd_alloc_begin_soft_oom(void);
void cd_alloc_end_soft_oom(void);
int cd_alloc_oom(void);

/* Never return NULL. A zero size yields a one-byte block, as the CRT does. */
void *cd_malloc(size_t nSize);
void *cd_calloc(size_t nCount, size_t nSize);
void *cd_realloc(void *pBlock, size_t nSize);
void cd_free(void *pBlock);
char *cd_strdup(const char *pText);
char *cd_strndup(const char *pText, size_t nLength);

/* May return NULL, and set the soft-OOM flag when they do. */
void *cd_try_malloc(size_t nSize);
void *cd_try_calloc(size_t nCount, size_t nSize);
void *cd_try_realloc(void *pBlock, size_t nSize);

/* ------------------------------------------------------------- CDBuf ---- */

/**
 * @brief A growable byte buffer. Binary safe: a zero byte is a value.
 *
 * Deliberately not xx_buf_t -- see the header comment on cdbuf_detach.
 */
typedef struct {
    char *pData;
    size_t nSize;
    size_t nCapacity;
} CDBuf;

void cdbuf_init(CDBuf *pBuf);
void cdbuf_free(CDBuf *pBuf);
void cdbuf_reserve(CDBuf *pBuf, size_t nCapacity);
void cdbuf_clear(CDBuf *pBuf);
void cdbuf_append(CDBuf *pBuf, const void *pData, size_t nSize);
void cdbuf_append_str(CDBuf *pBuf, const char *pText);
void cdbuf_append_ch(CDBuf *pBuf, char nChar);
X_PRINTF_LIKE(2, 3) void cdbuf_appendf(CDBuf *pBuf, const char *pFormat, ...);
/** @brief Take ownership of the bytes. Never NULL; always NUL-terminated. */
char *cdbuf_detach(CDBuf *pBuf, size_t *pnSize);

/* -------------------------------------------------------------- CDVec --- */

/**
 * @brief A growable array of pointers.
 *
 * xxfclib's xx_list_t stores elements by value and would turn each of the 145
 * sites here into an address-of/cast pair. Kept as-is: it is 40 lines.
 */
typedef struct {
    void **ppData;
    size_t nSize;
    size_t nCapacity;
} CDVec;

void cdvec_init(CDVec *pVec);
void cdvec_free(CDVec *pVec);
void cdvec_push(CDVec *pVec, void *pItem);
void cdvec_clear(CDVec *pVec);

/* ------------------------------------------------------------- string --- */

/** @brief ASCII-only case-insensitive compare. Does not guard NULL. */
int cd_stricmp_ascii(const char *pLeft, const char *pRight);

#endif /* DIE_ENGINE_COMPAT_H */
