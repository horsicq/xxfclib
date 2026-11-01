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
 * @file xx_fs.h
 * @brief Filesystem queries, directory enumeration and path manipulation.
 *
 * This sits above @ref xx_io.h rather than beside it. xx_io deals in devices -
 * an open stream of bytes that may be a file, a memory block or a slice of
 * another device. xx_fs deals in the names those files have and the
 * directories they live in, which a device abstraction deliberately says
 * nothing about.
 *
 * Reading a whole file is the one place the two meet: @ref xx_fs_read_file is
 * implemented on top of a file device, so there is a single code path for
 * opening, sizing and reading a file in the whole library.
 *
 * Everything here is built on the xx_rt_ layer and the Win32 / POSIX directory
 * APIs, so it is available in a build that links without a C runtime.
 *
 * @par Ownership
 * Every function returning `char *` returns a heap allocation the caller owns.
 * Release it with xx_str_free(). Functions return NULL on failure rather than
 * terminating.
 *
 * @par Provenance
 * Adapted from cdie's cd_fs.c (github.com/horsicq, MIT). The file-reading path
 * was rewired onto xx_io, and directory listings use xx_list_t.
 */

#ifndef XXFCLIB_XX_FS_H
#define XXFCLIB_XX_FS_H

#include "xxfclib/list/xx_list.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a directory entry is. */
typedef enum xx_fs_entry_type_e {
    XX_FS_ENTRY_FILE = 0, /**< A regular file. */
    XX_FS_ENTRY_DIR = 1   /**< A directory. */
} xx_fs_entry_type_t;

/**
 * @brief One entry from @ref xx_fs_list_dir.
 *
 * Both strings are owned by the entry. Release an entry with
 * @ref xx_fs_entry_free.
 */
typedef struct xx_fs_entry_s {
    char *name;              /**< Entry name, without any directory part. */
    char *path;              /**< Full path, the listed directory joined with @c name. */
    xx_fs_entry_type_t type; /**< File or directory. */
} xx_fs_entry_t;

/* --------------------------------------------------------------- queries */

/** @brief True when @p path names something that exists. */
XXFC_API bool xx_fs_exists(const char *path);

/** @brief True when @p path names a directory. */
XXFC_API bool xx_fs_is_dir(const char *path);

/** @brief True when @p path names a regular file. */
XXFC_API bool xx_fs_is_file(const char *path);

/**
 * @brief Read a whole file into one allocation.
 *
 * Opens @p path as a file device and reads it in full. The result carries a
 * zero byte past the end that is not counted in @p size, so a file holding
 * text can be used as a C string without copying. The content itself is
 * binary-safe and may contain zero bytes.
 *
 * @param path File to read.
 * @param size Receives the byte count, excluding the added terminator. May be
 *             NULL.
 * @return The bytes, owned by the caller, or NULL if the file could not be
 *         opened, sized or read. Release with xx_str_free().
 */
XXFC_API char *xx_fs_read_file(const char *path, int64_t *size);

/* ----------------------------------------------------------- directories */

/** @brief Release an entry produced by @ref xx_fs_list_dir. */
XXFC_API void xx_fs_entry_free(xx_fs_entry_t *entry);

/**
 * @brief List a directory, one @ref xx_fs_entry_t pointer per entry.
 *
 * "." and ".." are skipped. Entries come back sorted DIRECTORIES FIRST and
 * then by name, so a listing is reproducible across platforms whose native
 * enumeration order differs. Note the directories-first half: a caller that
 * wants plain name order -- because it prints the entries, or interleaves a
 * recursive walk at the point each subdirectory's name falls -- has to re-sort.
 * @p list must be a list of `xx_fs_entry_t *`,
 * that is `elem_size == sizeof(xx_fs_entry_t *)`; entries are appended to
 * whatever it already holds.
 *
 * The caller owns the entries. Release each with @ref xx_fs_entry_free before
 * destroying the list, unless the list was created with an element-free
 * function that does it.
 *
 * @return false if @p path could not be enumerated. Entries appended before
 *         the failure remain in @p list.
 */
XXFC_API bool xx_fs_list_dir(const char *path, xx_list_t *list);

/** @brief How @ref xx_fs_list_dir_sorted orders what it appends. */
typedef enum xx_fs_sort_e {
    /** Directories first, then by name -- what xx_fs_list_dir gives. */
    XX_FS_SORT_DIRS_FIRST = 0,
    /** By name alone, directories and files interleaved. */
    XX_FS_SORT_NAME
} xx_fs_sort_t;

/**
 * @brief As @ref xx_fs_list_dir, with the ordering chosen by the caller.
 *
 * XX_FS_SORT_NAME exists because "directories first" is a presentation
 * choice, not a property of the directory: a caller that walks a tree and
 * wants each subdirectory visited at the point its name falls, or that loads
 * files in name order and must keep doing so, needs plain name order. The
 * DIE signature database is loaded that way, and the order decides which of
 * two scripts claiming the same file wins.
 */
XXFC_API bool xx_fs_list_dir_sorted(const char *path, xx_list_t *list, xx_fs_sort_t order);

