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

/* js_value.c - values, strings, property maps and objects. */

#include "xx_js_internal.h"

/* --------------------------------------------------------------- strings  */

JSStr *jsstr_new(JSCtx *pCtx, const char *pData, size_t nSize)
{
    JSStr *pStr;
    if (!nSize && pCtx->pEmptyStr) return jsstr_ref(pCtx->pEmptyStr);
    pStr = (JSStr *)xx_js_malloc(sizeof(JSStr));

    pStr->nRef = 1;
    pStr->nSize = nSize;
    pStr->nCapacity = nSize;
    pStr->pData = (char *)xx_js_malloc(nSize + 1);

    if (nSize && pData) {
        xx_rt_memcpy(pStr->pData, pData, nSize);
    }

    pStr->pData[nSize] = 0;

    pStr->pPrevAll = NULL;
    pStr->pNextAll = pCtx->pAllStrings;

    if (pCtx->pAllStrings) {
        pCtx->pAllStrings->pPrevAll = pStr;
    }

    pCtx->pAllStrings = pStr;
    /* A context-owned reference keeps the shared empty string immutable:
     * in-place append's nRef==2 guard cannot mutate this cached value. */
    if (!nSize) pCtx->pEmptyStr = jsstr_ref(pStr);

    return pStr;
}

JSVal js_str_take(JSCtx *pCtx, char *pData, size_t nSize, size_t nCapacity)
{
    JSStr *pStr;
    if (!nSize) {
        xx_js_free(pData);
        return js_str(pCtx, "");
    }
    if (!pData || nCapacity <= nSize) {
        JSVal result = js_strn(pCtx, pData, nSize);
        xx_js_free(pData);
        return result;
    }
    pStr = (JSStr *)xx_js_malloc(sizeof(*pStr));
    pStr->nRef = 1;
    pStr->nSize = nSize;
    pStr->nCapacity = nCapacity - 1;
    pStr->pData = pData;
    pData[nSize] = 0;
    pStr->pPrevAll = NULL;
    pStr->pNextAll = pCtx->pAllStrings;
    if (pCtx->pAllStrings) pCtx->pAllStrings->pPrevAll = pStr;
    pCtx->pAllStrings = pStr;
    return jsval_str(pStr);
}

JSStr *jsstr_ref(JSStr *pStr)
{
    if (pStr) {
        pStr->nRef++;
    }

    return pStr;
}

void jsstr_unref(JSCtx *pCtx, JSStr *pStr)
{
    if (pStr == NULL) {
        return;
    }

    pStr->nRef--;

    if (pStr->nRef > 0) {
        return;
    }

    if (pStr->pPrevAll) {
        pStr->pPrevAll->pNextAll = pStr->pNextAll;
    } else {
        pCtx->pAllStrings = pStr->pNextAll;
    }

    if (pStr->pNextAll) {
        pStr->pNextAll->pPrevAll = pStr->pPrevAll;
    }

    xx_js_free(pStr->pData);
    xx_js_free(pStr);
}

/* -------------------------------------------------------------- property  */

void jsprops_init(JSPropMap *pMap)
{
    xx_rt_memset(pMap, 0, sizeof(*pMap));
}

/* Puts value into an entry: the entry takes over the caller's reference and
 * the previous one is released. A stored object also carries a count of the
 * property maps that hold it, and jsobj_unref refuses to free anything with
 * a map still pointing at it. A caller that releases one time too many then
 * costs a delayed reclamation - the entry lives until the final sweep, as
 * everything did before - instead of leaving a dangling entry behind.     */
void jsprop_store(JSCtx *pCtx, JSProp *pProp, JSVal value)
{
    JSVal old = pProp->value;

    if (value.tag == JT_OBJ) {
        value.u.o->nMapRefs++;
    }

    pProp->value = value;

    if (old.tag == JT_OBJ) {
        old.u.o->nMapRefs--;
    }

    js_release(pCtx, old);
}

void jsprops_free(JSCtx *pCtx, JSPropMap *pMap)
{
    size_t i = 0;

    for (i = 0; i < pMap->nSize; i++) {
        if (!pMap->pEntries[i].bBorrowedKey) xx_js_free(pMap->pEntries[i].pKey);

        if (!pMap->pEntries[i].bDeleted) {
            if (pMap->pEntries[i].value.tag == JT_OBJ) {
                pMap->pEntries[i].value.u.o->nMapRefs--;
            }

            js_release(pCtx, pMap->pEntries[i].value);
        }
    }

    xx_js_free(pMap->pEntries);
    xx_js_free(pMap->pIndex);
    jsprops_init(pMap);
}

/* -1 is empty; -2 is a deleted hash slot. Deleted entries release their
 * key immediately; insertion appends a new entry, preserving key order. */
static void jsprops_rehash(JSPropMap *pMap)
{
    size_t nNew = 16, i;
    while (nNew < (pMap->nLive + 1) * 2) nNew *= 2;
    xx_js_free(pMap->pIndex);
    pMap->pIndex = (int32_t *)xx_js_malloc(nNew * sizeof(int32_t));
    pMap->nIndexSize = nNew;
    for (i = 0; i < nNew; i++) pMap->pIndex[i] = -1;
    for (i = 0; i < pMap->nSize; i++) {
        size_t nSlot;
        if (pMap->pEntries[i].bDeleted) continue;
        nSlot = pMap->pEntries[i].nHash & (nNew - 1);
        while (pMap->pIndex[nSlot] != -1) nSlot = (nSlot + 1) & (nNew - 1);
        pMap->pIndex[nSlot] = (int32_t)i;
    }
}

/* Compaction invalidates entry pointers. Call only at the end of insertion
 * or public object deletion, after internal JSProp users have finished.
 * for-in and Object.keys use owned key snapshots. */
static void jsprops_compact(JSPropMap *pMap)
{
    size_t i, nOut = 0, nNew = 8;
    for (i = 0; i < pMap->nSize; i++) {
        if (!pMap->pEntries[i].bDeleted) pMap->pEntries[nOut++] = pMap->pEntries[i];
    }
    pMap->nSize = nOut;
    while (nNew < nOut + 1) nNew *= 2;
    if (nNew < pMap->nCapacity) {
        pMap->pEntries = (JSProp *)xx_js_realloc(pMap->pEntries, nNew * sizeof(JSProp));
        pMap->nCapacity = nNew;
    }
    jsprops_rehash(pMap);
}

JSProp *jsprops_find_hashed(JSPropMap *pMap, const char *pKey, size_t nKeySize, uint32_t nHash)
{
    size_t nSlot, nProbe;
    if (!pMap->nIndexSize) return NULL;
    nSlot = nHash & (pMap->nIndexSize - 1);
    for (nProbe = 0; nProbe < pMap->nIndexSize; nProbe++) {
        int32_t nIndex = pMap->pIndex[nSlot];
        if (nIndex == -1) return NULL;
        if (nIndex >= 0) {
            JSProp *pProp = &pMap->pEntries[nIndex];
            if (pProp->nHash == nHash && pProp->nKeySize == nKeySize && !xx_rt_memcmp(pProp->pKey, pKey, nKeySize)) return pProp;
        }
        nSlot = (nSlot + 1) & (pMap->nIndexSize - 1);
    }
    return NULL;
}

JSProp *jsprops_find(JSPropMap *pMap, const char *pKey, size_t nKeySize)
{
    return pMap->nIndexSize ? jsprops_find_hashed(pMap, pKey, nKeySize, xx_js_hash_str(pKey, nKeySize)) : NULL;
}

static JSProp *jsprops_put_impl(JSCtx *pCtx, JSPropMap *pMap, const char *pKey, size_t nKeySize, int bBorrowedKey)
{
    uint32_t nHash = xx_js_hash_str(pKey, nKeySize);
    size_t nSlot, nProbe, nDeleted = (size_t)-1;
    JSProp *pProp = jsprops_find_hashed(pMap, pKey, nKeySize, nHash);
    (void)pCtx;
    if (pProp) return pProp;
    if (pMap->nSize > 32 && pMap->nSize - pMap->nLive > pMap->nLive) jsprops_compact(pMap);
    if (!pMap->nIndexSize || (pMap->nLive + 1) * 2 > pMap->nIndexSize) jsprops_rehash(pMap);
    nSlot = nHash & (pMap->nIndexSize - 1);
    for (nProbe = 0; nProbe < pMap->nIndexSize; nProbe++) {
        if (pMap->pIndex[nSlot] == -1) break;
        if (pMap->pIndex[nSlot] == -2 && nDeleted == (size_t)-1) nDeleted = nSlot;
        nSlot = (nSlot + 1) & (pMap->nIndexSize - 1);
    }
    if (nDeleted != (size_t)-1) nSlot = nDeleted;
    if (pMap->nSize + 1 > pMap->nCapacity) {
        size_t nNew = pMap->nCapacity ? pMap->nCapacity * 2 : 8;
        pMap->pEntries = (JSProp *)xx_js_realloc(pMap->pEntries, nNew * sizeof(JSProp));
        pMap->nCapacity = nNew;
    }
    pProp = &pMap->pEntries[pMap->nSize];
    pProp->pKey = bBorrowedKey ? (char *)pKey : xx_js_strndup(pKey, nKeySize);
    pProp->nKeySize = nKeySize;
    pProp->nHash = nHash;
    pProp->value = js_undefined();
    pProp->bDeleted = pProp->bDontEnum = 0;
    pProp->bBorrowedKey = (uint8_t)bBorrowedKey;
    pMap->pIndex[nSlot] = (int32_t)pMap->nSize++;
    pMap->nLive++;
    return pProp;
}

JSProp *jsprops_put(JSCtx *pCtx, JSPropMap *pMap, const char *pKey, size_t nKeySize)
{
    return jsprops_put_impl(pCtx, pMap, pKey, nKeySize, 0);
}

