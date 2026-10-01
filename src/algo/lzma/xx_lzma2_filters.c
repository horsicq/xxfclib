/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_lzma2_filters_internal.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>

typedef struct lzma2_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t expected;
    uint64_t written;
    unsigned check_mask;
    uint32_t crc32;
    uint64_t crc64;
    xx_hash_context sha256;
    xx_pd_struct *pd;
    uint8_t delta_history[256];
    uint64_t delta_position;
    uint16_t delta_distance;
    bool failed;
    uint8_t *transformed;
    size_t capacity;
} lzma2_sink;

bool xx_lzma2_parse_property(uint8_t property, uint32_t *dictionary_size) {
    uint64_t dictionary;
    if (dictionary_size) *dictionary_size = 0U;
    if (property > 40U) return false;
    dictionary = property == 40U
                     ? UINT32_MAX
                     : ((uint64_t)2U | (property & 1U))
                           << (property / 2U + 11U);
    if (dictionary > XX_LZMA_MAX_DICT_SIZE) return false;
    if (dictionary_size) *dictionary_size = (uint32_t)dictionary;
    return true;
}

static ssize_t lzma2_sink_write(xx_io_device *device, const void *data,
                                size_t size) {
    lzma2_sink *sink = device ? (lzma2_sink *)device->priv : NULL;
    const uint8_t *source = (const uint8_t *)data;
    size_t original_size = size;
    if (!sink || (!data && size != 0U) || sink->written > sink->expected ||
        (uint64_t)size > sink->expected - sink->written) {
        if (sink) sink->failed = true;
        return -1;
    }
    while (size != 0U) {
        size_t amount = size > sink->capacity ? sink->capacity : size;
        const uint8_t *output = source;
        if (sink->delta_distance != 0U) {
            for (size_t i = 0U; i < amount; ++i) {
                size_t prior = (size_t)((sink->delta_position -
                    sink->delta_distance) & 0xffU);
                uint8_t value = (uint8_t)(source[i] +
                                           sink->delta_history[prior]);
                sink->transformed[i] = value;
                sink->delta_history[sink->delta_position & 0xffU] = value;
                ++sink->delta_position;
            }
            output = sink->transformed;
        }
        if (sink->target) {
            size_t done = 0;
            while (done < amount) {
                ssize_t written;
                if (sink->pd && xx_pd_is_stopped(sink->pd)) {
                    sink->failed = true;
                    return -1;
                }
                written = xx_io_write(sink->target, output + done, amount - done);
                if (written <= 0 || (size_t)written > amount - done) {
                    sink->failed = true;
                    return -1;
                }
                done += (size_t)written;
            }
        }
        if (sink->check_mask & XX_LZMA2_CALC_CRC32)
            sink->crc32 = xx_crc32_calc(sink->crc32, output, amount);
        if (sink->check_mask & XX_LZMA2_CALC_CRC64)
            sink->crc64 = xx_crc64_xz_calc(sink->crc64, output, amount);
        if (sink->check_mask & XX_LZMA2_CALC_SHA256)
            xx_hash_update(&sink->sha256, output, amount);
        sink->written += amount;
        source += amount;
        size -= amount;
    }
    return (ssize_t)original_size;
}

static int64_t lzma2_sink_size(xx_io_device *device) {
    lzma2_sink *sink = device ? (lzma2_sink *)device->priv : NULL;
    return sink && sink->written <= (uint64_t)INT64_MAX
               ? (int64_t)sink->written : -1;
}

static bool lzma2_sink_init(lzma2_sink *sink, xx_io_device *target,
                            uint64_t expected_size, uint16_t delta_distance,
                            unsigned check_mask, xx_pd_struct *pd) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->expected = expected_size;
    sink->delta_distance = delta_distance;
    sink->check_mask = check_mask;
    sink->pd = pd;
    sink->capacity = xx_get_file_buffer_size();
    if (expected_size < (uint64_t)sink->capacity)
        sink->capacity = (size_t)expected_size;
    if (sink->delta_distance != 0U && sink->capacity != 0U) {
        sink->transformed = (uint8_t *)xx_mem_alloc(sink->capacity);
        if (!sink->transformed) return false;
    }
    sink->device.write = lzma2_sink_write;
    sink->device.total_size = lzma2_sink_size;
    sink->device.get_total_size = lzma2_sink_size;
    sink->device.size = lzma2_sink_size;
    sink->device.priv = sink;
    if ((check_mask & XX_LZMA2_CALC_SHA256) &&
        !xx_hash_init(&sink->sha256, XX_HASH_SHA256)) {
        xx_mem_free(sink->transformed);
        sink->transformed = NULL;
        return false;
    }
    return true;
}

bool xx_lzma2_unpack_filtered_checked_device(
    xx_io_device *source, int64_t offset, int64_t compressed_size,
    uint8_t property, uint16_t delta_distance, uint64_t expected_size,
    xx_io_device *destination, unsigned check_mask,
    xx_lzma2_decoded_info *info, xx_pd_struct *pd) {
    lzma2_sink sink;
    bool result = false;
    if (!info) return false;
    xx_mem_zero(info, sizeof(*info));
    if (!source || source == destination || offset < 0 || compressed_size <= 0 ||
        delta_distance > 256U || (check_mask & ~XX_LZMA2_CALC_ALL) != 0U ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (!lzma2_sink_init(&sink, destination, expected_size, delta_distance,
                         check_mask, pd)) {
        return false;
    }
    if (!xx_lzma2_unpack_device(source, offset, compressed_size, property,
                                &sink.device, pd) || sink.failed ||
        sink.written != expected_size ||
        ((check_mask & XX_LZMA2_CALC_SHA256) &&
         !xx_hash_final(&sink.sha256, info->sha256, sizeof(info->sha256)))) {
        goto cleanup;
    }
    info->written = sink.written;
    info->crc32 = sink.crc32;
    info->crc64 = sink.crc64;
    result = true;

cleanup:
    xx_mem_free(sink.transformed);
    return result;
}

bool xx_lzma2_unpack_filtered_device(
    xx_io_device *source, int64_t offset, int64_t compressed_size,
    uint8_t property, uint16_t delta_distance, uint64_t expected_size,
    xx_io_device *destination, xx_lzma2_decoded_info *info, xx_pd_struct *pd) {
    return xx_lzma2_unpack_filtered_checked_device(source, offset, compressed_size,
        property, delta_distance, expected_size, destination, XX_LZMA2_CALC_ALL, info, pd);
}
