/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#ifndef XX_SCAN_CONSOLE_H
#define XX_SCAN_CONSOLE_H
#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/scan/xx_scan_color.h"
#include "xxfclib/terminal/xx_terminal.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Same visible text as xx_scan_format_text, with per-record category colors.
 * NULL colors selects the XScanEngine light defaults, mapped to console colors.
 * The caller owns terminal initialization, flushing and restoration. */
XXFC_API xxfc_status_t xx_scan_print_text(xx_scan_result *result, xx_scan_options *options, const xx_terminal_state *terminal, const xx_scan_color_options *colors);
#ifdef __cplusplus
}
#endif
#endif
