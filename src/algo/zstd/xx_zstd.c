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

/* Native Zstandard framing, raw-block encoding, and decompression. */

#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xx_zstd_dec.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

bool xx_zstd_is_available(void) {
    return true;
}

size_t xx_zstd_compress_bound(size_t source_size) {
    size_t blocks = source_size == 0U
                        ? 1U
                        : source_size / (128U * 1024U) +
                              (source_size % (128U * 1024U) != 0U);
    if (blocks > (SIZE_MAX - 13U) / 3U ||
        source_size > SIZE_MAX - 13U - blocks * 3U) return 0U;
    return source_size + 13U + blocks * 3U;
}

static bool xx_zstd_read_exact(xx_io_device *device, void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0;
    while (done < size) {
        ssize_t amount = xx_io_read(device, bytes + done, size - done);
        if (amount <= 0) {
            return false;
        }
        done += (size_t)amount;
    }
    return true;
}

static bool xx_zstd_write_exact(xx_io_device *device, const void *buffer,
                                size_t size, xx_pd_struct *progress) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0;

    if (!device || (!buffer && size != 0U)) {
        return false;
    }
    while (done < size) {
        size_t chunk = size - done;
        ssize_t amount;
        if (chunk > 64U * 1024U) {
            chunk = 64U * 1024U;
        }
        if (progress && xx_pd_is_stopped(progress)) {
            return false;
        }
        amount = xx_io_write(device, bytes + done, chunk);
        if (amount <= 0 || (size_t)amount > chunk) {
            return false;
        }
        done += (size_t)amount;
    }
    return true;
}

bool xx_zstd_compress_memory(const void *source, size_t source_size,
                             void *destination, size_t destination_capacity,
                             size_t *out_written, int level) {
    const uint8_t *input = (const uint8_t *)source;
    uint8_t *output = (uint8_t *)destination;
    size_t required;
    size_t input_position = 0U;
    size_t output_position = 0U;
    unsigned content_size_flag;
    unsigned content_size_bytes;

    (void)level;
    if (out_written) *out_written = 0U;
    if (!out_written || (!input && source_size != 0U) || !output ||
        (required = xx_zstd_compress_bound(source_size)) == 0U ||
        destination_capacity < required) return false;

    output[output_position++] = UINT8_C(0x28);
    output[output_position++] = UINT8_C(0xB5);
    output[output_position++] = UINT8_C(0x2F);
    output[output_position++] = UINT8_C(0xFD);
    if (source_size < 256U) {
        content_size_flag = 0U;
        content_size_bytes = 1U;
    } else if (source_size < 65792U) {
        content_size_flag = 1U;
        content_size_bytes = 2U;
    } else if ((uint64_t)source_size <= UINT32_MAX) {
        content_size_flag = 2U;
        content_size_bytes = 4U;
    } else {
        content_size_flag = 3U;
        content_size_bytes = 8U;
    }
    output[output_position++] = (uint8_t)((content_size_flag << 6) | 0x20U);
    {
        uint64_t stored_size = content_size_bytes == 2U
                                   ? (uint64_t)source_size - 256U
                                   : (uint64_t)source_size;
        for (unsigned index = 0; index < content_size_bytes; ++index) {
            output[output_position++] = (uint8_t)(stored_size >> (index * 8U));
        }
    }
    do {
        size_t block_size = source_size - input_position;
        bool last;
        uint32_t header;
        if (block_size > 128U * 1024U) block_size = 128U * 1024U;
        last = input_position + block_size == source_size;
        header = ((uint32_t)block_size << 3) | (last ? 1U : 0U);
        output[output_position++] = (uint8_t)header;
        output[output_position++] = (uint8_t)(header >> 8);
        output[output_position++] = (uint8_t)(header >> 16);
        for (size_t index = 0; index < block_size; ++index) {
            output[output_position++] = input[input_position + index];
        }
        input_position += block_size;
        if (last) break;
    } while (input_position < source_size);
    *out_written = output_position;
    return true;
}

bool xx_zstd_decompress_memory(const void *source, size_t source_size,
                               void *destination, size_t destination_size,
                               size_t *out_written) {
    return xx_zstd_decode_frames(source, source_size, destination,
                                 destination_size, out_written);
}

static bool xx_zstd_pack_device_internal(xx_io_device *source,
                                         int64_t source_offset,
                                         int64_t uncompressed_size,
                                         xx_io_device *destination, int level,
                                         xx_pd_struct *progress,
                                         size_t *out_compressed_size,
                                         uint32_t *out_crc32) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size;
    size_t output_capacity;
    size_t compressed_size = 0;
    size_t offset = 0;
    uint32_t crc32 = 0;
    int progress_level = -1;
    bool success = false;

    if (out_compressed_size) {
        *out_compressed_size = 0;
    }
    if (out_crc32) {
        *out_crc32 = 0;
    }
    if (!source || !destination || source_offset < 0 || uncompressed_size < 0 ||
        (uint64_t)uncompressed_size > (uint64_t)SIZE_MAX ||
        (progress && xx_pd_is_stopped(progress))) {
        return false;
    }

    input_size = (size_t)uncompressed_size;
    output_capacity = xx_zstd_compress_bound(input_size);
    if (output_capacity == 0 || (uint64_t)output_capacity > (uint64_t)INT64_MAX) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc(input_size ? input_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_capacity);
    if (!input || !output) {
        goto cleanup;
    }
    if (source_offset >= 0 &&
        xx_io_seek64(source, source_offset, SEEK_SET) != 0) {
        goto cleanup;
    }

    if (progress) {
        progress_level = xx_pd_enter_level(progress, (uint64_t)input_size,
                                           "Compressing Zstandard data");
    }
    while (offset < input_size) {
        size_t chunk = input_size - offset;
        ssize_t amount;
        if (chunk > 64U * 1024U) {
            chunk = 64U * 1024U;
        }
        if (progress && xx_pd_is_stopped(progress)) {
            goto cleanup;
        }
        amount = xx_io_read(source, input + offset, chunk);
        if (amount <= 0 || (size_t)amount > chunk) {
            goto cleanup;
        }
        crc32 = xx_crc32_calc(crc32, input + offset, (size_t)amount);
        offset += (size_t)amount;
        if (progress && progress_level >= 0) {
            xx_pd_set_current(progress, progress_level, (uint64_t)offset);
        }
    }

    if (progress && xx_pd_is_stopped(progress)) {
        goto cleanup;
    }
    if (!xx_zstd_compress_memory(input, input_size, output, output_capacity,
                                 &compressed_size, level) ||
        (uint64_t)compressed_size > (uint64_t)INT64_MAX ||
        (progress && xx_pd_is_stopped(progress))) {
        goto cleanup;
    }
    if (!xx_zstd_write_exact(destination, output, compressed_size, progress)) {
        goto cleanup;
    }

    if (out_compressed_size) {
        *out_compressed_size = compressed_size;
    }
    if (out_crc32) {
        *out_crc32 = crc32;
    }
    success = true;

