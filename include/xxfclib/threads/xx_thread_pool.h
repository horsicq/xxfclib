/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_thread_pool.h
 * @brief Fixed-size worker pool with a FIFO task queue.
 *
 * Uses xx_threads primitives, including semaphores rather than condition
 * variables, so the Windows backend also supports Windows XP without a CRT.
 * Submission, statistics and idle waits may run concurrently. Shutdown calls
 * must be serialized by the owner. Destroy requires exclusive ownership: no
 * other client may use the handle once destruction starts. Running callbacks
 * may still submit work (which is rejected after shutdown) or query statistics.
 */

#ifndef XX_THREAD_POOL_H
#define XX_THREAD_POOL_H

#include "xxfclib/threads/xx_threads.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_thread_pool_s xx_thread_pool_t;
typedef void (*xx_thread_pool_fn)(void *arg);

typedef enum xx_thread_pool_shutdown_e {
    XX_THREAD_POOL_DRAIN = 0,         /**< Execute every accepted task. */
    XX_THREAD_POOL_CANCEL_PENDING = 1 /**< Discard queued tasks; finish active tasks. */
} xx_thread_pool_shutdown_t;

typedef struct xx_thread_pool_stats_s {
    uint32_t thread_count; /**< Configured number of worker threads. */
    size_t pending;        /**< Tasks still in the queue. */
    size_t active;         /**< Worker tasks running, including their cleanup callbacks. */
    bool accepting;        /**< False after shutdown begins. */
    bool idle;             /**< No queued/active tasks or cancellation cleanup remains. */
} xx_thread_pool_stats_t;

/** Create a pool; thread_count 0 selects xx_thread_cpu_count(). NULL on failure. */
XXFC_API xx_thread_pool_t *xx_thread_pool_create(uint32_t thread_count);

/**
 * As create, with an optional pending-task limit and per-worker stack size.
 * max_pending 0 means no caller-specified limit; active tasks do not consume
 * queue capacity. stack_size 0 selects the platform default. Internal semaphore
 * limits still apply. A partially created pool is fully released on failure.
 */
XXFC_API xx_thread_pool_t *xx_thread_pool_create_ex(uint32_t thread_count, size_t max_pending, size_t stack_size);

/** Nonblocking submission. False on invalid input, shutdown, full queue or failure. */
XXFC_API bool xx_thread_pool_submit(xx_thread_pool_t *pool, xx_thread_pool_fn fn, void *arg);

/**
 * Submit with optional cleanup(arg), called exactly once after fn returns or
 * when shutdown discards the task. On rejection neither callback is called;
 * ownership of arg stays with the caller. Both callbacks run outside the pool
 * mutex. Cleanup runs on a worker, or on the thread cancelling queued tasks.
 * Callbacks must return normally and may submit other tasks. They must not
 * wait for, shut down or destroy their own pool; those calls report failure.
 * FIFO describes dequeue order, not start or completion order across workers.
 */
XXFC_API bool xx_thread_pool_submit_ex(xx_thread_pool_t *pool, xx_thread_pool_fn fn, void *arg, xx_thread_pool_fn cleanup);

/**
 * Wait until the pool reaches an idle point, including all cleanup callbacks.
 * Concurrent submissions can start new work before this function returns.
 * timeout_ms 0 polls; XX_WAIT_INFINITE waits without a timeout. Returns
 * XX_WAIT_ERROR for NULL, failure, or a call from this pool's callback.
 */
XXFC_API xx_wait_result_t xx_thread_pool_wait(xx_thread_pool_t *pool, uint32_t timeout_ms);

/** Take a consistent snapshot of queue/worker activity. False for NULL arguments. */
XXFC_API bool xx_thread_pool_get_stats(xx_thread_pool_t *pool, xx_thread_pool_stats_t *stats);

/**
 * Stop accepting tasks and wake workers. Does not wait for active tasks, but
 * CANCEL_PENDING invokes discarded tasks' cleanup callbacks before returning.
 * Repeated calls are allowed; CANCEL_PENDING may escalate an earlier DRAIN.
 * False on invalid arguments, a call from a callback, or a wake-up failure.
 * The owner must serialize shutdown calls and keep the pool alive afterward.
 */
XXFC_API bool xx_thread_pool_shutdown(xx_thread_pool_t *pool, xx_thread_pool_shutdown_t mode);

/**
 * Shut down, join every worker, and release the pool. NULL is a successful
 * no-op. Returns false if called from a callback or if shutdown/join fails;
 * in that case the handle remains valid for the owner to retry destruction.
 * No concurrent client calls or idle waiters may overlap destruction.
 */
XXFC_API bool xx_thread_pool_destroy(xx_thread_pool_t *pool, xx_thread_pool_shutdown_t mode);

#ifdef __cplusplus
}
#endif

#endif /* XX_THREAD_POOL_H */
