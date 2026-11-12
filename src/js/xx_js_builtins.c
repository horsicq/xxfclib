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

/* js_builtins.c - standard library: Object, Function, Array, String, Number,
 * Boolean, Math, JSON, RegExp and the global helper functions.             */

#include "xx_js_internal.h"
#include "../regex/xx_regex_js_strings.h"

#define ARG(i) (((i) < nArgc) ? pArgv[i] : js_undefined())

/* Upper bounds on the two places where a nominal array length is turned
 * into a single flat allocation.  Both are defensive: an array that really
 * holds this many elements has already cost far more memory in the property
 * map, so no realistic script is refused, while the multiplication by
 * sizeof(JSVal) can no longer wrap size_t and the count still fits an int. */
#define JS_MAX_SPREAD_ARGS 16777216
#define JS_MAX_SORT_ITEMS 16777216

/* ------------------------------------------------------------- utilities  */

JSVal js_builtin_string_value(JSCtx *pCtx, JSVal thisVal)
{
    if (thisVal.tag == JT_STR) {
        return js_dup(thisVal);
    }

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->cls == JCLASS_STRING)) {
        return js_dup(thisVal.u.o->primitive);
    }

    return js_to_string(pCtx, thisVal);
}

static double this_number(JSCtx *pCtx, JSVal thisVal)
{
    if (thisVal.tag == JT_NUM) {
        return thisVal.u.n;
    }

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->cls == JCLASS_NUMBER)) {
        return js_to_number(pCtx, thisVal.u.o->primitive);
    }

    return js_to_number(pCtx, thisVal);
}

static int64_t clamp_index(double nValue, int64_t nLength)
{
    int64_t nResult = 0;

    if (nValue != nValue) {
        return 0;
    }

    /* ToInteger truncates towards zero FIRST; only then is the sign tested.
     * Without this, -0.5 truncates to -0 but still takes the negative
     * branch and yields nLength instead of 0.                             */
    nValue = (nValue < 0) ? (-xx_rt_floor(-nValue)) : xx_rt_floor(nValue);

    /* The double has to be brought into range before the cast: converting a
     * value that does not fit into int64_t is undefined behaviour.          */
    if (nValue < 0) {
        if (nValue < (-(double)nLength)) {
            return 0;
        }

        nResult = nLength + (int64_t)nValue;

        if (nResult < 0) {
            nResult = 0;
        }
    } else {
        if (nValue > (double)nLength) {
            return nLength;
        }

        nResult = (int64_t)nValue;

        if (nResult > nLength) {
            nResult = nLength;
        }
    }

    return nResult;
}

/* ---------------------------------------------------------------- Object  */

static JSVal fn_object_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    if ((nArgc > 0) && (pArgv[0].tag == JT_OBJ)) {
        return js_dup(pArgv[0]);
    }

    return js_new_object(pCtx);
}

static JSVal fn_object_keys(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal object = ARG(0);

    (void)thisVal;
    (void)pUser;

    return (object.tag == JT_OBJ) ? jsobj_keys(pCtx, object.u.o) : js_new_array(pCtx);
}

static JSVal fn_object_hasownproperty(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal key = js_to_string(pCtx, ARG(0));
    int bResult = 0;

    (void)pUser;

    if (thisVal.tag == JT_OBJ) {
        int bFound = 0;
        JSVal value = jsobj_get_own(pCtx, thisVal.u.o, js_str_data(key), js_str_len(key), &bFound);

        js_release(pCtx, value);
        bResult = bFound;
    } else if (thisVal.tag == JT_STR) {
        int64_t nIndex = 0;

        if ((js_str_len(key) == 6) && (xx_rt_memcmp(js_str_data(key), "length", 6) == 0)) {
            bResult = 1;
        } else if (js_is_array_index(js_str_data(key), js_str_len(key), &nIndex)) {
            bResult = ((nIndex >= 0) && ((size_t)nIndex < js_str_len(thisVal))) ? 1 : 0;
        }
    }

    js_release(pCtx, key);

    return js_bool(bResult);
}

static JSVal fn_object_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->cls == JCLASS_ARRAY)) {
        JSVal join = js_get(pCtx, thisVal, "join");

        if (js_is_callable(join)) {
            JSVal result = js_call(pCtx, join, thisVal, 0, NULL);

            js_release(pCtx, join);

            return result;
        }

        js_release(pCtx, join);
    }

    return js_str(pCtx, "[object Object]");
}

static JSVal fn_object_valueof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_dup(thisVal);
}

static JSVal fn_object_defineproperty(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal object = ARG(0);
    JSVal key = js_to_string(pCtx, ARG(1));
    JSVal descriptor = ARG(2);

    (void)thisVal;
    (void)pUser;

    if ((object.tag == JT_OBJ) && (descriptor.tag == JT_OBJ)) {
        JSVal value = js_get(pCtx, descriptor, "value");
        JSVal enumerable = js_get(pCtx, descriptor, "enumerable");

        jsobj_put(pCtx, object.u.o, js_str_data(key), js_str_len(key), value);
        jsobj_set_enumerable(pCtx, object.u.o, js_str_data(key), js_str_len(key), js_to_bool(pCtx, enumerable));

        js_release(pCtx, enumerable);
    }

    js_release(pCtx, key);

    return js_dup(object);
}

/* -------------------------------------------------------------- Function  */

static JSVal fn_function_call(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal newThis = ARG(0);

    (void)pUser;

    if (!js_is_callable(thisVal)) {
        return js_throw(pCtx, "TypeError: Function.prototype.call on a non-function");
    }

    return js_call_function(pCtx, thisVal.u.o, newThis, (nArgc > 0) ? (nArgc - 1) : 0, (nArgc > 1) ? (pArgv + 1) : NULL, 0);
}

static JSVal fn_function_apply(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal newThis = ARG(0);
    JSVal args = ARG(1);
    JSVal localArgs[8];
    JSVal *pCallArgs = NULL;
    int64_t nCount = 0;
    int64_t i = 0;
    JSVal result;

    (void)pUser;

    if (!js_is_callable(thisVal)) {
        return js_throw(pCtx, "TypeError: Function.prototype.apply on a non-function");
    }

    if (args.tag == JT_OBJ) {
        nCount = js_array_length(pCtx, args);

        if (nCount < 0) {
            nCount = 0;
        }

        /* Keeps the allocation below from wrapping size_t and the argument
         * count from being truncated by the (int) cast further down.  The
         * bound is far above any real argument list - the reference engine
         * still spreads half a million elements - but low enough that the
         * multiplication cannot wrap and the block stays bounded.          */
        if (nCount > JS_MAX_SPREAD_ARGS) {
            return js_throw(pCtx, "RangeError: too many arguments");
        }

        if (nCount > 0) {
            pCallArgs = localArgs;

            if (nCount > (int64_t)(sizeof(localArgs) / sizeof(localArgs[0]))) {
                pCallArgs = (JSVal *)xx_js_malloc((size_t)nCount * sizeof(JSVal));
            }

            for (i = 0; i < nCount; i++) {
                pCallArgs[i] = js_get_index(pCtx, args, i);
                if (pCtx->bException) {
                    /* A getter threw: release what was read and give up. */
                    int64_t k;
                    for (k = 0; k <= i; k++) js_release(pCtx, pCallArgs[k]);
                    if (pCallArgs != localArgs) xx_js_free(pCallArgs);
                    return js_undefined();
                }
            }
        }
    }

    result = js_call_function(pCtx, thisVal.u.o, newThis, (int)nCount, pCallArgs, 0);

    for (i = 0; i < nCount; i++) {
        js_release(pCtx, pCallArgs[i]);
    }

    if (pCallArgs != localArgs) {
        xx_js_free(pCallArgs);
    }

    return result;
}

static JSVal fn_function_bind(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result;

    (void)pUser;

    if (!js_is_callable(thisVal)) {
        return js_throw(pCtx, "TypeError: Function.prototype.bind on a non-function");
    }

    result = js_new_native(pCtx, "bound", NULL, 0, NULL);
    result.u.o->pBoundTarget = jsobj_ref(thisVal.u.o);
    result.u.o->boundThis = js_dup(ARG(0));

    /* Everything after thisArg is partially applied and goes in front of the
     * arguments of the eventual call.                                      */
    if (nArgc > 1) {
        int i = 0;

        result.u.o->pBoundArgs = (JSVal *)xx_js_malloc((size_t)(nArgc - 1) * sizeof(JSVal));
        result.u.o->nBoundArgs = nArgc - 1;

        for (i = 1; i < nArgc; i++) {
            result.u.o->pBoundArgs[i - 1] = js_dup(pArgv[i]);
        }
    }

    return result;
}

static JSVal fn_function_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    char sBuf[256];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    xx_rt_snprintf(sBuf, sizeof(sBuf), "function %s() { [code] }", ((thisVal.tag == JT_OBJ) && thisVal.u.o->pFnName) ? thisVal.u.o->pFnName : "");

    return js_str(pCtx, sBuf);
}

/* A compact Uint32Array for bounded native primitive results. */
static JSVal fn_uint32array_ctor(JSCtx *ctx, JSVal thisVal, int argc, JSVal *argv, void *user)
{
    int64_t count = 0, i;
    JSVal result;
    (void)thisVal;
    (void)user;
    if (argc && argv[0].tag == JT_NUM) {
        double value = argv[0].u.n;
        if (!(value >= 0 && value <= 33554432.0) || (double)(int64_t)value != value) return js_throw(ctx, "RangeError: Uint32Array length exceeds native budget");
        count = (int64_t)value;
    } else if (argc && argv[0].tag == JT_OBJ) {
        count = js_array_length(ctx, argv[0]);
        if (count < 0 || count > 33554432) return js_throw(ctx, "RangeError: Uint32Array length exceeds native budget");
    } else if (argc && argv[0].tag != JT_UNDEF) return js_throw(ctx, "TypeError: Uint32Array requires length or array");
    result = js_new_uint32_array(ctx, NULL, (size_t)count);
    if (result.tag != JT_OBJ) return js_throw(ctx, "RangeError: Uint32Array allocation failed");
    if (argc && argv[0].tag == JT_OBJ)
        for (i = 0; i < count; ++i) {
            JSVal value = js_get_index(ctx, argv[0], i);
            result.u.o->pUint32Data[i] = (uint32_t)js_to_int32(ctx, value);
            js_release(ctx, value);
        }
    return result;
}

/* ----------------------------------------------------------------- Array  */

static JSVal fn_array_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result = js_new_array(pCtx);
    int i = 0;

    (void)thisVal;
    (void)pUser;

    if ((nArgc == 1) && (pArgv[0].tag == JT_NUM)) {
        double nValue = pArgv[0].u.n;

        /* Only a uint32 is a valid array length; anything else is a
         * RangeError and must never reach the (int64_t) conversion.        */
        if ((nValue != nValue) || (nValue < 0) || (nValue > 4294967295.0) || (nValue != xx_rt_floor(nValue))) {
            js_release(pCtx, result);

            /* Word for word what the reference engine reports. */
            return js_throw(pCtx, "RangeError: Array size is not a small enough positive integer.");
        }

        result.u.o->nArrayLen = (int64_t)nValue;

        return result;
    }

    for (i = 0; i < nArgc; i++) {
        js_set_index(pCtx, result, i, js_dup(pArgv[i]));
    }

    return result;
}

static JSVal fn_array_isarray(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal value = ARG(0);

    (void)pCtx;
    (void)thisVal;
    (void)pUser;

    return js_bool((value.tag == JT_OBJ) && (value.u.o->cls == JCLASS_ARRAY));
}

static JSVal fn_array_push(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    int i = 0;

    (void)pUser;

    for (i = 0; i < nArgc; i++) {
        js_set_index(pCtx, thisVal, nLength + i, js_dup(pArgv[i]));
    }

    return js_num((double)(nLength + nArgc));
}

static JSVal fn_array_pop(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    JSVal result;
    char sKey[32];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nLength <= 0) {
        return js_undefined();
    }

    result = js_get_index(pCtx, thisVal, nLength - 1);
    xx_rt_snprintf(sKey, sizeof(sKey), "%lld", (long long)(nLength - 1));

    if (thisVal.tag == JT_OBJ) {
        jsobj_del(pCtx, thisVal.u.o, sKey);
        if (thisVal.u.o->cls == JCLASS_ARRAY) thisVal.u.o->nArrayLen = nLength - 1;
    }

    return result;
}

