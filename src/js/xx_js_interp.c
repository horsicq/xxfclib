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

/* js_interp.c - tree walking evaluator. */

#include "xx_js_ast.h"

#include <limits.h>

typedef enum {
    CT_NORMAL = 0,
    CT_BREAK,
    CT_CONTINUE,
    CT_RETURN
} JSCompletionType;

typedef struct {
    JSCompletionType type;
    JSVal value;
    const char *pLabel;
} JSCompletion;

struct JSDebugFrameState {
    JSDebugFrameState *pPrevious;
    JSScope *pScope;
    JSScope *pBaseScope;
    JSVal thisVal;
    JSObj *pFunction;
    JSNode *pNode;
};
typedef struct {
    JSScope *pScope;
    JSVal thisVal;
    JSObj *pFunction;
    JSDebugFrameState *pDebug;
} JSFrame;

static JSDebugFrameState *debug_frame_at(JSCtx *pCtx, size_t nFrame)
{
    JSDebugFrameState *pFrame;
    if (!pCtx || !pCtx->bDebugInHook) return NULL;
    pFrame = pCtx->pDebugFrame;
    while (pFrame && nFrame--) pFrame = pFrame->pPrevious;
    return pFrame;
}
static const char *debug_source_name(JSDebugFrameState *pFrame)
{
    JSNode *pNode = pFrame ? pFrame->pNode : NULL;
    JSNode *pRoot = pNode ? pNode->pSourceRoot : NULL;
    return pRoot && pRoot->pSourceName ? pRoot->pSourceName : "";
}
static const char *debug_function_name(JSDebugFrameState *pFrame)
{
    if (!pFrame || !pFrame->pFunction) return "<program>";
    return pFrame->pFunction->pFnName && pFrame->pFunction->pFnName[0] ? pFrame->pFunction->pFnName : "<anonymous>";
}
void js_debug_set_hook(JSCtx *pCtx, JSDebugHook pHook, void *pUser)
{
    if (!pCtx || pCtx->pDebugFrame || pCtx->bDebugInHook) return;
    pCtx->pDebugHook = pHook;
    pCtx->pDebugUser = pUser;
    pCtx->bDebugAborted = 0;
}
void js_debug_get_hook(JSCtx *pCtx, JSDebugHook *ppHook, void **ppUser)
{
    if (ppHook) *ppHook = pCtx ? pCtx->pDebugHook : NULL;
    if (ppUser) *ppUser = pCtx ? pCtx->pDebugUser : NULL;
}
size_t js_debug_frame_count(JSCtx *pCtx)
{
    JSDebugFrameState *pFrame = debug_frame_at(pCtx, 0);
    size_t nCount = 0;
    for (; pFrame; pFrame = pFrame->pPrevious) nCount++;
    return nCount;
}
int js_debug_frame(JSCtx *pCtx, size_t nFrame, JSDebugFrameInfo *pInfo)
{
    JSDebugFrameState *pFrame = debug_frame_at(pCtx, nFrame);
    if (!pFrame || !pInfo) return 0;
    pInfo->pSourceName = debug_source_name(pFrame);
    pInfo->pFunctionName = debug_function_name(pFrame);
    pInfo->nLine = pFrame->pNode ? pFrame->pNode->nLine : 0;
    pInfo->nColumn = pFrame->pNode ? pFrame->pNode->nColumn : 0;
    pInfo->bNative = pFrame->pFunction && pFrame->pFunction->cls == JCLASS_NATIVE;
    return 1;
}
static JSProp *debug_local_at(JSCtx *pCtx, JSDebugFrameState *pFrame, size_t nLocal, size_t *pnCount)
{
    JSScope *pScope;
    size_t nCount = 0;
    if (!pFrame || (pFrame->pFunction && pFrame->pFunction->cls == JCLASS_NATIVE)) return NULL;
    for (pScope = pFrame->pScope; pScope; pScope = pScope->pParent) {
        size_t i;
        for (i = 0; i < pScope->pVars->props.nSize; i++) {
            JSProp *pProp = &pScope->pVars->props.pEntries[i];
            if (pProp->bDeleted) continue;
            if (!pnCount && nCount == nLocal) return pProp;
            nCount++;
        }
        if (pScope == pFrame->pBaseScope) break;
    }
    /* Global programs use a NULL scope: their bindings live on pGlobal. */
    if (!pFrame->pBaseScope) {
        size_t i;
        for (i = 0; i < pCtx->pGlobal->props.nSize; i++) {
            JSProp *pProp = &pCtx->pGlobal->props.pEntries[i];
            if (pProp->bDeleted) continue;
            if (!pnCount && nCount == nLocal) return pProp;
            nCount++;
        }
    }
    if (pnCount) *pnCount = nCount;
    return NULL;
}
size_t js_debug_local_count(JSCtx *pCtx, size_t nFrame)
{
    size_t nCount = 0;
    debug_local_at(pCtx, debug_frame_at(pCtx, nFrame), 0, &nCount);
    return nCount;
}
int js_debug_local(JSCtx *pCtx, size_t nFrame, size_t nLocal, const char **ppName, JSVal *pValue)
{
    JSProp *pProp = debug_local_at(pCtx, debug_frame_at(pCtx, nFrame), nLocal, NULL);
    if (ppName) *ppName = NULL;
    if (pValue) *pValue = js_undefined();
    if (!pProp) return 0;
    if (ppName) *ppName = pProp->pKey;
    if (pValue) *pValue = js_dup(pProp->value);
    return 1;
}
int js_debug_lookup(JSCtx *pCtx, size_t nFrame, const char *pIdentifier, JSVal *pValue)
{
    JSDebugFrameState *pFrame = debug_frame_at(pCtx, nFrame);
    JSScope *pScope;
    size_t nSize, i;
    if (pValue) *pValue = js_undefined();
    if (!pFrame || !pIdentifier || !*pIdentifier) return 0;
    if (!xx_rt_strcmp(pIdentifier, "this")) { if (pValue) *pValue = js_dup(pFrame->thisVal); return 1; }
    nSize = xx_rt_strlen(pIdentifier);
    for (i = 0; i < nSize; i++) {
        unsigned char c = (unsigned char)pIdentifier[i];
        if (!(c == '_' || c == '$' || c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (i && c >= '0' && c <= '9'))) return 0;
    }
    if (pFrame->pFunction && pFrame->pFunction->cls == JCLASS_NATIVE) return 0;
    for (pScope = pFrame->pScope; pScope; pScope = pScope->pParent) {
        JSProp *pProp = jsprops_find(&pScope->pVars->props, pIdentifier, nSize);
        if (pProp) { if (pValue) *pValue = js_dup(pProp->value); return 1; }
    }
    {
        JSProp *pProp = jsprops_find(&pCtx->pGlobal->props, pIdentifier, nSize);
        if (pProp) { if (pValue) *pValue = js_dup(pProp->value); return 1; }
    }
    return 0;
}
static void debug_abort(JSCtx *pCtx)
{
    pCtx->bDebugAborted = 1;
    js_clear_error(pCtx);
    pCtx->bException = 1;
    pCtx->exception = js_str(pCtx, "Debugger: execution cancelled");
    pCtx->pErrorText = xx_js_strdup("Debugger: execution cancelled");
}
static int debug_emit(JSCtx *pCtx, JSDebugEventType type)
{
    JSDebugEvent event;
    JSDebugFrameState *pFrame = pCtx->pDebugFrame, *pCurrent;
    int bContinue;
    if (pCtx->bDebugAborted) return 0;
    if (!pCtx->pDebugHook || pCtx->bDebugInHook || !pFrame) return 1;
    event.type = type;
    event.pSourceName = debug_source_name(pFrame);
    event.pFunctionName = debug_function_name(pFrame);
    event.nLine = pFrame->pNode ? pFrame->pNode->nLine : 0;
    event.nColumn = pFrame->pNode ? pFrame->pNode->nColumn : 0;
    event.nDepth = 0;
    for (pCurrent = pFrame; pCurrent; pCurrent = pCurrent->pPrevious) event.nDepth++;
    pCtx->bDebugInHook = 1;
    bContinue = pCtx->pDebugHook(pCtx, &event, pCtx->pDebugUser);
    pCtx->bDebugInHook = 0;
    if (!bContinue) debug_abort(pCtx);
    return bContinue != 0;
}
void js_debug_exception(JSCtx *pCtx)
{
    if (pCtx && pCtx->pDebugHook && !pCtx->bDebugAborted) debug_emit(pCtx, JS_DEBUG_EXCEPTION);
}
static int debug_checkpoint(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    if (pCtx->bDebugAborted) return 0;
    if (!pCtx->pDebugHook) return 1;
    pFrame->pDebug->pNode = pNode;
    pFrame->pDebug->pScope = pFrame->pScope;
    return debug_emit(pCtx, pNode && pNode->type == N_DEBUGGER ? JS_DEBUG_DEBUGGER : JS_DEBUG_STATEMENT);
}
static void debug_push(JSCtx *pCtx, JSDebugFrameState *pDebug, JSScope *pScope, JSObj *pFunction, JSVal thisVal, JSNode *pNode)
{
    pDebug->pPrevious = pCtx->pDebugFrame;
    pDebug->pScope = pDebug->pBaseScope = pScope;
    pDebug->pFunction = pFunction;
    pDebug->thisVal = thisVal;
    pDebug->pNode = pNode;
    pCtx->pDebugFrame = pDebug;
}

static JSVal eval_expr(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame);
static JSCompletion exec_stmt(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame);
static void hoist_declarations(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel);
static void hoist_prepared(JSCtx *pCtx, JSHoistPlan *pPlan, JSNode *pBody, JSFrame *pFrame);