int jsprops_del(JSCtx *pCtx, JSPropMap *pMap, const char *pKey)
{
    size_t nKeySize = xx_rt_strlen(pKey), nSlot, nProbe;
    uint32_t nHash = xx_js_hash_str(pKey, nKeySize);
    if (!pMap->nIndexSize) return 0;
    nSlot = nHash & (pMap->nIndexSize - 1);
    for (nProbe = 0; nProbe < pMap->nIndexSize; nProbe++) {
        int32_t nIndex = pMap->pIndex[nSlot];
        JSProp *pProp;
        if (nIndex == -1) return 0;
        if (nIndex < 0) { nSlot = (nSlot + 1) & (pMap->nIndexSize - 1); continue; }
        pProp = &pMap->pEntries[nIndex];
        if (pProp->nHash == nHash && pProp->nKeySize == nKeySize && !xx_rt_memcmp(pProp->pKey, pKey, nKeySize)) {
            jsprop_store(pCtx, pProp, js_undefined());
            if (!pProp->bBorrowedKey) xx_js_free(pProp->pKey);
            pProp->pKey = NULL;
            pProp->bDeleted = 1;
            pMap->pIndex[nSlot] = -2;
            if (!--pMap->nLive) {
                xx_js_free(pMap->pEntries);
                xx_js_free(pMap->pIndex);
                jsprops_init(pMap);
            }
            return 1;
        }
        nSlot = (nSlot + 1) & (pMap->nIndexSize - 1);
    }
    return 0;
}

#define JS_ARRAY_HOLE ((JSTag)-1)
#define JS_ARRAY_GAP ((JSTag)-2)
#define JS_IS_ARRAY_HOLE(value) ((int)(value).tag < 0)
#define JS_DENSE_ARRAY_LIMIT 1048576u

static size_t js_index_key(char *pKey, int64_t nIndex);

static void jsarray_store(JSCtx *pCtx, JSVal *pSlot, JSVal value)
{
    JSVal old = *pSlot;
    if (value.tag == JT_OBJ) value.u.o->nMapRefs++;
    *pSlot = value;
    if (old.tag == JT_OBJ) old.u.o->nMapRefs--;
    js_release(pCtx, old);
}

/* Descriptor changes migrate to the general map. Named metadata and sparse
 * trailing indices may coexist with a dense prefix. */
static void jsarray_to_sparse(JSCtx *pCtx, JSObj *pObj)
{
    size_t i;
    JSPropMap old;
    if (pObj->bArraySparse) return;
    old = pObj->props;
    jsprops_init(&pObj->props);
    for (i = 0; i < pObj->nArrayUsed; i++) {
        char key[32];
        size_t nKeySize;
        JSProp *pProp;
        JSVal value = pObj->pArrayData[pObj->nArrayStart + i];
        if (JS_IS_ARRAY_HOLE(value)) continue;
        nKeySize = js_index_key(key, (int64_t)i);
        pProp = jsprops_put(pCtx, &pObj->props, key, nKeySize);
        if (value.tag == JT_OBJ) value.u.o->nMapRefs--;
        jsprop_store(pCtx, pProp, value);
    }
    for (i = 0; i < old.nSize; i++) {
        JSProp *pOld = &old.pEntries[i], *pNew;
        if (pOld->bDeleted) continue;
        pNew = jsprops_put_impl(pCtx, &pObj->props, pOld->pKey, pOld->nKeySize, pOld->bBorrowedKey);
        jsprop_store(pCtx, pNew, js_dup(pOld->value));
        pNew->bDontEnum = pOld->bDontEnum;
    }
    jsprops_free(pCtx, &old);
    xx_js_free(pObj->pArrayData);
    pObj->pArrayData = NULL;
    pObj->nArrayCapacity = pObj->nArrayUsed = 0;
    pObj->nArrayStart = pObj->nArrayHoles = 0;
    pObj->bArraySparse = 1;
}

/* Growing after an enumerable named/map property would reorder Object.keys
 * relative to the legacy insertion order. Keep those new entries in props;
 * updating the existing prefix still stays dense. Hidden metadata is harmless. */
static int jsarray_can_grow(JSObj *pObj)
{
    size_t i;
    for (i = 0; i < pObj->props.nSize; i++) {
        if (!pObj->props.pEntries[i].bDeleted && !pObj->props.pEntries[i].bDontEnum) return 0;
    }
    return 1;
}

static void jsarray_reserve(JSObj *pObj, size_t nNeeded)
{
    size_t i, nNew;
    if (pObj->nArrayStart + nNeeded <= pObj->nArrayCapacity) return;
    if (pObj->nArrayStart && nNeeded <= pObj->nArrayCapacity) {
        xx_rt_memmove(pObj->pArrayData, pObj->pArrayData + pObj->nArrayStart, pObj->nArrayUsed * sizeof(JSVal));
        pObj->nArrayStart = 0;
        return;
    }
    nNew = pObj->nArrayCapacity ? pObj->nArrayCapacity : 8;
    while (nNew < nNeeded) nNew *= 2;
    if (pObj->nArrayStart) {
        xx_rt_memmove(pObj->pArrayData, pObj->pArrayData + pObj->nArrayStart, pObj->nArrayUsed * sizeof(JSVal));
        pObj->nArrayStart = 0;
    }
    pObj->pArrayData = (JSVal *)xx_js_realloc(pObj->pArrayData, nNew * sizeof(JSVal));
    for (i = pObj->nArrayUsed; i < nNew; i++) {
        pObj->pArrayData[i] = js_undefined();
        pObj->pArrayData[i].tag = JS_ARRAY_HOLE;
    }
    pObj->nArrayCapacity = nNew;
}

static int jsarray_put_dense(JSCtx *pCtx, JSObj *pObj, int64_t nIndex, JSVal value)
{
    size_t i, nSlot;
    int bNew = (uint64_t)nIndex >= pObj->nArrayUsed;
    if (pObj->bArraySparse || nIndex < 0 || nIndex >= JS_DENSE_ARRAY_LIMIT) return 0;
    if ((uint64_t)nIndex >= pObj->nArrayUsed) {
        /* Bound hole allocations and retain old ordering of arbitrary sparse
         * writes. A hole in a freshly built array can grow by at most 64. */
        if ((uint64_t)nIndex > pObj->nArrayUsed + 64 || !jsarray_can_grow(pObj)) return 0;
        jsarray_reserve(pObj, (size_t)nIndex + 1);
        for (i = pObj->nArrayUsed; i <= (size_t)nIndex; i++) {
            pObj->pArrayData[pObj->nArrayStart + i] = js_undefined();
            pObj->pArrayData[pObj->nArrayStart + i].tag = JS_ARRAY_GAP;
        }
        pObj->pArrayData[pObj->nArrayStart + nIndex].tag = JS_ARRAY_HOLE;
        pObj->nArrayHoles += (size_t)nIndex + 1 - pObj->nArrayUsed;
        pObj->nArrayUsed = (size_t)nIndex + 1;
    }
    nSlot = pObj->nArrayStart + (size_t)nIndex;
    if (pObj->pArrayData[nSlot].tag == JS_ARRAY_GAP || (!bNew && pObj->bArrayMapOrder && JS_IS_ARRAY_HOLE(pObj->pArrayData[nSlot]))) return 0;
    if (pObj->pArrayData[nSlot].tag == JS_ARRAY_HOLE) {
        /* A hidden descriptor or a previously sparse entry may own this key. */
        char key[32];
        size_t nKeySize = js_index_key(key, nIndex);
        if (jsprops_find(&pObj->props, key, nKeySize)) return 0;
        if (!jsarray_can_grow(pObj)) return 0;
        pObj->nArrayHoles--;
    }
    jsarray_store(pCtx, &pObj->pArrayData[nSlot], value);
    if (nIndex >= pObj->nArrayLen) pObj->nArrayLen = nIndex + 1;
    return 1;
}

int js_array_shift_dense(JSCtx *pCtx, JSObj *pObj, JSVal *pResult)
{
    JSVal value;
    (void)pCtx;
    if (pObj->cls != JCLASS_ARRAY || pObj->bArraySparse || pObj->nArrayHoles ||
        (uint64_t)pObj->nArrayLen != pObj->nArrayUsed) return 0;
    if (!pObj->nArrayUsed) { *pResult = js_undefined(); return 1; }
    value = pObj->pArrayData[pObj->nArrayStart];
    if (value.tag == JT_OBJ) value.u.o->nMapRefs--;
    pObj->pArrayData[pObj->nArrayStart].tag = JS_ARRAY_HOLE;
    pObj->nArrayStart++;
    pObj->nArrayUsed--;
    pObj->nArrayLen--;
    if (!pObj->nArrayUsed) pObj->nArrayStart = 0;
    *pResult = value;
    return 1;
}

int js_array_unshift_dense(JSCtx *pCtx, JSObj *pObj, int nArgc, JSVal *pArgv)
{
    size_t i, nTotal;
    if (pObj->cls != JCLASS_ARRAY || pObj->bArraySparse || pObj->nArrayHoles || nArgc < 0 ||
        (uint64_t)pObj->nArrayLen != pObj->nArrayUsed || (size_t)nArgc > JS_DENSE_ARRAY_LIMIT - pObj->nArrayUsed) return 0;
    if (!nArgc) return 1;
    /* Named keys must remain between the old prefix and subsequent entries. */
    if (!jsarray_can_grow(pObj)) return 0;
    nTotal = pObj->nArrayUsed + (size_t)nArgc;
    if (pObj->nArrayStart >= (size_t)nArgc) pObj->nArrayStart -= (size_t)nArgc;
    else {
        jsarray_reserve(pObj, nTotal);
        xx_rt_memmove(pObj->pArrayData + pObj->nArrayStart + nArgc, pObj->pArrayData + pObj->nArrayStart, pObj->nArrayUsed * sizeof(JSVal));
    }
    for (i = 0; i < (size_t)nArgc; i++) {
        JSVal value = js_dup(pArgv[i]);
        if (value.tag == JT_OBJ) value.u.o->nMapRefs++;
        pObj->pArrayData[pObj->nArrayStart + i] = value;
    }
    pObj->nArrayUsed = nTotal;
    pObj->nArrayLen = (int64_t)nTotal;
    return 1;
}