static JSVal fn_array_shift(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    JSVal result;
    int64_t i = 0;
    char sKey[32];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nLength <= 0) {
        return js_undefined();
    }

    if (thisVal.tag == JT_OBJ && js_array_shift_dense(pCtx, thisVal.u.o, &result)) {
        return result;
    }

    result = js_get_index(pCtx, thisVal, 0);

    for (i = 1; i < nLength; i++) {
        js_set_index(pCtx, thisVal, i - 1, js_get_index(pCtx, thisVal, i));
    }

    xx_rt_snprintf(sKey, sizeof(sKey), "%lld", (long long)(nLength - 1));

    if (thisVal.tag == JT_OBJ) {
        jsobj_del(pCtx, thisVal.u.o, sKey);
        if (thisVal.u.o->cls == JCLASS_ARRAY) thisVal.u.o->nArrayLen = nLength - 1;
    }

    return result;
}

static JSVal fn_array_unshift(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    int64_t i = 0;

    (void)pUser;

    if (thisVal.tag == JT_OBJ && js_array_unshift_dense(pCtx, thisVal.u.o, nArgc, pArgv)) {
        return js_num((double)(nLength + nArgc));
    }

    for (i = nLength - 1; i >= 0; i--) {
        js_set_index(pCtx, thisVal, i + nArgc, js_get_index(pCtx, thisVal, i));
    }

    for (i = 0; i < nArgc; i++) {
        js_set_index(pCtx, thisVal, i, js_dup(pArgv[i]));
    }

    return js_num((double)(nLength + nArgc));
}

/* Append primitives without allocating temporary JS strings. Object conversion
 * stays on the ordinary ToString path, exactly once and in argument order. */
static int append_value_string(JSCtx *pCtx, xx_buf_t *pBuf, JSVal value)
{
    JSVal text;
    char sNumber[64];

    if (!xx_buf_ok(pBuf)) {
        return 0;
    }

    switch (value.tag) {
        case JT_STR: return xx_buf_append(pBuf, js_str_data(value), js_str_len(value));
        case JT_UNDEF: return xx_buf_append(pBuf, "undefined", 9);
        case JT_NULL: return xx_buf_append(pBuf, "null", 4);
        case JT_BOOL: return xx_buf_append(pBuf, value.u.b ? "true" : "false", value.u.b ? 4 : 5);
        case JT_NUM:
            if (!js_int_to_buf(sNumber, value.u.n)) {
                xx_rt_dtoa_shortest(value.u.n, sNumber, sizeof(sNumber));
            }
            return xx_buf_append_str(pBuf, sNumber);
        default: break;
    }

    text = js_to_string(pCtx, value);

    if (js_has_exception(pCtx)) {
        js_release(pCtx, text);
        return 0;
    }

    xx_buf_append(pBuf, js_str_data(text), js_str_len(text));
    js_release(pCtx, text);

    return xx_buf_ok(pBuf);
}

/* xx_buf already owns terminated storage. Transfer it rather than copying the
 * completed string a second time. This also rejects partial buffers on OOM. */
static JSVal string_from_buffer(JSCtx *pCtx, xx_buf_t *pBuf)
{
    size_t nCapacity = pBuf->capacity;
    size_t nSize = 0;
    char *pData = xx_buf_detach(pBuf, &nSize);

    if (!pData) {
        return js_throw(pCtx, "RangeError: string allocation failed");
    }

    return js_str_take(pCtx, pData, nSize, nCapacity ? nCapacity : 1);
}

/* Validate the whole range before borrowing it: primitive conversion cannot
 * call script, whereas a hole or object must retain the observable Get and
 * ToString order of the general path. Separator conversion has already run. */
static int array_join_dense_primitives(JSCtx *pCtx, JSVal array, int64_t nLength,
                                      const char *pSeparator, size_t nSeparatorSize, JSVal *pResult)
{
    JSObj *pArray;
    JSVal *pItems;
    size_t i, nCapacity = 0, nOffset = 0;
    char *pText;

    if (array.tag != JT_OBJ || array.u.o->cls != JCLASS_ARRAY) return 0;
    pArray = array.u.o;
    if (pArray->bArraySparse || pArray->nArrayLen != nLength ||
        pArray->nArrayUsed != (size_t)nLength || pArray->nArrayHoles) return 0;
    pItems = pArray->pArrayData ? pArray->pArrayData + pArray->nArrayStart : NULL;
    if (nLength > 1) {
        if (nSeparatorSize > ((size_t)-2) / (size_t)(nLength - 1)) goto too_large;
        nCapacity = nSeparatorSize * (size_t)(nLength - 1);
    }
    for (i = 0; i < (size_t)nLength; i++) {
        size_t nPiece;
        switch (pItems[i].tag) {
            case JT_UNDEF:
            case JT_NULL: nPiece = 0; break;
            case JT_BOOL: nPiece = pItems[i].u.b ? 4 : 5; break;
            case JT_NUM: nPiece = 64; break; /* includes a temporary terminator */
            case JT_STR: nPiece = pItems[i].u.s->nSize; break;
            default: return 0;
        }
        if (nPiece > (size_t)-2 - nCapacity) goto too_large;
        nCapacity += nPiece;
    }

    pText = (char *)xx_js_malloc(nCapacity + 1);
    for (i = 0; i < (size_t)nLength; i++) {
        JSVal item = pItems[i];
        const char *pPiece = NULL;
        size_t nPiece = 0;
        if (i && nSeparatorSize) {
            xx_rt_memcpy(pText + nOffset, pSeparator, nSeparatorSize);
            nOffset += nSeparatorSize;
        }
        switch (item.tag) {
            case JT_STR: pPiece = item.u.s->pData; nPiece = item.u.s->nSize; break;
            case JT_BOOL: pPiece = item.u.b ? "true" : "false"; nPiece = item.u.b ? 4 : 5; break;
            case JT_NUM: {
                int nDigits = js_int_to_buf(pText + nOffset, item.u.n);
                if (nDigits > 0) nPiece = (size_t)nDigits;
                else {
                    xx_rt_dtoa_shortest(item.u.n, pText + nOffset, 64);
                    nPiece = xx_rt_strlen(pText + nOffset);
                }
                nOffset += nPiece;
                continue;
            }
            default: break;
        }
        if (nPiece) {
            xx_rt_memcpy(pText + nOffset, pPiece, nPiece);
            nOffset += nPiece;
        }
    }
    pText[nOffset] = 0;
    *pResult = js_str_take(pCtx, pText, nOffset, nCapacity + 1);
    return 1;

too_large:
    *pResult = js_throw(pCtx, "RangeError: string allocation failed");
    return 1;
}

static JSVal fn_array_join(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    xx_buf_t buf;
    int64_t i = 0;
    JSVal separator = js_undefined();
    const char *pSeparator = ",";
    size_t nSeparatorSize = 1;

    (void)pUser;

    if ((nArgc > 0) && (pArgv[0].tag != JT_UNDEF)) {
        separator = js_to_string(pCtx, pArgv[0]);

        if (js_has_exception(pCtx)) {
            js_release(pCtx, separator);
            return js_undefined();
        }

        pSeparator = js_str_data(separator);
        nSeparatorSize = js_str_len(separator);
    }

    {
        JSVal result;
        if (array_join_dense_primitives(pCtx, thisVal, nLength, pSeparator, nSeparatorSize, &result)) {
            js_release(pCtx, separator);
            return result;
        }
    }

    xx_buf_init(&buf);

    for (i = 0; i < nLength; i++) {
        JSVal item = js_get_index(pCtx, thisVal, i);
        int bOkay = !js_has_exception(pCtx);

        if (bOkay && (i > 0)) {
            bOkay = xx_buf_append(&buf, pSeparator, nSeparatorSize);
        }

        if (bOkay && (item.tag != JT_UNDEF) && (item.tag != JT_NULL)) {
            bOkay = append_value_string(pCtx, &buf, item);
        }

        js_release(pCtx, item);

        if (!bOkay) {
            xx_buf_free(&buf);
            js_release(pCtx, separator);
            return js_has_exception(pCtx) ? js_undefined() : js_throw(pCtx, "RangeError: string allocation failed");
        }
    }

    js_release(pCtx, separator);

    return string_from_buffer(pCtx, &buf);
}

static JSVal fn_array_slice(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    int64_t nStart = (nArgc > 0) ? clamp_index(js_to_number(pCtx, pArgv[0]), nLength) : 0;
    int64_t nEnd = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? clamp_index(js_to_number(pCtx, pArgv[1]), nLength) : nLength;
    JSVal result = js_new_array(pCtx);
    int64_t i = 0;
    int64_t nCount = 0;

    (void)pUser;

    for (i = nStart; i < nEnd; i++) {
        js_set_index(pCtx, result, nCount++, js_get_index(pCtx, thisVal, i));
    }

    return result;
}

static JSVal fn_array_splice(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    int64_t nStart = (nArgc > 0) ? clamp_index(js_to_number(pCtx, pArgv[0]), nLength) : 0;
    /* The reference engine narrows the delete count to an int32 before it
     * clamps, so 2^32 + 1 deletes one element and 1e300 deletes none.     */
    /* splice() with no start deletes nothing; splice(start) deletes to the end. */
    int64_t nDelete = (nArgc == 0) ? 0 : (nArgc > 1) ? (int64_t)js_to_int32(pCtx, pArgv[1]) : (nLength - nStart);
    JSVal removed = js_new_array(pCtx);
    JSVal tail = js_new_array(pCtx);
    int64_t i = 0;
    int64_t nCount = 0;
    int64_t nTail = 0;
    int64_t nInsert = (nArgc > 2) ? (nArgc - 2) : 0;
    int64_t nNewLength = 0;

    (void)pUser;

    if (nDelete < 0) {
        nDelete = 0;
    }

    /* nStart is in [0, nLength], so the subtraction cannot overflow while
     * the addition would for a delete count near INT64_MAX.               */
    if (nDelete > (nLength - nStart)) {
        nDelete = nLength - nStart;
    }

    for (i = 0; i < nDelete; i++) {
        js_set_index(pCtx, removed, nCount++, js_get_index(pCtx, thisVal, nStart + i));
    }

    for (i = nStart + nDelete; i < nLength; i++) {
        js_set_index(pCtx, tail, nTail++, js_get_index(pCtx, thisVal, i));
    }

    nNewLength = nStart;

    for (i = 0; i < nInsert; i++) {
        js_set_index(pCtx, thisVal, nNewLength++, js_dup(pArgv[2 + i]));
    }

    for (i = 0; i < nTail; i++) {
        js_set_index(pCtx, thisVal, nNewLength++, js_get_index(pCtx, tail, i));
    }

    for (i = nNewLength; i < nLength; i++) {
        char sKey[32];

        xx_rt_snprintf(sKey, sizeof(sKey), "%lld", (long long)i);

        if (thisVal.tag == JT_OBJ) {
            jsobj_del(pCtx, thisVal.u.o, sKey);
        }
    }

    /* Only an Array owns a nominal length: a Uint32Array's length is its
     * allocation, and growing it here would let later index writes run past
     * the buffer. */
    if (thisVal.tag == JT_OBJ && thisVal.u.o->cls == JCLASS_ARRAY) {
        thisVal.u.o->nArrayLen = nNewLength;
    }

    js_release(pCtx, tail);

    return removed;
}

static JSVal fn_array_concat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result = js_new_array(pCtx);
    int64_t nCount = 0;
    int64_t i = 0;
    int j = 0;

    (void)pUser;

    {
        int64_t nLength = js_array_length(pCtx, thisVal);

        for (i = 0; i < nLength; i++) {
            js_set_index(pCtx, result, nCount++, js_get_index(pCtx, thisVal, i));
        }
    }

    for (j = 0; j < nArgc; j++) {
        if ((pArgv[j].tag == JT_OBJ) && (pArgv[j].u.o->cls == JCLASS_ARRAY)) {
            int64_t nLength = js_array_length(pCtx, pArgv[j]);

            for (i = 0; i < nLength; i++) {
                js_set_index(pCtx, result, nCount++, js_get_index(pCtx, pArgv[j], i));
            }
        } else {
            js_set_index(pCtx, result, nCount++, js_dup(pArgv[j]));
        }
    }

    return result;
}

