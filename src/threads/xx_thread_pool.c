/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/threads/xx_thread_pool.h"
#include "xxfclib/memory/xx_memory.h"

typedef struct xx_pool_task_s {
    xx_thread_pool_fn fn;
    xx_thread_pool_fn cleanup;
    void *arg;
    struct xx_pool_task_s *next;
} xx_pool_task_t;

typedef struct xx_pool_worker_s {
    xx_thread_pool_t *pool;
    xx_thread_t *thread;
    uint64_t id;
    bool live;
} xx_pool_worker_t;

/* Stack-owned by wait(); all access and removal is under pool->mutex. */
typedef struct xx_pool_waiter_s {
    xx_semaphore_t *ready;
    struct xx_pool_waiter_s *next;
    bool signalled;
} xx_pool_waiter_t;

struct xx_thread_pool_s {
    xx_mutex_t *mutex;
    xx_semaphore_t *work;
    xx_pool_worker_t *workers;
    uint32_t thread_count;
    size_t max_pending;
    xx_pool_task_t *head;
    xx_pool_task_t *tail;
    size_t pending;
    size_t active;
    bool accepting;
    bool cancelling;
    uint64_t cleanup_thread;
    xx_pool_waiter_t *waiters;
};

/* Call with the pool mutex locked. */
static bool xx_pool_idle(const xx_thread_pool_t *pool)
{
    return !pool->pending && !pool->active && !pool->cancelling;
}

static bool xx_pool_in_callback(const xx_thread_pool_t *pool)
{
    uint64_t id = xx_thread_current_id();
    uint32_t i;

    if (pool->cancelling && pool->cleanup_thread == id) {
        return true;
    }

    for (i = 0; i < pool->thread_count; ++i) {
        if (pool->workers[i].live && pool->workers[i].id == id) {
            return true;
        }
    }

    return false;
}

static void xx_pool_signal_idle(xx_thread_pool_t *pool)
{
    xx_pool_waiter_t *waiter;

    if (!xx_pool_idle(pool)) {
        return;
    }

    for (waiter = pool->waiters; waiter; waiter = waiter->next) {
        /* A private semaphore receives at most one notification. */
        if (!waiter->signalled && xx_semaphore_post(waiter->ready, 1)) {
            waiter->signalled = true;
        }
    }
}

static int xx_pool_worker(void *arg)
{
    xx_pool_worker_t *worker = (xx_pool_worker_t *)arg;
    xx_thread_pool_t *pool = worker->pool;

    xx_mutex_lock(pool->mutex);
    worker->id = xx_thread_current_id();
    worker->live = true;
    xx_mutex_unlock(pool->mutex);

    for (;;) {
        xx_pool_task_t *task;

        /* An infinite wait on this live semaphore has no timeout. */
        if (xx_semaphore_wait(pool->work, XX_WAIT_INFINITE) != XX_WAIT_OK) {
            xx_thread_yield();
            continue;
        }

        xx_mutex_lock(pool->mutex);
        task = pool->head;

        if (!task) {
            if (!pool->accepting) {
                worker->live = false;
                xx_mutex_unlock(pool->mutex);
                return 0;
            }
            xx_mutex_unlock(pool->mutex);
            continue;
        }

        pool->head = task->next;
        if (!pool->head) {
            pool->tail = NULL;
        }
        --pool->pending;
        ++pool->active;
        xx_mutex_unlock(pool->mutex);

        task->fn(task->arg);
        if (task->cleanup) {
            task->cleanup(task->arg);
        }
        xx_mem_free(task);

        xx_mutex_lock(pool->mutex);
        --pool->active;
        xx_pool_signal_idle(pool);
        xx_mutex_unlock(pool->mutex);
    }
}

xx_thread_pool_t *xx_thread_pool_create(uint32_t thread_count)
{
    return xx_thread_pool_create_ex(thread_count, 0, 0);
}

