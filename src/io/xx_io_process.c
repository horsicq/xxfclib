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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

/**
 * @file xx_io_process.c
 * @brief Process-memory backed I/O device.
 *
 * A thin xx_io_device over another process's address space. The stream offset
 * is an absolute memory address, so seek positions the address, and read /
 * write transfer memory at it and advance. There is no total size. The
 * platform layer (xx_io_platform_process_*) does the actual reads and writes.
 */

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include "platforms/xx_io_platform.h"

typedef struct {
    void    *handle;   /* platform process handle */
    uint64_t pos;      /* current absolute address */
    int      owns;     /* close the handle when the device closes */
} xx_io_process_state_t;

static ssize_t xx_io_process_read_cb(xx_io_device *self, void *buf, size_t n) {
    xx_io_process_state_t *st;
    ssize_t got;

    if (!self || !self->priv || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n > (size_t)PTRDIFF_MAX) {
        n = (size_t)PTRDIFF_MAX;
    }
    st = (xx_io_process_state_t *)self->priv;
    got = xx_io_platform_process_read(st->handle, st->pos, buf, n);
    if (got > 0) {
        st->pos += (uint64_t)got;
    }
    return got;
}

static ssize_t xx_io_process_write_cb(xx_io_device *self, const void *buf,
                                      size_t n) {
    xx_io_process_state_t *st;
    ssize_t put;

    if (!self || !self->priv || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n > (size_t)PTRDIFF_MAX) {
        n = (size_t)PTRDIFF_MAX;
    }
    st = (xx_io_process_state_t *)self->priv;
    put = xx_io_platform_process_write(st->handle, st->pos, buf, n);
    if (put > 0) {
        st->pos += (uint64_t)put;
    }
    return put;
}

static int xx_io_process_seek64_cb(xx_io_device *self, int64_t off,
                                   int whence) {
    xx_io_process_state_t *st;
    int64_t base;

    if (!self || !self->priv) {
        return -1;
    }
    st = (xx_io_process_state_t *)self->priv;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)st->pos;
            break;
        default:
            /* No size, so SEEK_END is meaningless for a process address. */
            return -1;
    }
    if (off < -base) {
        return -1;   /* would place the address before 0 */
    }
    st->pos = (uint64_t)(base + off);
    return 0;
}

static int xx_io_process_seek_cb(xx_io_device *self, long off, int whence) {
    return xx_io_process_seek64_cb(self, (int64_t)off, whence);
}

static int64_t xx_io_process_tell_cb(xx_io_device *self) {
    if (!self || !self->priv) {
        return -1;
    }
    return (int64_t)((xx_io_process_state_t *)self->priv)->pos;
}

static int64_t xx_io_process_total_size_cb(xx_io_device *self) {
    (void)self;
    return -1;   /* an address space has no fixed size */
}

static int xx_io_process_close_cb(xx_io_device *self) {
    xx_io_process_state_t *st;
    int rc = 0;

    if (!self) {
        return -1;
    }
    st = (xx_io_process_state_t *)self->priv;
    if (st) {
        if (st->handle && st->owns) {
            rc = xx_io_platform_process_close(st->handle);
        }
        xx_mem_free(st);
        self->priv = NULL;
    }
    xx_mem_free(self);
    return rc;
}

/* Wrap a platform process handle in a device. When owns is set the handle is
 * closed with the device; otherwise it is borrowed and left open. On failure
 * an owned handle is closed and a borrowed one is left untouched. */
static xx_io_device* xx_io_process_wrap(void *handle, int owns) {
    xx_io_process_state_t *st;
    xx_io_device *dev;

    if (!handle) {
        return NULL;
    }
    st = (xx_io_process_state_t *)xx_mem_calloc(1, sizeof(*st));
    if (!st) {
        if (owns) xx_io_platform_process_close(handle);
        return NULL;
    }
    st->handle = handle;
    st->pos = 0;
    st->owns = owns;

    dev = (xx_io_device *)xx_mem_calloc(1, sizeof(*dev));
    if (!dev) {
        if (owns) xx_io_platform_process_close(handle);
        xx_mem_free(st);
        return NULL;
    }
    dev->read           = xx_io_process_read_cb;
    dev->write          = xx_io_process_write_cb;
    dev->seek           = xx_io_process_seek_cb;
    dev->close          = xx_io_process_close_cb;
    dev->total_size     = xx_io_process_total_size_cb;
    dev->get_total_size = xx_io_process_total_size_cb;
    dev->size           = xx_io_process_total_size_cb;
    dev->priv           = st;
    dev->seek64         = xx_io_process_seek64_cb;
    dev->tell           = xx_io_process_tell_cb;
    return dev;
}

xx_io_device* xx_io_process_open(uint64_t pid) {
    return xx_io_process_wrap(xx_io_platform_process_open(pid), 1);
}

xx_io_device* xx_io_process_open_handle(void *native_handle) {
    /* Borrow the caller's handle: the device never closes it. */
    return xx_io_process_wrap(xx_io_platform_process_adopt(native_handle), 0);
}

xx_io_device* io_process_open_handle(void *native_handle) {
    return xx_io_process_open_handle(native_handle);
}

xx_io_device* io_process_open(uint64_t pid) {
    return xx_io_process_open(pid);
}
