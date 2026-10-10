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
 * @file xx_io_windows.c
 * @brief Windows platform file I/O implementation using pure Win32 API.
 */

#if defined(_WIN32)

#include "xx_io_platform.h"
#include "../xx_io_policy.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

static int xx_win_mode_has_char(const char *str, char c)
{
    if (!str) {
        return 0;
    }
    while (*str) {
        if (*str == c) {
            return 1;
        }
        str++;
    }
    return 0;
}

#define XX_WIN_PATH_LIMIT 32767U

static wchar_t *xx_win_codepage_path(const char *path, UINT codepage, DWORD flags, wchar_t *stack, size_t stack_count)
{
    int length;
    wchar_t *result = stack;
    HANDLE heap = GetProcessHeap();
    if (!path || !stack || stack_count == 0U) return NULL;
    length = MultiByteToWideChar(codepage, flags, path, -1, NULL, 0);
    if (length <= 0 || (size_t)length > XX_WIN_PATH_LIMIT) return NULL;
    if ((size_t)length > stack_count) {
        result = (wchar_t *)HeapAlloc(heap, 0, (SIZE_T)length * sizeof(wchar_t));
        if (!result) return NULL;
    }
    if (MultiByteToWideChar(codepage, flags, path, -1, result, length) <= 0) {
        if (result != stack) HeapFree(heap, 0, result);
        return NULL;
    }
    return result;
}

static wchar_t *xx_win_utf8_path(const char *path, wchar_t *stack, size_t stack_count)
{
    return xx_win_codepage_path(path, CP_UTF8, MB_ERR_INVALID_CHARS, stack, stack_count);
}

static void xx_win_free_path(wchar_t *path, wchar_t *stack)
{
    if (path && path != stack) HeapFree(GetProcessHeap(), 0, path);
}

static wchar_t *xx_win_copy_path(const wchar_t *path, size_t length, wchar_t *stack, size_t stack_count)
{
    wchar_t *result = stack;
    size_t i;
    if (length + 1U > stack_count) {
        result = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (length + 1U) * sizeof(wchar_t));
        if (!result) return NULL;
    }
    for (i = 0U; i <= length; ++i) result[i] = path[i];
    return result;
}

/* Extended paths must be absolute and use backslashes.  Resolve ordinary
 * relative paths before adding the prefix, without requiring a registry or
 * application-manifest change.  Short file API paths retain their previous
 * Win32 interpretation; directory walks request the absolute form so every
 * ancestor can be checked.  Existing extended/device namespaces are retained. */
static wchar_t *xx_win_api_path(const wchar_t *path, wchar_t *stack, size_t stack_count, bool absolute)
{
    size_t length = 0U, prefix, skip, i;
    DWORD needed, full_length;
    wchar_t *full, *result;
    if (!path || !stack || !stack_count) return NULL;
    while (length < XX_WIN_PATH_LIMIT && path[length]) ++length;
    if (!length || length == XX_WIN_PATH_LIMIT) return NULL;
    if (length >= 4U && path[0] == L'\\' && path[1] == L'\\' && (path[2] == L'?' || path[2] == L'.') && path[3] == L'\\')
        return xx_win_copy_path(path, length, stack, stack_count);
    needed = GetFullPathNameW(path, 0, NULL, NULL);
    if (!needed || needed > XX_WIN_PATH_LIMIT) return NULL;
    full = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)needed * sizeof(wchar_t));
    if (!full) return NULL;
    full_length = GetFullPathNameW(path, needed, full, NULL);
    if (!full_length || full_length >= needed) {
        HeapFree(GetProcessHeap(), 0, full);
        return NULL;
    }
    if (full_length < MAX_PATH && length < MAX_PATH && !absolute) {
        HeapFree(GetProcessHeap(), 0, full);
        return xx_win_copy_path(path, length, stack, stack_count);
    }
    for (i = 0U; i < full_length; ++i)
        if (full[i] == L'/') full[i] = L'\\';
    /* CreateDirectory's legacy limit also reserves room for an 8.3 name. */
    if (full_length < MAX_PATH - 12U) {
        result = xx_win_copy_path(full, full_length, stack, stack_count);
        HeapFree(GetProcessHeap(), 0, full);
        return result;
    }
    skip = full[0] == L'\\' && full[1] == L'\\' ? 2U : 0U;
    prefix = skip ? 8U : 4U;
    length = prefix + (size_t)full_length - skip;
    if (length + 1U > XX_WIN_PATH_LIMIT) {
        HeapFree(GetProcessHeap(), 0, full);
        return NULL;
    }
    result = length + 1U <= stack_count ? stack : (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (length + 1U) * sizeof(wchar_t));
    if (result) {
        static const wchar_t local_prefix[] = L"\\\\?\\";
        static const wchar_t unc_prefix[] = L"\\\\?\\UNC\\";
        const wchar_t *text = skip ? unc_prefix : local_prefix;
        for (i = 0U; i < prefix; ++i) result[i] = text[i];
        for (i = skip; i <= full_length; ++i) result[prefix + i - skip] = full[i];
    }
    HeapFree(GetProcessHeap(), 0, full);
    return result;
}

