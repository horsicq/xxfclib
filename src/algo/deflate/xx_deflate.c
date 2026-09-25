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

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xx_deflate_internal.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/memory/xx_memory.h"

/* ========================================================================= */
/* --- Deflate / Deflate64 Decompression APIs                             --- */
/* ========================================================================= */

bool xx_deflate_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                             xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd) {
    if (!src_dev || !dst_dev) {
        return false;
    }
    if (comp_size == 0) {
        return true;
    }

    if (src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
            return false;
        }
    }

    xx_bit_reader reader;
    if (!xx_br_init(&reader, src_dev, NULL, 0, comp_size)) {
        return false;
    }

    bool success = xx_deflate_decompress_stream(&reader, dst_dev, NULL, 0, NULL, is_deflate64, pd);
    xx_br_free(&reader);
    return success;
}

bool xx_deflate_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                      const char *dst_file_path, bool is_deflate64, xx_pd_struct *pd) {
    if (!src_dev || !dst_file_path) {
        return false;
    }

    xx_io_device *out_file = xx_io_file_open(dst_file_path, "wb");
    if (!out_file) {
        return false;
    }

    bool success = xx_deflate_unpack_device(src_dev, src_offset, comp_size, out_file, is_deflate64, pd);
    xx_io_close(out_file);
    if (!success) {
        xx_io_file_remove_a(dst_file_path);
    }
    return success;
}

bool xx_deflate_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                        const wchar_t *dst_file_path_w, bool is_deflate64, xx_pd_struct *pd) {
    if (!src_dev || !dst_file_path_w) {
        return false;
    }

    char *utf8 = xx_str_unicode_to_utf8(dst_file_path_w);
    if (!utf8) {
        return false;
    }

    bool success = xx_deflate_unpack_device_to_file(src_dev, src_offset, comp_size, utf8, is_deflate64, pd);
    xx_str_free(utf8);
    return success;
}

bool xx_deflate_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                        void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                        bool is_deflate64, xx_pd_struct *pd) {
    if (!src_dev || (!dst_buf && dst_buf_size > 0)) {
        return false;
    }

    if (comp_size == 0) {
        if (out_written) *out_written = 0;
        return true;
    }

    if (src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
            return false;
        }
    }

    xx_bit_reader reader;
    if (!xx_br_init(&reader, src_dev, NULL, 0, comp_size)) {
        return false;
    }

    bool success = xx_deflate_decompress_stream(&reader, NULL, (uint8_t *)dst_buf, dst_buf_size,
                                                out_written, is_deflate64, pd);
    xx_br_free(&reader);
    return success;
}

bool xx_deflate_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                        xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd) {
    if (!src_buf || !dst_dev) {
        return false;
    }

    if (comp_size == 0) {
        return true;
    }

    xx_bit_reader reader;
    if (!xx_br_init(&reader, NULL, (const uint8_t *)src_buf, comp_size, (int64_t)comp_size)) {
        return false;
    }

    bool success = xx_deflate_decompress_stream(&reader, dst_dev, NULL, 0, NULL, is_deflate64, pd);
    xx_br_free(&reader);
    return success;
}

bool xx_deflate_unpack_memory_to_device_ex(
    const void *src_buf, size_t comp_size, xx_io_device *dst_dev,
    size_t *out_consumed, bool is_deflate64, xx_pd_struct *pd) {
    xx_bit_reader reader;
    bool success;
    size_t consumed_bits;
    if (out_consumed) *out_consumed = 0U;
    if (!src_buf || !dst_dev || !out_consumed || comp_size == 0U) {
        return false;
    }
    if (!xx_br_init(&reader, NULL, (const uint8_t *)src_buf, comp_size,
                    (int64_t)comp_size)) {
        return false;
    }
    success = xx_deflate_decompress_stream(&reader, dst_dev, NULL, 0, NULL,
                                           is_deflate64, pd);
    if (success && (reader.mem_pos > SIZE_MAX / 8U ||
                    reader.bit_count < 0 ||
                    (size_t)reader.bit_count > reader.mem_pos * 8U)) {
        success = false;
    }
    if (success) {
        consumed_bits = reader.mem_pos * 8U - (size_t)reader.bit_count;
        if (consumed_bits > SIZE_MAX - 7U) {
            success = false;
        } else {
            *out_consumed = (consumed_bits + 7U) / 8U;
            if (*out_consumed == 0U || *out_consumed > comp_size) {
                success = false;
                *out_consumed = 0U;
            }
        }
    }
    xx_br_free(&reader);
    return success;
}

