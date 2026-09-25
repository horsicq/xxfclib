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

#include "xxfclib/rt/xx_rt.h"
#include "xx_lzma_internal.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
#include <string.h>

/* =========================================================================
 * LZMA Decompression APIs
 * ========================================================================= */

bool xx_lzma_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                           const uint8_t *props, size_t props_size, int64_t uncomp_size,
                           xx_io_device *dst_dev, xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev || !props) return false;
    if (comp_size < 0 || src_offset < 0) return false;
    if (comp_size == 0) return true;
    if (src_offset >= 0)
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;

    lzma_props p;
    if (!lzma_parse_props(props, props_size, &p)) return false;

    lzma_range_dec rd;
    if (!lzma_rd_init(&rd, src_dev, NULL, 0, comp_size)) return false;
    bool ok = xx_lzma_decompress_stream(&rd, &p, uncomp_size, dst_dev, NULL, 0, NULL, pd);
    lzma_rd_free(&rd);
    return ok;
}

bool xx_lzma_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                   const uint8_t *props, size_t props_size, int64_t uncomp_size,
                                   const char *dst_file_path, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path || !props) return false;
    xx_io_device *out = xx_io_file_open(dst_file_path, "wb");
    if (!out) return false;
    bool ok = xx_lzma_unpack_device(src_dev, src_offset, comp_size, props, props_size,
                                    uncomp_size, out, pd);
    xx_io_close(out);
    if (!ok) xx_io_file_remove_a(dst_file_path);
    return ok;
}

bool xx_lzma_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                     const uint8_t *props, size_t props_size, int64_t uncomp_size,
                                     const wchar_t *dst_file_path_w, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path_w || !props) return false;
    char *utf8 = xx_str_unicode_to_utf8(dst_file_path_w);
    if (!utf8) return false;
    bool ok = xx_lzma_unpack_device_to_file(src_dev, src_offset, comp_size, props, props_size,
                                            uncomp_size, utf8, pd);
    xx_str_free(utf8);
    return ok;
}

bool xx_lzma_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                     const uint8_t *props, size_t props_size, int64_t uncomp_size,
                                     void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                     xx_pd_struct *pd)
{
    if (!src_dev || (!dst_buf && dst_buf_size > 0) || !props) return false;
    if (out_written) *out_written = 0;
    if (comp_size < 0 || src_offset < 0) return false;
    if (comp_size == 0) return true;
    if (src_offset >= 0)
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    lzma_props p;
    if (!lzma_parse_props(props, props_size, &p)) return false;
    lzma_range_dec rd;
    if (!lzma_rd_init(&rd, src_dev, NULL, 0, comp_size)) return false;
    bool ok = xx_lzma_decompress_stream(&rd, &p, uncomp_size, NULL, (uint8_t *)dst_buf,
                                        dst_buf_size, out_written, pd);
    lzma_rd_free(&rd);
    return ok;
}

bool xx_lzma_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                     const uint8_t *props, size_t props_size, int64_t uncomp_size,
                                     xx_io_device *dst_dev, xx_pd_struct *pd)
{
    if (!src_buf || !dst_dev || !props) return false;
    if (comp_size == 0) return true;
    lzma_props p;
    if (!lzma_parse_props(props, props_size, &p)) return false;
    lzma_range_dec rd;
    if (!lzma_rd_init(&rd, NULL, (const uint8_t *)src_buf, comp_size, (int64_t)comp_size))
        return false;
    bool ok = xx_lzma_decompress_stream(&rd, &p, uncomp_size, dst_dev, NULL, 0, NULL, pd);
    lzma_rd_free(&rd);
    return ok;
}

bool xx_lzma_decompress_memory(const void *src_buf, size_t src_size,
                               const uint8_t *props, size_t props_size, int64_t uncomp_size,
                               void *dst_buf, size_t dst_buf_size, size_t *out_written)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0) || !props) return false;
    if (out_written) *out_written = 0;
    if (src_size == 0) return true;
    lzma_props p;
    if (!lzma_parse_props(props, props_size, &p)) return false;
    lzma_range_dec rd;
    if (!lzma_rd_init(&rd, NULL, (const uint8_t *)src_buf, src_size, (int64_t)src_size))
        return false;
    bool ok = xx_lzma_decompress_stream(&rd, &p, uncomp_size, NULL, (uint8_t *)dst_buf,
                                        dst_buf_size, out_written, NULL);
    lzma_rd_free(&rd);
    return ok;
}

/* =========================================================================
 * LZMA2 Decompression APIs
 * ========================================================================= */

bool xx_lzma2_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                            uint8_t props2_byte, xx_io_device *dst_dev, xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev) return false;
    if (comp_size <= 0 || src_offset < 0) return false;
    if (src_offset >= 0)
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    lzma_range_dec rd;
    /* LZMA2 does not have the 5-byte range-coder init prefix at stream level */
    xx_rt_memset(&rd, 0, sizeof(rd));
    rd.dev = src_dev; rd.remaining = comp_size;
    rd.range = 0xFFFFFFFFu;
    bool ok = xx_lzma2_decompress_stream(&rd, props2_byte, dst_dev, NULL, 0, NULL, pd);
    lzma_rd_free(&rd);
    return ok;
}

