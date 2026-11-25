/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SORT_INTERNAL_H
#define XX_SORT_INTERNAL_H

#include "xxfclib/sort/xx_sort.h"
#include <stdint.h> /* PTRDIFF_MAX */

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_SORT_X86 1
#endif

#if !defined(_M_ARM64EC) && (defined(_M_ARM64) || defined(__aarch64__))
#define XX_SORT_ARM_NEON 1
#endif

typedef void (*xx_sort_swap_fn)(char *pLeft, char *pRight, size_t nSize);
typedef void (*xx_sort_copy_fn)(uint8_t *pDestination, const uint8_t *pSource, size_t nSize);

typedef struct xx_sort_movement_s {
    xx_sort_swap_fn swap;
    xx_sort_copy_fn copy;
} xx_sort_movement;

/* Select once for a sorting operation; honors the global CPU switches. */
xx_sort_movement xx_sort_select_movement(size_t nSize);

#define XX_SORT_DECLARE_BACKEND(suffix)                                                                                            \
    void xx_sort_swap_##suffix(char *pLeft, char *pRight, size_t nSize);                                                           \
    void xx_sort_context_##suffix(void *pBase, size_t nCount, size_t nSize, xx_sort_compare_context_fn fnCompare, void *pContext); \
    void xx_sort_quick_##suffix(void *pBase, size_t nCount, size_t nSize, xx_sort_compare_fn fnCompare);                           \
    void xx_sort_stable_##suffix(void *pBase, size_t nCount, size_t nSize, xx_sort_compare_fn fnCompare)

XX_SORT_DECLARE_BACKEND(scalar);
#ifdef XX_SORT_X86
XX_SORT_DECLARE_BACKEND(sse2);
XX_SORT_DECLARE_BACKEND(avx2);
#endif
#ifdef XX_SORT_ARM_NEON
XX_SORT_DECLARE_BACKEND(neon);
#endif
#undef XX_SORT_DECLARE_BACKEND

#endif /* XX_SORT_INTERNAL_H */
