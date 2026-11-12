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

/* JavaScript RegExp object adapter for the shared regex engine. */
#include "../js/xx_js_internal.h"
#include "xx_regex_internal.h"
#include "xx_regex_js_strings.h"

/* Keep the interpreter's emergency reserve, sticky OOM flag and non-NULL
 * allocation contract while the shared core remains independent of JS. */
JSRegExp *js_compile_regexp_n(const char *pattern, size_t pattern_size, const char *flags, size_t flags_size, char **error)
{
    static const xx_regex_allocator allocator = {
        xx_js_malloc, xx_js_calloc, xx_js_realloc
    };
    return xx_regex_compile_n_with_allocator(pattern, pattern_size, flags, flags_size, error, &allocator);
}

JSRegExp *js_compile_regexp(const char *pattern, const char *flags, char **error)
{
    return js_compile_regexp_n(pattern, pattern ? xx_rt_strlen(pattern) : 0,
                              flags, flags ? xx_rt_strlen(flags) : 0, error);
}

/* ------------------------------------------------- JavaScript RegExp API  */

static JSVal fn_regexp_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal pattern = js_undefined();
    JSVal flags = js_undefined();
    JSVal result = js_undefined();
    JSRegExp *pExisting = nArgc > 0 ? js_get_regexp(pArgv[0]) : NULL;
    int bFlagsUndefined = nArgc < 2 || pArgv[1].tag == JT_UNDEF;

    (void)thisVal;
    (void)pUser;

    if (pExisting && bFlagsUndefined && !pCtx->bNativeConstruct) {
        return js_dup(pArgv[0]);
    }
    if (pExisting) {
        pattern = js_strn(pCtx, xx_regex_source(pExisting), xx_regex_source_size(pExisting));
    } else if (nArgc == 0 || pArgv[0].tag == JT_UNDEF) {
        pattern = js_str(pCtx, "");
    } else {
        pattern = js_to_string(pCtx, pArgv[0]);
    }
    if (pCtx->bException) goto done;

    if (!bFlagsUndefined) {
        flags = js_to_string(pCtx, pArgv[1]);
    } else if (pExisting) {
        flags = js_strn(pCtx, xx_regex_flags(pExisting), xx_regex_flags_size(pExisting));
    } else {
        flags = js_str(pCtx, "");
    }
    if (!pCtx->bException) {
        result = js_new_regexp_n_val(pCtx, js_str_data(pattern), js_str_len(pattern),
                                    js_str_data(flags), js_str_len(flags));
    }

done:
    js_release(pCtx, pattern);
    js_release(pCtx, flags);
    return result;
}

static JSVal fn_regexp_test(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSRegExp *pRegExp = js_get_regexp(thisVal);
    JSVal text;
    int32_t sCaps[32];
    int32_t *pCaps = NULL;
    int bResult = 0;
    size_t nStart = 0;

    (void)pUser;

    if (pRegExp == NULL) {
        return js_throw(pCtx, "TypeError: RegExp.prototype.test on a non-RegExp");
    }

    text = js_to_string(pCtx, (nArgc > 0) ? pArgv[0] : js_undefined());
    if (pCtx->bException) {
        js_release(pCtx, text);
        return js_undefined();
    }
    if (xx_regex_global(pRegExp)) {
        JSVal lastIndex = js_get(pCtx, thisVal, "lastIndex");
        int64_t nLastIndex = js_to_int64(pCtx, lastIndex);
        js_release(pCtx, lastIndex);
        if (pCtx->bException) {
            js_release(pCtx, text);
            return js_undefined();
        }
        if (nLastIndex > 0) nStart = (size_t)nLastIndex;
    }
    /* Most detection patterns need only the whole match and a few captures.
     * Keep the common case off the heap; larger patterns retain full captures. */
    pCaps = xx_regex_ngroups(pRegExp) < 16 ? sCaps :
        (int32_t *)xx_js_malloc((size_t)(xx_regex_ngroups(pRegExp) + 1) * 2 * sizeof(int32_t));
    bResult = js_regex_exec_checked(pCtx, pRegExp, js_str_data(text), js_str_len(text), nStart, pCaps);
    if (xx_regex_global(pRegExp)) js_set(pCtx, thisVal, "lastIndex", js_num(bResult ? (double)pCaps[1] : 0));
    if (pCaps != sCaps) xx_js_free(pCaps);
    js_release(pCtx, text);

    return js_bool(bResult);
}

