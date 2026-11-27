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
 * @file xx_io_file.c
 * @brief File-backed I/O device implementation.
 */

#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#endif

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "platforms/xx_io_platform.h"
#include "xx_io_policy.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <errno.h>
#else
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct xx_io_file_state {
    void *handle;
    char *path;
    int read_write_at;
    int read_only;
} xx_io_file_state;

/* Internal dispatch keeps an attached read cache coherent for direct calls
 * through the optional callback as well as calls through xx_io_resize. */
int xx_io_buffer_resize_dispatch(xx_io_device *device, int64_t new_size);

static int xx_io_file_close_cb(xx_io_device *self);

static xx_io_file_state *xx_io_file_state_of(xx_io_device *self)
{
    if (!self || self->close != xx_io_file_close_cb) return NULL;
    return (xx_io_file_state *)self->priv;
}

int xx_io_file_read_write_at_capability(xx_io_device *device)
{
    xx_io_file_state *state = xx_io_file_state_of(device);
    return state ? state->read_write_at : -1;
}

static ssize_t xx_io_file_read_cb(xx_io_device *self, void *buf, size_t n)
{
    xx_io_file_state *state = xx_io_file_state_of(self);
    if (!state || !state->handle) {
        return -1;
    }
    return xx_io_platform_file_read(state->handle, buf, n);
}

/* A failure that a temporary copy (no path) records is kept as
 * XX_IO_EXTRACT_TEMP_FAILED: no file of the member failed. */
static void xx_io_file_temp_failed(const xx_io_file_state *state, int kept)
{
    if (!state->path && !kept && xx_io_extract_errno()) xx_io_extract_restore(XX_IO_EXTRACT_TEMP_FAILED);
}

static ssize_t xx_io_file_write_cb(xx_io_device *self, const void *buf, size_t n)
{
    xx_io_file_state *state = xx_io_file_state_of(self);
    int kept;
    ssize_t written;
    if (!state || !state->handle) {
        return -1;
    }
    kept = xx_io_extract_errno();
    written = xx_io_platform_file_write(state->handle, buf, n);
    xx_io_file_temp_failed(state, kept);
    return written;
}

static int xx_io_file_resize_cb(xx_io_device *self, int64_t new_size)
{
    xx_io_file_state *state = xx_io_file_state_of(self);
    int kept, result;
    if (!state || !state->handle) return -1;
    if (self->buffer_state) return xx_io_buffer_resize_dispatch(self, new_size);
    kept = xx_io_extract_errno();
    result = xx_io_platform_file_resize(state->handle, new_size);
    xx_io_file_temp_failed(state, kept);
    return result;
}

static int xx_io_file_seek_cb(xx_io_device *self, long off, int whence)
{
    xx_io_file_state *state = xx_io_file_state_of(self);
    if (!state || !state->handle) {
        return -1;
    }
    return xx_io_platform_file_seek(state->handle, off, whence);
}

static int xx_io_file_seek64_cb(xx_io_device *self, int64_t off, int whence)
{
    xx_io_file_state *state = xx_io_file_state_of(self);
    if (!state || !state->handle) {
        return -1;
    }
    return xx_io_platform_file_seek64(state->handle, off, whence);
}

static int64_t xx_io_file_tell_cb(xx_io_device *self)
{
    xx_io_file_state *state = xx_io_file_state_of(self);
    if (!state || !state->handle) {
        return -1;
    }
    return xx_io_platform_file_tell(state->handle);
}

static int xx_io_file_close_cb(xx_io_device *self)
{
    xx_io_file_state *state;
    int rc = 0;
    if (!self) {
        return -1;
    }
    state = xx_io_file_state_of(self);
    if (state) {
        if (state->handle) {
            int kept = xx_io_extract_errno();
            rc = xx_io_platform_file_close(state->handle);
            xx_io_file_temp_failed(state, kept);
        }
        xx_mem_free(state->path);
        xx_mem_free(state);
    }
    xx_mem_free(self);
    return rc;
}

