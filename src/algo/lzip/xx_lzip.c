/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native Lzip framing.  The compressor payload uses xxfclib's independent
 * LZMA implementation; this file owns the Lzip member discovery, dictionary
 * decoding, end-marker validation, output bounds, and trailer CRC checks.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzip/xx_lzip.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

#define XX_LZIP_MAX_MEMBERS ((size_t)1000000U)

typedef struct xx_lzip_member_s {
    int64_t offset;
    int64_t compressed_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t crc32;
    uint32_t dictionary_size;
} xx_lzip_member;

typedef struct xx_lzip_counter_s {
    xx_io_device *destination;
    uint64_t expected_size;
    uint64_t written;
    uint32_t crc32;
    bool failed;
} xx_lzip_counter;

static uint32_t xx_lzip_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint64_t xx_lzip_read_u64le(const uint8_t *data) {
    uint64_t result = 0U;
    unsigned index;
    for (index = 0U; index < 8U; ++index) {
        result |= (uint64_t)data[index] << (index * 8U);
    }
    return result;
}

static bool xx_lzip_read_exact_at(xx_io_device *device, int64_t offset,
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

static bool xx_lzip_dictionary_size(uint8_t code, uint32_t *dictionary_size) {
    unsigned exponent = code & UINT8_C(0x1f);
    unsigned fraction = code >> 5U;
    uint32_t base;
    uint32_t result;
    if (!dictionary_size || exponent < 12U || exponent > 29U) return false;
    base = UINT32_C(1) << exponent;
    result = base - (base / 16U) * fraction;
    if (result < UINT32_C(4096) || result > (UINT32_C(1) << 29U)) {
        return false;
    }
    *dictionary_size = result;
    return true;
}

bool xx_lzip_has_header(const uint8_t *data, size_t size) {
    uint32_t ignored_size;
    return data && size >= XX_LZIP_HEADER_SIZE &&
           data[0] == (uint8_t)'L' && data[1] == (uint8_t)'Z' &&
           data[2] == (uint8_t)'I' && data[3] == (uint8_t)'P' &&
           data[4] == UINT8_C(1) &&
           xx_lzip_dictionary_size(data[5], &ignored_size);
}

/* Locate the member ending at end while keeping it inside the supplied
 * stream.  Lzip's self-sized footer deliberately permits reliable reverse
 * discovery of concatenated members. */
static bool xx_lzip_member_before(xx_io_device *source, int64_t stream_start,
                                  int64_t end, xx_lzip_member *member) {
    uint8_t trailer[XX_LZIP_TRAILER_SIZE];
    uint8_t header[XX_LZIP_HEADER_SIZE];
    uint64_t member_size64;
    uint64_t output_size64;
    int64_t member_size;
    int64_t offset;
    uint32_t dictionary_size;

    if (!source || !member || stream_start < 0 || end < stream_start ||
        end - stream_start < (int64_t)XX_LZIP_MIN_MEMBER_SIZE ||
        !xx_lzip_read_exact_at(source, end - (int64_t)sizeof(trailer),
                               trailer, sizeof(trailer))) {
        return false;
    }
    member_size64 = xx_lzip_read_u64le(trailer + 12U);
    output_size64 = xx_lzip_read_u64le(trailer + 4U);
    if (member_size64 < XX_LZIP_MIN_MEMBER_SIZE ||
        member_size64 > (uint64_t)(end - stream_start) ||
        member_size64 > (uint64_t)INT64_MAX ||
        output_size64 > (uint64_t)INT64_MAX) {
        return false;
    }
    member_size = (int64_t)member_size64;
    offset = end - member_size;
    if (!xx_lzip_read_exact_at(source, offset, header, sizeof(header)) ||
        !xx_lzip_has_header(header, sizeof(header)) ||
        !xx_lzip_dictionary_size(header[5], &dictionary_size)) {
        return false;
    }
    member->offset = offset;
    member->compressed_offset = offset + (int64_t)sizeof(header);
    member->compressed_size =
        member_size - (int64_t)sizeof(header) - (int64_t)sizeof(trailer);
    member->uncompressed_size = (int64_t)output_size64;
    member->crc32 = xx_lzip_read_u32le(trailer);
    member->dictionary_size = dictionary_size;
    return member->compressed_size >= 5;
}

static bool xx_lzip_collect_members(xx_io_device *source,
                                    int64_t source_offset,
                                    int64_t source_size,
                                    xx_lzip_member **members_out,
                                    size_t *count_out,
                                    int64_t *output_size) {
    int64_t end;
    int64_t cursor;
    size_t count = 0U;
    size_t index;
    uint64_t total_output = 0U;
    xx_lzip_member temporary;
    xx_lzip_member *members;

    if (!source || !members_out || !count_out || !output_size ||
        source_offset < 0 || source_size < (int64_t)XX_LZIP_MIN_MEMBER_SIZE) {
        return false;
    }
    end = source_offset + source_size;
    cursor = end;
    while (cursor > source_offset) {
        if (count >= XX_LZIP_MAX_MEMBERS ||
            !xx_lzip_member_before(source, source_offset, cursor,
                                   &temporary) ||
            (uint64_t)temporary.uncompressed_size >
                (uint64_t)INT64_MAX - total_output) {
            return false;
        }
        total_output += (uint64_t)temporary.uncompressed_size;
        cursor = temporary.offset;
        ++count;
    }
    if (cursor != source_offset || count == 0U ||
        count > SIZE_MAX / sizeof(*members)) {
        return false;
    }
    members = (xx_lzip_member *)xx_mem_alloc(count * sizeof(*members));
    if (!members) return false;
    cursor = end;
    for (index = count; index != 0U; --index) {
        if (!xx_lzip_member_before(source, source_offset, cursor,
                                   &members[index - 1U])) {
            xx_mem_free(members);
            return false;
        }
        cursor = members[index - 1U].offset;
    }
    *members_out = members;
    *count_out = count;
    *output_size = (int64_t)total_output;
    return true;
}

static ssize_t xx_lzip_counter_write(xx_io_device *device, const void *data,
                                     size_t size) {
    xx_lzip_counter *counter =
        device ? (xx_lzip_counter *)device->priv : NULL;
    size_t done = 0U;
    if (!counter || (!data && size != 0U) ||
        (uint64_t)size > counter->expected_size - counter->written) {
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
    counter->crc32 = xx_crc32_calc(counter->crc32, data, size);
    counter->written += (uint64_t)size;
    return (ssize_t)size;
}

bool xx_lzip_decode_device(xx_io_device *source, int64_t source_offset,
                           int64_t source_size, xx_io_device *destination,
                           int64_t *output_size, size_t *member_count,
                           xx_pd_struct *pd) {
    int64_t total_size;
    int64_t expected_size;
    xx_lzip_member *members = NULL;
    size_t count = 0U;
    size_t index;
    bool result = false;

    if (output_size) *output_size = -1;
    if (member_count) *member_count = 0U;
    if (!source || !destination || source_offset < 0 || source_size < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(source);
    if (total_size < source_offset || source_size > total_size - source_offset ||
        !xx_lzip_collect_members(source, source_offset, source_size, &members,
                                 &count, &expected_size)) {
        return false;
    }

    for (index = 0U; index < count; ++index) {
        uint8_t properties[XX_LZMA_PROPS_SIZE];
        xx_lzip_counter counter;
        xx_io_device sink;
        const xx_lzip_member *member = &members[index];
        if ((pd && xx_pd_is_stopped(pd)) || member->uncompressed_size < 0) {
            goto cleanup;
        }
        xx_rt_memset(properties, 0, sizeof(properties));
        properties[0] = UINT8_C(0x5d); /* lc=3, lp=0, pb=2 */
        properties[1] = (uint8_t)member->dictionary_size;
        properties[2] = (uint8_t)(member->dictionary_size >> 8U);
        properties[3] = (uint8_t)(member->dictionary_size >> 16U);
        properties[4] = (uint8_t)(member->dictionary_size >> 24U);
        xx_rt_memset(&counter, 0, sizeof(counter));
        counter.destination = destination;
        counter.expected_size = (uint64_t)member->uncompressed_size;
        xx_rt_memset(&sink, 0, sizeof(sink));
        sink.write = xx_lzip_counter_write;
        sink.priv = &counter;
        /* Lzip requires LZMA's end marker even though its footer also records
         * the expanded size.  Decode to the marker and constrain output via
         * the member's size field in the write callback. */
        if (!xx_lzma_unpack_device(source, member->compressed_offset,
                                   member->compressed_size, properties,
                                   sizeof(properties), -1, &sink, pd) ||
            counter.failed || counter.written != counter.expected_size ||
            counter.crc32 != member->crc32) {
            goto cleanup;
        }
    }
    result = true;

cleanup:
    xx_mem_free(members);
    if (result) {
        if (output_size) *output_size = expected_size;
        if (member_count) *member_count = count;
    }
    return result;
}
