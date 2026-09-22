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

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
/* The xx_rt_* runtime memory primitives that used to live here now sit in
 * src/memory/platforms/, beside the other memory platform code. */


/* ------------------------------------------------------------------------ */
/*  I/O                                                                      */
/* ------------------------------------------------------------------------ */

typedef struct {
    xx_io_volume volume;
    int64_t start;
    int64_t end;
    bool close_child;
} xx_io_volume_entry;

typedef struct {
    xx_io_volume_entry *entries;
    size_t count;
    int64_t size;
    int64_t pos;
} xx_io_multivolume_state;

static int xx_io_multivolume_close_cb(xx_io_device *self);

static xx_io_multivolume_state *xx_io_multivolume_state_of(xx_io_device *self) {
    if (!self || self->close != xx_io_multivolume_close_cb) return NULL;
    return (xx_io_multivolume_state *)self->priv;
}

/* Find the first range ending after pos, skipping any empty ranges. */
static size_t xx_io_multivolume_find(const xx_io_multivolume_state *st, int64_t pos) {
    size_t lo = 0, hi = st->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (st->entries[mid].end <= pos) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static ssize_t xx_io_multivolume_transfer(xx_io_device *self, void *read_buf,
                                         const void *write_buf, size_t n, bool writing) {
    xx_io_multivolume_state *st = xx_io_multivolume_state_of(self);
    size_t done = 0, index;
    if (!st) return -1;
    if (n == 0) return 0;
    if (writing ? !write_buf : !read_buf) return -1;
    if (st->pos == st->size) return 0;

    /* Keep both the returned ssize_t and position arithmetic representable. */
    if (n > (size_t)PTRDIFF_MAX) n = (size_t)PTRDIFF_MAX;
    if ((uint64_t)n > (uint64_t)(st->size - st->pos)) n = (size_t)(st->size - st->pos);
    index = xx_io_multivolume_find(st, st->pos);
    while (done < n && index < st->count) {
        const xx_io_volume_entry *entry = &st->entries[index];
        size_t request = n - done;
        ssize_t transferred;
        int64_t remaining = entry->end - st->pos;
        if (remaining == 0) {
            ++index;
            continue;
        }
        if ((uint64_t)request > (uint64_t)remaining) request = (size_t)remaining;
        if (xx_io_seek64(entry->volume.device,
                         entry->volume.offset + (st->pos - entry->start), SEEK_SET) != 0) {
            return done ? (ssize_t)done : -1;
        }
        if (writing) {
            transferred = xx_io_write(entry->volume.device,
                                      (const unsigned char *)write_buf + done, request);
        } else {
            transferred = xx_io_read(entry->volume.device, (unsigned char *)read_buf + done, request);
        }
        /* Do not skip truncated volumes or loop forever on zero progress. */
        if (transferred <= 0 || (size_t)transferred > request) {
            return done ? (ssize_t)done : -1;
        }
        done += (size_t)transferred;
        st->pos += (int64_t)transferred;
        if (st->pos == entry->end) ++index;
    }
    return (ssize_t)done;
}

static ssize_t xx_io_multivolume_read_cb(xx_io_device *self, void *buf, size_t n) {
    return xx_io_multivolume_transfer(self, buf, NULL, n, false);
}

static ssize_t xx_io_multivolume_write_cb(xx_io_device *self, const void *buf, size_t n) {
    return xx_io_multivolume_transfer(self, NULL, buf, n, true);
}

static int xx_io_multivolume_seek64_cb(xx_io_device *self, int64_t off, int whence) {
    xx_io_multivolume_state *st = xx_io_multivolume_state_of(self);
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

static int xx_io_multivolume_seek_cb(xx_io_device *self, long off, int whence) {
    return xx_io_multivolume_seek64_cb(self, (int64_t)off, whence);
}

static int64_t xx_io_multivolume_tell_cb(xx_io_device *self) {
    xx_io_multivolume_state *st = xx_io_multivolume_state_of(self);
    return st ? st->pos : -1;
}

static int64_t xx_io_multivolume_size_cb(xx_io_device *self) {
    xx_io_multivolume_state *st = xx_io_multivolume_state_of(self);
    return st ? st->size : -1;
}

static int xx_io_multivolume_close_cb(xx_io_device *self) {
    xx_io_multivolume_state *st = xx_io_multivolume_state_of(self);
    int result = 0;
    size_t i;
    if (!st) return -1;
    for (i = 0; i < st->count; ++i) {
        if (st->entries[i].close_child && xx_io_close(st->entries[i].volume.device) != 0) result = -1;
    }
    xx_mem_free(st->entries);
    xx_mem_free(st);
    xx_mem_free(self);
    return result;
}

xx_io_device *xx_io_multivolume_open(const xx_io_volume *volumes, size_t count, bool take_ownership) {
    xx_io_multivolume_state *st;
    xx_io_device *dev;
    int64_t total = 0;
    size_t i, j;
    if (!volumes || !count || count > SIZE_MAX / sizeof(xx_io_volume_entry)) return NULL;
    /* Validate all descriptors before acquiring ownership or moving any cursor. */
    for (i = 0; i < count; ++i) {
        xx_io_device *child = volumes[i].device;
        int64_t physical_size;
        if (!child || (!child->seek64 && !child->seek) ||
            (!child->read && !child->write) || (take_ownership && !child->close) ||
            volumes[i].offset < 0 || volumes[i].size < 0) return NULL;
        physical_size = xx_io_total_size(child);
        if (physical_size < 0 || volumes[i].offset > physical_size ||
            volumes[i].size > physical_size - volumes[i].offset ||
            volumes[i].size > INT64_MAX - total) return NULL;
        total += volumes[i].size;
    }
    st = (xx_io_multivolume_state *)xx_mem_calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->entries = (xx_io_volume_entry *)xx_mem_calloc(count, sizeof(*st->entries));
    if (!st->entries) {
        xx_mem_free(st);
        return NULL;
    }
    dev = (xx_io_device *)xx_mem_calloc(1, sizeof(*dev));
    if (!dev) {
        xx_mem_free(st->entries);
        xx_mem_free(st);
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        xx_io_volume_entry *entry = &st->entries[i];
        entry->volume = volumes[i];
        entry->start = st->size;
        st->size += entry->volume.size;
        entry->end = st->size;
        entry->close_child = take_ownership;
        if (take_ownership) {
            for (j = 0; j < i; ++j) {
                if (st->entries[j].volume.device == entry->volume.device) {
                    entry->close_child = false;
                    break;
                }
            }
        }
    }
    st->count = count;
    dev->priv = st;
    dev->read = xx_io_multivolume_read_cb;
    dev->write = xx_io_multivolume_write_cb;
    dev->seek = xx_io_multivolume_seek_cb;
    dev->seek64 = xx_io_multivolume_seek64_cb;
    dev->tell = xx_io_multivolume_tell_cb;
    dev->close = xx_io_multivolume_close_cb;
    dev->total_size = xx_io_multivolume_size_cb;
    dev->get_total_size = xx_io_multivolume_size_cb;
    dev->size = xx_io_multivolume_size_cb;
    return dev;
}

xx_io_device *io_multivolume_open(const xx_io_volume *volumes, size_t count, bool take_ownership) {
    return xx_io_multivolume_open(volumes, count, take_ownership);
}

xx_io_device *xx_io_multivolume_open_files(const char *const *paths, size_t count, const char *mode) {
    xx_io_volume *volumes;
    xx_io_device *dev = NULL;
    size_t i, opened = 0;
    if (!paths || !count || !mode || count > SIZE_MAX / sizeof(*volumes)) return NULL;
    if (!(mode[0] == 'r' && ((mode[1] == 'b' && mode[2] == '\0') ||
                           (mode[1] == '+' && mode[2] == 'b' && mode[3] == '\0')))) return NULL;
    volumes = (xx_io_volume *)xx_mem_calloc(count, sizeof(*volumes));
    if (!volumes) return NULL;
    for (i = 0; i < count; ++i) {
        if (!paths[i] || !paths[i][0]) break;
        volumes[i].device = xx_io_file_open(paths[i], mode);
        if (!volumes[i].device) break;
        ++opened;
        volumes[i].size = xx_io_total_size(volumes[i].device);
        if (volumes[i].size < 0) break;
    }
    if (i == count) dev = xx_io_multivolume_open(volumes, count, true);
    if (!dev) {
        for (i = 0; i < opened; ++i) xx_io_close(volumes[i].device);
    }
    xx_mem_free(volumes);
    return dev;
}

xx_io_device *io_multivolume_open_files(const char *const *paths, size_t count, const char *mode) {
    return xx_io_multivolume_open_files(paths, count, mode);
}

size_t xx_io_multivolume_count(xx_io_device *device) {
    xx_io_multivolume_state *st = xx_io_multivolume_state_of(device);
    return st ? st->count : 0;
}

bool xx_io_multivolume_get_volume(xx_io_device *device, size_t index,
                                 xx_io_volume *volume, int64_t *logical_offset) {
    xx_io_multivolume_state *st = xx_io_multivolume_state_of(device);
    if (!st || index >= st->count) return false;
    if (volume) *volume = st->entries[index].volume;
    if (logical_offset) *logical_offset = st->entries[index].start;
    return true;
}
