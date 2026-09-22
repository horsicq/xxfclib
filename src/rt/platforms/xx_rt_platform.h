/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_rt_platform.h
 * @brief Internal platform interface for the runtime layer.
 *
 * Almost every xx_rt_ entry point is defined directly in xx_rt_windows.c or
 * xx_rt_posix.c under its public name, so this header carries only the symbol
 * that has to cross the boundary in the other direction.
 *
 * xx_rt_vfprintf_stream is portable and stays in xx_rt.c, but the byte-level
 * write beneath it is not: Windows writes through a HANDLE with WriteFile,
 * POSIX through a FILE * with fwrite. That single call is what this declares.
 */

#ifndef XX_RT_PLATFORM_H
#define XX_RT_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write @p size bytes to a stream handle obtained from
 *        xx_rt_stdout() or xx_rt_stderr().
 *
 * @return The number of bytes written, or a negative value on failure.
 */
int xx_rt_platform_write_stream(void *stream, const char *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XX_RT_PLATFORM_H */
