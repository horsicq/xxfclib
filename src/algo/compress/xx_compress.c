/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native implementation of the documented Unix compress transport.  Codes are
 * LSB-first and are organized in eight-code groups; a width transition or a
 * CLEAR code discards the remainder of the current group before continuing.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/compress/xx_compress.h"

#include "xxfclib/io/xx_io.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define XX_COMPRESS_MIN_BITS 9U
#define XX_COMPRESS_MAX_BITS 16U
#define XX_COMPRESS_CLEAR 256U
#define XX_COMPRESS_FIRST 257U
#define XX_COMPRESS_TABLE_SIZE (UINT32_C(1) << XX_COMPRESS_MAX_BITS)
#define XX_COMPRESS_INPUT_CHUNK 4096U
#define XX_COMPRESS_OUTPUT_CHUNK 4096U

typedef struct xx_compress_reader_s {
    xx_io_device *device;
    int64_t remaining;
    uint8_t input[XX_COMPRESS_INPUT_CHUNK];
    size_t input_used;
    size_t input_position;
    uint64_t bits;
    unsigned bit_count;
    uint64_t consumed_bits;
} xx_compress_reader;

typedef struct xx_compress_writer_s {
    xx_io_device *device;
    uint8_t output[XX_COMPRESS_OUTPUT_CHUNK];
    size_t output_used;
    int64_t output_size;
} xx_compress_writer;

static bool xx_compress_read_exact(xx_io_device *device, void *buffer,
                                   size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U)) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

bool xx_compress_has_header(const uint8_t *data, size_t size) {
    unsigned maximum_bits;
    if (!data || size < 3U || data[0] != XX_COMPRESS_MAGIC0 ||
        data[1] != XX_COMPRESS_MAGIC1 || (data[2] & UINT8_C(0x60)) != 0U) {
        return false;
    }
    maximum_bits = data[2] & UINT8_C(0x1f);
    return maximum_bits >= XX_COMPRESS_MIN_BITS &&
           maximum_bits <= XX_COMPRESS_MAX_BITS;
}

/* 1 = byte, 0 = clean range end, -1 = device error. */
static int xx_compress_read_byte(xx_compress_reader *reader, uint8_t *value) {
    size_t wanted;
    ssize_t amount;
    if (!reader || !value) return -1;
    if (reader->input_position == reader->input_used) {
        if (reader->remaining == 0) return 0;
        wanted = reader->remaining > (int64_t)sizeof(reader->input)
                     ? sizeof(reader->input)
                     : (size_t)reader->remaining;
        amount = xx_io_read(reader->device, reader->input, wanted);
        if (amount <= 0 || (size_t)amount > wanted) return -1;
        reader->input_used = (size_t)amount;
        reader->input_position = 0U;
        reader->remaining -= amount;
    }
    *value = reader->input[reader->input_position++];
    return 1;
}

/* 1 = code, 0 = legal zero padding/end, -1 = malformed partial code. */
static int xx_compress_read_bits(xx_compress_reader *reader, unsigned width,
                                 uint32_t *value) {
    if (!reader || !value || width == 0U || width > XX_COMPRESS_MAX_BITS) {
        return -1;
    }
    while (reader->bit_count < width) {
        uint8_t byte;
        int status = xx_compress_read_byte(reader, &byte);
        if (status < 0) return -1;
        if (status == 0) {
            return reader->bits == 0U ? 0 : -1;
        }
        reader->bits |= (uint64_t)byte << reader->bit_count;
        reader->bit_count += 8U;
    }
    *value = (uint32_t)(reader->bits &
                        ((UINT64_C(1) << width) - UINT64_C(1)));
    reader->bits >>= width;
    reader->bit_count -= width;
    reader->consumed_bits += width;
    return 1;
}

static bool xx_compress_skip_group_padding(xx_compress_reader *reader,
                                           unsigned width,
                                           uint64_t *group_start) {
    uint64_t group_bits;
    uint64_t used;
    uint64_t padding;
    if (!reader || !group_start || width < XX_COMPRESS_MIN_BITS ||
        width > XX_COMPRESS_MAX_BITS || reader->consumed_bits < *group_start) {
        return false;
    }
    group_bits = (uint64_t)width * 8U;
    used = reader->consumed_bits - *group_start;
    padding = (group_bits - used % group_bits) % group_bits;
    while (padding != 0U) {
        unsigned take = padding > XX_COMPRESS_MAX_BITS
                            ? XX_COMPRESS_MAX_BITS
                            : (unsigned)padding;
        uint32_t ignored;
        if (xx_compress_read_bits(reader, take, &ignored) != 1) return false;
        padding -= take;
    }
    *group_start = reader->consumed_bits;
    return true;
}

static bool xx_compress_flush(xx_compress_writer *writer) {
    size_t done = 0U;
    if (!writer || !writer->device) return false;
    while (done < writer->output_used) {
        ssize_t amount = xx_io_write(writer->device, writer->output + done,
                                     writer->output_used - done);
        if (amount <= 0 || (size_t)amount > writer->output_used - done) {
            return false;
        }
        done += (size_t)amount;
    }
    writer->output_used = 0U;
    return true;
}

