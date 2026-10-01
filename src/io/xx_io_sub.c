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

/** @file xx_io_sub.c @brief Borrowed, bounded I/O device views. */
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

typedef struct {
    xx_io_device *parent;
    int64_t offset;
    int64_t size;
    int64_t pos;
    bool read_only;
} xx_io_sub_state;

static int xx_io_sub_close_cb(xx_io_device *self);

static xx_io_sub_state *xx_io_sub_state_of(const xx_io_device *self) {
    if (!self || self->close != xx_io_sub_close_cb) return NULL;
    return (xx_io_sub_state *)self->priv;
}

static ssize_t xx_io_sub_transfer(xx_io_device *self, void *read_buf,
                                  const void *write_buf, size_t n, bool writing) {
    xx_io_sub_state *st = xx_io_sub_state_of(self);
    size_t done = 0;
    if (!st || (writing && st->read_only)) return -1;
    if (!n) return 0;
    if (writing ? !write_buf : !read_buf) return -1;
    if (st->pos == st->size) return 0;
    if (n > (size_t)PTRDIFF_MAX) n = (size_t)PTRDIFF_MAX;
    if ((uint64_t)n > (uint64_t)(st->size - st->pos))
        n = (size_t)(st->size - st->pos);
    while (done < n) {
        size_t request = n - done;
        ssize_t transferred;
        if (xx_io_seek64(st->parent, st->offset + st->pos, SEEK_SET) != 0)
            return done ? (ssize_t)done : -1;
        if (writing) {
            transferred = xx_io_write(st->parent,
                                       (const unsigned char *)write_buf + done, request);
        } else {
            transferred = xx_io_read(st->parent,
                                      (unsigned char *)read_buf + done, request);
        }
        if (transferred <= 0 || (size_t)transferred > request)
            return done ? (ssize_t)done : -1;
        done += (size_t)transferred;
        st->pos += (int64_t)transferred;
    }
    return (ssize_t)done;
}

static ssize_t xx_io_sub_read_cb(xx_io_device *self, void *buf, size_t n) {
    return xx_io_sub_transfer(self, buf, NULL, n, false);
}

static ssize_t xx_io_sub_write_cb(xx_io_device *self, const void *buf, size_t n) {
    return xx_io_sub_transfer(self, NULL, buf, n, true);
}

static int xx_io_sub_seek64_cb(xx_io_device *self, int64_t off, int whence) {
    xx_io_sub_state *st = xx_io_sub_state_of(self);
    int64_t base;
    if (!st) return -1;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = st->pos; break;
        case SEEK_END: base = st->size; break;
        default: return -1;
    }
    if (off < -base || off > st->size - base) return -1;
    st->pos = base + off;
    return 0;
}

static int xx_io_sub_seek_cb(xx_io_device *self, long off, int whence) {
    return xx_io_sub_seek64_cb(self, (int64_t)off, whence);
}

static int64_t xx_io_sub_tell_cb(xx_io_device *self) {
    xx_io_sub_state *st = xx_io_sub_state_of(self);
    return st ? st->pos : -1;
}

static int64_t xx_io_sub_size_cb(xx_io_device *self) {
    xx_io_sub_state *st = xx_io_sub_state_of(self);
    return st ? st->size : -1;
}

static int xx_io_sub_close_cb(xx_io_device *self) {
    xx_io_sub_state *st = xx_io_sub_state_of(self);
    if (!st) return -1;
    xx_mem_free(st);
    xx_mem_free(self);
    return 0;
}

static xx_io_device *xx_io_sub_open_impl(xx_io_device *parent, int64_t offset,
                                        int64_t size, bool read_only) {
    xx_io_sub_state *st;
    xx_io_device *device;
    int64_t parent_size;
    if (!parent || (!parent->seek64 && !parent->seek) ||
        (read_only ? !parent->read : (!parent->read && !parent->write)) ||
        offset < 0 || size < 0 || size > INT64_MAX - offset) return NULL;
    parent_size = xx_io_total_size(parent);
    if (parent_size >= 0 &&
        (offset > parent_size || size > parent_size - offset)) return NULL;
    st = (xx_io_sub_state *)xx_mem_calloc(1, sizeof(*st));
    if (!st) return NULL;
    device = (xx_io_device *)xx_mem_calloc(1, sizeof(*device));
    if (!device) {
        xx_mem_free(st);
        return NULL;
    }
    st->parent = parent;
    st->offset = offset;
    st->size = size;
    st->read_only = read_only;
    device->read = xx_io_sub_read_cb;
    device->write = xx_io_sub_write_cb;
    device->seek = xx_io_sub_seek_cb;
    device->seek64 = xx_io_sub_seek64_cb;
    device->tell = xx_io_sub_tell_cb;
    device->close = xx_io_sub_close_cb;
    device->total_size = xx_io_sub_size_cb;
    device->get_total_size = xx_io_sub_size_cb;
    device->size = xx_io_sub_size_cb;
    device->priv = st;
    return device;
}

xx_io_device *xx_io_sub_open(xx_io_device *parent, int64_t offset, int64_t size) {
    return xx_io_sub_open_impl(parent, offset, size, false);
}

xx_io_device *io_sub_open(xx_io_device *parent, int64_t offset, int64_t size) {
    return xx_io_sub_open(parent, offset, size);
}

xx_io_device *xx_io_sub_open_ro(xx_io_device *parent, int64_t offset, int64_t size) {
    return xx_io_sub_open_impl(parent, offset, size, true);
}

xx_io_device *io_sub_open_ro(xx_io_device *parent, int64_t offset, int64_t size) {
    return xx_io_sub_open_ro(parent, offset, size);
}

bool xx_io_sub_get_range(const xx_io_device *device, xx_io_device **parent,
                         int64_t *offset, int64_t *size) {
    xx_io_sub_state *st = xx_io_sub_state_of(device);
    if (!st) return false;
    if (parent) *parent = st->parent;
    if (offset) *offset = st->offset;
    if (size) *size = st->size;
    return true;
}