static JSCompletion completion_normal(void)
{
    JSCompletion completion;

    completion.type = CT_NORMAL;
    completion.value = js_undefined();
    completion.pLabel = NULL;

    return completion;
}

/* ------------------------------------------------------------ variables  */

/* Walks the scope chain, then the global object, with a hash the caller has
 * already computed, and hands back the entry itself so that the caller does
 * not have to look it up a second time. Variable access is by far the
 * hottest operation in the engine.                                        */
static JSProp *scope_find_prop(JSCtx *pCtx, JSScope *pScope, const char *pName, size_t nNameSize, uint32_t nHash, JSObj **ppHolder)
{
    JSScope *pCurrent = pScope;
    JSProp *pProp = NULL;

    while (pCurrent) {
        pProp = jsprops_find_hashed(&pCurrent->pVars->props, pName, nNameSize, nHash);

        if (pProp) {
            *ppHolder = pCurrent->pVars;

            return pProp;
        }

        pCurrent = pCurrent->pParent;
    }

    pProp = jsprops_find_hashed(&pCtx->pGlobal->props, pName, nNameSize, nHash);

    if (pProp) {
        *ppHolder = pCtx->pGlobal;

        return pProp;
    }

    *ppHolder = NULL;

    return NULL;
}

/* Fills in a node's cached key length and hash on first use. */
static void node_key(JSNode *pNode, size_t *pnSize, uint32_t *pnHash)
{
    if (!pNode->bStrKey) {
        pNode->nStrSize = xx_rt_strlen(pNode->pStr);
        pNode->nStrHash = xx_js_hash_str(pNode->pStr, pNode->nStrSize);
        pNode->bStrKey = 1;
    }

    *pnSize = pNode->nStrSize;
    *pnHash = pNode->nStrHash;
}

static JSVal scope_get(JSCtx *pCtx, JSScope *pScope, const char *pName, size_t nNameSize, uint32_t nHash, int *pbFound)
{
    JSObj *pHolder = NULL;
    JSProp *pProp = scope_find_prop(pCtx, pScope, pName, nNameSize, nHash, &pHolder);

    if (pProp == NULL) {
        if (pbFound) {
            *pbFound = 0;
        }

        return js_undefined();
    }

    if (pbFound) {
        *pbFound = 1;
    }

    /* jsobj_get_own only deviates from the stored value for arrays and for
     * string wrappers, which a scope record and the global object are not. */
    if ((pHolder->cls == JCLASS_ARRAY) || (pHolder->cls == JCLASS_STRING)) {
        return jsobj_get_own(pCtx, pHolder, pName, nNameSize, NULL);
    }

    return js_dup(pProp->value);
}

static void scope_set(JSCtx *pCtx, JSScope *pScope, const char *pName, size_t nNameSize, uint32_t nHash, JSVal value)
{
    JSObj *pHolder = NULL;
    JSProp *pProp = scope_find_prop(pCtx, pScope, pName, nNameSize, nHash, &pHolder);

    if (pHolder == NULL) {
        /* Implicit global, as in sloppy-mode ECMAScript. */
        pHolder = pCtx->pGlobal;
    } else if (pHolder->cls != JCLASS_ARRAY) {
        /* Overwriting an existing entry of a plain object is all jsobj_put
         * would do here, and the entry is already in hand.                */
        jsprop_store(pCtx, pProp, value);

        return;
    }

    jsobj_put(pCtx, pHolder, pName, nNameSize, value);
}

static JSVal scope_get_node(JSCtx *pCtx, JSScope *pScope, JSNode *pNode, int *pbFound)
{
    size_t nNameSize = 0;
    uint32_t nHash = 0;

    node_key(pNode, &nNameSize, &nHash);

    return scope_get(pCtx, pScope, pNode->pStr, nNameSize, nHash, pbFound);
}

static void scope_set_node(JSCtx *pCtx, JSScope *pScope, JSNode *pNode, JSVal value)
{
    size_t nNameSize = 0;
    uint32_t nHash = 0;

    node_key(pNode, &nNameSize, &nHash);

    scope_set(pCtx, pScope, pNode->pStr, nNameSize, nHash, value);
}

static void scope_declare(JSCtx *pCtx, JSScope *pScope, const char *pName, JSVal value)
{
    JSObj *pVars = pScope ? pScope->pVars : pCtx->pGlobal;

    jsobj_put(pCtx, pVars, pName, xx_rt_strlen(pName), value);
}

/* ----------------------------------------------------- property helpers  */

static JSObj *proto_for_primitive(JSCtx *pCtx, JSVal value)
{
    switch (value.tag) {
        case JT_STR: return pCtx->pStringProto;
        case JT_NUM: return pCtx->pNumberProto;
        case JT_BOOL: return pCtx->pBooleanProto;
        default: return NULL;
    }
}

static JSVal get_property(JSCtx *pCtx, JSVal base, const char *pKey, size_t nKeySize)
{
    if ((base.tag == JT_UNDEF) || (base.tag == JT_NULL)) {
        char sKey[128];

        xx_rt_snprintf(sKey, sizeof(sKey), "%.*s", (int)nKeySize, pKey);
        js_throw(pCtx, "TypeError: cannot read property '%s' of %s", sKey, (base.tag == JT_NULL) ? "null" : "undefined");

        return js_undefined();
    }

    if (base.tag == JT_STR) {
        if ((nKeySize == 6) && (xx_rt_memcmp(pKey, "length", 6) == 0)) {
            return js_num((double)base.u.s->nSize);
        }

        {
            int64_t nIndex = 0;

            if (js_is_array_index(pKey, nKeySize, &nIndex)) {
                if ((nIndex >= 0) && ((size_t)nIndex < base.u.s->nSize)) {
                    return js_strn(pCtx, base.u.s->pData + nIndex, 1);
                }

                return js_undefined();
            }
        }
    }

    if (base.tag == JT_OBJ) {
        return jsobj_get(pCtx, base.u.o, pKey, nKeySize);
    }

    return jsobj_get(pCtx, proto_for_primitive(pCtx, base), pKey, nKeySize);
}

static void set_property(JSCtx *pCtx, JSVal base, const char *pKey, size_t nKeySize, JSVal value)
{
    if (base.tag != JT_OBJ) {
        js_release(pCtx, value);
        return;
    }

    jsobj_put(pCtx, base.u.o, pKey, nKeySize, value);
}

/* Converts an arbitrary value into a property key string. Numeric keys -
 * array indices, overwhelmingly - are rendered straight into the caller's
 * scratch buffer, so the common case allocates nothing at all. String keys
 * borrow their bytes while the caller keeps value alive. Release the result
 * with key_release() before releasing value.                               */
#define KEY_SCRATCH_SIZE 32

static char *key_from_value(JSCtx *pCtx, JSVal value, size_t *pnSize, char *pScratch)
{
    JSVal str;
    char *pResult = NULL;

    if (value.tag == JT_STR) {
        if (pnSize) *pnSize = value.u.s->nSize;
        return value.u.s->pData;
    }

    if (value.tag == JT_NUM) {
        int nSize = js_int_to_buf(pScratch, value.u.n);

        if (nSize > 0) {
            if (pnSize) {
                *pnSize = (size_t)nSize;
            }

            return pScratch;
        }
    }

    str = js_to_string(pCtx, value);
    pResult = xx_js_strndup(js_str_data(str), js_str_len(str));

    if (pnSize) {
        *pnSize = js_str_len(str);
    }

    js_release(pCtx, str);

    return pResult;
}

static void key_release(char *pKey, const char *pScratch, JSVal value)
{
    if (pKey != pScratch && (value.tag != JT_STR || pKey != value.u.s->pData)) {
        xx_js_free(pKey);
    }
}

/* js_get_index includes prototype fallback and ordinary object properties.
 * Stay inside the exact-integer decimal range, where ToString(number) and
 * the integer-key spelling are identical (including negative zero). */
static JSVal get_indexed_property(JSCtx *pCtx, JSVal base, JSVal key)
{
    char sScratch[KEY_SCRATCH_SIZE];
    size_t nKeySize = 0;
    char *pKey;
    JSVal result;
    if (base.tag == JT_OBJ && key.tag == JT_NUM && key.u.n >= 0 && key.u.n <= 9007199254740991.0 &&
        (double)(int64_t)key.u.n == key.u.n) return js_get_index(pCtx, base, (int64_t)key.u.n);
    pKey = key_from_value(pCtx, key, &nKeySize, sScratch);
    result = get_property(pCtx, base, pKey, nKeySize);
    key_release(pKey, sScratch, key);
    return result;
}

/* Most calls in signature scripts have only a few arguments. Keep their
 * temporary argument vectors on the stack; values still have their usual
 * references and the heap path remains available for larger calls. */
#define JS_LOCAL_ARGS 8

/* --------------------------------------------------------------- calling  */

static JSVal make_function(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSObj *pFn = jsobj_new(pCtx, JCLASS_FUNCTION, pCtx->pFunctionProto);
    JSVal prototype = js_new_object(pCtx);

    pFn->pFnNode = pNode;
    pFn->pScope = pFrame->pScope ? jsscope_ref(pFrame->pScope) : NULL;
    /* Programs stay retained while their runtime objects can execute. Keep
     * the immutable AST spelling instead of duplicating it for every closure. */
    pFn->pFnName = pNode->pStr ? pNode->pStr : "";
    pFn->bBorrowedFnName = 1;

    jsobj_put_hidden_static(pCtx, prototype.u.o, "constructor", jsval_obj(jsobj_ref(pFn)));
    jsobj_put_hidden_static(pCtx, pFn, "prototype", prototype);
    jsobj_put_hidden_static(pCtx, pFn, "length", js_num((double)(pNode->a ? pNode->a->nList : 0)));
    jsobj_put_hidden_static(pCtx, pFn, "name", js_str(pCtx, pFn->pFnName));

    return jsval_obj(pFn);
}