xx_thread_pool_t *xx_thread_pool_create_ex(uint32_t thread_count, size_t max_pending, size_t stack_size)
{
    xx_thread_pool_t *pool;
    uint32_t i;

    if (!thread_count) {
        thread_count = xx_thread_cpu_count();
    }

    /* Reserve one shutdown wake-up per worker in the counting semaphore. */
    if (!thread_count || thread_count >= INT32_MAX || sizeof(xx_pool_worker_t) > SIZE_MAX / (size_t)thread_count || max_pending > (size_t)INT32_MAX - thread_count) {
        return NULL;
    }

    pool = (xx_thread_pool_t *)xx_mem_calloc(1, sizeof(*pool));
    if (!pool) {
        return NULL;
    }

    pool->mutex = xx_mutex_create();
    pool->work = xx_semaphore_create(0);
    pool->workers = (xx_pool_worker_t *)xx_mem_calloc(thread_count, sizeof(*pool->workers));
    if (!pool->mutex || !pool->work || !pool->workers) {
        xx_mem_free(pool->workers);
        xx_semaphore_destroy(pool->work);
        xx_mutex_destroy(pool->mutex);
        xx_mem_free(pool);
        return NULL;
    }

    pool->thread_count = thread_count;
    pool->max_pending = max_pending ? max_pending : (size_t)INT32_MAX - thread_count;
    pool->accepting = true;

    for (i = 0; i < thread_count; ++i) {
        pool->workers[i].pool = pool;
        pool->workers[i].thread = xx_thread_create_ex(xx_pool_worker, &pool->workers[i], stack_size);
        if (!pool->workers[i].thread) {
            /* No tasks can be submitted until creation returns to the caller. */
            xx_thread_pool_destroy(pool, XX_THREAD_POOL_CANCEL_PENDING);
            return NULL;
        }
    }

    return pool;
}

bool xx_thread_pool_submit(xx_thread_pool_t *pool, xx_thread_pool_fn fn, void *arg)
{
    return xx_thread_pool_submit_ex(pool, fn, arg, NULL);
}

bool xx_thread_pool_submit_ex(xx_thread_pool_t *pool, xx_thread_pool_fn fn, void *arg, xx_thread_pool_fn cleanup)
{
    xx_pool_task_t *task;
    xx_pool_task_t *previous;

    if (!pool || !fn) {
        return false;
    }

    task = (xx_pool_task_t *)xx_mem_alloc(sizeof(*task));
    if (!task) {
        return false;
    }
    task->fn = fn;
    task->cleanup = cleanup;
    task->arg = arg;
    task->next = NULL;

    xx_mutex_lock(pool->mutex);
    if (!pool->accepting || pool->pending == pool->max_pending) {
        xx_mutex_unlock(pool->mutex);
        xx_mem_free(task);
        return false;
    }

    previous = pool->tail;
    if (previous) {
        previous->next = task;
    } else {
        pool->head = task;
    }
    pool->tail = task;
    ++pool->pending;

    if (!xx_semaphore_post(pool->work, 1)) {
        /* Workers cannot take the task until the queue mutex is released. */
        if (previous) {
            previous->next = NULL;
        } else {
            pool->head = NULL;
        }
        pool->tail = previous;
        --pool->pending;
        xx_mutex_unlock(pool->mutex);
        xx_mem_free(task);
        return false;
    }

    xx_mutex_unlock(pool->mutex);
    return true;
}

