/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_threads_windows.c
 * @brief Windows thread backend using KERNEL32 only.
 *
 * CreateThread rather than _beginthreadex, so the CRT-free builds link.
 * Mutexes are critical sections and semaphores are kernel semaphores, both
 * available on Windows XP. Condition variables and processor groups arrived
 * later and are looked up at run time.
 */

#if defined(_WIN32)

#include "xx_threads_platform.h"
#include "xxfclib/memory/xx_memory.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* ------------------------------------------------------------- threads --- */

#ifndef STACK_SIZE_PARAM_IS_A_RESERVATION
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#endif

static DWORD WINAPI xx_threads_windows_start(LPVOID parameter)
{
    xx_threads_run((xx_thread_t *)parameter);

    return 0;
}

bool xx_threads_platform_start(xx_thread_t *thread, size_t stack_size)
{
    DWORD flags = stack_size ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0;
    HANDLE handle;

    if ((SIZE_T)stack_size != stack_size) {
        return false;
    }

    handle = CreateThread(NULL, (SIZE_T)stack_size, xx_threads_windows_start, thread, flags, NULL);

    if (!handle) {
        return false;
    }

    thread->native = handle;

    return true;
}

bool xx_threads_platform_join(xx_thread_t *thread)
{
    if (WaitForSingleObject((HANDLE)thread->native, INFINITE) != WAIT_OBJECT_0) {
        return false;
    }

    CloseHandle((HANDLE)thread->native);
    thread->native = NULL;

    return true;
}

void xx_threads_platform_detach(xx_thread_t *thread)
{
    CloseHandle((HANDLE)thread->native);
    thread->native = NULL;
}

uint64_t xx_thread_current_id(void)
{
    return (uint64_t)GetCurrentThreadId();
}

void xx_thread_yield(void)
{
    if (!SwitchToThread()) {
        Sleep(0);
    }
}

void xx_thread_sleep(uint32_t milliseconds)
{
    /* INFINITE would never return; the caller asked for a finite sleep. */
    Sleep((milliseconds == INFINITE) ? (INFINITE - 1) : milliseconds);
}

/* ------------------------------------------- run-time KERNEL32 lookups --- */

/* CONDITION_VARIABLE is one pointer; declaring it here keeps the file
 * compiling against the XP SDK, whose headers lack it. */
typedef struct xx_threads_windows_cv {
    void *ptr;
} xx_threads_windows_cv;

typedef VOID(WINAPI *xx_init_cv_fn)(xx_threads_windows_cv *cv);
typedef BOOL(WINAPI *xx_sleep_cv_cs_fn)(xx_threads_windows_cv *cv, CRITICAL_SECTION *cs, DWORD milliseconds);
typedef VOID(WINAPI *xx_wake_cv_fn)(xx_threads_windows_cv *cv);
typedef DWORD(WINAPI *xx_active_processor_count_fn)(WORD group);

static struct {
    xx_init_cv_fn init_cv;
    xx_sleep_cv_cs_fn sleep_cv_cs;
    xx_wake_cv_fn wake_cv;
    xx_wake_cv_fn wake_all_cv;
    xx_active_processor_count_fn active_processor_count;
} xx_threads_kernel32;

static xx_once_t xx_threads_kernel32_once = XX_ONCE_INIT;

static void xx_threads_windows_resolve(void *arg)
{
    HMODULE module = GetModuleHandleW(L"kernel32.dll");
    xx_init_cv_fn init_cv;
    xx_sleep_cv_cs_fn sleep_cv_cs;
    xx_wake_cv_fn wake_cv;
    xx_wake_cv_fn wake_all_cv;

    (void)arg;

    if (!module) {
        return;
    }

    init_cv = (xx_init_cv_fn)(void (*)(void))GetProcAddress(module, "InitializeConditionVariable");
    sleep_cv_cs = (xx_sleep_cv_cs_fn)(void (*)(void))GetProcAddress(module, "SleepConditionVariableCS");
    wake_cv = (xx_wake_cv_fn)(void (*)(void))GetProcAddress(module, "WakeConditionVariable");
    wake_all_cv = (xx_wake_cv_fn)(void (*)(void))GetProcAddress(module, "WakeAllConditionVariable");

    /* All four or none, so xx_cond needs only one test. */
    if (init_cv && sleep_cv_cs && wake_cv && wake_all_cv) {
        xx_threads_kernel32.init_cv = init_cv;
        xx_threads_kernel32.sleep_cv_cs = sleep_cv_cs;
        xx_threads_kernel32.wake_cv = wake_cv;
        xx_threads_kernel32.wake_all_cv = wake_all_cv;
    }

    xx_threads_kernel32.active_processor_count = (xx_active_processor_count_fn)(void (*)(void))GetProcAddress(module, "GetActiveProcessorCount");
}

uint32_t xx_thread_cpu_count(void)
{
    SYSTEM_INFO info;
    DWORD count = 0;

    xx_once(&xx_threads_kernel32_once, xx_threads_windows_resolve, NULL);

    /* GetSystemInfo sees only the caller's processor group (at most 64). */
    if (xx_threads_kernel32.active_processor_count) {
        count = xx_threads_kernel32.active_processor_count(0xFFFF /* ALL_PROCESSOR_GROUPS */);
    }

    if (!count) {
        GetSystemInfo(&info);
        count = info.dwNumberOfProcessors;
    }

    return count ? (uint32_t)count : 1;
}

/* ------------------------------------------------------------- mutexes --- */

struct xx_mutex_s {
    CRITICAL_SECTION section;
};

