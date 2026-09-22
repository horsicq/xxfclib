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
#include "platforms/xx_io_platform.h"

static ssize_t xx_io_file_read_cb(xx_io_device *self, void *buf, size_t n) {
    if (!self || !self->priv) {
        return -1;
    }
    return xx_io_platform_file_read(self->priv, buf, n);
}

static ssize_t xx_io_file_write_cb(xx_io_device *self, const void *buf, size_t n) {
    if (!self || !self->priv) {
        return -1;
    }
    return xx_io_platform_file_write(self->priv, buf, n);
}

static int xx_io_file_seek_cb(xx_io_device *self, long off, int whence) {
    if (!self || !self->priv) {
        return -1;
    }
    return xx_io_platform_file_seek(self->priv, off, whence);
}

static int xx_io_file_seek64_cb(xx_io_device *self, int64_t off, int whence) {
    if (!self || !self->priv) {
        return -1;
    }
    return xx_io_platform_file_seek64(self->priv, off, whence);
}

static int64_t xx_io_file_tell_cb(xx_io_device *self) {
    if (!self || !self->priv) {
        return -1;
    }
    return xx_io_platform_file_tell(self->priv);
}

static int xx_io_file_close_cb(xx_io_device *self) {
    if (!self) {
        return -1;
    }
    int rc = 0;
    if (self->priv) {
        rc = xx_io_platform_file_close(self->priv);
        self->priv = NULL;
    }
    xx_mem_free(self);
    return rc;
}

static int64_t xx_io_file_total_size_cb(xx_io_device *self) {
    if (!self || !self->priv) {
        return -1;
    }
    return xx_io_platform_file_size(self->priv);
}

xx_io_device* xx_io_file_open(const char *path, const char *mode) {
    if (!path || !mode) {
        return NULL;
    }

    void *handle = xx_io_platform_file_open(path, mode);
    if (!handle) {
        return NULL;
    }

    xx_io_device *dev = (xx_io_device*)xx_mem_calloc(1, sizeof(xx_io_device));
    if (!dev) {
        xx_io_platform_file_close(handle);
        return NULL;
    }

    dev->read           = xx_io_file_read_cb;
    dev->write          = xx_io_file_write_cb;
    dev->seek           = xx_io_file_seek_cb;
    dev->close          = xx_io_file_close_cb;
    dev->total_size     = xx_io_file_total_size_cb;
    dev->get_total_size = xx_io_file_total_size_cb;
    dev->size           = xx_io_file_total_size_cb;
    dev->priv           = handle;
    dev->seek64         = xx_io_file_seek64_cb;
    dev->tell           = xx_io_file_tell_cb;

    return dev;
}

xx_io_device* io_file_open(const char *path, const char *mode) {
    return xx_io_file_open(path, mode);
}

bool xx_io_file_exists_a(const char *path) {
    return xx_io_platform_file_exists_a(path);
}

bool xx_io_file_exists_w(const wchar_t *path) {
    return xx_io_platform_file_exists_w(path);
}

bool xx_io_file_remove_a(const char *path) {
    return xx_io_platform_file_remove_a(path);
}

bool xx_io_file_remove_w(const wchar_t *path) {
    return xx_io_platform_file_remove_w(path);
}

bool xx_io_file_replace_a(const char *source, const char *destination,
                          bool overwrite) {
    return xx_io_platform_file_replace_a(source, destination, overwrite);
}

bool xx_io_file_replace_w(const wchar_t *source, const wchar_t *destination,
                          bool overwrite) {
    return xx_io_platform_file_replace_w(source, destination, overwrite);
}

/* ========================================================================= */
/* --- Directory and File Attribute Operations                          --- */
/* ========================================================================= */

bool xx_io_create_dirs_w(const wchar_t *path, bool is_dir) {
    return xx_io_platform_create_dirs_w(path, is_dir);
}

bool xx_io_create_dirs_a(const char *path, bool is_dir) {
    return xx_io_platform_create_dirs_a(path, is_dir);
}

bool xx_io_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    return xx_io_platform_apply_dos_time_and_attrs_w(path, dos_date, dos_time, attrs);
}

bool xx_io_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    return xx_io_platform_apply_dos_time_and_attrs_a(path, dos_date, dos_time, attrs);
}
