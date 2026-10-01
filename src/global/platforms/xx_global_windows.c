/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#if defined(_WIN32)

#include "xx_global_platform.h"
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

xx_terminal_type_t xx_global_platform_detect_terminal_type(bool standard_error) {
    HANDLE handle = GetStdHandle(standard_error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    if (!handle || handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
        return XX_TERMINAL_TYPE_NONE;
    }

#ifndef _USING_V110_SDK71_
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
        return XX_TERMINAL_TYPE_ANSI;
    }
    if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        SetConsoleMode(handle, mode);
        return XX_TERMINAL_TYPE_ANSI;
    }
#endif
    return XX_TERMINAL_TYPE_WINDOWS;
}

#endif
