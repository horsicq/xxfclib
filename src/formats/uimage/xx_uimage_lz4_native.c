/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_uimage_lz4_native.h"

#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>

#define XX_UIMAGE_LZ4_MAX_PACKED (64U * 1024U * 1024U)

bool xx_uimage_lz4_decode_device(xx_io_device *source, int64_t offset, int64_t packed_size, xx_io_device *destination, size_t max_output, xx_pd_struct *pd)
{
    uint8_t *packed = NULL;
    uint8_t *decoded = NULL;
    int64_t saved, input_size;
    size_t capacity, completed = 0U, written = 0U;
    bool result = false;
    if (!source || !destination || offset < 0 || packed_size <= 0 || packed_size > XX_UIMAGE_LZ4_MAX_PACKED || max_output == 0U || max_output > (size_t)INT32_MAX ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    input_size = xx_io_get_size(source);
    if (input_size >= 0 && (offset > input_size || packed_size > input_size - offset)) {
        return false;
    }
    saved = xx_io_tell(source);
    if (saved < 0) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    if (!packed || xx_io_seek64(source, offset, SEEK_SET) != 0) goto done;
    while (completed < (size_t)packed_size) {
        ssize_t received = xx_io_read(source, packed + completed, (size_t)packed_size - completed);
        if (received <= 0 || (pd && xx_pd_is_stopped(pd))) goto done;
        completed += (size_t)received;
    }
    capacity = (size_t)packed_size;
    if (capacity <= max_output / 4U) capacity *= 4U;
    if (capacity < 4096U) capacity = 4096U;
    if (capacity > max_output) capacity = max_output;
    for (;;) {
        if (pd && xx_pd_is_stopped(pd)) goto done;
        decoded = (uint8_t *)xx_mem_alloc(capacity);
        if (!decoded) goto done;
        if (xx_lz4_decompress_frames(packed, (size_t)packed_size, decoded, capacity, &written)) {
            size_t sent = 0U;
            while (sent < written) {
                ssize_t count = xx_io_write(destination, decoded + sent, written - sent);
                if (count <= 0 || (pd && xx_pd_is_stopped(pd))) goto done;
                sent += (size_t)count;
            }
            result = true;
            break;
        }
        xx_mem_free(decoded);
        decoded = NULL;
        if (capacity == max_output) break;
        capacity = capacity > max_output / 2U ? max_output : capacity * 2U;
    }
done:
    if (xx_io_seek64(source, saved, SEEK_SET) != 0) result = false;
    if (decoded) xx_mem_free(decoded);
    if (packed) xx_mem_free(packed);
    return result;
}