bool xx_lzma2_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                    uint8_t props2_byte, const char *dst_file_path, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path) return false;
    xx_io_device *out = xx_io_file_open(dst_file_path, "wb");
    if (!out) return false;
    bool ok = xx_lzma2_unpack_device(src_dev, src_offset, comp_size, props2_byte, out, pd);
    xx_io_close(out);
    if (!ok) xx_io_file_remove_a(dst_file_path);
    return ok;
}

bool xx_lzma2_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                      uint8_t props2_byte, const wchar_t *dst_file_path_w, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path_w) return false;
    char *utf8 = xx_str_unicode_to_utf8(dst_file_path_w);
    if (!utf8) return false;
    bool ok = xx_lzma2_unpack_device_to_file(src_dev, src_offset, comp_size, props2_byte, utf8, pd);
    xx_str_free(utf8);
    return ok;
}

bool xx_lzma2_decompress_memory(const void *src_buf, size_t src_size,
                                uint8_t props2_byte,
                                void *dst_buf, size_t dst_buf_size, size_t *out_written)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) return false;
    if (out_written) *out_written = 0;
    if (src_size == 0) return false;
    lzma_range_dec rd;
    xx_rt_memset(&rd, 0, sizeof(rd));
    rd.mem = (const uint8_t *)src_buf;
    rd.mem_size = src_size;
    rd.remaining = (int64_t)src_size;
    rd.range = 0xFFFFFFFFu;
    bool ok = xx_lzma2_decompress_stream(&rd, props2_byte, NULL, (uint8_t *)dst_buf,
                                         dst_buf_size, out_written, NULL);
    return ok;
}

/* =========================================================================
 * LZMA2 Compression APIs
 * ========================================================================= */

bool xx_lzma2_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                          xx_io_device *dst_dev, int level,
                          uint8_t *out_props2_byte, xx_pd_struct *pd)
{
    if (out_props2_byte) *out_props2_byte = 0;
    if (!src_dev || !dst_dev || src_offset < 0 || uncomp_size < 0) return false;
    return xx_lzma2_compress_stream(src_dev, NULL, 0, src_offset, uncomp_size,
                                    dst_dev, NULL, 0, NULL, level,
                                    out_props2_byte, pd);
}

bool xx_lzma2_pack_source(xx_io_device *src_dev, const char *src_file_path,
                          int64_t *out_uncomp_size, int64_t *out_comp_size,
                          uint32_t *out_crc32, xx_io_device *dst_dev, int level,
                          uint8_t *out_props2_byte, xx_pd_struct *pd)
{
    xx_io_device *owned = NULL;
    xx_io_device *source = src_dev;
    uint8_t *buffer = NULL;
    uint32_t crc = 0;
    int64_t total;
    int64_t remaining;
    size_t compressed_size = 0;
    size_t buffer_size;
    uint8_t props2 = 0;
    bool ok = false;

    if (out_uncomp_size) *out_uncomp_size = 0;
    if (out_comp_size) *out_comp_size = 0;
    if (out_crc32) *out_crc32 = 0;
    if (out_props2_byte) *out_props2_byte = 0;
    if (!dst_dev || !out_uncomp_size || !out_comp_size || !out_crc32) return false;

    if (!source) {
        if (!src_file_path) return false;
        owned = xx_io_file_open(src_file_path, "rb");
        source = owned;
        if (!source) return false;
    }

    total = xx_io_size(source);
    if (total < 0 || (uint64_t)total > (uint64_t)SIZE_MAX ||
        xx_io_seek(source, 0, SEEK_SET) != 0) goto cleanup;

    buffer_size = xx_get_file_buffer_size();
    if (!buffer_size) buffer_size = XX_DEFAULT_FILE_BUFFER_SIZE;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size);
    if (!buffer) goto cleanup;

    remaining = total;
    while (remaining > 0) {
        size_t request = remaining > (int64_t)buffer_size ? buffer_size : (size_t)remaining;
        size_t done = 0;
        while (done < request) {
            ssize_t amount;
            if (pd && xx_pd_is_stopped(pd)) goto cleanup;
            amount = xx_io_read(source, buffer + done, request - done);
            if (amount <= 0 || (size_t)amount > request - done) goto cleanup;
            done += (size_t)amount;
        }
        crc = xx_crc32_calc(crc, buffer, request);
        remaining -= (int64_t)request;
    }

    if (xx_io_seek(source, 0, SEEK_SET) != 0) goto cleanup;
    if (!xx_lzma2_compress_stream(source, NULL, 0, 0, total,
                                  dst_dev, NULL, 0, &compressed_size,
                                  level, &props2, pd) ||
        (uint64_t)compressed_size > (uint64_t)INT64_MAX) goto cleanup;

    *out_uncomp_size = total;
    *out_comp_size = (int64_t)compressed_size;
    *out_crc32 = crc;
    if (out_props2_byte) *out_props2_byte = props2;
    ok = true;