static int64_t xx_io_file_total_size_cb(xx_io_device *self)
{
    xx_io_file_state *state = xx_io_file_state_of(self);
    if (!state || !state->handle) {
        return -1;
    }
    if (state->read_only) {
        return xx_io_platform_file_size_read_only(state->handle);
    }
    return xx_io_platform_file_size(state->handle);
}

/* Allocate the file state and device structure before opening the native
 * handle so a path that cannot afford cleanup (such as staging) never fails
 * after creating the file on disk. */
static xx_io_device *xx_io_file_alloc(const char *path, const char *mode)
{
    xx_io_file_state *state;
    xx_io_device *dev;
    size_t path_size;

    path_size = path ? xx_rt_strlen(path) : 0U;
    if (path_size == SIZE_MAX) {
        return NULL;
    }
    state = (xx_io_file_state *)xx_mem_calloc(1, sizeof(*state));
    if (!state) {
        return NULL;
    }
    if (path) {
        state->path = (char *)xx_mem_alloc(path_size + 1U);
        if (!state->path) {
            xx_mem_free(state);
            return NULL;
        }
        xx_rt_memcpy(state->path, path, path_size + 1U);
    }
    if (mode) {
        int update = 0, append = 0;
        const char *p;
        for (p = mode; *p; ++p) {
            if (*p == '+') update = 1;
            if (*p == 'a') append = 1;
        }
        state->read_write_at = update && !append;
        state->read_only = mode[0] == 'r' && !update;
    }

    dev = (xx_io_device *)xx_mem_calloc(1, sizeof(xx_io_device));
    if (!dev) {
        xx_mem_free(state->path);
        xx_mem_free(state);
        return NULL;
    }

    dev->read = xx_io_file_read_cb;
    dev->write = xx_io_file_write_cb;
    dev->seek = xx_io_file_seek_cb;
    dev->close = xx_io_file_close_cb;
    dev->total_size = xx_io_file_total_size_cb;
    dev->get_total_size = xx_io_file_total_size_cb;
    dev->size = xx_io_file_total_size_cb;
    dev->priv = state;
    dev->seek64 = xx_io_file_seek64_cb;
    dev->tell = xx_io_file_tell_cb;
    return dev;
}

/* Bind an open native handle to a preallocated file device; cannot fail. */
static void xx_io_file_attach(xx_io_device *dev, void *handle, const char *mode)
{
    xx_io_file_state *state = (xx_io_file_state *)dev->priv;
    state->handle = handle;
    if (xx_io_platform_file_can_resize(handle, mode)) dev->resize = xx_io_file_resize_cb;
}

static xx_io_device *xx_io_file_wrap(void *handle, const char *path, const char *mode)
{
    xx_io_device *dev;
    if (!handle) {
        return NULL;
    }

    dev = xx_io_file_alloc(path, mode);
    if (!dev) {
        xx_io_platform_file_close(handle);
        return NULL;
    }
    xx_io_file_attach(dev, handle, mode);

    return dev;
}

xx_io_device *xx_io_file_open(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
    return xx_io_file_wrap(xx_io_platform_file_open(path, mode), path, mode);
}

xx_io_device *xx_io_file_open_w(const wchar_t *path, const char *mode)
{
    if (!path || !mode) return NULL;
    return xx_io_file_wrap(xx_io_platform_file_open_w(path, mode), NULL, mode);
}

xx_io_device *io_file_open_w(const wchar_t *path, const char *mode)
{
    return xx_io_file_open_w(path, mode);
}

xx_io_device *xx_io_temp_open(void)
{
    if (xx_io_memory_only_active()) return xx_io_memory_temp_open();
    return xx_io_file_wrap(xx_io_platform_temp_open(), NULL, "w+b");
}

bool xx_io_temp_free_space(uint64_t *bytes)
{
    uint64_t value;
    if (!bytes || !xx_io_platform_temp_free_space(&value)) return false;
    *bytes = value;
    return true;
}

