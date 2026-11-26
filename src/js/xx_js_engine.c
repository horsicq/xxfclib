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

/* js_engine.c - context creation, teardown and program evaluation. */

#include "xx_js_internal.h"
#include "xx_js_ast.h"
#include "xxfclib/threads/xx_threads.h"
#include "xxfclib/algo/crc/xx_crc.h"

/* Contexts own references to immutable programs because functions retain AST
 * pointers after evaluation returns. The optional cache owns a separate
 * reference; eviction never invalidates functions in a live context. */
typedef struct JSProgram {
    volatile long nRefs;
    JSNode *pRoot;
    struct JSProgram *pPrev, *pNext, *pHashNext;
    unsigned char *pData;
    char *pName;
    size_t nDataSize, nStorage;
    uint32_t nHash;
    int bBytecode;
} JSProgram;

#define JS_PROGRAM_CACHE_BUCKETS 257U
struct JSProgramCache {
    volatile long nRefs;
    xx_mutex_t *pMutex;
    size_t nLimit, nRetained;
    JSProgram *pHead, *pTail;
    JSProgram *pBuckets[JS_PROGRAM_CACHE_BUCKETS];
};

static size_t program_add_size(size_t a, size_t b)
{
    return b > SIZE_MAX - a ? SIZE_MAX : a + b;
}

/* Requested allocation sizes, including spare list capacity and compiled
 * regular expressions. Saturation makes an unrepresentable program uncachable. */
static size_t program_node_size(const JSNode *pNode)
{
    size_t nSize = 0;
    while (pNode) {
        size_t i;
        size_t nTextSize = pNode->pStr ? xx_rt_strlen(pNode->pStr) : 0;
        if ((pNode->type == N_STR || pNode->type == N_PROP) && pNode->nNum > 0 && pNode->nNum < (double)SIZE_MAX) nTextSize = (size_t)pNode->nNum;
        nSize = program_add_size(nSize, sizeof(JSNode));
        if (pNode->pStr) nSize = program_add_size(nSize, program_add_size(nTextSize, 1));
        if (pNode->pSourceName) nSize = program_add_size(nSize, program_add_size(xx_rt_strlen(pNode->pSourceName), 1));
        if (pNode->pStr2) nSize = program_add_size(nSize, program_add_size(xx_rt_strlen(pNode->pStr2), 1));
        if (pNode->nListCapacity > SIZE_MAX / sizeof(JSNode *)) return SIZE_MAX;
        nSize = program_add_size(nSize, pNode->nListCapacity * sizeof(JSNode *));
        if (pNode->nVarNames > SIZE_MAX / sizeof(char *)) return SIZE_MAX;
        nSize = program_add_size(nSize, pNode->nVarNames * sizeof(char *));
        for (i = 0; i < pNode->nVarNames; i++) nSize = program_add_size(nSize, program_add_size(xx_rt_strlen(pNode->ppVarNames[i]), 1));
        if (pNode->pHoistPlan) {
            if (pNode->pHoistPlan->nSteps > (SIZE_MAX - sizeof(JSHoistPlan)) / sizeof(JSHoistStep)) return SIZE_MAX;
            nSize = program_add_size(nSize, sizeof(JSHoistPlan) + pNode->pHoistPlan->nSteps * sizeof(JSHoistStep));
        }
        if (pNode->type == N_FUNCTION && pNode->pLayout) nSize = program_add_size(nSize, pNode->pLayout->nBytes);
        nSize = program_add_size(nSize, xx_regex_memory_size(pNode->pRegExpLiteral));
        nSize = program_add_size(nSize, program_node_size(pNode->b));
        nSize = program_add_size(nSize, program_node_size(pNode->c));
        nSize = program_add_size(nSize, program_node_size(pNode->d));
        for (i = 0; i < pNode->nList; i++) nSize = program_add_size(nSize, program_node_size(pNode->ppList[i]));
        pNode = pNode->a;
    }
    return nSize;
}

static JSProgram *program_ref(JSProgram *pProgram)
{
    xx_atomic_add(&pProgram->nRefs, 1);
    return pProgram;
}

