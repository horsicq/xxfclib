/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_die_engine_posix.c
 * @brief POSIX half of the engine platform interface.
 *
 * Apple and Linux are two answers from one file rather than two files, because
 * CMake selects only windows-or-posix; the inner split is the same shape as
 * the ISA fork inside xx_global_cpu.c.
 */

#if !defined(_WIN32)

#include "xx_die_engine_platform.h"

const char *xx_die_engine_platform_os_name(void)
{
#if defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

#endif /* !_WIN32 */