static JSVal fn_array_indexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    JSVal search = ARG(0);
    int64_t i = (nArgc > 1) ? js_to_int64(pCtx, pArgv[1]) : 0;

    (void)pUser;

    if (i < 0) {
        i += nLength;

        if (i < 0) {
            i = 0;
        }
    }

    for (; i < nLength; i++) {
        JSVal item = js_get_index(pCtx, thisVal, i);
        int bMatch = js_strict_equals(pCtx, item, search);

        js_release(pCtx, item);

        if (bMatch) {
            return js_num((double)i);
        }
    }

    return js_num(-1);
}

static JSVal fn_array_lastindexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    JSVal search = ARG(0);
    int64_t i = nLength - 1;

    (void)pUser;

    if ((nArgc > 1) && (nLength > 0)) {
        int64_t nFrom = js_to_int64(pCtx, pArgv[1]);

        if (nFrom < 0) {
            if (nFrom < (-nLength)) {
                return js_num(-1);
            }

            nFrom += nLength;
        } else if (nFrom > (nLength - 1)) {
            nFrom = nLength - 1;
        }

        i = nFrom;
    }

    for (; i >= 0; i--) {
        JSVal item = js_get_index(pCtx, thisVal, i);
        int bMatch = js_strict_equals(pCtx, item, search);

        js_release(pCtx, item);

        if (bMatch) {
            return js_num((double)i);
        }
    }

    return js_num(-1);
}

static JSVal fn_array_reverse(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    int64_t i = 0;

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    for (i = 0; i < nLength / 2; i++) {
        JSVal a = js_get_index(pCtx, thisVal, i);
        JSVal b = js_get_index(pCtx, thisVal, nLength - 1 - i);

        js_set_index(pCtx, thisVal, i, b);
        js_set_index(pCtx, thisVal, nLength - 1 - i, a);
    }

    return js_dup(thisVal);
}

#include "../sort/xx_sort_js.inc"

typedef enum {
    ITER_FOREACH,
    ITER_MAP,
    ITER_FILTER,
    ITER_EVERY,
    ITER_SOME
} IterKind;

static JSVal array_iterate(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, IterKind kind)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    JSVal callback = ARG(0);
    JSVal thisArg = ARG(1);
    JSVal result = js_undefined();
    int64_t i = 0;
    int64_t nCount = 0;

    if (!js_is_callable(callback)) {
        return js_throw(pCtx, "TypeError: callback is not a function");
    }

    if ((kind == ITER_MAP) || (kind == ITER_FILTER)) {
        result = js_new_array(pCtx);
    } else if (kind == ITER_EVERY) {
        result = js_bool(1);
    } else if (kind == ITER_SOME) {
        result = js_bool(0);
    }

    for (i = 0; i < nLength; i++) {
        JSVal args[3];
        JSVal item;
        JSVal callResult;

        /* The iteration methods skip holes: no prototype lookup and no
         * callback for a missing element of a dense array, and nothing
         * exists past its used part however large the nominal length is. */
        if (thisVal.tag == JT_OBJ && thisVal.u.o->cls == JCLASS_ARRAY) {
            if (!thisVal.u.o->bArraySparse && i < (int64_t)thisVal.u.o->nArrayUsed) {
                if ((int)thisVal.u.o->pArrayData[thisVal.u.o->nArrayStart + i].tag < 0) {
                    continue;
                }
            } else {
                /* Past the dense part an element can only live in the
                 * property map (a far index write): a missing key is a hole,
                 * and with no live property at all the rest is holes. */
                char sKey[32];
                size_t nKeySize;
                if (thisVal.u.o->props.nLive == 0) {
                    break;
                }
                nKeySize = (size_t)xx_rt_snprintf(sKey, sizeof(sKey), "%lld", (long long)i);
                if (!jsprops_find(&thisVal.u.o->props, sKey, nKeySize)) {
                    continue;
                }
            }
        }
        item = js_get_index(pCtx, thisVal, i);

        args[0] = item;
        args[1] = js_num((double)i);
        args[2] = thisVal;

        callResult = js_call(pCtx, callback, thisArg, 3, args);

        if (pCtx->bException) {
            js_release(pCtx, item);
            js_release(pCtx, callResult);
            js_release(pCtx, result);

            return js_undefined();
        }

        if (kind == ITER_MAP) {
            js_set_index(pCtx, result, i, callResult);
            callResult = js_undefined();
        } else if (kind == ITER_FILTER) {
            if (js_to_bool(pCtx, callResult)) {
                js_set_index(pCtx, result, nCount++, js_dup(item));
            }
        } else if (kind == ITER_EVERY) {
            if (!js_to_bool(pCtx, callResult)) {
                js_release(pCtx, item);
                js_release(pCtx, callResult);
                js_release(pCtx, result);

                return js_bool(0);
            }
        } else if (kind == ITER_SOME) {
            if (js_to_bool(pCtx, callResult)) {
                js_release(pCtx, item);
                js_release(pCtx, callResult);
                js_release(pCtx, result);

                return js_bool(1);
            }
        }

        js_release(pCtx, callResult);
        js_release(pCtx, item);
    }

    return result;
}

static JSVal fn_array_foreach(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_FOREACH);
}

static JSVal fn_array_map(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_MAP);
}

static JSVal fn_array_filter(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_FILTER);
}

static JSVal fn_array_every(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_EVERY);
}

static JSVal fn_array_some(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_SOME);
}

static JSVal fn_array_reduce(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int64_t nLength = js_array_length(pCtx, thisVal);
    JSVal callback = ARG(0);
    JSVal accumulator = js_undefined();
    int64_t i = 0;

    (void)pUser;

    if (!js_is_callable(callback)) {
        return js_throw(pCtx, "TypeError: callback is not a function");
    }

    if ((nArgc <= 1) && (nLength == 0)) {
        return js_throw(pCtx, "TypeError: Reduce of empty array with no initial value");
    }

    if (nArgc > 1) {
        accumulator = js_dup(pArgv[1]);
    } else if (nLength > 0) {
        accumulator = js_get_index(pCtx, thisVal, 0);
        i = 1;
    }

    for (; i < nLength; i++) {
        JSVal args[4];
        JSVal item = js_get_index(pCtx, thisVal, i);
        JSVal result;

        args[0] = accumulator;
        args[1] = item;
        args[2] = js_num((double)i);
        args[3] = thisVal;

        result = js_call(pCtx, callback, js_undefined(), 4, args);
        js_release(pCtx, accumulator);
        js_release(pCtx, item);
        accumulator = result;

        if (pCtx->bException) {
            break;
        }
    }

    return accumulator;
}

/* ---------------------------------------------------------------- String  */

static JSVal fn_string_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = (nArgc == 0) ? js_str(pCtx, "") : js_to_string(pCtx, pArgv[0]);

    (void)thisVal;
    (void)pUser;

    /* new String(x) is a wrapper object whose primitive the string methods
     * unwrap; without it they looped through toString until the call limit. */
    if (pCtx->bNativeConstruct && !pCtx->bException) {
        JSObj *pObj = jsobj_new(pCtx, JCLASS_STRING, pCtx->pStringProto);

        pObj->primitive = text; /* takes the reference */

        return jsval_obj(pObj);
    }

    return text;
}

static JSVal fn_string_fromcharcode(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    xx_buf_t buf;
    int i = 0;
    JSVal result;

    (void)thisVal;
    (void)pUser;

    xx_buf_init(&buf);

    for (i = 0; i < nArgc; i++) {
        uint32_t nCode = (uint32_t)js_to_int32(pCtx, pArgv[i]) & 0xFFFF;

        /* Strings are byte strings (length, charCodeAt and indexOf count
         * bytes), so a code below 256 is one byte: the scripts build raw
         * signatures this way and compare them with Binary.getString. Only
         * larger code units still take the UTF-8 spelling. */
        if (nCode < 0x100) {
            xx_buf_append_char(&buf, (char)nCode);
        } else if (nCode < 0x800) {
            xx_buf_append_char(&buf, (char)(0xC0 | (nCode >> 6)));
            xx_buf_append_char(&buf, (char)(0x80 | (nCode & 0x3F)));
        } else {
            xx_buf_append_char(&buf, (char)(0xE0 | (nCode >> 12)));
            xx_buf_append_char(&buf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
            xx_buf_append_char(&buf, (char)(0x80 | (nCode & 0x3F)));
        }
    }

    result = js_strn(pCtx, buf.data, buf.size);
    xx_buf_free(&buf);

    return result;
}

static JSVal fn_string_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_builtin_string_value(pCtx, thisVal);
}

static JSVal fn_string_charat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    int64_t nIndex = (nArgc > 0) ? js_to_int64(pCtx, pArgv[0]) : 0;
    JSVal result;

    (void)pUser;

    if ((nIndex < 0) || ((size_t)nIndex >= js_str_len(text))) {
        result = js_str(pCtx, "");
    } else {
        result = js_strn(pCtx, js_str_data(text) + nIndex, 1);
    }

    js_release(pCtx, text);

    return result;
}

static JSVal fn_string_charcodeat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    int64_t nIndex = (nArgc > 0) ? js_to_int64(pCtx, pArgv[0]) : 0;
    JSVal result;

    (void)pUser;

    if ((nIndex < 0) || ((size_t)nIndex >= js_str_len(text))) {
        result = js_num(xx_rt_nan());
    } else {
        result = js_num((double)(unsigned char)js_str_data(text)[nIndex]);
    }

    js_release(pCtx, text);

    return result;
}

/* Long needles use the KMP failure function, so repeated prefixes cannot turn
 * indexOf/split/replace into quadratic scans. Short needles have a fixed upper
 * bound and avoid the prefix-table setup. The reverse mode visits bytes from
 * the end and returns the last match without scanning an already matched tail. */
static int64_t search_substring(JSCtx *pCtx, const char *pText, size_t nTextSize, const char *pNeedle,
                                size_t nNeedleSize, size_t nStart, int bReverse)
{
    size_t i;
    size_t j;
    size_t nScanSize;
    size_t localPrefix[128];
    size_t *pPrefix = localPrefix;
    int64_t nResult = -1;

    if (nNeedleSize == 0) {
        return (int64_t)((nStart > nTextSize) ? nTextSize : nStart);
    }

    if (nNeedleSize > nTextSize) {
        return -1;
    }

    if (bReverse) {
        if (nStart > nTextSize - nNeedleSize) {
            nStart = nTextSize - nNeedleSize;
        }
        nScanSize = nStart + nNeedleSize;
    } else {
        if (nStart > nTextSize - nNeedleSize) {
            return -1;
        }
        pText += nStart;
        nTextSize -= nStart;
        nScanSize = nTextSize;
    }

    if (nNeedleSize <= 16) {
        if (bReverse) {
            for (i = nStart + 1; i > 0; i--) {
                if ((unsigned char)pText[i + nNeedleSize - 2] == (unsigned char)pNeedle[nNeedleSize - 1] &&
                    ((nNeedleSize == 1) || (xx_rt_memcmp(pText + i - 1, pNeedle, nNeedleSize - 1) == 0))) {
                    return (int64_t)(i - 1);
                }
            }
        } else {
            size_t nLast = nScanSize - nNeedleSize;
            i = 0;
            while (i <= nLast) {
                /* Probe the last byte first: a long repeated prefix such as
                 * aaaa...ab should skip the whole text when b is absent. */
                const char *pFound = (const char *)xx_rt_memchr(pText + i + nNeedleSize - 1,
                    (unsigned char)pNeedle[nNeedleSize - 1], nLast - i + 1);
                if (!pFound) {
                    break;
                }
                i = (size_t)(pFound - pText) - (nNeedleSize - 1);
                if ((nNeedleSize == 1) || (xx_rt_memcmp(pText + i, pNeedle, nNeedleSize - 1) == 0)) {
                    return (int64_t)(nStart + i);
                }
                i++;
            }
        }
        return -1;
    }

    if (nNeedleSize > sizeof(localPrefix) / sizeof(localPrefix[0])) {
        if (nNeedleSize > (size_t)-1 / sizeof(size_t)) {
            js_throw(pCtx, "RangeError: search pattern is too large");
            return -1;
        }
        pPrefix = (size_t *)xx_js_malloc(nNeedleSize * sizeof(size_t));
        if (!pPrefix) {
            js_throw(pCtx, "RangeError: search allocation failed");
            return -1;
        }
    }

    pPrefix[0] = 0;
    for (i = 1, j = 0; i < nNeedleSize; i++) {
        unsigned char c = (unsigned char)pNeedle[bReverse ? nNeedleSize - 1 - i : i];
        while (j && (c != (unsigned char)pNeedle[bReverse ? nNeedleSize - 1 - j : j])) {
            j = pPrefix[j - 1];
        }
        if (c == (unsigned char)pNeedle[bReverse ? nNeedleSize - 1 - j : j]) {
            j++;
        }
        pPrefix[i] = j;
    }

    for (i = 0, j = 0; i < nScanSize; i++) {
        unsigned char c = (unsigned char)pText[bReverse ? nScanSize - 1 - i : i];
        while (j && (c != (unsigned char)pNeedle[bReverse ? nNeedleSize - 1 - j : j])) {
            j = pPrefix[j - 1];
        }
        if (c == (unsigned char)pNeedle[bReverse ? nNeedleSize - 1 - j : j]) {
            j++;
        }
        if (j == nNeedleSize) {
            nResult = (int64_t)(bReverse ? nScanSize - 1 - i : nStart + i + 1 - nNeedleSize);
            break;
        }
    }

    if (pPrefix != localPrefix) {
        xx_js_free(pPrefix);
    }
    return nResult;
}

