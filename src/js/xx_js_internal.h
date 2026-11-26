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
    uint8_t bInline;    /* pData points into this block (see jsstr_new) */
    uint8_t nPoolClass; /* free list this header returns to */
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
/* The key must outlive the map (an AST spelling: programs stay pinned for the
 * lifetime of the context) and nHash must be xx_js_hash_str(pKey, nKeySize). */
JSProp *jsprops_put_borrowed_hashed(JSCtx *pCtx, JSPropMap *pMap, const char *pKey, size_t nKeySize, uint32_t nHash);
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
struct JSScopeLayout;

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

    /* scope record: the layout its entries were copied from, while they are
     * still exactly that layout's bindings in order (see JSScopeLayout) */
    const struct JSScopeLayout *pLayout;

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

/* A hit needs the same spelling pointer and the current epoch. A spelling is
 * the pStr of a node of a program pinned by the context, so the pointer
 * identifies its bytes for the lifetime of the context. The epoch advances
 * whenever an entry of the global object is deleted or entries move (map
 * compaction); appending and overwriting keep every index valid. Misses are
 * never recorded. */
typedef struct {
    const char *pName;
    uint32_t nEpoch;
    uint32_t nIndex;
} JSGlobalCacheEntry;
#define JS_GLOBAL_CACHE_SIZE 512u

enum {
    JS_POOL_OBJ = 0,
    JS_POOL_SCOPE,
    JS_POOL_STR0, /* header + JS_STR_INLINE0 + 1 bytes */
    JS_POOL_STR1,
    JS_POOL_STR2,
    JS_POOL_STRHDR, /* header of a string with a separate buffer */
    JS_POOL_ENTRIES, /* JS_PROPS_INITIAL JSProp */
    JS_POOL_INDEX,   /* JS_PROPS_INITIAL_INDEX int32_t */
    JS_POOL_COUNT
};
#define JS_POOL_LIMIT 256u
#define JS_STR_INLINE0 15u
#define JS_STR_INLINE1 47u
#define JS_STR_INLINE2 111u
#define JS_PROPS_INITIAL 8u
#define JS_PROPS_INITIAL_INDEX 16u

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

    /* Recycled blocks of the hottest fixed sizes (activation records, small
     * strings, initial property tables). Every block is an ordinary heap
     * block: a free list only postpones its release, at most JS_POOL_LIMIT
     * blocks per class, until the next allocation of that class or js_free. */
    void *pPool[JS_POOL_COUNT];
    uint32_t nPool[JS_POOL_COUNT];

    /* Global bindings by AST spelling: the entry index a spelling resolved to
     * on the global object, valid while nGlobalEpoch is unchanged (see
     * JSGlobalCacheEntry). */
    uint32_t nGlobalEpoch;
    JSGlobalCacheEntry globalCache[JS_GLOBAL_CACHE_SIZE];
};

void jspool_release(JSCtx *pCtx);

/* --------------------------------------------------------------- helpers  */

JSObj *jsobj_new(JSCtx *pCtx, JSClass cls, JSObj *pProto);
JSObj *jsobj_ref(JSObj *pObj);
void jsobj_unref(JSCtx *pCtx, JSObj *pObj);

JSScope *jsscope_new(JSCtx *pCtx, JSScope *pParent);
/* A scope whose record starts as a copy of the layout's bindings. */
JSScope *jsscope_new_layout(JSCtx *pCtx, JSScope *pParent, const struct JSScopeLayout *pLayout);
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
/* jsobj_get / jsobj_put with nHash == xx_js_hash_str(pKey, nKeySize) already known. */
JSVal jsobj_get_hashed(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, uint32_t nHash);
void jsobj_put_hashed(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, uint32_t nHash, JSVal value);
/* Same, and a new entry borrows pKey, which must outlive the context's
 * objects (an AST spelling; see jsprops_put_borrowed_hashed). */
void jsobj_put_borrowed_hashed(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, uint32_t nHash, JSVal value);
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
/* js_concat_str for operands that need no conversion call: strings and
 * exact safe integers. Returns 0 (and does nothing) for anything else. */
int js_concat_primitive(JSCtx *pCtx, JSVal left, JSVal right, JSVal *pResult);
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

/* ------------------------------------------------------- inline helpers  */

#if defined(_MSC_VER)
#define JS_NOINLINE __declspec(noinline)
#define JS_FORCEINLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define JS_NOINLINE __attribute__((noinline))
#define JS_FORCEINLINE static inline __attribute__((always_inline))
#else
#define JS_NOINLINE
#define JS_FORCEINLINE static inline
#endif