xx_wait_result_t xx_thread_pool_wait(xx_thread_pool_t *pool, uint32_t timeout_ms)
{
    xx_pool_waiter_t waiter;
    xx_pool_waiter_t **link;
    xx_wait_result_t result;

    if (!pool) {
        return XX_WAIT_ERROR;
    }

    xx_mutex_lock(pool->mutex);
    if (xx_pool_in_callback(pool)) {
        xx_mutex_unlock(pool->mutex);
        return XX_WAIT_ERROR;
    }
    if (xx_pool_idle(pool)) {
        xx_mutex_unlock(pool->mutex);
        return XX_WAIT_OK;
    }
    if (!timeout_ms) {
        xx_mutex_unlock(pool->mutex);
        return XX_WAIT_TIMEOUT;
    }

    waiter.ready = xx_semaphore_create(0);
    if (!waiter.ready) {
        xx_mutex_unlock(pool->mutex);
        return XX_WAIT_ERROR;
    }
    waiter.signalled = false;
    waiter.next = pool->waiters;
    pool->waiters = &waiter;
    xx_mutex_unlock(pool->mutex);

    result = xx_semaphore_wait(waiter.ready, timeout_ms);

    xx_mutex_lock(pool->mutex);
    /* A notification racing a timeout still establishes the idle point. */
    if (waiter.signalled) {
        result = XX_WAIT_OK;
    }
    for (link = &pool->waiters; *link != &waiter; link = &(*link)->next) {
    }
    *link = waiter.next;
    xx_mutex_unlock(pool->mutex);
    xx_semaphore_destroy(waiter.ready);

    return result;
}

bool xx_thread_pool_get_stats(xx_thread_pool_t *pool, xx_thread_pool_stats_t *stats)
{
    if (!pool || !stats) {
        return false;
    }

    xx_mutex_lock(pool->mutex);
    stats->thread_count = pool->thread_count;
    stats->pending = pool->pending;
    stats->active = pool->active;
    stats->accepting = pool->accepting;
    stats->idle = xx_pool_idle(pool);
    xx_mutex_unlock(pool->mutex);
    return true;
}

bool xx_thread_pool_shutdown(xx_thread_pool_t *pool, xx_thread_pool_shutdown_t mode)
{
    xx_pool_task_t *discarded = NULL;

    if (!pool || (mode != XX_THREAD_POOL_DRAIN && mode != XX_THREAD_POOL_CANCEL_PENDING)) {
        return false;
    }

    xx_mutex_lock(pool->mutex);
    if (xx_pool_in_callback(pool)) {
        xx_mutex_unlock(pool->mutex);
        return false;
    }

    if (pool->accepting) {
        /* Pending permits + these wake-ups cannot exceed INT32_MAX. */
        if (!xx_semaphore_post(pool->work, pool->thread_count)) {
            xx_mutex_unlock(pool->mutex);
            return false;
        }
        pool->accepting = false;
    }

    if (mode == XX_THREAD_POOL_CANCEL_PENDING && pool->head) {
        discarded = pool->head;
        pool->head = NULL;
        pool->tail = NULL;
        pool->pending = 0;
        pool->cancelling = true;
        pool->cleanup_thread = xx_thread_current_id();
    }
    xx_pool_signal_idle(pool);
    xx_mutex_unlock(pool->mutex);

    if (discarded) {
        while (discarded) {
            xx_pool_task_t *next = discarded->next;
            if (discarded->cleanup) {
                discarded->cleanup(discarded->arg);
            }
            xx_mem_free(discarded);
            discarded = next;
        }
        xx_mutex_lock(pool->mutex);
        pool->cancelling = false;
        xx_pool_signal_idle(pool);
        xx_mutex_unlock(pool->mutex);
    }

    return true;
}

bool xx_thread_pool_destroy(xx_thread_pool_t *pool, xx_thread_pool_shutdown_t mode)
{
    uint32_t i;

    if (!pool) {
        return true;
    }
    if (!xx_thread_pool_shutdown(pool, mode)) {
        return false;
    }

    for (i = 0; i < pool->thread_count; ++i) {
        if (pool->workers[i].thread) {
            if (!xx_thread_join(pool->workers[i].thread, NULL)) {
                return false;
            }
            pool->workers[i].thread = NULL;
        }
    }

    xx_mem_free(pool->workers);
    xx_semaphore_destroy(pool->work);
    xx_mutex_destroy(pool->mutex);
    xx_mem_free(pool);
    return true;
}
