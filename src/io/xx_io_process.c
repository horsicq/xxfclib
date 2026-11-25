/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Native implementation is included below in this translation unit. These
 * feature profiles must precede even the public header's system includes. */
#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#endif

#include "xxfclib/io/xx_io.h"
#include "platforms/xx_io_platform.h"

#include <errno.h> /* Error constants only; native code stores the error. */
#include <stdint.h>

typedef struct xx_io_process_state {
    void *handle;
    uint64_t pos;
    unsigned int access;
    int owns;
} xx_io_process_state_t;

static ssize_t xx_io_process_read_cb(xx_io_device *, void *, size_t);
static ssize_t xx_io_process_write_cb(xx_io_device *, const void *, size_t);
static int xx_io_process_seek64_cb(xx_io_device *, int64_t, int);
static int xx_io_process_close_cb(xx_io_device *);

static int xx_io_process_access_valid(xx_io_process_access_t access)
{
    return access == XX_IO_PROCESS_READ || access == XX_IO_PROCESS_WRITE || access == XX_IO_PROCESS_READ_WRITE;
}

bool xx_io_is_process(const xx_io_device *device)
{
    return device && device->priv && device->read == xx_io_process_read_cb && device->write == xx_io_process_write_cb && device->seek64 == xx_io_process_seek64_cb &&
           device->close == xx_io_process_close_cb;
}

static xx_io_process_state_t *xx_io_process_state_of(const xx_io_device *device)
{
    if (!xx_io_is_process(device)) {
        xx_io_platform_process_set_error(EINVAL);
        return NULL;
    }
    return (xx_io_process_state_t *)device->priv;
}

/* Use the last addressed byte rather than an exclusive end, so one byte at
 * UINT64_MAX remains representable by the direct-address API. */
static int xx_io_process_range_valid(uint64_t address, size_t n)
{
    if (n && (uint64_t)(n - 1) > UINT64_MAX - address) {
        xx_io_platform_process_set_error(EOVERFLOW);
        return 0;
    }
    return 1;
}

static xx_io_process_state_t *xx_io_process_transfer_state(xx_io_device *device, uint64_t address, const void *buffer, size_t n, unsigned int access)
{
    xx_io_process_state_t *state = xx_io_process_state_of(device);
    if (!state) return NULL;
    if (!xx_io_process_access_valid((xx_io_process_access_t)state->access)) {
        xx_io_platform_process_set_error(EINVAL);
        return NULL;
    }
    if (!(state->access & access)) {
        xx_io_platform_process_set_error(EACCES);
        return NULL;
    }
    if ((!buffer && n) || !xx_io_process_range_valid(address, n)) {
        if (!buffer && n) xx_io_platform_process_set_error(EINVAL);
        return NULL;
    }
    return state;
}

ssize_t xx_io_process_read_at(xx_io_device *device, uint64_t address, void *buffer, size_t n)
{
    xx_io_process_state_t *state = xx_io_process_transfer_state(device, address, buffer, n, XX_IO_PROCESS_READ);
    ssize_t result;
    if (!state) return -1;
    if (!n) return 0;
    if (n > (size_t)PTRDIFF_MAX) n = (size_t)PTRDIFF_MAX;
    result = xx_io_platform_process_read(state->handle, address, buffer, n);
    if (result > 0 && (size_t)result > n) {
        xx_io_platform_process_set_error(EIO);
        return -1;
    }
    return result;
}

ssize_t xx_io_process_write_at(xx_io_device *device, uint64_t address, const void *buffer, size_t n)
{
    xx_io_process_state_t *state = xx_io_process_transfer_state(device, address, buffer, n, XX_IO_PROCESS_WRITE);
    ssize_t result;
    if (!state) return -1;
    if (!n) return 0;
    if (n > (size_t)PTRDIFF_MAX) n = (size_t)PTRDIFF_MAX;
    result = xx_io_platform_process_write(state->handle, address, buffer, n);
    if (result > 0 && (size_t)result > n) {
        xx_io_platform_process_set_error(EIO);
        return -1;
    }
    return result;
}