static JSVal fn_regexp_exec(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSRegExp *pRegExp = js_get_regexp(thisVal);
    JSVal text;
    int32_t sCaps[32];
    int32_t *pCaps = NULL;
    JSVal result = js_null();
    size_t nStart = 0;

    (void)pUser;

    if (pRegExp == NULL) {
        return js_throw(pCtx, "TypeError: RegExp.prototype.exec on a non-RegExp");
    }

    text = js_to_string(pCtx, (nArgc > 0) ? pArgv[0] : js_undefined());
    if (pCtx->bException) {
        js_release(pCtx, text);
        return js_undefined();
    }

    if (xx_regex_global(pRegExp)) {
        JSVal lastIndex = js_get(pCtx, thisVal, "lastIndex");
        int64_t nLastIndex = js_to_int64(pCtx, lastIndex);

        js_release(pCtx, lastIndex);
        if (pCtx->bException) {
            js_release(pCtx, text);
            return js_undefined();
        }

        if (nLastIndex > 0) {
            nStart = (size_t)nLastIndex;
        }
    }

    /* Most detection patterns need only the whole match and a few captures.
     * Keep the common case off the heap; larger patterns retain full captures. */
    pCaps = xx_regex_ngroups(pRegExp) < 16 ? sCaps :
        (int32_t *)xx_js_malloc((size_t)(xx_regex_ngroups(pRegExp) + 1) * 2 * sizeof(int32_t));

    if ((nStart <= js_str_len(text)) && js_regex_exec_checked(pCtx, pRegExp, js_str_data(text), js_str_len(text), nStart, pCaps)) {
        int g = 0;

        result = js_new_array(pCtx);

        for (g = 0; g <= xx_regex_ngroups(pRegExp); g++) {
            if (pCaps[g * 2] >= 0) {
                js_set_index(pCtx, result, g, js_strn(pCtx, js_str_data(text) + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2])));
            } else {
                js_set_index(pCtx, result, g, js_undefined());
            }
        }

        result.u.o->nArrayLen = xx_regex_ngroups(pRegExp) + 1;
        jsobj_put_hidden_static(pCtx, result.u.o, "index", js_num((double)pCaps[0]));
        jsobj_put_hidden_static(pCtx, result.u.o, "input", js_dup(text));

        if (xx_regex_global(pRegExp)) {
            js_set(pCtx, thisVal, "lastIndex", js_num((double)pCaps[1]));
        }
    } else if (xx_regex_global(pRegExp)) {
        js_set(pCtx, thisVal, "lastIndex", js_num(0));
    }

    if (pCaps != sCaps) xx_js_free(pCaps);
    js_release(pCtx, text);

    return result;
}

/* A regex source property must remain a valid literal body, even when its
 * constructor received an empty string, a raw slash or a line terminator. */
static JSVal regexp_display_source(JSCtx *pCtx, const JSRegExp *pRegExp)
{
    const char *pSource = xx_regex_source(pRegExp);
    size_t nSize = xx_regex_source_size(pRegExp), i;
    int bEscaped = 0;
    xx_buf_t buf;
    JSVal result;
    if (nSize == 0) return js_str(pCtx, "(?:)");
    for (i = 0; i < nSize; i++) {
        char ch = pSource[i];
        if ((ch == '/' && !bEscaped) || ch == '\n' || ch == '\r') break;
        bEscaped = ch == '\\' ? !bEscaped : 0;
    }
    if (i == nSize) return js_strn(pCtx, pSource, nSize);
    bEscaped = 0;
    xx_buf_init(&buf);
    for (i = 0; i < nSize; i++) {
        char ch = pSource[i];
        if (ch == '/' && !bEscaped) xx_buf_append_char(&buf, '\\');
        if (ch == '\n' || ch == '\r') {
            xx_buf_append_str(&buf, ch == '\n' ? "\\n" : "\\r");
        } else {
            xx_buf_append_char(&buf, ch);
        }
        bEscaped = ch == '\\' ? !bEscaped : 0;
    }
    result = js_strn(pCtx, buf.data, buf.size);
    xx_buf_free(&buf);
    return result;
}

