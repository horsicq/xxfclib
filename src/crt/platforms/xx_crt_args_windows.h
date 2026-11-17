/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_args_windows.h
 * @brief UCRT-compatible command-line splitting (internal; see
 *        xx_crt_args_windows.c).
 */

#ifndef XX_CRT_ARGS_WINDOWS_H
#define XX_CRT_ARGS_WINDOWS_H

#include <wchar.h>

/* argv for a command line, NULL-terminated, in one malloc block. */
wchar_t **xx_crt_parse_command_line_w(const wchar_t *pCommand, int *pnArgc);
char **xx_crt_parse_command_line_a(const char *pCommand, int *pnArgc);

/* The narrow parser for an explicit code page (xx_crt_parse_command_line_a
 * uses the ANSI code page, CP_ACP): double-byte characters of that code page
 * stay together. */
char **xx_crt_parse_command_line_a_cp(const char *pCommand, int *pnArgc, unsigned nCodePage);

#endif /* XX_CRT_ARGS_WINDOWS_H */
