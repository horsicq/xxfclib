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
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#elif !defined(_SSIZE_T_DEFINED) && !defined(__ssize_t_defined)
#include <sys/types.h>
#endif

/* Forward declaration and types */
typedef struct xx_io_device xx_io_device;
typedef struct xx_io_device io_device;
typedef struct xx_io_buffer_state xx_io_buffer_state;

/**
 * @brief Abstract I/O device structure with function pointer vtable.
 */
struct xx_io_device {
    ssize_t (*read)(xx_io_device *self, void *buf, size_t n);
    ssize_t (*write)(xx_io_device *self, const void *buf, size_t n);
    int (*seek)(xx_io_device *self, long off, int whence);
    int (*close)(xx_io_device *self);
    int64_t (*total_size)(xx_io_device *self);
    int64_t (*get_total_size)(xx_io_device *self);
    int64_t (*size)(xx_io_device *self);
    void *priv; /* implementation-specific state */
    /* Optional extensions. Zero-initialize custom devices before setting fields. */
    int (*seek64)(xx_io_device *self, int64_t off, int whence);
    int64_t (*tell)(xx_io_device *self);
    xx_io_buffer_state *buffer_state; /* Opaque state managed by xx_io_buffer_*. */
    bool buffer_enabled; /* Use xx_io_set_buffer_enabled to change buffering. */
    /* Optional whole-device resizing: 0 on success, -1 on failure.
     * NULL means unsupported. Use xx_io_resize for cache-aware dispatch. */
    int (*resize)(xx_io_device *self, int64_t new_size);
};

/* Compatibility member aliases matching camelCase / user naming */
#ifndef totalSize
#define totalSize total_size
#endif
#ifndef getTotalSize
#define getTotalSize get_total_size
#endif

/* --- Inline Convenience Wrappers --- */

XXFC_API ssize_t xx_io_read(xx_io_device *d, void *b, size_t n);

XXFC_API ssize_t xx_io_write(xx_io_device *d, const void *b, size_t n);

XXFC_API int xx_io_seek(xx_io_device *d, long off, int whence);

/** Reposition with a 64-bit offset; legacy devices work within long's range. */
XXFC_API int xx_io_seek64(xx_io_device *d, int64_t off, int whence);

/** Return the current offset, or -1 if unavailable. */
XXFC_API int64_t xx_io_tell(xx_io_device *d);

XXFC_API int xx_io_close(xx_io_device *d);

/** Resize a supported device to a nonnegative byte size. Returns 0 on success,
 * -1 for unsupported devices, invalid sizes or backend failure. The cursor is
 * preserved, even if it lies beyond the new EOF. Existing bytes before the new
 * EOF are retained; the contents of an extension follow the native backend.
 * Built-in callbacks are provided for writable whole regular files and disk
 * temporary files whose native access permits resizing. Read-only files,
 * fixed memory, subdevices, multivolumes, processes and read-only buffer
 * wrappers do not resize their storage. Successful resizing invalidates any
 * borrowed buffer views; attached caches keep their logical cursor. */
XXFC_API int xx_io_resize(xx_io_device *device, int64_t new_size);

/** Whether a known-size device's seek/read/write callbacks support modification at an absolute
 * position. Built-in files require a read/write update mode without append;
 * append writes ignore seeking and therefore cannot shift file contents.
 * Read-only memory and subdevices are respected; unknown-size devices
 * (including process memory) are unavailable through this bounded query.
 * multivolumes require every nonempty child range to permit modification.
 * Custom devices must implement ordinary random-access callback semantics.
 * This checks capabilities only; individual operations may still fail. */
XXFC_API bool xx_io_can_read_write_at(xx_io_device *device);

static inline int64_t xx_io_total_size(xx_io_device *d)
{
    if (!d) return -1;
    if (d->total_size) return d->total_size(d);
    if (d->get_total_size) return d->get_total_size(d);
    if (d->size) return d->size(d);
    return -1;
}

static inline int64_t xx_io_get_total_size(xx_io_device *d)
{
    return xx_io_total_size(d);
}

