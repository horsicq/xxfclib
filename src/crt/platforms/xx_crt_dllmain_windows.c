/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_dllmain_windows.c
 * @brief The default DllMain of a CRT-free DLL.
 *
 * Kept alone in its object file: the linker takes it from the library only
 * when the DLL's own objects define no DllMain.
 */

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD nReason, LPVOID pReserved)
{
    (void)hInstance;
    (void)nReason;
    (void)pReserved;
    return TRUE;
}

#endif /* _WIN32 && _MSC_VER */