/**
 * @brief Append the path of every file under @p path to @p list.
 *
 * @param path      Directory to walk, or a single file.
 * @param list      List of `char *`, that is `elem_size == sizeof(char *)`.
 * @param recursive Descend into subdirectories.
 * @return false if @p path could not be walked.
 */
XXFC_API bool xx_fs_find_files(const char *path, xx_list_t *list, bool recursive);

/* ------------------------------------------------------------- mutation */

/**
 * @brief Create one directory.
 *
 * Exclusive: false when anything already exists at @p path, or when its parent
 * directory is missing. That makes it usable as a claim -- two processes asking
 * for the same name cannot both succeed. For "create with parents, existing is
 * fine" use xx_io_create_dirs_a().
 */
XXFC_API bool xx_fs_make_dir(const char *path);

/**
 * @brief Rename (move) a file or a directory.
 *
 * Never replaces anything: false when something exists at @p to. Both paths
 * must be on the same volume; a directory is never copied. On POSIX the
 * existence check and the rename are two steps, so a name created by another
 * process between them can still be replaced if it is a file or an empty
 * directory.
 */
XXFC_API bool xx_fs_rename(const char *from, const char *to);

/**
 * @brief Delete a file, or a directory and everything below it.
 *
 * Links are never followed: a symbolic link, a junction or any other reparse
 * point met at any depth -- including @p path itself, also when it is given
 * with trailing separators, which are ignored -- is removed as a link and its
 * target is left untouched. The folders leading to @p path are resolved as
 * usual; only what @p path names, and what is below it, is protected.
 *
 * That holds while other processes change the tree during the walk, too:
 * - POSIX: everything below @p path is reached through directory descriptors
 *   (openat with O_NOFOLLOW, fstatat, unlinkat), never by a path, so a
 *   directory renamed away and replaced by a link mid-walk is not entered.
 *   One descriptor is held per level being emptied.
 * - Windows: the walk goes by path, but each directory is opened first
 *   without following a reparse point, refused if the handle shows one, and
 *   held without delete sharing until its contents are gone, so it can be
 *   neither renamed nor replaced while paths below it are used. A directory
 *   that something else holds open with delete access cannot be held, and
 *   makes the call fail.
 * A link swapped in for a file or a link is removed itself; a directory that
 * turns into anything else between the look and the descent makes the call
 * fail rather than be entered.
 *
 * Refused (false, nothing touched): NULL, "", a root ("/", "C:\", "C:",
 * "\"), and a path whose final component is "." or "..". On Windows
 * read-only attributes are cleared on the way; POSIX permissions are not
 * changed. The walk is depth-limited (256 levels) so a hostile tree cannot
 * exhaust the stack.
 *
 * @return true when nothing is left at @p path (also when nothing was there to
 *         begin with); false if any entry could not be removed, or if @p path
 *         or anything below it could not be examined (no permission, an I/O
 *         error) -- in which case whatever could be removed has been.
 */
XXFC_API bool xx_fs_remove_tree(const char *path);

/**
 * @brief The current working directory, or NULL.
 *
 * Release with xx_str_free().
 */
XXFC_API char *xx_fs_current_dir(void);

/* ----------------------------------------------------------------- paths */

/** @brief True for '/' and, on any platform, '\\'. */
XXFC_API bool xx_fs_is_separator(char c);

/**
 * @brief Join two path fragments, inserting a separator when needed.
 *
 * A NULL fragment contributes nothing, so joining onto an empty left side
 * yields the right side unchanged.
 */
XXFC_API char *xx_fs_path_join(const char *left, const char *right);

/** @brief The directory part, without a trailing separator. "" if there is none. */
XXFC_API char *xx_fs_path_dir(const char *path);

/** @brief The final component, directory part removed. */
XXFC_API char *xx_fs_path_file_name(const char *path);

/** @brief The final component with its last suffix removed. */
XXFC_API char *xx_fs_path_base_name(const char *path);

/** @brief Text after the LAST '.' of the final component, or "". */
XXFC_API char *xx_fs_path_suffix(const char *path);

/** @brief Text after the FIRST '.' of the final component, or "". */
XXFC_API char *xx_fs_path_complete_suffix(const char *path);

/**
 * @brief xx_fs_path_complete_suffix with the hidden-name rule made explicit.
 *
 * With @p skip_leading_dot true -- what xx_fs_path_complete_suffix does -- a
 * dot in position 0 marks a hidden name and the search for the first suffix
 * dot starts past it, so ".hidden.txt" yields "txt".
 *
 * With it false, the leading dot counts, so ".hidden.txt" yields "". That is
 * what cdie does and what the DIE script API's getFileCompleteSuffix returns,
 * and the signature databases were written against it. It is the less
 * self-consistent of the two -- it can report a complete suffix shorter than
 * the plain suffix of the same name -- which is why it is not the default.
 */
XXFC_API char *xx_fs_path_complete_suffix_ex(const char *path, bool skip_leading_dot);

/** @brief The path with separators in the platform's native direction. */
XXFC_API char *xx_fs_path_native(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_FS_H */
