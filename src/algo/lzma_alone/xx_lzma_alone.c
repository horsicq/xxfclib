/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzma_alone/xx_lzma_alone.h"

#include "xxfclib/algo/lzma/xx_lzma.h"

#include <limits.h>
#include <string.h>

typedef struct xx_lzma_alone_counter_s {
    xx_io_device *destination;
    uint64_t maximum_size;
    uint64_t written;
    bool failed;
} xx_lzma_alone_counter;

static uint64_t xx_lzma_alone_read_u64le(const uint8_t *data) {
    uint64_t result = 0U;
    unsigned index;
    for (index = 0U; index < 8U; ++index) {
        result |= (uint64_t)data[index] << (index * 8U);
    }
    return result;
}

static bool xx_lzma_alone_read_exact_at(xx_io_device *device, int64_t offset,
                                        void *buffer, size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_lzma_alone_properties_are_valid(const uint8_t *properties) {
    uint32_t dictionary_size;
    unsigned value;
    unsigned pb;
    unsigned lp;
    unsigned lc;
    if (!properties) return false;
    value = properties[0];
    if (value >= 9U * 5U * 5U) return false;
    pb = value / (9U * 5U);
    value -= pb * 9U * 5U;
    lp = value / 9U;
    lc = value - lp * 9U;
    if (lc + lp > 4U) return false;
    dictionary_size = (uint32_t)properties[1] |
                      ((uint32_t)properties[2] << 8U) |
                      ((uint32_t)properties[3] << 16U) |
                      ((uint32_t)properties[4] << 24U);
    return dictionary_size <= XX_LZMA_MAX_DICT_SIZE;
}

bool xx_lzma_alone_has_header(const uint8_t *data, size_t size) {
    uint64_t declared_size;
    if (!data || size < XX_LZMA_ALONE_HEADER_SIZE ||
        !xx_lzma_alone_properties_are_valid(data)) {
        return false;
    }
    declared_size = xx_lzma_alone_read_u64le(data + 5U);
    return declared_size == UINT64_MAX || declared_size <= (uint64_t)INT64_MAX;
}

static ssize_t xx_lzma_alone_counter_write(xx_io_device *device,
                                           const void *data, size_t size) {
    xx_lzma_alone_counter *counter =
        device ? (xx_lzma_alone_counter *)device->priv : NULL;
    size_t done = 0U;
    if (!counter || (!data && size != 0U) ||
        (uint64_t)size > counter->maximum_size - counter->written) {
        if (counter) counter->failed = true;
        return -1;
    }
    while (done < size && counter->destination) {
        ssize_t amount = xx_io_write(counter->destination,
                                     (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            counter->failed = true;
            return -1;
        }
        done += (size_t)amount;
    }
    counter->written += (uint64_t)size;
    return (ssize_t)size;
}

bool xx_lzma_alone_decode_device(xx_io_device *source, int64_t source_offset,
                                 int64_t source_size,
                                 xx_io_device *destination,
                                 int64_t *output_size, xx_pd_struct *pd) {
    uint8_t header[XX_LZMA_ALONE_HEADER_SIZE];
    uint64_t declared_size;
    int64_t total_size;
    int64_t compressed_offset;
    int64_t compressed_size;
    xx_lzma_alone_counter counter;
    xx_io_device sink;

    if (output_size) *output_size = -1;
    if (!source || !destination || source_offset < 0 ||
        source_size < (int64_t)XX_LZMA_ALONE_HEADER_SIZE + 5 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(source);
    if (total_size < source_offset || source_size > total_size - source_offset ||
        !xx_lzma_alone_read_exact_at(source, source_offset, header,
                                     sizeof(header)) ||
        !xx_lzma_alone_has_header(header, sizeof(header))) {
        return false;
    }
    declared_size = xx_lzma_alone_read_u64le(header + 5U);
    compressed_offset = source_offset + (int64_t)sizeof(header);
    compressed_size = source_size - (int64_t)sizeof(header);
    xx_rt_memset(&counter, 0, sizeof(counter));
    counter.destination = destination;
    counter.maximum_size = declared_size == UINT64_MAX
                               ? (uint64_t)INT64_MAX
                               : declared_size;
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.write = xx_lzma_alone_counter_write;
    sink.priv = &counter;
    /* Use the end marker rather than stopping at the header's size field.  A
     * bounded output sink still rejects an inflated or inconsistent claim. */
    if (!xx_lzma_unpack_device(source, compressed_offset, compressed_size,
                               header, XX_LZMA_PROPS_SIZE, -1, &sink, pd) ||
        counter.failed ||
        (declared_size != UINT64_MAX && counter.written != declared_size)) {
        return false;
    }
    if (output_size) *output_size = (int64_t)counter.written;
    return true;
}