bool xx_io_process_read_exact_at(xx_io_device *device, uint64_t address, void *buffer, size_t n)
{
    size_t offset = 0;
    if (!xx_io_process_transfer_state(device, address, buffer, n, XX_IO_PROCESS_READ)) return false;
    while (offset < n) {
        ssize_t got = xx_io_process_read_at(device, address + (uint64_t)offset, (unsigned char *)buffer + offset, n - offset);
        if (got < 0) return false;
        if (!got) {
            xx_io_platform_process_set_error(EIO);
            return false;
        }
        offset += (size_t)got;
    }
    return true;
}

bool xx_io_process_write_exact_at(xx_io_device *device, uint64_t address, const void *buffer, size_t n)
{
    size_t offset = 0;
    if (!xx_io_process_transfer_state(device, address, buffer, n, XX_IO_PROCESS_WRITE)) return false;
    while (offset < n) {
        ssize_t put = xx_io_process_write_at(device, address + (uint64_t)offset, (const unsigned char *)buffer + offset, n - offset);
        if (put < 0) return false;
        if (!put) {
            xx_io_platform_process_set_error(EIO);
            return false;
        }
        offset += (size_t)put;
    }
    return true;
}

static int xx_io_process_stream_range(xx_io_process_state_t *state, size_t n)
{
    /* The cursor itself must remain representable by xx_io_tell's int64_t. */
    if (state->pos > INT64_MAX || (uint64_t)n > (uint64_t)INT64_MAX - state->pos) {
        xx_io_platform_process_set_error(EOVERFLOW);
        return 0;
    }
    return 1;
}

static ssize_t xx_io_process_read_cb(xx_io_device *self, void *buffer, size_t n)
{
    xx_io_process_state_t *state = xx_io_process_state_of(self);
    ssize_t got;
    if (!state || !xx_io_process_stream_range(state, n)) return -1;
    got = xx_io_process_read_at(self, state->pos, buffer, n);
    if (got > 0) state->pos += (uint64_t)got;
    return got;
}

static ssize_t xx_io_process_write_cb(xx_io_device *self, const void *buffer, size_t n)
{
    xx_io_process_state_t *state = xx_io_process_state_of(self);
    ssize_t put;
    if (!state || !xx_io_process_stream_range(state, n)) return -1;
    put = xx_io_process_write_at(self, state->pos, buffer, n);
    if (put > 0) state->pos += (uint64_t)put;
    return put;
}

static int xx_io_process_seek64_cb(xx_io_device *self, int64_t offset, int origin)
{
    xx_io_process_state_t *state = xx_io_process_state_of(self);
    int64_t base;
    if (!state) return -1;
    if (state->pos > INT64_MAX) {
        xx_io_platform_process_set_error(EOVERFLOW);
        return -1;
    }
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = (int64_t)state->pos;
    else {
        xx_io_platform_process_set_error(origin == SEEK_END ? ESPIPE : EINVAL);
        return -1;
    }
    if (offset < -base) {
        xx_io_platform_process_set_error(EINVAL);
        return -1;
    }
    if (offset > INT64_MAX - base) {
        xx_io_platform_process_set_error(EOVERFLOW);
        return -1;
    }
    state->pos = (uint64_t)(base + offset);
    return 0;
}

static int xx_io_process_seek_cb(xx_io_device *self, long offset, int origin)
{
    return xx_io_process_seek64_cb(self, (int64_t)offset, origin);
}

static int64_t xx_io_process_tell_cb(xx_io_device *self)
{
    xx_io_process_state_t *state = xx_io_process_state_of(self);
    if (!state) return -1;
    if (state->pos > INT64_MAX) {
        xx_io_platform_process_set_error(EOVERFLOW);
        return -1;
    }
    return (int64_t)state->pos;
}

static int64_t xx_io_process_total_size_cb(xx_io_device *self)
{
    (void)self;
    return -1; /* A process address space has no fixed stream size. */
}