static inline int64_t xx_io_size(xx_io_device *d)
{
    return xx_io_total_size(d);
}

static inline int64_t xx_io_get_size(xx_io_device *d)
{
    return xx_io_total_size(d);
}

/** Test whether a nonnegative offset and size fit the device's known size.
 * Zero-length ranges at EOF are valid. Unknown-size devices fail. This uses
 * subtraction to avoid overflow and does not move the cursor or read bytes. */
XXFC_API bool xx_io_check(xx_io_device *device, int64_t offset, int64_t size);

/** Clamp a range to the device's known size. A negative requested size means
 * "to the end". Offset must be within [0,total_size), including for an empty
 * range. On failure *size stays unchanged. Unknown-size devices and NULL size
 * pointers fail. This does not move the cursor or read bytes. */
XXFC_API bool xx_io_range_clamp(xx_io_device *device, int64_t offset, int64_t *size);

/** Read exactly n bytes at an absolute offset and restore the device cursor.
 * The device must be seekable and report its current cursor. For known-size
 * devices, out-of-range requests fail before changing the cursor. Short reads
 * and failed cursor restores also fail. A zero-length read does not move it. */
XXFC_API bool xx_io_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t n);

/* Aliases matching user request without prefix */
static inline ssize_t io_read(xx_io_device *d, void *b, size_t n)
{
    return xx_io_read(d, b, n);
}
static inline ssize_t io_write(xx_io_device *d, const void *b, size_t n)
{
    return xx_io_write(d, b, n);
}
static inline int io_seek(xx_io_device *d, long off, int whence)
{
    return xx_io_seek(d, off, whence);
}
static inline int io_seek64(xx_io_device *d, int64_t off, int whence)
{
    return xx_io_seek64(d, off, whence);
}
static inline int64_t io_tell(xx_io_device *d)
{
    return xx_io_tell(d);
}
static inline int io_close(xx_io_device *d)
{
    return xx_io_close(d);
}
static inline int io_resize(xx_io_device *d, int64_t new_size)
{
    return xx_io_resize(d, new_size);
}
static inline int64_t io_total_size(xx_io_device *d)
{
    return xx_io_total_size(d);
}
static inline int64_t io_get_total_size(xx_io_device *d)
{
    return xx_io_total_size(d);
}
static inline int64_t io_size(xx_io_device *d)
{
    return xx_io_total_size(d);
}
static inline int64_t io_get_size(xx_io_device *d)
{
    return xx_io_total_size(d);
}

/* --- Device Constructors --- */

/**
 * @brief Open a file as an abstract I/O device.
 * @param path File path.
 * @param mode Open mode ("r", "w", "rb", "wb", "r+b", etc.).
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device *xx_io_file_open(const char *path, const char *mode);
XXFC_API xx_io_device *xx_io_file_open_w(const wchar_t *path, const char *mode);
/** Seekable 64-bit temporary stream, removed automatically on close. While a
 * memory-only scope is active it is RAM-backed and never spills to disk. */
XXFC_API xx_io_device *xx_io_temp_open(void);
XXFC_API xx_io_device *io_file_open(const char *path, const char *mode);
XXFC_API xx_io_device *io_file_open_w(const wchar_t *path, const char *mode);

/** Memory-only archive work policy, local to the calling thread. Initialize
 * each scope to zero. Begin before opening/detecting an archive and end after
 * its iterators/readers/temporary streams have closed. Scopes nest in LIFO
 * order and are not inherited by worker threads. Do not share a scope or its
 * temporary streams between threads.
 *
 * Read-only file opens remain allowed. Filesystem mutations (including writes
 * to already-open file devices) fail explicitly. Temporary streams use owned,
 * seekable RAM with an aggregate capacity ceiling, including nested scopes;
 * there is no disk fallback. A zero ceiling permits empty streams only.
 * This ceiling covers temporary payload capacity, not decoder allocations.
 * Inside a nested scope, outer temporary streams may be read/seeked but not
 * written: every temporary write belongs to the innermost active scope.
 */
