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

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "platforms/xx_io_platform.h"
#include "xx_io_policy.h"

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

static xx_io_device *xx_io_file_wrap(void *handle, const char *path, const char *mode)
{
    xx_io_file_state *state;
    xx_io_device *dev;
    size_t path_size;
    if (!handle) {
        return NULL;
    }

    path_size = path ? xx_rt_strlen(path) : 0U;
    if (path_size == SIZE_MAX) {
        xx_io_platform_file_close(handle);
        return NULL;
    }
    state = (xx_io_file_state *)xx_mem_calloc(1, sizeof(*state));
    if (!state) {
        xx_io_platform_file_close(handle);
        return NULL;
    }
    if (path) {
        state->path = (char *)xx_mem_alloc(path_size + 1U);
        if (!state->path) {
            xx_mem_free(state);
            xx_io_platform_file_close(handle);
            return NULL;
        }
        xx_rt_memcpy(state->path, path, path_size + 1U);
    }
    state->handle = handle;
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
        xx_io_platform_file_close(handle);
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
    if (xx_io_platform_file_can_resize(handle, mode)) dev->resize = xx_io_file_resize_cb;

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

xx_io_device *xx_io_stage_open(const char *destination, const char *candidate, bool *retry)
{
    int kept = xx_io_extract_errno();
    bool present;
    xx_io_device *device;
    *retry = destination && candidate;
    if (!*retry || xx_io_stage_same_leaf(destination, candidate) || xx_io_platform_file_exists_a(candidate)) return NULL;
    present = xx_io_platform_file_exists_a(destination);
    device = xx_io_file_open(candidate, "wbx");
    if (!device) {
        /* Taken meanwhile, which is no failure; else the folder refuses it. */
        if (xx_io_platform_file_exists_a(candidate)) xx_io_extract_restore(kept);
        else *retry = false;
        return NULL;
    }
    if (!present && xx_io_platform_file_exists_a(destination)) {
        /* The file system reads the candidate as the destination. */
        xx_io_close(device);
        xx_io_platform_file_remove_a(candidate);
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
