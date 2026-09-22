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
 * @file xx_js_deps.h
 * @brief Everything the JavaScript engine needs from the rest of the library.
 *
 * Private to src/js. The engine was written against a single header that
 * gathered its dependencies, and keeping that shape means the engine sources
 * stay close to the form they were tested in.
 *
 * @par The allocation contract
 * Around sixty allocation sites in the engine use the result without a NULL
 * check, and there is no unwind path to make them checkable cheaply. The
 * allocators declared here therefore NEVER return NULL, which is the contract
 * those call sites were written against.
 *
 * When an allocation cannot be satisfied they call the handler installed by
 * @ref xx_js_set_oom_handler. The default handler terminates the process,
 * which is right for a command-line tool and wrong inside a host application,
 * so an embedder is expected to install one that does not return - typically
 * a longjmp back to its own entry point.
 *
 * @ref xx_js_begin_soft_oom narrows the window: it takes an emergency reserve
 * up front, and the first failure releases that reserve and retries, so the
 * caller still receives real memory and the engine can wind down through
 * @ref xx_js_oom_raised instead of dying. A single request larger than the
 * reserve still reaches the handler.
 */

#ifndef XXFCLIB_XX_JS_DEPS_H
#define XXFCLIB_XX_JS_DEPS_H

#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Last-resort handler for an allocation that cannot be satisfied.
 *
 * Must not return. Returning from it leaves the engine about to dereference
 * NULL at a site that cannot check.
 */
typedef void (*xx_js_oom_handler_fn)(void);

/**
 * @brief Install the handler called when an allocation cannot be satisfied.
 *
 * Passing NULL restores the default, which writes a line to stderr and
 * terminates the process.
 *
 * @return The handler that was previously installed.
 */
XXFC_API xx_js_oom_handler_fn xx_js_set_oom_handler(xx_js_oom_handler_fn handler);

/**
 * @brief Take the emergency reserve for the duration of one evaluation.
 *
 * State is process-wide, so one evaluation at a time per process.
 *
 * @return true if the reserve is in place, false if even it could not be
 *         allocated - in which case the caller should not proceed.
 */
XXFC_API bool xx_js_begin_soft_oom(void);

/** @brief Release the emergency reserve and clear the raised flag. */
XXFC_API void xx_js_end_soft_oom(void);

/** @brief True once an allocation has had to fall back on the reserve. */
XXFC_API bool xx_js_oom_raised(void);

/* --- allocation: these never return NULL --- */

void *xx_js_malloc(size_t size);
void *xx_js_calloc(size_t count, size_t size);
void *xx_js_realloc(void *ptr, size_t size);
void xx_js_free(void *ptr);
char *xx_js_strdup(const char *text);
char *xx_js_strndup(const char *text, size_t size);

/**
 * @brief FNV-1a over a counted range.
 *
 * Used only to index the engine's open-addressed property tables. Nothing
 * outside a single run ever compares these values, and enumeration order does
 * not depend on them, so the exact function is not part of any contract.
 */
uint32_t xx_js_hash_str(const char *text, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_JS_DEPS_H */