JSVal js_call_function(JSCtx *pCtx, JSObj *pFn, JSVal thisVal, int nArgc, JSVal *pArgv, int bConstruct)
{
    JSVal result = js_undefined();

    if (pCtx->bDebugAborted) return result;

    if (pFn == NULL) {
        return js_throw(pCtx, "TypeError: value is not a function");
    }

    /* Public callers supply an int argument count. Validate it before any
     * unsigned loop bound or bound-function addition can use it. */
    if (nArgc < 0) {
        return js_throw(pCtx, "RangeError: invalid argument count");
    }
    if ((pFn->cls == JCLASS_NATIVE) && pFn->pBoundTarget && (pFn->nBoundArgs > 0)) {
        if ((pFn->nBoundArgs > INT_MAX - nArgc) ||
            ((size_t)(pFn->nBoundArgs + nArgc) > SIZE_MAX / sizeof(JSVal))) {
            return js_throw(pCtx, "RangeError: too many arguments");
        }
    }

    if (pCtx->nCallDepth >= pCtx->nMaxCallDepth) {
        return js_throw(pCtx, "RangeError: maximum call stack size exceeded");
    }

    pCtx->nCallDepth++;

    if (pFn->cls == JCLASS_NATIVE) {
        JSDebugFrameState debug;
        debug_push(pCtx, &debug, NULL, pFn, thisVal, pCtx->pDebugFrame ? pCtx->pDebugFrame->pNode : NULL);
        if (pFn->pBoundTarget) {
            /* bound function: the arguments captured by bind come first */
            JSVal target = jsval_obj(jsobj_ref(pFn->pBoundTarget));

            if (pFn->nBoundArgs > 0) {
                int nTotal = pFn->nBoundArgs + nArgc;
                JSVal localArgs[JS_LOCAL_ARGS];
                JSVal *pCallArgs = (nTotal <= JS_LOCAL_ARGS) ? localArgs : (JSVal *)xx_js_malloc((size_t)nTotal * sizeof(JSVal));
                int i = 0;

                for (i = 0; i < pFn->nBoundArgs; i++) {
                    pCallArgs[i] = pFn->pBoundArgs[i];
                }

                for (i = 0; i < nArgc; i++) {
                    pCallArgs[pFn->nBoundArgs + i] = pArgv[i];
                }

                result = js_call_function(pCtx, pFn->pBoundTarget, pFn->boundThis, nTotal, pCallArgs, bConstruct);
                if (pCallArgs != localArgs) {
                    xx_js_free(pCallArgs);
                }
            } else {
                result = js_call_function(pCtx, pFn->pBoundTarget, pFn->boundThis, nArgc, pArgv, bConstruct);
            }

            js_release(pCtx, target);
        } else if (pFn->nativeFn) {
            int bPreviousConstruct = pCtx->bNativeConstruct;
            pCtx->bNativeConstruct = bConstruct;
            result = pFn->nativeFn(pCtx, thisVal, nArgc, pArgv, pFn->pUser);
            pCtx->bNativeConstruct = bPreviousConstruct;
        }

        pCtx->pDebugFrame = debug.pPrevious;
        pCtx->nCallDepth--;

        return result;
    }

    if (pFn->cls != JCLASS_FUNCTION) {
        pCtx->nCallDepth--;

        return js_throw(pCtx, "TypeError: value is not a function");
    }

    {
        JSScope *pScope = jsscope_new(pCtx, pFn->pScope);
        JSFrame frame;
        JSDebugFrameState debug;
        JSNode *pParams = pFn->pFnNode->a;
        size_t i = 0;
        JSVal globalThis = js_undefined();
        JSCompletion completion;

        frame.pScope = pScope;
        frame.thisVal = thisVal;
        frame.pFunction = pFn;

        /* Sloppy-mode ECMAScript: an undefined or null `this` becomes the
           global object. */
        if ((thisVal.tag == JT_UNDEF) || (thisVal.tag == JT_NULL)) {
            globalThis = jsval_obj(jsobj_ref(pCtx->pGlobal));
            frame.thisVal = globalThis;
        }

        frame.pDebug = &debug;
        debug_push(pCtx, &debug, pScope, pFn, frame.thisVal, pFn->pFnNode);

        /* Identifier references to arguments/eval, including nested
         * closures, conservatively keep this activation's arguments. */
        if (pFn->pFnNode->bNeedsArguments) {
            JSVal arguments = js_new_array(pCtx);

            for (i = 0; i < (size_t)nArgc; i++) {
                js_set_index(pCtx, arguments, (int64_t)i, js_dup(pArgv[i]));
            }

            scope_declare(pCtx, pScope, "arguments", arguments);
        }

        /* Named function expressions can refer to themselves; a parameter
         * of the same name shadows that binding, so it is declared first. */
        if (pFn->pFnName && pFn->pFnName[0] && (pFn->pFnNode->nNum != 0)) {
            scope_declare(pCtx, pScope, pFn->pFnName, jsval_obj(jsobj_ref(pFn)));
        }

        for (i = 0; i < pParams->nList; i++) {
            JSVal value = ((int)i < nArgc) ? js_dup(pArgv[i]) : js_undefined();

            scope_declare(pCtx, pScope, pParams->ppList[i]->pStr, value);
        }

        if (pFn->pFnNode->bNeedsHoist) {
            hoist_prepared(pCtx, pFn->pFnNode->pHoistPlan, pFn->pFnNode->b, &frame);
        }

        completion = exec_stmt(pCtx, pFn->pFnNode->b, &frame);

        if (completion.type == CT_RETURN) {
            result = completion.value;
        } else {
            if (completion.type != CT_NORMAL && !pCtx->bException) {
                js_throw(pCtx, "SyntaxError: illegal %s statement", (completion.type == CT_BREAK) ? "break" : "continue");
            }
            js_release(pCtx, completion.value);
            result = js_undefined();
        }

        pCtx->pDebugFrame = debug.pPrevious;
        js_release(pCtx, globalThis);
        jsscope_close(pCtx, pScope);
    }

    pCtx->nCallDepth--;

    return result;
}

JSVal js_call(JSCtx *pCtx, JSVal fn, JSVal thisVal, int nArgc, JSVal *pArgv)
{
    if (!js_is_callable(fn)) {
        return js_throw(pCtx, "TypeError: value is not a function");
    }

    return js_call_function(pCtx, fn.u.o, thisVal, nArgc, pArgv, 0);
}

JSVal js_construct(JSCtx *pCtx, JSVal fn, int nArgc, JSVal *pArgv)
{
    JSObj *pObj = NULL;
    JSVal prototype;
    JSVal thisVal;
    JSVal result;

    if (!js_is_callable(fn)) {
        return js_throw(pCtx, "TypeError: value is not a constructor");
    }

    prototype = js_get(pCtx, fn, "prototype");
    pObj = jsobj_new(pCtx, JCLASS_OBJECT, (prototype.tag == JT_OBJ) ? prototype.u.o : pCtx->pObjectProto);
    js_release(pCtx, prototype);

    thisVal = jsval_obj(pObj);
    result = js_call_function(pCtx, fn.u.o, thisVal, nArgc, pArgv, 1);

    if (pCtx->bException) {
        js_release(pCtx, result);
        js_release(pCtx, thisVal);

        return js_undefined();
    }

    if (result.tag == JT_OBJ) {
        js_release(pCtx, thisVal);

        return result;
    }

    js_release(pCtx, result);

    return thisVal;
}

/* --------------------------------------------------------------- hoisting */

/* hoist_declarations, eval_expr and exec_stmt_labeled walk the AST
 * recursively, so like js_call_function they need a ceiling: a deep but
 * perfectly valid tree would otherwise run the native stack out. They share
 * one budget, which bounds mixed statement and expression nesting too. The
 * test is written out at each entry point rather than factored into a helper
 * because these are the hottest functions in the engine.                   */
static void hoist_declarations_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel);

static void hoist_prepared(JSCtx *pCtx, JSHoistPlan *pPlan, JSNode *pBody, JSFrame *pFrame)
{
    size_t i;
    unsigned nBudget = pCtx->nEvalDepth < pCtx->nMaxEvalDepth ? (unsigned)(pCtx->nMaxEvalDepth - pCtx->nEvalDepth) : 0;
    if (!pPlan) {
        hoist_declarations(pCtx, pBody, pFrame, 1);
        return;
    }
    for (i = 0; i < pPlan->nSteps; i++) {
        JSHoistStep *pStep = &pPlan->steps[i];
        JSNode *pNode = pStep->pNode;
        if (pStep->nCheckDepth >= nBudget) js_throw(pCtx, "RangeError: maximum expression nesting exceeded");
        /* The original walker continues with sibling branches after a
         * depth error, but omits bindings inside the rejected subtree. */
        if (!pNode || pStep->nDepth >= nBudget) continue;
        if (pNode->type == N_FUNCTION) {
            scope_declare(pCtx, pFrame->pScope, pNode->pStr, make_function(pCtx, pNode, pFrame));
        } else {
            JSObj *pVars = pFrame->pScope ? pFrame->pScope->pVars : pCtx->pGlobal;
            if (!jsprops_find_hashed(&pVars->props, pNode->pStr, pNode->nStrSize, pNode->nStrHash)) {
                scope_declare(pCtx, pFrame->pScope, pNode->pStr, js_undefined());
            }
        }
    }
}

static void hoist_declarations(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel)
{
    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) {
        js_throw(pCtx, "RangeError: maximum expression nesting exceeded");

        return;
    }

    pCtx->nEvalDepth++;
    hoist_declarations_node(pCtx, pNode, pFrame, bTopLevel);
    pCtx->nEvalDepth--;
}