void *xx_io_platform_temp_open(void)
{
    if (!xx_io_policy_mutation_allowed()) return NULL;
    static const wchar_t hex[] = L"0123456789abcdef";
    wchar_t stack_path[MAX_PATH];
    wchar_t api_stack[MAX_PATH];
    wchar_t *path = stack_path;
    DWORD length = GetTempPathW(MAX_PATH, stack_path);
    size_t capacity;
    unsigned int attempt;
    HANDLE result = INVALID_HANDLE_VALUE;
    if (!length) return NULL;
    capacity = (size_t)length + 48U;
    if (capacity > MAX_PATH) {
        if (capacity > 32767U) return NULL;
        path = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(wchar_t));
        if (!path) return NULL;
        length = GetTempPathW((DWORD)capacity, path);
        if (!length || (size_t)length + 48U > capacity) goto cleanup;
    }
    for (attempt = 0U; attempt < 8U; ++attempt) {
        uint8_t random[16];
        size_t at = length, i;
        if (!xx_io_platform_secure_random(random, sizeof(random))) break;
        if (at && path[at - 1U] != L'\\' && path[at - 1U] != L'/') path[at++] = L'\\';
        path[at++] = L'x';
        path[at++] = L'x';
        path[at++] = L'f';
        path[at++] = L'c';
        for (i = 0U; i < sizeof(random); ++i) {
            path[at++] = hex[random[i] >> 4U];
            path[at++] = hex[random[i] & 15U];
        }
        path[at++] = L'.';
        path[at++] = L't';
        path[at++] = L'm';
        path[at++] = L'p';
        path[at] = L'\0';
        {
            wchar_t *api = xx_win_api_path(path, api_stack, MAX_PATH, false);
            if (!api) break;
            result = CreateFileW(api, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
            xx_win_free_path(api, api_stack);
        }
        if (result != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
cleanup:
    if (path != stack_path) HeapFree(GetProcessHeap(), 0, path);
    return result == INVALID_HANDLE_VALUE ? NULL : (void *)result;
}

void *xx_io_platform_file_open(const char *path, const char *mode)
{
    if (!xx_io_policy_file_open_allowed(mode)) return NULL;
    if (!path || !mode) {
        return NULL;
    }

    DWORD dwDesiredAccess = 0;
    DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD dwCreationDisposition = 0;
    int append = 0;

    if (xx_win_mode_has_char(mode, 'r')) {
        if (xx_win_mode_has_char(mode, '+')) {
            dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        } else {
            dwDesiredAccess = GENERIC_READ;
        }
        dwCreationDisposition = OPEN_EXISTING;
    } else if (xx_win_mode_has_char(mode, 'w')) {
        if (xx_win_mode_has_char(mode, '+')) {
            dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        } else {
            dwDesiredAccess = GENERIC_WRITE;
        }
        dwCreationDisposition = xx_win_mode_has_char(mode, 'x') ? CREATE_NEW : CREATE_ALWAYS;
    } else if (xx_win_mode_has_char(mode, 'a')) {
        if (xx_win_mode_has_char(mode, '+')) {
            dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        } else {
            dwDesiredAccess = FILE_APPEND_DATA | SYNCHRONIZE;
        }
        dwCreationDisposition = OPEN_ALWAYS;
        append = 1;
    } else {
        return NULL;
    }

    wchar_t text_stack[MAX_PATH], api_stack[MAX_PATH];
    wchar_t *text, *api;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    unsigned int attempt;
    /* Retain the legacy ANSI retry used by file_open, including short paths. */
    for (attempt = 0U; attempt < 2U; ++attempt) {
        text = xx_win_codepage_path(path, attempt ? CP_ACP : CP_UTF8, 0, text_stack, MAX_PATH);
        if (!text) continue;
        api = xx_win_api_path(text, api_stack, MAX_PATH, false);
        if (api) {
            hFile = CreateFileW(api, dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
            xx_win_free_path(api, api_stack);
        }
        xx_win_free_path(text, text_stack);
        if (hFile != INVALID_HANDLE_VALUE) break;
    }
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    if (append) {
        LARGE_INTEGER liZero;
        liZero.QuadPart = 0;
        SetFilePointerEx(hFile, liZero, NULL, FILE_END);
    }

    return (void *)hFile;
}

ssize_t xx_io_platform_file_read(void *handle, void *buf, size_t n)
{
    if (!handle || handle == INVALID_HANDLE_VALUE || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }

    DWORD bytes_to_read = (n > 0x7FFFFFFF) ? 0x7FFFFFFF : (DWORD)n;
    DWORD bytes_read = 0;

    if (!ReadFile((HANDLE)handle, buf, bytes_to_read, &bytes_read, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_HANDLE_EOF) {
            return 0;
        }
        return -1;
    }

    return (ssize_t)bytes_read;
}

ssize_t xx_io_platform_file_write(void *handle, const void *buf, size_t n)
{
    if (!xx_io_policy_mutation_allowed()) return -1;
    if (!handle || handle == INVALID_HANDLE_VALUE || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }

    DWORD bytes_to_write = (n > 0x7FFFFFFF) ? 0x7FFFFFFF : (DWORD)n;
    DWORD bytes_written = 0;

    if (!WriteFile((HANDLE)handle, buf, bytes_to_write, &bytes_written, NULL)) {
        return -1;
    }

    return (ssize_t)bytes_written;
}

int xx_io_platform_file_seek(void *handle, long off, int whence)
{
    return xx_io_platform_file_seek64(handle, (int64_t)off, whence);
}

int xx_io_platform_file_seek64(void *handle, int64_t off, int whence)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = xx_io_platform_file_tell(handle); break;
        case SEEK_END: base = xx_io_platform_file_size(handle); break;
        default: return -1;
    }

    if (base < 0 || off < -base || off > INT64_MAX - base) {
        return -1;
    }

    LARGE_INTEGER liOffset;
    liOffset.QuadPart = base + off;
    LARGE_INTEGER liNewPos;

    if (!SetFilePointerEx((HANDLE)handle, liOffset, &liNewPos, FILE_BEGIN)) {
        return -1;
    }

    return 0;
}

int64_t xx_io_platform_file_tell(void *handle)
{
    LARGE_INTEGER zero;
    LARGE_INTEGER pos;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    zero.QuadPart = 0;
    if (!SetFilePointerEx((HANDLE)handle, zero, &pos, FILE_CURRENT)) {
        return -1;
    }
    return (int64_t)pos.QuadPart;
}

int xx_io_platform_file_close(void *handle)
{
    BOOL flushed = TRUE;
    DWORD flush_error = ERROR_SUCCESS;
    BOOL closed;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    /* FlushFileBuffers is meaningful for disk files.  Device handles such as
     * NUL/CONOUT$ reject it, while named-pipe flushing has blocking semantics
     * that an ordinary file close must not acquire. */
    if (GetFileType((HANDLE)handle) == FILE_TYPE_DISK) {
        flushed = FlushFileBuffers((HANDLE)handle);
        if (!flushed) flush_error = GetLastError();
    }
    closed = CloseHandle((HANDLE)handle);
    /* Read-only file handles cannot be flushed and report ACCESS_DENIED. */
    return closed && (flushed || flush_error == ERROR_ACCESS_DENIED) ? 0 : -1;
}

int64_t xx_io_platform_file_size(void *handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx((HANDLE)handle, &liSize)) {
        return -1;
    }

    return (int64_t)liSize.QuadPart;
}

