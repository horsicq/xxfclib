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

/* Win32's wide APIs still enforce MAX_PATH unless an absolute path carries
 * the extended prefix.  The I/O layer already does this; filesystem
 * operations must use the same spelling or a long updater stage can be
 * created by one layer but neither enumerated, renamed nor removed by this
 * one.  The returned block uses xx_rt_malloc. */
static void *xx_fs_windows_api_path(const char *path)
{
    WCHAR *text;
    WCHAR *full;
    WCHAR *result;
    DWORD needed;
    DWORD length;
    size_t skip;
    size_t prefix;
    size_t i;

    if (!path) return NULL;
    text = (WCHAR *)xx_rt_utf8_to_utf16(path);
    if (!text) return NULL;
    if (text[0] == L'\\' && text[1] == L'\\' && text[2] == L'?' && text[3] == L'\\') return text;
    needed = GetFullPathNameW(text, 0, NULL, NULL);
    if (!needed) {
        xx_rt_free(text);
        return NULL;
    }
    full = (WCHAR *)xx_rt_malloc(((size_t)needed + 1U) * sizeof(WCHAR));
    if (!full) {
        xx_rt_free(text);
        return NULL;
    }
    length = GetFullPathNameW(text, needed + 1U, full, NULL);
    xx_rt_free(text);
    if (!length || length > needed) {
        xx_rt_free(full);
        return NULL;
    }
    skip = full[0] == L'\\' && full[1] == L'\\' ? 2U : 0U;
    prefix = skip ? 8U : 4U;
    result = (WCHAR *)xx_rt_malloc((prefix + (size_t)length - skip + 1U) * sizeof(WCHAR));
    if (result) {
        static const WCHAR local_prefix[] = L"\\\\?\\";
        static const WCHAR unc_prefix[] = L"\\\\?\\UNC\\";
        const WCHAR *head = skip ? unc_prefix : local_prefix;
        for (i = 0; i < prefix; ++i) result[i] = head[i];
        for (i = skip; i <= (size_t)length; ++i) result[prefix + i - skip] = full[i];
    }
    xx_rt_free(full);
    return result;
}

/* *error, when given, receives GetLastError() of a failed lookup, taken
 * before anything else can change it. */
static DWORD xx_fs_windows_attributes(const char *path, DWORD *error)
{
    void *wide;
    DWORD attributes;

    if (error) {
        *error = ERROR_INVALID_NAME;
    }
    if (!path) {
        return INVALID_FILE_ATTRIBUTES;
    }
    wide = xx_fs_windows_api_path(path);
    if (!wide) {
        return INVALID_FILE_ATTRIBUTES;
    }
    attributes = GetFileAttributesW((LPCWSTR)wide);
    if (attributes == INVALID_FILE_ATTRIBUTES && error) {
        *error = GetLastError();
    }
    xx_rt_free(wide);
    return attributes;
}

xx_fs_platform_kind_t xx_fs_platform_stat(const char *path)
{
    DWORD attributes = xx_fs_windows_attributes(path, NULL);

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return XX_FS_PLATFORM_MISSING;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? XX_FS_PLATFORM_DIR : XX_FS_PLATFORM_FILE;
}

