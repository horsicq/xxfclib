/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_tls.h
 * @brief Internal per-thread pointer slots, without compiler thread-local storage.
 *
 * A __declspec(thread) variable makes the Windows linker emit a PE TLS
 * directory and a .tls section, and it needs the CRT's _tls_index and
 * _tls_used, which a CRT-free consumer would have to supply itself. xxfclib
 * therefore keeps per-thread state in these slots: on Windows a key is a
 * TlsAlloc slot (KERNEL32), elsewhere an index into a small per-thread table.
 * A key is a zero-initialised static object; its slot is created on first
 * use, safely from any number of threads. Nothing is cleaned up when a
 * thread exits, so a value that owns memory should be released by the code
 * that set it. A DLL that contains xxfclib gives the slots back on unload
 * with xx_global_release_thread_slots() (xxfclib/global/xx_global.h).
 */

#ifndef XX_TLS_H
#define XX_TLS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tls_key {
    volatile long slot; /* 0 until first use, then the slot number + 1 */
} xx_tls_key;

/** The calling thread's value for key: NULL until it sets one, or when no slot is available. */
void *xx_tls_get(xx_tls_key *key);

/** Store value for the calling thread. False only when no slot is available. */
bool xx_tls_set(xx_tls_key *key, void *value);

/** Identifies the calling thread for as long as it runs. */
uintptr_t xx_tls_thread_id(void);

#ifdef __cplusplus
}
#endif

#endif /* XX_TLS_H */
