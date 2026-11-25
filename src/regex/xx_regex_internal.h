/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_REGEX_INTERNAL_H
#define XX_REGEX_INTERNAL_H

#include "xxfclib/regex/xx_regex.h"

/* Private compile-time allocation policy. Every callback must allocate from
 * the xx_rt-compatible heap: compiled programs and error strings are released
 * with xx_rt_free. Callbacks may return NULL or enforce an adapter's OOM policy.
 * The table is borrowed only during compile and is never stored in a program. */
typedef struct xx_regex_allocator {
    void *(*malloc_fn)(size_t size);
    void *(*calloc_fn)(size_t count, size_t size);
    void *(*realloc_fn)(void *pointer, size_t size);
} xx_regex_allocator;

/* NULL selects the public core's allocator. A non-NULL table must supply all
 * three callbacks. This bridge is private and introduces no JS dependency. */
xx_regex *xx_regex_compile_with_allocator(const char *pattern, const char *flags, char **error, const xx_regex_allocator *allocator);

xx_regex *xx_regex_compile_n_with_allocator(const char *pattern, size_t pattern_size, const char *flags, size_t flags_size, char **error,
                                            const xx_regex_allocator *allocator);

#endif /* XX_REGEX_INTERNAL_H */