bool xx_io_platform_file_exists_w(const wchar_t *path)
{
    wchar_t stack[MAX_PATH];
    wchar_t *api = xx_win_api_path(path, stack, MAX_PATH, false);
    bool result = api && GetFileAttributesW(api) != INVALID_FILE_ATTRIBUTES;
    xx_win_free_path(api, stack);
    return result;
}

bool xx_io_platform_file_exists_a(const char *path)
{
    wchar_t stack[MAX_PATH];
    wchar_t *wide = xx_win_utf8_path(path, stack, sizeof(stack) / sizeof(stack[0]));
    bool result = wide && xx_io_platform_file_exists_w(wide);
    xx_win_free_path(wide, stack);
    return result;
}

bool xx_io_platform_file_remove_w(const wchar_t *path)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    wchar_t stack[MAX_PATH];
    wchar_t *api = xx_win_api_path(path, stack, MAX_PATH, false);
    bool result = api && DeleteFileW(api) != 0;
    xx_win_free_path(api, stack);
    return result;
}

bool xx_io_platform_file_remove_a(const char *path)
{
    wchar_t stack[MAX_PATH];
    wchar_t *wide = xx_win_utf8_path(path, stack, sizeof(stack) / sizeof(stack[0]));
    bool result = wide && xx_io_platform_file_remove_w(wide);
    xx_win_free_path(wide, stack);
    return result;
}