typedef struct xx_io_memory_only_scope {
    void *internal;
    uint32_t error;
} xx_io_memory_only_scope;
typedef enum xx_io_memory_only_error_t {
    XX_IO_MEMORY_ONLY_OK = 0,
    XX_IO_MEMORY_ONLY_DISK_WRITE = 1,
    XX_IO_MEMORY_ONLY_LIMIT = 2,
    XX_IO_MEMORY_ONLY_ALLOCATION = 3,
    XX_IO_MEMORY_ONLY_LIVE_TEMP = 4,
    XX_IO_MEMORY_ONLY_SCOPE_ORDER = 5
} xx_io_memory_only_error_t;
XXFC_API bool xx_io_memory_only_begin(xx_io_memory_only_scope *scope, uint64_t temporary_byte_limit);
/** Pop this scope, returning false on an attempted disk mutation, allocation
 * or capacity failure, or an unclosed temporary stream. Except for an invalid
 * nesting order, the scope is popped even on failure. Unclosed RAM streams
 * remain safely closable but must not be used after the scope ends. */
XXFC_API bool xx_io_memory_only_end(xx_io_memory_only_scope *scope);
XXFC_API bool xx_io_memory_only_active(void);
/** Times the active scope, or one inside it, refused to grow a temporary
 * stream over its limit (0 outside a scope). Read before and after a step,
 * it tells whether that step hit the limit, which the kept first failure
 * no longer tells once it is set. */
XXFC_API unsigned long xx_io_memory_only_refusals(void);
/** Aggregate allocated temporary payload capacity for the active scope. */
XXFC_API uint64_t xx_io_memory_only_used(void);
/** Temporary payload capacity still free: the least headroom of the active
 * scope and the scopes around it, UINT64_MAX outside a scope. Temporary
 * streams may grow by this many bytes in all without failing the scope. */
XXFC_API uint64_t xx_io_memory_only_available(void);
/** First failure, retained in the token after end for useful diagnostics. */
XXFC_API xx_io_memory_only_error_t xx_io_memory_only_error(const xx_io_memory_only_scope *scope);

/** Extraction policy, local to the calling thread like the memory-only scope
 * and not inherited by worker threads. Initialize each scope to zero, begin it
 * before members are written below root (the output folder named for them;
 * copied) and end it after. Scopes nest in LIFO order. While one is active:
 * - xx_io_create_dirs_a/_w follow a link (on Windows, a reparse point) that is
 *   root itself or one of its existing ancestors, as given. Below root such a
 *   component, like a file, still fails the call. (Outside a scope every link
 *   in the path is followed, as mkdir -p does: the path is the caller's own.)
 * - A file opened for writing with truncation ("w" without "x") is never
 *   written through a link, nor into a file with other hard links, that is
 *   already at its path: that entry is replaced by a new file.
 * - The first failed filesystem change since begin or xx_io_extract_clear() is
 *   kept as an errno value (xx_io_extract_errno): ELOOP for a link and ENOTDIR
 *   for a file where a folder belongs below root, ENAMETOOLONG for a path
 *   longer than the system takes, otherwise the system's own error; and
 *   XX_IO_EXTRACT_TEMP_FAILED (no errno value) when only a temporary copy
 *   (xx_io_temp_open) could not be written, which is no file of the member.
 */
#define XX_IO_EXTRACT_TEMP_FAILED (-1)
typedef struct xx_io_extract_scope {
    void *internal;
} xx_io_extract_scope;
XXFC_API bool xx_io_extract_begin(xx_io_extract_scope *scope, const char *root);
/** Pop this scope; false (and nothing popped) when it is not the innermost. */
XXFC_API bool xx_io_extract_end(xx_io_extract_scope *scope);
/** Forget the kept failure and the count of files opened for writing of the
 * innermost scope, e.g. before each member. */
XXFC_API void xx_io_extract_clear(void);
/** The kept failure of the innermost scope: 0 when none or outside a scope. */
XXFC_API int xx_io_extract_errno(void);
/** Files the innermost scope opened for writing since begin or the last
 * clear (0 outside a scope): none for a member that a reader refused to
 * write, for example because a file is already at its path. */
XXFC_API unsigned long xx_io_extract_files(void);

