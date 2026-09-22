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

#include "xx_ppmd7_internal.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>

/* =========================================================================
 * PPMd7 Decompression APIs
 * ========================================================================= */

static uint32_t xx_ppmd7_memory_bytes(uint32_t mem_mb)
{
    if (mem_mb < 1) mem_mb = 1;
    if (mem_mb > XX_PPMD7_MAX_MEM_MB) mem_mb = XX_PPMD7_MAX_MEM_MB;
    return mem_mb * 1024u * 1024u;
}

bool xx_ppmd7_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                            int order, uint32_t mem_mb,
                            xx_io_device *dst_dev, xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev) return false;
    if (comp_size == 0) return true;
    if (src_offset >= 0)
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    ppmd7_range_dec rd;
    if (!ppmd7_rd_init(&rd, src_dev, NULL, 0, comp_size)) return false;
    return xx_ppmd7_decompress_stream(&rd, order, xx_ppmd7_memory_bytes(mem_mb),
                                      dst_dev, NULL, 0, NULL, pd);
}

bool xx_ppmd7_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                    int order, uint32_t mem_mb,
                                    const char *dst_file_path, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path) return false;
    xx_io_device *out = xx_io_file_open(dst_file_path, "wb");
    if (!out) return false;
    bool ok = xx_ppmd7_unpack_device(src_dev, src_offset, comp_size, order, mem_mb, out, pd);
    xx_io_close(out);
    return ok;
}

bool xx_ppmd7_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                      int order, uint32_t mem_mb,
                                      const wchar_t *dst_file_path_w, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path_w) return false;
    char *utf8 = xx_str_unicode_to_utf8(dst_file_path_w);
    if (!utf8) return false;
    bool ok = xx_ppmd7_unpack_device_to_file(src_dev, src_offset, comp_size, order, mem_mb, utf8, pd);
    xx_str_free(utf8);
    return ok;
}

bool xx_ppmd7_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                      int order, uint32_t mem_mb,
                                      void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                      xx_pd_struct *pd)
{
    if (!src_dev || (!dst_buf && dst_buf_size > 0)) return false;
    if (comp_size == 0) { if (out_written) *out_written = 0; return true; }
    if (src_offset >= 0)
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    ppmd7_range_dec rd;
    if (!ppmd7_rd_init(&rd, src_dev, NULL, 0, comp_size)) return false;
    return xx_ppmd7_decompress_stream(&rd, order, xx_ppmd7_memory_bytes(mem_mb),
                                      NULL, (uint8_t *)dst_buf, dst_buf_size, out_written, pd);
}

bool xx_ppmd7_unpack_device_to_memory_bytes(
    xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
    int order, uint32_t mem_size, void *dst_buf, size_t dst_buf_size,
    size_t *out_written, xx_pd_struct *pd)
{
    ppmd7_range_dec rd;
    if (!src_dev || (!dst_buf && dst_buf_size > 0)) return false;
    if (comp_size == 0) {
        if (out_written) *out_written = 0;
        return true;
    }
    if (src_offset >= 0 && xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0)
        return false;
    if (!ppmd7_rd_init(&rd, src_dev, NULL, 0, comp_size)) return false;
    return xx_ppmd7_decompress_stream(&rd, order, mem_size, NULL,
                                      (uint8_t *)dst_buf, dst_buf_size,
                                      out_written, pd);
}

bool xx_ppmd7_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                      int order, uint32_t mem_mb,
                                      xx_io_device *dst_dev, xx_pd_struct *pd)
{
    if (!src_buf || !dst_dev) return false;
    if (comp_size == 0) return true;
    ppmd7_range_dec rd;
    if (!ppmd7_rd_init(&rd, NULL, (const uint8_t *)src_buf, comp_size, (int64_t)comp_size)) return false;
    return xx_ppmd7_decompress_stream(&rd, order, xx_ppmd7_memory_bytes(mem_mb),
                                      dst_dev, NULL, 0, NULL, pd);
}

bool xx_ppmd7_decompress_memory(const void *src_buf, size_t src_size,
                                int order, uint32_t mem_mb,
                                void *dst_buf, size_t dst_buf_size, size_t *out_written)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) return false;
    if (src_size == 0) { if (out_written) *out_written = 0; return true; }
    ppmd7_range_dec rd;
    if (!ppmd7_rd_init(&rd, NULL, (const uint8_t *)src_buf, src_size, (int64_t)src_size)) return false;
    return xx_ppmd7_decompress_stream(&rd, order, xx_ppmd7_memory_bytes(mem_mb),
                                      NULL, (uint8_t *)dst_buf, dst_buf_size, out_written, NULL);
}

/* =========================================================================
 * PPMd7 Compression APIs
 * ========================================================================= */

bool xx_ppmd7_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                          xx_io_device *dst_dev, int order, uint32_t mem_mb, xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev) return false;
    if (uncomp_size == 0) return true;
    return xx_ppmd7_compress_stream(src_dev, NULL, 0, src_offset, uncomp_size,
                                    dst_dev, NULL, 0, NULL, order, mem_mb, pd);
}

bool xx_ppmd7_pack_source(xx_io_device *src_dev, const char *src_file_path,
                          int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                          xx_io_device *dst_dev, int order, uint32_t mem_mb, xx_pd_struct *pd)
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

    size_t comp_sz = 0;
    bool ok = xx_ppmd7_compress_stream(target, NULL, 0, 0, total,
                                       dst_dev, NULL, 0, &comp_sz, order, mem_mb, pd);

    if (ok)
        *out_comp_size = (int64_t)comp_sz;

    if (owned) xx_io_close(owned);
    return ok;
}

bool xx_ppmd7_compress_memory(const void *src_buf, size_t src_size,
                              void *dst_buf, size_t dst_buf_size, size_t *out_written,
                              int order, uint32_t mem_mb)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) return false;
    if (src_size == 0) { if (out_written) *out_written = 0; return true; }

    return xx_ppmd7_compress_stream(NULL, (const uint8_t *)src_buf, src_size,
                                    0, (int64_t)src_size,
                                    NULL, (uint8_t *)dst_buf, dst_buf_size, out_written,
                                    order, mem_mb, NULL);
}
