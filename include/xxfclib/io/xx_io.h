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
 * @file xx_io.h
 * @brief Abstract I/O device interface and standard device backends.
 */

#ifndef XX_IO_H
#define XX_IO_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ensure ssize_t is defined across platforms (e.g. MSVC) */
#if defined(_MSC_VER)
#  include <BaseTsd.h>
   typedef SSIZE_T ssize_t;
#elif !defined(_SSIZE_T_DEFINED) && !defined(__ssize_t_defined)
#  include <sys/types.h>
#endif

/* Forward declaration and types */
typedef struct xx_io_device xx_io_device;
typedef struct xx_io_device io_device;

/**
 * @brief Abstract I/O device structure with function pointer vtable.
 */
struct xx_io_device {
    ssize_t (*read)(xx_io_device *self, void *buf, size_t n);
    ssize_t (*write)(xx_io_device *self, const void *buf, size_t n);
    int     (*seek)(xx_io_device *self, long off, int whence);
    int     (*close)(xx_io_device *self);
    int64_t (*total_size)(xx_io_device *self);
    int64_t (*get_total_size)(xx_io_device *self);
    int64_t (*size)(xx_io_device *self);
    void *priv;   /* implementation-specific state */
    /* Optional extensions. Zero-initialize custom devices before setting fields. */
    int     (*seek64)(xx_io_device *self, int64_t off, int whence);
    int64_t (*tell)(xx_io_device *self);
};

/* Compatibility member aliases matching camelCase / user naming */
#ifndef totalSize
#define totalSize      total_size
#endif
#ifndef getTotalSize
#define getTotalSize  get_total_size
#endif

/* --- Inline Convenience Wrappers --- */

static inline ssize_t xx_io_read(xx_io_device *d, void *b, size_t n) {
    return (d && d->read) ? d->read(d, b, n) : -1;
}

static inline ssize_t xx_io_write(xx_io_device *d, const void *b, size_t n) {
    return (d && d->write) ? d->write(d, b, n) : -1;
}

static inline int xx_io_seek(xx_io_device *d, long off, int whence) {
    return (d && d->seek) ? d->seek(d, off, whence) : -1;
}

/** Reposition with a 64-bit offset; legacy devices work within long's range. */
static inline int xx_io_seek64(xx_io_device *d, int64_t off, int whence) {
    if (!d) return -1;
    if (d->seek64) return d->seek64(d, off, whence);
    if (off < LONG_MIN || off > LONG_MAX) return -1;
    return xx_io_seek(d, (long)off, whence);
}

/** Return the current offset, or -1 if unavailable. */
static inline int64_t xx_io_tell(xx_io_device *d) {
    return (d && d->tell) ? d->tell(d) : -1;
}

static inline int xx_io_close(xx_io_device *d) {
    return (d && d->close) ? d->close(d) : -1;
}

static inline int64_t xx_io_total_size(xx_io_device *d) {
    if (!d) return -1;
    if (d->total_size) return d->total_size(d);
    if (d->get_total_size) return d->get_total_size(d);
    if (d->size) return d->size(d);
    return -1;
}

static inline int64_t xx_io_get_total_size(xx_io_device *d) {
    return xx_io_total_size(d);
}

static inline int64_t xx_io_size(xx_io_device *d) {
    return xx_io_total_size(d);
}

static inline int64_t xx_io_get_size(xx_io_device *d) {
    return xx_io_total_size(d);
}

/* Aliases matching user request without prefix */
static inline ssize_t io_read(xx_io_device *d, void *b, size_t n)  { return xx_io_read(d, b, n); }
static inline ssize_t io_write(xx_io_device *d, const void *b, size_t n) { return xx_io_write(d, b, n); }
static inline int io_seek(xx_io_device *d, long off, int whence) { return xx_io_seek(d, off, whence); }
static inline int io_seek64(xx_io_device *d, int64_t off, int whence) { return xx_io_seek64(d, off, whence); }
static inline int64_t io_tell(xx_io_device *d) { return xx_io_tell(d); }
static inline int io_close(xx_io_device *d) { return xx_io_close(d); }
static inline int64_t io_total_size(xx_io_device *d) { return xx_io_total_size(d); }
static inline int64_t io_get_total_size(xx_io_device *d) { return xx_io_total_size(d); }
static inline int64_t io_size(xx_io_device *d) { return xx_io_total_size(d); }
static inline int64_t io_get_size(xx_io_device *d) { return xx_io_total_size(d); }

