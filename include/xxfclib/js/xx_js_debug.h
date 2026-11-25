/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_XX_JS_DEBUG_H
#define XXFCLIB_XX_JS_DEBUG_H
#include "xxfclib/js/xx_js.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XX_JS_DEBUG_API 1
typedef enum {
    JS_DEBUG_STATEMENT = 0,
    JS_DEBUG_EXCEPTION,
    JS_DEBUG_DEBUGGER
} JSDebugEventType;
typedef struct {
    JSDebugEventType type;
    const char *pSourceName;
    const char *pFunctionName;
    int nLine;
    int nColumn;
    int nDepth;
} JSDebugEvent;
typedef struct {
    const char *pSourceName;
    const char *pFunctionName;
    int nLine;
    int nColumn;
    int bNative;
} JSDebugFrameInfo;
/* Runs synchronously on the context's execution thread before executable
 * statements and every loop condition/update. The callback may block to pause.
 * Return nonzero to continue, zero to cancel the evaluation uncatchably.
 * No context API may be used by another thread, including while paused.
 * Do not evaluate JS or change the hook from inside the callback. Event strings
 * and all inspection names are borrowed until the callback returns.
 * Lines/columns are one based; column zero denotes legacy bytecode without
 * column metadata. Depth includes program, interpreted and native frames. */
typedef int (*JSDebugHook)(JSCtx *pCtx, const JSDebugEvent *pEvent, void *pUser);
XXFC_API void js_debug_set_hook(JSCtx *pCtx, JSDebugHook pHook, void *pUser);
XXFC_API void js_debug_get_hook(JSCtx *pCtx, JSDebugHook *ppHook, void **ppUser);
/* Inspection is available only inside the hook, on its execution thread.
 * Frame zero is current. Native frames have no local variables. */
XXFC_API size_t js_debug_frame_count(JSCtx *pCtx);
XXFC_API int js_debug_frame(JSCtx *pCtx, size_t nFrame, JSDebugFrameInfo *pInfo);
XXFC_API size_t js_debug_local_count(JSCtx *pCtx, size_t nFrame);
/* Successful lookup returns one owned value: release with js_release before
 * leaving the hook. Values are read directly, without getters or conversions.
 * Locals include active catch bindings and the current activation's variables.
 * lookup accepts one identifier or "this", never arbitrary expressions. */
XXFC_API int js_debug_local(JSCtx *pCtx, size_t nFrame, size_t nLocal, const char **ppName, JSVal *pValue);
XXFC_API int js_debug_lookup(JSCtx *pCtx, size_t nFrame, const char *pIdentifier, JSVal *pValue);
#ifdef __cplusplus
}
#endif
#endif
