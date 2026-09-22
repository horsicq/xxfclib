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
 * @file xx_io_mem.c
 * @brief In-memory buffer backed I/O device implementation.
 */

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

typedef struct {
    uint8_t       *buffer;
    const uint8_t *ro_buffer;
    size_t         size;
    size_t         pos;
    bool           is_read_only;
} xx_io_mem_state_t;

static ssize_t xx_io_mem_read_cb(xx_io_device *self, void *buf, size_t n) {
    if (!self || !self->priv || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n > (size_t)PTRDIFF_MAX) {
        n = (size_t)PTRDIFF_MAX;
    }

    xx_io_mem_state_t *st = (xx_io_mem_state_t*)self->priv;
    if (st->pos >= st->size) {
        return 0; /* EOF */
    }

    size_t avail = st->size - st->pos;
    size_t to_read = (n < avail) ? n : avail;

    const uint8_t *src = st->is_read_only ? (st->ro_buffer + st->pos) : (st->buffer + st->pos);
    xx_mem_copy(buf, src, to_read);
    st->pos += to_read;

    return (ssize_t)to_read;
}

static ssize_t xx_io_mem_write_cb(xx_io_device *self, const void *buf, size_t n) {
    if (!self || !self->priv || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n > (size_t)PTRDIFF_MAX) {
        n = (size_t)PTRDIFF_MAX;
    }

    xx_io_mem_state_t *st = (xx_io_mem_state_t*)self->priv;
    if (st->is_read_only) {
        return -1; /* Cannot write to read-only memory device */
    }
    if (st->pos >= st->size) {
        return 0; /* No space left in buffer */
    }

    size_t avail = st->size - st->pos;
    size_t to_write = (n < avail) ? n : avail;

    xx_mem_copy(st->buffer + st->pos, buf, to_write);
    st->pos += to_write;

    return (ssize_t)to_write;
}

static int xx_io_mem_seek64_cb(xx_io_device *self, int64_t off, int whence) {
    if (!self || !self->priv) {
        return -1;
    }

    xx_io_mem_state_t *st = (xx_io_mem_state_t*)self->priv;
    int64_t base;

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)st->pos;
            break;
        case SEEK_END:
            base = (int64_t)st->size;
            break;
        default:
            return -1;
    }

    /* Check both bounds before adding, including INT64_MIN offsets. */
    if (off < -base || off > (int64_t)st->size - base) {
        return -1; /* Out of buffer bounds */
    }

    st->pos = (size_t)(base + off);
    return 0;
}

static int xx_io_mem_seek_cb(xx_io_device *self, long off, int whence) {
    return xx_io_mem_seek64_cb(self, (int64_t)off, whence);
}

static int64_t xx_io_mem_tell_cb(xx_io_device *self) {
    if (!self || !self->priv) {
        return -1;
    }
    return (int64_t)((xx_io_mem_state_t*)self->priv)->pos;
}

static int xx_io_mem_close_cb(xx_io_device *self) {
    if (!self) {
        return -1;
    }
    if (self->priv) {
        xx_mem_free(self->priv);
        self->priv = NULL;
    }
    xx_mem_free(self);
    return 0;
}

static int64_t xx_io_mem_total_size_cb(xx_io_device *self) {
    if (!self || !self->priv) {
        return -1;
    }
    xx_io_mem_state_t *st = (xx_io_mem_state_t*)self->priv;
    return (int64_t)st->size;
}

xx_io_device* xx_io_mem_open(void *buf, size_t size) {
    if ((!buf && size > 0) || (uint64_t)size > INT64_MAX) {
        return NULL;
    }

    xx_io_mem_state_t *st = (xx_io_mem_state_t*)xx_mem_calloc(1, sizeof(xx_io_mem_state_t));
    if (!st) {
        return NULL;
    }

    st->buffer       = (uint8_t*)buf;
    st->ro_buffer    = NULL;
    st->size         = size;
    st->pos          = 0;
    st->is_read_only = false;

    xx_io_device *dev = (xx_io_device*)xx_mem_calloc(1, sizeof(xx_io_device));
    if (!dev) {
        xx_mem_free(st);
        return NULL;
    }

    dev->read           = xx_io_mem_read_cb;
    dev->write          = xx_io_mem_write_cb;
    dev->seek           = xx_io_mem_seek_cb;
    dev->close          = xx_io_mem_close_cb;
    dev->total_size     = xx_io_mem_total_size_cb;
    dev->get_total_size = xx_io_mem_total_size_cb;
    dev->size           = xx_io_mem_total_size_cb;
    dev->priv           = st;
    dev->seek64         = xx_io_mem_seek64_cb;
    dev->tell           = xx_io_mem_tell_cb;

    return dev;
}

xx_io_device* io_mem_open(void *buf, size_t size) {
    return xx_io_mem_open(buf, size);
}

xx_io_device* xx_io_mem_open_ro(const void *buf, size_t size) {
    if ((!buf && size > 0) || (uint64_t)size > INT64_MAX) {
        return NULL;
    }

    xx_io_mem_state_t *st = (xx_io_mem_state_t*)xx_mem_calloc(1, sizeof(xx_io_mem_state_t));
    if (!st) {
        return NULL;
    }

    st->buffer       = NULL;
    st->ro_buffer    = (const uint8_t*)buf;
    st->size         = size;
    st->pos          = 0;
    st->is_read_only = true;

    xx_io_device *dev = (xx_io_device*)xx_mem_calloc(1, sizeof(xx_io_device));
    if (!dev) {
        xx_mem_free(st);
        return NULL;
    }

    dev->read           = xx_io_mem_read_cb;
    dev->write          = xx_io_mem_write_cb;
    dev->seek           = xx_io_mem_seek_cb;
    dev->close          = xx_io_mem_close_cb;
    dev->total_size     = xx_io_mem_total_size_cb;
    dev->get_total_size = xx_io_mem_total_size_cb;
    dev->size           = xx_io_mem_total_size_cb;
    dev->priv           = st;
    dev->seek64         = xx_io_mem_seek64_cb;
    dev->tell           = xx_io_mem_tell_cb;

    return dev;
}

xx_io_device* io_mem_open_ro(const void *buf, size_t size) {
    return xx_io_mem_open_ro(buf, size);
}