/* "." and ".." are filtered here so neither implementation's caller has to. */
static bool xx_fs_windows_is_dot(const char *name)
{
    return name && name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

bool xx_fs_platform_enumerate(const char *path, xx_fs_platform_entry_fn callback, void *context)
{
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
    if (length > 0U && pattern[length - 1U] != '/' && pattern[length - 1U] != '\\') {
        pattern[length++] = '\\';
    }
    pattern[length] = '*';
    pattern[length + 1U] = '\0';

    wide = xx_fs_windows_api_path(pattern);
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
            bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
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

char xx_fs_platform_separator(void)
{
    return '\\';
}

xx_fs_platform_kind_t xx_fs_platform_lstat(const char *path)
{
    DWORD error;
    DWORD attributes = xx_fs_windows_attributes(path, &error);

    if (!path) {
        return XX_FS_PLATFORM_MISSING;
    }
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        /* Access denied, a sharing violation or an unreachable share say
         * nothing about whether something is there. */
        return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? XX_FS_PLATFORM_MISSING : XX_FS_PLATFORM_ERROR;
    }
    /* GetFileAttributesW does not follow a final reparse point, so a junction
     * or symbolic link reports its own attributes rather than its target's. */
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        return XX_FS_PLATFORM_LINK;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? XX_FS_PLATFORM_DIR : XX_FS_PLATFORM_FILE;
}

bool xx_fs_platform_remove(const char *path, xx_fs_platform_kind_t kind)
{
    void *wide;
    DWORD attributes;
    BOOL removed;

    if (!path) {
        return false;
    }
    wide = xx_fs_windows_api_path(path);
    if (!wide) {
        return false;
    }
    attributes = GetFileAttributesW((LPCWSTR)wide);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        xx_rt_free(wide);
        return false;
    }
    /* Neither DeleteFileW nor RemoveDirectoryW removes a read-only entry. */
    if (attributes & FILE_ATTRIBUTE_READONLY) {
        SetFileAttributesW((LPCWSTR)wide, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
    /* A directory junction or directory symbolic link is removed with
     * RemoveDirectoryW, which deletes the link and leaves its target alone. */
    if (kind == XX_FS_PLATFORM_DIR || (kind == XX_FS_PLATFORM_LINK && (attributes & FILE_ATTRIBUTE_DIRECTORY))) {
        removed = RemoveDirectoryW((LPCWSTR)wide);
    } else {
        removed = DeleteFileW((LPCWSTR)wide);
    }
    if (!removed && (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW((LPCWSTR)wide, attributes);
    }
    xx_rt_free(wide);
    return removed != 0;
}

/* ---------------------------------------------------------- tree removal */

static bool xx_fs_windows_is_separator(char c)
{
    return c == '/' || c == '\\';
}

/* The names of one directory level, NUL-separated in one block. They are
 * collected before anything below is removed, because Windows refuses to
 * delete a directory that still has an open find handle inside it. */
typedef struct xx_fs_windows_names_s {
    char *block;
    size_t used;
    size_t capacity;
    size_t count;
} xx_fs_windows_names;

static bool xx_fs_windows_collect_name(void *context, const char *name, bool is_dir)
{
    xx_fs_windows_names *names = (xx_fs_windows_names *)context;
    size_t length = xx_rt_strlen(name) + 1U;

    (void)is_dir; /* Followed links report true here; lstat decides instead. */
    if (names->capacity - names->used < length) {
        size_t grown = names->capacity ? names->capacity : 1024U;
        char *bigger;

        while (grown - names->used < length && grown <= ((size_t)-1) / 2U) {
            grown *= 2U;
        }
        bigger = grown - names->used < length ? NULL : (char *)xx_rt_realloc(names->block, grown);
        if (!bigger) {
            return false;
        }
        names->block = bigger;
        names->capacity = grown;
    }
    xx_rt_memcpy(names->block + names->used, name, length);
    names->used += length;
    ++names->count;
    return true;
}

/* Opens the directory at path itself -- FILE_FLAG_OPEN_REPARSE_POINT, so a
 * junction or symbolic link put there opens as what it is -- and refuses it
 * unless the handle shows a directory that is not a reparse point. The handle
 * asks for FILE_LIST_DIRECTORY, so it takes part in share checks, and does
 * not share delete access: while it is open nobody can rename or delete the
 * directory, so the path walked below it keeps naming the directory checked
 * here. */
static HANDLE xx_fs_windows_hold_directory(const char *path)
{
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE handle;
    void *wide = xx_fs_windows_api_path(path);

    if (!wide) {
        return INVALID_HANDLE_VALUE;
    }
    handle = CreateFileW((LPCWSTR)wide, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    xx_rt_free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return handle;
    }
    if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

static char *xx_fs_windows_join(const char *directory, const char *name)
{
    size_t left = xx_rt_strlen(directory);
    size_t right = xx_rt_strlen(name);
    char *result = (char *)xx_rt_malloc(left + right + 2U);

    if (!result) {
        return NULL;
    }
    xx_rt_memcpy(result, directory, left);
    result[left] = '\\';
    xx_rt_memcpy(result + left + 1U, name, right + 1U);
    return result;
}

/* By path, but each directory is held (see above) from before its listing
 * until its contents are gone, and so is every directory above it in the
 * walk: no rename can redirect a child's path out of the tree. The final
 * component of every path is removed without being followed (DeleteFileW and
 * RemoveDirectoryW act on a link itself). */
static bool xx_fs_windows_remove_level(const char *path, unsigned depth, unsigned max_depth)
{
    xx_fs_platform_kind_t kind = xx_fs_platform_lstat(path);
    xx_fs_windows_names names = {NULL, 0U, 0U, 0U};
    const char *cursor;
    size_t index;
    HANDLE held;
    bool result;

    if (kind == XX_FS_PLATFORM_MISSING) {
        return true;
    }
    if (kind == XX_FS_PLATFORM_ERROR) {
        return false;
    }
    if (kind != XX_FS_PLATFORM_DIR) {
        return xx_fs_platform_remove(path, kind);
    }
    if (depth >= max_depth) {
        return false;
    }
    held = xx_fs_windows_hold_directory(path);
    if (held == INVALID_HANDLE_VALUE) {
        /* Turned into a link since the look, or cannot be held. */
        return false;
    }
    result = xx_fs_platform_enumerate(path, xx_fs_windows_collect_name, &names);
    cursor = names.block;
    for (index = 0U; index < names.count; ++index) {
        char *child = xx_fs_windows_join(path, cursor);

        if (!child || !xx_fs_windows_remove_level(child, depth + 1U, max_depth)) {
            result = false;
        }
        xx_rt_free(child);
        cursor += xx_rt_strlen(cursor) + 1U;
    }
    xx_rt_free(names.block);
    CloseHandle(held);

    if (!xx_fs_platform_remove(path, XX_FS_PLATFORM_DIR)) {
        result = false;
    }
    return result && xx_fs_platform_lstat(path) == XX_FS_PLATFORM_MISSING;
}

bool xx_fs_platform_remove_tree(const char *path, unsigned max_depth)
{
    size_t length;
    size_t start;
    char *root;
    bool result;

    if (!path || !path[0]) {
        return false;
    }
    /* Dropped so that no call below resolves a final link through them. */
    length = xx_rt_strlen(path);
    while (length > 1U && xx_fs_windows_is_separator(path[length - 1U])) {
        --length;
    }
    start = length;
    while (start > 0U && !xx_fs_windows_is_separator(path[start - 1U]) && path[start - 1U] != ':') {
        --start;
    }
    /* "C:\", "C:" and "\" name no tree this call may remove, and neither
     * does a final "." or "..". */
    if (start == length || (path[start] == '.' && (start + 1U == length || (path[start + 1U] == '.' && start + 2U == length)))) {
        SetLastError(ERROR_INVALID_NAME);
        return false;
    }
    root = (char *)xx_rt_malloc(length + 1U);
    if (!root) {
        return false;
    }
    xx_rt_memcpy(root, path, length);
    root[length] = '\0';
    result = xx_fs_windows_remove_level(root, 0U, max_depth);
    xx_rt_free(root);
    return result;
}

bool xx_fs_platform_make_dir(const char *path)
{
    void *wide;
    BOOL created;

    if (!path || !path[0]) {
        return false;
    }
    wide = xx_fs_windows_api_path(path);
    if (!wide) {
        return false;
    }
    created = CreateDirectoryW((LPCWSTR)wide, NULL);
    xx_rt_free(wide);
    return created != 0;
}

bool xx_fs_platform_rename(const char *from, const char *to)
{
    void *wide_from;
    void *wide_to;
    BOOL moved = FALSE;

    if (!from || !to) {
        return false;
    }
    wide_from = xx_fs_windows_api_path(from);
    wide_to = xx_fs_windows_api_path(to);
    /* No MOVEFILE_REPLACE_EXISTING and no MOVEFILE_COPY_ALLOWED: an existing
     * destination fails, and a directory never turns into a slow copy. */
    if (wide_from && wide_to) {
        moved = MoveFileExW((LPCWSTR)wide_from, (LPCWSTR)wide_to, 0);
    }
    xx_rt_free(wide_from);
    xx_rt_free(wide_to);
    return moved != 0;
}

char *xx_fs_platform_current_dir(void)
{
    DWORD capacity = GetCurrentDirectoryW(0, NULL);
    WCHAR *wide;
    DWORD length;
    char *result;

    if (!capacity) {
        return NULL;
    }
    wide = (WCHAR *)xx_rt_malloc((size_t)capacity * sizeof(WCHAR));
    if (!wide) {
        return NULL;
    }
    length = GetCurrentDirectoryW(capacity, wide);
    if (!length || length >= capacity) {
        xx_rt_free(wide);
        return NULL;
    }
    wide[length] = 0;
    result = xx_rt_utf16_to_utf8(wide);
    xx_rt_free(wide);
    return result;
}

#endif /* _WIN32 */
