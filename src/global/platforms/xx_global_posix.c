/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32)

#include "xx_global_platform.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

xx_terminal_type_t xx_global_platform_detect_terminal_type(bool standard_error) {
    const char *term;

    if (!isatty(standard_error ? STDERR_FILENO : STDOUT_FILENO)) {
        return XX_TERMINAL_TYPE_NONE;
    }
    term = getenv("TERM");
    if (term && (!term[0] || strcmp(term, "dumb") == 0)) {
        return XX_TERMINAL_TYPE_NONE;
    }
    return XX_TERMINAL_TYPE_ANSI;
}

#endif