/* --------------------------------------------------------------- objects  */

JSObj *jsobj_new(JSCtx *pCtx, JSClass cls, JSObj *pProto)
{
    JSObj *pObj = (JSObj *)xx_js_calloc(1, sizeof(JSObj));

    pObj->nRef = 1;
    pObj->cls = cls;
    pObj->pProto = pProto ? jsobj_ref(pProto) : NULL;
    pObj->bExtensible = 1;
    pObj->primitive = js_undefined();
    pObj->boundThis = js_undefined();

    jsprops_init(&pObj->props);

    pObj->pPrevAll = NULL;
    pObj->pNextAll = pCtx->pAllObjects;

    if (pCtx->pAllObjects) {
        pCtx->pAllObjects->pPrevAll = pObj;
    }

    pCtx->pAllObjects = pObj;

    return pObj;
}

JSObj *jsobj_ref(JSObj *pObj)
{
    if (pObj) {
        pObj->nRef++;
    }

    return pObj;
}

/* Deepest chain of nested frees; anything below is handed to the final
 * sweep rather than risking the native stack on a long object chain.     */
#define JS_MAX_FREE_DEPTH 96

void jsobj_unref(JSCtx *pCtx, JSObj *pObj)
{
    int i = 0;

    if (pObj == NULL) {
        return;
    }

    pObj->nRef--;

    /* A script constructor and its otherwise unreferenced prototype form
     * a small, identifiable cycle. Both incoming references must be exactly
     * the two property links checked below; external aliases or additional
     * cycles make the counts larger and keep the general final sweep path.
     * Hold the function while removing constructor so recursive unref cannot
     * reclaim it before this frame has finished. */
    if (pObj->nRef == 1 && pObj->nMapRefs == 1 && pObj->cls == JCLASS_FUNCTION && !pObj->bSweeping && pCtx->nFreeDepth < JS_MAX_FREE_DEPTH) {
        JSProp *pPrototype = jsprops_find(&pObj->props, "prototype", 9);
        JSObj *pProto = pPrototype && pPrototype->value.tag == JT_OBJ ? pPrototype->value.u.o : NULL;
        if (pProto && pProto != pObj && pProto->nRef == 1 && pProto->nMapRefs == 1 && !pProto->bSweeping) {
            JSProp *pConstructor = jsprops_find(&pProto->props, "constructor", 11);
            if (pConstructor && pConstructor->value.tag == JT_OBJ && pConstructor->value.u.o == pObj) {
                jsobj_ref(pObj);
                jsprops_del(pCtx, &pProto->props, "constructor");
                jsobj_unref(pCtx, pObj);
                return;
            }
        }
    }

    if (pObj->nRef > 0) {
        return;
    }

    /* Objects that take part in a reference cycle - a closure and its scope,
     * a constructor and its prototype - never reach zero and are reclaimed
     * by the final sweep in js_free. Everything else goes now.            */
    if (pObj->bSweeping || (pObj->nMapRefs > 0) || (pCtx->nFreeDepth >= JS_MAX_FREE_DEPTH)) {
        return;
    }

    pObj->bSweeping = 1;

    if (pObj->pPrevAll) {
        pObj->pPrevAll->pNextAll = pObj->pNextAll;
    } else {
        pCtx->pAllObjects = pObj->pNextAll;
    }

    if (pObj->pNextAll) {
        pObj->pNextAll->pPrevAll = pObj->pPrevAll;
    }

    pCtx->nFreeDepth++;

    jsprops_free(pCtx, &pObj->props);
    {
        size_t j;
        for (j = 0; j < pObj->nArrayUsed; j++) {
            if (pObj->pArrayData[pObj->nArrayStart + j].tag == JT_OBJ) pObj->pArrayData[pObj->nArrayStart + j].u.o->nMapRefs--;
            js_release(pCtx, pObj->pArrayData[pObj->nArrayStart + j]);
        }
        xx_js_free(pObj->pArrayData);
    }
    jsobj_unref(pCtx, pObj->pProto);
    jsobj_unref(pCtx, pObj->pBoundTarget);
    jsscope_unref(pCtx, pObj->pScope);
    js_release(pCtx, pObj->primitive);
    js_release(pCtx, pObj->boundThis);

    for (i = 0; i < pObj->nBoundArgs; i++) {
        js_release(pCtx, pObj->pBoundArgs[i]);
    }

    pCtx->nFreeDepth--;

    xx_js_free(pObj->pUint32Data);
    xx_js_free(pObj->pBoundArgs);
    if (!pObj->bBorrowedFnName) xx_js_free(pObj->pFnName);

    if (pObj->pRegExp) {
        xx_regex_free(pObj->pRegExp);
    }

    xx_js_free(pObj);
}

JSScope *jsscope_new(JSCtx *pCtx, JSScope *pParent)
{
    JSScope *pScope = (JSScope *)xx_js_calloc(1, sizeof(JSScope));

    pScope->nRef = 1;
    pScope->pVars = jsobj_new(pCtx, JCLASS_SCOPE, NULL);
    pScope->pParent = pParent ? jsscope_ref(pParent) : NULL;

    pScope->pPrevAll = NULL;
    pScope->pNextAll = pCtx->pAllScopes;

    if (pCtx->pAllScopes) {
        pCtx->pAllScopes->pPrevAll = pScope;
    }

    pCtx->pAllScopes = pScope;

    return pScope;
}

JSScope *jsscope_ref(JSScope *pScope)
{
    if (pScope) {
        pScope->nRef++;
    }

    return pScope;
}

void jsscope_unref(JSCtx *pCtx, JSScope *pScope)
{
    if (pScope == NULL) {
        return;
    }

    pScope->nRef--;
    if (pScope->bSweeping) return;

    if (pScope->nRef > 0) {
        return;
    }

    if (pCtx->nFreeDepth >= JS_MAX_FREE_DEPTH) {
        return;
    }

    if (pScope->pPrevAll) {
        pScope->pPrevAll->pNextAll = pScope->pNextAll;
    } else {
        pCtx->pAllScopes = pScope->pNextAll;
    }

    if (pScope->pNextAll) {
        pScope->pNextAll->pPrevAll = pScope->pPrevAll;
    }

    pCtx->nFreeDepth++;
    jsobj_unref(pCtx, pScope->pVars);
    jsscope_unref(pCtx, pScope->pParent);
    pCtx->nFreeDepth--;

    xx_js_free(pScope);
}

/* Trial deletion runs only after an interpreted frame relinquishes its scope.
 * Subtract references from the candidate graph, then mark nodes with external
 * references and everything they retain. Context roots and ancestor scopes
 * are boundaries; references to them are released if a child cycle dies.
 * This does not infer escape from source syntax or particular refcounts. */
typedef struct {
    void *pNode;
    int bScope;
} JSGCNode;

typedef struct {
    JSCtx *pCtx;
    JSScope *pBoundary;
    JSGCNode *pNodes;
    size_t nSize, nCapacity, nEdges;
    int bOverflow;
} JSGCTrial;

#define JS_GC_CANDIDATE 1
#define JS_GC_MARKED 2
#define JS_GC_MAX_NODES 65536u
#define JS_GC_MAX_EDGES 1048576u

static int jsgc_object_boundary(JSCtx *pCtx, JSObj *pObj)
{
    return pObj == pCtx->pGlobal || pObj == pCtx->pObjectProto || pObj == pCtx->pFunctionProto ||
           pObj == pCtx->pArrayProto || pObj == pCtx->pUint32ArrayProto || pObj == pCtx->pStringProto ||
           pObj == pCtx->pNumberProto || pObj == pCtx->pBooleanProto || pObj == pCtx->pRegExpProto ||
           pObj == pCtx->pErrorProto;
}

static void jsgc_add(JSGCTrial *pTrial, void *pNode, int bScope)
{
    uint8_t *pState;
    if (!pNode || pTrial->bOverflow) return;
    if (bScope) {
        JSScope *pScope = (JSScope *)pNode, *pAncestor;
        if (pScope == pTrial->pCtx->pGlobalScope || pScope->bSweeping) return;
        for (pAncestor = pTrial->pBoundary; pAncestor; pAncestor = pAncestor->pParent) if (pScope == pAncestor) return;
        pState = &pScope->bGCState;
    } else {
        JSObj *pObj = (JSObj *)pNode;
        if (pObj->bSweeping || jsgc_object_boundary(pTrial->pCtx, pObj)) return;
        pState = &pObj->bGCState;
    }
    if (*pState & JS_GC_CANDIDATE) return;
    if (pTrial->nSize >= JS_GC_MAX_NODES) { pTrial->bOverflow = 1; return; }
    if (pTrial->nSize == pTrial->nCapacity) {
        pTrial->nCapacity = pTrial->nCapacity ? pTrial->nCapacity * 2 : 16;
        pTrial->pNodes = (JSGCNode *)xx_js_realloc(pTrial->pNodes, pTrial->nCapacity * sizeof(JSGCNode));
    }
    *pState = JS_GC_CANDIDATE;
    pTrial->pNodes[pTrial->nSize].pNode = pNode;
    pTrial->pNodes[pTrial->nSize++].bScope = bScope;
    if (bScope) ((JSScope *)pNode)->nGCRef = ((JSScope *)pNode)->nRef;
    else ((JSObj *)pNode)->nGCRef = ((JSObj *)pNode)->nRef;
}

/* phase 0 discovers edges; phase 1 subtracts internal edges; phase 2 marks
 * reachability. A negative residual count conservatively becomes a root. */