static int xx_io_process_close_cb(xx_io_device *self)
{
    xx_io_process_state_t *state = xx_io_process_state_of(self);
    int result = 0;
    if (!state) return -1;
    if (state->owns) result = xx_io_platform_process_close(state->handle);
    self->priv = NULL;
    xx_io_platform_process_free(state);
    xx_io_platform_process_free(self);
    return result;
}

int xx_io_process_close(xx_io_device *device)
{
    if (!xx_io_is_process(device)) {
        xx_io_platform_process_set_error(EINVAL);
        return -1;
    }
    return xx_io_process_close_cb(device);
}

/* Platform allocation returns zeroed storage. No xx_mem/xx_rt implementation
 * is required when a small consumer compiles this file by itself. */
static xx_io_device *xx_io_process_wrap(void *handle, int owns, xx_io_process_access_t access)
{
    xx_io_process_state_t *state;
    xx_io_device *device;
    if (!handle) return NULL; /* Preserve the platform open/adopt error. */
    state = (xx_io_process_state_t *)xx_io_platform_process_alloc(sizeof(*state));
    if (!state) {
        if (owns) (void)xx_io_platform_process_close(handle);
        xx_io_platform_process_set_error(ENOMEM);
        return NULL;
    }
    device = (xx_io_device *)xx_io_platform_process_alloc(sizeof(*device));
    if (!device) {
        if (owns) (void)xx_io_platform_process_close(handle);
        xx_io_platform_process_free(state);
        xx_io_platform_process_set_error(ENOMEM);
        return NULL;
    }
    state->handle = handle;
    state->access = (unsigned int)access;
    state->owns = owns;
    device->read = xx_io_process_read_cb;
    device->write = xx_io_process_write_cb;
    device->seek = xx_io_process_seek_cb;
    device->seek64 = xx_io_process_seek64_cb;
    device->tell = xx_io_process_tell_cb;
    device->close = xx_io_process_close_cb;
    device->total_size = xx_io_process_total_size_cb;
    device->get_total_size = xx_io_process_total_size_cb;
    device->size = xx_io_process_total_size_cb;
    device->priv = state;
    return device;
}

xx_io_device *xx_io_process_open_ex(uint64_t pid, xx_io_process_access_t access)
{
    if (!xx_io_process_access_valid(access)) {
        xx_io_platform_process_set_error(EINVAL);
        return NULL;
    }
    return xx_io_process_wrap(xx_io_platform_process_open_ex(pid, (unsigned int)access), 1, access);
}

xx_io_device *xx_io_process_open_ro(uint64_t pid)
{
    return xx_io_process_open_ex(pid, XX_IO_PROCESS_READ);
}

xx_io_device *xx_io_process_open(uint64_t pid)
{
    return xx_io_process_wrap(xx_io_platform_process_open(pid), 1, XX_IO_PROCESS_READ_WRITE);
}

xx_io_device *xx_io_process_open_handle_ex(void *native_handle, xx_io_process_access_t access)
{
    if (!xx_io_process_access_valid(access)) {
        xx_io_platform_process_set_error(EINVAL);
        return NULL;
    }
    return xx_io_process_wrap(xx_io_platform_process_adopt(native_handle), 0, access);
}

xx_io_device *xx_io_process_open_handle(void *native_handle)
{
    return xx_io_process_open_handle_ex(native_handle, XX_IO_PROCESS_READ_WRITE);
}

xx_io_device *io_process_open_handle(void *native_handle)
{
    return xx_io_process_open_handle(native_handle);
}

xx_io_device *io_process_open(uint64_t pid)
{
    return xx_io_process_open(pid);
}

/* Keep this long-standing source file self-contained for consumers with fixed
 * source lists; native process functions no longer live in the file backend. */
#ifdef _WIN32
#include "platforms/xx_io_process_windows.inc"
#else
#include "platforms/xx_io_process_posix.inc"
#endif