static void program_unref(JSProgram *pProgram)
{
    if (!pProgram || xx_atomic_add(&pProgram->nRefs, -1) != 1) return;
    js_free_node(pProgram->pRoot);
    xx_js_free(pProgram->pData);
    xx_js_free(pProgram->pName);
    xx_js_free(pProgram);
}

JSProgramCache *js_program_cache_new(size_t nLimit)
{
    xx_mutex_t *pMutex = xx_mutex_create();
    JSProgramCache *pCache;
    if (!pMutex) return NULL;
    pCache = (JSProgramCache *)xx_js_calloc(1, sizeof(*pCache));
    pCache->nRefs = 1;
    pCache->pMutex = pMutex;
    pCache->nLimit = nLimit;
    return pCache;
}

void js_program_cache_free(JSProgramCache *pCache)
{
    JSProgram *pProgram;
    if (!pCache || xx_atomic_add(&pCache->nRefs, -1) != 1) return;
    pProgram = pCache->pHead;
    while (pProgram) {
        JSProgram *pNext = pProgram->pNext;
        program_unref(pProgram);
        pProgram = pNext;
    }
    xx_mutex_destroy(pCache->pMutex);
    xx_js_free(pCache);
}

void js_set_program_cache(JSCtx *pCtx, JSProgramCache *pCache)
{
    JSProgramCache *pOld;
    if (!pCtx) return;
    if (pCache) xx_atomic_add(&pCache->nRefs, 1);
    pOld = pCtx->pProgramCache;
    pCtx->pProgramCache = pCache;
    js_program_cache_free(pOld);
}

static void program_cache_touch(JSProgramCache *pCache, JSProgram *pProgram)
{
    if (pCache->pHead == pProgram) return;
    if (pProgram->pPrev) pProgram->pPrev->pNext = pProgram->pNext;
    if (pProgram->pNext) pProgram->pNext->pPrev = pProgram->pPrev;
    if (pCache->pTail == pProgram) pCache->pTail = pProgram->pPrev;
    pProgram->pPrev = NULL;
    pProgram->pNext = pCache->pHead;
    if (pCache->pHead) pCache->pHead->pPrev = pProgram;
    else pCache->pTail = pProgram;
    pCache->pHead = pProgram;
}

static JSProgram *program_cache_find(JSProgramCache *pCache, const void *pData, size_t nSize, const char *pName, int bBytecode, uint32_t nHash)
{
    JSProgram *pProgram = pCache->pBuckets[nHash % JS_PROGRAM_CACHE_BUCKETS];
    while (pProgram) {
        if (pProgram->nHash == nHash && pProgram->nDataSize == nSize && pProgram->bBytecode == bBytecode && !xx_rt_strcmp(pProgram->pName, pName) &&
            (!nSize || !xx_rt_memcmp(pProgram->pData, pData, nSize))) {
            program_cache_touch(pCache, pProgram);
            return program_ref(pProgram);
        }
        pProgram = pProgram->pHashNext;
    }
    return NULL;
}

static void program_cache_evict(JSProgramCache *pCache)
{
    JSProgram *pProgram = pCache->pTail;
    JSProgram **ppHash = &pCache->pBuckets[pProgram->nHash % JS_PROGRAM_CACHE_BUCKETS];
    while (*ppHash != pProgram) ppHash = &(*ppHash)->pHashNext;
    *ppHash = pProgram->pHashNext;
    pCache->pTail = pProgram->pPrev;
    if (pCache->pTail) pCache->pTail->pNext = NULL;
    else pCache->pHead = NULL;
    pCache->nRetained -= pProgram->nStorage;
    pProgram->pPrev = pProgram->pNext = pProgram->pHashNext = NULL;
    program_unref(pProgram);
}