static void jsgc_edge(JSGCTrial *pTrial, void *pNode, int bScope, int nPhase)
{
    uint8_t *pState;
    int32_t *pRef;
    if (!pNode || pTrial->bOverflow) return;
    if (++pTrial->nEdges > JS_GC_MAX_EDGES) { pTrial->bOverflow = 1; return; }
    if (!nPhase) { jsgc_add(pTrial, pNode, bScope); return; }
    pState = bScope ? &((JSScope *)pNode)->bGCState : &((JSObj *)pNode)->bGCState;
    if (!(*pState & JS_GC_CANDIDATE)) return;
    pRef = bScope ? &((JSScope *)pNode)->nGCRef : &((JSObj *)pNode)->nGCRef;
    if (nPhase == 1) (*pRef)--;
    else *pState |= JS_GC_MARKED;
}

static void jsgc_edges(JSGCTrial *pTrial, JSGCNode node, int nPhase)
{
    size_t i;
    if (node.bScope) {
        JSScope *pScope = (JSScope *)node.pNode;
        jsgc_edge(pTrial, pScope->pVars, 0, nPhase);
        jsgc_edge(pTrial, pScope->pParent, 1, nPhase);
    } else {
        JSObj *pObj = (JSObj *)node.pNode;
        jsgc_edge(pTrial, pObj->pProto, 0, nPhase);
        jsgc_edge(pTrial, pObj->pBoundTarget, 0, nPhase);
        jsgc_edge(pTrial, pObj->pScope, 1, nPhase);
        if (pObj->primitive.tag == JT_OBJ) jsgc_edge(pTrial, pObj->primitive.u.o, 0, nPhase);
        if (pObj->boundThis.tag == JT_OBJ) jsgc_edge(pTrial, pObj->boundThis.u.o, 0, nPhase);
        for (i = 0; i < (size_t)pObj->nBoundArgs; i++) if (pObj->pBoundArgs[i].tag == JT_OBJ) jsgc_edge(pTrial, pObj->pBoundArgs[i].u.o, 0, nPhase);
        for (i = 0; i < pObj->props.nSize; i++) {
            JSProp *pProp = &pObj->props.pEntries[i];
            if (!pProp->bDeleted && pProp->value.tag == JT_OBJ) jsgc_edge(pTrial, pProp->value.u.o, 0, nPhase);
        }
        for (i = 0; i < pObj->nArrayUsed; i++) {
            JSVal value = pObj->pArrayData[pObj->nArrayStart + i];
            if (value.tag == JT_OBJ) jsgc_edge(pTrial, value.u.o, 0, nPhase);
        }
    }
}

static void jsscope_collect_cycle(JSCtx *pCtx, JSScope *pScope)
{
    JSGCTrial trial;
    size_t i;
    int bChanged;
    if (pCtx->bCollectingCycles || pCtx->nFreeDepth >= JS_MAX_FREE_DEPTH) return;
    xx_rt_memset(&trial, 0, sizeof(trial));
    trial.pCtx = pCtx;
    trial.pBoundary = pScope->pParent;
    pCtx->bCollectingCycles = 1;
    jsgc_add(&trial, pScope, 1);
    for (i = 0; i < trial.nSize && !trial.bOverflow; i++) jsgc_edges(&trial, trial.pNodes[i], 0);
    for (i = 0; i < trial.nSize && !trial.bOverflow; i++) jsgc_edges(&trial, trial.pNodes[i], 1);
    for (i = 0; i < trial.nSize; i++) {
        JSGCNode node = trial.pNodes[i];
        int32_t nRef = node.bScope ? ((JSScope *)node.pNode)->nGCRef : ((JSObj *)node.pNode)->nGCRef;
        if (nRef != 0) {
            if (node.bScope) ((JSScope *)node.pNode)->bGCState |= JS_GC_MARKED;
            else ((JSObj *)node.pNode)->bGCState |= JS_GC_MARKED;
        }
    }
    /* Iterative fixed point keeps the native stack bounded. Each edge is
     * visited once after its source first becomes marked. */
    do {
        bChanged = 0;
        for (i = 0; i < trial.nSize && !trial.bOverflow; i++) {
            JSGCNode node = trial.pNodes[i];
            if (++trial.nEdges > JS_GC_MAX_EDGES) { trial.bOverflow = 1; break; }
            uint8_t *pState = node.bScope ? &((JSScope *)node.pNode)->bGCState : &((JSObj *)node.pNode)->bGCState;
            if ((*pState & (JS_GC_MARKED | 4)) == JS_GC_MARKED) {
                *pState |= 4;
                jsgc_edges(&trial, node, 2);
                bChanged = 1;
            }
        }
    } while (bChanged && !trial.bOverflow);
    if (trial.bOverflow) {
        for (i = 0; i < trial.nSize; i++) {
            if (trial.pNodes[i].bScope) ((JSScope *)trial.pNodes[i].pNode)->bGCState = 0;
            else ((JSObj *)trial.pNodes[i].pNode)->bGCState = 0;
        }
        xx_js_free(trial.pNodes);
        pCtx->bCollectingCycles = 0;
        return;
    }
    /* Hold all survivors before releasing any graph edge. Garbage headers
     * remain alive and bSweeping throughout the complete release phase. */
    for (i = 0; i < trial.nSize; i++) {
        JSGCNode node = trial.pNodes[i];
        if (node.bScope) {
            JSScope *p = (JSScope *)node.pNode;
            if (p->bGCState & JS_GC_MARKED) { jsscope_ref(p); p->bGCState = JS_GC_MARKED; }
            else {
                p->bSweeping = 1;
                if (p->pPrevAll) p->pPrevAll->pNextAll = p->pNextAll; else pCtx->pAllScopes = p->pNextAll;
                if (p->pNextAll) p->pNextAll->pPrevAll = p->pPrevAll;
            }
        } else {
            JSObj *p = (JSObj *)node.pNode;
            if (p->bGCState & JS_GC_MARKED) { jsobj_ref(p); p->bGCState = JS_GC_MARKED; }
            else {
                p->bSweeping = 1;
                if (p->pPrevAll) p->pPrevAll->pNextAll = p->pNextAll; else pCtx->pAllObjects = p->pNextAll;
                if (p->pNextAll) p->pNextAll->pPrevAll = p->pPrevAll;
            }
        }
    }
    for (i = 0; i < trial.nSize; i++) {
        JSGCNode node = trial.pNodes[i];
        if (node.bScope) {
            JSScope *p = (JSScope *)node.pNode;
            if (!(p->bGCState & JS_GC_MARKED)) { jsobj_unref(pCtx, p->pVars); jsscope_unref(pCtx, p->pParent); }
        } else {
            JSObj *p = (JSObj *)node.pNode;
            size_t j;
            if (p->bGCState & JS_GC_MARKED) continue;
            jsprops_free(pCtx, &p->props);
            for (j = 0; j < p->nArrayUsed; j++) {
                JSVal value = p->pArrayData[p->nArrayStart + j];
                if (value.tag == JT_OBJ) value.u.o->nMapRefs--;
                js_release(pCtx, value);
            }
            jsobj_unref(pCtx, p->pProto);
            jsobj_unref(pCtx, p->pBoundTarget);
            jsscope_unref(pCtx, p->pScope);
            js_release(pCtx, p->primitive);
            js_release(pCtx, p->boundThis);
            for (j = 0; j < (size_t)p->nBoundArgs; j++) js_release(pCtx, p->pBoundArgs[j]);
        }
    }
    for (i = 0; i < trial.nSize; i++) {
        JSGCNode node = trial.pNodes[i];
        if (node.bScope) {
            JSScope *p = (JSScope *)node.pNode;
            if (!(p->bGCState & JS_GC_MARKED)) { xx_js_free(p); trial.pNodes[i].bScope = -1; }
            else trial.pNodes[i].bScope = 2;
        } else {
            JSObj *p = (JSObj *)node.pNode;
            if (!(p->bGCState & JS_GC_MARKED)) {
                xx_js_free(p->pArrayData);
                xx_js_free(p->pUint32Data);
                xx_js_free(p->pBoundArgs);
                if (!p->bBorrowedFnName) xx_js_free(p->pFnName);
                if (p->pRegExp) xx_regex_free(p->pRegExp);
                xx_js_free(p);
                trial.pNodes[i].bScope = -1;
            } else trial.pNodes[i].bScope = 3;
        }
    }
    /* Every remaining entry has its own hold, so releasing earlier entries
     * cannot invalidate a later survivor before its flag is reset. */
    for (i = 0; i < trial.nSize; i++) {
        JSGCNode node = trial.pNodes[i];
        /* Freed candidate addresses must not be dereferenced: save survivor
         * identity in the vector before physical reclamation (below). */
        if (node.bScope == 2) {
            JSScope *p = (JSScope *)node.pNode;
            p->bGCState = 0;
            jsscope_unref(pCtx, p);
        } else if (node.bScope == 3) {
            JSObj *p = (JSObj *)node.pNode;
            p->bGCState = 0;
            jsobj_unref(pCtx, p);
        }
    }
    xx_js_free(trial.pNodes);
    pCtx->bCollectingCycles = 0;
}

void jsscope_close(JSCtx *pCtx, JSScope *pScope)
{
    int bRetained = pScope && pScope->nRef > 1;
    jsscope_unref(pCtx, pScope);
    if (bRetained) jsscope_collect_cycle(pCtx, pScope);
}

/* ---------------------------------------------------------------- values  */

JSVal js_undefined(void)
{
    JSVal value;

    value.tag = JT_UNDEF;
    value.u.n = 0;

    return value;
}

JSVal js_null(void)
{
    JSVal value;

    value.tag = JT_NULL;
    value.u.n = 0;

    return value;
}

JSVal js_bool(int bValue)
{
    JSVal value;

    value.tag = JT_BOOL;
    value.u.b = bValue ? 1 : 0;

    return value;
}

JSVal js_num(double nValue)
{
    JSVal value;

    value.tag = JT_NUM;
    value.u.n = nValue;

    return value;
}