static void hoist_declarations_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel)
{
    size_t i = 0;

    if (pNode == NULL) {
        return;
    }

    switch (pNode->type) {
        case N_FUNCTION:
            if ((pNode->nNum == 0) && pNode->pStr) {
                scope_declare(pCtx, pFrame->pScope, pNode->pStr, make_function(pCtx, pNode, pFrame));
            }

            return; /* never descend into a nested function body */

        case N_VAR:
            for (i = 0; i < pNode->nList; i++) {
                JSNode *pDecl = pNode->ppList[i];
                JSObj *pVars = pFrame->pScope ? pFrame->pScope->pVars : pCtx->pGlobal;

                if (jsprops_find(&pVars->props, pDecl->pStr, xx_rt_strlen(pDecl->pStr)) == NULL) {
                    scope_declare(pCtx, pFrame->pScope, pDecl->pStr, js_undefined());
                }
            }

            return;

        case N_PROGRAM:
        case N_BLOCK:
        case N_CASE:
            for (i = 0; i < pNode->nList; i++) {
                hoist_declarations(pCtx, pNode->ppList[i], pFrame, 0);
            }

            return;

        case N_SWITCH:
            for (i = 0; i < pNode->nList; i++) {
                hoist_declarations(pCtx, pNode->ppList[i], pFrame, 0);
            }

            return;

        case N_IF:
            hoist_declarations(pCtx, pNode->b, pFrame, 0);
            hoist_declarations(pCtx, pNode->c, pFrame, 0);
            return;

        case N_FOR:
            hoist_declarations(pCtx, pNode->a, pFrame, 0);
            hoist_declarations(pCtx, pNode->d, pFrame, 0);
            return;

        case N_FORIN:
            hoist_declarations(pCtx, pNode->a, pFrame, 0);
            hoist_declarations(pCtx, pNode->d, pFrame, 0);
            return;

        case N_WHILE:
        case N_DOWHILE: hoist_declarations(pCtx, pNode->d, pFrame, 0); return;

        case N_TRY:
            hoist_declarations(pCtx, pNode->a, pFrame, 0);
            hoist_declarations(pCtx, pNode->b, pFrame, 0);
            hoist_declarations(pCtx, pNode->c, pFrame, 0);
            return;

        case N_LABELED: hoist_declarations(pCtx, pNode->a, pFrame, 0); return;

        case N_EXPRSTMT: (void)bTopLevel; return;

        default: return;
    }
}

/* ------------------------------------------------------------- operators  */

static double js_mod(double a, double b)
{
    if ((b == 0) || (a != a) || (b != b)) {
        return xx_rt_nan();
    }

    return xx_rt_fmod(a, b);
}

static int compare_values(JSCtx *pCtx, JSVal left, JSVal right, JSOp op, int *pbUndefined)
{
    JSVal primLeft = js_to_primitive(pCtx, left, 0);
    JSVal primRight;
    int bResult = 0;

    *pbUndefined = 0;
    if (pCtx->bException) {
        /* The right operand's valueOf must not run once the left one threw. */
        js_release(pCtx, primLeft);
        *pbUndefined = 1;
        return 0;
    }
    primRight = js_to_primitive(pCtx, right, 0);

    if ((primLeft.tag == JT_STR) && (primRight.tag == JT_STR)) {
        size_t nSizeA = primLeft.u.s->nSize;
        size_t nSizeB = primRight.u.s->nSize;
        size_t nMin = (nSizeA < nSizeB) ? nSizeA : nSizeB;
        int nCmp = xx_rt_memcmp(primLeft.u.s->pData, primRight.u.s->pData, nMin);

        if (nCmp == 0) {
            nCmp = (nSizeA < nSizeB) ? -1 : ((nSizeA > nSizeB) ? 1 : 0);
        }

        switch (op) {
            case OP_LT: bResult = (nCmp < 0); break;
            case OP_GT: bResult = (nCmp > 0); break;
            case OP_LE: bResult = (nCmp <= 0); break;
            case OP_GE: bResult = (nCmp >= 0); break;
            default: break;
        }
    } else {
        double a = js_to_number(pCtx, primLeft);
        double b = js_to_number(pCtx, primRight);

        if ((a != a) || (b != b)) {
            *pbUndefined = 1;
        } else {
            switch (op) {
                case OP_LT: bResult = (a < b); break;
                case OP_GT: bResult = (a > b); break;
                case OP_LE: bResult = (a <= b); break;
                case OP_GE: bResult = (a >= b); break;
                default: break;
            }
        }
    }

    js_release(pCtx, primLeft);
    js_release(pCtx, primRight);

    return bResult;
}

static JSVal apply_binary(JSCtx *pCtx, JSOp op, JSVal left, JSVal right)
{
    switch (op) {
        case OP_ADD: {
            JSVal primLeft = js_to_primitive(pCtx, left, 0);
            JSVal primRight;
            JSVal result;

            /* The right operand's valueOf must not run once the left one threw. */
            if (pCtx->bException) {
                js_release(pCtx, primLeft);
                return js_undefined();
            }
            primRight = js_to_primitive(pCtx, right, 0);

            if ((primLeft.tag == JT_STR) || (primRight.tag == JT_STR)) {
                result = js_concat_str(pCtx, primLeft, primRight);
            } else {
                result = js_num(js_to_number(pCtx, primLeft) + js_to_number(pCtx, primRight));
            }

            js_release(pCtx, primLeft);
            js_release(pCtx, primRight);

            return result;
        }

        case OP_SUB: return js_num(js_to_number(pCtx, left) - js_to_number(pCtx, right));
        case OP_MUL: return js_num(js_to_number(pCtx, left) * js_to_number(pCtx, right));
        case OP_DIV: return js_num(js_to_number(pCtx, left) / js_to_number(pCtx, right));
        case OP_MOD: return js_num(js_mod(js_to_number(pCtx, left), js_to_number(pCtx, right)));

        case OP_AND: return js_num((double)(js_to_int32(pCtx, left) & js_to_int32(pCtx, right)));
        case OP_OR: return js_num((double)(js_to_int32(pCtx, left) | js_to_int32(pCtx, right)));
        case OP_XOR: return js_num((double)(js_to_int32(pCtx, left) ^ js_to_int32(pCtx, right)));
        /* Shifted as unsigned: a signed left shift of a negative value, or one
           that overflows, is undefined behaviour in C99. */
        case OP_SHL: return js_num((double)(int32_t)(((uint32_t)js_to_int32(pCtx, left)) << (js_to_int32(pCtx, right) & 31)));
        case OP_SHR: return js_num((double)(js_to_int32(pCtx, left) >> (js_to_int32(pCtx, right) & 31)));
        case OP_USHR: return js_num((double)(((uint32_t)js_to_int32(pCtx, left)) >> (js_to_int32(pCtx, right) & 31)));

        case OP_EQ: return js_bool(js_loose_equals(pCtx, left, right));
        case OP_NE: return js_bool(!js_loose_equals(pCtx, left, right));
        case OP_SEQ: return js_bool(js_strict_equals(pCtx, left, right));
        case OP_SNE: return js_bool(!js_strict_equals(pCtx, left, right));

        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE: {
            int bUndefined = 0;
            int bResult = compare_values(pCtx, left, right, op, &bUndefined);

            return js_bool(bUndefined ? 0 : bResult);
        }

        case OP_IN: {
            char sScratch[KEY_SCRATCH_SIZE];
            char *pKey = NULL;
            size_t nKeySize = 0;
            int bResult = 0;

            if (right.tag != JT_OBJ) {
                return js_throw(pCtx, "TypeError: 'in' requires an object");
            }

            pKey = key_from_value(pCtx, left, &nKeySize, sScratch);
            bResult = jsobj_has(pCtx, right.u.o, pKey, nKeySize);
            key_release(pKey, sScratch, left);

            return js_bool(bResult);
        }

        case OP_INSTANCEOF: {
            JSVal prototype;
            JSObj *pCurrent = NULL;

            if (!js_is_callable(right)) {
                return js_throw(pCtx, "TypeError: 'instanceof' requires a function");
            }

            if (left.tag != JT_OBJ) {
                return js_bool(0);
            }

            prototype = js_get(pCtx, right, "prototype");
            pCurrent = left.u.o->pProto;

            while (pCurrent) {
                if ((prototype.tag == JT_OBJ) && (pCurrent == prototype.u.o)) {
                    js_release(pCtx, prototype);

                    return js_bool(1);
                }

                pCurrent = pCurrent->pProto;
            }

            js_release(pCtx, prototype);

            return js_bool(0);
        }

        default: break;
    }

    return js_undefined();
}

static const char *typeof_string(JSVal value)
{
    switch (value.tag) {
        case JT_UNDEF: return "undefined";
        case JT_NULL: return "object";
        case JT_BOOL: return "boolean";
        case JT_NUM: return "number";
        case JT_STR: return "string";
        case JT_OBJ: return js_is_callable(value) ? "function" : "object";
        default: break;
    }

    return "undefined";
}

/* ------------------------------------------------------------ expression  */

/* A resolved assignment target. See ref_resolve below. The optional call
 * hint lets a native concat retain the ordinary method/argument evaluation
 * order while proving that an assignment still owns its string receiver. */
typedef struct {
    JSNode *pTarget;
    JSVal base;
    JSVal key;
} JSRef;

static int ref_owns_string(JSCtx *pCtx, JSRef *pRef, JSFrame *pFrame, JSVal current);