bool xx_deflate_decompress_memory(const void *src_buf, size_t src_size,
                                  void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                  bool is_deflate64) {
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) {
        return false;
    }

    if (src_size == 0) {
        if (out_written) *out_written = 0;
        return true;
    }

    xx_bit_reader reader;
    if (!xx_br_init(&reader, NULL, (const uint8_t *)src_buf, src_size, (int64_t)src_size)) {
        return false;
    }

    bool success = xx_deflate_decompress_stream(&reader, NULL, (uint8_t *)dst_buf, dst_buf_size,
                                                out_written, is_deflate64, NULL);
    xx_br_free(&reader);
    return success;
}

/* ========================================================================= */
/* --- Deflate / Deflate64 Compression APIs                               --- */
/* ========================================================================= */

bool xx_deflate_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                            xx_io_device *dst_dev, int level, bool is_deflate64, xx_pd_struct *pd) {
    if (!src_dev || !dst_dev || uncomp_size < 0) {
        return false;
    }

    if (uncomp_size == 0) {
        return true;
    }

    xx_bit_writer writer;
    if (!xx_bw_init(&writer, dst_dev, NULL, 0)) {
        return false;
    }

    bool success = xx_deflate_compress_stream(src_dev, NULL, 0, src_offset, uncomp_size,
                                              &writer, level, is_deflate64, pd);
    xx_bw_free(&writer);
    return success;
}

bool xx_deflate_pack_source(xx_io_device *src_dev, const char *src_file_path,
                            int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                            xx_io_device *dst_dev, int level, bool is_deflate64, xx_pd_struct *pd) {
    if (!dst_dev || !out_uncomp_size || !out_comp_size || !out_crc32) {
        return false;
    }

    *out_uncomp_size = 0;
    *out_comp_size = 0;
    *out_crc32 = 0;

    xx_io_device *owned_dev = NULL;
    xx_io_device *target_dev = src_dev;

    if (!target_dev) {
        if (!src_file_path) {
            return false;
        }
        owned_dev = xx_io_file_open(src_file_path, "rb");
        target_dev = owned_dev;
        if (!target_dev) {
            return false;
        }
    }

    int64_t total = xx_io_size(target_dev);
    if (total < 0) {
        if (owned_dev) xx_io_close(owned_dev);
        return false;
    }

    *out_uncomp_size = total;
    if (total == 0) {
        if (owned_dev) xx_io_close(owned_dev);
        return true;
    }

    if (xx_io_seek(target_dev, 0, SEEK_SET) != 0) {
        if (owned_dev) xx_io_close(owned_dev);
        return false;
    }

    /* 1. Calculate CRC-32 over uncompressed stream */
    size_t buf_size = xx_get_file_buffer_size();
    if (buf_size == 0) buf_size = XX_DEFAULT_FILE_BUFFER_SIZE;
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buf_size);
    if (!buffer) {
        if (owned_dev) xx_io_close(owned_dev);
        return false;
    }

    int64_t remaining = total;
    uint32_t running_crc = 0;
    bool read_ok = true;

    while (remaining > 0) {
        size_t chunk = (remaining > (int64_t)buf_size) ? buf_size : (size_t)remaining;
        ssize_t n_read = xx_io_read(target_dev, buffer, chunk);
        if (n_read <= 0) {
            read_ok = false;
            break;
        }
        running_crc = xx_crc32_calc(running_crc, buffer, (size_t)n_read);
        remaining -= n_read;
    }

    xx_mem_free(buffer);

    if (!read_ok || remaining != 0) {
        if (owned_dev) xx_io_close(owned_dev);
        return false;
    }

    *out_crc32 = running_crc;

    /* Rewind to compress */
    if (xx_io_seek(target_dev, 0, SEEK_SET) != 0) {
        if (owned_dev) xx_io_close(owned_dev);
        return false;
    }

    /* 2. Compress data into dst_dev */
    xx_bit_writer writer;
    if (!xx_bw_init(&writer, dst_dev, NULL, 0)) {
        if (owned_dev) xx_io_close(owned_dev);
        return false;
    }

    bool success = xx_deflate_compress_stream(target_dev, NULL, 0, 0, total,
                                              &writer, level, is_deflate64, pd);
    *out_comp_size = writer.total_written;
    xx_bw_free(&writer);

    if (owned_dev) {
        xx_io_close(owned_dev);
    }

    return success;
}

bool xx_deflate_compress_memory(const void *src_buf, size_t src_size,
                                void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                int level, bool is_deflate64) {
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) {
        return false;
    }

    if (src_size == 0) {
        if (out_written) *out_written = 0;
        return true;
    }

    xx_bit_writer writer;
    if (!xx_bw_init(&writer, NULL, (uint8_t *)dst_buf, dst_buf_size)) {
        return false;
    }

    bool success = xx_deflate_compress_stream(NULL, (const uint8_t *)src_buf, src_size,
                                              0, (int64_t)src_size, &writer, level, is_deflate64, NULL);
    if (success && out_written) {
        *out_written = (size_t)writer.total_written;
    }
    xx_bw_free(&writer);
    return success;
}