#define JS_GLOBAL_CACHE_SLOT(pName) ((((uintptr_t)(pName) >> 4) ^ ((uintptr_t)(pName) >> 13)) & (JS_GLOBAL_CACHE_SIZE - 1))

/* The value constructors and reference helpers of the public API, for the
 * engine's own translation units: same results, no call. */
static inline JSVal jsv_undefined(void)
{
    JSVal value;
    value.tag = JT_UNDEF;
    value.u.n = 0;
    return value;
}

static inline JSVal jsv_null(void)
{
    JSVal value;
    value.tag = JT_NULL;
    value.u.n = 0;
    return value;
}

static inline JSVal jsv_bool(int bValue)
{
    JSVal value;
    value.tag = JT_BOOL;
    value.u.n = 0;
    value.u.b = bValue ? 1 : 0;
    return value;
}

static inline JSVal jsv_num(double nValue)
{
    JSVal value;
    value.tag = JT_NUM;
    value.u.n = nValue;
    return value;
}

static inline JSVal jsv_dup(JSVal value)
{
    if (value.tag == JT_STR) {
        if (value.u.s) value.u.s->nRef++;
    } else if (value.tag == JT_OBJ) {
        if (value.u.o) value.u.o->nRef++;
    }
    return value;
}

/* Only the decrements that cannot free anything or reach jsobj_unref's
 * constructor/prototype check (a function left with one reference) stay
 * inline. */
static inline void jsv_release(JSCtx *pCtx, JSVal value)
{
    if (value.tag == JT_STR) {
        JSStr *pStr = value.u.s;
        if (pStr && pStr->nRef > 1) pStr->nRef--;
        else jsstr_unref(pCtx, pStr);
    } else if (value.tag == JT_OBJ) {
        JSObj *pObj = value.u.o;
        if (pObj && (pObj->nRef > 2 || (pObj->nRef == 2 && pObj->cls != JCLASS_FUNCTION))) pObj->nRef--;
        else jsobj_unref(pCtx, pObj);
    }
}

static inline int jsv_to_bool(JSVal value)
{
    switch (value.tag) {
        case JT_BOOL: return value.u.b;
        case JT_NUM: return ((value.u.n != 0) && (!(value.u.n != value.u.n))) ? 1 : 0;
        case JT_STR: return (value.u.s->nSize > 0) ? 1 : 0;
        case JT_OBJ: return 1;
        default: break;
    }
    return 0;
}

/* js_strict_equals. */
static inline int jsv_strict_equals(JSVal left, JSVal right)
{
    if (left.tag != right.tag) return 0;
    switch (left.tag) {
        case JT_UNDEF:
        case JT_NULL: return 1;
        case JT_BOOL: return left.u.b == right.u.b;
        case JT_NUM: return left.u.n == right.u.n;
        case JT_STR: {
            size_t nSize = left.u.s->nSize, i;
            const char *pLeft = left.u.s->pData, *pRight = right.u.s->pData;
            if (left.u.s == right.u.s) return 1;
            if (nSize != right.u.s->nSize) return 0;
            if (nSize >= 32) return xx_rt_memcmp(pLeft, pRight, nSize) == 0;
            for (i = 0; i < nSize; i++) {
                if (pLeft[i] != pRight[i]) return 0;
            }
            return 1;
        }
        case JT_OBJ: return left.u.o == right.u.o;
        default: break;
    }
    return 0;
}

static inline int jsv_is_callable(JSVal value)
{
    return value.tag == JT_OBJ && ((value.u.o->cls == JCLASS_FUNCTION) || (value.u.o->cls == JCLASS_NATIVE));
}

/* ToInt32 of a number that is already an integer in [-2^31, 2^32): the
 * value js_to_int32 computes for it (truncate, modulo 2^32, reinterpret).
 * Returns 0 for every other number, which takes js_to_int32. */
static inline int jsv_int32_fast(double nValue, int32_t *pnResult)
{
    if (nValue >= -2147483648.0 && nValue <= 4294967295.0) {
        int64_t nInt = (int64_t)nValue;
        if ((double)nInt == nValue) {
            *pnResult = (int32_t)(uint32_t)nInt;
            return 1;
        }
    }
    return 0;
}

/* Property keys are short: a byte loop beats the dispatching xx_rt_memcmp. */
static inline int jskey_equal(const char *pLeft, const char *pRight, size_t nSize)
{
    size_t i;
    for (i = 0; i < nSize; i++) {
        if (pLeft[i] != pRight[i]) return 0;
    }
    return 1;
}
JSNode *js_parse_bytecode(const void *pData, size_t nSize);
JSVal js_new_regexp_shared_val(JSCtx *pCtx, JSRegExp *pRegExp);

#endif /* JS_INTERNAL_H */
