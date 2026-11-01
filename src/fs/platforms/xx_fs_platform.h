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
    XX_FS_PLATFORM_DIR = 2,     /**< A directory. */
    XX_FS_PLATFORM_LINK = 3,    /**< A symbolic link, junction or other reparse point (lstat only). */
    XX_FS_PLATFORM_ERROR = 4    /**< Could not be examined: no permission, an I/O error, a name too long (lstat only). */
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
typedef bool (*xx_fs_platform_entry_fn)(void *context, const char *name, bool is_dir);

/**
 * @brief Walk a directory, invoking @p callback once per entry.
 *
 * "." and ".." are filtered out by the implementation. Entries arrive in
 * whatever order the platform supplies; the caller sorts.
 *
 * @return false if the directory could not be opened, or if @p callback
 *         stopped the walk.
 */
bool xx_fs_platform_enumerate(const char *path, xx_fs_platform_entry_fn callback, void *context);

/** @brief The separator this platform writes, '\\' on Windows and '/' elsewhere. */
char xx_fs_platform_separator(void);

/**
 * @brief Classify a path WITHOUT following a final link.
 *
 * A symbolic link, a junction or any other reparse point reports
 * XX_FS_PLATFORM_LINK whatever it points at, so a tree walk built on this
 * never leaves the tree it was asked to walk. Anything that is neither a
 * regular file, a directory nor a link (a device, a FIFO) reports FILE: it is
 * removed with the file call.
 *
 * Unlike @ref xx_fs_platform_stat, MISSING here means the path or one of its
 * folders does not exist (ENOENT/ENOTDIR, ERROR_FILE_NOT_FOUND/
 * ERROR_PATH_NOT_FOUND). Any other failure reports ERROR, so a caller that
 * must not mistake "could not look" for "nothing there" can tell them apart.
 */
xx_fs_platform_kind_t xx_fs_platform_lstat(const char *path);

/**
 * @brief Remove one entry of the kind @p kind reported by
 *        @ref xx_fs_platform_lstat.
 *
 * FILE deletes the file (on Windows a read-only attribute is cleared first),
 * DIR removes an EMPTY directory, LINK removes the link itself and never its
 * target.
 */
bool xx_fs_platform_remove(const char *path, xx_fs_platform_kind_t kind);

/**
 * @brief The walk behind xx_fs_remove_tree (see its contract in xx_fs.h).
 *
 * @p path is non-empty. Trailing separators are dropped first (they would
 * make the final link resolve); a path that is then a file-system or drive
 * root, or that ends in "." or "..", is refused. Directories deeper than
 * @p max_depth below @p path are not entered, and the call fails.
 *
 * POSIX walks with descriptors (openat with O_NOFOLLOW, fstatat and unlinkat
 * relative to the parent's descriptor). Windows walks by path, but opens each
 * directory before entering it, refuses it if the handle shows a reparse
 * point, and keeps that handle (without FILE_SHARE_DELETE) open until its
 * contents are gone, so it cannot be renamed or replaced meanwhile.
 */
bool xx_fs_platform_remove_tree(const char *path, unsigned max_depth);

/** @brief Create one directory. False when it exists or its parent does not. */
bool xx_fs_platform_make_dir(const char *path);

/**
 * @brief Rename a file or directory. Never replaces: false when @p to exists.
 *
 * POSIX has no portable no-replace rename, so there the existence check and
 * the rename are two steps.
 */
bool xx_fs_platform_rename(const char *from, const char *to);

/** @brief The current working directory, xx_rt_malloc'd UTF-8, or NULL. */
char *xx_fs_platform_current_dir(void);

#ifdef __cplusplus
}
#endif

#endif /* XX_FS_PLATFORM_H */