JSVal js_int(int64_t nValue)
{
    return js_num((double)nValue);
}

JSVal jsval_obj(JSObj *pObj)
{
    JSVal value;

    value.tag = JT_OBJ;
    value.u.o = pObj;

    return value;
}

JSVal jsval_str(JSStr *pStr)
{
    JSVal value;

    value.tag = JT_STR;
    value.u.s = pStr;

    return value;
}

JSVal js_str(JSCtx *pCtx, const char *pString)
{
    return js_strn(pCtx, pString, pString ? xx_rt_strlen(pString) : 0);
}

JSVal js_strn(JSCtx *pCtx, const char *pString, size_t nSize)
{
    return jsval_str(jsstr_new(pCtx, pString, nSize));
}

JSVal js_dup(JSVal value)
{
    if (value.tag == JT_STR) {
        jsstr_ref(value.u.s);
    } else if (value.tag == JT_OBJ) {
        jsobj_ref(value.u.o);
    }

    return value;
}

void js_release(JSCtx *pCtx, JSVal value)
{
    if (value.tag == JT_STR) {
        jsstr_unref(pCtx, value.u.s);
    } else if (value.tag == JT_OBJ) {
        jsobj_unref(pCtx, value.u.o);
    }
}

int js_is_undefined(JSVal value)
{
    return (value.tag == JT_UNDEF) ? 1 : 0;
}

int js_is_array(JSVal value)
{
    return value.tag == JT_OBJ && value.u.o->cls == JCLASS_ARRAY;
}

int js_is_callable(JSVal value)
{
    if (value.tag != JT_OBJ) {
        return 0;
    }

    return ((value.u.o->cls == JCLASS_FUNCTION) || (value.u.o->cls == JCLASS_NATIVE)) ? 1 : 0;
}

const char *js_str_data(JSVal value)
{
    if (value.tag == JT_STR) {
        return value.u.s->pData;
    }

    return "";
}

size_t js_str_len(JSVal value)
{
    if (value.tag == JT_STR) {
        return value.u.s->nSize;
    }

    return 0;
}

/* ------------------------------------------------------------ conversion  */

int js_is_array_index(const char *pKey, size_t nKeySize, int64_t *pnIndex)
{
    size_t i = 0;
    int64_t nValue = 0;
    int bNegative = 0;

    if (nKeySize == 0) {
        return 0;
    }

    if (pKey[0] == '-') {
        bNegative = 1;
        i = 1;

        if (nKeySize == 1) {
            return 0;
        }
    }

    for (; i < nKeySize; i++) {
        if ((pKey[i] < '0') || (pKey[i] > '9')) {
            return 0;
        }

        nValue = nValue * 10 + (pKey[i] - '0');

        if (nValue > 0x7FFFFFFFll) {
            return 0;
        }
    }

    /* Reject "01" style keys - they are ordinary property names. */
    if ((nKeySize > (size_t)(bNegative ? 2 : 1)) && (pKey[bNegative ? 1 : 0] == '0')) {
        return 0;
    }

    if (pnIndex) {
        *pnIndex = bNegative ? -nValue : nValue;
    }

    return bNegative ? 0 : 1;
}

double js_string_to_number(const char *pData, size_t nSize)
{
    size_t nStart = 0;
    size_t nEnd = nSize;
    char *pCopy = NULL;
    const char *pEndPtr = NULL;
    double nResult = 0;

    while ((nStart < nEnd) && ((unsigned char)pData[nStart] <= ' ')) {
        nStart++;
    }

    while ((nEnd > nStart) && ((unsigned char)pData[nEnd - 1] <= ' ')) {
        nEnd--;
    }

    if (nStart == nEnd) {
        return 0;
    }

    pCopy = xx_js_strndup(pData + nStart, nEnd - nStart);

    if (((xx_rt_strncmp(pCopy, "0x", 2) == 0) || (xx_rt_strncmp(pCopy, "0X", 2) == 0)) &&
        (((pCopy[2] >= '0') && (pCopy[2] <= '9')) || ((pCopy[2] >= 'a') && (pCopy[2] <= 'f')) || ((pCopy[2] >= 'A') && (pCopy[2] <= 'F')))) {
        /* A hex digit must follow: strtoull would otherwise skip white space
         * and accept a sign ("0x -1"), and a bare "0x" falls to strtod -> NaN. */
        nResult = (double)xx_rt_strtoull(pCopy + 2, &pEndPtr, 16);
        /* "0x" with no digits is NaN, not strtoull's empty conversion. */
        if (pEndPtr == pCopy + 2) {
            xx_js_free(pCopy);
            return xx_rt_nan();
        }
    } else if (xx_rt_strcmp(pCopy, "Infinity") == 0 || xx_rt_strcmp(pCopy, "+Infinity") == 0) {
        xx_js_free(pCopy);
        return xx_rt_inf();
    } else if (xx_rt_strcmp(pCopy, "-Infinity") == 0) {
        xx_js_free(pCopy);
        return (-xx_rt_inf());
    } else {
        nResult = xx_rt_strtod(pCopy, &pEndPtr);
    }

    if ((pEndPtr == NULL) || (*pEndPtr != 0)) {
        xx_js_free(pCopy);
        return xx_rt_nan();
    }

    xx_js_free(pCopy);

    return nResult;
}

JSVal js_to_primitive(JSCtx *pCtx, JSVal value, int bPreferString)
{
    static const char *pOrderString[2] = {"toString", "valueOf"};
    static const char *pOrderNumber[2] = {"valueOf", "toString"};
    const char **ppOrder = bPreferString ? pOrderString : pOrderNumber;
    int i = 0;

    if (value.tag != JT_OBJ) {
        return js_dup(value);
    }

    for (i = 0; i < 2; i++) {
        JSVal fn = jsobj_get(pCtx, value.u.o, ppOrder[i], xx_rt_strlen(ppOrder[i]));

        if (js_is_callable(fn)) {
            JSVal result = js_call(pCtx, fn, value, 0, NULL);

            js_release(pCtx, fn);

            if (pCtx->bException) {
                js_release(pCtx, result);
                return js_undefined();
            }

            if (result.tag != JT_OBJ) {
                return result;
            }

            js_release(pCtx, result);
        } else {
            js_release(pCtx, fn);
        }
    }

    return js_str(pCtx, "[object Object]");
}

double js_to_number(JSCtx *pCtx, JSVal value)
{
    switch (value.tag) {
        case JT_UNDEF: return xx_rt_nan();
        case JT_NULL: return 0;
        case JT_BOOL: return value.u.b ? 1 : 0;
        case JT_NUM: return value.u.n;
        case JT_STR: return js_string_to_number(value.u.s->pData, value.u.s->nSize);
        case JT_OBJ: {
            JSVal prim = js_to_primitive(pCtx, value, 0);
            double nResult = 0;

            if (prim.tag == JT_OBJ) {
                js_release(pCtx, prim);
                return xx_rt_nan();
            }

            nResult = js_to_number(pCtx, prim);
            js_release(pCtx, prim);

            return nResult;
        }

        default: break;
    }

    return xx_rt_nan();
}

int js_to_bool(JSCtx *pCtx, JSVal value)
{
    (void)pCtx;

    switch (value.tag) {
        case JT_UNDEF:
        case JT_NULL: return 0;
        case JT_BOOL: return value.u.b;
        case JT_NUM: return ((value.u.n != 0) && (!(value.u.n != value.u.n))) ? 1 : 0;
        case JT_STR: return (value.u.s->nSize > 0) ? 1 : 0;
        case JT_OBJ: return 1;
        default: break;
    }

    return 0;
}

/* Every exact safe integer has its exact decimal integer spelling under
 * Number::toString. Stay below 2^53: larger integral doubles can have a shorter
 * round-tripping decimal representation. pBuf must hold 32 bytes. */
int js_int_to_buf(char *pBuf, double nValue)
{
    char sTmp[20];
    int64_t nInt = 0;
    uint64_t nAbs = 0;
    int nTmp = 0;
    int nOut = 0;

    /* Written so that NaN, which compares false either way, falls through. */
    if (!((nValue >= -9007199254740991.0) && (nValue <= 9007199254740991.0))) {
        return 0;
    }

    nInt = (int64_t)nValue;

    if ((double)nInt != nValue) {
        return 0;
    }

    /* -0 takes this path too: it compares equal to 0 and String(-0) is "0". */
    if (nInt < 0) {
        pBuf[nOut++] = '-';
        nAbs = (uint64_t)(-nInt);
    } else {
        nAbs = (uint64_t)nInt;
    }

    do {
        sTmp[nTmp++] = (char)('0' + (int)(nAbs % 10));
        nAbs /= 10;
    } while (nAbs != 0);

    while (nTmp > 0) {
        pBuf[nOut++] = sTmp[--nTmp];
    }

    pBuf[nOut] = 0;

    return nOut;
}

/* ECMAScript Number::toString. xx_rt_dtoa_shortest implements the specification
 * directly - shortest round-tripping digits plus the exponent rules - so no
 * printf-style float formatting is involved anywhere in the engine.        */
static void js_double_to_buf(char *pBuf, size_t nBufSize, double nValue)
{
    if ((nBufSize >= 32) && (js_int_to_buf(pBuf, nValue) > 0)) {
        return;
    }

    xx_rt_dtoa_shortest(nValue, pBuf, nBufSize);
}