int64_t js_builtin_find_substring(JSCtx *pCtx, const char *pText, size_t nTextSize, const char *pNeedle, size_t nNeedleSize, size_t nStart)
{
    return search_substring(pCtx, pText, nTextSize, pNeedle, nNeedleSize, nStart, 0);
}

static JSVal fn_string_indexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    JSVal needle = js_to_string(pCtx, ARG(0));
    int64_t nStart = (nArgc > 1) ? js_to_int64(pCtx, pArgv[1]) : 0;
    int64_t nResult = 0;

    (void)pUser;

    if (nStart < 0) {
        nStart = 0;
    }

    nResult = js_builtin_find_substring(pCtx, js_str_data(text), js_str_len(text), js_str_data(needle), js_str_len(needle), (size_t)nStart);

    js_release(pCtx, text);
    js_release(pCtx, needle);

    return js_has_exception(pCtx) ? js_undefined() : js_num((double)nResult);
}

static JSVal fn_string_lastindexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    JSVal needle = js_to_string(pCtx, ARG(0));
    size_t nTextSize = js_str_len(text);
    size_t nNeedleSize = js_str_len(needle);
    int64_t nResult = -1;

    (void)pUser;

    if (nNeedleSize <= nTextSize) {
        size_t nStart = nTextSize - nNeedleSize;

        /* An absent or NaN position means +Infinity, i.e. no upper bound. */
        if (nArgc > 1) {
            double nValue = js_to_number(pCtx, pArgv[1]);

            if (nValue == nValue) {
                size_t nFrom = 0;

                if (nValue > 0) {
                    nFrom = (nValue > (double)nTextSize) ? nTextSize : (size_t)nValue;
                }

                if (nFrom < nStart) {
                    nStart = nFrom;
                }
            }
        }

        nResult = search_substring(pCtx, js_str_data(text), nTextSize, js_str_data(needle), nNeedleSize, nStart, 1);
    }

    js_release(pCtx, text);
    js_release(pCtx, needle);

    return js_has_exception(pCtx) ? js_undefined() : js_num((double)nResult);
}

static JSVal fn_string_slice(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    int64_t nLength = (int64_t)js_str_len(text);
    int64_t nStart = (nArgc > 0) ? clamp_index(js_to_number(pCtx, pArgv[0]), nLength) : 0;
    int64_t nEnd = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? clamp_index(js_to_number(pCtx, pArgv[1]), nLength) : nLength;
    JSVal result;

    (void)pUser;

    if (nEnd < nStart) {
        nEnd = nStart;
    }

    result = js_strn(pCtx, js_str_data(text) + nStart, (size_t)(nEnd - nStart));
    js_release(pCtx, text);

    return result;
}

/* substring arguments: ToInteger first, then clamp to 0..length, so that
 * Infinity, -Infinity and oversized doubles never reach an int64_t cast. */
static int64_t substring_index(double nValue, int64_t nLength)
{
    double nTrunc;

    if (nValue != nValue) {
        return 0;
    }

    nTrunc = (nValue < 0) ? (-xx_rt_floor(-nValue)) : xx_rt_floor(nValue);

    if (nTrunc <= 0) {
        return 0;
    }

    if (nTrunc >= (double)nLength) {
        return nLength;
    }

    return (int64_t)nTrunc;
}

static JSVal fn_string_substring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    int64_t nLength = (int64_t)js_str_len(text);
    int64_t nStart = 0;
    int64_t nEnd = nLength;
    JSVal result;

    (void)pUser;

    if (nArgc > 0) {
        nStart = substring_index(js_to_number(pCtx, pArgv[0]), nLength);
    }

    if ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) {
        nEnd = substring_index(js_to_number(pCtx, pArgv[1]), nLength);
    }

    if (nStart < 0) {
        nStart = 0;
    }

    if (nStart > nLength) {
        nStart = nLength;
    }

    if (nEnd < 0) {
        nEnd = 0;
    }

    if (nEnd > nLength) {
        nEnd = nLength;
    }

    if (nStart > nEnd) {
        int64_t nTemp = nStart;

        nStart = nEnd;
        nEnd = nTemp;
    }

    result = js_strn(pCtx, js_str_data(text) + nStart, (size_t)(nEnd - nStart));
    js_release(pCtx, text);

    return result;
}

static JSVal fn_string_substr(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    int64_t nLength = (int64_t)js_str_len(text);
    int64_t nStart = (nArgc > 0) ? js_to_int64(pCtx, pArgv[0]) : 0;
    int64_t nCount = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? js_to_int64(pCtx, pArgv[1]) : nLength;
    JSVal result;

    (void)pUser;

    if (nStart < 0) {
        nStart = nLength + nStart;

        if (nStart < 0) {
            nStart = 0;
        }
    }

    if (nStart > nLength) {
        nStart = nLength;
    }

    if (nCount < 0) {
        nCount = 0;
    }

    /* nStart is in [0, nLength] here, so clamping by subtraction avoids the
     * overflow that a count near INT64_MAX would cause in the addition.   */
    if (nCount > (nLength - nStart)) {
        nCount = nLength - nStart;
    }

    result = js_strn(pCtx, js_str_data(text) + nStart, (size_t)nCount);
    js_release(pCtx, text);

    return result;
}

static JSVal string_case(JSCtx *pCtx, JSVal thisVal, int bUpper)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    size_t nSize = js_str_len(text);
    JSStr *pStr = jsstr_new(pCtx, js_str_data(text), nSize);
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char nChar = pStr->pData[i];

        if (bUpper) {
            if ((nChar >= 'a') && (nChar <= 'z')) {
                pStr->pData[i] = (char)(nChar - 'a' + 'A');
            }
        } else {
            if ((nChar >= 'A') && (nChar <= 'Z')) {
                pStr->pData[i] = (char)(nChar - 'A' + 'a');
            }
        }
    }

    js_release(pCtx, text);

    return jsval_str(pStr);
}

static JSVal fn_string_toupper(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return string_case(pCtx, thisVal, 1);
}

static JSVal fn_string_tolower(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return string_case(pCtx, thisVal, 0);
}

/* QtScript's trim removes whitespace, not every control byte below SPACE.
 * Preserve its whitespace set, including U+200B but excluding U+180E and
 * U+FEFF. Strings can contain Latin-1 bytes or UTF-8 passed through by the
 * interpreter, so recognize NBSP in both forms and the other UTF-8 spaces. */
static size_t string_trim_space_size(const char *pData, size_t nSize)
{
    const unsigned char *pBytes = (const unsigned char *)pData;

    if (nSize == 0) {
        return 0;
    }

    if ((pBytes[0] == 0x20) || ((pBytes[0] >= 0x09) && (pBytes[0] <= 0x0D)) || (pBytes[0] == 0xA0)) {
        return 1;
    }

    if ((nSize >= 2) && (pBytes[0] == 0xC2) && (pBytes[1] == 0xA0)) {
        return 2;
    }

    if (nSize >= 3) {
        if (((pBytes[0] == 0xE1) && (pBytes[1] == 0x9A) && (pBytes[2] == 0x80)) ||
            ((pBytes[0] == 0xE2) && (pBytes[1] == 0x80) &&
             (((pBytes[2] >= 0x80) && (pBytes[2] <= 0x8B)) || (pBytes[2] == 0xA8) || (pBytes[2] == 0xA9) || (pBytes[2] == 0xAF))) ||
            ((pBytes[0] == 0xE2) && (pBytes[1] == 0x81) && (pBytes[2] == 0x9F)) || ((pBytes[0] == 0xE3) && (pBytes[1] == 0x80) && (pBytes[2] == 0x80))) {
            return 3;
        }
    }

    return 0;
}

static size_t string_trim_last_size(const char *pData, size_t nStart, size_t nEnd)
{
    const unsigned char *pBytes = (const unsigned char *)pData;
    size_t nPos = nEnd - 1;
    size_t nSize = 0;

    while ((nPos > nStart) && ((nEnd - nPos) < 4) && ((pBytes[nPos] & 0xC0) == 0x80)) {
        nPos--;
    }

    nSize = nEnd - nPos;

    /* Do not treat a valid non-whitespace UTF-8 sequence's final 0xA0 as
     * a standalone Latin-1 NBSP; that would leave a truncated code point. */
    if (((nSize == 2) && (pBytes[nPos] >= 0xC2) && (pBytes[nPos] <= 0xDF)) ||
        ((nSize == 3) && (pBytes[nPos] >= 0xE0) && (pBytes[nPos] <= 0xEF) && ((pBytes[nPos] != 0xE0) || (pBytes[nPos + 1] >= 0xA0)) &&
         ((pBytes[nPos] != 0xED) || (pBytes[nPos + 1] < 0xA0))) ||
        ((nSize == 4) && (pBytes[nPos] >= 0xF0) && (pBytes[nPos] <= 0xF4) && ((pBytes[nPos] != 0xF0) || (pBytes[nPos + 1] >= 0x90)) &&
         ((pBytes[nPos] != 0xF4) || (pBytes[nPos + 1] < 0x90)))) {
        return nSize;
    }

    return 1;
}

static JSVal fn_string_trim(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    const char *pData = js_str_data(text);
    size_t nStart = 0;
    size_t nEnd = js_str_len(text);
    size_t nSpaceSize = 0;
    JSVal result;

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    while ((nStart < nEnd) && ((nSpaceSize = string_trim_space_size(pData + nStart, nEnd - nStart)) != 0)) {
        nStart += nSpaceSize;
    }

    while (nEnd > nStart) {
        size_t nCharSize = string_trim_last_size(pData, nStart, nEnd);

        nSpaceSize = string_trim_space_size(pData + nEnd - nCharSize, nCharSize);

        if (nSpaceSize != nCharSize) {
            break;
        }

        nEnd -= nCharSize;
    }

    result = js_strn(pCtx, pData + nStart, nEnd - nStart);
    js_release(pCtx, text);

    return result;
}

static JSVal fn_string_concat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_builtin_string_value(pCtx, thisVal);
    xx_buf_t buf;
    int i;

    (void)pUser;

    if (js_has_exception(pCtx) || (nArgc == 0)) {
        return text;
    }

    xx_buf_init(&buf);
    xx_buf_append(&buf, js_str_data(text), js_str_len(text));
    js_release(pCtx, text);

    for (i = 0; i < nArgc; i++) {
        if (!append_value_string(pCtx, &buf, pArgv[i])) {
            xx_buf_free(&buf);
            return js_has_exception(pCtx) ? js_undefined() : js_throw(pCtx, "RangeError: string allocation failed");
        }
    }

    return string_from_buffer(pCtx, &buf);
}

/* Assignment fusion must use the captured native function's identity: the
 * public concat property can be replaced or bound by JavaScript. */
int js_is_string_concat(JSObj *pObj)
{
    return pObj && pObj->cls == JCLASS_NATIVE && !pObj->pBoundTarget && pObj->nativeFn == fn_string_concat;
}

