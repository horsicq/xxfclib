/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_main_windows.c
 * @brief mainCRTStartup: the console entry point for programs with
 *        int main(int argc, char **argv) in a CRT-free link.
 *
 * argv comes from GetCommandLineA split with UCRT's rules
 * (xx_crt_args_windows.c); main's result goes to exit(), which runs the
 * atexit functions and flushes the streams. The linker selects this symbol
 * itself when the program defines main.
 */

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>

#include "xx_crt_args_windows.h"
#include "xx_crt_windows_internal.h"

int __cdecl main(int nArgc, char **ppArgv);

/* Runs main in an ordinary call so the stack keeps the alignment compiled
 * code expects after a CALL. */
static int xx_crt_run_main(void)
{
    int nArgc = 0;
    char **ppArgv;
    const char *pCommand = GetCommandLineA();
    char sModule[MAX_PATH + 1];
    xx_crt_clock_start();
    /* An empty command line splits the module path instead, as in UCRT. */
    if (!pCommand || !*pCommand) {
        DWORD nLength = GetModuleFileNameA(NULL, sModule, MAX_PATH);
        sModule[nLength <= MAX_PATH ? nLength : MAX_PATH] = 0;
        pCommand = sModule;
    }
    ppArgv = xx_crt_parse_command_line_a(pCommand, &nArgc);
    if (!ppArgv) return 255;
    return main(nArgc, ppArgv);
}

void __cdecl mainCRTStartup(void)
{
    exit(xx_crt_run_main());
}

#endif /* _WIN32 && _MSC_VER */
