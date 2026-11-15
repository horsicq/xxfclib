/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_parallel.c
 * @brief Indexed parallel loop on short-lived helper threads.
 */

#include "xxfclib/threads/xx_parallel.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

typedef struct xx_parallel_state_s {
    volatile long next;
    long count;
    xx_parallel_fn fn;
    void (*helper_done)(void *context);
    void *context;
} xx_parallel_state_t;

/* Claims indices until none is left. Every index is claimed exactly once:
 * xx_atomic_add returns each value of next to one caller only. */
static void xx_parallel_drain(xx_parallel_state_t *state)
{
    for (;;) {
        long index = xx_atomic_add(&state->next, 1);

        if (index >= state->count) {
            break;
        }

        state->fn((size_t)index, state->context);
    }
}

static int xx_parallel_helper(void *arg)
{
    xx_parallel_state_t *state = (xx_parallel_state_t *)arg;

    xx_parallel_drain(state);

    if (state->helper_done) {
        state->helper_done(state->context);
    }

    return 0;
}

uint32_t xx_parallel_for(size_t count, uint32_t thread_count, size_t stack_size,
                         xx_parallel_fn fn, void *context)
{
    return xx_parallel_for_ex(count, thread_count, stack_size, fn, NULL, context);
}

uint32_t xx_parallel_for_ex(size_t count, uint32_t thread_count, size_t stack_size,
                            xx_parallel_fn fn, void (*helper_done)(void *context),
                            void *context)
{
    xx_parallel_state_t state;
    xx_thread_t **helpers = NULL;
    uint32_t started = 0;
    uint32_t wanted;

    if (!fn || !count) {
        return 0;
    }

    /* Each thread claims at most one index past count; keep next in range. */
    if (thread_count <= 1 || count == 1 || count > (size_t)(LONG_MAX / 2)) {
        for (size_t i = 0; i < count; ++i) {
            fn(i, context);
        }

        return 1;
    }

    wanted = thread_count;

    if ((size_t)wanted > count) {
        wanted = (uint32_t)count;
    }

    state.next = 0;
    state.count = (long)count;
    state.fn = fn;
    state.helper_done = helper_done;
    state.context = context;

    helpers = (xx_thread_t **)xx_mem_calloc((size_t)wanted - 1, sizeof(*helpers));

    if (helpers) {
        for (uint32_t i = 0; i + 1 < wanted; ++i) {
            helpers[i] = xx_thread_create_ex(xx_parallel_helper, &state, stack_size);

            if (!helpers[i]) {
                break;
            }

            ++started;
        }
    }

    xx_parallel_drain(&state);

    for (uint32_t i = 0; i < started; ++i) {
        (void)xx_thread_join(helpers[i], NULL);
    }

    xx_mem_free(helpers);

    return started + 1;
}