/* --- Device Constructors --- */

/**
 * @brief Open a file as an abstract I/O device.
 * @param path File path.
 * @param mode Open mode ("r", "w", "rb", "wb", "r+b", etc.).
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device* xx_io_file_open(const char *path, const char *mode);
XXFC_API xx_io_device* io_file_open(const char *path, const char *mode);

/** UTF-8/wide filesystem helpers used for failure-safe extraction. */
XXFC_API bool xx_io_file_exists_a(const char *path);
XXFC_API bool xx_io_file_exists_w(const wchar_t *path);
XXFC_API bool xx_io_file_remove_a(const char *path);
XXFC_API bool xx_io_file_remove_w(const wchar_t *path);
/** Publish source at destination. If overwrite is false, an existing
 * destination is preserved and the operation fails. Source and destination
 * must reside on the same filesystem for the replacement to be atomic. */
XXFC_API bool xx_io_file_replace_a(const char *source, const char *destination,
                                   bool overwrite);
XXFC_API bool xx_io_file_replace_w(const wchar_t *source,
                                   const wchar_t *destination,
                                   bool overwrite);

/**
 * @brief Open a mutable memory buffer as an abstract I/O device.
 * @param buf Pointer to memory buffer.
 * @param size Size in bytes of buffer.
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device* xx_io_mem_open(void *buf, size_t size);
XXFC_API xx_io_device* io_mem_open(void *buf, size_t size);

/**
 * @brief Open a read-only memory buffer as an abstract I/O device.
 * @param buf Pointer to const memory buffer.
 * @param size Size in bytes of buffer.
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device* xx_io_mem_open_ro(const void *buf, size_t size);
XXFC_API xx_io_device* io_mem_open_ro(const void *buf, size_t size);

/**
 * @brief Open a process's address space as an abstract I/O device.
 *
 * The device reads and writes the target process's memory: the stream offset
 * is an absolute memory address (use xx_io_seek64 / xx_io_tell), and there is
 * no total size. @p pid is the target process id, or 0 for the current
 * process. On POSIX this uses /proc/<pid>/mem; on Windows,
 * ReadProcessMemory / WriteProcessMemory on an OpenProcess() handle.
 *
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device* xx_io_process_open(uint64_t pid);
XXFC_API xx_io_device* io_process_open(uint64_t pid);

/**
 * @brief Wrap an already-open process handle as a process-memory device.
 *
 * Like xx_io_process_open, but adopts a handle the caller already holds
 * instead of opening one: a Windows process HANDLE, or on POSIX a memory file
 * descriptor (such as /proc/<pid>/mem) passed as (void*)(intptr_t)fd. The
 * device borrows the handle and does NOT close it - the caller keeps
 * ownership - so a debugger can share the handle it already has.
 *
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device* xx_io_process_open_handle(void *native_handle);
XXFC_API xx_io_device* io_process_open_handle(void *native_handle);

/** One fixed byte range in a multi-volume stream (zero-length ranges allowed). */
typedef struct xx_io_volume {
    xx_io_device *device;
    int64_t offset;  /**< Physical start in device, in bytes. */
    int64_t size;    /**< Number of bytes contributed to the logical stream. */
} xx_io_volume;

