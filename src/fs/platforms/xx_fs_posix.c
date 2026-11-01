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
 * @file xx_fs_posix.c
 * @brief POSIX implementation of the filesystem platform interface.
 *
 * Also used on Cygwin, which supplies a POSIX layer and which CMake routes
 * here because its if(WIN32) test is false there.
 */

#if !defined(_WIN32)

/* Feature-test macros must precede every system header so clock and directory
 * declarations are visible under a strict -std=c99. */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "xx_fs_platform.h"

#include "xxfclib/rt/xx_rt.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

xx_fs_platform_kind_t xx_fs_platform_stat(const char *path)
{
    struct stat info;

    if (!path || stat(path, &info) != 0) {
        return XX_FS_PLATFORM_MISSING;
    }
    if (S_ISDIR(info.st_mode)) {
        return XX_FS_PLATFORM_DIR;
    }
    return S_ISREG(info.st_mode) ? XX_FS_PLATFORM_FILE : XX_FS_PLATFORM_MISSING;
}

static bool xx_fs_posix_is_dot(const char *name)
{
    return name && name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

bool xx_fs_platform_enumerate(const char *path, xx_fs_platform_entry_fn callback, void *context)
{
    DIR *directory;
    struct dirent *item;
    bool result = true;

    if (!path || !callback) {
        return false;
    }
    directory = opendir(path);
    if (!directory) {
        return false;
    }
    while ((item = readdir(directory)) != NULL) {
        bool is_dir;
        bool known_type = false;

        if (xx_fs_posix_is_dot(item->d_name)) {
            continue;
        }

        /* d_type is an optional extension. Only trust the unambiguous file
         * and directory cases; stat still follows links and handles unknown
         * types, special files and platforms without the extension. */
#if defined(DT_REG) && defined(DT_DIR) && \
    (defined(_DIRENT_HAVE_D_TYPE) || defined(__APPLE__) || defined(__FreeBSD__) || \
     defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__CYGWIN__))
        if (item->d_type == DT_REG || item->d_type == DT_DIR) {
            is_dir = item->d_type == DT_DIR;
            known_type = true;
        }
#endif
        if (!known_type) {
            size_t path_length = xx_rt_strlen(path);
            size_t name_length = xx_rt_strlen(item->d_name);
            char *full = (char *)xx_rt_malloc(path_length + name_length + 2U);

            if (!full) {
                result = false;
                break;
            }
            xx_rt_memcpy(full, path, path_length);
            if (path_length > 0U && full[path_length - 1U] != '/') {
                full[path_length++] = '/';
            }
            xx_rt_memcpy(full + path_length, item->d_name, name_length);
            full[path_length + name_length] = '\0';

            is_dir = xx_fs_platform_stat(full) == XX_FS_PLATFORM_DIR;
            xx_rt_free(full);
        }

        if (!callback(context, item->d_name, is_dir)) {
            result = false;
            break;
        }
    }
    closedir(directory);
    return result;
}

char xx_fs_platform_separator(void)
{
    return '/';
}

xx_fs_platform_kind_t xx_fs_platform_lstat(const char *path)
{
    struct stat info;

    if (!path) {
        return XX_FS_PLATFORM_MISSING;
    }
    if (lstat(path, &info) != 0) {
        /* EACCES, ELOOP, ENAMETOOLONG or EIO say nothing about whether
         * something is there. */
        return (errno == ENOENT || errno == ENOTDIR) ? XX_FS_PLATFORM_MISSING : XX_FS_PLATFORM_ERROR;
    }
    if (S_ISLNK(info.st_mode)) {
        return XX_FS_PLATFORM_LINK;
    }
    return S_ISDIR(info.st_mode) ? XX_FS_PLATFORM_DIR : XX_FS_PLATFORM_FILE;
}

bool xx_fs_platform_remove(const char *path, xx_fs_platform_kind_t kind)
{
    if (!path) {
        return false;
    }
    /* unlink() removes a link itself, whatever it points at. */
    return (kind == XX_FS_PLATFORM_DIR ? rmdir(path) : unlink(path)) == 0;
}

/* ---------------------------------------------------------- tree removal */

/* A directory opened to be emptied: never through a link in its final
 * component (O_NOFOLLOW), never a FIFO or device swapped in (O_DIRECTORY;
 * O_NONBLOCK and O_NOCTTY so even a racing open cannot wait or take a
 * terminal). */
#ifdef O_CLOEXEC
#define XX_FS_POSIX_TREE_DIR_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC)
#else
#define XX_FS_POSIX_TREE_DIR_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY)
#endif

/* The names in the open directory, NUL-separated in one block, read through
 * a duplicate descriptor so the stream is closed before anything in it is
 * removed: some file systems skip entries when a directory changes under
 * readdir. *names is NULL when there are none. */
static bool xx_fs_posix_read_names(int directory, char **names, size_t *count)
{
    DIR *stream;
    struct dirent *item;
    char *block = NULL;
    size_t used = 0U;
    size_t capacity = 0U;
    size_t found = 0U;
    int copy;

    *names = NULL;
    *count = 0U;
#ifdef F_DUPFD_CLOEXEC
    copy = fcntl(directory, F_DUPFD_CLOEXEC, 0);
#else
    copy = dup(directory);
#endif
    if (copy < 0) {
        return false;
    }
    stream = fdopendir(copy);
    if (!stream) {
        close(copy);
        return false;
    }
    for (;;) {
        size_t length;

        errno = 0;
        item = readdir(stream);
        if (!item) {
            break;
        }
        if (xx_fs_posix_is_dot(item->d_name)) {
            continue;
        }
        length = xx_rt_strlen(item->d_name) + 1U;
        if (capacity - used < length) {
            size_t grown = capacity ? capacity : 1024U;
            char *bigger;

            while (grown - used < length && grown <= ((size_t)-1) / 2U) {
                grown *= 2U;
            }
            bigger = grown - used < length ? NULL : (char *)xx_rt_realloc(block, grown);
            if (!bigger) {
                closedir(stream);
                xx_rt_free(block);
                return false;
            }
            block = bigger;
            capacity = grown;
        }
        xx_rt_memcpy(block + used, item->d_name, length);
        used += length;
        ++found;
    }
    if (errno != 0) {
        closedir(stream);
        xx_rt_free(block);
        return false;
    }
    closedir(stream);
    *names = block;
    *count = found;
    return true;
}

