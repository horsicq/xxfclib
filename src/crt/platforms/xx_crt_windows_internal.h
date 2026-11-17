/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_windows_internal.h
 * @brief Shared helpers of the Windows standard-name C runtime units
 *        (internal).
 */

#ifndef XX_CRT_WINDOWS_INTERNAL_H
#define XX_CRT_WINDOWS_INTERNAL_H

#include <wchar.h>

/* Narrow <-> UTF-16 in the code page UCRT uses for narrow file and
 * environment names: the ANSI code page, or the OEM one after
 * SetFileApisToOEM. The results are allocated with malloc. */
wchar_t *xx_crt_narrow_to_wide(const char *pText);
char *xx_crt_wide_to_narrow(const wchar_t *pText);

/* Flushes every open stream; 0 or EOF. */
int xx_crt_flush_all(void);

/* Runs the atexit functions (last registered first) and flushes the
 * streams; used by exit() and by the DLL detach. */
void xx_crt_run_exit_handlers(void);

/* Release the runtime's own resources when a DLL that contains it is
 * unloaded: open streams and stream buffers, the environment table, the
 * atexit list. */
void xx_crt_stdio_shutdown(void);
void xx_crt_env_shutdown(void);
void xx_crt_exit_shutdown(void);

/* Starts the clock() epoch (called by the entry points). */
void xx_crt_clock_start(void);

#endif /* XX_CRT_WINDOWS_INTERNAL_H */