/* ---------------------------------------------------------------- Number  */

static JSVal fn_number_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = (nArgc == 0) ? 0 : js_to_number(pCtx, pArgv[0]);

    (void)thisVal;
    (void)pUser;

    if (pCtx->bNativeConstruct) {
        JSObj *pObj = jsobj_new(pCtx, JCLASS_NUMBER, pCtx->pNumberProto);

        pObj->primitive = js_num(nValue);

        return jsval_obj(pObj);
    }

    return js_num(nValue);
}

static JSVal fn_number_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = this_number(pCtx, thisVal);
    int nRadix = (nArgc > 0) ? (int)js_to_int64(pCtx, pArgv[0]) : 10;

    (void)pUser;

    if ((nRadix < 2) || (nRadix > 36)) {
        return js_throw(pCtx, "RangeError: toString() radix must be between 2 and 36");
    }

    return js_number_to_string(pCtx, nValue, nRadix);
}

static JSVal fn_number_valueof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_num(this_number(pCtx, thisVal));
}

static JSVal fn_number_tofixed(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = this_number(pCtx, thisVal);
    int nDigits = (nArgc > 0) ? (int)js_to_int64(pCtx, pArgv[0]) : 0;
    char sBuf[512];

    (void)pUser;

    if ((nDigits < 0) || (nDigits > 100)) {
        nDigits = 0;
    }

    if (nValue != nValue) {
        return js_str(pCtx, "NaN");
    }

    xx_rt_dtoa_fixed(nValue, nDigits, sBuf, sizeof(sBuf));

    return js_str(pCtx, sBuf);
}

static JSVal fn_number_toprecision(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = this_number(pCtx, thisVal);
    int nDigits = (nArgc > 0) ? (int)js_to_int64(pCtx, pArgv[0]) : 6;
    char sBuf[512];

    (void)pUser;

    if ((nArgc == 0) || (pArgv[0].tag == JT_UNDEF)) {
        return js_number_to_string(pCtx, nValue, 10);
    }

    if ((nDigits < 1) || (nDigits > 100)) {
        nDigits = 6;
    }

    xx_rt_dtoa_precision(nValue, nDigits, sBuf, sizeof(sBuf));

    return js_str(pCtx, sBuf);
}

/* --------------------------------------------------------------- Boolean  */

static JSVal fn_boolean_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    int bValue = (nArgc > 0) ? js_to_bool(pCtx, pArgv[0]) : 0;

    (void)thisVal;
    (void)pUser;

    if (pCtx->bNativeConstruct) {
        JSObj *pObj = jsobj_new(pCtx, JCLASS_BOOLEAN, pCtx->pBooleanProto);

        pObj->primitive = js_bool(bValue);

        return jsval_obj(pObj);
    }

    return js_bool(bValue);
}

/* The boolean behind a Boolean wrapper, or the value's own truth. */
static int this_boolean(JSCtx *pCtx, JSVal thisVal)
{
    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->cls == JCLASS_BOOLEAN)) {
        return js_to_bool(pCtx, thisVal.u.o->primitive);
    }

    return js_to_bool(pCtx, thisVal);
}

static JSVal fn_boolean_valueof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_bool(this_boolean(pCtx, thisVal));
}

static JSVal fn_boolean_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_str(pCtx, this_boolean(pCtx, thisVal) ? "true" : "false");
}

/* ------------------------------------------------------------------ Date  */

/* Pure integer civil-calendar arithmetic: the CRT-free build has no clock
 * and no timezone database, so local time is UTC and a Date built without
 * arguments (and Date.now) reports the epoch. Everything that the signature
 * database actually evaluates - a time value, a broken-down UTC field or an
 * ISO string - is exact.                                                   */

#define DATE_MS_PER_DAY 86400000.0

typedef enum {
    DATE_TIME,
    DATE_YEAR,
    DATE_MONTH,
    DATE_DATE,
    DATE_DAY,
    DATE_HOURS,
    DATE_MINUTES,
    DATE_SECONDS,
    DATE_MS,
    DATE_TZOFFSET
} DateField;

static int date_is_finite(double nValue)
{
    return ((nValue == nValue) && (nValue != xx_rt_inf()) && (nValue != (-xx_rt_inf()))) ? 1 : 0;
}

static double date_trunc(double nValue)
{
    return (nValue < 0) ? (-xx_rt_floor(-nValue)) : xx_rt_floor(nValue);
}

/* Days since 1970-01-01 for a proleptic Gregorian date, nMonth in [1, 12]. */
static double date_days_from_civil(int64_t nYear, int64_t nMonth, int64_t nDay)
{
    int64_t y = nYear - ((nMonth <= 2) ? 1 : 0);
    int64_t nEra = ((y >= 0) ? y : (y - 399)) / 400;
    int64_t nYoe = y - nEra * 400;
    int64_t nDoy = (153 * (nMonth + ((nMonth > 2) ? (-3) : 9)) + 2) / 5 + nDay - 1;
    int64_t nDoe = nYoe * 365 + nYoe / 4 - nYoe / 100 + nDoy;

    return (double)(nEra * 146097 + nDoe - 719468);
}

static void date_civil_from_days(int64_t nDays, int64_t *pnYear, int64_t *pnMonth, int64_t *pnDay)
{
    int64_t z = nDays + 719468;
    int64_t nEra = ((z >= 0) ? z : (z - 146096)) / 146097;
    int64_t nDoe = z - nEra * 146097;
    int64_t nYoe = (nDoe - nDoe / 1460 + nDoe / 36524 - nDoe / 146096) / 365;
    int64_t nDoy = nDoe - (365 * nYoe + nYoe / 4 - nYoe / 100);
    int64_t nMp = (5 * nDoy + 2) / 153;
    int64_t nDay = nDoy - (153 * nMp + 2) / 5 + 1;
    int64_t nMonth = nMp + ((nMp < 10) ? 3 : (-9));
    int64_t nYear = nYoe + nEra * 400 + ((nMonth <= 2) ? 1 : 0);

    *pnYear = nYear;
    *pnMonth = nMonth;
    *pnDay = nDay;
}

static double date_make_day(double nYear, double nMonth, double nDate)
{
    double nYm = 0;
    double nMn = 0;

    if ((!date_is_finite(nYear)) || (!date_is_finite(nMonth)) || (!date_is_finite(nDate))) {
        return xx_rt_nan();
    }

    nYear = date_trunc(nYear);
    nMonth = date_trunc(nMonth);
    nDate = date_trunc(nDate);

    nYm = nYear + xx_rt_floor(nMonth / 12.0);
    nMn = nMonth - xx_rt_floor(nMonth / 12.0) * 12.0;

    /* A time value is capped at +-8.64e15 ms, i.e. roughly +-274000 years;
     * anything outside that cannot survive the clip and would overflow the
     * integer arithmetic below.                                           */
    if ((nYm < (-400000.0)) || (nYm > 400000.0) || (nDate < (-1.0e9)) || (nDate > 1.0e9)) {
        return xx_rt_nan();
    }

    return date_days_from_civil((int64_t)nYm, (int64_t)nMn + 1, 1) + (nDate - 1.0);
}

static double date_make_time(double nHours, double nMinutes, double nSeconds, double nMs)
{
    if ((!date_is_finite(nHours)) || (!date_is_finite(nMinutes)) || (!date_is_finite(nSeconds)) || (!date_is_finite(nMs))) {
        return xx_rt_nan();
    }

    /* Hours, minutes and seconds arrive already narrowed to an int32; the
     * millisecond field is the one component the reference engine keeps as
     * a full double, and it floors it rather than truncating towards zero
     * (a ms of -1.9 moves the time value back by 2, not by 1).            */
    return date_trunc(nHours) * 3600000.0 + date_trunc(nMinutes) * 60000.0 + date_trunc(nSeconds) * 1000.0 + xx_rt_floor(nMs);
}

static double date_time_clip(double nTime)
{
    if ((!date_is_finite(nTime)) || (xx_rt_fabs(nTime) > 8.64e15)) {
        return xx_rt_nan();
    }

    return date_trunc(nTime);
}

/* The reference engine rejects a NaN component but narrows every other one
 * to an int32, so Infinity and 1e300 both behave as zero.                  */
static double date_component(JSCtx *pCtx, JSVal value, int *pbNaN)
{
    double nValue = js_to_number(pCtx, value);

    if (nValue != nValue) {
        *pbNaN = 1;

        return 0;
    }

    return (double)js_to_int32(pCtx, js_num(nValue));
}

static double date_from_parts(JSCtx *pCtx, int nArgc, JSVal *pArgv)
{
    int bNaN = 0;
    double nYear = date_component(pCtx, pArgv[0], &bNaN);
    double nMonth = (nArgc > 1) ? date_component(pCtx, pArgv[1], &bNaN) : 0;
    double nDate = (nArgc > 2) ? date_component(pCtx, pArgv[2], &bNaN) : 1;
    double nHours = (nArgc > 3) ? date_component(pCtx, pArgv[3], &bNaN) : 0;
    double nMinutes = (nArgc > 4) ? date_component(pCtx, pArgv[4], &bNaN) : 0;
    double nSeconds = (nArgc > 5) ? date_component(pCtx, pArgv[5], &bNaN) : 0;
    /* Deliberately NOT narrowed: a millisecond count of 1e16 has to push the
     * time value out of the clip range and give NaN, which an int32 wrap
     * would hide.                                                         */
    double nMs = (nArgc > 6) ? js_to_number(pCtx, pArgv[6]) : 0;

    if (bNaN) {
        return xx_rt_nan();
    }

    if ((nYear >= 0) && (nYear <= 99)) {
        nYear = 1900 + nYear;
    }

    return date_time_clip(date_make_day(nYear, nMonth, nDate) * DATE_MS_PER_DAY + date_make_time(nHours, nMinutes, nSeconds, nMs));
}

/* The time value lives in the object's primitive slot; a value that is not a
 * number means the receiver is not a Date.                                 */
static double date_this_time(JSVal thisVal)
{
    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->primitive.tag == JT_NUM)) {
        return thisVal.u.o->primitive.u.n;
    }

    return xx_rt_nan();
}

static JSVal fn_date_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSObj *pProto = (JSObj *)pUser;
    double nTime = 0;

    if (nArgc == 1) {
        nTime = date_time_clip(js_to_number(pCtx, pArgv[0]));
    } else if (nArgc > 1) {
        nTime = date_from_parts(pCtx, nArgc, pArgv);
    }

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->pProto == pProto)) {
        thisVal.u.o->primitive = js_num(nTime);

        return js_undefined();
    }

    {
        JSObj *pObj = jsobj_new(pCtx, JCLASS_OBJECT, pProto);

        pObj->primitive = js_num(nTime);

        return jsval_obj(pObj);
    }
}

static JSVal fn_date_utc(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    /* The reference engine needs at least the year and the month. */
    if (nArgc < 2) {
        return js_num(xx_rt_nan());
    }

    return js_num(date_from_parts(pCtx, nArgc, pArgv));
}

static JSVal fn_date_now(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_num(0);
}

static JSVal fn_date_get(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nTime = date_this_time(thisVal);
    DateField field = (DateField)(size_t)pUser;
    int64_t nDays = 0;
    int64_t nInDay = 0;
    int64_t nYear = 0;
    int64_t nMonth = 0;
    int64_t nDate = 0;

    (void)pCtx;
    (void)nArgc;
    (void)pArgv;

    if (nTime != nTime) {
        return js_num(xx_rt_nan());
    }

    if (field == DATE_TIME) {
        return js_num(nTime);
    }

    if (field == DATE_TZOFFSET) {
        return js_num(0);
    }

    nDays = (int64_t)xx_rt_floor(nTime / DATE_MS_PER_DAY);
    nInDay = (int64_t)(nTime - (double)nDays * DATE_MS_PER_DAY);
    date_civil_from_days(nDays, &nYear, &nMonth, &nDate);

    switch (field) {
        case DATE_TIME: return js_num(nTime);
        case DATE_TZOFFSET: return js_num(0);
        case DATE_YEAR: return js_num((double)nYear);
        case DATE_MONTH: return js_num((double)(nMonth - 1));
        case DATE_DATE: return js_num((double)nDate);
        case DATE_DAY: return js_num((double)(((nDays % 7) + 11) % 7));
        case DATE_HOURS: return js_num((double)(nInDay / 3600000));
        case DATE_MINUTES: return js_num((double)((nInDay / 60000) % 60));
        case DATE_SECONDS: return js_num((double)((nInDay / 1000) % 60));
        case DATE_MS: return js_num((double)(nInDay % 1000));
        default: break;
    }

    return js_num(xx_rt_nan());
}

