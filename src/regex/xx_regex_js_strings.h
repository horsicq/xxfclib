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

/* Private JavaScript adapter declarations. The public regex core has no
 * dependency on interpreter values or builtin registration. */
#ifndef XX_REGEX_JS_STRINGS_H
#define XX_REGEX_JS_STRINGS_H

#include "../js/xx_js_internal.h"

/* General String primitives shared with the non-regex builtin methods. */
JSVal js_builtin_string_value(JSCtx *pCtx, JSVal thisVal);
/* xx_regex_exec with a memory failure or an overlong subject raised as a
 * RangeError; a spent backtracking budget stays "no match". */
int js_regex_exec_checked(JSCtx *pCtx, const xx_regex *pRegExp, const char *pText, size_t nSize, size_t nStart, int32_t *pCaps);
int64_t js_builtin_find_substring(JSCtx *pCtx, const char *pText, size_t nTextSize, const char *pNeedle,
                                  size_t nNeedleSize, size_t nStart);

JSVal js_regex_string_split(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser);
JSVal js_regex_string_match(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser);
JSVal js_regex_string_search(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser);
JSVal js_regex_string_replace(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser);

#endif
