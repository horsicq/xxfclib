/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_DIE_ENGINE_DEBUG_H
#define XX_DIE_ENGINE_DEBUG_H

#include "die_engine.h"
#include "xxfclib/js/xx_js_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIE_ENGINE_DEBUG_VERSION 1u

/* Borrowed text, including its line ending, valid only for the call. Runs on
 * the scanning thread, as does the JavaScript debug hook. */
typedef void (*die_engine_debug_log_fn)(const char *text, void *user_data);

typedef struct die_engine_debug_config {
    size_t size;                 /* Initialise to sizeof(die_engine_debug_config). */
    unsigned int version;       /* DIE_ENGINE_DEBUG_VERSION. */
    JSDebugHook hook;
    void *hook_user;
    die_engine_debug_log_fn log;
    void *log_user;
    const char *signature_source; /* Optional edited source; NULL scans the database. */
    const char *signature_name;   /* Source identity; NULL/empty uses untitled.sg. */
} die_engine_debug_config;

/* Synchronously debug one input using the actual DIE format objects, include
 * helpers, _init scripts and detect() calls. The database and config are
 * borrowed through return. Each format pass owns its JavaScript context; hook
 * callbacks receive that actual context and may inspect it while paused. A
 * zero hook result cancels the whole pass and progress object. Do not call
 * widget APIs, recursively scan, free the context or retain its values here.
 *
 * With signature_source, the original database supplies _init/include helpers
 * and only the supplied source/detect() is run. Both source strings are borrowed
 * until return. Without it, normal database selection applies. No source files
 * or shared database records are changed by this function.
 * UNKNOWN selects normal automatic detection; an explicit type runs that pass.
 * offset/size bound the input, and record offsets are rebased to the input file.
 * Recursive, all-types, overlay, resource and archive expansion are rejected.
 *
 * result is initialised and may contain partial records/errors on cancellation
 * or failure. Release it with xx_scan_result_free() on every outcome. NULL
 * progress uses private progress storage. _log output is captured independently
 * of show_messages; profiling output obeys options.profiling. With no log
 * callback, the normal console output rules apply. */
XXFC_API int die_engine_scan_file_type_debug_pd(const char *file_name, DBase *database,
    xx_scan_options *options, XFileType file_type, xx_scan_result *result,
    xx_pd_struct *progress, const die_engine_debug_config *config);

#ifdef __cplusplus
}
#endif
#endif
