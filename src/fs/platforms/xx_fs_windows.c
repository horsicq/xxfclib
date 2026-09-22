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
 * @file xx_fs_windows.c
 * @brief Win32 implementation of the filesystem platform interface.
 *
 * Paths arrive as UTF-8 and are converted to UTF-16 for the wide Win32 calls,
 * so a name outside the active code page still round-trips.
 */

#if defined(_WIN32)

#include "xx_fs_platform.h"

#include "xxfclib/rt/xx_rt.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static DWORD xx_fs_windows_attributes(const char *path) {
    void *wide;
    DWORD attributes;

    if (!path) {
        return INVALID_FILE_ATTRIBUTES;
    }
    wide = xx_rt_utf8_to_utf16(path);
    if (!wide) {
        return INVALID_FILE_ATTRIBUTES;
    }
    attributes = GetFileAttributesW((LPCWSTR)wide);
    xx_rt_free(wide);
    return attributes;
}

xx_fs_platform_kind_t xx_fs_platform_stat(const char *path) {
    DWORD attributes = xx_fs_windows_attributes(path);

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return XX_FS_PLATFORM_MISSING;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? XX_FS_PLATFORM_DIR
                                                   : XX_FS_PLATFORM_FILE;
}

/* "." and ".." are filtered here so neither implementation's caller has to. */
static bool xx_fs_windows_is_dot(const char *name) {
    return name && name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

bool xx_fs_platform_enumerate(const char *path,
                              xx_fs_platform_entry_fn callback,
                              void *context) {
    size_t length;
    char *pattern;
    void *wide;
    WIN32_FIND_DATAW find_data;
    HANDLE handle;
    bool result = true;

    if (!path || !callback) {
        return false;
    }
    /* FindFirstFileW wants a wildcard rather than a directory name. */
    length = xx_rt_strlen(path);
    pattern = (char *)xx_rt_malloc(length + 3U);
    if (!pattern) {
        return false;
    }
    xx_rt_memcpy(pattern, path, length);
    if (length > 0U && pattern[length - 1U] != '/' &&
        pattern[length - 1U] != '\\') {
        pattern[length++] = '\\';
    }
    pattern[length] = '*';
    pattern[length + 1U] = '\0';

    wide = xx_rt_utf8_to_utf16(pattern);
    xx_rt_free(pattern);
    if (!wide) {
        return false;
    }
    handle = FindFirstFileW((LPCWSTR)wide, &find_data);
    xx_rt_free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    do {
        char *name = xx_rt_utf16_to_utf8(find_data.cFileName);
        if (!name) {
            continue;
        }
        if (!xx_fs_windows_is_dot(name)) {
            bool is_dir =
                (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!callback(context, name, is_dir)) {
                xx_rt_free(name);
                result = false;
                break;
            }
        }
        xx_rt_free(name);
    } while (FindNextFileW(handle, &find_data));

    FindClose(handle);
    return result;
}

char xx_fs_platform_separator(void) {
    return '\\';
}

#endif /* _WIN32 */
