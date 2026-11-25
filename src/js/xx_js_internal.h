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

/* js_internal.h - shared internals of the interpreter. */

#ifndef JS_INTERNAL_H
#define JS_INTERNAL_H

#include "xxfclib/js/xx_js.h"
#include "xxfclib/regex/xx_regex.h"
#include "xxfclib/js/xx_js_debug.h"
typedef struct JSDebugFrameState JSDebugFrameState;
#include "xx_js_deps.h"

/* --------------------------------------------------------------- strings  */

/* Every string, object and scope is linked into an engine-wide list so that
 * js_free can reclaim whatever reference counting could not (see the note
 * above jsobj_unref). The lists are doubly linked so that an allocation
 * whose count drops to zero can unlink itself in constant time.           */
struct JSStr {
    int32_t nRef;
    size_t nSize;
    size_t nCapacity;
    JSStr *pNextAll;
    JSStr *pPrevAll;
    char *pData;
};

JSStr *jsstr_new(JSCtx *pCtx, const char *pData, size_t nSize);
/* Takes a malloc-compatible buffer with room for a trailing zero. */
JSVal js_str_take(JSCtx *pCtx, char *pData, size_t nSize, size_t nCapacity);
JSStr *jsstr_ref(JSStr *pStr);
void jsstr_unref(JSCtx *pCtx, JSStr *pStr);

/* -------------------------------------------------------------- property  */

typedef struct {
    char *pKey;
    size_t nKeySize; /* xx_rt_strlen(pKey), cached: the probe loop is very hot */
    uint32_t nHash;
    JSVal value;
    uint8_t bDeleted;
    uint8_t bDontEnum;
    uint8_t bBorrowedKey;
} JSProp;

typedef struct {
    JSProp *pEntries;
    size_t nSize;
    size_t nCapacity;
    int32_t *pIndex; /* open addressing hash -> entry index, -1 = empty */
    size_t nIndexSize;
    size_t nLive;
} JSPropMap;

void jsprops_init(JSPropMap *pMap);
void jsprops_free(JSCtx *pCtx, JSPropMap *pMap);
JSProp *jsprops_find(JSPropMap *pMap, const char *pKey, size_t nKeySize);
/* Same lookup with a hash the caller already computed (prototype walks). */
JSProp *jsprops_find_hashed(JSPropMap *pMap, const char *pKey, size_t nKeySize, uint32_t nHash);
JSProp *jsprops_put(JSCtx *pCtx, JSPropMap *pMap, const char *pKey, size_t nKeySize);
/* Stores value in an entry, taking its reference and releasing the old one. */
void jsprop_store(JSCtx *pCtx, JSProp *pProp, JSVal value);
int jsprops_del(JSCtx *pCtx, JSPropMap *pMap, const char *pKey);

/* --------------------------------------------------------------- objects  */

typedef enum {
    JCLASS_OBJECT = 0,
    JCLASS_ARRAY,
    JCLASS_UINT32_ARRAY,
    JCLASS_FUNCTION,
    JCLASS_NATIVE,
    JCLASS_STRING,
    JCLASS_NUMBER,
    JCLASS_BOOLEAN,
    JCLASS_REGEXP,
    JCLASS_ERROR,
    JCLASS_ARGUMENTS,
    JCLASS_SCOPE
} JSClass;

typedef struct JSScope JSScope;
typedef xx_regex JSRegExp;

struct JSObj {
    int32_t nRef;
    int32_t nMapRefs; /* how many property maps hold this object; see jsprop_store */
    JSClass cls;
    JSObj *pProto;
    JSObj *pNextAll;
    JSObj *pPrevAll;
    JSPropMap props;
    uint8_t bExtensible;
    uint8_t bSweeping;
    uint8_t bArraySparse;
    uint8_t bBorrowedFnName;
    uint8_t bArrayMapOrder; /* named/sparse metadata established map ordering */
    uint8_t bGCState;       /* temporary cycle-collector candidate/mark bits */
    int32_t nGCRef;