/**
 * Return the borrowed path used to open a file-backed device. Buffered devices
 * inherit their source path. A one-range
 * multivolume view inherits it only when the range covers its whole child.
 * Memory, subdevice, partial-range, and multi-file devices have no source path and
 * return NULL. The pointer remains valid only while the device is open.
 */
XXFC_API const char *xx_io_source_path(xx_io_device *device);

/** UTF-8/wide filesystem helpers used for failure-safe extraction. */
XXFC_API bool xx_io_file_exists_a(const char *path);
XXFC_API bool xx_io_file_exists_w(const wchar_t *path);
XXFC_API bool xx_io_file_remove_a(const char *path);
XXFC_API bool xx_io_file_remove_w(const wchar_t *path);
/** Publish source at destination. If overwrite is false, an existing
 * destination is preserved and the operation fails. Source and destination
 * must reside on the same filesystem for the replacement to be atomic. */
XXFC_API bool xx_io_file_replace_a(const char *source, const char *destination, bool overwrite);
XXFC_API bool xx_io_file_replace_w(const wchar_t *source, const wchar_t *destination, bool overwrite);

/**
 * @brief Open a mutable memory buffer as an abstract I/O device.
 * @param buf Pointer to memory buffer.
 * @param size Size in bytes of buffer.
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device *xx_io_mem_open(void *buf, size_t size);
XXFC_API xx_io_device *io_mem_open(void *buf, size_t size);

/**
 * @brief Open a read-only memory buffer as an abstract I/O device.
 * @param buf Pointer to const memory buffer.
 * @param size Size in bytes of buffer.
 * @return Allocated xx_io_device pointer, or NULL on error.
 */
XXFC_API xx_io_device *xx_io_mem_open_ro(const void *buf, size_t size);
XXFC_API xx_io_device *io_mem_open_ro(const void *buf, size_t size);

/** True for memory devices and buffer/subdevices backed by memory. */
XXFC_API bool xx_io_is_memory(const xx_io_device *device);

/** Enable or disable read buffering on this same device.
 * Ordinary devices start disabled; xx_io_buffer_open* devices start enabled.
 * Enabling an ordinary device requires read, seek, tell, and a known size;
 * it captures the current global capacity/memory limit without changing its
 * callbacks, priv, or address. Use xx_io_read/write/seek/tell/close for I/O.
 * Disabling restores the underlying cursor and releases attached cache state.
 * Writes automatically disable an attached cache before reaching the backend.
 * Existing buffered wrappers remain read-only and keep their cursor/capacity
 * when disabled: reads bypass the cache, views use fresh scratch reads, and
 * whole-file promotion is disabled. Wrapper source sizes remain fixed until
 * close. Required failures remain sticky.
 * Changing modes invalidates all borrowed views. The source must remain
 * unchanged while buffering is enabled, except through xx_io_resize on this
 * attached device. Successful resizing keeps buffering enabled, invalidates
 * borrowed views/cached bytes, and clears transient cache read failures.
 * No concurrent access is supported.
 * Returns false on NULL, unsupported input, allocation failure, or a failed
 * cursor restore; failure leaves the buffering mode unchanged. Repeating the
 * current mode succeeds without I/O. Do not change buffer_enabled directly. */
XXFC_API bool xx_io_set_buffer_enabled(xx_io_device *device, bool enabled);
/** Open a read-only buffered view of a seekable, known-size source.
 * The wrapper has its own cursor, initially zero. Reads reposition the source
 * on cache misses; neither the source cursor nor concurrent source access is
 * preserved. The source contents and size must remain unchanged while open.
 * A zero capacity captures the current global file-buffer capacity. The
 * supplied memory_limit is captured at open; zero
 * disables optional whole-file promotion. Memory sources never duplicate the
 * whole file for promotion. Ownership transfers only on successful open;
 * close frees the wrapper/cache and closes the source only if requested.
 * Reads retry short transfers. A source error, premature EOF, or required
 * cache allocation failure is sticky: subsequent nonempty reads/views fail.
 * A failed read returns bytes already copied, or -1, and advances the wrapper
 * cursor only by copied bytes. Seeks within [0,size] remain possible after an
 * error and do not clear it. */
