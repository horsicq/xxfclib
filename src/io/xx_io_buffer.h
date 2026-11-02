/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Internal dispatch for attached and wrapper I/O buffers. */
#ifndef XX_IO_BUFFER_INTERNAL_H
#define XX_IO_BUFFER_INTERNAL_H

#include "xxfclib/io/xx_io.h"

ssize_t xx_io_buffer_read_dispatch(xx_io_device *device, void *buffer, size_t size);
ssize_t xx_io_buffer_write_dispatch(xx_io_device *device, const void *buffer, size_t size);
int xx_io_buffer_seek_dispatch(xx_io_device *device, int64_t offset, int whence);
int64_t xx_io_buffer_tell_dispatch(xx_io_device *device);
int xx_io_buffer_resize_dispatch(xx_io_device *device, int64_t new_size);
int xx_io_buffer_close_dispatch(xx_io_device *device);

/* An I/O buffer sized by the file buffer setting is no larger than what it
 * serves: min(capacity, need), at least one byte. A zero capacity means the
 * current xx_get_file_buffer_size() (or its default). */
size_t xx_io_buffer_size_for(size_t capacity, uint64_t need);

/* Allocation failures of such buffers, counted per thread. A reader that
 * loses one cannot see bytes it needed, so the scan that ran it compares the
 * count before and after and fails with an out-of-memory error instead of
 * reporting a different detection. Optional allocations (a slower path to
 * the same bytes remains) are not counted. */
void xx_io_note_alloc_failure(void);
uintptr_t xx_io_alloc_failures(void);

#endif /* XX_IO_BUFFER_INTERNAL_H */