    /* array */
    int64_t nArrayLen;
    uint32_t *pUint32Data;
    JSVal *pArrayData;
    size_t nArrayCapacity;
    size_t nArrayUsed;
    size_t nArrayStart; /* logical slot zero in pArrayData */
    size_t nArrayHoles;

    /* function (script) */
    JSNode *pFnNode;
    JSScope *pScope;
    char *pFnName;

    /* function (native) */
    JSNativeFn nativeFn;
    void *pUser;
    int nNativeArgc;

    /* bound function */
    JSObj *pBoundTarget;
    JSVal boundThis;
    JSVal *pBoundArgs;
    int nBoundArgs;

    /* primitive wrapper */
    JSVal primitive;

    /* regexp */
    JSRegExp *pRegExp;
};

struct JSScope {
    int32_t nRef;
    int32_t nGCRef;
    uint8_t bGCState;
    uint8_t bSweeping;
    JSObj *pVars;
    JSScope *pParent;
    JSScope *pNextAll;
    JSScope *pPrevAll;
};

/* ------------------------------------------------------------- context  */

typedef struct {
    JSNode **ppNodes;
    size_t nSize;
    size_t nCapacity;
} JSNodePool;

struct JSCtx {
    JSObj *pGlobal;
    JSScope *pGlobalScope;

    JSObj *pObjectProto;
    JSObj *pFunctionProto;
    JSObj *pArrayProto;
    JSObj *pUint32ArrayProto;
    JSObj *pStringProto;
    JSObj *pNumberProto;
    JSObj *pBooleanProto;
    JSObj *pRegExpProto;
    JSObj *pErrorProto;

    /* engine-wide allocation lists for the final sweep */
    JSStr *pAllStrings;
    JSObj *pAllObjects;
    JSScope *pAllScopes;
    xx_list_t vecPrograms; /* owned references to private parsed JSPrograms */
    void **ppProgramPins;  /* pointer hash set; vecPrograms owns the references */
    size_t nProgramPinsCapacity;
    JSProgramCache *pProgramCache;

    int bException;
    JSVal exception;
    char *pErrorText;

    /* Whether the currently executing native callback was called with new. */
    int bNativeConstruct;
    int nCallDepth;
    int nMaxCallDepth;

    /* AST recursion depth of the evaluator, guarded like the call depth. */
    int nEvalDepth;
    int nMaxEvalDepth;

    /* Nesting of the eager free path; see jsobj_unref. */
    int nFreeDepth;
    int bCollectingCycles;

    void *pUser;
    JSDebugHook pDebugHook;
    void *pDebugUser;
    JSDebugFrameState *pDebugFrame;
    int bDebugInHook;
    int bDebugAborted;

    /* Cached empty string. */
    JSStr *pEmptyStr;
};

/* --------------------------------------------------------------- helpers  */

JSObj *jsobj_new(JSCtx *pCtx, JSClass cls, JSObj *pProto);
JSObj *jsobj_ref(JSObj *pObj);
void jsobj_unref(JSCtx *pCtx, JSObj *pObj);

JSScope *jsscope_new(JSCtx *pCtx, JSScope *pParent);
JSScope *jsscope_ref(JSScope *pScope);
void jsscope_unref(JSCtx *pCtx, JSScope *pScope);
/* Completed interpreted frame release; may collect unreachable local cycles. */
void jsscope_close(JSCtx *pCtx, JSScope *pScope);

JSVal jsval_obj(JSObj *pObj); /* takes ownership of one reference */
JSVal jsval_str(JSStr *pStr); /* takes ownership of one reference */