XXFC_API xx_io_device *xx_io_buffer_open(xx_io_device *source, size_t capacity, uint64_t memory_limit, bool take_ownership);

/** Open a buffered source and eagerly copy inputs smaller than the captured
 * capacity into owned whole-file bytes, including a trailing NUL. Eager loading
 * starts at source offset zero, retries short reads, and ignores memory_limit.
 * Equal/larger inputs use the normal streaming/promotion policy. The original
 * source is retained until close; ownership transfers only if opening and any
 * required eager load succeed. A failed open leaves the source open. */
XXFC_API xx_io_device *xx_io_buffer_open_auto(xx_io_device *source, size_t capacity, uint64_t memory_limit, bool take_ownership);

/** Open borrowed, immutable resident bytes without a duplicate cache.
 * The bytes must remain alive until close. The wrapper owns its internal
 * read-only memory device, but never frees the bytes. Capacity has the same
 * meaning as xx_io_buffer_open; the logical cursor starts at zero. */
XXFC_API xx_io_device *xx_io_buffer_open_memory(const void *data, size_t size, size_t capacity);

/** Captured capacity, or zero when device is not a buffered device. */
XXFC_API size_t xx_io_buffer_capacity(const xx_io_device *device);
/** Estimate source transfers for a search: min(nonnegative range, captured
 * capacity). Other devices use the current global capacity. */
XXFC_API uint64_t xx_io_buffer_search_access(const xx_io_device *device, int64_t range);
/** Sticky required-read/cache failure; false for another device type. */
XXFC_API bool xx_io_buffer_failed(const xx_io_device *device);
/** Borrowed source, or NULL when device is not a buffered device. */
XXFC_API xx_io_device *xx_io_buffer_source(const xx_io_device *device);

/** Return resident or promoted whole-file bytes, otherwise NULL.
 * With nonzero upcoming, promotion is attempted when source bytes already read
 * plus upcoming bytes reach the file size and that size fits the captured
 * memory limit. A failed
 * optional allocation/read leaves window streaming usable and is not sticky;
 * promotion is not retried. This does not move the wrapper cursor. Returned
 * whole-file bytes stay valid until close, a buffering mode change, or successful
 * resizing of an attached device. The previous window allocation is
 * retained on promotion so pointers into that window remain readable. */
XXFC_API const unsigned char *xx_io_buffer_whole(xx_io_device *device, uint64_t upcoming);

/** Return a borrowed contiguous view at an absolute offset without moving the
 * wrapper cursor. On entry *count is the requested maximum; on return it is
 * the available byte count, zero on failure. The view ends at a cache boundary
 * or EOF, and may use a retained eight-byte tail of the previous window.
 * Window/tail pointers may be overwritten by a later cache miss; resident and
 * promoted pointers remain valid until close, a buffering mode change, or
 * successful resizing of an attached device. Successful promotion retains the
 * old window allocation, but does not preserve prior tail contents. */
XXFC_API const unsigned char *xx_io_buffer_view(xx_io_device *device, int64_t offset, size_t *count);

/**
 * @brief Open a fixed byte range of a parent as an independent logical stream.
 * The parent is borrowed and must remain open until every view is closed.
 * Closing a view frees only the view. Offset and size must be nonnegative and
 * their sum must fit int64_t. The range must fit the parent's known size;
 * an unknown-size parent accepts any finite, nonoverflowing range.
 * Reads/writes stop at the range end and never grow it. Short transfers are
 * retried; an error or premature parent EOF returns prior progress, or -1.
 * The view has its own cursor; I/O repositions the parent, so concurrent use
 * of the parent and its views requires external synchronization.
 */
XXFC_API xx_io_device *xx_io_sub_open(xx_io_device *parent, int64_t offset, int64_t size);
XXFC_API xx_io_device *io_sub_open(xx_io_device *parent, int64_t offset, int64_t size);

/** Open a byte range whose write operations always fail. */
XXFC_API xx_io_device *xx_io_sub_open_ro(xx_io_device *parent, int64_t offset, int64_t size);
XXFC_API xx_io_device *io_sub_open_ro(xx_io_device *parent, int64_t offset, int64_t size);

