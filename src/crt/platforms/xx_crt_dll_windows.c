/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_dll_windows.c
 * @brief _DllMainCRTStartup: the DLL entry point of a CRT-free link
 *        (xxyara.dll).
 *
 * It calls the DLL's DllMain (xx_crt_dllmain_windows.c supplies a default
 * that the linker uses only when the DLL defines none) and, when the DLL is
 * unloaded with FreeLibrary, runs its atexit functions and flushes its
 * streams, as the static UCRT does for a DLL. At process exit only the
 * streams are flushed: other threads are already gone.
 */

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "xx_crt_windows_internal.h"

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD nReason, LPVOID pReserved);

BOOL WINAPI _DllMainCRTStartup(HINSTANCE hInstance, DWORD nReason, LPVOID pReserved)
{
    BOOL bResult;
    if (nReason == DLL_PROCESS_ATTACH) xx_crt_clock_start();
    bResult = DllMain(hInstance, nReason, pReserved);
    if (nReason == DLL_PROCESS_DETACH) {
        if (pReserved == NULL) {
            /* FreeLibrary: the process goes on, so nothing may stay behind:
             * run the atexit functions, close the streams still open (a
             * tmpfile goes away with its handle) and free the runtime's
             * tables */
            xx_crt_run_exit_handlers();
            xx_crt_stdio_shutdown();
            xx_crt_env_shutdown();
            xx_crt_exit_shutdown();
        } else {
            /* process exit: only make sure buffered output is written */
            xx_crt_flush_all();
        }
    }
    return bResult;
}

#endif /* _WIN32 && _MSC_VER */