bool xx_io_platform_file_replace_w(const wchar_t *source, const wchar_t *destination, bool overwrite)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    wchar_t source_stack[MAX_PATH], destination_stack[MAX_PATH];
    wchar_t *source_api = xx_win_api_path(source, source_stack, MAX_PATH, false);
    wchar_t *destination_api = xx_win_api_path(destination, destination_stack, MAX_PATH, false);
    bool result;
    if (overwrite) flags |= MOVEFILE_REPLACE_EXISTING;
    result = source_api && destination_api && MoveFileExW(source_api, destination_api, flags) != 0;
    xx_win_free_path(source_api, source_stack);
    xx_win_free_path(destination_api, destination_stack);
    return result;
}

bool xx_io_platform_file_replace_a(const char *source, const char *destination, bool overwrite)
{
    wchar_t source_stack[MAX_PATH], destination_stack[MAX_PATH];
    wchar_t *source_wide = xx_win_utf8_path(source, source_stack, sizeof(source_stack) / sizeof(source_stack[0]));
    wchar_t *destination_wide = xx_win_utf8_path(destination, destination_stack, sizeof(destination_stack) / sizeof(destination_stack[0]));
    bool result = source_wide && destination_wide && xx_io_platform_file_replace_w(source_wide, destination_wide, overwrite);
    xx_win_free_path(source_wide, source_stack);
    xx_win_free_path(destination_wide, destination_stack);
    return result;
}

/* Returns the filesystem root's end, including its trailing separator when
 * present. Never split a UNC server/share or an extended prefix as directories. */
static size_t xx_win_root_length(const wchar_t *path)
{
    size_t at = 0U, component;
    bool unc = false;
    if (path[0] == L'\\' && path[1] == L'\\') {
        if (path[2] == L'.' && path[3] == L'\\') return 0U;
        if (path[2] == L'?' && path[3] == L'\\') {
            at = 4U;
            if ((path[at] == L'U' || path[at] == L'u') && (path[at + 1U] == L'N' || path[at + 1U] == L'n') && (path[at + 2U] == L'C' || path[at + 2U] == L'c') &&
                path[at + 3U] == L'\\') {
                at += 4U;
                unc = true;
            } else if (path[at] == L'V' && path[at + 1U] == L'o' && path[at + 2U] == L'l' && path[at + 3U] == L'u' && path[at + 4U] == L'm' && path[at + 5U] == L'e' &&
                       path[at + 6U] == L'{') {
                while (path[at] && path[at] != L'\\') ++at;
                return path[at] == L'\\' ? at + 1U : 0U;
            }
        } else {
            at = 2U;
            unc = true;
        }
    }
    if (!unc) {
        if (!((path[at] >= L'A' && path[at] <= L'Z') || (path[at] >= L'a' && path[at] <= L'z')) || path[at + 1U] != L':' || path[at + 2U] != L'\\') return 0U;
        return at + 3U;
    }
    for (component = 0U; component < 2U; ++component) {
        size_t begin = at;
        while (path[at] && path[at] != L'\\') ++at;
        if (at == begin) return 0U;
        if (component == 0U && !path[at]) return 0U;
        if (path[at] == L'\\') ++at;
    }
    return at;
}

static bool xx_win_directory_attributes_ok(DWORD attributes)
{
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
}