cleanup:
    if (progress && progress_level >= 0) {
        xx_pd_leave_level(progress, progress_level);
    }
    xx_mem_free(input);
    xx_mem_free(output);
    return success;
}

bool xx_zstd_pack_device(xx_io_device *source, int64_t source_offset,
                         int64_t uncompressed_size, xx_io_device *destination,
                         int level, xx_pd_struct *progress) {
    return xx_zstd_pack_device_internal(source, source_offset,
                                        uncompressed_size, destination, level,
                                        progress, NULL, NULL);
}

bool xx_zstd_pack_source(xx_io_device *source, const char *source_path,
                         int64_t *out_uncompressed_size,
                         int64_t *out_compressed_size, uint32_t *out_crc32,
                         xx_io_device *destination, int level,
                         xx_pd_struct *progress) {
    xx_io_device *owned_source = NULL;
    xx_io_device *actual_source = source;
    int64_t uncompressed_size;
    size_t compressed_size = 0;
    uint32_t crc32 = 0;
    bool success = false;

    if (out_uncompressed_size) {
        *out_uncompressed_size = 0;
    }
    if (out_compressed_size) {
        *out_compressed_size = 0;
    }
    if (out_crc32) {
        *out_crc32 = 0;
    }
    if (!out_uncompressed_size || !out_compressed_size || !out_crc32 ||
        !destination || (!source && !source_path)) {
        return false;
    }

    if (!actual_source) {
        owned_source = xx_io_file_open(source_path, "rb");
        actual_source = owned_source;
        if (!actual_source) {
            return false;
        }
    }
    uncompressed_size = xx_io_size(actual_source);
    if (uncompressed_size < 0 ||
        (uint64_t)uncompressed_size > (uint64_t)SIZE_MAX) {
        goto cleanup;
    }

    success = xx_zstd_pack_device_internal(
        actual_source, 0, uncompressed_size, destination, level, progress,
        &compressed_size, &crc32);
    if (!success || (uint64_t)compressed_size > (uint64_t)INT64_MAX) {
        success = false;
        goto cleanup;
    }

    *out_uncompressed_size = uncompressed_size;
    *out_compressed_size = (int64_t)compressed_size;
    *out_crc32 = crc32;

cleanup:
    if (owned_source) {
        xx_io_close(owned_source);
    }
    return success;
}

bool xx_zstd_unpack_device_to_device(xx_io_device *source, int64_t source_offset,
                                     int64_t compressed_size, xx_io_device *destination,
                                     uint64_t uncompressed_size, xx_pd_struct *progress) {
    if (!source || !destination || source_offset < 0 || compressed_size < 0 ||
        (uint64_t)compressed_size > (uint64_t)SIZE_MAX ||
        uncompressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (progress && xx_pd_is_stopped(progress)) {
        return false;
    }

    size_t input_size = (size_t)compressed_size;
    size_t output_size = (size_t)uncompressed_size;
    uint8_t *input = (uint8_t *)xx_mem_alloc(input_size ? input_size : 1);
    uint8_t *output = (uint8_t *)xx_mem_alloc(output_size ? output_size : 1);
    if (!input || !output) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }

    bool success = xx_io_seek64(source, source_offset, SEEK_SET) == 0 &&
                   xx_zstd_read_exact(source, input, input_size);
    if (success) {
        size_t result = 0;
        success = xx_zstd_decompress_memory(input, input_size, output,
                                            output_size, &result) &&
                  result == output_size;
    }
    if (success && output_size > 0) {
        success = xx_zstd_write_exact(destination, output, output_size,
                                      progress);
    }

    xx_mem_free(input);
    xx_mem_free(output);
    return success;
}

bool xx_zstd_unpack_device_to_file(xx_io_device *source, int64_t source_offset,
                                   int64_t compressed_size, const char *destination_path,
                                   uint64_t uncompressed_size, xx_pd_struct *progress) {
    if (!destination_path) {
        return false;
    }
    xx_io_device *destination = xx_io_file_open(destination_path, "wb");
    if (!destination) {
        return false;
    }
    bool success = xx_zstd_unpack_device_to_device(source, source_offset, compressed_size,
                                                   destination, uncompressed_size, progress);
    xx_io_close(destination);
    return success;
}