/* Raw property access (no prototype chain). */
JSVal jsobj_get_own(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, int *pbFound);
/* Full lookup with prototype chain and special cases (length, indices...). */
JSVal jsobj_get(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize);
void jsobj_put(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, JSVal value);
void jsobj_put_hidden(JSCtx *pCtx, JSObj *pObj, const char *pKey, JSVal value);
/* The key must remain valid for the lifetime of the context. */
void jsobj_put_hidden_static(JSCtx *pCtx, JSObj *pObj, const char *pKey, JSVal value);
int jsobj_del(JSCtx *pCtx, JSObj *pObj, const char *pKey);
JSVal jsobj_keys(JSCtx *pCtx, JSObj *pObj);
int jsobj_property_is_enumerable(JSObj *pObj, const char *pKey, size_t nKeySize);
void jsobj_set_enumerable(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, int bEnumerable);
int jsobj_has(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize);

/* Number/string helpers shared by the builtins. */
JSVal js_number_to_string(JSCtx *pCtx, double nValue, int nRadix);
/* Exact decimal safe integers; pBuf needs 32 bytes. 0 = not applicable. */
int js_int_to_buf(char *pBuf, double nValue);
/* Complete, descriptor-free dense ranges can shift without copying slots.
 * shift transfers one reference to pResult; unshift duplicates its arguments. */
int js_array_shift_dense(JSCtx *pCtx, JSObj *pObj, JSVal *pResult);
int js_array_unshift_dense(JSCtx *pCtx, JSObj *pObj, int nArgc, JSVal *pArgv);
double js_string_to_number(const char *pData, size_t nSize);
int js_is_array_index(const char *pKey, size_t nKeySize, int64_t *pnIndex);
JSVal js_concat_str(JSCtx *pCtx, JSVal left, JSVal right);
JSVal js_concat_str_assign(JSCtx *pCtx, JSVal left, JSVal right);

/* Equality / comparison used by the interpreter and by Array.sort. */
int js_strict_equals(JSCtx *pCtx, JSVal left, JSVal right);
int js_loose_equals(JSCtx *pCtx, JSVal left, JSVal right);
JSVal js_to_primitive(JSCtx *pCtx, JSVal value, int bPreferString);

/* Object construction helpers. */
JSVal js_construct(JSCtx *pCtx, JSVal fn, int nArgc, JSVal *pArgv);

/* Builtin installation (js_builtins.c). */
void js_install_builtins(JSCtx *pCtx);
int js_is_string_concat(JSObj *pObj);
void js_install_regexp(JSCtx *pCtx);

/* JavaScript RegExp adapters; compilation retains interpreter OOM policy. */
JSRegExp *js_compile_regexp(const char *pattern, const char *flags, char **error);
JSRegExp *js_compile_regexp_n(const char *pattern, size_t pattern_size, const char *flags, size_t flags_size, char **error);
JSVal js_new_regexp_val(JSCtx *pCtx, const char *pPattern, const char *pFlags);
JSVal js_new_regexp_n_val(JSCtx *pCtx, const char *pPattern, size_t nPatternSize, const char *pFlags, size_t nFlagsSize);
JSRegExp *js_get_regexp(JSVal value);

/* Parser (js_parse.c). */
JSNode *js_parse_program(JSCtx *pCtx, const char *pSource, const char *pName, char **ppError);
void js_free_node(JSNode *pNode);

/* Interpreter (js_interp.c). */
JSVal js_run_program(JSCtx *pCtx, JSNode *pProgram, JSScope *pScope, JSVal thisVal);
JSVal js_call_function(JSCtx *pCtx, JSObj *pFn, JSVal thisVal, int nArgc, JSVal *pArgv, int bConstruct);

void js_debug_exception(JSCtx *pCtx);

/* Sets the pending exception from a printf-style message. */
JSVal js_throw_value(JSCtx *pCtx, JSVal value);

void js_prepare_program(JSNode *pProgram);
JSNode *js_parse_bytecode(const void *pData, size_t nSize);
JSVal js_new_regexp_shared_val(JSCtx *pCtx, JSRegExp *pRegExp);

#endif /* JS_INTERNAL_H */