static JSVal fn_date_toisostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nTime = date_this_time(thisVal);
    int64_t nDays = 0;
    int64_t nInDay = 0;
    int64_t nYear = 0;
    int64_t nMonth = 0;
    int64_t nDate = 0;
    /* Exactly the width the reference engine uses: a year that does not fit
     * four digits pushes the trailing 'Z' out of the buffer.              */
    char sBuf[27];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nTime != nTime) {
        return js_str(pCtx, "Invalid Date");
    }

    nDays = (int64_t)xx_rt_floor(nTime / DATE_MS_PER_DAY);
    nInDay = (int64_t)(nTime - (double)nDays * DATE_MS_PER_DAY);
    date_civil_from_days(nDays, &nYear, &nMonth, &nDate);

    /* The millisecond field is truncated towards zero rather than floored,
     * so a negative time prints ".-01"; the reference does the same.      */
    xx_rt_snprintf(sBuf, sizeof(sBuf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", (int)nYear, (int)nMonth, (int)nDate, (int)(nInDay / 3600000), (int)((nInDay / 60000) % 60),
                   (int)((nInDay / 1000) % 60), (int)xx_rt_fmod(nTime, 1000.0));

    return js_str(pCtx, sBuf);
}

static JSVal fn_date_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    static const char *pWeekDays = "SunMonTueWedThuFriSat";
    static const char *pMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
    double nTime = date_this_time(thisVal);
    int64_t nDays = 0;
    int64_t nInDay = 0;
    int64_t nYear = 0;
    int64_t nMonth = 0;
    int64_t nDate = 0;
    char sBuf[64];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nTime != nTime) {
        return js_str(pCtx, "Invalid Date");
    }

    nDays = (int64_t)xx_rt_floor(nTime / DATE_MS_PER_DAY);
    nInDay = (int64_t)(nTime - (double)nDays * DATE_MS_PER_DAY);
    date_civil_from_days(nDays, &nYear, &nMonth, &nDate);

    xx_rt_snprintf(sBuf, sizeof(sBuf), "%.3s %.3s %02d %04d %02d:%02d:%02d GMT+0000 (UTC)", pWeekDays + ((((nDays % 7) + 11) % 7) * 3), pMonths + (nMonth - 1) * 3,
                   (int)nDate, (int)nYear, (int)(nInDay / 3600000), (int)((nInDay / 60000) % 60), (int)((nInDay / 1000) % 60));

    return js_str(pCtx, sBuf);
}

/* ------------------------------------------------------------------ Math  */

typedef enum {
    MATH_ABS,
    MATH_FLOOR,
    MATH_CEIL,
    MATH_ROUND,
    MATH_SQRT,
    MATH_LOG,
    MATH_EXP,
    MATH_SIN,
    MATH_COS,
    MATH_TAN,
    MATH_TRUNC,
    MATH_SIGN,
    MATH_LOG2,
    MATH_LOG10
} MathKind;

static JSVal fn_math_unary(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = (nArgc > 0) ? js_to_number(pCtx, pArgv[0]) : xx_rt_nan();
    MathKind kind = (MathKind)(size_t)pUser;

    (void)thisVal;

    switch (kind) {
        case MATH_ABS: return js_num(xx_rt_fabs(nValue));
        case MATH_FLOOR: return js_num(xx_rt_floor(nValue));
        case MATH_CEIL: return js_num(xx_rt_ceil(nValue));
        case MATH_ROUND: {
            /* floor(x + 0.5) rounds 0.49999999999999994 up and misplaces
             * values whose x + 0.5 is not representable. */
            double nFloor = xx_rt_floor(nValue);
            return js_num(((nValue - nFloor) >= 0.5) ? (nFloor + 1) : nFloor);
        }
        case MATH_SQRT: return js_num(xx_rt_sqrt(nValue));
        case MATH_LOG: return js_num(xx_rt_log(nValue));
        case MATH_EXP: return js_num(xx_rt_exp(nValue));
        case MATH_SIN: return js_num(xx_rt_sin(nValue));
        case MATH_COS: return js_num(xx_rt_cos(nValue));
        case MATH_TAN: return js_num(xx_rt_tan(nValue));
        case MATH_TRUNC: return js_num((nValue < 0) ? xx_rt_ceil(nValue) : xx_rt_floor(nValue));
        case MATH_SIGN: return js_num((nValue > 0) ? 1 : ((nValue < 0) ? -1 : nValue));
        case MATH_LOG2: return js_num(xx_rt_log(nValue) / xx_rt_log(2.0));
        case MATH_LOG10: return js_num(xx_rt_log10(nValue));
        default: break;
    }

    return js_num(xx_rt_nan());
}

static JSVal fn_math_min(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nResult = xx_rt_inf();
    int i = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < nArgc; i++) {
        double nValue = js_to_number(pCtx, pArgv[i]);

        if (nValue != nValue) {
            return js_num(xx_rt_nan());
        }

        if (nValue < nResult) {
            nResult = nValue;
        }
    }

    return js_num(nResult);
}

static JSVal fn_math_max(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nResult = (-xx_rt_inf());
    int i = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < nArgc; i++) {
        double nValue = js_to_number(pCtx, pArgv[i]);

        if (nValue != nValue) {
            return js_num(xx_rt_nan());
        }

        if (nValue > nResult) {
            nResult = nValue;
        }
    }

    return js_num(nResult);
}

static JSVal fn_math_pow(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    return js_num(xx_rt_pow((nArgc > 0) ? js_to_number(pCtx, pArgv[0]) : xx_rt_nan(), (nArgc > 1) ? js_to_number(pCtx, pArgv[1]) : xx_rt_nan()));
}

static JSVal fn_math_random(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_num((double)xx_rt_rand() / ((double)XX_RT_RAND_MAX + 1.0));
}

/* ---------------------------------------------------------------- global  */

static JSVal fn_parseint(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_to_string(pCtx, ARG(0));
    const char *pData = js_str_data(text);
    size_t nSize = js_str_len(text);
    size_t i = 0;
    int nRadix = (nArgc > 1) ? (int)js_to_int64(pCtx, pArgv[1]) : 0;
    int bNegative = 0;
    double nResult = 0;
    int bAny = 0;

    (void)thisVal;
    (void)pUser;

    while ((i < nSize) && ((unsigned char)pData[i] <= ' ')) {
        i++;
    }

    if ((i < nSize) && ((pData[i] == '+') || (pData[i] == '-'))) {
        bNegative = (pData[i] == '-');
        i++;
    }

    if ((nRadix == 0) || (nRadix == 16)) {
        if ((i + 1 < nSize) && (pData[i] == '0') && ((pData[i + 1] == 'x') || (pData[i + 1] == 'X'))) {
            i += 2;
            nRadix = 16;
        } else if (nRadix == 0) {
            nRadix = 10;
        }
    }

    if ((nRadix < 2) || (nRadix > 36)) {
        js_release(pCtx, text);

        return js_num(xx_rt_nan());
    }

    for (; i < nSize; i++) {
        int nDigit = -1;
        char nChar = pData[i];

        if ((nChar >= '0') && (nChar <= '9')) {
            nDigit = nChar - '0';
        } else if ((nChar >= 'a') && (nChar <= 'z')) {
            nDigit = nChar - 'a' + 10;
        } else if ((nChar >= 'A') && (nChar <= 'Z')) {
            nDigit = nChar - 'A' + 10;
        }

        if ((nDigit < 0) || (nDigit >= nRadix)) {
            break;
        }

        nResult = nResult * nRadix + nDigit;
        bAny = 1;
    }

    js_release(pCtx, text);

    if (!bAny) {
        return js_num(xx_rt_nan());
    }

    return js_num(bNegative ? -nResult : nResult);
}

static JSVal fn_parsefloat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_to_string(pCtx, ARG(0));
    const char *pData = js_str_data(text);
    const char *pEnd = NULL;
    double nResult = xx_rt_strtod(pData, &pEnd);

    (void)thisVal;
    (void)pUser;

    if (pEnd == pData) {
        nResult = xx_rt_nan();
    }

    js_release(pCtx, text);

    return js_num(nResult);
}

static JSVal fn_isnan(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = js_to_number(pCtx, ARG(0));

    (void)thisVal;
    (void)pUser;

    return js_bool(nValue != nValue);
}

static JSVal fn_isfinite(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = js_to_number(pCtx, ARG(0));

    (void)thisVal;
    (void)pUser;

    return js_bool((nValue == nValue) && (nValue != xx_rt_inf()) && (nValue != (-xx_rt_inf())));
}

static JSVal fn_error_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    /* Error.prototype has to be the prototype, otherwise the instance never
     * finds its own toString and stringifies as "[object Object]".        */
    JSVal object = jsval_obj(jsobj_new(pCtx, JCLASS_ERROR, pCtx->pErrorProto));

    (void)thisVal;
    (void)pUser;

    js_set(pCtx, object, "message", (nArgc > 0) ? js_to_string(pCtx, pArgv[0]) : js_str(pCtx, ""));
    js_set(pCtx, object, "name", js_str(pCtx, "Error"));

    return object;
}

static JSVal fn_error_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal name = js_get(pCtx, thisVal, "name");
    JSVal message = js_get(pCtx, thisVal, "message");
    xx_buf_t buf;
    JSVal result;

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    xx_buf_init(&buf);
    xx_buf_append_str(&buf, (name.tag == JT_STR) ? js_str_data(name) : "Error");

    if (js_str_len(message)) {
        xx_buf_append_str(&buf, ": ");
        xx_buf_append(&buf, js_str_data(message), js_str_len(message));
    }

    result = js_strn(pCtx, buf.data, buf.size);
    xx_buf_free(&buf);
    js_release(pCtx, name);
    js_release(pCtx, message);

    return result;
}

/* ------------------------------------------------------------------ JSON  */

static void json_quote(xx_buf_t *pBuf, const char *pData, size_t nSize)
{
    size_t i = 0;

    xx_buf_append_char(pBuf, '"');

    for (i = 0; i < nSize; i++) {
        unsigned char nChar = (unsigned char)pData[i];

        switch (nChar) {
            case '"': xx_buf_append_str(pBuf, "\\\""); break;
            case '\\': xx_buf_append_str(pBuf, "\\\\"); break;
            case '\n': xx_buf_append_str(pBuf, "\\n"); break;
            case '\r': xx_buf_append_str(pBuf, "\\r"); break;
            case '\t': xx_buf_append_str(pBuf, "\\t"); break;
            case '\b': xx_buf_append_str(pBuf, "\\b"); break;
            case '\f': xx_buf_append_str(pBuf, "\\f"); break;
            default:
                if (nChar < 0x20) {
                    xx_buf_appendf(pBuf, "\\u%04x", nChar);
                } else {
                    xx_buf_append_char(pBuf, (char)nChar);
                }
                break;
        }
    }

    xx_buf_append_char(pBuf, '"');
}

static void json_stringify_value(JSCtx *pCtx, xx_buf_t *pBuf, JSVal value, int nDepth);

/* Serialises a value that has already had toJSON applied to it (or that never
 * had one).  Keeping this separate is what stops an object returned BY toJSON
 * from having its own toJSON invoked again, which is both what the spec says
 * and what the reference engine does.                                       */
