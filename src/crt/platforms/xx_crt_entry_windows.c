/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_entry_windows.c
 * @brief Process termination for the standard-name C runtime: atexit,
 *        exit, _exit, abort, assert's _wassert, and the _fltused marker.
 *
 * The program and DLL entry points live in their own units
 * (xx_crt_main_windows.c, xx_crt_wmain_windows.c, xx_crt_dll_windows.c) so
 * that a link pulls in only the one it uses.
 *
 * As in UCRT: exit() runs the atexit functions last-registered-first, then
 * flushes every stream; abort() ends the process with the fail-fast code
 * 0xC0000409 (FAST_FAIL_FATAL_APP_EXIT) without flushing; a failed assert
 * prints "Assertion failed: <expression>, file <file>, line <n>" to stderr
 * before aborting.
 */

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>

#include "xx_crt_windows_internal.h"

/* MSVC references this from every object that uses floating point. */
int _fltused = 0x9875;

typedef void(__cdecl *xx_crt_exit_fn)(void);

static xx_crt_exit_fn *s_xx_crt_exit_fns;
static size_t s_xx_crt_exit_count;
static size_t s_xx_crt_exit_capacity;
static SRWLOCK s_xx_crt_exit_lock = SRWLOCK_INIT;

int __cdecl atexit(void(__cdecl *fnFunction)(void))
{
    int nResult = 0;
    if (!fnFunction) return -1;
    AcquireSRWLockExclusive(&s_xx_crt_exit_lock);
    if (s_xx_crt_exit_count == s_xx_crt_exit_capacity) {
        size_t nCapacity = s_xx_crt_exit_capacity ? s_xx_crt_exit_capacity * 2 : 32;
        xx_crt_exit_fn *pGrown = (xx_crt_exit_fn *)realloc((void *)s_xx_crt_exit_fns, nCapacity * sizeof(xx_crt_exit_fn));
        if (!pGrown) {
            nResult = -1;
        } else {
            s_xx_crt_exit_fns = pGrown;
            s_xx_crt_exit_capacity = nCapacity;
        }
    }
    if (nResult == 0) s_xx_crt_exit_fns[s_xx_crt_exit_count++] = fnFunction;
    ReleaseSRWLockExclusive(&s_xx_crt_exit_lock);
    return nResult;
}

void xx_crt_run_exit_handlers(void)
{
    for (;;) {
        xx_crt_exit_fn fnFunction = NULL;
        AcquireSRWLockExclusive(&s_xx_crt_exit_lock);
        if (s_xx_crt_exit_count) fnFunction = s_xx_crt_exit_fns[--s_xx_crt_exit_count];
        ReleaseSRWLockExclusive(&s_xx_crt_exit_lock);
        if (!fnFunction) break;
        fnFunction();
    }
    xx_crt_flush_all();
}

void xx_crt_exit_shutdown(void)
{
    AcquireSRWLockExclusive(&s_xx_crt_exit_lock);
    free((void *)s_xx_crt_exit_fns);
    s_xx_crt_exit_fns = NULL;
    s_xx_crt_exit_count = 0;
    s_xx_crt_exit_capacity = 0;
    ReleaseSRWLockExclusive(&s_xx_crt_exit_lock);
}

__declspec(noreturn) void __cdecl exit(int nCode)
{
    xx_crt_run_exit_handlers();
    ExitProcess((UINT)nCode);
}

__declspec(noreturn) void __cdecl _exit(int nCode)
{
    ExitProcess((UINT)nCode);
}

__declspec(noreturn) void __cdecl _Exit(int nCode)
{
    ExitProcess((UINT)nCode);
}

__declspec(noreturn) void __cdecl abort(void)
{
    __fastfail(7); /* FAST_FAIL_FATAL_APP_EXIT, what UCRT's abort reports */
}

void __cdecl _wassert(const wchar_t *pMessage, const wchar_t *pFile, unsigned nLine)
{
    fprintf(stderr, "Assertion failed: %ls, file %ls, line %u\n", pMessage, pFile, nLine);
    abort();
}

#endif /* _WIN32 && _MSC_VER */