JSVal js_number_to_string(JSCtx *pCtx, double nValue, int nRadix)
{
    char sBuf[64];

    if ((nRadix == 10) || (nRadix == 0)) {
        js_double_to_buf(sBuf, sizeof(sBuf), nValue);

        return js_str(pCtx, sBuf);
    }

    if (nValue != nValue) {
        return js_str(pCtx, "NaN");
    }

    if (nValue == xx_rt_inf()) {
        return js_str(pCtx, "Infinity");
    }

    if (nValue == (-xx_rt_inf())) {
        return js_str(pCtx, "-Infinity");
    }

    /* Everything below stays in double arithmetic: a value of 2^64 or more
     * cannot be converted to an integer type, and the digit sequence has to
     * match the reference engine, which divides and multiplies doubles.   */
    {
        static const char *pDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
        /* A double needs at most 1075 binary digits on either side of the
         * point, plus the sign and the terminator.                        */
        char sTmp[1088];
        int nPos = (int)sizeof(sTmp);
        int bNegative = (nValue < 0) ? 1 : 0;
        double nAbs = bNegative ? -nValue : nValue;
        double nFrac = nAbs - xx_rt_floor(nAbs);
        double nInteger = xx_rt_floor(nAbs);

        sTmp[--nPos] = 0;

        do {
            sTmp[--nPos] = pDigits[(int)xx_rt_fmod(nInteger, (double)nRadix)];
            nInteger = xx_rt_floor(nInteger / nRadix);
        } while ((nInteger != 0) && (nPos > 1));

        if (bNegative) {
            sTmp[--nPos] = '-';
        }

        if (nFrac > 0) {
            xx_buf_t buf;
            int i = 0;
            JSVal result;

            xx_buf_init(&buf);
            xx_buf_append_str(&buf, sTmp + nPos);
            xx_buf_append_char(&buf, '.');

            for (i = 0; (i < 20) && (nFrac > 0); i++) {
                int nDigit = 0;

                nFrac *= nRadix;
                nDigit = (int)xx_rt_floor(nFrac);
                nFrac -= nDigit;
                xx_buf_append_char(&buf, pDigits[nDigit]);
            }

            result = js_strn(pCtx, buf.data, buf.size);
            xx_buf_free(&buf);

            return result;
        }

        return js_str(pCtx, sTmp + nPos);
    }
}

JSVal js_to_string(JSCtx *pCtx, JSVal value)
{
    switch (value.tag) {
        case JT_UNDEF: return js_str(pCtx, "undefined");
        case JT_NULL: return js_str(pCtx, "null");
        case JT_BOOL: return js_str(pCtx, value.u.b ? "true" : "false");
        case JT_NUM: return js_number_to_string(pCtx, value.u.n, 10);
        case JT_STR: return js_dup(value);
        case JT_OBJ: {
            JSVal prim = js_to_primitive(pCtx, value, 1);
            JSVal result;

            if (prim.tag == JT_OBJ) {
                js_release(pCtx, prim);
                return js_str(pCtx, "[object Object]");
            }

            result = js_to_string(pCtx, prim);
            js_release(pCtx, prim);

            return result;
        }

        default: break;
    }

    return js_str(pCtx, "");
}

char *js_to_cstr(JSCtx *pCtx, JSVal value)
{
    JSVal str = js_to_string(pCtx, value);
    char *pResult = xx_js_strndup(js_str_data(str), js_str_len(str));

    js_release(pCtx, str);

    return pResult;
}

int64_t js_to_int64(JSCtx *pCtx, JSVal value)
{
    double nValue = js_to_number(pCtx, value);

    if (nValue != nValue) {
        return 0;
    }

    if (nValue >= 9.2233720368547758e18) {
        return 0x7FFFFFFFFFFFFFFFll;
    }

    if (nValue <= -9.2233720368547758e18) {
        return (-0x7FFFFFFFFFFFFFFFll - 1);
    }

    return (int64_t)nValue;
}

int32_t js_to_int32(JSCtx *pCtx, JSVal value)
{
    double nValue = js_to_number(pCtx, value);
    double nTrunc = 0;
    uint32_t nBits = 0;

    if ((nValue != nValue) || (nValue == xx_rt_inf()) || (nValue == (-xx_rt_inf()))) {
        return 0;
    }

    nTrunc = (nValue < 0) ? -xx_rt_floor(-nValue) : xx_rt_floor(nValue);
    nTrunc = xx_rt_fmod(nTrunc, 4294967296.0);

    if (nTrunc < 0) {
        nTrunc += 4294967296.0;
    }

    nBits = (uint32_t)nTrunc;

    return (int32_t)nBits;
}

/* Called only after the interpreter proves the identifier still owns left.
 * The two references are that slot and the evaluated left operand; any alias
 * (including a self-append right operand) forces the immutable fallback. */
JSVal js_concat_str_assign(JSCtx *pCtx, JSVal left, JSVal right)
{
    JSStr *pStr;
    size_t nNeeded, nCapacity;
    (void)pCtx;
    if (left.tag != JT_STR || right.tag != JT_STR || left.u.s->nRef != 2) return js_undefined();
    pStr = left.u.s;
    if (right.u.s->nSize > SIZE_MAX - pStr->nSize - 1) return js_undefined();
    nNeeded = pStr->nSize + right.u.s->nSize;
    if (nNeeded > pStr->nCapacity) {
        nCapacity = pStr->nCapacity < 32 ? 32 : pStr->nCapacity;
        while (nCapacity < nNeeded) {
            if (nCapacity > (SIZE_MAX - 1) / 2) { nCapacity = nNeeded; break; }
            nCapacity *= 2;
        }
        pStr->pData = (char *)xx_js_realloc(pStr->pData, nCapacity + 1);
        pStr->nCapacity = nCapacity;
    }
    if (right.u.s->nSize) xx_rt_memcpy(pStr->pData + pStr->nSize, right.u.s->pData, right.u.s->nSize);
    pStr->nSize = nNeeded;
    pStr->pData[nNeeded] = 0;
    return js_dup(left);
}

JSVal js_concat_str(JSCtx *pCtx, JSVal left, JSVal right)
{
    JSVal a = js_to_string(pCtx, left);
    JSVal b = js_to_string(pCtx, right);
    size_t nSizeA = js_str_len(a);
    size_t nSizeB = js_str_len(b);
    /* Two strings whose sizes do not fit size_t together cannot be joined. */
    if (nSizeB > SIZE_MAX - nSizeA - 1) {
        js_release(pCtx, a);
        js_release(pCtx, b);
        return js_throw(pCtx, "RangeError: string too long");
    }
    JSStr *pStr = jsstr_new(pCtx, NULL, nSizeA + nSizeB);

    xx_rt_memcpy(pStr->pData, js_str_data(a), nSizeA);
    xx_rt_memcpy(pStr->pData + nSizeA, js_str_data(b), nSizeB);
    pStr->pData[nSizeA + nSizeB] = 0;

    js_release(pCtx, a);
    js_release(pCtx, b);

    return jsval_str(pStr);
}

/* ---------------------------------------------------------- object model  */

static JSVal jsobj_get_own_hashed(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, uint32_t nHash, int *pbFound)
{
    JSProp *pProp = NULL;

    if (pbFound) {
        *pbFound = 0;
    }

    if (pObj == NULL) {
        return js_undefined();
    }

    if (pObj->cls == JCLASS_UINT32_ARRAY) {
        int64_t index;
        if ((nKeySize == 6 && xx_rt_memcmp(pKey, "length", 6) == 0) || (nKeySize == 10 && xx_rt_memcmp(pKey, "byteLength", 10) == 0)) {
            if (pbFound) *pbFound = 1;
            return js_num((double)pObj->nArrayLen * (nKeySize == 10 ? 4 : 1));
        }
        if (js_is_array_index(pKey, nKeySize, &index)) {
            if (index < pObj->nArrayLen) {
                if (pbFound) *pbFound = 1;
                return js_num((double)pObj->pUint32Data[index]);
            }
            return js_undefined();
        }
    }

    if (pObj->cls == JCLASS_ARRAY && !pObj->bArraySparse) {
        int64_t nIndex;
        if (js_is_array_index(pKey, nKeySize, &nIndex) && (uint64_t)nIndex < pObj->nArrayUsed && !JS_IS_ARRAY_HOLE(pObj->pArrayData[pObj->nArrayStart + nIndex])) {
            if (pbFound) *pbFound = 1;
            return js_dup(pObj->pArrayData[pObj->nArrayStart + nIndex]);
        }
    }

    /* Array/String length and string indices are computed. */
    if ((nKeySize == 6) && (xx_rt_memcmp(pKey, "length", 6) == 0)) {
        if (pObj->cls == JCLASS_ARRAY) {
            if (pbFound) {
                *pbFound = 1;
            }

            return js_num((double)pObj->nArrayLen);
        }

        if (pObj->cls == JCLASS_STRING) {
            if (pbFound) {
                *pbFound = 1;
            }

            return js_num((double)js_str_len(pObj->primitive));
        }
    }

    if (pObj->cls == JCLASS_STRING) {
        int64_t nIndex = 0;

        if (js_is_array_index(pKey, nKeySize, &nIndex)) {
            const char *pData = js_str_data(pObj->primitive);
            size_t nSize = js_str_len(pObj->primitive);

            if ((nIndex >= 0) && ((size_t)nIndex < nSize)) {
                if (pbFound) {
                    *pbFound = 1;
                }

                return js_strn(pCtx, pData + nIndex, 1);
            }
        }
    }

    pProp = jsprops_find_hashed(&pObj->props, pKey, nKeySize, nHash);

    if (pProp) {
        if (pbFound) {
            *pbFound = 1;
        }

        return js_dup(pProp->value);
    }

    return js_undefined();
}

JSVal jsobj_get_own(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, int *pbFound)
{
    if (pObj == NULL) {
        if (pbFound) {
            *pbFound = 0;
        }

        return js_undefined();
    }

    return jsobj_get_own_hashed(pCtx, pObj, pKey, nKeySize, xx_js_hash_str(pKey, nKeySize), pbFound);
}

/* The key hash is computed once and reused for the whole prototype walk. */
JSVal jsobj_get(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize)
{
    JSObj *pCurrent = pObj;
    uint32_t nHash = 0;

    if (pCurrent == NULL) {
        return js_undefined();
    }

    nHash = xx_js_hash_str(pKey, nKeySize);

    while (pCurrent) {
        int bFound = 0;
        JSVal value = jsobj_get_own_hashed(pCtx, pCurrent, pKey, nKeySize, nHash, &bFound);

        if (bFound) {
            return value;
        }

        js_release(pCtx, value);
        pCurrent = pCurrent->pProto;
    }

    return js_undefined();
}

