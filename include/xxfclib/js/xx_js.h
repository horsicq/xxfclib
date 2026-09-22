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
 * @file xx_js.h
 * @brief Embedded ECMAScript interpreter.
 *
 * A practical ES5 subset: objects, prototypes, closures, exceptions, regular
 * expressions and the standard Object/Function/Array/String/Number/Boolean/
 * Math/JSON globals.
 *
 * Strings are byte strings. Source is treated as Latin-1/UTF-8 pass-through,
 * which is how the Detect It Easy signature database is written, and why the
 * engine builds its strings on xx_buf_t rather than a NUL-delimited builder -
 * a script can legitimately construct a byte sequence containing zeroes.
 *
 * @par Provenance
 * Adapted from the JavaScript engine of cdie (github.com/horsicq, MIT).
 */

#ifndef XXFCLIB_XX_JS_H
#define XXFCLIB_XX_JS_H

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/xxfc_defs.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JSCtx JSCtx;
typedef struct JSStr JSStr;
typedef struct JSObj JSObj;
typedef struct JSNode JSNode;

typedef enum { JT_UNDEF = 0, JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ } JSTag;

typedef struct {
    JSTag tag;
    union {
        int b;
        double n;
        JSStr *s;
        JSObj *o;
    } u;
} JSVal;

/* ------------------------------------------------------------ lifecycle  */

XXFC_API JSCtx *js_new(void);
XXFC_API void js_free(JSCtx *pCtx);

/* Evaluates a program in the global scope. Returns 0 on error and stores the
 * message (owned by the context) in *ppError.                              */
XXFC_API int js_eval(JSCtx *pCtx, const char *pSource, const char *pName, JSVal *pResult);

/* Same as js_eval but keeps a pending exception intact; used for nested
 * evaluation such as the DIE includeScript() helper.                       */
XXFC_API int js_eval_nested(JSCtx *pCtx, const char *pSource, const char *pName);

/* Last error string; valid until the next evaluation. */
XXFC_API const char *js_error(JSCtx *pCtx);
XXFC_API void js_clear_error(JSCtx *pCtx);

/* User data pointer carried by the context (used by the DIE bindings). */
XXFC_API void js_set_user(JSCtx *pCtx, void *pUser);
XXFC_API void *js_get_user(JSCtx *pCtx);

/* ---------------------------------------------------------------- values  */

XXFC_API JSVal js_undefined(void);
XXFC_API JSVal js_null(void);
XXFC_API JSVal js_bool(int bValue);
XXFC_API JSVal js_num(double nValue);
XXFC_API JSVal js_int(int64_t nValue);
XXFC_API JSVal js_str(JSCtx *pCtx, const char *pString);
XXFC_API JSVal js_strn(JSCtx *pCtx, const char *pString, size_t nSize);

XXFC_API JSVal js_dup(JSVal value);
XXFC_API void js_release(JSCtx *pCtx, JSVal value);

XXFC_API int js_is_undefined(JSVal value);
XXFC_API int js_is_callable(JSVal value);

XXFC_API const char *js_str_data(JSVal value);
XXFC_API size_t js_str_len(JSVal value);

/* Conversions (the string variants return an owned JSVal of type JT_STR). */
XXFC_API double js_to_number(JSCtx *pCtx, JSVal value);
XXFC_API int js_to_bool(JSCtx *pCtx, JSVal value);
XXFC_API JSVal js_to_string(JSCtx *pCtx, JSVal value);
XXFC_API int64_t js_to_int64(JSCtx *pCtx, JSVal value);
XXFC_API int32_t js_to_int32(JSCtx *pCtx, JSVal value);

/* Convenience: returns a freshly allocated C string. */
XXFC_API char *js_to_cstr(JSCtx *pCtx, JSVal value);

/* --------------------------------------------------------------- objects  */

XXFC_API JSVal js_new_object(JSCtx *pCtx);
XXFC_API JSVal js_new_array(JSCtx *pCtx);

typedef JSVal (*JSNativeFn)(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser);

XXFC_API JSVal js_new_native(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, void *pUser);

XXFC_API JSVal js_get(JSCtx *pCtx, JSVal object, const char *pKey);
XXFC_API void js_set(JSCtx *pCtx, JSVal object, const char *pKey, JSVal value);
XXFC_API int js_has(JSCtx *pCtx, JSVal object, const char *pKey);

XXFC_API JSVal js_get_index(JSCtx *pCtx, JSVal object, int64_t nIndex);
XXFC_API void js_set_index(JSCtx *pCtx, JSVal object, int64_t nIndex, JSVal value);

XXFC_API int64_t js_array_length(JSCtx *pCtx, JSVal array);
XXFC_API void js_array_push(JSCtx *pCtx, JSVal array, JSVal value);

XXFC_API JSVal js_global(JSCtx *pCtx);

/* Calls a callable value. Returns an owned value; on exception the context
 * error flag is set and undefined is returned.                             */
XXFC_API JSVal js_call(JSCtx *pCtx, JSVal fn, JSVal thisVal, int nArgc, JSVal *pArgv);

/* Registers a native function on the global object. */
XXFC_API void js_def_fn(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, void *pUser);
/* Registers a native method on an object. */
XXFC_API void js_def_method(JSCtx *pCtx, JSVal object, const char *pName, JSNativeFn fn, int nArgc, void *pUser);

/* Raises a JavaScript exception; always returns undefined. */
XXFC_API XX_RT_PRINTF_LIKE(2, 3) JSVal js_throw(JSCtx *pCtx, const char *pFormat, ...);
XXFC_API int js_has_exception(JSCtx *pCtx);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_JS_H */