static bool xx_fs_posix_remove_at(int parent, const char *name, unsigned depth, unsigned max_depth);

/* Remove everything in the open directory, whose own level is depth, then
 * close it. */
static bool xx_fs_posix_empty(int directory, unsigned depth, unsigned max_depth)
{
    char *names;
    const char *cursor;
    size_t count;
    size_t index;
    bool result = xx_fs_posix_read_names(directory, &names, &count);

    cursor = names;
    for (index = 0U; index < count; ++index) {
        if (!xx_fs_posix_remove_at(directory, cursor, depth + 1U, max_depth)) {
            result = false;
        }
        cursor += xx_rt_strlen(cursor) + 1U;
    }
    xx_rt_free(names);
    close(directory);
    return result;
}

/* Remove name, found in the directory open as parent, and everything below
 * it. Nothing is reached by a path: fstatat does not follow the name,
 * openat with O_NOFOLLOW refuses a link put there after that look, and each
 * level below is opened relative to the descriptor of the level above. A
 * directory renamed away and replaced by a link while the walk runs can
 * therefore never take it outside the tree. One descriptor stays open per
 * level being emptied. */
static bool xx_fs_posix_remove_at(int parent, const char *name, unsigned depth, unsigned max_depth)
{
    struct stat info;
    int directory;
    bool result;

    if (fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(info.st_mode)) {
        /* Without AT_REMOVEDIR this removes a link itself, and refuses a
         * directory swapped in since the look. */
        return unlinkat(parent, name, 0) == 0 || errno == ENOENT;
    }
    if (depth >= max_depth) {
        return false;
    }
    directory = openat(parent, name, XX_FS_POSIX_TREE_DIR_FLAGS);
    if (directory < 0) {
        /* ELOOP or ENOTDIR: no longer the directory examined. */
        return false;
    }
    result = xx_fs_posix_empty(directory, depth, max_depth);
    if (unlinkat(parent, name, AT_REMOVEDIR) != 0 && errno != ENOENT) {
        result = false;
    }
    return result && fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
}

bool xx_fs_platform_remove_tree(const char *path, unsigned max_depth)
{
    struct stat info;
    size_t length;
    size_t start;
    char *root;
    bool result;

    if (!path || !path[0]) {
        return false;
    }
    /* "link/" would make lstat, and everything after it, resolve the link. */
    length = xx_rt_strlen(path);
    while (length > 1U && path[length - 1U] == '/') {
        --length;
    }
    start = length;
    while (start > 0U && path[start - 1U] != '/') {
        --start;
    }
    /* Nothing below "/", and nothing named by "." or ".." -- which resolve
     * through a link in front of them -- is a tree this call may remove. */
    if (start == length || (path[start] == '.' && (start + 1U == length || (path[start + 1U] == '.' && start + 2U == length)))) {
        errno = EINVAL;
        return false;
    }
    root = (char *)xx_rt_malloc(length + 1U);
    if (!root) {
        return false;
    }
    xx_rt_memcpy(root, path, length);
    root[length] = '\0';

    /* The root is named by the caller's path, so it is examined and removed by
     * that path; only its final component is never followed. */
    if (lstat(root, &info) != 0) {
        result = errno == ENOENT || errno == ENOTDIR;
    } else if (!S_ISDIR(info.st_mode)) {
        result = unlink(root) == 0 || errno == ENOENT;
    } else if (max_depth == 0U) {
        result = false;
    } else {
        int directory = open(root, XX_FS_POSIX_TREE_DIR_FLAGS);

        if (directory < 0) {
            result = false;
        } else {
            result = xx_fs_posix_empty(directory, 0U, max_depth);
            if (rmdir(root) != 0 && errno != ENOENT) {
                result = false;
            }
            result = result && lstat(root, &info) != 0 && (errno == ENOENT || errno == ENOTDIR);
        }
    }
    xx_rt_free(root);
    return result;
}

bool xx_fs_platform_make_dir(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    return mkdir(path, 0777) == 0;
}

bool xx_fs_platform_rename(const char *from, const char *to)
{
    struct stat info;

    if (!from || !to) {
        return false;
    }
    /* rename() would silently replace a file or an empty directory. */
    if (lstat(to, &info) == 0) {
        errno = EEXIST;
        return false;
    }
    return rename(from, to) == 0;
}

char *xx_fs_platform_current_dir(void)
{
    size_t capacity = 256U;

    while (capacity <= 65536U) {
        char *buffer = (char *)xx_rt_malloc(capacity);

        if (!buffer) {
            return NULL;
        }
        if (getcwd(buffer, capacity)) {
            return buffer;
        }
        xx_rt_free(buffer);
        if (errno != ERANGE) {
            return NULL;
        }
        capacity *= 2U;
    }
    return NULL;
}

#endif /* !_WIN32 */