static void json_stringify_raw(JSCtx *pCtx, xx_buf_t *pBuf, JSVal value, int nDepth)
{
    if (nDepth > 64) {
        xx_buf_append_str(pBuf, "null");
        return;
    }

    switch (value.tag) {
        case JT_UNDEF:
        case JT_NULL: xx_buf_append_str(pBuf, "null"); break;
        case JT_BOOL: xx_buf_append_str(pBuf, value.u.b ? "true" : "false"); break;
        case JT_NUM: {
            /* Number formatting needs no short-lived JSStr allocation. */
            char sNumber[64];
            int nSize = js_int_to_buf(sNumber, value.u.n);
            if (nSize <= 0) {
                xx_rt_dtoa_shortest(value.u.n, sNumber, sizeof(sNumber));
                nSize = (int)xx_rt_strlen(sNumber);
            }
            xx_buf_append(pBuf, sNumber, (size_t)nSize);
            break;
        }
        case JT_STR: json_quote(pBuf, js_str_data(value), js_str_len(value)); break;
        case JT_OBJ: {
            if (js_is_callable(value)) {
                xx_buf_append_str(pBuf, "null");
            } else if (value.u.o->cls == JCLASS_ARRAY) {
                int64_t nLength = js_array_length(pCtx, value);
                int64_t i = 0;

                xx_buf_append_char(pBuf, '[');

                for (i = 0; i < nLength; i++) {
                    JSVal item = js_get_index(pCtx, value, i);

                    if (i > 0) {
                        xx_buf_append_char(pBuf, ',');
                    }

                    json_stringify_value(pCtx, pBuf, item, nDepth + 1);
                    js_release(pCtx, item);
                }

                xx_buf_append_char(pBuf, ']');
            } else {
                JSPropMap *pMap = &value.u.o->props;
                size_t i = 0;
                int bFirst = 1;

                xx_buf_append_char(pBuf, '{');

                for (i = 0; i < pMap->nSize; i++) {
                    if (pMap->pEntries[i].bDeleted || pMap->pEntries[i].bDontEnum) {
                        continue;
                    }

                    /* Properties holding undefined or a function are omitted, as
                     * JSON has no spelling for them (array slots write null). */
                    if ((pMap->pEntries[i].value.tag == JT_UNDEF) ||
                        ((pMap->pEntries[i].value.tag == JT_OBJ) && js_is_callable(pMap->pEntries[i].value))) {
                        continue;
                    }

                    if (!bFirst) {
                        xx_buf_append_char(pBuf, ',');
                    }

                    bFirst = 0;
                    json_quote(pBuf, pMap->pEntries[i].pKey, pMap->pEntries[i].nKeySize);
                    xx_buf_append_char(pBuf, ':');
                    json_stringify_value(pCtx, pBuf, pMap->pEntries[i].value, nDepth + 1);
                }

                xx_buf_append_char(pBuf, '}');
            }

            break;
        }

        default: break;
    }
}

static void json_stringify_value(JSCtx *pCtx, xx_buf_t *pBuf, JSVal value, int nDepth)
{
    /* An object that supplies toJSON is serialised through it - the only
     * reason Date.prototype.toJSON exists at all.  The replacement value is
     * then serialised raw, so a toJSON that returns the receiver terminates
     * instead of recursing.                                               */
    if ((value.tag == JT_OBJ) && (!js_is_callable(value))) {
        JSVal toJson = js_get(pCtx, value, "toJSON");

        if (js_is_callable(toJson)) {
            JSVal replaced = js_call(pCtx, toJson, value, 0, NULL);

            js_release(pCtx, toJson);
            json_stringify_raw(pCtx, pBuf, replaced, nDepth);
            js_release(pCtx, replaced);

            return;
        }

        js_release(pCtx, toJson);
    }

    json_stringify_raw(pCtx, pBuf, value, nDepth);
}

static JSVal fn_json_stringify(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    xx_buf_t buf;
    JSVal result;

    (void)thisVal;
    (void)pUser;

    /* At the top level undefined and functions serialise to undefined; the
     * "null" spelling is for nested values only. */
    if ((ARG(0).tag == JT_UNDEF) || ((ARG(0).tag == JT_OBJ) && js_is_callable(ARG(0)))) {
        return js_undefined();
    }

    xx_buf_init(&buf);
    json_stringify_value(pCtx, &buf, ARG(0), 0);
    if (js_has_exception(pCtx)) {
        xx_buf_free(&buf);
        return js_undefined();
    }
    result = string_from_buffer(pCtx, &buf);
    xx_buf_free(&buf);

    return result;
}

typedef struct {
    const char *p;
    JSCtx *pCtx;
    int bError;
    int nDepth;
} JsonParser;

/* json_parse_value recurses once per nesting level and its frame is about
 * 200 bytes, so this bound costs well under half a megabyte of stack while
 * being orders of magnitude deeper than any real document.  A limit of any
 * size necessarily diverges from the reference engine, which keeps going
 * until its own stack runs out.                                           */
#define JSON_MAX_DEPTH 2048

static JSVal json_parse_value(JsonParser *pParser);

static void json_skip_ws(JsonParser *pParser)
{
    while ((*pParser->p == ' ') || (*pParser->p == '\t') || (*pParser->p == '\n') || (*pParser->p == '\r')) {
        pParser->p++;
    }
}

static JSVal json_parse_value(JsonParser *pParser)
{
    JSCtx *pCtx = pParser->pCtx;

    json_skip_ws(pParser);

    if ((*pParser->p == '{') || (*pParser->p == '[')) {
        if (pParser->nDepth >= JSON_MAX_DEPTH) {
            pParser->bError = 1;

            return js_null();
        }
    }

    if (*pParser->p == '{') {
        JSVal object = js_new_object(pCtx);

        pParser->nDepth++;
        pParser->p++;
        json_skip_ws(pParser);

        if (*pParser->p == '}') {
            pParser->p++;
            pParser->nDepth--;

            return object;
        }

        for (;;) {
            JSVal key;
            JSVal value;

            json_skip_ws(pParser);
            key = json_parse_value(pParser);
            json_skip_ws(pParser);

            if (*pParser->p != ':') {
                pParser->bError = 1;
                js_release(pCtx, key);
                pParser->nDepth--;

                return object;
            }

            pParser->p++;
            value = json_parse_value(pParser);
            js_set(pCtx, object, js_str_data(key), value);
            js_release(pCtx, key);
            json_skip_ws(pParser);

            if (*pParser->p == ',') {
                pParser->p++;
                continue;
            }

            if (*pParser->p == '}') {
                pParser->p++;
            } else {
                pParser->bError = 1;
            }

            break;
        }

        pParser->nDepth--;

        return object;
    }

    if (*pParser->p == '[') {
        JSVal array = js_new_array(pCtx);
        int64_t nCount = 0;

        pParser->nDepth++;
        pParser->p++;
        json_skip_ws(pParser);

        if (*pParser->p == ']') {
            pParser->p++;
            pParser->nDepth--;

            return array;
        }

        for (;;) {
            js_set_index(pCtx, array, nCount++, json_parse_value(pParser));
            json_skip_ws(pParser);

            if (*pParser->p == ',') {
                pParser->p++;
                continue;
            }

            if (*pParser->p == ']') {
                pParser->p++;
            } else {
                pParser->bError = 1;
            }

            break;
        }

        pParser->nDepth--;

        return array;
    }

    if (*pParser->p == '"') {
        xx_buf_t buf;
        JSVal result;

        xx_buf_init(&buf);
        pParser->p++;

        while (*pParser->p && (*pParser->p != '"')) {
            if (*pParser->p == '\\') {
                int bBadEscape = 0;

                pParser->p++;

                /* A backslash immediately before the terminator must not
                 * push the cursor past the end of the string.            */
                if (*pParser->p == 0) {
                    pParser->bError = 1;
                    break;
                }

                switch (*pParser->p) {
                    case 'n': xx_buf_append_char(&buf, '\n'); break;
                    case 't': xx_buf_append_char(&buf, '\t'); break;
                    case 'r': xx_buf_append_char(&buf, '\r'); break;
                    case 'b': xx_buf_append_char(&buf, '\b'); break;
                    case 'f': xx_buf_append_char(&buf, '\f'); break;
                    case 'u': {
                        unsigned int nCode = 0;
                        int i = 0;

                        /* Stops at the first byte that is not a hex digit,
                         * so a truncated escape cannot read past the end. */
                        for (i = 1; i <= 4; i++) {
                            char nChar = pParser->p[i];
                            int nDigit = -1;

                            if ((nChar >= '0') && (nChar <= '9')) {
                                nDigit = nChar - '0';
                            } else if ((nChar >= 'a') && (nChar <= 'f')) {
                                nDigit = nChar - 'a' + 10;
                            } else if ((nChar >= 'A') && (nChar <= 'F')) {
                                nDigit = nChar - 'A' + 10;
                            }

                            if (nDigit < 0) {
                                break;
                            }

                            nCode = nCode * 16 + (unsigned int)nDigit;
                        }

                        if (i <= 4) {
                            bBadEscape = 1;
                            break;
                        }

                        if (nCode < 0x80) {
                            xx_buf_append_char(&buf, (char)nCode);
                        } else if (nCode < 0x800) {
                            xx_buf_append_char(&buf, (char)(0xC0 | (nCode >> 6)));
                            xx_buf_append_char(&buf, (char)(0x80 | (nCode & 0x3F)));
                        } else {
                            xx_buf_append_char(&buf, (char)(0xE0 | (nCode >> 12)));
                            xx_buf_append_char(&buf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
                            xx_buf_append_char(&buf, (char)(0x80 | (nCode & 0x3F)));
                        }

                        pParser->p += 4;
                        break;
                    }
                    default: xx_buf_append_char(&buf, *pParser->p); break;
                }

                if (bBadEscape) {
                    pParser->bError = 1;
                    break;
                }

                pParser->p++;
            } else {
                xx_buf_append_char(&buf, *pParser->p);
                pParser->p++;
            }
        }

        if (*pParser->p == '"') {
            pParser->p++;
        } else {
            /* An unterminated string is not JSON. */
            pParser->bError = 1;
        }

        result = js_strn(pCtx, buf.data, buf.size);
        xx_buf_free(&buf);

        return result;
    }

    if (xx_rt_strncmp(pParser->p, "true", 4) == 0) {
        pParser->p += 4;

        return js_bool(1);
    }

    if (xx_rt_strncmp(pParser->p, "false", 5) == 0) {
        pParser->p += 5;

        return js_bool(0);
    }

    if (xx_rt_strncmp(pParser->p, "null", 4) == 0) {
        pParser->p += 4;

        return js_null();
    }

    {
        const char *pEnd = NULL;
        double nValue = xx_rt_strtod(pParser->p, &pEnd);

        if (pEnd == pParser->p) {
            pParser->bError = 1;

            return js_undefined();
        }

        pParser->p = pEnd;

        return js_num(nValue);
    }
}

static JSVal fn_json_parse(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_to_string(pCtx, ARG(0));
    JsonParser parser;
    JSVal result;

    (void)thisVal;
    (void)pUser;

    parser.p = js_str_data(text);
    parser.pCtx = pCtx;
    parser.bError = 0;
    parser.nDepth = 0;

    result = json_parse_value(&parser);
    /* Anything but white space after the value makes the document invalid. */
    json_skip_ws(&parser);
    if (*parser.p) {
        parser.bError = 1;
    }
    js_release(pCtx, text);

    if (parser.bError) {
        js_release(pCtx, result);

        return js_throw(pCtx, "SyntaxError: invalid JSON");
    }

    return result;
}

/* ------------------------------------------------------------ installer  */

static void def_proto_method(JSCtx *pCtx, JSObj *pProto, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    jsobj_put_hidden(pCtx, pProto, pName, js_new_native(pCtx, pName, fn, nArgc, pUser));
}

static JSVal make_constructor(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, JSObj *pProto)
{
    JSVal ctor = js_new_native(pCtx, pName, fn, nArgc, NULL);

    jsobj_put_hidden(pCtx, ctor.u.o, "prototype", jsval_obj(jsobj_ref(pProto)));
    jsobj_put_hidden(pCtx, pProto, "constructor", js_dup(ctor));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, pName, js_dup(ctor));

    return ctor;
}