static JSProgram *program_load(JSCtx *pCtx, const void *pData, size_t nSize, const char *pName, int bBytecode, char **ppError)
{
    JSProgramCache *pCache = pCtx->pProgramCache;
    JSProgram *pProgram = NULL;
    JSNode *pRoot;
    uint32_t nHash = 0;
    size_t nStorage;
    if (!pName) pName = "";
    if (pCache && nSize <= pCache->nLimit) {
        /* Hardware CRC32C accelerates large immutable-content cache keys.
         * Exact source/bytecode bytes, name, and kind still decide equality. */
        nHash = xx_crc32c_calc(UINT32_MAX, pData, nSize) ^ xx_js_hash_str(pName, xx_rt_strlen(pName)) ^ (uint32_t)bBytecode;
        xx_mutex_lock(pCache->pMutex);
        pProgram = program_cache_find(pCache, pData, nSize, pName, bBytecode, nHash);
        xx_mutex_unlock(pCache->pMutex);
        if (pProgram) return pProgram;
    }
    pRoot = bBytecode ? js_parse_bytecode(pData, nSize) : js_parse_program(pCtx, (const char *)pData, pName, ppError);
    if (!pRoot) return NULL;
    if (bBytecode) js_node_set_source(pRoot, *pName ? pName : NULL);
    pProgram = (JSProgram *)xx_js_calloc(1, sizeof(*pProgram));
    pProgram->nRefs = 1;
    pProgram->pRoot = pRoot;
    if (!pCache || nSize > pCache->nLimit) return pProgram;
    nStorage = program_add_size(sizeof(*pProgram), program_node_size(pRoot));
    /* Serialized bytecode may contain embedded NULs or inconsistent N_STR
     * length metadata. All encoded string payloads together fit in nSize;
     * reserve this conservative extra bound instead of undercounting them. */
    if (bBytecode) nStorage = program_add_size(nStorage, nSize);
    nStorage = program_add_size(nStorage, nSize ? nSize : 1);
    nStorage = program_add_size(nStorage, program_add_size(xx_rt_strlen(pName), 1));
    if (nStorage == SIZE_MAX || nStorage > pCache->nLimit) return pProgram;
    pProgram->pData = (unsigned char *)xx_js_malloc(nSize ? nSize : 1);
    if (nSize) xx_rt_memcpy(pProgram->pData, pData, nSize);
    pProgram->pName = xx_js_strdup(pName);
    pProgram->nDataSize = nSize;
    pProgram->nHash = nHash;
    pProgram->bBytecode = bBytecode;
    pProgram->nStorage = nStorage;
    xx_mutex_lock(pCache->pMutex);
    {
        JSProgram *pExisting = program_cache_find(pCache, pData, nSize, pName, bBytecode, nHash);
        if (pExisting) {
            xx_mutex_unlock(pCache->pMutex);
            program_unref(pProgram);
            return pExisting;
        }
    }
    while (pCache->pTail && nStorage > pCache->nLimit - pCache->nRetained) program_cache_evict(pCache);
    pProgram->pHashNext = pCache->pBuckets[nHash % JS_PROGRAM_CACHE_BUCKETS];
    pCache->pBuckets[nHash % JS_PROGRAM_CACHE_BUCKETS] = pProgram;
    program_cache_touch(pCache, pProgram);
    pCache->nRetained += nStorage;
    program_ref(pProgram);
    xx_mutex_unlock(pCache->pMutex);
    return pProgram;
}

JSCtx *js_new(void)
{
    JSCtx *pCtx = (JSCtx *)xx_js_calloc(1, sizeof(JSCtx));

    /* Deep enough for any database rule, shallow enough that the native
     * stack cannot be exhausted by a runaway recursive script.            */
    pCtx->nMaxCallDepth = 200;
    /* The evaluator recurses once per AST node, and a left-leaning operator
     * chain nests as deeply as it is long without the parser ever recursing,
     * so the evaluator needs a ceiling of its own. Measured overflow of an
     * 8 MB stack sits above 18000 levels; this leaves better than a factor
     * of two in hand.                                                      */
    pCtx->nMaxEvalDepth = 8000;
    pCtx->nGlobalEpoch = 1;
    pCtx->exception = js_undefined();
    xx_list_init(&pCtx->vecPrograms, sizeof(void *), NULL);

    /* The prototypes have to exist before the global object is populated. */
    pCtx->pObjectProto = jsobj_new(pCtx, JCLASS_OBJECT, NULL);
    pCtx->pFunctionProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pArrayProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pUint32ArrayProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pStringProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pNumberProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pBooleanProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pRegExpProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pErrorProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);

    pCtx->pGlobal = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pGlobalScope = NULL;

    js_install_builtins(pCtx);

    jsobj_put_hidden(pCtx, pCtx->pGlobal, "globalThis", jsval_obj(jsobj_ref(pCtx->pGlobal)));

    return pCtx;
}

