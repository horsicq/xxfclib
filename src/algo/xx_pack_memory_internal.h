/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT */
#ifndef XX_PACK_MEMORY_INTERNAL_H
#define XX_PACK_MEMORY_INTERNAL_H
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
typedef size_t (*xx_codec_bound_fn)(size_t);
typedef bool (*xx_codec_memory_fn)(const void *, size_t, void *, size_t, size_t *);
/* Buffered adapter. Owns only temporary buffers, never caller devices. */
static bool xx_codec_pack_memory_device(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd, xx_codec_bound_fn bound,
                                        xx_codec_memory_fn encode)
{
    if (!source || !destination || source == destination || !source->read || !destination->write || size < 0 || (uint64_t)size > INT32_MAX || xx_pd_is_stopped(pd))
        return false;
    if (offset >= 0 && xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    size_t capacity = bound((size_t)size), written = 0, done = 0;
    if (!capacity) return false;
    uint8_t *input = xx_mem_alloc(size ? (size_t)size : 1U), *output = xx_mem_alloc(capacity);
    bool ok = input && output;
    while (ok && done < (size_t)size) {
        if (xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        size_t request = (size_t)size - done;
        if (request > 1048576U) request = 1048576U;
        ssize_t n = xx_io_read(source, input + done, request);
        if (n <= 0 || (size_t)n > request) {
            ok = false;
            break;
        }
        done += (size_t)n;
    }
    if (ok && !xx_pd_is_stopped(pd)) ok = encode(input, (size_t)size, output, capacity, &written);
    else ok = false;
    done = 0;
    while (ok && done < written) {
        if (xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        size_t request = written - done;
        if (request > 1048576U) request = 1048576U;
        ssize_t n = xx_io_write(destination, output + done, request);
        if (n <= 0 || (size_t)n > request) {
            ok = false;
            break;
        }
        done += (size_t)n;
    }
    xx_mem_free(input);
    xx_mem_free(output);
    return ok;
}
#endif