void js_install_builtins(JSCtx *pCtx)
{
    /* Object.prototype */
    def_proto_method(pCtx, pCtx->pObjectProto, "hasOwnProperty", fn_object_hasownproperty, 1, NULL);
    def_proto_method(pCtx, pCtx->pObjectProto, "toString", fn_object_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pObjectProto, "toLocaleString", fn_object_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pObjectProto, "valueOf", fn_object_valueof, 0, NULL);

    /* Function.prototype */
    def_proto_method(pCtx, pCtx->pFunctionProto, "call", fn_function_call, 1, NULL);
    def_proto_method(pCtx, pCtx->pFunctionProto, "apply", fn_function_apply, 2, NULL);
    def_proto_method(pCtx, pCtx->pFunctionProto, "bind", fn_function_bind, 1, NULL);
    def_proto_method(pCtx, pCtx->pFunctionProto, "toString", fn_function_tostring, 0, NULL);

    /* Array.prototype */
    def_proto_method(pCtx, pCtx->pArrayProto, "push", fn_array_push, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "pop", fn_array_pop, 0, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "shift", fn_array_shift, 0, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "unshift", fn_array_unshift, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "join", fn_array_join, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "slice", fn_array_slice, 2, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "splice", fn_array_splice, 2, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "concat", fn_array_concat, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "indexOf", fn_array_indexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "lastIndexOf", fn_array_lastindexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "reverse", fn_array_reverse, 0, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "sort", fn_array_sort, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "forEach", fn_array_foreach, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "map", fn_array_map, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "filter", fn_array_filter, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "every", fn_array_every, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "some", fn_array_some, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "reduce", fn_array_reduce, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "toString", fn_object_tostring, 0, NULL);

    /* String.prototype */
    def_proto_method(pCtx, pCtx->pStringProto, "toString", fn_string_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "valueOf", fn_string_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "charAt", fn_string_charat, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "charCodeAt", fn_string_charcodeat, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "indexOf", fn_string_indexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "lastIndexOf", fn_string_lastindexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "slice", fn_string_slice, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "substring", fn_string_substring, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "substr", fn_string_substr, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toUpperCase", fn_string_toupper, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toLowerCase", fn_string_tolower, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toLocaleUpperCase", fn_string_toupper, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toLocaleLowerCase", fn_string_tolower, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "trim", fn_string_trim, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "concat", fn_string_concat, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "split", js_regex_string_split, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "match", js_regex_string_match, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "search", js_regex_string_search, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "replace", js_regex_string_replace, 2, NULL);

    /* Number.prototype */
    def_proto_method(pCtx, pCtx->pNumberProto, "toString", fn_number_tostring, 1, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "valueOf", fn_number_valueof, 0, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "toFixed", fn_number_tofixed, 1, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "toPrecision", fn_number_toprecision, 1, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "toLocaleString", fn_number_tostring, 0, NULL);

    /* Boolean.prototype */
    def_proto_method(pCtx, pCtx->pBooleanProto, "toString", fn_boolean_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pBooleanProto, "valueOf", fn_boolean_valueof, 0, NULL);

    /* Error.prototype */
    def_proto_method(pCtx, pCtx->pErrorProto, "toString", fn_error_tostring, 0, NULL);

    /* Constructors */
    {
        JSVal objectCtor = make_constructor(pCtx, "Object", fn_object_ctor, 1, pCtx->pObjectProto);
        JSVal uint32Ctor = make_constructor(pCtx, "Uint32Array", fn_uint32array_ctor, 1, pCtx->pUint32ArrayProto);
        jsobj_put_hidden(pCtx, uint32Ctor.u.o, "BYTES_PER_ELEMENT", js_num(4));
        jsobj_put_hidden(pCtx, pCtx->pUint32ArrayProto, "BYTES_PER_ELEMENT", js_num(4));
        JSVal arrayCtor = make_constructor(pCtx, "Array", fn_array_ctor, 1, pCtx->pArrayProto);
        JSVal stringCtor = make_constructor(pCtx, "String", fn_string_ctor, 1, pCtx->pStringProto);
        JSVal numberCtor = make_constructor(pCtx, "Number", fn_number_ctor, 1, pCtx->pNumberProto);
        JSVal booleanCtor = make_constructor(pCtx, "Boolean", fn_boolean_ctor, 1, pCtx->pBooleanProto);
        JSVal errorCtor = make_constructor(pCtx, "Error", fn_error_ctor, 1, pCtx->pErrorProto);
        JSVal functionCtor = make_constructor(pCtx, "Function", fn_object_ctor, 1, pCtx->pFunctionProto);

        js_def_method(pCtx, objectCtor, "keys", fn_object_keys, 1, NULL);
        js_def_method(pCtx, objectCtor, "defineProperty", fn_object_defineproperty, 3, NULL);
        js_def_method(pCtx, arrayCtor, "isArray", fn_array_isarray, 1, NULL);
        js_def_method(pCtx, stringCtor, "fromCharCode", fn_string_fromcharcode, 1, NULL);

        jsobj_put_hidden(pCtx, numberCtor.u.o, "MAX_SAFE_INTEGER", js_num(9007199254740991.0));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "MIN_SAFE_INTEGER", js_num(-9007199254740991.0));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "MAX_VALUE", js_num(1.7976931348623157e308));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "MIN_VALUE", js_num(5e-324));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "NaN", js_num(xx_rt_nan()));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "POSITIVE_INFINITY", js_num(xx_rt_inf()));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "NEGATIVE_INFINITY", js_num((-xx_rt_inf())));

        js_release(pCtx, objectCtor);
        js_release(pCtx, arrayCtor);
        js_release(pCtx, uint32Ctor);
        js_release(pCtx, stringCtor);
        js_release(pCtx, numberCtor);
        js_release(pCtx, booleanCtor);
        js_release(pCtx, errorCtor);
        js_release(pCtx, functionCtor);
    }

    /* Date - the prototype is not shared, so the constructor carries it as
     * its user pointer instead of living on the context.                  */
    {
        JSObj *pDateProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
        JSVal dateCtor = js_new_native(pCtx, "Date", fn_date_ctor, 7, pDateProto);

        def_proto_method(pCtx, pDateProto, "getTime", fn_date_get, 0, (void *)(size_t)DATE_TIME);
        def_proto_method(pCtx, pDateProto, "valueOf", fn_date_get, 0, (void *)(size_t)DATE_TIME);
        def_proto_method(pCtx, pDateProto, "getFullYear", fn_date_get, 0, (void *)(size_t)DATE_YEAR);
        def_proto_method(pCtx, pDateProto, "getUTCFullYear", fn_date_get, 0, (void *)(size_t)DATE_YEAR);
        def_proto_method(pCtx, pDateProto, "getMonth", fn_date_get, 0, (void *)(size_t)DATE_MONTH);
        def_proto_method(pCtx, pDateProto, "getUTCMonth", fn_date_get, 0, (void *)(size_t)DATE_MONTH);
        def_proto_method(pCtx, pDateProto, "getDate", fn_date_get, 0, (void *)(size_t)DATE_DATE);
        def_proto_method(pCtx, pDateProto, "getUTCDate", fn_date_get, 0, (void *)(size_t)DATE_DATE);
        def_proto_method(pCtx, pDateProto, "getDay", fn_date_get, 0, (void *)(size_t)DATE_DAY);
        def_proto_method(pCtx, pDateProto, "getUTCDay", fn_date_get, 0, (void *)(size_t)DATE_DAY);
        def_proto_method(pCtx, pDateProto, "getHours", fn_date_get, 0, (void *)(size_t)DATE_HOURS);
        def_proto_method(pCtx, pDateProto, "getUTCHours", fn_date_get, 0, (void *)(size_t)DATE_HOURS);
        def_proto_method(pCtx, pDateProto, "getMinutes", fn_date_get, 0, (void *)(size_t)DATE_MINUTES);
        def_proto_method(pCtx, pDateProto, "getUTCMinutes", fn_date_get, 0, (void *)(size_t)DATE_MINUTES);
        def_proto_method(pCtx, pDateProto, "getSeconds", fn_date_get, 0, (void *)(size_t)DATE_SECONDS);
        def_proto_method(pCtx, pDateProto, "getUTCSeconds", fn_date_get, 0, (void *)(size_t)DATE_SECONDS);
        def_proto_method(pCtx, pDateProto, "getMilliseconds", fn_date_get, 0, (void *)(size_t)DATE_MS);
        def_proto_method(pCtx, pDateProto, "getUTCMilliseconds", fn_date_get, 0, (void *)(size_t)DATE_MS);
        def_proto_method(pCtx, pDateProto, "getTimezoneOffset", fn_date_get, 0, (void *)(size_t)DATE_TZOFFSET);
        def_proto_method(pCtx, pDateProto, "toISOString", fn_date_toisostring, 0, NULL);
        def_proto_method(pCtx, pDateProto, "toJSON", fn_date_toisostring, 1, NULL);
        def_proto_method(pCtx, pDateProto, "toString", fn_date_tostring, 0, NULL);

        jsobj_put_hidden(pCtx, dateCtor.u.o, "prototype", jsval_obj(jsobj_ref(pDateProto)));
        jsobj_put_hidden(pCtx, pDateProto, "constructor", js_dup(dateCtor));
        jsobj_put_hidden(pCtx, pCtx->pGlobal, "Date", js_dup(dateCtor));

        js_def_method(pCtx, dateCtor, "UTC", fn_date_utc, 7, NULL);
        js_def_method(pCtx, dateCtor, "now", fn_date_now, 0, NULL);

        js_release(pCtx, dateCtor);
    }

    /* Math */
    {
        JSVal math = js_new_object(pCtx);

        js_def_method(pCtx, math, "abs", fn_math_unary, 1, (void *)(size_t)MATH_ABS);
        js_def_method(pCtx, math, "floor", fn_math_unary, 1, (void *)(size_t)MATH_FLOOR);
        js_def_method(pCtx, math, "ceil", fn_math_unary, 1, (void *)(size_t)MATH_CEIL);
        js_def_method(pCtx, math, "round", fn_math_unary, 1, (void *)(size_t)MATH_ROUND);
        js_def_method(pCtx, math, "sqrt", fn_math_unary, 1, (void *)(size_t)MATH_SQRT);
        js_def_method(pCtx, math, "log", fn_math_unary, 1, (void *)(size_t)MATH_LOG);
        js_def_method(pCtx, math, "log2", fn_math_unary, 1, (void *)(size_t)MATH_LOG2);
        js_def_method(pCtx, math, "log10", fn_math_unary, 1, (void *)(size_t)MATH_LOG10);
        js_def_method(pCtx, math, "exp", fn_math_unary, 1, (void *)(size_t)MATH_EXP);
        js_def_method(pCtx, math, "sin", fn_math_unary, 1, (void *)(size_t)MATH_SIN);
        js_def_method(pCtx, math, "cos", fn_math_unary, 1, (void *)(size_t)MATH_COS);
        js_def_method(pCtx, math, "tan", fn_math_unary, 1, (void *)(size_t)MATH_TAN);
        js_def_method(pCtx, math, "trunc", fn_math_unary, 1, (void *)(size_t)MATH_TRUNC);
        js_def_method(pCtx, math, "sign", fn_math_unary, 1, (void *)(size_t)MATH_SIGN);
        js_def_method(pCtx, math, "min", fn_math_min, 2, NULL);
        js_def_method(pCtx, math, "max", fn_math_max, 2, NULL);
        js_def_method(pCtx, math, "pow", fn_math_pow, 2, NULL);
        js_def_method(pCtx, math, "random", fn_math_random, 0, NULL);

        jsobj_put_hidden(pCtx, math.u.o, "PI", js_num(3.141592653589793));
        jsobj_put_hidden(pCtx, math.u.o, "E", js_num(2.718281828459045));
        jsobj_put_hidden(pCtx, math.u.o, "LN2", js_num(0.6931471805599453));
        jsobj_put_hidden(pCtx, math.u.o, "LN10", js_num(2.302585092994046));

        jsobj_put_hidden(pCtx, pCtx->pGlobal, "Math", math);
    }

    /* JSON */
    {
        JSVal json = js_new_object(pCtx);

        js_def_method(pCtx, json, "stringify", fn_json_stringify, 3, NULL);
        js_def_method(pCtx, json, "parse", fn_json_parse, 2, NULL);
        jsobj_put_hidden(pCtx, pCtx->pGlobal, "JSON", json);
    }

    /* Global functions and values */
    js_def_fn(pCtx, "parseInt", fn_parseint, 2, NULL);
    js_def_fn(pCtx, "parseFloat", fn_parsefloat, 1, NULL);
    js_def_fn(pCtx, "isNaN", fn_isnan, 1, NULL);
    js_def_fn(pCtx, "isFinite", fn_isfinite, 1, NULL);

    jsobj_put_hidden(pCtx, pCtx->pGlobal, "NaN", js_num(xx_rt_nan()));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, "Infinity", js_num(xx_rt_inf()));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, "undefined", js_undefined());

    js_install_regexp(pCtx);
}
