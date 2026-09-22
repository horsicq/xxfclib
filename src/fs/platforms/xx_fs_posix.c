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

#include "xx_fs_platform.h"

#include "xxfclib/rt/xx_rt.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

xx_fs_platform_kind_t xx_fs_platform_stat(const char *path) {
    struct stat info;

    if (!path || stat(path, &info) != 0) {
        return XX_FS_PLATFORM_MISSING;
    }
    if (S_ISDIR(info.st_mode)) {
        return XX_FS_PLATFORM_DIR;
    }
    return S_ISREG(info.st_mode) ? XX_FS_PLATFORM_FILE : XX_FS_PLATFORM_MISSING;
}

static bool xx_fs_posix_is_dot(const char *name) {
    return name && name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

bool xx_fs_platform_enumerate(const char *path,
                              xx_fs_platform_entry_fn callback,
                              void *context) {
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
        size_t path_length;
        size_t name_length;
        char *full;
        bool is_dir;

        if (xx_fs_posix_is_dot(item->d_name)) {
            continue;
        }
        /* d_type would save a stat, but it is not in POSIX and reports
         * DT_UNKNOWN on several filesystems, so the type comes from stat. */
        path_length = xx_rt_strlen(path);
        name_length = xx_rt_strlen(item->d_name);
        full = (char *)xx_rt_malloc(path_length + name_length + 2U);
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

        if (!callback(context, item->d_name, is_dir)) {
            result = false;
            break;
        }
    }
    closedir(directory);
    return result;
}

char xx_fs_platform_separator(void) {
    return '/';
}

#endif /* !_WIN32 */