const char *xx_io_source_path(xx_io_device *device)
{
    xx_io_file_state *state;
    xx_io_volume volume;
    xx_io_device *buffer_source;
    int64_t child_size;
    while (device) {
        buffer_source = xx_io_buffer_source(device);
        if (buffer_source) {
            device = buffer_source;
            continue;
        }
        state = xx_io_file_state_of(device);
        if (state) return state->path;
        if (xx_io_multivolume_count(device) != 1U || !xx_io_multivolume_get_volume(device, 0U, &volume, NULL)) return NULL;
        child_size = xx_io_total_size(volume.device);
        if (volume.offset != 0 || child_size < 0 || volume.size != child_size) return NULL;
        device = volume.device;
    }
    return NULL;
}

xx_io_device *io_file_open(const char *path, const char *mode)
{
    return xx_io_file_open(path, mode);
}

bool xx_io_file_exists_a(const char *path)
{
    return xx_io_platform_file_exists_a(path);
}

bool xx_io_file_exists_w(const wchar_t *path)
{
    return xx_io_platform_file_exists_w(path);
}

bool xx_io_file_remove_a(const char *path)
{
    return xx_io_platform_file_remove_a(path);
}

bool xx_io_file_remove_w(const wchar_t *path)
{
    return xx_io_platform_file_remove_w(path);
}

bool xx_io_file_replace_a(const char *source, const char *destination, bool overwrite)
{
    return xx_io_platform_file_replace_a(source, destination, overwrite);
}

bool xx_io_file_replace_w(const wchar_t *source, const wchar_t *destination, bool overwrite)
{
    return xx_io_platform_file_replace_w(source, destination, overwrite);
}

/* The last name of path, without the trailing dots and spaces that Windows
 * drops from it. */
static const char *xx_io_stage_leaf(const char *path, size_t *length)
{
    const char *leaf = path;
    size_t index;
    for (index = 0U; path[index]; ++index) {
        if (path[index] == '/' || path[index] == '\\') leaf = path + index + 1U;
    }
    *length = (size_t)(path + index - leaf);
    while (*length && (leaf[*length - 1U] == '.' || leaf[*length - 1U] == ' ')) --*length;
    return leaf;
}

/* Whether a file system that folds ASCII case reads both last names as one. */
static bool xx_io_stage_same_leaf(const char *a, const char *b)
{
    size_t length_a, length_b, index;
    const char *leaf_a = xx_io_stage_leaf(a, &length_a);
    const char *leaf_b = xx_io_stage_leaf(b, &length_b);
    if (length_a != length_b) return false;
    for (index = 0U; index < length_a; ++index) {
        unsigned char x = (unsigned char)leaf_a[index], y = (unsigned char)leaf_b[index];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + ('a' - 'A'));
        if (x != y) return false;
    }
    return true;
}

#if defined(_WIN32)
#define XX_IO_STAGE_WIN_PATH_LIMIT 32767U

static int xx_io_stage_win_errno(DWORD error)
{
    switch (error) {
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION: return EACCES;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: return ENOSPC;
        case ERROR_FILENAME_EXCED_RANGE:
        case ERROR_BUFFER_OVERFLOW: return ENAMETOOLONG;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS: return EEXIST;
        case ERROR_WRITE_PROTECT: return EROFS;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: return ENOENT;
        case ERROR_DIRECTORY: return ENOTDIR;
        case ERROR_INVALID_NAME: return EINVAL;
        default: return EIO;
    }
}

static wchar_t *xx_io_stage_win_alloc(size_t count)
{
    HANDLE heap = GetProcessHeap();
    if (!heap || count > (SIZE_MAX / sizeof(wchar_t))) return NULL;
    return (wchar_t *)HeapAlloc(heap, 0, (SIZE_T)count * sizeof(wchar_t));
}

static void xx_io_stage_win_free_path(wchar_t *path, wchar_t *stack)
{
    if (path && path != stack) {
        HANDLE heap = GetProcessHeap();
        if (heap) HeapFree(heap, 0, path);
    }
}

