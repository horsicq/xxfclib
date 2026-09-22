/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_fs_platform.h
 * @brief Internal platform interface for filesystem queries and enumeration.
 *
 * Implemented by xx_fs_windows.c and xx_fs_posix.c, exactly one of which is
 * compiled into the library. xx_fs.c calls only these and contains no
 * platform conditional of its own.
 */

#ifndef XX_FS_PLATFORM_H
#define XX_FS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What @ref xx_fs_platform_stat reports about a path. */
typedef enum xx_fs_platform_kind_e {
    XX_FS_PLATFORM_MISSING = 0, /**< Nothing exists at the path. */
    XX_FS_PLATFORM_FILE = 1,    /**< A regular file. */
    XX_FS_PLATFORM_DIR = 2      /**< A directory. */
} xx_fs_platform_kind_t;

/**
 * @brief Classify a path in one call.
 *
 * One entry point rather than three separate exists/is_file/is_dir probes,
 * because each of those costs a system call and the callers want the
 * distinction anyway.
 */
xx_fs_platform_kind_t xx_fs_platform_stat(const char *path);

/**
 * @brief Callback invoked once per directory entry by
 *        @ref xx_fs_platform_enumerate.
 *
 * @param context Opaque value handed to the enumerator.
 * @param name    Entry name, without any directory part. Never "." or "..".
 * @param is_dir  True when the entry is a directory.
 * @return false to stop the walk and make the enumerator report failure.
 */
typedef bool (*xx_fs_platform_entry_fn)(void *context, const char *name,
                                        bool is_dir);

/**
 * @brief Walk a directory, invoking @p callback once per entry.
 *
 * "." and ".." are filtered out by the implementation. Entries arrive in
 * whatever order the platform supplies; the caller sorts.
 *
 * @return false if the directory could not be opened, or if @p callback
 *         stopped the walk.
 */
bool xx_fs_platform_enumerate(const char *path,
                              xx_fs_platform_entry_fn callback,
                              void *context);

/** @brief The separator this platform writes, '\\' on Windows and '/' elsewhere. */
char xx_fs_platform_separator(void);

#ifdef __cplusplus
}
#endif

#endif /* XX_FS_PLATFORM_H */