static JSVal eval_call(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, JSRef *pAppendRef)
{
    JSVal thisVal = js_undefined();
    JSVal fn = js_undefined();
    JSVal localArgs[JS_LOCAL_ARGS];
    JSVal *pArgv = pNode->nList ? localArgs : NULL;
    JSVal result = js_undefined();
    size_t i = 0;
    JSNode *pCallee = pNode->a;

    if (pCallee->type == N_MEMBER) {
        thisVal = eval_expr(pCtx, pCallee->a, pFrame);

        if (pCtx->bException) {
            return js_undefined();
        }

        fn = get_property(pCtx, thisVal, pCallee->pStr, xx_rt_strlen(pCallee->pStr));
    } else if (pCallee->type == N_INDEX) {
        JSVal key;

        thisVal = eval_expr(pCtx, pCallee->a, pFrame);

        if (pCtx->bException) {
            return js_undefined();
        }

        key = eval_expr(pCtx, pCallee->b, pFrame);

        if (pCtx->bException) {
            js_release(pCtx, thisVal);
            js_release(pCtx, key);

            return js_undefined();
        }

        fn = get_indexed_property(pCtx, thisVal, key);
        js_release(pCtx, key);
    } else {
        fn = eval_expr(pCtx, pCallee, pFrame);
    }

    if (pCtx->bException) {
        js_release(pCtx, thisVal);
        js_release(pCtx, fn);

        return js_undefined();
    }

    if (!js_is_callable(fn)) {
        const char *pName = "expression";

        if (pCallee->type == N_MEMBER) {
            pName = pCallee->pStr;
        } else if (pCallee->type == N_IDENT) {
            pName = pCallee->pStr;
        }

        js_release(pCtx, thisVal);
        js_release(pCtx, fn);

        return js_throw(pCtx, "TypeError: %s is not a function (line %d)", pName, pNode->nLine);
    }

    if (pNode->nList > JS_LOCAL_ARGS) {
        pArgv = (JSVal *)xx_js_malloc(pNode->nList * sizeof(JSVal));
    }

    for (i = 0; i < pNode->nList; i++) {
        pArgv[i] = eval_expr(pCtx, pNode->ppList[i], pFrame);

        if (pCtx->bException) {
            size_t j = 0;

            for (j = 0; j <= i; j++) {
                js_release(pCtx, pArgv[j]);
            }

            if (pArgv != localArgs) {
                xx_js_free(pArgv);
            }
            js_release(pCtx, thisVal);
            js_release(pCtx, fn);

            return js_undefined();
        }
    }

    if (pAppendRef && pNode->nList == 1 && thisVal.tag == JT_STR && pArgv[0].tag == JT_STR &&
        js_is_string_concat(fn.u.o) && pCtx->nCallDepth < pCtx->nMaxCallDepth &&
        ref_owns_string(pCtx, pAppendRef, pFrame, thisVal)) {
        result = js_concat_str_assign(pCtx, thisVal, pArgv[0]);
    }
    if (result.tag == JT_UNDEF) result = js_call_function(pCtx, fn.u.o, thisVal, (int)pNode->nList, pArgv, 0);

    for (i = 0; i < pNode->nList; i++) {
        js_release(pCtx, pArgv[i]);
    }

    if (pArgv != localArgs) {
        xx_js_free(pArgv);
    }
    js_release(pCtx, thisVal);
    js_release(pCtx, fn);

    return result;
}

static JSVal eval_new(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSVal fn = eval_expr(pCtx, pNode->a, pFrame);
    JSVal localArgs[JS_LOCAL_ARGS];
    JSVal *pArgv = pNode->nList ? localArgs : NULL;
    JSVal result = js_undefined();
    size_t i = 0;

    if (pCtx->bException) {
        js_release(pCtx, fn);

        return js_undefined();
    }

    if (pNode->nList > JS_LOCAL_ARGS) {
        pArgv = (JSVal *)xx_js_malloc(pNode->nList * sizeof(JSVal));
    }

    for (i = 0; i < pNode->nList; i++) {
        pArgv[i] = eval_expr(pCtx, pNode->ppList[i], pFrame);
    }

    if (!pCtx->bException) {
        result = js_construct(pCtx, fn, (int)pNode->nList, pArgv);
    }

    for (i = 0; i < pNode->nList; i++) {
        js_release(pCtx, pArgv[i]);
    }

    if (pArgv != localArgs) {
        xx_js_free(pArgv);
    }
    js_release(pCtx, fn);

    return result;
}

/* A resolved assignment target. ECMAScript evaluates the left hand side to a
   reference once, before the right hand side runs, and reuses that reference
   for both the read and the store.

   For an indexed target the reference keeps the key *value*, not the converted
   key string: ToPropertyKey is applied again by every read and every store, so
   `o[k] += 1` calls a side effecting k.toString() twice, as the reference
   engine does. Only the key expression itself is evaluated once. */
static int ref_resolve(JSCtx *pCtx, JSNode *pTarget, JSFrame *pFrame, JSRef *pRef)
{
    pRef->pTarget = pTarget;
    pRef->base = js_undefined();
    pRef->key = js_undefined();

    if ((pTarget->type == N_MEMBER) || (pTarget->type == N_INDEX)) {
        pRef->base = eval_expr(pCtx, pTarget->a, pFrame);

        if (pCtx->bException) {
            return 0;
        }
    }

    if (pTarget->type == N_INDEX) {
        pRef->key = eval_expr(pCtx, pTarget->b, pFrame);

        if (pCtx->bException) {
            return 0;
        }
    }

    return 1;
}

static void ref_free(JSCtx *pCtx, JSRef *pRef)
{
    js_release(pCtx, pRef->key);
    js_release(pCtx, pRef->base);
}

static JSVal ref_get(JSCtx *pCtx, JSRef *pRef, JSFrame *pFrame)
{
    JSNode *pTarget = pRef->pTarget;

    if (pTarget->type == N_IDENT) {
        int bFound = 0;
        JSVal value = scope_get_node(pCtx, pFrame->pScope, pTarget, &bFound);

        if (!bFound) {
            js_release(pCtx, value);

            return js_throw(pCtx, "ReferenceError: %s is not defined (line %d)", pTarget->pStr, pTarget->nLine);
        }

        return value;
    }

    if (pTarget->type == N_MEMBER) {
        return get_property(pCtx, pRef->base, pTarget->pStr, xx_rt_strlen(pTarget->pStr));
    }

    if (pTarget->type == N_INDEX) {
        return get_indexed_property(pCtx, pRef->base, pRef->key);
    }

    return eval_expr(pCtx, pTarget, pFrame);
}

static void ref_put(JSCtx *pCtx, JSRef *pRef, JSFrame *pFrame, JSVal value)
{
    JSNode *pTarget = pRef->pTarget;

    if (pTarget->type == N_IDENT) {
        scope_set_node(pCtx, pFrame->pScope, pTarget, value);
    } else if (pTarget->type == N_MEMBER) {
        set_property(pCtx, pRef->base, pTarget->pStr, xx_rt_strlen(pTarget->pStr), value);
    } else if (pTarget->type == N_INDEX) {
        char sScratch[KEY_SCRATCH_SIZE];
        size_t nKeySize = 0;
        char *pKey = key_from_value(pCtx, pRef->key, &nKeySize, sScratch);

        set_property(pCtx, pRef->base, pKey, nKeySize, value);
        key_release(pKey, sScratch, pRef->key);
    } else {
        js_release(pCtx, value);
    }
}

/* A primitive string can be extended only while its original assignment
 * slot still owns it. Never convert an object key here: conversion belongs
 * to ref_get/ref_put and can execute arbitrary JavaScript. */
static int ref_owns_string(JSCtx *pCtx, JSRef *pRef, JSFrame *pFrame, JSVal current)
{
    JSNode *pTarget = pRef->pTarget;
    JSProp *pProp = NULL;
    if (current.tag != JT_STR || current.u.s->nRef != 2) return 0;
    if (pTarget->type == N_IDENT) {
        JSObj *pHolder = NULL;
        pProp = scope_find_prop(pCtx, pFrame->pScope, pTarget->pStr, pTarget->nStrSize, pTarget->nStrHash, &pHolder);
    } else if (pRef->base.tag == JT_OBJ && pTarget->type == N_MEMBER) {
        pProp = jsprops_find(&pRef->base.u.o->props, pTarget->pStr, xx_rt_strlen(pTarget->pStr));
    } else if (pRef->base.tag == JT_OBJ && pTarget->type == N_INDEX && (pRef->key.tag == JT_STR || pRef->key.tag == JT_NUM)) {
        char sScratch[KEY_SCRATCH_SIZE];
        size_t nKeySize;
        char *pKey = key_from_value(pCtx, pRef->key, &nKeySize, sScratch);
        pProp = jsprops_find(&pRef->base.u.o->props, pKey, nKeySize);
        key_release(pKey, sScratch, pRef->key);
    }
    return pProp && pProp->value.tag == JT_STR && pProp->value.u.s == current.u.s;
}

static JSVal eval_add_assignment(JSCtx *pCtx, JSNode *pBinary, JSFrame *pFrame, JSRef *pRef)
{
    JSVal left, right = js_undefined(), result = js_undefined();
    /* This replaces eval_expr(binary), so retain its nesting budget exactly. */
    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) return js_throw(pCtx, "RangeError: maximum expression nesting exceeded");
    pCtx->nEvalDepth++;
    left = eval_expr(pCtx, pBinary->a, pFrame);
    if (!pCtx->bException) right = eval_expr(pCtx, pBinary->b, pFrame);
    if (!pCtx->bException) {
        if (right.tag == JT_STR && ref_owns_string(pCtx, pRef, pFrame, left)) result = js_concat_str_assign(pCtx, left, right);
        if (result.tag == JT_UNDEF) result = apply_binary(pCtx, OP_ADD, left, right);
    }
    js_release(pCtx, left);
    js_release(pCtx, right);
    pCtx->nEvalDepth--;
    return result;
}

