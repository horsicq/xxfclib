/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* VCRUNTIME140.dll replacement for hosted MSVC executables.
 *
 * A /MD program imports a handful of memory and string routines from
 * VCRUNTIME140.dll (the Visual C++ redistributable, not part of Windows),
 * while everything else comes from the UCRT, which ships with Windows 10 and
 * later. Linking this object into the executable defines those routines on
 * top of the xxfclib runtime (xx_rt_*), so the image no longer needs
 * VCRUNTIME140.dll. It is built only as the xxfclib::vcruntime_free OBJECT
 * target: an object file always wins over the import library, which a static
 * archive member would not.
 *
 * The routines here must not be rewritten by the compiler into calls of
 * themselves: they only forward to xx_rt_* (memory: xx_memory_rt.c, whose
 * scalar paths use volatile accesses; strings: xx_string_windows.c) or use
 * plain loops in functions the compiler never pattern-matches (wcschr,
 * wcsrchr). Keep it that way.
 *
 * The SEH entries (__C_specific_handler, __current_exception,
 * __current_exception_context) are supplied for x64 and ARM64 at the end of
 * this file. 32-bit x86 uses other handlers (_except_handler4_common) and is
 * not covered. */

#if defined(_MSC_VER) && defined(_WIN32)

#include <stddef.h>
#include "xxfclib/rt/xx_rt.h"

#pragma function(memset, memcpy, memmove, memcmp, memchr)

void *__cdecl memcpy(void *destination, const void *source, size_t size)
{
    return xx_rt_memcpy(destination, source, size);
}

void *__cdecl memmove(void *destination, const void *source, size_t size)
{
    return xx_rt_memmove(destination, source, size);
}

void *__cdecl memset(void *destination, int value, size_t size)
{
    return xx_rt_memset(destination, value, size);
}

int __cdecl memcmp(const void *first, const void *second, size_t size)
{
    return xx_rt_memcmp(first, second, size);
}

void *__cdecl memchr(const void *source, int value, size_t size)
{
    return xx_rt_memchr(source, value, size);
}

char *__cdecl strchr(const char *string, int character)
{
    return xx_rt_strchr(string, character);
}

char *__cdecl strrchr(const char *string, int character)
{
    return xx_rt_strrchr(string, character);
}

char *__cdecl strstr(const char *haystack, const char *needle)
{
    return xx_rt_strstr(haystack, needle);
}

wchar_t *__cdecl wcschr(const wchar_t *string, wchar_t character)
{
    return xx_rt_wcschr(string, character);
}

wchar_t *__cdecl wcsrchr(const wchar_t *string, wchar_t character)
{
    const wchar_t *found = NULL;
    for (;; ++string) {
        if (*string == character) found = string;
        if (*string == L'\0') return (wchar_t *)found;
    }
}

#if defined(_M_X64) || defined(_M_ARM64)
/* Structured exception handling. The CRT startup (__scrt_common_main_seh)
 * itself uses __try/__except, so every /MD executable needs the language
 * handler. ntdll.dll exports the same routine on x64 and ARM64; it is looked
 * up at run time (on the first exception that reaches a __try frame) rather
 * than through ntdll.lib, because ntdll.lib would sit before the UCRT on the
 * link line and could resolve sprintf/strlen/... to ntdll's reduced copies.
 * Minimal prototypes instead of <windows.h>, whose excpt.h declares
 * __C_specific_handler as an import. */
__declspec(dllimport) void *__stdcall GetModuleHandleW(const wchar_t *module_name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *proc_name);

typedef int(__cdecl *xx_vcrt_c_specific_handler)(void *, void *, void *, void *);

int __cdecl __C_specific_handler(void *exception_record, void *establisher_frame, void *context_record, void *dispatcher_context)
{
    static xx_vcrt_c_specific_handler volatile cached = NULL; /* every thread stores the same value */
    xx_vcrt_c_specific_handler handler = cached;
    if (!handler) {
        void *ntdll = GetModuleHandleW(L"ntdll.dll"); /* mapped into every process */
        handler = ntdll ? (xx_vcrt_c_specific_handler)GetProcAddress(ntdll, "__C_specific_handler") : NULL;
        if (!handler) return 1; /* ExceptionContinueSearch */
        cached = handler;
    }
    return handler(exception_record, establisher_frame, context_record, dispatcher_context);
}

/* The C++ exception currently being handled, per thread. A C program never
 * throws C++ exceptions, so these slots stay empty; the CRT startup only
 * reads them. */
static __declspec(thread) void *xx_vcrt_exception;
static __declspec(thread) void *xx_vcrt_exception_context;

void **__cdecl __current_exception(void)
{
    return &xx_vcrt_exception;
}

void **__cdecl __current_exception_context(void)
{
    return &xx_vcrt_exception_context;
}
#endif /* _M_X64 || _M_ARM64 */

#endif /* _MSC_VER && _WIN32 */
