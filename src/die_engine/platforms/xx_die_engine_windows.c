/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_die_engine_windows.c @brief Win32 half of the engine platform interface. */

#if defined(_WIN32)

#include "xx_die_engine_platform.h"

const char *xx_die_engine_platform_os_name(void)
{
    return "win32";
}

#endif /* _WIN32 */