static JSVal eval_concat_assignment(JSCtx *pCtx, JSNode *pCall, JSFrame *pFrame, JSRef *pRef)
{
    JSVal result;
    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) return js_throw(pCtx, "RangeError: maximum expression nesting exceeded");
    pCtx->nEvalDepth++;
    result = eval_call(pCtx, pCall, pFrame, pRef);
    pCtx->nEvalDepth--;
    return result;
}

static JSVal eval_assign(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSVal value = js_undefined();
    JSRef ref;

    if (!ref_resolve(pCtx, pNode->a, pFrame, &ref)) {
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    if (pNode->op == OP_ASSIGN) {
        JSNode *pBinary = pNode->b;
        if (pNode->a->type == N_IDENT && pBinary->type == N_BINARY && pBinary->op == OP_ADD &&
            pBinary->a->type == N_IDENT && !xx_rt_strcmp(pNode->a->pStr, pBinary->a->pStr)) {
            value = eval_add_assignment(pCtx, pBinary, pFrame, &ref);
        } else if (pNode->a->type == N_IDENT && pBinary->type == N_CALL && pBinary->nList == 1 &&
                   pBinary->a->type == N_MEMBER && !xx_rt_strcmp(pBinary->a->pStr, "concat") &&
                   pBinary->a->a->type == N_IDENT && !xx_rt_strcmp(pNode->a->pStr, pBinary->a->a->pStr)) {
            value = eval_concat_assignment(pCtx, pBinary, pFrame, &ref);
        } else {
            value = eval_expr(pCtx, pBinary, pFrame);
        }
    } else {
        JSVal current = ref_get(pCtx, &ref, pFrame);
        JSVal operand;

        if (pCtx->bException) {
            js_release(pCtx, current);
            ref_free(pCtx, &ref);

            return js_undefined();
        }

        operand = eval_expr(pCtx, pNode->b, pFrame);

        if (pCtx->bException) {
            js_release(pCtx, current);
            js_release(pCtx, operand);
            ref_free(pCtx, &ref);

            return js_undefined();
        }

        /* A callback may reassign the slot or retain the old string. Own
         * property checks also exclude strings inherited from prototypes. */
        if (pNode->op == OP_ADD && operand.tag == JT_STR && ref_owns_string(pCtx, &ref, pFrame, current)) {
            value = js_concat_str_assign(pCtx, current, operand);
        }

        if (value.tag == JT_UNDEF) {
            value = apply_binary(pCtx, pNode->op, current, operand);
        }
        js_release(pCtx, current);
        js_release(pCtx, operand);
    }

    if (pCtx->bException) {
        js_release(pCtx, value);
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    ref_put(pCtx, &ref, pFrame, js_dup(value));
    ref_free(pCtx, &ref);

    return value;
}

static JSVal eval_update(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSRef ref;
    JSVal current;
    double nOld = 0;
    double nNew = 0;
    JSVal newValue;

    if (!ref_resolve(pCtx, pNode->a, pFrame, &ref)) {
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    current = ref_get(pCtx, &ref, pFrame);

    if (pCtx->bException) {
        js_release(pCtx, current);
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    nOld = js_to_number(pCtx, current);
    js_release(pCtx, current);
    nNew = (pNode->op == OP_INC) ? (nOld + 1) : (nOld - 1);
    newValue = js_num(nNew);

    ref_put(pCtx, &ref, pFrame, js_dup(newValue));
    ref_free(pCtx, &ref);

    if (pNode->nNum != 0) {
        return newValue;
    }

    js_release(pCtx, newValue);

    return js_num(nOld);
}

static JSVal eval_unary(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    if (pNode->op == OP_TYPEOF) {
        JSVal value;

        if (pNode->a->type == N_IDENT) {
            int bFound = 0;

            value = scope_get_node(pCtx, pFrame->pScope, pNode->a, &bFound);

            if (!bFound) {
                return js_str(pCtx, "undefined");
            }
        } else {
            value = eval_expr(pCtx, pNode->a, pFrame);

            if (pCtx->bException) {
                js_release(pCtx, value);

                return js_undefined();
            }
        }

        {
            JSVal result = js_str(pCtx, typeof_string(value));

            js_release(pCtx, value);

            return result;
        }
    }

    if (pNode->op == OP_DELETE) {
        JSNode *pTarget = pNode->a;

        if (pTarget->type == N_MEMBER) {
            JSVal base = eval_expr(pCtx, pTarget->a, pFrame);

            if ((!pCtx->bException) && (base.tag == JT_OBJ)) {
                jsobj_del(pCtx, base.u.o, pTarget->pStr);
            }

            js_release(pCtx, base);

            /* Sloppy-mode delete reports success even for a property that was
               not there, which is what the index form below does as well. */
            return js_bool(1);
        }

        if (pTarget->type == N_INDEX) {
            JSVal base = eval_expr(pCtx, pTarget->a, pFrame);
            JSVal key = js_undefined();

            if (!pCtx->bException) {
                key = eval_expr(pCtx, pTarget->b, pFrame);
            }

            if ((!pCtx->bException) && (base.tag == JT_OBJ)) {
                char sScratch[KEY_SCRATCH_SIZE];
                char *pKey = key_from_value(pCtx, key, NULL, sScratch);

                jsobj_del(pCtx, base.u.o, pKey);
                key_release(pKey, sScratch, key);
            }

            js_release(pCtx, key);
            js_release(pCtx, base);

            return js_bool(1);
        }

        return js_bool(1);
    }

    {
        JSVal value = eval_expr(pCtx, pNode->a, pFrame);
        JSVal result = js_undefined();

        if (pCtx->bException) {
            js_release(pCtx, value);

            return js_undefined();
        }

        switch (pNode->op) {
            case OP_NOT: result = js_bool(!js_to_bool(pCtx, value)); break;
            case OP_BNOT: result = js_num((double)(~js_to_int32(pCtx, value))); break;
            case OP_NEG: result = js_num(-js_to_number(pCtx, value)); break;
            case OP_POS: result = js_num(js_to_number(pCtx, value)); break;
            case OP_VOID: result = js_undefined(); break;
            default: break;
        }

        js_release(pCtx, value);

        return result;
    }
}

static JSVal eval_expr_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame);

static JSVal eval_expr(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSVal result;

    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) {
        return js_throw(pCtx, "RangeError: maximum expression nesting exceeded");
    }

    pCtx->nEvalDepth++;
    result = eval_expr_node(pCtx, pNode, pFrame);
    pCtx->nEvalDepth--;

    return result;
}

static JSVal eval_expr_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    if ((pNode == NULL) || pCtx->bException) {
        return js_undefined();
    }

    switch (pNode->type) {
        case N_NUM: return js_num(pNode->nNum);
        case N_STR: return js_strn(pCtx, pNode->pStr, (size_t)pNode->nNum);
        case N_NULL: return js_null();
        case N_BOOL: return js_bool(pNode->nNum != 0);
        case N_THIS: return js_dup(pFrame->thisVal);
        case N_EMPTY: return js_undefined();

        case N_REGEXP:
            return pNode->pRegExpLiteral ? js_new_regexp_shared_val(pCtx, pNode->pRegExpLiteral) : js_new_regexp_val(pCtx, pNode->pStr, pNode->pStr2);

        case N_IDENT: {
            int bFound = 0;
            JSVal value = scope_get_node(pCtx, pFrame->pScope, pNode, &bFound);

            if (!bFound) {
                js_release(pCtx, value);

                return js_throw(pCtx, "ReferenceError: %s is not defined (line %d)", pNode->pStr, pNode->nLine);
            }

            return value;
        }

        case N_ARRAY: {
            JSVal array = js_new_array(pCtx);
            size_t i = 0;

            for (i = 0; i < pNode->nList; i++) {
                /* An elision is a missing property, not an own undefined
                 * value: prototype lookup and enumeration can observe it. */
                if (!pNode->ppList[i] || pNode->ppList[i]->type == N_EMPTY) continue;
                JSVal item = eval_expr(pCtx, pNode->ppList[i], pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, item);
                    js_release(pCtx, array);

                    return js_undefined();
                }

                js_set_index(pCtx, array, (int64_t)i, item);
            }

            array.u.o->nArrayLen = (int64_t)pNode->nList;

            return array;
        }

        case N_OBJECT: {
            JSVal object = js_new_object(pCtx);
            size_t i = 0;

            for (i = 0; i < pNode->nList; i++) {
                JSNode *pProp = pNode->ppList[i];
                JSVal value = eval_expr(pCtx, pProp->a, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, value);
                    js_release(pCtx, object);

                    return js_undefined();
                }

                /* The parser stores the key's byte count in nNum, as N_STR does. */
                jsobj_put(pCtx, object.u.o, pProp->pStr, (size_t)pProp->nNum, value);
            }

            return object;
        }

        case N_FUNCTION: return make_function(pCtx, pNode, pFrame);

        case N_MEMBER: {
            JSVal base = eval_expr(pCtx, pNode->a, pFrame);
            JSVal result;

            if (pCtx->bException) {
                js_release(pCtx, base);

                return js_undefined();
            }

            result = get_property(pCtx, base, pNode->pStr, xx_rt_strlen(pNode->pStr));
            js_release(pCtx, base);

            return result;
        }

        case N_INDEX: {
            JSVal base = eval_expr(pCtx, pNode->a, pFrame);
            JSVal key;
            JSVal result;

            if (pCtx->bException) {
                js_release(pCtx, base);

                return js_undefined();
            }

            key = eval_expr(pCtx, pNode->b, pFrame);

            if (pCtx->bException) {
                js_release(pCtx, base);
                js_release(pCtx, key);

                return js_undefined();
            }

            result = get_indexed_property(pCtx, base, key);
            js_release(pCtx, key);
            js_release(pCtx, base);

            return result;
        }

        case N_CALL: return eval_call(pCtx, pNode, pFrame, NULL);
        case N_NEW: return eval_new(pCtx, pNode, pFrame);
        case N_ASSIGN: return eval_assign(pCtx, pNode, pFrame);
        case N_UPDATE: return eval_update(pCtx, pNode, pFrame);
        case N_UNARY: return eval_unary(pCtx, pNode, pFrame);

        case N_BINARY: {
            JSVal left = eval_expr(pCtx, pNode->a, pFrame);
            JSVal right;
            JSVal result;

            if (pCtx->bException) {
                js_release(pCtx, left);

                return js_undefined();
            }

            right = eval_expr(pCtx, pNode->b, pFrame);

            if (pCtx->bException) {
                js_release(pCtx, left);
                js_release(pCtx, right);

                return js_undefined();
            }

            result = apply_binary(pCtx, pNode->op, left, right);
            js_release(pCtx, left);
            js_release(pCtx, right);

            return result;
        }

        case N_LOGICAL: {
            JSVal left = eval_expr(pCtx, pNode->a, pFrame);
            int bLeft = 0;

            if (pCtx->bException) {
                js_release(pCtx, left);

                return js_undefined();
            }

            bLeft = js_to_bool(pCtx, left);

            if (((pNode->op == OP_LAND) && (!bLeft)) || ((pNode->op == OP_LOR) && bLeft)) {
                return left;
            }

            js_release(pCtx, left);

            return eval_expr(pCtx, pNode->b, pFrame);
        }

        case N_COND: {
            JSVal test = eval_expr(pCtx, pNode->a, pFrame);
            int bTest = 0;

            if (pCtx->bException) {
                js_release(pCtx, test);

                return js_undefined();
            }

            bTest = js_to_bool(pCtx, test);
            js_release(pCtx, test);

            return eval_expr(pCtx, bTest ? pNode->b : pNode->c, pFrame);
        }

        case N_SEQ: {
            JSVal left = eval_expr(pCtx, pNode->a, pFrame);

            js_release(pCtx, left);

            return eval_expr(pCtx, pNode->b, pFrame);
        }

        default: break;
    }

    return js_undefined();
}