xx_mutex_t *xx_mutex_create(void)
{
    xx_mutex_t *mutex = (xx_mutex_t *)xx_mem_calloc(1, sizeof(*mutex));

    if (!mutex) {
        return NULL;
    }

    /* A short spin avoids a kernel transition for briefly held locks. */
    if (!InitializeCriticalSectionAndSpinCount(&mutex->section, 1000)) {
        xx_mem_free(mutex);
        return NULL;
    }

    return mutex;
}

void xx_mutex_destroy(xx_mutex_t *mutex)
{
    if (mutex) {
        DeleteCriticalSection(&mutex->section);
        xx_mem_free(mutex);
    }
}

void xx_mutex_lock(xx_mutex_t *mutex)
{
    if (mutex) {
        EnterCriticalSection(&mutex->section);
    }
}

bool xx_mutex_trylock(xx_mutex_t *mutex)
{
    return mutex && TryEnterCriticalSection(&mutex->section);
}

void xx_mutex_unlock(xx_mutex_t *mutex)
{
    if (mutex) {
        LeaveCriticalSection(&mutex->section);
    }
}

/* -------------------------------------------------- condition variables --- */

struct xx_cond_s {
    xx_threads_windows_cv cv;
};

xx_cond_t *xx_cond_create(void)
{
    xx_cond_t *cond;

    xx_once(&xx_threads_kernel32_once, xx_threads_windows_resolve, NULL);

    if (!xx_threads_kernel32.init_cv) {
        return NULL;
    }

    cond = (xx_cond_t *)xx_mem_calloc(1, sizeof(*cond));

    if (cond) {
        xx_threads_kernel32.init_cv(&cond->cv);
    }

    return cond;
}

void xx_cond_destroy(xx_cond_t *cond)
{
    /* Windows condition variables own no resources. */
    xx_mem_free(cond);
}

xx_wait_result_t xx_cond_wait(xx_cond_t *cond, xx_mutex_t *mutex, uint32_t timeout_ms)
{
    if (!cond || !mutex) {
        return XX_WAIT_ERROR;
    }

    if (xx_threads_kernel32.sleep_cv_cs(&cond->cv, &mutex->section, (DWORD)timeout_ms)) {
        return XX_WAIT_OK;
    }

    return (GetLastError() == ERROR_TIMEOUT) ? XX_WAIT_TIMEOUT : XX_WAIT_ERROR;
}

void xx_cond_signal(xx_cond_t *cond)
{
    if (cond) {
        xx_threads_kernel32.wake_cv(&cond->cv);
    }
}

void xx_cond_broadcast(xx_cond_t *cond)
{
    if (cond) {
        xx_threads_kernel32.wake_all_cv(&cond->cv);
    }
}

/* ---------------------------------------------------------- semaphores --- */

struct xx_semaphore_s {
    HANDLE handle;
};

xx_semaphore_t *xx_semaphore_create(uint32_t initial_count)
{
    xx_semaphore_t *semaphore;

    if (initial_count > 0x7FFFFFFFu) {
        return NULL;
    }

    semaphore = (xx_semaphore_t *)xx_mem_calloc(1, sizeof(*semaphore));

    if (!semaphore) {
        return NULL;
    }

    semaphore->handle = CreateSemaphoreW(NULL, (LONG)initial_count, 0x7FFFFFFF, NULL);

    if (!semaphore->handle) {
        xx_mem_free(semaphore);
        return NULL;
    }

    return semaphore;
}

void xx_semaphore_destroy(xx_semaphore_t *semaphore)
{
    if (semaphore) {
        CloseHandle(semaphore->handle);
        xx_mem_free(semaphore);
    }
}

xx_wait_result_t xx_semaphore_wait(xx_semaphore_t *semaphore, uint32_t timeout_ms)
{
    if (!semaphore) {
        return XX_WAIT_ERROR;
    }

    switch (WaitForSingleObject(semaphore->handle, (DWORD)timeout_ms)) {
        case WAIT_OBJECT_0: return XX_WAIT_OK;
        case WAIT_TIMEOUT: return XX_WAIT_TIMEOUT;
        default: return XX_WAIT_ERROR;
    }
}

bool xx_semaphore_post(xx_semaphore_t *semaphore, uint32_t count)
{
    if (!semaphore || (count == 0) || (count > 0x7FFFFFFFu)) {
        return false;
    }

    /* Fails without changing the count when it would pass the maximum. */
    return ReleaseSemaphore(semaphore->handle, (LONG)count, NULL) != FALSE;
}

/* ------------------------------------------------------------- atomics --- */

/* The Interlocked functions are full barriers on every Windows target. A
 * plain volatile read is not an acquire on ARM64, so loads use a no-op CAS. */

long xx_atomic_load(volatile long *value)
{
    return InterlockedCompareExchange(value, 0, 0);
}

void xx_atomic_store(volatile long *value, long desired)
{
    InterlockedExchange(value, desired);
}

long xx_atomic_exchange(volatile long *value, long desired)
{
    return InterlockedExchange(value, desired);
}

long xx_atomic_add(volatile long *value, long delta)
{
    return InterlockedExchangeAdd(value, delta);
}

long xx_atomic_compare_exchange(volatile long *value, long expected, long desired)
{
    return InterlockedCompareExchange(value, desired, expected);
}

void *xx_atomic_load_ptr(void *volatile *value)
{
    return InterlockedCompareExchangePointer(value, NULL, NULL);
}

void xx_atomic_store_ptr(void *volatile *value, void *desired)
{
    InterlockedExchangePointer(value, desired);
}

void *xx_atomic_exchange_ptr(void *volatile *value, void *desired)
{
    return InterlockedExchangePointer(value, desired);
}

void *xx_atomic_compare_exchange_ptr(void *volatile *value, void *expected, void *desired)
{
    return InterlockedCompareExchangePointer(value, desired, expected);
}

#endif