static JSVal regexp_display_flags(JSCtx *pCtx, const JSRegExp *pRegExp)
{
    char flags[3];
    size_t nSize = 0;
    if (xx_regex_global(pRegExp)) flags[nSize++] = 'g';
    if (xx_regex_ignorecase(pRegExp)) flags[nSize++] = 'i';
    if (xx_regex_multiline(pRegExp)) flags[nSize++] = 'm';
    return js_strn(pCtx, flags, nSize);
}

static JSVal fn_regexp_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSRegExp *pRegExp = js_get_regexp(thisVal);
    xx_buf_t buf;
    JSVal result, source, flags;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;
    if (pRegExp == NULL) return js_throw(pCtx, "TypeError: RegExp.prototype.toString on a non-RegExp");
    source = regexp_display_source(pCtx, pRegExp);
    flags = regexp_display_flags(pCtx, pRegExp);
    xx_buf_init(&buf);
    xx_buf_append_char(&buf, '/');
    xx_buf_append(&buf, js_str_data(source), js_str_len(source));
    xx_buf_append_char(&buf, '/');
    xx_buf_append(&buf, js_str_data(flags), js_str_len(flags));
    result = js_strn(pCtx, buf.data, buf.size);
    xx_buf_free(&buf);
    js_release(pCtx, source);
    js_release(pCtx, flags);
    return result;
}

JSRegExp *js_get_regexp(JSVal value)
{
    if ((value.tag == JT_OBJ) && (value.u.o->cls == JCLASS_REGEXP)) {
        return value.u.o->pRegExp;
    }

    return NULL;
}

JSVal js_new_regexp_shared_val(JSCtx *pCtx, JSRegExp *pRegExp)
{
    JSObj *pObj = jsobj_new(pCtx, JCLASS_REGEXP, pCtx->pRegExpProto);
    pObj->pRegExp = xx_regex_ref(pRegExp);

    jsobj_put_hidden_static(pCtx, pObj, "source", regexp_display_source(pCtx, pRegExp));
    jsobj_put_hidden_static(pCtx, pObj, "flags", regexp_display_flags(pCtx, pRegExp));
    jsobj_put_hidden_static(pCtx, pObj, "global", js_bool(xx_regex_global(pObj->pRegExp)));
    jsobj_put_hidden_static(pCtx, pObj, "ignoreCase", js_bool(xx_regex_ignorecase(pObj->pRegExp)));
    jsobj_put_hidden_static(pCtx, pObj, "multiline", js_bool(xx_regex_multiline(pObj->pRegExp)));
    jsobj_put_hidden_static(pCtx, pObj, "lastIndex", js_num(0));

    return jsval_obj(pObj);
}

JSVal js_new_regexp_n_val(JSCtx *pCtx, const char *pPattern, size_t nPatternSize, const char *pFlags, size_t nFlagsSize)
{
    char *pError = NULL;
    JSRegExp *pRegExp = js_compile_regexp_n(pPattern, nPatternSize, pFlags, nFlagsSize, &pError);
    JSVal result;
    if (!pRegExp) {
        result = js_throw(pCtx, "SyntaxError: Invalid regular expression: %s", pError ? pError : "");
        xx_js_free(pError);
        return result;
    }
    result = js_new_regexp_shared_val(pCtx, pRegExp);
    xx_regex_free(pRegExp);
    return result;
}

JSVal js_new_regexp_val(JSCtx *pCtx, const char *pPattern, const char *pFlags)
{
    return js_new_regexp_n_val(pCtx, pPattern, pPattern ? xx_rt_strlen(pPattern) : 0,
                              pFlags, pFlags ? xx_rt_strlen(pFlags) : 0);
}

void js_install_regexp(JSCtx *pCtx)
{
    JSVal ctor = js_new_native(pCtx, "RegExp", fn_regexp_ctor, 2, NULL);

    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "test", js_new_native(pCtx, "test", fn_regexp_test, 1, NULL));
    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "exec", js_new_native(pCtx, "exec", fn_regexp_exec, 1, NULL));
    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "toString", js_new_native(pCtx, "toString", fn_regexp_tostring, 0, NULL));

    jsobj_put_hidden(pCtx, ctor.u.o, "prototype", jsval_obj(jsobj_ref(pCtx->pRegExpProto)));
    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "constructor", js_dup(ctor));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, "RegExp", ctor);
}
