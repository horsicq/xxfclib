/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#if defined(_WIN32)

#include "xx_global_platform.h"
#include "../xx_tls.h"
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

xx_terminal_type_t xx_global_platform_detect_terminal_type(bool standard_error)
{
    HANDLE handle = GetStdHandle(standard_error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    if (!handle || handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
        return XX_TERMINAL_TYPE_NONE;
    }

#ifndef _USING_V110_SDK71_
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
        return XX_TERMINAL_TYPE_ANSI;
    }
    if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        SetConsoleMode(handle, mode);
        return XX_TERMINAL_TYPE_ANSI;
    }
#endif
    return XX_TERMINAL_TYPE_WINDOWS;
}

/* ---------------------------------------------------------------- xx_tls --- */

/* Every key that has published a slot, so that unloading a DLL can free the
 * indices (xx_global_release_thread_slots). xxfclib has only a few keys. */
#define XX_TLS_MAX_PUBLISHED 32

static xx_tls_key *volatile xx_tls_published[XX_TLS_MAX_PUBLISHED];
static volatile LONG xx_tls_published_count;

static DWORD xx_tls_slot(xx_tls_key *key)
{
    LONG slot = key->slot;

    if (slot == 0) {
        DWORD index = TlsAlloc();
        LONG previous;

        if (index == TLS_OUT_OF_INDEXES) {
            return TLS_OUT_OF_INDEXES;
        }

        previous = InterlockedCompareExchange((LONG volatile *)&key->slot, (LONG)index + 1, 0);

        if (previous != 0) {
            /* Another thread published a slot for this key first. */
            TlsFree(index);
            slot = previous;
        } else {
            LONG published = InterlockedIncrement(&xx_tls_published_count);

            if (published <= XX_TLS_MAX_PUBLISHED) {
                xx_tls_published[published - 1] = key;
            }

            slot = (LONG)index + 1;
        }
    }

    return (DWORD)(slot - 1);
}

void xx_global_release_thread_slots(void)
{
    LONG count = xx_tls_published_count;
    LONG i;

    if (count > XX_TLS_MAX_PUBLISHED) {
        count = XX_TLS_MAX_PUBLISHED;
    }

    for (i = 0; i < count; i++) {
        xx_tls_key *key = xx_tls_published[i];

        if (key && key->slot) {
            TlsFree((DWORD)(key->slot - 1));
            key->slot = 0;
        }

        xx_tls_published[i] = NULL;
    }

    xx_tls_published_count = 0;
}

void *xx_tls_get(xx_tls_key *key)
{
    DWORD index = xx_tls_slot(key);
    DWORD last_error;
    void *value;

    if (index == TLS_OUT_OF_INDEXES) {
        return NULL;
    }

    /* TlsGetValue clears the thread's last-error code; a caller between an
     * API call and its GetLastError must not lose it. */
    last_error = GetLastError();
    value = TlsGetValue(index);
    SetLastError(last_error);

    return value;
}

bool xx_tls_set(xx_tls_key *key, void *value)
{
    DWORD index = xx_tls_slot(key);

    return (index != TLS_OUT_OF_INDEXES) && TlsSetValue(index, value);
}

uintptr_t xx_tls_thread_id(void)
{
    return (uintptr_t)GetCurrentThreadId();
}

#endif
