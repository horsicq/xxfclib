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

/** Reusable byte-oriented regular expression engine, shared by the JS adapter.
 * Compiled expressions are immutable and may be shared between threads when
 * each execution uses its own capture array. Text lengths and offsets are bytes;
 * matching retains the existing JavaScript byte/Latin-1 interpretation. */
#ifndef XXFCLIB_REGEX_XX_REGEX_H
#define XXFCLIB_REGEX_XX_REGEX_H
#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_regex xx_regex;

/* NUL-terminated pattern and flags (g, i, m); NULL means empty. Returns NULL
 * on invalid syntax or allocation failure. Optional error is owned by caller
 * and released with xx_rt_free. This module does not depend on the JS runtime. */
XXFC_API xx_regex *xx_regex_compile(const char *pattern, const char *flags, char **error);
/* Counted pattern may contain NUL bytes. Flags are counted and must contain
 * each of g, i, m at most once; unknown/duplicate flags are syntax errors. */
XXFC_API xx_regex *xx_regex_compile_n(const char *pattern, size_t pattern_size, const char *flags,
                                     size_t flags_size, char **error);
/* Optional byte-scanner dialect. Existing compile functions keep their JS
 * byte/Latin-1 behavior. RAW_BYTES gives every subject byte its own position. */
enum { XX_REGEX_RAW_BYTES = 1u, XX_REGEX_DOTALL = 2u };
XXFC_API xx_regex *xx_regex_compile_n_options(const char *pattern, size_t pattern_size,
    const char *flags, size_t flags_size, unsigned options, char **error);
XXFC_API xx_regex *xx_regex_ref(xx_regex *regex);
XXFC_API void xx_regex_free(xx_regex *regex);
XXFC_API int xx_regex_ngroups(const xx_regex *regex);
XXFC_API int xx_regex_global(const xx_regex *regex);
XXFC_API int xx_regex_ignorecase(const xx_regex *regex);
XXFC_API int xx_regex_multiline(const xx_regex *regex);
XXFC_API const char *xx_regex_source(const xx_regex *regex);
XXFC_API const char *xx_regex_flags(const xx_regex *regex);
XXFC_API size_t xx_regex_source_size(const xx_regex *regex);
XXFC_API size_t xx_regex_flags_size(const xx_regex *regex);
XXFC_API size_t xx_regex_memory_size(const xx_regex *regex);
/* Search at/after start. caps must have 2*(ngroups+1) int32_t entries: full
 * match first, then groups; -1 for nonparticipating groups. Returns 1 for a
 * match, 0 for no match/invalid input/budget exhaustion. Text is counted and
 * may contain NUL bytes; text_size must fit INT32_MAX. No shared lastIndex. */
XXFC_API int xx_regex_exec(const xx_regex *regex, const char *text, size_t text_size,
                           size_t start, int32_t *caps);
typedef enum xx_regex_exec_status {
    XX_REGEX_EXEC_MATCH = 1,
    XX_REGEX_EXEC_NO_MATCH = 0,
    XX_REGEX_EXEC_INVALID = -1,
    XX_REGEX_EXEC_BUDGET = -2,
    XX_REGEX_EXEC_NO_MEMORY = -3,
    XX_REGEX_EXEC_CANCELLED = -4
} xx_regex_exec_status;
typedef bool (*xx_regex_abort_fn)(void *user);
/* Same matching rules as exec, with distinct failure outcomes. An optional
 * synchronous callback returns true to abort. It is polled between bounded
 * matcher work and at most 64 KiB native search chunks. It must not modify the
 * subject, expression or captures. On valid inputs all non-match outcomes clear
 * captures to -1. NULL callback retains the legacy native search fast paths. */
XXFC_API xx_regex_exec_status xx_regex_exec_ex(const xx_regex *regex, const char *text, size_t text_size,
    size_t start, int32_t *caps, xx_regex_abort_fn should_abort, void *abort_user);
enum {
    XX_REGEX_ANCHORED = 1u, XX_REGEX_ENDANCHORED = 2u,
    XX_REGEX_NOTBOL = 4u, XX_REGEX_NOTEOL = 8u
};
/* Path control returns 0 to continue, positive to reject the current path and
 * backtrack, or negative to cancel the search. Captures retain byte offsets. */
typedef int (*xx_regex_path_fn)(void *user, size_t start, size_t position);
XXFC_API xx_regex_exec_status xx_regex_exec_controlled(const xx_regex *regex,
    const char *text, size_t text_size, size_t start, int32_t *caps, unsigned options,
    xx_regex_abort_fn should_abort, void *abort_user, xx_regex_path_fn path, void *path_user);
#ifdef __cplusplus
}
#endif
#endif