/**
 * Query the borrowed parent and physical range of a subdevice. Outputs are
 * optional and remain unchanged on failure. Returns false for other devices.
 */
XXFC_API bool xx_io_sub_get_range(const xx_io_device *device, xx_io_device **parent, int64_t *offset, int64_t *size);
static inline bool io_sub_get_range(const xx_io_device *d, xx_io_device **p, int64_t *offset, int64_t *size)
{
    return xx_io_sub_get_range(d, p, offset, size);
}

/** Process-memory access declared by a device. Explicit constructors enforce
 * this mode before calling the OS; the native object must also grant access. */
typedef enum xx_io_process_access_t {
    XX_IO_PROCESS_READ = 1,
    XX_IO_PROCESS_WRITE = 2,
    XX_IO_PROCESS_READ_WRITE = 3
} xx_io_process_access_t;

/** Open a process's memory with exactly the requested access (no fallback).
 * pid == 0 means the current process. Linux uses /proc/<pid>/mem, macOS a
 * Mach task port, and Windows OpenProcess/ReadProcessMemory/WriteProcessMemory.
 * OS permissions and address-width restrictions apply; inaccessible memory is
 * not manufactured as zeroes. The returned device owns the native handle.
 *
 * Process devices have no total size (size returns -1). The stream cursor is
 * an absolute virtual address in [0,INT64_MAX]; use seek64/tell for that cursor.
 * Direct process_*_at calls below accept the full uint64_t address range and
 * do not move it. They bypass generic device buffering. Synchronize concurrent
 * stream operations and keep the device open until all direct I/O completes.
 *
 * Only READ, WRITE, and READ_WRITE modes are valid. Failures return NULL or -1
 * (false for exact helpers), with errno on POSIX or GetLastError on Windows.
 * Invalid arguments/modes, denied device access and address arithmetic
 * overflow are reported before any transfer. */
XXFC_API xx_io_device *xx_io_process_open_ex(uint64_t pid, xx_io_process_access_t access);
XXFC_API xx_io_device *xx_io_process_open_ro(uint64_t pid);

/** Compatibility constructor: request read/write and fall back to read-only
 * when the OS denies writable access. The compatibility device permits write
 * attempts, which may still fail at the OS. Use open_ex/open_ro when the
 * declared device access must be enforced. pid == 0 selects this process. */
XXFC_API xx_io_device *xx_io_process_open(uint64_t pid);
XXFC_API xx_io_device *io_process_open(uint64_t pid);

/** Borrow an already-open native object with declared device access.
 * Windows: process HANDLE. Linux: raw /proc/<pid>/mem file descriptor cast as
 * (void *)(intptr_t)fd, including fd == 0. macOS: Mach task port cast as
 * (void *)(uintptr_t)task. The OS object must grant the requested operations;
 * this call cannot add native rights. The device never closes/deallocates a
 * borrowed object, including when device allocation fails. The caller must
 * keep it alive until after closing the device. */
XXFC_API xx_io_device *xx_io_process_open_handle_ex(void *native_handle, xx_io_process_access_t access);
/** Borrowed-handle compatibility form; declares read/write access. */
XXFC_API xx_io_device *xx_io_process_open_handle(void *native_handle);
XXFC_API xx_io_device *io_process_open_handle(void *native_handle);

/** Transfer memory at an absolute virtual address without changing the stream
 * cursor. NULL buffer is valid only when n == 0. A zero-length call still
 * validates the device and its access mode. The complete addressed range must
 * fit uint64_t; individual transfers are capped at PTRDIFF_MAX bytes so their
 * count fits ssize_t. A nonnegative return is the actual byte count, possibly
 * short (including zero); -1 means no reported progress. A short transfer may
 * end at an inaccessible page. OS address-width limits can reject ranges that
 * the uint64_t API itself can represent. These functions accept process
 * devices only; use xx_io_is_process to check a device's kind. */
