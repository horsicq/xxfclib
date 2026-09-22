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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef SEEK_SET
#  define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#  define SEEK_CUR 1
#endif
#ifndef SEEK_END
#  define SEEK_END 2
#endif

static int xx_win_mode_has_char(const char *str, char c) {
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

static wchar_t *xx_win_utf8_path(const char *path, wchar_t *stack,
                                 size_t stack_count) {
    int length;
    wchar_t *result = stack;
    HANDLE heap = GetProcessHeap();
    if (!path || !stack || stack_count == 0U) return NULL;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 path, -1, NULL, 0);
    if (length <= 0) return NULL;
    if ((size_t)length > stack_count) {
        result = (wchar_t *)HeapAlloc(heap, 0,
                                     (SIZE_T)length * sizeof(wchar_t));
        if (!result) return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            result, length) <= 0) {
        if (result != stack) HeapFree(heap, 0, result);
        return NULL;
    }
    return result;
}

static void xx_win_free_path(wchar_t *path, wchar_t *stack) {
    if (path && path != stack) HeapFree(GetProcessHeap(), 0, path);
}

void* xx_io_platform_file_open(const char *path, const char *mode) {
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
        dwCreationDisposition = xx_win_mode_has_char(mode, 'x')
                                    ? CREATE_NEW : CREATE_ALWAYS;
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

    /* Convert UTF-8 path to UTF-16 wide string */
    int path_wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (path_wlen <= 0) {
        return NULL;
    }

    wchar_t stack_wpath[MAX_PATH];
    wchar_t *wpath = stack_wpath;
    HANDLE hHeap = GetProcessHeap();

    if ((size_t)path_wlen > (sizeof(stack_wpath) / sizeof(stack_wpath[0]))) {
        wpath = (wchar_t*)HeapAlloc(hHeap, 0, (SIZE_T)path_wlen * sizeof(wchar_t));
        if (!wpath) {
            return NULL;
        }
    }

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, path_wlen) <= 0) {
        if (wpath != stack_wpath) {
            HeapFree(hHeap, 0, wpath);
        }
        return NULL;
    }

    HANDLE hFile = CreateFileW(
        wpath,
        dwDesiredAccess,
        dwShareMode,
        NULL,
        dwCreationDisposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (wpath != stack_wpath) {
        HeapFree(hHeap, 0, wpath);
    }

    if (hFile == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    if (append) {
        LARGE_INTEGER liZero;
        liZero.QuadPart = 0;
        SetFilePointerEx(hFile, liZero, NULL, FILE_END);
    }

    return (void*)hFile;
}

ssize_t xx_io_platform_file_read(void *handle, void *buf, size_t n) {
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

ssize_t xx_io_platform_file_write(void *handle, const void *buf, size_t n) {
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

int xx_io_platform_file_seek(void *handle, long off, int whence) {
    return xx_io_platform_file_seek64(handle, (int64_t)off, whence);
}

int xx_io_platform_file_seek64(void *handle, int64_t off, int whence) {
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    int64_t base;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = xx_io_platform_file_tell(handle);
            break;
        case SEEK_END:
            base = xx_io_platform_file_size(handle);
            break;
        default:
            return -1;
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

int64_t xx_io_platform_file_tell(void *handle) {
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

int xx_io_platform_file_close(void *handle) {
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

int64_t xx_io_platform_file_size(void *handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx((HANDLE)handle, &liSize)) {
        return -1;
    }

    return (int64_t)liSize.QuadPart;
}

bool xx_io_platform_file_exists_w(const wchar_t *path) {
    return path && path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

bool xx_io_platform_file_exists_a(const char *path) {
    wchar_t stack[MAX_PATH];
    wchar_t *wide = xx_win_utf8_path(path, stack,
                                     sizeof(stack) / sizeof(stack[0]));
    bool result = wide && xx_io_platform_file_exists_w(wide);
    xx_win_free_path(wide, stack);
    return result;
}

bool xx_io_platform_file_remove_w(const wchar_t *path) {
    return path && path[0] && DeleteFileW(path) != 0;
}

bool xx_io_platform_file_remove_a(const char *path) {
    wchar_t stack[MAX_PATH];
    wchar_t *wide = xx_win_utf8_path(path, stack,
                                     sizeof(stack) / sizeof(stack[0]));
    bool result = wide && xx_io_platform_file_remove_w(wide);
    xx_win_free_path(wide, stack);
    return result;
}

bool xx_io_platform_file_replace_w(const wchar_t *source,
                                   const wchar_t *destination,
                                   bool overwrite) {
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (!source || !source[0] || !destination || !destination[0]) return false;
    if (overwrite) flags |= MOVEFILE_REPLACE_EXISTING;
    return MoveFileExW(source, destination, flags) != 0;
}

bool xx_io_platform_file_replace_a(const char *source,
                                   const char *destination,
                                   bool overwrite) {
    wchar_t source_stack[MAX_PATH], destination_stack[MAX_PATH];
    wchar_t *source_wide = xx_win_utf8_path(
        source, source_stack, sizeof(source_stack) / sizeof(source_stack[0]));
    wchar_t *destination_wide = xx_win_utf8_path(
        destination, destination_stack,
        sizeof(destination_stack) / sizeof(destination_stack[0]));
    bool result = source_wide && destination_wide &&
        xx_io_platform_file_replace_w(source_wide, destination_wide,
                                      overwrite);
    xx_win_free_path(source_wide, source_stack);
    xx_win_free_path(destination_wide, destination_stack);
    return result;
}

bool xx_io_platform_create_dirs_w(const wchar_t *path, bool is_dir) {
    if (!path || !path[0]) {
        return false;
    }
    wchar_t temp[MAX_PATH];
    size_t len = 0;
    while (path[len] != L'\0') {
        len++;
    }
    if (len >= MAX_PATH) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        temp[i] = path[i];
    }
    temp[len] = L'\0';

    for (size_t i = 0; i < len; ++i) {
        if (temp[i] == L'/' || temp[i] == L'\\') {
            DWORD attributes;
            temp[i] = L'\0';
            if (i > 0 && temp[i - 1] != L':') {
                attributes = GetFileAttributesW(temp);
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    if (!CreateDirectoryW(temp, NULL)) return false;
                } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    return false;
                }
            }
            temp[i] = L'\\';
        }
    }
    if (is_dir) {
        DWORD attributes = GetFileAttributesW(temp);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryW(temp, NULL)) return false;
        } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                   (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            return false;
        }
    }
    return true;
}

bool xx_io_platform_create_dirs_a(const char *path, bool is_dir) {
    if (!path || !path[0]) {
        return false;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) {
        return false;
    }
    wchar_t stack_wpath[MAX_PATH];
    wchar_t *wpath = stack_wpath;
    HANDLE hHeap = GetProcessHeap();
    if ((size_t)wlen > MAX_PATH) {
        wpath = (wchar_t*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, (size_t)wlen * sizeof(wchar_t));
        if (!wpath) {
            return false;
        }
    }
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    bool res = xx_io_platform_create_dirs_w(wpath, is_dir);
    if (wpath != stack_wpath) {
        HeapFree(hHeap, 0, wpath);
    }
    return res;
}

bool xx_io_platform_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    if (!path || !path[0]) {
        return false;
    }

    if (dos_date != 0 || dos_time != 0) {
        FILETIME ftLocal, ftUtc;
        if (DosDateTimeToFileTime((WORD)dos_date, (WORD)dos_time, &ftLocal)) {
            if (LocalFileTimeToFileTime(&ftLocal, &ftUtc)) {
                DWORD flags = FILE_ATTRIBUTE_NORMAL;
                DWORD attr_existing = GetFileAttributesW(path);
                if (attr_existing != INVALID_FILE_ATTRIBUTES && (attr_existing & FILE_ATTRIBUTE_DIRECTORY)) {
                    flags = FILE_FLAG_BACKUP_SEMANTICS;
                }
                HANDLE h = CreateFileW(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING, flags, NULL);
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
            SetFileAttributesW(path, win_attrs);
        }
    }

    return true;
}

bool xx_io_platform_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    if (!path || !path[0]) {
        return false;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) {
        return false;
    }
    wchar_t stack_wpath[MAX_PATH];
    wchar_t *wpath = stack_wpath;
    HANDLE hHeap = GetProcessHeap();
    if ((size_t)wlen > MAX_PATH) {
        wpath = (wchar_t*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, (size_t)wlen * sizeof(wchar_t));
        if (!wpath) {
            return false;
        }
    }
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    bool res = xx_io_platform_apply_dos_time_and_attrs_w(wpath, dos_date, dos_time, attrs);
    if (wpath != stack_wpath) {
        HeapFree(hHeap, 0, wpath);
    }
    return res;
}


