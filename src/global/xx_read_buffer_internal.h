/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* The scratch buffer of a chunked read loop: a hash, checksum, search or
 * copy that reads a device range into memory of its own. Its size is the
 * general buffer size, xx_get_buffer_size() (Detect It Easy's
 * Feature/ReadBufferSize), or the bytes the loop has left when that is less.
 * When that much memory cannot be allocated, the default size is tried
 * instead, so the setting changes how a loop reads, never what it returns.
 * Loops that read views of a device's own cache (xx_io_buffer_view) have no
 * scratch buffer; they follow the file buffer size instead. */
#ifndef XX_READ_BUFFER_INTERNAL_H
#define XX_READ_BUFFER_INTERNAL_H

#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include <stdint.h>

/* limit: the bytes still to be read, 0 for no limit. *capacity receives the
 * size, 0 when NULL is returned. Free the buffer with xx_mem_free(). */
static inline void *xx_read_buffer_alloc(uint64_t limit, size_t *capacity)
{
    size_t size = xx_get_buffer_size();
    void *buffer;
    if (!size) size = XX_DEFAULT_BUFFER_SIZE;
    if (size > (size_t)PTRDIFF_MAX) size = (size_t)PTRDIFF_MAX;
    if (limit && limit < (uint64_t)size) size = (size_t)limit;
    buffer = xx_mem_alloc(size);
    if (!buffer && size > XX_DEFAULT_BUFFER_SIZE) {
        size = XX_DEFAULT_BUFFER_SIZE;
        buffer = xx_mem_alloc(size);
    }
    *capacity = buffer ? size : 0;
    return buffer;
}

#endif /* XX_READ_BUFFER_INTERNAL_H */
