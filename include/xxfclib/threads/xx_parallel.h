/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_parallel.h
 * @brief Run an indexed loop body on several threads.
 *
 * xx_parallel_for calls fn(index, context) exactly once for every index in
 * [0, count) and returns after the last call has returned. The calling thread
 * takes part; at most thread_count - 1 helper threads are started for the call
 * and joined before it returns, so nothing outlives it. Indices are handed out
 * in increasing order, one at a time, to whichever thread is free; calls run
 * concurrently and finish in any order.
 *
 * Results are deterministic when every call writes only its own output (for
 * example slot index of an array) and the caller reads those outputs after
 * the loop in index order. With thread_count 0 or 1, or when no helper thread
 * can be started, the calls run on the calling thread in index order.
 */

#ifndef XX_PARALLEL_H
#define XX_PARALLEL_H

#include "xxfclib/threads/xx_threads.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*xx_parallel_fn)(size_t index, void *context);

/**
 * Run fn(i, context) for i in [0, count) on up to thread_count threads, the
 * calling thread included. stack_size is the reservation for each helper
 * thread (0 selects the platform default). Returns the number of threads
 * that took part (1 when the loop ran on the calling thread alone, 0 for an
 * empty loop or a NULL fn).
 */
XXFC_API uint32_t xx_parallel_for(size_t count, uint32_t thread_count, size_t stack_size,
                                  xx_parallel_fn fn, void *context);

/**
 * As xx_parallel_for. When helper_done is not NULL, every helper thread calls
 * helper_done(context) after its last call of fn and before it ends, while
 * its thread-local state is still alive; the calling thread does not. Calls
 * of helper_done on different helpers may run at the same time.
 */
XXFC_API uint32_t xx_parallel_for_ex(size_t count, uint32_t thread_count, size_t stack_size,
                                     xx_parallel_fn fn, void (*helper_done)(void *context),
                                     void *context);

#ifdef __cplusplus
}
#endif

#endif /* XX_PARALLEL_H */
