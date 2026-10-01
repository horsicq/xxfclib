/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if defined(_WIN32)
#include "xx_settings_platform.h"
#include <windows.h>

static WCHAR *absolute_path(const char *path) {
    WCHAR *input = (WCHAR *)xx_rt_utf8_to_utf16(path), *result = NULL;
    DWORD size;
    if (!input) return NULL;
    size = GetFullPathNameW(input, 0, NULL, NULL);
    if (size && size <= 32768) {
        result = (WCHAR *)xx_rt_malloc((size_t)size * sizeof(WCHAR));
        if (result && !GetFullPathNameW(input, size, result, NULL)) { xx_rt_free(result); result = NULL; }
    }
    xx_rt_free(input);
    return result;
}

static size_t path_length(const WCHAR *path) { size_t size = 0; while (path[size]) ++size; return size; }

static bool create_parents(WCHAR *path) {
    unsigned int unc_parts = path[0] == L'\\' && path[1] == L'\\' ? 2 : 0;
    for (size_t i = 2; path[i]; ++i) {
        if (path[i] != L'\\' && path[i] != L'/') continue;
        if (unc_parts) { --unc_parts; continue; }
        if (i == 2 && path[1] == L':') continue;
        WCHAR separator = path[i];
        path[i] = 0;
        bool ok = CreateDirectoryW(path, NULL) != 0;
        if (!ok && GetLastError() == ERROR_ALREADY_EXISTS) {
            DWORD attributes = GetFileAttributesW(path);
            ok = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
        path[i] = separator;
        if (!ok) return false;
    }
    return true;
}

xxfc_status_t xx_settings_platform_read_file(const char *path, char **text, size_t *size) {
    WCHAR *wide = absolute_path(path);
    HANDLE file;
    LARGE_INTEGER length;
    DWORD read_size = 0;
    *text = NULL; *size = 0;
    if (!wide) return XXFC_ERR_INVALID_ARG;
    file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD error = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    xx_rt_free(wide);
    if (file == INVALID_HANDLE_VALUE) {
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? XXFC_OK : XXFC_ERR_IO;
    }
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 || length.QuadPart > XX_SETTINGS_MAX_FILE_SIZE) { CloseHandle(file); return XXFC_ERR_IO; }
    char *data = (char *)xx_rt_malloc((size_t)length.QuadPart + 1);
    if (!data) { CloseHandle(file); return XXFC_ERR_OUT_OF_MEMORY; }
    bool ok = ReadFile(file, data, (DWORD)length.QuadPart, &read_size, NULL) && read_size == length.QuadPart;
    CloseHandle(file);
    if (!ok) { xx_rt_free(data); return XXFC_ERR_IO; }
    data[read_size] = 0; *text = data; *size = read_size;
    return XXFC_OK;
}

xxfc_status_t xx_settings_platform_write_file(const char *path, const char *text, size_t size) {
    WCHAR *wide = absolute_path(path), *temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    static volatile LONG sequence = 0;
    char suffix[80];
    size_t length;
    if (!wide || size > XX_SETTINGS_MAX_FILE_SIZE) { xx_rt_free(wide); return XXFC_ERR_INVALID_ARG; }
    if (!create_parents(wide)) { xx_rt_free(wide); return XXFC_ERR_IO; }
    length = path_length(wide);
    temporary = (WCHAR *)xx_rt_malloc((length + 80) * sizeof(WCHAR));
    if (!temporary) { xx_rt_free(wide); return XXFC_ERR_OUT_OF_MEMORY; }
    xx_rt_memcpy(temporary, wide, length * sizeof(WCHAR));
    for (int attempt = 0; attempt < 100; ++attempt) {
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx-settings-%lu-%lu.tmp", (unsigned long)GetCurrentProcessId(), (unsigned long)InterlockedIncrement(&sequence));
        size_t i = 0;
        do { temporary[length + i] = (WCHAR)suffix[i]; } while (suffix[i++]);
        file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    bool ok = file != INVALID_HANDLE_VALUE;
    if (ok) {
        ok = WriteFile(file, text, (DWORD)size, &written, NULL) && written == size && FlushFileBuffers(file);
        if (!CloseHandle(file)) ok = false;
        if (ok) ok = MoveFileExW(temporary, wide, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok) DeleteFileW(temporary);
    }
    xx_rt_free(temporary); xx_rt_free(wide);
    return ok ? XXFC_OK : XXFC_ERR_IO;
}

bool xx_settings_platform_file_writable(const char *path) {
    WCHAR *wide = absolute_path(path);
    bool result = false;
    if (!wide) return false;
    for (;;) {
        DWORD attributes = GetFileAttributesW(wide);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!directory && (attributes & FILE_ATTRIBUTE_READONLY)) break;
            HANDLE handle = CreateFileW(wide, directory ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY : GENERIC_WRITE | DELETE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                        directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, NULL);
            result = handle != INVALID_HANDLE_VALUE;
            if (result) CloseHandle(handle);
            break;
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) break;
        size_t i = path_length(wide);
        while (i && wide[i - 1] != L'\\' && wide[i - 1] != L'/') --i;
        if (!i) break;
        if (i == 3 && wide[1] == L':') { wide[3] = 0; }
        else wide[i - 1] = 0;
    }
    xx_rt_free(wide);
    return result;
}
#endif