/* --- Path safety and entropy, shared by the archive readers --------------- */

/* The reserved device names. Creating one of these opens the device rather
 * than a file, so a reader must refuse the name before extracting. Lifted out
 * of xx_zip.c and xx_rar.c, which carried byte-identical copies. */
static wchar_t xx_io_ascii_upper(wchar_t value) {
    return value >= L'a' && value <= L'z' ? value - (L'a' - L'A') : value;
}

bool xx_io_platform_wsegment_is_reserved(const wchar_t *segment,
                                                size_t length) {
    size_t base = 0;
    wchar_t a, b, c;
    while (base < length && segment[base] != L'.') ++base;
    if (base < 3U) return false;
    a = xx_io_ascii_upper(segment[0]);
    b = xx_io_ascii_upper(segment[1]);
    c = xx_io_ascii_upper(segment[2]);
    if (base == 3U &&
        ((a == L'C' && b == L'O' && c == L'N') ||
         (a == L'P' && b == L'R' && c == L'N') ||
         (a == L'A' && b == L'U' && c == L'X') ||
         (a == L'N' && b == L'U' && c == L'L'))) return true;
    if (base == 4U &&
        ((a == L'C' && b == L'O' && c == L'M') ||
         (a == L'L' && b == L'P' && c == L'T')) &&
        ((segment[3] >= L'1' && segment[3] <= L'9') ||
         segment[3] == 0x00b9 || segment[3] == 0x00b2 ||
         segment[3] == 0x00b3)) return true;
    if (base == 6U &&
        ((a == L'C' && b == L'O' && c == L'N' &&
          xx_io_ascii_upper(segment[3]) == L'I' &&
          xx_io_ascii_upper(segment[4]) == L'N' && segment[5] == L'$') ||
         (a == L'C' && b == L'L' && c == L'O' &&
          xx_io_ascii_upper(segment[3]) == L'C' &&
          xx_io_ascii_upper(segment[4]) == L'K' && segment[5] == L'$')))
        return true;
    if (base == 7U && a == L'C' && b == L'O' && c == L'N' &&
        xx_io_ascii_upper(segment[3]) == L'O' &&
        xx_io_ascii_upper(segment[4]) == L'U' &&
        xx_io_ascii_upper(segment[5]) == L'T' && segment[6] == L'$')
        return true;
    return false;
}

bool xx_io_platform_secure_random(uint8_t *output, size_t size) {
    if (!output) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
{
        typedef LONG (WINAPI *xx_bcrypt_gen_random_fn)(void *, unsigned char *,
                                                        ULONG, ULONG);
        HMODULE module = LoadLibraryW(L"bcrypt.dll");
        xx_bcrypt_gen_random_fn generate;
        size_t offset = 0U;
        bool success = true;
        if (!module) {
            return false;
        }
        generate = (xx_bcrypt_gen_random_fn)(void *)GetProcAddress(
            module, "BCryptGenRandom");
        if (!generate) {
            FreeLibrary(module);
            return false;
        }
        while (offset < size) {
            size_t remaining = size - offset;
            ULONG amount = remaining > (size_t)ULONG_MAX
                ? ULONG_MAX : (ULONG)remaining;
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

wchar_t xx_io_platform_wseparator(void) {
    return L'\\';
}

#endif /* _WIN32 */
