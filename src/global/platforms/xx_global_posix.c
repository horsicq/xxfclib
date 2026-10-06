/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32)

#include "xx_global_platform.h"
#include "../xx_tls.h"
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

xx_terminal_type_t xx_global_platform_detect_terminal_type(bool standard_error) {
    const char *term;

    if (!isatty(standard_error ? STDERR_FILENO : STDOUT_FILENO)) {
        return XX_TERMINAL_TYPE_NONE;
    }
    term = getenv("TERM");
    if (term && (!term[0] || strcmp(term, "dumb") == 0)) {
        return XX_TERMINAL_TYPE_NONE;
    }
    return XX_TERMINAL_TYPE_ANSI;
}

/* ---------------------------------------------------------------- xx_tls --- */

/* ELF and Mach-O have no TLS directory to avoid, so one small per-thread
 * table holds every key's value. Keys take table indices in first-use order. */
#define XX_TLS_MAX_KEYS 64

/* Transient key states, private to this file: one thread is drawing the
 * key's index, or the table was full and the key never gets one. */
#define XX_TLS_CLAIMED (-1L)
#define XX_TLS_NONE (-2L)

static _Thread_local void *xx_tls_values[XX_TLS_MAX_KEYS];
static long xx_tls_next_slot;

static long xx_tls_slot(xx_tls_key *key) {
    long slot = __atomic_load_n(&key->slot, __ATOMIC_ACQUIRE);
    long expected = 0;

    if (slot > 0) {
        return slot;
    }

    /* Only the thread that claims the key draws an index, so every key uses
     * exactly one and the counter never passes the table size. */
    if ((slot == 0) && __atomic_compare_exchange_n(&key->slot, &expected, XX_TLS_CLAIMED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        long next = __atomic_load_n(&xx_tls_next_slot, __ATOMIC_RELAXED);

        do {
            if (next >= XX_TLS_MAX_KEYS) {
                __atomic_store_n(&key->slot, XX_TLS_NONE, __ATOMIC_RELEASE);
                return 0;
            }
        } while (!__atomic_compare_exchange_n(&xx_tls_next_slot, &next, next + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

        __atomic_store_n(&key->slot, next + 1, __ATOMIC_RELEASE);

        return next + 1;
    }

    /* Another thread is a few instructions away from publishing. */
    while ((slot = __atomic_load_n(&key->slot, __ATOMIC_ACQUIRE)) == XX_TLS_CLAIMED) {
        sched_yield();
    }

    return (slot > 0) ? slot : 0;
}

void *xx_tls_get(xx_tls_key *key) {
    long slot = xx_tls_slot(key);

    return slot ? xx_tls_values[slot - 1] : NULL;
}

bool xx_tls_set(xx_tls_key *key, void *value) {
    long slot = xx_tls_slot(key);

    if (!slot) {
        return false;
    }

    xx_tls_values[slot - 1] = value;

    return true;
}

uintptr_t xx_tls_thread_id(void) {
    /* The table is per thread, so its address names the thread. */
    return (uintptr_t)&xx_tls_values[0];
}

void xx_global_release_thread_slots(void) {
    /* The table is module thread storage, which the loader reclaims. */
}

#endif