static wchar_t *xx_io_stage_win_codepage_path(const char *path, UINT codepage, wchar_t *stack, size_t stack_count)
{
    int length;
    wchar_t *result = stack;
    if (!path || !stack || !stack_count) return NULL;
    length = MultiByteToWideChar(codepage, 0, path, -1, NULL, 0);
    if (length <= 0 || (size_t)length > XX_IO_STAGE_WIN_PATH_LIMIT) return NULL;
    if ((size_t)length > stack_count) {
        result = xx_io_stage_win_alloc((size_t)length);
        if (!result) return NULL;
    }
    if (MultiByteToWideChar(codepage, 0, path, -1, result, length) <= 0) {
        xx_io_stage_win_free_path(result, stack);
        return NULL;
    }
    return result;
}

static wchar_t *xx_io_stage_win_copy_path(const wchar_t *path, size_t length, wchar_t *stack, size_t stack_count)
{
    wchar_t *result = stack;
    size_t i;
    if (length + 1U > stack_count) {
        result = xx_io_stage_win_alloc(length + 1U);
        if (!result) return NULL;
    }
    for (i = 0U; i <= length; ++i) result[i] = path[i];
    return result;
}

static wchar_t *xx_io_stage_win_api_path(const wchar_t *path, wchar_t *stack, size_t stack_count)
{
    size_t length = 0U, prefix, skip, i;
    DWORD needed, full_length;
    wchar_t *full, *result;
    HANDLE heap = GetProcessHeap();
    if (!path || !stack || !stack_count || !heap) return NULL;
    while (length < XX_IO_STAGE_WIN_PATH_LIMIT && path[length]) ++length;
    if (!length || length == XX_IO_STAGE_WIN_PATH_LIMIT) return NULL;
    if (length >= 4U && path[0] == L'\\' && path[1] == L'\\' && (path[2] == L'?' || path[2] == L'.') && path[3] == L'\\')
        return xx_io_stage_win_copy_path(path, length, stack, stack_count);
    needed = GetFullPathNameW(path, 0, NULL, NULL);
    if (!needed || needed > XX_IO_STAGE_WIN_PATH_LIMIT) return NULL;
    full = xx_io_stage_win_alloc((size_t)needed);
    if (!full) return NULL;
    full_length = GetFullPathNameW(path, needed, full, NULL);
    if (!full_length || full_length >= needed) {
        HeapFree(heap, 0, full);
        return NULL;
    }
    if (full_length < MAX_PATH && length < MAX_PATH) {
        HeapFree(heap, 0, full);
        return xx_io_stage_win_copy_path(path, length, stack, stack_count);
    }
    for (i = 0U; i < full_length; ++i)
        if (full[i] == L'/') full[i] = L'\\';
    if (full_length < MAX_PATH - 12U) {
        result = xx_io_stage_win_copy_path(full, full_length, stack, stack_count);
        HeapFree(heap, 0, full);
        return result;
    }
    skip = full[0] == L'\\' && full[1] == L'\\' ? 2U : 0U;
    prefix = skip ? 8U : 4U;
    length = prefix + (size_t)full_length - skip;
    if (length + 1U > XX_IO_STAGE_WIN_PATH_LIMIT) {
        HeapFree(heap, 0, full);
        return NULL;
    }
    result = length + 1U <= stack_count ? stack : xx_io_stage_win_alloc(length + 1U);
    if (result) {
        static const wchar_t local_prefix[] = L"\\\\?\\";
        static const wchar_t unc_prefix[] = L"\\\\?\\UNC\\";
        const wchar_t *text = skip ? unc_prefix : local_prefix;
        for (i = 0U; i < prefix; ++i) result[i] = text[i];
        for (i = skip; i <= full_length; ++i) result[prefix + i - skip] = full[i];
    }
    HeapFree(heap, 0, full);
    return result;
}