XXFC_API ssize_t xx_io_process_read_at(xx_io_device *device, uint64_t address, void *buffer, size_t n);
XXFC_API ssize_t xx_io_process_write_at(xx_io_device *device, uint64_t address, const void *buffer, size_t n);

/** Retry positive short transfers until all bytes have been transferred.
 * Zero progress before completion fails. The cursor is unchanged, including
 * on failure. Neither operation is transactional: a failed read can leave its
 * destination buffer partially updated, and a failed write can have already
 * changed target memory. No rollback or target suspension is performed. */
XXFC_API bool xx_io_process_read_exact_at(xx_io_device *device, uint64_t address, void *buffer, size_t n);
XXFC_API bool xx_io_process_write_exact_at(xx_io_device *device, uint64_t address, const void *buffer, size_t n);

/** True only for a directly opened/wrapped process device, not a subdevice or
 * an unrelated custom device. NULL returns false. Does not perform native I/O. */
XXFC_API bool xx_io_is_process(const xx_io_device *device);
/** Close a process device and its owned native object, preserving borrowed
 * objects. xx_io_close(device) remains valid. Rejects other device kinds. */
XXFC_API int xx_io_process_close(xx_io_device *device);

/** One fixed byte range in a multi-volume stream (zero-length ranges allowed). */
typedef struct xx_io_volume {
    xx_io_device *device;
    int64_t offset; /**< Physical start in device, in bytes. */
    int64_t size;   /**< Number of bytes contributed to the logical stream. */
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
XXFC_API xx_io_device *xx_io_multivolume_open(const xx_io_volume *volumes, size_t count, bool take_ownership);
XXFC_API xx_io_device *io_multivolume_open(const xx_io_volume *volumes, size_t count, bool take_ownership);

/**
 * @brief Open existing files in the supplied order as a multi-volume stream.
 * Only "rb" and "r+b" modes are accepted. All files must exist; no filename
 * discovery, creation, truncation, or archive-specific header removal occurs.
 * The returned device owns all opened files. Failure closes files it opened.
 */
XXFC_API xx_io_device *xx_io_multivolume_open_files(const char *const *paths, size_t count, const char *mode);
XXFC_API xx_io_device *io_multivolume_open_files(const char *const *paths, size_t count, const char *mode);

/** Return the range count, or zero if device is not a multi-volume device. */
XXFC_API size_t xx_io_multivolume_count(xx_io_device *device);

/**
 * Query a range and its logical start (either output may be NULL).
 * Returned child pointers are borrowed; do not close owned children separately.
 * Returns false for another device type or an out-of-range index.
 */
XXFC_API bool xx_io_multivolume_get_volume(xx_io_device *device, size_t index, xx_io_volume *volume, int64_t *logical_offset);

static inline size_t io_multivolume_count(xx_io_device *d)
{
    return xx_io_multivolume_count(d);
}
static inline bool io_multivolume_get_volume(xx_io_device *d, size_t index, xx_io_volume *v, int64_t *offset)
{
    return xx_io_multivolume_get_volume(d, index, v, offset);
}

/* ========================================================================= */
/* --- Directory and File Attribute Operations                          --- */
/* ========================================================================= */

/**
 * @brief Recursively create parent directories or folder path (wide string).
 * @param path Directory or file path.
 * @param is_dir True if path is a directory itself; false if path is a file path whose parent dirs should be created.
 * @return True on success, false on failure. A path that the system cannot
 * take (too long, or a component too long for the file system, the file's
 * own name included) fails, and on failure the folders this call created are
 * removed again. See xx_io_extract_begin for links in the path.
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

static inline bool io_create_dirs_w(const wchar_t *path, bool is_dir)
{
    return xx_io_create_dirs_w(path, is_dir);
}
static inline bool io_create_dirs_a(const char *path, bool is_dir)
{
    return xx_io_create_dirs_a(path, is_dir);
}
static inline bool io_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
    return xx_io_apply_dos_time_and_attrs_w(path, dos_date, dos_time, attrs);
}
static inline bool io_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
    return xx_io_apply_dos_time_and_attrs_a(path, dos_date, dos_time, attrs);
}

#ifdef __cplusplus
}
#endif

#endif /* XX_IO_H */