static bool xx_compress_emit(xx_compress_writer *writer, uint8_t value,
                             xx_pd_struct *pd) {
    if (!writer || !writer->device || writer->output_size == INT64_MAX ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    writer->output[writer->output_used++] = value;
    ++writer->output_size;
    return writer->output_used < sizeof(writer->output) ||
           xx_compress_flush(writer);
}

bool xx_compress_decode_device(xx_io_device *source, int64_t source_offset,
                               int64_t source_size, xx_io_device *destination,
                               int64_t *output_size, xx_pd_struct *pd) {
    uint8_t header[3];
    xx_compress_reader reader = {0};
    xx_compress_writer writer = {0};
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL;
    uint8_t *stack = NULL;
    uint32_t maximum_codes;
    uint32_t next_code;
    uint32_t width_limit;
    uint32_t old_code;
    uint8_t final_byte;
    unsigned width = XX_COMPRESS_MIN_BITS;
    unsigned maximum_bits;
    bool block_mode;
    uint64_t group_start = 0U;
    int64_t total_size;
    uint32_t code;
    int status;
    bool result = false;

    if (output_size) *output_size = -1;
    if (!source || !destination || source_offset < 0 || source_size < 3 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(source);
    if (total_size < source_offset || source_size > total_size - source_offset ||
        xx_io_seek64(source, source_offset, SEEK_SET) != 0 ||
        !xx_compress_read_exact(source, header, sizeof(header)) ||
        !xx_compress_has_header(header, sizeof(header))) {
        return false;
    }
    maximum_bits = header[2] & UINT8_C(0x1f);
    block_mode = (header[2] & UINT8_C(0x80)) != 0U;
    maximum_codes = UINT32_C(1) << maximum_bits;
    prefix = (uint16_t *)xx_rt_malloc((size_t)maximum_codes * sizeof(*prefix));
    suffix = (uint8_t *)xx_rt_malloc((size_t)maximum_codes * sizeof(*suffix));
    stack = (uint8_t *)xx_rt_malloc((size_t)maximum_codes * sizeof(*stack));
    if (!prefix || !suffix || !stack) goto cleanup;
    reader.device = source;
    reader.remaining = source_size - (int64_t)sizeof(header);
    writer.device = destination;

    status = xx_compress_read_bits(&reader, width, &code);
    if (status == 0) {
        result = xx_compress_flush(&writer);
        goto cleanup;
    }
    if (status != 1 || code >= 256U ||
        !xx_compress_emit(&writer, (uint8_t)code, pd)) {
        goto cleanup;
    }
    old_code = code;
    final_byte = (uint8_t)code;
    next_code = block_mode ? XX_COMPRESS_FIRST : 256U;
    width_limit = UINT32_C(1) << width;

    for (;;) {
        uint32_t input_code;
        size_t stack_size = 0U;
        status = xx_compress_read_bits(&reader, width, &code);
        if (status == 0) break;
        if (status != 1 || (pd && xx_pd_is_stopped(pd))) goto cleanup;

        if (block_mode && code == XX_COMPRESS_CLEAR) {
            if (!xx_compress_skip_group_padding(&reader, width, &group_start)) {
                goto cleanup;
            }
            width = XX_COMPRESS_MIN_BITS;
            width_limit = UINT32_C(1) << width;
            next_code = XX_COMPRESS_FIRST;
            status = xx_compress_read_bits(&reader, width, &code);
            if (status != 1 || code >= 256U ||
                !xx_compress_emit(&writer, (uint8_t)code, pd)) {
                goto cleanup;
            }
            old_code = code;
            final_byte = (uint8_t)code;
            continue;
        }

        input_code = code;
        if (code >= next_code) {
            if (code != next_code || code >= maximum_codes) goto cleanup;
            if (stack_size >= maximum_codes) goto cleanup;
            stack[stack_size++] = final_byte;
            code = old_code;
        }
        while (code >= 256U) {
            if (code >= next_code || code >= maximum_codes ||
                stack_size >= maximum_codes) {
                goto cleanup;
            }
            stack[stack_size++] = suffix[code];
            code = prefix[code];
        }
        if (stack_size >= maximum_codes || code >= 256U) goto cleanup;
        final_byte = (uint8_t)code;
        stack[stack_size++] = final_byte;
        while (stack_size != 0U) {
            if (!xx_compress_emit(&writer, stack[--stack_size], pd)) {
                goto cleanup;
            }
        }

        if (next_code < maximum_codes) {
            prefix[next_code] = (uint16_t)old_code;
            suffix[next_code] = final_byte;
            ++next_code;
            if (next_code >= width_limit && width < maximum_bits) {
                if (!xx_compress_skip_group_padding(&reader, width,
                                                    &group_start)) {
                    goto cleanup;
                }
                ++width;
                width_limit = UINT32_C(1) << width;
            }
        }
        old_code = input_code;
    }
    result = xx_compress_flush(&writer);
cleanup:
    if (result && output_size) *output_size = writer.output_size;
    xx_rt_free(stack);
    xx_rt_free(suffix);
    xx_rt_free(prefix);
    return result;
}