static bool xx_win_check_root(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION info;
    bool result;
    if (attributes != INVALID_FILE_ATTRIBUTES) return xx_win_directory_attributes_ok(attributes);
    /* GetFileAttributes cannot always inspect a UNC share root. Query its
     * directory handle without following a reparse point instead. */
    handle = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) return false;
    result = GetFileInformationByHandle(handle, &info) != 0 && xx_win_directory_attributes_ok(info.dwFileAttributes);
    CloseHandle(handle);
    return result;
}

static bool xx_win_ensure_directory(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        attributes = GetFileAttributesW(path);
    }
    return xx_win_directory_attributes_ok(attributes);
}

bool xx_io_platform_create_dirs_w(const wchar_t *path, bool is_dir)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    wchar_t stack[MAX_PATH];
    wchar_t *temp = xx_win_api_path(path, stack, MAX_PATH, true);
    size_t root, length = 0U, i;
    bool result = false;
    wchar_t saved;
    if (!temp) return false;
    while (temp[length]) ++length;
    root = xx_win_root_length(temp);
    if (!root || root > length) goto cleanup;
    saved = temp[root];
    temp[root] = L'\0';
    result = xx_win_check_root(temp);
    temp[root] = saved;
    if (!result) goto cleanup;
    for (i = root; i < length; ++i) {
        if (temp[i] == L'/' || temp[i] == L'\\') {
            saved = temp[i];
            temp[i] = L'\0';
            result = xx_win_ensure_directory(temp);
            temp[i] = saved;
            if (!result) goto cleanup;
        }
    }
    if (is_dir && length > root && temp[length - 1U] != L'\\') result = xx_win_ensure_directory(temp);
cleanup:
    xx_win_free_path(temp, stack);
    return result;
}

bool xx_io_platform_create_dirs_a(const char *path, bool is_dir)
{
    wchar_t stack[MAX_PATH];
    wchar_t *wide = xx_win_utf8_path(path, stack, MAX_PATH);
    bool result = wide && xx_io_platform_create_dirs_w(wide, is_dir);
    xx_win_free_path(wide, stack);
    return result;
}

bool xx_io_platform_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    wchar_t stack[MAX_PATH];
    wchar_t *api = xx_win_api_path(path, stack, MAX_PATH, false);
    if (!api) return false;

    if (dos_date != 0 || dos_time != 0) {
        FILETIME ftLocal, ftUtc;
        if (DosDateTimeToFileTime((WORD)dos_date, (WORD)dos_time, &ftLocal)) {
            if (LocalFileTimeToFileTime(&ftLocal, &ftUtc)) {
                DWORD flags = FILE_ATTRIBUTE_NORMAL;
                DWORD attr_existing = GetFileAttributesW(api);
                if (attr_existing != INVALID_FILE_ATTRIBUTES && (attr_existing & FILE_ATTRIBUTE_DIRECTORY)) {
                    flags = FILE_FLAG_BACKUP_SEMANTICS;
                }
                HANDLE h = CreateFileW(api, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, flags, NULL);
                if (h != INVALID_HANDLE_VALUE) {
                    SetFileTime(h, NULL, NULL, &ftUtc);
                    CloseHandle(h);
                }
            }
        }
    }

    if (attrs != 0) {
        DWORD win_attrs = attrs & 0x3F; /* Archive, Read-Only, Hidden, System */
        if (win_attrs) {
            SetFileAttributesW(api, win_attrs);
        }
    }

    xx_win_free_path(api, stack);
    return true;
}

bool xx_io_platform_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
    wchar_t stack[MAX_PATH];
    wchar_t *wide = xx_win_utf8_path(path, stack, MAX_PATH);
    bool result = wide && xx_io_platform_apply_dos_time_and_attrs_w(wide, dos_date, dos_time, attrs);
    xx_win_free_path(wide, stack);
    return result;
}

/* --- Path safety and entropy, shared by the archive readers --------------- */

/* The reserved device names. Creating one of these opens the device rather
 * than a file, so a reader must refuse the name before extracting. Lifted out
 * of xx_zip.c and xx_rar.c, which carried byte-identical copies. */
static wchar_t xx_io_ascii_upper(wchar_t value)
{
    return value >= L'a' && value <= L'z' ? value - (L'a' - L'A') : value;
}