/**
 * @brief Concatenate ordered device ranges into one seekable, fixed-size stream.
 * The descriptors are copied. Each range must fit its device's known size and
 * the combined size must fit int64_t. At least one range is required.
 * Read/write operations cross volume boundaries; writes never grow volumes.
 * Short transfers are retried; an error returns prior progress, or -1 if none.
 * Premature physical EOF is an error, not the end of the logical stream.
 * Child cursors are repositioned during I/O; concurrent use is not supported.
 * @param take_ownership Close each distinct child once when this device closes.
 * On failure, ownership remains with the caller and no child is closed.
 */
XXFC_API xx_io_device* xx_io_multivolume_open(const xx_io_volume *volumes,
                                            size_t count, bool take_ownership);
XXFC_API xx_io_device* io_multivolume_open(const xx_io_volume *volumes,
                                         size_t count, bool take_ownership);

/**
 * @brief Open existing files in the supplied order as a multi-volume stream.
 * Only "rb" and "r+b" modes are accepted. All files must exist; no filename
 * discovery, creation, truncation, or archive-specific header removal occurs.
 * The returned device owns all opened files. Failure closes files it opened.
 */
XXFC_API xx_io_device* xx_io_multivolume_open_files(const char *const *paths,
                                                  size_t count, const char *mode);
XXFC_API xx_io_device* io_multivolume_open_files(const char *const *paths,
                                               size_t count, const char *mode);

/** Return the range count, or zero if device is not a multi-volume device. */
XXFC_API size_t xx_io_multivolume_count(xx_io_device *device);

/**
 * Query a range and its logical start (either output may be NULL).
 * Returned child pointers are borrowed; do not close owned children separately.
 * Returns false for another device type or an out-of-range index.
 */
XXFC_API bool xx_io_multivolume_get_volume(xx_io_device *device, size_t index,
                                          xx_io_volume *volume, int64_t *logical_offset);

static inline size_t io_multivolume_count(xx_io_device *d) {
    return xx_io_multivolume_count(d);
}
static inline bool io_multivolume_get_volume(xx_io_device *d, size_t index,
                                            xx_io_volume *v, int64_t *offset) {
    return xx_io_multivolume_get_volume(d, index, v, offset);
}

/* ========================================================================= */
/* --- Directory and File Attribute Operations                          --- */
/* ========================================================================= */

/**
 * @brief Recursively create parent directories or folder path (wide string).
 * @param path Directory or file path.
 * @param is_dir True if path is a directory itself; false if path is a file path whose parent dirs should be created.
 * @return True on success, false on failure.
 */
XXFC_API bool xx_io_create_dirs_w(const wchar_t *path, bool is_dir);

/**
 * @brief Recursively create parent directories or folder path (ANSI / UTF-8 string).
 */
XXFC_API bool xx_io_create_dirs_a(const char *path, bool is_dir);

/**
 * @brief Apply DOS date, time, and external attributes to a file on disk (wchar_t path).
 * @param path Wide path of the file or directory.
 * @param dos_date DOS date (bits: 0-4 day, 5-8 month, 9-15 year-1980). 0 to skip time.
 * @param dos_time DOS time (bits: 0-4 sec/2, 5-10 min, 11-15 hour). 0 to skip time.
 * @param attrs External/DOS attributes (e.g. 0x01 Read-Only, 0x10 Directory, 0x20 Archive). 0 to skip.
 * @return True on success.
 */
XXFC_API bool xx_io_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs);

/**
 * @brief Apply DOS date, time, and external attributes to a file on disk (UTF-8 path).
 */
XXFC_API bool xx_io_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs);

static inline bool io_create_dirs_w(const wchar_t *path, bool is_dir) {
    return xx_io_create_dirs_w(path, is_dir);
}
static inline bool io_create_dirs_a(const char *path, bool is_dir) {
    return xx_io_create_dirs_a(path, is_dir);
}
static inline bool io_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    return xx_io_apply_dos_time_and_attrs_w(path, dos_date, dos_time, attrs);
}
static inline bool io_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    return xx_io_apply_dos_time_and_attrs_a(path, dos_date, dos_time, attrs);
}

#ifdef __cplusplus
}
#endif

#endif /* XX_IO_H */
