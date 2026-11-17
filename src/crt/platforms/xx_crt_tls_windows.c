/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_tls_windows.c
 * @brief The PE thread-local storage directory of a CRT-free link.
 *
 * __declspec(thread) / _Thread_local variables live in the .tls$ section and
 * every access goes through _tls_index, which the loader fills in. The
 * linker points the image's TLS directory at the symbol _tls_used. This
 * unit supplies the three, as described by the PE/COFF specification
 * ("The .tls Section"):
 *  - _tls_start / _tls_end bracket the template data: the compiler's .tls$
 *    contributions sort between .tls and .tls$ZZZ;
 *  - the callback array runs from just after __xl_a (.CRT$XLA) to the null
 *    __xl_z (.CRT$XLZ), so callbacks a program registers in .CRT$XLB..XLY
 *    (xxyara's per-thread error message cleanup) are called on thread and
 *    process attach and detach.
 * Any object that uses a thread-local variable references _tls_index, which
 * pulls this object, and with it the directory, into the link.
 */

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#pragma section(".CRT$XLA", read)
#pragma section(".CRT$XLZ", read)
#pragma section(".rdata$T", read)

unsigned long _tls_index = 0;

#pragma data_seg(".tls")
char _tls_start = 0;
#pragma data_seg(".tls$ZZZ")
char _tls_end = 0;
#pragma data_seg()

__declspec(allocate(".CRT$XLA")) const PIMAGE_TLS_CALLBACK __xl_a = 0;
__declspec(allocate(".CRT$XLZ")) const PIMAGE_TLS_CALLBACK __xl_z = 0;

#if defined(_WIN64)
__declspec(allocate(".rdata$T")) extern const IMAGE_TLS_DIRECTORY64 _tls_used;
__declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY64 _tls_used = {
    (ULONGLONG)(ULONG_PTR)&_tls_start, (ULONGLONG)(ULONG_PTR)&_tls_end, (ULONGLONG)(ULONG_PTR)&_tls_index, (ULONGLONG)(ULONG_PTR)(&__xl_a + 1), 0, 0};
#pragma comment(linker, "/include:_tls_used")
#else
__declspec(allocate(".rdata$T")) extern const IMAGE_TLS_DIRECTORY32 _tls_used;
__declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY32 _tls_used = {
    (DWORD)(ULONG_PTR)&_tls_start, (DWORD)(ULONG_PTR)&_tls_end, (DWORD)(ULONG_PTR)&_tls_index, (DWORD)(ULONG_PTR)(&__xl_a + 1), 0, 0};
#pragma comment(linker, "/include:__tls_used")
#endif

#endif /* _WIN32 && _MSC_VER */