bool xx_io_platform_wsegment_is_reserved(const wchar_t *segment, size_t length)
{
    size_t base = 0;
    wchar_t a, b, c;
    while (base < length && segment[base] != L'.') ++base;
    if (base < 3U) return false;
    a = xx_io_ascii_upper(segment[0]);
    b = xx_io_ascii_upper(segment[1]);
    c = xx_io_ascii_upper(segment[2]);
    if (base == 3U && ((a == L'C' && b == L'O' && c == L'N') || (a == L'P' && b == L'R' && c == L'N') || (a == L'A' && b == L'U' && c == L'X') ||
                       (a == L'N' && b == L'U' && c == L'L')))
        return true;
    if (base == 4U && ((a == L'C' && b == L'O' && c == L'M') || (a == L'L' && b == L'P' && c == L'T')) &&
        ((segment[3] >= L'1' && segment[3] <= L'9') || segment[3] == 0x00b9 || segment[3] == 0x00b2 || segment[3] == 0x00b3))
        return true;
    if (base == 6U && ((a == L'C' && b == L'O' && c == L'N' && xx_io_ascii_upper(segment[3]) == L'I' && xx_io_ascii_upper(segment[4]) == L'N' && segment[5] == L'$') ||
                       (a == L'C' && b == L'L' && c == L'O' && xx_io_ascii_upper(segment[3]) == L'C' && xx_io_ascii_upper(segment[4]) == L'K' && segment[5] == L'$')))
        return true;
    if (base == 7U && a == L'C' && b == L'O' && c == L'N' && xx_io_ascii_upper(segment[3]) == L'O' && xx_io_ascii_upper(segment[4]) == L'U' &&
        xx_io_ascii_upper(segment[5]) == L'T' && segment[6] == L'$')
        return true;
    return false;
}

bool xx_io_platform_secure_random(uint8_t *output, size_t size)
{
    if (!output) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
    {
        typedef LONG(WINAPI * xx_bcrypt_gen_random_fn)(void *, unsigned char *, ULONG, ULONG);
        HMODULE module = LoadLibraryW(L"bcrypt.dll");
        xx_bcrypt_gen_random_fn generate;
        size_t offset = 0U;
        bool success = true;
        if (!module) {
            return false;
        }
        generate = (xx_bcrypt_gen_random_fn)(void (*)(void))GetProcAddress(module, "BCryptGenRandom");
        if (!generate) {
            FreeLibrary(module);
            return false;
        }
        while (offset < size) {
            size_t remaining = size - offset;
            ULONG amount = remaining > (size_t)ULONG_MAX ? ULONG_MAX : (ULONG)remaining;
            /* BCRYPT_USE_SYSTEM_PREFERRED_RNG */
            if (generate(NULL, output + offset, amount, 0x00000002UL) < 0) {
                success = false;
                break;
            }
            offset += (size_t)amount;
        }
        FreeLibrary(module);
        return success;
    }
}

wchar_t xx_io_platform_wseparator(void)
{
    return L'\\';
}

/* ---------------------------------------------------------- process memory */

void *xx_io_platform_process_open(uint64_t pid)
{
    HANDLE handle;
    if (pid == 0) {
        pid = (uint64_t)GetCurrentProcessId();
    }
    handle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    return (void *)handle;
}

ssize_t xx_io_platform_process_read(void *handle, uint64_t addr, void *buf, size_t n)
{
    SIZE_T done = 0;
    if (!handle || !buf) {
        return -1;
    }
    if (!ReadProcessMemory((HANDLE)handle, (LPCVOID)(uintptr_t)addr, buf, n, &done)) {
        return done > 0 ? (ssize_t)done : -1;
    }
    return (ssize_t)done;
}

ssize_t xx_io_platform_process_write(void *handle, uint64_t addr, const void *buf, size_t n)
{
    SIZE_T done = 0;
    if (!handle || !buf) {
        return -1;
    }
    if (!WriteProcessMemory((HANDLE)handle, (LPVOID)(uintptr_t)addr, buf, n, &done)) {
        return done > 0 ? (ssize_t)done : -1;
    }
    FlushInstructionCache((HANDLE)handle, (LPCVOID)(uintptr_t)addr, done);
    return (ssize_t)done;
}

int xx_io_platform_process_close(void *handle)
{
    if (!handle) {
        return -1;
    }
    return CloseHandle((HANDLE)handle) ? 0 : -1;
}

void *xx_io_platform_process_adopt(void *native)
{
    /* A HANDLE is used directly by the read/write hooks. */
    return native;
}

#endif /* _WIN32 */