cleanup:
    xx_mem_free(buffer);
    if (owned) xx_io_close(owned);
    return ok;
}

bool xx_lzma2_compress_memory(const void *src_buf, size_t src_size,
                              void *dst_buf, size_t dst_buf_size, size_t *out_written,
                              int level, uint8_t *out_props2_byte)
{
    if (out_written) *out_written = 0;
    if (out_props2_byte) *out_props2_byte = 0;
    if ((!src_buf && src_size != 0) || (!dst_buf && dst_buf_size != 0) ||
        (uint64_t)src_size > (uint64_t)INT64_MAX) return false;
    return xx_lzma2_compress_stream(NULL, (const uint8_t *)src_buf, src_size,
                                    0, (int64_t)src_size, NULL,
                                    (uint8_t *)dst_buf, dst_buf_size, out_written,
                                    level, out_props2_byte, NULL);
}

/* =========================================================================
 * LZMA Compression APIs
 * ========================================================================= */

bool xx_lzma_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                         xx_io_device *dst_dev, int level,
                         uint8_t *out_props, size_t *out_props_size,
                         xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev || uncomp_size < 0) return false;
    if (uncomp_size == 0) {
        if (out_props_size) *out_props_size = 0;
        return true;
    }
    return xx_lzma_compress_stream(src_dev, NULL, 0, src_offset, uncomp_size,
                                   dst_dev, NULL, 0, NULL, level, out_props, out_props_size, pd, true);
}

bool xx_lzma_pack_source(xx_io_device *src_dev, const char *src_file_path,
                         int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                         xx_io_device *dst_dev, int level,
                         uint8_t *out_props, size_t *out_props_size,
                         xx_pd_struct *pd)
{
    if (!dst_dev || !out_uncomp_size || !out_comp_size || !out_crc32) return false;
    *out_uncomp_size = 0; *out_comp_size = 0; *out_crc32 = 0;

    xx_io_device *owned = NULL;
    xx_io_device *target = src_dev;
    if (!target) {
        if (!src_file_path) return false;
        owned = xx_io_file_open(src_file_path, "rb");
        target = owned;
        if (!target) return false;
    }

    int64_t total = xx_io_size(target);
    if (total < 0) { if (owned) xx_io_close(owned); return false; }
    *out_uncomp_size = total;
    if (total == 0) { if (owned) xx_io_close(owned); return true; }

    if (xx_io_seek(target, 0, SEEK_SET) != 0) { if (owned) xx_io_close(owned); return false; }

    /* CRC pass */
    size_t buf_size = xx_get_file_buffer_size();
    if (!buf_size) buf_size = XX_DEFAULT_FILE_BUFFER_SIZE;
    uint8_t *buf = (uint8_t *)xx_mem_alloc(buf_size);
    if (!buf) { if (owned) xx_io_close(owned); return false; }
    int64_t rem = total; uint32_t crc = 0; bool rd_ok = true;
    while (rem > 0) {
        size_t want = rem > (int64_t)buf_size ? buf_size : (size_t)rem;
        ssize_t got = xx_io_read(target, buf, want);
        if (got <= 0) { rd_ok = false; break; }
        crc = xx_crc32_calc(crc, buf, (size_t)got);
        rem -= got;
    }
    xx_mem_free(buf);
    if (!rd_ok) { if (owned) xx_io_close(owned); return false; }
    *out_crc32 = crc;

    if (xx_io_seek(target, 0, SEEK_SET) != 0) { if (owned) xx_io_close(owned); return false; }

    size_t props_size_local = XX_LZMA_PROPS_SIZE;
    uint8_t props_local[XX_LZMA_PROPS_SIZE];
    size_t comp_sz = 0;
    bool ok = xx_lzma_compress_stream(target, NULL, 0, 0, total,
                                      dst_dev, NULL, 0, &comp_sz, level, props_local, &props_size_local, pd, true);

    if (ok) {
        if (out_props && out_props_size && *out_props_size >= props_size_local) {
            xx_rt_memcpy(out_props, props_local, props_size_local);
            *out_props_size = props_size_local;
        }
        *out_comp_size = (int64_t)comp_sz;
    }

    if (owned) xx_io_close(owned);
    return ok;
}

bool xx_lzma_compress_memory(const void *src_buf, size_t src_size,
                             void *dst_buf, size_t dst_buf_size, size_t *out_written,
                             int level,
                             uint8_t *out_props, size_t *out_props_size)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) return false;
    if (src_size == 0) { if (out_written) *out_written = 0; return true; }

    size_t props_sz = out_props_size ? *out_props_size : 0;
    bool ok = xx_lzma_compress_stream(NULL, (const uint8_t *)src_buf, src_size,
                                      0, (int64_t)src_size,
                                      NULL, (uint8_t *)dst_buf, dst_buf_size, out_written,
                                      level, out_props, &props_sz, NULL, true);
    if (out_props_size) *out_props_size = props_sz;
    return ok;
}
