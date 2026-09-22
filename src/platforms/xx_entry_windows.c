/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_entry_windows.c
 * @brief Windows DLL entry point and compiler support routines for CRT-free build.
 */

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

/* DLL entry point. The CRT-free shared build names this as /ENTRY, so it has
 * to exist there -- but only there. Defined unconditionally it also lands in
 * the static archive, and then any consumer that links xxfclib into a DLL of
 * its own and defines no DllMain silently inherits this one, handing xxfclib
 * that DLL's load-time behaviour. XXFC_BUILD_SHARED is set on the shared
 * target and nowhere else. */
#if defined(XXFC_BUILD_SHARED)
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return TRUE;
}
#endif

/* The compiler-support routines below exist only for a CRT-free link, where
 * nothing else supplies them. In a hosted build LIBCMT already defines all
 * four, so emitting them unconditionally makes any link that pulls this object
 * fail with LNK2005. XXFC_NO_CRT is defined by the build for the /NODEFAULTLIB
 * configuration and nowhere else. */
#if defined(_MSC_VER) && defined(XXFC_NO_CRT)

/* Keep compiler from replacing explicit calls with intrinsics */
#pragma function(memset, memcpy, memmove)

/* Floating-point support marker required by MSVC */
int _fltused = 0x9875;

/* Turn off optimization to prevent compiler from rewriting loops into recursive calls to memset/memcpy */
#pragma optimize("", off)

void *memset(void *dst, int val, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char b = (unsigned char)val;
    while (n--) {
        *d++ = b;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s || d >= s + n) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

#pragma optimize("", on)

#endif /* _MSC_VER && XXFC_NO_CRT */

#endif /* _WIN32 */