void js_free(JSCtx *pCtx)
{
    size_t i = 0;

    if (pCtx == NULL) {
        return;
    }

    for (i = 0; i < xx_list_count(&pCtx->vecPrograms); i++) {
        program_unref(*(JSProgram **)xx_list_at(&pCtx->vecPrograms, i));
    }

    xx_list_cleanup(&pCtx->vecPrograms);
    xx_js_free(pCtx->ppProgramPins);
    js_program_cache_free(pCtx->pProgramCache);

    /* Whatever is left is caught in a reference cycle; sweep it flat, without
     * releasing the values, so that nothing can be freed twice.           */
    {
        JSObj *pObj = pCtx->pAllObjects;

        while (pObj) {
            JSObj *pNext = pObj->pNextAll;
            size_t j = 0;

            for (j = 0; j < pObj->props.nSize; j++) {
                if (!pObj->props.pEntries[j].bBorrowedKey) xx_js_free(pObj->props.pEntries[j].pKey);
            }

            xx_js_free(pObj->props.pEntries);
            xx_js_free(pObj->props.pIndex);
            xx_js_free(pObj->pUint32Data);
            xx_js_free(pObj->pArrayData);
            xx_js_free(pObj->pBoundArgs);
            if (!pObj->bBorrowedFnName) xx_js_free(pObj->pFnName);

            if (pObj->pRegExp) {
                xx_regex_free(pObj->pRegExp);
            }

            xx_js_free(pObj);
            pObj = pNext;
        }
    }

    {
        JSScope *pScope = pCtx->pAllScopes;

        while (pScope) {
            JSScope *pNext = pScope->pNextAll;

            xx_js_free(pScope);
            pScope = pNext;
        }
    }

    {
        JSStr *pStr = pCtx->pAllStrings;

        while (pStr) {
            JSStr *pNext = pStr->pNextAll;

            if (!pStr->bInline) xx_js_free(pStr->pData);
            xx_js_free(pStr);
            pStr = pNext;
        }
    }

    jspool_release(pCtx);
    xx_js_free(pCtx->pErrorText);
    xx_js_free(pCtx);
}

/* A context needs one AST lifetime pin per immutable program, regardless of
 * how many times eval executes it. program_load transfers one reference here. */
static size_t program_pin_slot(JSProgram *pProgram, size_t nCapacity)
{
    uintptr_t nHash = (uintptr_t)pProgram >> 4;
    nHash ^= nHash >> 17;
    nHash *= (uintptr_t)2654435761u;
    return (size_t)nHash & (nCapacity - 1);
}