/* ------------------------------------------------------------ statements  */

static JSCompletion exec_block_list(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    size_t i = 0;

    for (i = 0; i < pNode->nList; i++) {
        JSCompletion completion = exec_stmt(pCtx, pNode->ppList[i], pFrame);

        if (pCtx->bException) {
            js_release(pCtx, completion.value);

            return completion_normal();
        }

        if (completion.type != CT_NORMAL) {
            return completion;
        }

        js_release(pCtx, completion.value);
    }

    return completion_normal();
}

static int loop_break(JSCompletion *pCompletion, const char *pLabel, JSCompletion *pOut)
{
    if (pCompletion->type == CT_BREAK) {
        if ((pCompletion->pLabel == NULL) || (pLabel && (xx_rt_strcmp(pCompletion->pLabel, pLabel) == 0))) {
            *pOut = completion_normal();

            return 1;
        }

        *pOut = *pCompletion;

        return 1;
    }

    if (pCompletion->type == CT_RETURN) {
        *pOut = *pCompletion;

        return 1;
    }

    if (pCompletion->type == CT_CONTINUE) {
        if ((pCompletion->pLabel != NULL) && (!(pLabel && (xx_rt_strcmp(pCompletion->pLabel, pLabel) == 0)))) {
            *pOut = *pCompletion;

            return 1;
        }
    }

    return 0;
}

static JSCompletion exec_stmt_labeled(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel);

static JSCompletion exec_stmt(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    return exec_stmt_labeled(pCtx, pNode, pFrame, NULL);
}

static JSCompletion exec_stmt_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel);

static JSCompletion exec_stmt_labeled(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel)
{
    JSCompletion completion;

    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) {
        js_throw(pCtx, "RangeError: maximum expression nesting exceeded");

        return completion_normal();
    }

    if (pCtx->bDebugAborted) return completion_normal();
    if (pNode && pNode->type != N_PROGRAM && pNode->type != N_BLOCK &&
        pNode->type != N_FUNCTION && pNode->type != N_EMPTY && pNode->type != N_CASE &&
        !pCtx->bException && !debug_checkpoint(pCtx, pNode, pFrame)) return completion_normal();
    pCtx->nEvalDepth++;
    completion = exec_stmt_node(pCtx, pNode, pFrame, pLabel);
    pCtx->nEvalDepth--;

    return completion;
}