int jsobj_has(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize)
{
    JSObj *pCurrent = pObj;
    uint32_t nHash = 0;

    if (pCurrent == NULL) {
        return 0;
    }

    nHash = xx_js_hash_str(pKey, nKeySize);

    while (pCurrent) {
        int bFound = 0;
        JSVal value = jsobj_get_own_hashed(pCtx, pCurrent, pKey, nKeySize, nHash, &bFound);

        js_release(pCtx, value);

        if (bFound) {
            return 1;
        }

        pCurrent = pCurrent->pProto;
    }

    return 0;
}

void jsobj_put(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, JSVal value)
{
    JSProp *pProp = NULL;

    if (pObj == NULL) {
        js_release(pCtx, value);
        return;
    }

    if (pObj->cls == JCLASS_UINT32_ARRAY) {
        int64_t index;
        if ((nKeySize == 6 && xx_rt_memcmp(pKey, "length", 6) == 0) || (nKeySize == 10 && xx_rt_memcmp(pKey, "byteLength", 10) == 0)) {
            js_release(pCtx, value);
            return;
        }
        if (js_is_array_index(pKey, nKeySize, &index)) {
            if (index < pObj->nArrayLen) pObj->pUint32Data[index] = (uint32_t)js_to_int32(pCtx, value);
            js_release(pCtx, value);
            return;
        }
    }

    if ((pObj->cls == JCLASS_ARRAY) && (nKeySize == 6) && (xx_rt_memcmp(pKey, "length", 6) == 0)) {
        int64_t nNewLen = js_to_int64(pCtx, value);
        if (nNewLen < 0) {
            nNewLen = 0;
        }

        /* An array index never exceeds a uint32, so neither may the length. */
        if (nNewLen > 0xFFFFFFFFll) {
            nNewLen = 0xFFFFFFFFll;
        }

        if (nNewLen < pObj->nArrayLen) {
            size_t j;
            if (!pObj->bArraySparse) {
                for (j = (size_t)nNewLen; j < pObj->nArrayUsed; j++) {
                    JSVal *pSlot = &pObj->pArrayData[pObj->nArrayStart + j];
                    if (JS_IS_ARRAY_HOLE(*pSlot)) pObj->nArrayHoles--;
                    jsarray_store(pCtx, pSlot, js_undefined());
                    pSlot->tag = JS_ARRAY_HOLE;
                }
                if ((uint64_t)nNewLen < pObj->nArrayUsed) pObj->nArrayUsed = (size_t)nNewLen;
                if (!pObj->nArrayUsed) {
                    xx_js_free(pObj->pArrayData);
                    pObj->pArrayData = NULL;
                    pObj->nArrayCapacity = pObj->nArrayStart = pObj->nArrayHoles = 0;
                }
            }
            /* Map deletion itself never compacts: retain pointers throughout
             * this pass, then compact once at the completed mutation boundary. */
            for (j = 0; j < pObj->props.nSize; j++) {
                JSProp *pEntry = &pObj->props.pEntries[j];
                int64_t nIndex;
                if (!pEntry->bDeleted && js_is_array_index(pEntry->pKey, pEntry->nKeySize, &nIndex) && nIndex >= nNewLen) jsprops_del(pCtx, &pObj->props, pEntry->pKey);
            }
            if (pObj->props.nSize > 8 && pObj->props.nLive < pObj->props.nSize / 4) jsprops_compact(&pObj->props);
        }

        pObj->nArrayLen = nNewLen;
        js_release(pCtx, value);

        return;
    }

    if (pObj->cls == JCLASS_ARRAY) {
        int64_t nIndex = 0;

        if (js_is_array_index(pKey, nKeySize, &nIndex)) {
            if (jsarray_put_dense(pCtx, pObj, nIndex, value)) return;
            if (nIndex >= pObj->nArrayLen) {
                pObj->nArrayLen = nIndex + 1;
            }
        }
    }

    if (pObj->cls == JCLASS_ARRAY) pObj->bArrayMapOrder = 1;
    pProp = jsprops_put(pCtx, &pObj->props, pKey, nKeySize);
    jsprop_store(pCtx, pProp, value);
}

void jsobj_put_hidden(JSCtx *pCtx, JSObj *pObj, const char *pKey, JSVal value)
{
    JSProp *pProp;
    if (pObj->cls == JCLASS_ARRAY) {
        pObj->bArrayMapOrder = 1;
        if (js_is_array_index(pKey, xx_rt_strlen(pKey), NULL)) jsarray_to_sparse(pCtx, pObj);
    }
    pProp = jsprops_put(pCtx, &pObj->props, pKey, xx_rt_strlen(pKey));
    jsprop_store(pCtx, pProp, value);
    pProp->bDontEnum = 1;
}

void jsobj_put_hidden_static(JSCtx *pCtx, JSObj *pObj, const char *pKey, JSVal value)
{
    JSProp *pProp;
    if (pObj->cls == JCLASS_ARRAY) {
        pObj->bArrayMapOrder = 1;
        if (js_is_array_index(pKey, xx_rt_strlen(pKey), NULL)) jsarray_to_sparse(pCtx, pObj);
    }
    pProp = jsprops_put_impl(pCtx, &pObj->props, pKey, xx_rt_strlen(pKey), 1);
    jsprop_store(pCtx, pProp, value);
    pProp->bDontEnum = 1;
}

int jsobj_del(JSCtx *pCtx, JSObj *pObj, const char *pKey)
{
    int64_t nIndex;
    int bDeleted;
    if (!pObj) return 0;
    if (pObj->cls == JCLASS_ARRAY && !pObj->bArraySparse && js_is_array_index(pKey, xx_rt_strlen(pKey), &nIndex)) {
        if ((uint64_t)nIndex < pObj->nArrayUsed && !JS_IS_ARRAY_HOLE(pObj->pArrayData[pObj->nArrayStart + nIndex])) {
            jsarray_store(pCtx, &pObj->pArrayData[pObj->nArrayStart + nIndex], js_undefined());
            pObj->pArrayData[pObj->nArrayStart + nIndex].tag = JS_ARRAY_HOLE;
            pObj->nArrayHoles++;
            return 1;
        }
    }
    bDeleted = jsprops_del(pCtx, &pObj->props, pKey);
    /* No JSProp pointer from lookup or an active mutation is retained here.
     * Script callbacks never run in deletion or refcount reclamation. */
    if (bDeleted && pObj->props.nSize > 8 && pObj->props.nLive < pObj->props.nSize / 4) jsprops_compact(&pObj->props);
    return bDeleted;
}

JSVal jsobj_keys(JSCtx *pCtx, JSObj *pObj)
{
    JSVal result = js_new_array(pCtx);
    size_t i;
    if (pObj->cls == JCLASS_ARRAY && !pObj->bArraySparse) {
        for (i = 0; i < pObj->nArrayUsed; i++) if (!JS_IS_ARRAY_HOLE(pObj->pArrayData[pObj->nArrayStart + i])) {
            char key[32];
            size_t nKeySize = js_index_key(key, (int64_t)i);
            js_array_push(pCtx, result, js_strn(pCtx, key, nKeySize));
        }
    }
    for (i = 0; i < pObj->props.nSize; i++) {
        JSProp *pProp = &pObj->props.pEntries[i];
        if (!pProp->bDeleted && !pProp->bDontEnum) js_array_push(pCtx, result, js_strn(pCtx, pProp->pKey, pProp->nKeySize));
    }
    return result;
}

int jsobj_property_is_enumerable(JSObj *pObj, const char *pKey, size_t nKeySize)
{
    JSProp *pProp;
    int64_t nIndex;
    if (pObj->cls == JCLASS_ARRAY && !pObj->bArraySparse && js_is_array_index(pKey, nKeySize, &nIndex) && (uint64_t)nIndex < pObj->nArrayUsed && !JS_IS_ARRAY_HOLE(pObj->pArrayData[pObj->nArrayStart + nIndex])) return 1;
    pProp = jsprops_find(&pObj->props, pKey, nKeySize);
    return pProp && !pProp->bDontEnum;
}

void jsobj_set_enumerable(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, int bEnumerable)
{
    JSProp *pProp;
    if (pObj->cls == JCLASS_ARRAY && js_is_array_index(pKey, nKeySize, NULL)) jsarray_to_sparse(pCtx, pObj);
    pProp = jsprops_find(&pObj->props, pKey, nKeySize);
    if (pProp) pProp->bDontEnum = !bEnumerable;
}

/* ------------------------------------------------------------ public API  */

JSVal js_new_object(JSCtx *pCtx)
{
    return jsval_obj(jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto));
}

JSVal js_new_array(JSCtx *pCtx)
{
    return jsval_obj(jsobj_new(pCtx, JCLASS_ARRAY, pCtx->pArrayProto));
}

JSVal js_new_uint32_array(JSCtx *pCtx, const uint32_t *data, size_t count)
{
    JSObj *object;
    uint32_t *storage = NULL;
    /* Mirrors the bounded native relation result budget (128 MiB). */
    if (count > 33554432u) return js_null();
    if (count) {
        storage = xx_rt_calloc(count, sizeof(uint32_t));
        if (!storage) return js_null();
    }
    object = jsobj_new(pCtx, JCLASS_UINT32_ARRAY, pCtx->pUint32ArrayProto);
    object->nArrayLen = (int64_t)count;
    if (count) {
        object->pUint32Data = storage;
        if (data) xx_rt_memcpy(object->pUint32Data, data, count * sizeof(uint32_t));
    }
    return jsval_obj(object);
}