static void program_pin(JSCtx *pCtx, JSProgram *pProgram)
{
    size_t nSlot, i, nCount = xx_list_count(&pCtx->vecPrograms);
    if (pCtx->nProgramPinsCapacity) {
        nSlot = program_pin_slot(pProgram, pCtx->nProgramPinsCapacity);
        while (pCtx->ppProgramPins[nSlot]) {
            if (pCtx->ppProgramPins[nSlot] == pProgram) {
                program_unref(pProgram);
                return;
            }
            nSlot = (nSlot + 1) & (pCtx->nProgramPinsCapacity - 1);
        }
    }
    if (!pCtx->nProgramPinsCapacity || nCount + 1 > pCtx->nProgramPinsCapacity / 2) {
        size_t nNew = pCtx->nProgramPinsCapacity ? pCtx->nProgramPinsCapacity * 2 : 16;
        xx_js_free(pCtx->ppProgramPins);
        pCtx->ppProgramPins = (void **)xx_js_calloc(nNew, sizeof(void *));
        pCtx->nProgramPinsCapacity = nNew;
        for (i = 0; i < nCount; i++) {
            JSProgram *pOld = *(JSProgram **)xx_list_at(&pCtx->vecPrograms, i);
            nSlot = program_pin_slot(pOld, nNew);
            while (pCtx->ppProgramPins[nSlot]) nSlot = (nSlot + 1) & (nNew - 1);
            pCtx->ppProgramPins[nSlot] = pOld;
        }
    }
    nSlot = program_pin_slot(pProgram, pCtx->nProgramPinsCapacity);
    while (pCtx->ppProgramPins[nSlot]) nSlot = (nSlot + 1) & (pCtx->nProgramPinsCapacity - 1);
    pCtx->ppProgramPins[nSlot] = pProgram;
    xx_list_append(&pCtx->vecPrograms, &pProgram);
}

/* Evaluates a nested program without clearing a pending outer exception. */
static int eval_nested_data(JSCtx *pCtx, const void *pData, size_t nSize, const char *pName, int bBytecode)
{
    char *pError = NULL;
    JSProgram *pProgram;
    JSVal value;
    if (!pCtx || !pData || pCtx->bDebugAborted) return 0;
    if (bBytecode && !js_is_bytecode(pData, nSize)) {
        js_throw(pCtx, "Invalid bytecode format");
        return 0;
    }
    pProgram = program_load(pCtx, pData, nSize, pName, bBytecode, &pError);
    if (!pProgram) {
        js_throw(pCtx, "%s", pError ? pError : (bBytecode ? "Corrupt bytecode stream" : "SyntaxError"));
        xx_js_free(pError);
        return 0;
    }
    program_pin(pCtx, pProgram);
    value = js_run_program(pCtx, pProgram->pRoot, pCtx->pGlobalScope, jsval_obj(jsobj_ref(pCtx->pGlobal)));
    js_release(pCtx, value);
    return pCtx->bException || pCtx->bDebugAborted ? 0 : 1;
}

int js_eval_nested(JSCtx *pCtx, const char *pSource, const char *pName)
{
    return eval_nested_data(pCtx, pSource, pSource ? xx_rt_strlen(pSource) : 0, pName, 0);
}

int js_eval_nested_bytecode(JSCtx *pCtx, const void *pBytecode, size_t nSize, const char *pName)
{
    return eval_nested_data(pCtx, pBytecode, nSize, pName, 1);
}

int js_eval(JSCtx *pCtx, const char *pSource, const char *pName, JSVal *pResult)
{
    char *pError = NULL;
    JSProgram *pProgram;
    JSVal value;
    if (pResult) *pResult = js_undefined();
    if (!pCtx || !pSource) return 0;
    if (pCtx->bDebugInHook || (pCtx->bDebugAborted && pCtx->pDebugFrame)) return 0;
    if (!pCtx->pDebugFrame) pCtx->bDebugAborted = 0;
    js_clear_error(pCtx);
    pProgram = program_load(pCtx, pSource, xx_rt_strlen(pSource), pName, 0, &pError);
    if (!pProgram) {
        xx_js_free(pCtx->pErrorText);
        pCtx->pErrorText = pError ? pError : xx_js_strdup("SyntaxError");
        pCtx->bException = 1;
        pCtx->exception = js_str(pCtx, pCtx->pErrorText);
        return 0;
    }
    program_pin(pCtx, pProgram);
    value = js_run_program(pCtx, pProgram->pRoot, pCtx->pGlobalScope, jsval_obj(jsobj_ref(pCtx->pGlobal)));
    if (pCtx->bException || pCtx->bDebugAborted) {
        js_release(pCtx, value);
        return 0;
    }
    if (pResult) *pResult = value;
    else js_release(pCtx, value);
    return 1;
}