static void *xx_io_stage_open_handle(const char *path)
{
    wchar_t text_stack[MAX_PATH], api_stack[MAX_PATH];
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD error = ERROR_SUCCESS;
    unsigned int attempt;
    if (!xx_io_policy_file_open_allowed("wbx") || !path) return NULL;
    /* Retain the UTF-8 then CP_ACP open order of xx_io_platform_file_open,
     * requesting DELETE access so alias cleanup deletes via this handle. */
    for (attempt = 0U; attempt < 2U; ++attempt) {
        wchar_t *text = xx_io_stage_win_codepage_path(path, attempt ? CP_ACP : CP_UTF8, text_stack, MAX_PATH);
        wchar_t *api;
        if (!text) continue;
        api = xx_io_stage_win_api_path(text, api_stack, MAX_PATH);
        if (api) {
            handle = CreateFileW(api, GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (handle == INVALID_HANDLE_VALUE) error = GetLastError();
            xx_io_stage_win_free_path(api, api_stack);
        }
        xx_io_stage_win_free_path(text, text_stack);
        if (handle != INVALID_HANDLE_VALUE) break;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        if (error != ERROR_SUCCESS) xx_io_extract_fail(xx_io_stage_win_errno(error));
        return NULL;
    }
    xx_io_extract_opened();
    return (void *)handle;
}

static bool xx_io_stage_win_delete(void *handle, DWORD *error_out)
{
    typedef struct xx_win_file_disposition_info {
        BOOLEAN DeleteFile;
    } xx_win_file_disposition_info;
    typedef struct xx_win_file_disposition_info_ex {
        DWORD Flags;
    } xx_win_file_disposition_info_ex;
    typedef BOOL(WINAPI * xx_win_set_file_info_fn)(HANDLE, int, LPVOID, DWORD);
    HMODULE module;
    xx_win_set_file_info_fn set_info;
    BOOL deleted = FALSE;
    DWORD error = ERROR_SUCCESS;

    if (!xx_io_policy_mutation_allowed()) {
        *error_out = ERROR_ACCESS_DENIED;
        xx_io_extract_fail(EACCES);
        return false;
    }
    if (!handle || (HANDLE)handle == INVALID_HANDLE_VALUE) {
        *error_out = ERROR_INVALID_HANDLE;
        xx_io_extract_fail(EBADF);
        return false;
    }
    module = GetModuleHandleW(L"kernel32.dll");
    set_info = module ? (xx_win_set_file_info_fn)(void (*)(void))GetProcAddress(module, "SetFileInformationByHandle") : NULL;
    if (!set_info) {
        *error_out = ERROR_CALL_NOT_IMPLEMENTED;
        xx_io_extract_fail(EIO);
        return false;
    }
    {
        xx_win_file_disposition_info_ex ex_info;
        /* FileDispositionInfoEx (21): FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS.
         * Unlinks the entry on close even if another handle is open. */
        ex_info.Flags = 0x00000001UL | 0x00000002UL;
        deleted = set_info((HANDLE)handle, 21, &ex_info, sizeof(ex_info));
        if (!deleted) error = GetLastError();
    }
    if (!deleted) {
        xx_win_file_disposition_info info;
        /* FileDispositionInfo (4) fallback for older Windows or filesystems without Ex support. */
        info.DeleteFile = TRUE;
        deleted = set_info((HANDLE)handle, 4, &info, sizeof(info));
        if (!deleted) {
            DWORD fallback_error = GetLastError();
            if (fallback_error != ERROR_SUCCESS) error = fallback_error;
        }
    }
    if (!deleted) {
        *error_out = error;
        xx_io_extract_fail(xx_io_stage_win_errno(error));
        return false;
    }
    return true;
}

static bool xx_io_stage_discard(xx_io_device *device, const char *candidate)
{
    xx_io_file_state *state = xx_io_file_state_of(device);
    void *handle = state ? state->handle : NULL;
    DWORD error = ERROR_SUCCESS;
    bool disposed = xx_io_stage_win_delete(handle, &error);
    int close_rc;
    (void)candidate;
    close_rc = xx_io_close(device);
    if (!disposed && close_rc == 0 && error != ERROR_SUCCESS) SetLastError(error);
    return disposed && close_rc == 0;
}
#else
static void *xx_io_stage_open_handle(const char *path)
{
    return xx_io_platform_file_open(path, "wbx");
}

static bool xx_io_stage_discard(xx_io_device *device, const char *candidate)
{
    xx_io_file_state *state = xx_io_file_state_of(device);
    FILE *stream = state ? (FILE *)state->handle : NULL;
    struct stat fd_info, path_info;
    int fd, saved_errno = 0, close_rc;
    bool unlinked = false;

    if (!xx_io_policy_mutation_allowed()) {
        saved_errno = EACCES;
        xx_io_extract_fail(saved_errno);
    } else if (!stream) {
        saved_errno = EBADF;
        xx_io_extract_fail(saved_errno);
    } else {
        errno = 0;
        fd = fileno(stream);
        if (fd < 0) {
            saved_errno = errno ? errno : EBADF;
            xx_io_extract_fail(saved_errno);
        } else if (fstat(fd, &fd_info) != 0) {
            saved_errno = errno ? errno : EIO;
            xx_io_extract_fail(saved_errno);
        } else if (lstat(candidate, &path_info) != 0) {
            saved_errno = errno ? errno : EIO;
            xx_io_extract_fail(saved_errno);
        } else if (!S_ISREG(fd_info.st_mode) || !S_ISREG(path_info.st_mode) || fd_info.st_dev != path_info.st_dev || fd_info.st_ino != path_info.st_ino) {
            /* Best effort on POSIX: without handle-based unlink, refuse to
             * unlink when candidate no longer names the opened file's inode. */
            saved_errno = EIO;
            xx_io_extract_fail(saved_errno);
        } else if (unlink(candidate) != 0) {
            saved_errno = errno ? errno : EIO;
            xx_io_extract_fail(saved_errno);
        } else {
            unlinked = true;
        }
    }
    close_rc = xx_io_close(device);
    if (!unlinked && close_rc == 0 && saved_errno) errno = saved_errno;
    return unlinked && close_rc == 0;
}
#endif

xx_io_device *xx_io_stage_open(const char *destination, const char *candidate, bool *retry)
{
    int kept = xx_io_extract_errno();
    bool present;
    xx_io_device *device;
    void *handle;
    if (!retry) return NULL;
    *retry = destination && candidate;
    if (!*retry || xx_io_stage_same_leaf(destination, candidate) || xx_io_platform_file_exists_a(candidate)) return NULL;
    /* Preallocate wrapper state before exclusive creation so open cannot leave
     * an orphaned stage file on allocation failure. */
    device = xx_io_file_alloc(candidate, "wbx");
    if (!device) {
        *retry = false;
        return NULL;
    }
    present = xx_io_platform_file_exists_a(destination);
    handle = xx_io_stage_open_handle(candidate);
    if (!handle) {
        (void)xx_io_file_close_cb(device);
        /* Taken meanwhile, which is no failure; else the folder refuses it. */
        if (xx_io_platform_file_exists_a(candidate)) xx_io_extract_restore(kept);
        else *retry = false;
        return NULL;
    }
    xx_io_file_attach(device, handle, "wbx");
    if (!present && xx_io_platform_file_exists_a(destination)) {
        /* The file system reads the candidate as the destination. */
        if (!xx_io_stage_discard(device, candidate)) {
            *retry = false;
            return NULL;
        }
        xx_io_extract_restore(kept);
        return NULL;
    }
    return device;
}

/* ========================================================================= */
/* --- Directory and File Attribute Operations                          --- */
/* ========================================================================= */

bool xx_io_create_dirs_w(const wchar_t *path, bool is_dir)
{
    return xx_io_platform_create_dirs_w(path, is_dir);
}

bool xx_io_create_dirs_a(const char *path, bool is_dir)
{
    return xx_io_platform_create_dirs_a(path, is_dir);
}

bool xx_io_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
    return xx_io_platform_apply_dos_time_and_attrs_w(path, dos_date, dos_time, attrs);
}

bool xx_io_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
    return xx_io_platform_apply_dos_time_and_attrs_a(path, dos_date, dos_time, attrs);
}