static JSCompletion exec_stmt_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel)
{
    JSCompletion completion = completion_normal();

    if ((pNode == NULL) || pCtx->bException) {
        return completion;
    }

    switch (pNode->type) {
        case N_PROGRAM:
        case N_BLOCK: return exec_block_list(pCtx, pNode, pFrame);

        case N_EMPTY:
        case N_DEBUGGER: return completion;

        case N_FUNCTION:
            /* Handled by hoisting. */
            return completion;

        case N_EXPRSTMT: {
            JSVal value = eval_expr(pCtx, pNode->a, pFrame);

            js_release(pCtx, value);

            return completion;
        }

        case N_VAR: {
            size_t i = 0;

            for (i = 0; i < pNode->nList; i++) {
                JSNode *pDecl = pNode->ppList[i];

                if (pDecl->a) {
                    JSVal value = eval_expr(pCtx, pDecl->a, pFrame);

                    if (pCtx->bException) {
                        js_release(pCtx, value);

                        return completion;
                    }

                    scope_set_node(pCtx, pFrame->pScope, pDecl, value);
                } else {
                    JSObj *pVars = pFrame->pScope ? pFrame->pScope->pVars : pCtx->pGlobal;

                    if (jsprops_find(&pVars->props, pDecl->pStr, xx_rt_strlen(pDecl->pStr)) == NULL) {
                        scope_declare(pCtx, pFrame->pScope, pDecl->pStr, js_undefined());
                    }
                }
            }

            return completion;
        }

        case N_IF: {
            JSNode *branch = pNode;

            /* An else-if chain is represented as nested N_IF nodes. Walk
             * the false branches without consuming one C stack frame per
             * clause; bundled detection scripts can have hundreds of them. */
            while (branch && branch->type == N_IF) {
                JSVal test;
                if (branch != pNode && !debug_checkpoint(pCtx, branch, pFrame)) return completion;
                test = eval_expr(pCtx, branch->a, pFrame);
                int bTest;

                if (pCtx->bException) {
                    js_release(pCtx, test);
                    return completion;
                }

                bTest = js_to_bool(pCtx, test);
                js_release(pCtx, test);
                if (bTest) return exec_stmt(pCtx, branch->b, pFrame);
                branch = branch->c;
            }
            return branch ? exec_stmt(pCtx, branch, pFrame) : completion;
        }

        case N_WHILE: {
            int bFirst = 1;
            for (;;) {
                JSVal test;
                if (!bFirst && !debug_checkpoint(pCtx, pNode, pFrame)) return completion_normal();
                bFirst = 0;
                test = eval_expr(pCtx, pNode->a, pFrame);
                int bTest = 0;
                JSCompletion body;
                JSCompletion out;

                if (pCtx->bException) {
                    js_release(pCtx, test);

                    return completion_normal();
                }

                bTest = js_to_bool(pCtx, test);
                js_release(pCtx, test);

                if (!bTest) {
                    break;
                }

                body = exec_stmt(pCtx, pNode->d, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (loop_break(&body, pLabel, &out)) {
                    return out;
                }

                js_release(pCtx, body.value);
            }

            return completion_normal();
        }

        case N_DOWHILE: {
            int bFirst = 1;
            for (;;) {
                JSCompletion body;
                if (!bFirst && !debug_checkpoint(pCtx, pNode, pFrame)) return completion_normal();
                bFirst = 0;
                body = exec_stmt(pCtx, pNode->d, pFrame);
                JSCompletion out;
                JSVal test;
                int bTest = 0;

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (loop_break(&body, pLabel, &out)) {
                    return out;
                }

                js_release(pCtx, body.value);

                if (!debug_checkpoint(pCtx, pNode, pFrame)) return completion_normal();
                test = eval_expr(pCtx, pNode->a, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, test);

                    return completion_normal();
                }

                bTest = js_to_bool(pCtx, test);
                js_release(pCtx, test);

                if (!bTest) {
                    break;
                }
            }

            return completion_normal();
        }

        case N_FOR: {
            int bFirst = 1;
            if (pNode->a) {
                JSCompletion init = exec_stmt(pCtx, pNode->a, pFrame);

                js_release(pCtx, init.value);

                if (pCtx->bException) {
                    return completion_normal();
                }
            }

            for (;;) {
                JSCompletion body;
                JSCompletion out;

                if (!bFirst && !debug_checkpoint(pCtx, pNode, pFrame)) return completion_normal();
                bFirst = 0;
                if (pNode->b) {
                    JSVal test = eval_expr(pCtx, pNode->b, pFrame);
                    int bTest = 0;

                    if (pCtx->bException) {
                        js_release(pCtx, test);

                        return completion_normal();
                    }

                    bTest = js_to_bool(pCtx, test);
                    js_release(pCtx, test);

                    if (!bTest) {
                        break;
                    }
                }

                body = exec_stmt(pCtx, pNode->d, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (loop_break(&body, pLabel, &out)) {
                    return out;
                }

                js_release(pCtx, body.value);

                if (pNode->c) {
                    JSVal update;
                    if (!debug_checkpoint(pCtx, pNode->c, pFrame)) return completion_normal();
                    update = eval_expr(pCtx, pNode->c, pFrame);

                    js_release(pCtx, update);

                    if (pCtx->bException) {
                        return completion_normal();
                    }
                }
            }

            return completion_normal();
        }

        case N_FORIN: {
            JSVal object = eval_expr(pCtx, pNode->b, pFrame);
            JSVal keys;
            JSNode *pVarDecl = NULL;
            JSNode *pTarget = NULL;
            int64_t i, nKeys;

            if (pCtx->bException) {
                js_release(pCtx, object);
                return completion_normal();
            }
            if (pNode->a->type == N_VAR) {
                pVarDecl = pNode->a->ppList[0];
            } else if (pNode->a->type == N_EXPRSTMT) {
                pTarget = pNode->a->a;
            }

            /* An owned snapshot preserves enumeration order while the body
             * inserts/deletes properties or promotes a dense array to a map. */
            keys = object.tag == JT_OBJ ? jsobj_keys(pCtx, object.u.o) : js_new_array(pCtx);
            if (object.tag == JT_STR) {
                size_t j;
                for (j = 0; j < object.u.s->nSize; j++) {
                    char sBuf[32];
                    xx_rt_snprintf(sBuf, sizeof(sBuf), "%llu", (unsigned long long)j);
                    js_set_index(pCtx, keys, (int64_t)j, js_str(pCtx, sBuf));
                }
            }
            nKeys = js_array_length(pCtx, keys);
            for (i = 0; i < nKeys; i++) {
                JSVal key = js_get_index(pCtx, keys, i);
                JSCompletion body;
                JSCompletion out;

                if (i && !debug_checkpoint(pCtx, pNode, pFrame)) { js_release(pCtx, key); break; }
                if (pVarDecl) {
                    scope_set_node(pCtx, pFrame->pScope, pVarDecl, js_dup(key));
                } else if (pTarget && pTarget->type == N_IDENT) {
                    scope_set_node(pCtx, pFrame->pScope, pTarget, js_dup(key));
                } else if (pTarget && pTarget->type == N_MEMBER) {
                    JSVal base = eval_expr(pCtx, pTarget->a, pFrame);
                    set_property(pCtx, base, pTarget->pStr, xx_rt_strlen(pTarget->pStr), js_dup(key));
                    js_release(pCtx, base);
                } else if (pTarget && pTarget->type == N_INDEX) {
                    /* for (a[i] in o): the computed target is re-evaluated per key. */
                    JSVal base = eval_expr(pCtx, pTarget->a, pFrame);
                    JSVal index = eval_expr(pCtx, pTarget->b, pFrame);
                    if (!pCtx->bException) {
                        char sScratch[KEY_SCRATCH_SIZE];
                        size_t nKeySize = 0;
                        char *pKey = key_from_value(pCtx, index, &nKeySize, sScratch);
                        set_property(pCtx, base, pKey, nKeySize, js_dup(key));
                        key_release(pKey, sScratch, index);
                    }
                    js_release(pCtx, index);
                    js_release(pCtx, base);
                }
                js_release(pCtx, key);
                body = exec_stmt(pCtx, pNode->d, pFrame);
                if (pCtx->bException) {
                    js_release(pCtx, body.value);
                    break;
                }
                if (loop_break(&body, pLabel, &out)) {
                    js_release(pCtx, keys);
                    js_release(pCtx, object);
                    return out;
                }
                js_release(pCtx, body.value);
            }
            js_release(pCtx, keys);
            js_release(pCtx, object);
            return completion_normal();
        }

        case N_RETURN: {
            JSCompletion result;

            result.type = CT_RETURN;
            result.pLabel = NULL;
            result.value = pNode->a ? eval_expr(pCtx, pNode->a, pFrame) : js_undefined();

            return result;
        }

        case N_BREAK: {
            JSCompletion result;

            result.type = CT_BREAK;
            result.pLabel = pNode->pStr;
            result.value = js_undefined();

            return result;
        }

        case N_CONTINUE: {
            JSCompletion result;

            result.type = CT_CONTINUE;
            result.pLabel = pNode->pStr;
            result.value = js_undefined();

            return result;
        }

        case N_THROW: {
            JSVal value = eval_expr(pCtx, pNode->a, pFrame);

            if (!pCtx->bException) {
                js_throw_value(pCtx, value);
            } else {
                js_release(pCtx, value);
            }

            return completion_normal();
        }

        case N_TRY: {
            JSCompletion result = exec_stmt(pCtx, pNode->a, pFrame);

            if (pCtx->bException && !pCtx->bDebugAborted && pNode->b) {
                JSVal exception = pCtx->exception;
                JSScope *pCatchScope = jsscope_new(pCtx, pFrame->pScope);
                JSFrame catchFrame = *pFrame;

                pCtx->bException = 0;
                pCtx->exception = js_undefined();
                xx_js_free(pCtx->pErrorText);
                pCtx->pErrorText = NULL;

                catchFrame.pScope = pCatchScope;

                if (pNode->pStr) {
                    scope_declare(pCtx, pCatchScope, pNode->pStr, exception);
                } else {
                    js_release(pCtx, exception);
                }

                js_release(pCtx, result.value);
                result = exec_stmt(pCtx, pNode->b, &catchFrame);
                jsscope_unref(pCtx, pCatchScope);
            }

            if (pNode->c && !pCtx->bDebugAborted) {
                int bSavedException = pCtx->bException;
                JSVal savedValue = pCtx->exception;
                char *pSavedText = pCtx->pErrorText;
                JSCompletion finallyResult;

                pCtx->bException = 0;
                pCtx->exception = js_undefined();
                pCtx->pErrorText = NULL;

                finallyResult = exec_stmt(pCtx, pNode->c, pFrame);

                if (pCtx->bException) {
                    /* An exception in `finally` replaces the pending one. */
                    if (bSavedException) {
                        js_release(pCtx, savedValue);
                        xx_js_free(pSavedText);
                    }

                    js_release(pCtx, result.value);
                    js_release(pCtx, finallyResult.value);

                    return completion_normal();
                }

                if (finallyResult.type != CT_NORMAL) {
                    /* An abrupt completion in `finally` replaces the try
                       completion, discarding the pending exception. */
                    if (bSavedException) {
                        js_release(pCtx, savedValue);
                        xx_js_free(pSavedText);
                    }

                    js_release(pCtx, result.value);

                    return finallyResult;
                }

                pCtx->bException = bSavedException;
                pCtx->exception = savedValue;
                pCtx->pErrorText = pSavedText;

                js_release(pCtx, finallyResult.value);
            }

            return result;
        }

        case N_SWITCH: {
            JSVal discriminant = eval_expr(pCtx, pNode->a, pFrame);
            size_t i = 0;
            size_t nStart = (size_t)-1;
            size_t nDefault = (size_t)-1;

            if (pCtx->bException) {
                js_release(pCtx, discriminant);

                return completion_normal();
            }

            for (i = 0; i < pNode->nList; i++) {
                JSNode *pCase = pNode->ppList[i];

                if (pCase->a == NULL) {
                    nDefault = i;
                    continue;
                }

                {
                    JSVal test = eval_expr(pCtx, pCase->a, pFrame);
                    int bMatch = 0;

                    if (pCtx->bException) {
                        js_release(pCtx, test);
                        js_release(pCtx, discriminant);

                        return completion_normal();
                    }

                    bMatch = js_strict_equals(pCtx, discriminant, test);
                    js_release(pCtx, test);

                    if (bMatch) {
                        nStart = i;
                        break;
                    }
                }
            }

            js_release(pCtx, discriminant);

            if (nStart == (size_t)-1) {
                nStart = nDefault;
            }

            if (nStart == (size_t)-1) {
                return completion_normal();
            }

            for (i = nStart; i < pNode->nList; i++) {
                JSCompletion body = exec_block_list(pCtx, pNode->ppList[i], pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (body.type == CT_BREAK) {
                    if (body.pLabel == NULL) {
                        js_release(pCtx, body.value);

                        return completion_normal();
                    }

                    return body;
                }

                if (body.type != CT_NORMAL) {
                    return body;
                }

                js_release(pCtx, body.value);
            }

            return completion_normal();
        }

        case N_LABELED: {
            JSCompletion body = exec_stmt_labeled(pCtx, pNode->a, pFrame, pNode->pStr);

            if ((body.type == CT_BREAK) && body.pLabel && (xx_rt_strcmp(body.pLabel, pNode->pStr) == 0)) {
                js_release(pCtx, body.value);

                return completion_normal();
            }

            return body;
        }

        default: {
            JSVal value = eval_expr(pCtx, pNode, pFrame);

            js_release(pCtx, value);

            return completion_normal();
        }
    }
}

/* ---------------------------------------------------------------- driver  */

JSVal js_run_program(JSCtx *pCtx, JSNode *pProgram, JSScope *pScope, JSVal thisVal)
{
    JSFrame frame;
    JSDebugFrameState debug;
    JSCompletion completion;

    frame.pScope = pScope;
    frame.thisVal = thisVal;
    frame.pFunction = NULL;
    frame.pDebug = &debug;
    debug_push(pCtx, &debug, pScope, NULL, thisVal, pProgram);

    hoist_prepared(pCtx, pProgram->pHoistPlan, pProgram, &frame);

    if (pCtx->bException || pCtx->bDebugAborted) {
        pCtx->pDebugFrame = debug.pPrevious;
        js_release(pCtx, thisVal);
        return js_undefined();
    }

    completion = exec_stmt(pCtx, pProgram, &frame);
    pCtx->pDebugFrame = debug.pPrevious;
    /* The caller hands over one reference to thisVal; nothing kept it. */
    js_release(pCtx, thisVal);

    if (completion.type == CT_RETURN) {
        return completion.value;
    }

    if (completion.type != CT_NORMAL && !pCtx->bException) {
        /* A break or continue outside any loop is a syntax error in the
         * reference engine; silently ending the program hid script typos. */
        js_throw(pCtx, "SyntaxError: illegal %s statement", (completion.type == CT_BREAK) ? "break" : "continue");
    }

    js_release(pCtx, completion.value);

    return js_undefined();
}