JSVal js_new_native(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    JSObj *pObj = jsobj_new(pCtx, JCLASS_NATIVE, pCtx->pFunctionProto);

    pObj->nativeFn = fn;
    pObj->pUser = pUser;
    pObj->nNativeArgc = nArgc;
    pObj->pFnName = xx_js_strdup(pName ? pName : "");

    jsobj_put_hidden_static(pCtx, pObj, "length", js_num(nArgc));
    jsobj_put_hidden_static(pCtx, pObj, "name", js_str(pCtx, pName ? pName : ""));

    return jsval_obj(pObj);
}

JSVal js_get(JSCtx *pCtx, JSVal object, const char *pKey)
{
    if (object.tag != JT_OBJ) {
        return js_undefined();
    }

    return jsobj_get(pCtx, object.u.o, pKey, xx_rt_strlen(pKey));
}

void js_set(JSCtx *pCtx, JSVal object, const char *pKey, JSVal value)
{
    if (object.tag != JT_OBJ) {
        js_release(pCtx, value);
        return;
    }

    jsobj_put(pCtx, object.u.o, pKey, xx_rt_strlen(pKey), value);
}

int js_has(JSCtx *pCtx, JSVal object, const char *pKey)
{
    if (object.tag != JT_OBJ) {
        return 0;
    }

    return jsobj_has(pCtx, object.u.o, pKey, xx_rt_strlen(pKey));
}

/* Full int64 conversion without locale machinery or signed-negation overflow. */
static size_t js_index_key(char *pKey, int64_t nIndex)
{
    char digits[20];
    size_t nDigits = 0, nSize = 0;
    uint64_t nMagnitude;
    if (nIndex < 0) {
        pKey[nSize++] = '-';
        nMagnitude = (uint64_t)(-(nIndex + 1)) + 1;
    } else nMagnitude = (uint64_t)nIndex;
    do {
        digits[nDigits++] = (char)('0' + nMagnitude % 10);
        nMagnitude /= 10;
    } while (nMagnitude);
    while (nDigits) pKey[nSize++] = digits[--nDigits];
    pKey[nSize] = 0;
    return nSize;
}

JSVal js_get_index(JSCtx *pCtx, JSVal object, int64_t nIndex)
{
    char sKey[32];
    size_t nKeySize;
    if (object.tag != JT_OBJ) return js_undefined();
    if (object.u.o->cls == JCLASS_UINT32_ARRAY && nIndex >= 0 && nIndex < object.u.o->nArrayLen) {
        return js_num((double)object.u.o->pUint32Data[nIndex]);
    }
    if (object.u.o->cls == JCLASS_ARRAY && !object.u.o->bArraySparse && nIndex >= 0 && (uint64_t)nIndex < object.u.o->nArrayUsed && !JS_IS_ARRAY_HOLE(object.u.o->pArrayData[object.u.o->nArrayStart + nIndex])) return js_dup(object.u.o->pArrayData[object.u.o->nArrayStart + nIndex]);
    nKeySize = js_index_key(sKey, nIndex);
    return jsobj_get(pCtx, object.u.o, sKey, nKeySize);
}

void js_set_index(JSCtx *pCtx, JSVal object, int64_t nIndex, JSVal value)
{
    char sKey[32];
    size_t nKeySize;
    if (object.tag != JT_OBJ) { js_release(pCtx, value); return; }
    if (object.u.o->cls == JCLASS_UINT32_ARRAY && nIndex >= 0 && nIndex < 0xFFFFFFFFll) {
        if (nIndex < object.u.o->nArrayLen) object.u.o->pUint32Data[nIndex] = (uint32_t)js_to_int32(pCtx, value);
        js_release(pCtx, value);
        return;
    }
    if (object.u.o->cls == JCLASS_ARRAY && jsarray_put_dense(pCtx, object.u.o, nIndex, value)) return;
    nKeySize = js_index_key(sKey, nIndex);
    jsobj_put(pCtx, object.u.o, sKey, nKeySize, value);
}

int64_t js_array_length(JSCtx *pCtx, JSVal array)
{
    if ((array.tag == JT_OBJ) && (array.u.o->cls == JCLASS_ARRAY)) {
        return array.u.o->nArrayLen;
    }

    {
        JSVal length = js_get(pCtx, array, "length");
        int64_t nResult = js_to_int64(pCtx, length);

        js_release(pCtx, length);

        return nResult;
    }
}

void js_array_push(JSCtx *pCtx, JSVal array, JSVal value)
{
    if ((array.tag == JT_OBJ) && (array.u.o->cls == JCLASS_ARRAY)) {
        js_set_index(pCtx, array, array.u.o->nArrayLen, value);
    } else {
        js_release(pCtx, value);
    }
}

JSVal js_global(JSCtx *pCtx)
{
    return jsval_obj(jsobj_ref(pCtx->pGlobal));
}

void js_def_fn(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    jsobj_put_hidden(pCtx, pCtx->pGlobal, pName, js_new_native(pCtx, pName, fn, nArgc, pUser));
}

void js_def_method(JSCtx *pCtx, JSVal object, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    if (object.tag == JT_OBJ) {
        jsobj_put_hidden(pCtx, object.u.o, pName, js_new_native(pCtx, pName, fn, nArgc, pUser));
    }
}

/* -------------------------------------------------------------- equality  */

int js_strict_equals(JSCtx *pCtx, JSVal left, JSVal right)
{
    (void)pCtx;

    if (left.tag != right.tag) {
        return 0;
    }

    switch (left.tag) {
        case JT_UNDEF:
        case JT_NULL: return 1;
        case JT_BOOL: return (left.u.b == right.u.b) ? 1 : 0;
        case JT_NUM: return (left.u.n == right.u.n) ? 1 : 0;
        case JT_STR:
            if (left.u.s == right.u.s) {
                return 1;
            }

            return ((left.u.s->nSize == right.u.s->nSize) && (xx_rt_memcmp(left.u.s->pData, right.u.s->pData, left.u.s->nSize) == 0)) ? 1 : 0;
        case JT_OBJ: return (left.u.o == right.u.o) ? 1 : 0;
        default: break;
    }

    return 0;
}

int js_loose_equals(JSCtx *pCtx, JSVal left, JSVal right)
{
    if (left.tag == right.tag) {
        return js_strict_equals(pCtx, left, right);
    }

    if (((left.tag == JT_NULL) && (right.tag == JT_UNDEF)) || ((left.tag == JT_UNDEF) && (right.tag == JT_NULL))) {
        return 1;
    }

    if ((left.tag == JT_NULL) || (left.tag == JT_UNDEF) || (right.tag == JT_NULL) || (right.tag == JT_UNDEF)) {
        return 0;
    }

    if ((left.tag == JT_NUM) && (right.tag == JT_STR)) {
        return (left.u.n == js_to_number(pCtx, right)) ? 1 : 0;
    }

    if ((left.tag == JT_STR) && (right.tag == JT_NUM)) {
        return (js_to_number(pCtx, left) == right.u.n) ? 1 : 0;
    }

    if (left.tag == JT_BOOL) {
        JSVal converted = js_num(left.u.b ? 1 : 0);

        return js_loose_equals(pCtx, converted, right);
    }

    if (right.tag == JT_BOOL) {
        JSVal converted = js_num(right.u.b ? 1 : 0);

        return js_loose_equals(pCtx, left, converted);
    }

    if (((left.tag == JT_NUM) || (left.tag == JT_STR)) && (right.tag == JT_OBJ)) {
        JSVal prim = js_to_primitive(pCtx, right, 0);
        int bResult = js_loose_equals(pCtx, left, prim);

        js_release(pCtx, prim);

        return bResult;
    }

    if ((left.tag == JT_OBJ) && ((right.tag == JT_NUM) || (right.tag == JT_STR))) {
        JSVal prim = js_to_primitive(pCtx, left, 0);
        int bResult = js_loose_equals(pCtx, prim, right);

        js_release(pCtx, prim);

        return bResult;
    }

    return 0;
}

/* ------------------------------------------------------------ exceptions  */

int js_has_exception(JSCtx *pCtx)
{
    return pCtx->bException;
}

JSVal js_throw_value(JSCtx *pCtx, JSVal value)
{
    if (!pCtx->bException) {
        /* The text has to be produced before the flag is raised: with the
         * flag set js_to_primitive refuses to call the value's toString and
         * every object would report "undefined".                          */
        JSVal text = js_to_string(pCtx, value);

        if (pCtx->bException) {
            /* Stringifying threw; that exception wins. */
            js_release(pCtx, text);
            js_release(pCtx, value);

            return js_undefined();
        }

        pCtx->bException = 1;
        pCtx->exception = value;

        xx_js_free(pCtx->pErrorText);
        pCtx->pErrorText = xx_js_strndup(js_str_data(text), js_str_len(text));
        js_release(pCtx, text);
        js_debug_exception(pCtx);
    } else {
        js_release(pCtx, value);
    }

    return js_undefined();
}

XX_RT_PRINTF_LIKE(2, 3) JSVal js_throw(JSCtx *pCtx, const char *pFormat, ...)
{
    char sBuf[1024];
    XX_RT_VA_LIST args;

    XX_RT_VA_START(args, pFormat);
    xx_rt_vsnprintf(sBuf, sizeof(sBuf), pFormat, args);
    XX_RT_VA_END(args);

    return js_throw_value(pCtx, js_str(pCtx, sBuf));
}

const char *js_error(JSCtx *pCtx)
{
    return pCtx->pErrorText ? pCtx->pErrorText : "";
}

void js_clear_error(JSCtx *pCtx)
{
    if (pCtx->bException) {
        js_release(pCtx, pCtx->exception);
    }

    pCtx->bException = 0;
    pCtx->exception = js_undefined();
    xx_js_free(pCtx->pErrorText);
    pCtx->pErrorText = NULL;
}

void js_set_user(JSCtx *pCtx, void *pUser)
{
    pCtx->pUser = pUser;
}

void *js_get_user(JSCtx *pCtx)
{
    return pCtx->pUser;
}
