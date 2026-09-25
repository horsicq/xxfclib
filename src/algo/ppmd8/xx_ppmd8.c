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
#include "xx_ppmd8_internal.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>

/* =========================================================================
 * ZIP Properties Helpers
 * ========================================================================= */

uint16_t xx_ppmd8_build_zip_props(int order, uint32_t mem_mb, int restore_method)
{
    if (order < XX_PPMD8_MIN_ORDER) order = XX_PPMD8_MIN_ORDER;
    if (order > XX_PPMD8_MAX_ORDER) order = XX_PPMD8_MAX_ORDER;
    if (mem_mb < XX_PPMD8_MIN_MEM_MB) mem_mb = XX_PPMD8_MIN_MEM_MB;
    if (mem_mb > XX_PPMD8_MAX_MEM_MB) mem_mb = XX_PPMD8_MAX_MEM_MB;
    if (restore_method < 0 || restore_method > 1) restore_method = 0;

    return (uint16_t)(((order - 1) & 0x0F) | (((mem_mb - 1) & 0xFF) << 4) | ((restore_method & 0x0F) << 12));
}

bool xx_ppmd8_parse_zip_props(uint16_t val, int *out_order, uint32_t *out_mem_mb, int *out_restore_method)
{
    int o = (int)((val & 0x0F) + 1);
    uint32_t m = (uint32_t)(((val >> 4) & 0xFF) + 1);
    int r = (int)(val >> 12);

    if (o < XX_PPMD8_MIN_ORDER || o > XX_PPMD8_MAX_ORDER || r > 1)
        return false;

    if (out_order) *out_order = o;
    if (out_mem_mb) *out_mem_mb = m;
    if (out_restore_method) *out_restore_method = r;
    return true;
}

/* =========================================================================
 * PPMd8 Decompression APIs
 * ========================================================================= */

bool xx_ppmd8_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                            int64_t uncomp_size,
                            int order, uint32_t mem_mb, int restore_method,
                            xx_io_device *dst_dev, xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev) return false;
    if (comp_size == 0 || uncomp_size == 0) return true;
    if (src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    }
    ppmd8_range_dec rd;
    xx_rt_memset(&rd, 0, sizeof(rd));
    rd.dev = src_dev;
    rd.remaining = comp_size;
    return xx_ppmd8_decompress_stream(&rd, uncomp_size, order, mem_mb, restore_method, dst_dev, NULL, 0, NULL, pd);
}

bool xx_ppmd8_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                    int64_t uncomp_size,
                                    int order, uint32_t mem_mb, int restore_method,
                                    const char *dst_file_path, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path) return false;
    xx_io_device *out = xx_io_file_open(dst_file_path, "wb");
    if (!out) return false;
    bool ok = xx_ppmd8_unpack_device(src_dev, src_offset, comp_size, uncomp_size,
                                     order, mem_mb, restore_method, out, pd);
    xx_io_close(out);
    if (!ok) xx_io_file_remove_a(dst_file_path);
    return ok;
}

bool xx_ppmd8_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                      int64_t uncomp_size,
                                      int order, uint32_t mem_mb, int restore_method,
                                      const wchar_t *dst_file_path_w, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path_w) return false;
    char *utf8 = xx_str_unicode_to_utf8(dst_file_path_w);
    if (!utf8) return false;
    bool ok = xx_ppmd8_unpack_device_to_file(src_dev, src_offset, comp_size, uncomp_size,
                                             order, mem_mb, restore_method, utf8, pd);
    xx_str_free(utf8);
    return ok;
}

bool xx_ppmd8_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                      int64_t uncomp_size,
                                      int order, uint32_t mem_mb, int restore_method,
                                      void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                      xx_pd_struct *pd)
{
    if (!src_dev || (!dst_buf && dst_buf_size > 0)) return false;
    if (comp_size == 0 || uncomp_size == 0) {
        if (out_written) *out_written = 0;
        return true;
    }
    if (src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    }
    ppmd8_range_dec rd;
    xx_rt_memset(&rd, 0, sizeof(rd));
    rd.dev = src_dev;
    rd.remaining = comp_size;
    return xx_ppmd8_decompress_stream(&rd, uncomp_size >= 0 ? uncomp_size : (int64_t)dst_buf_size,
                                      order, mem_mb, restore_method,
                                      NULL, (uint8_t *)dst_buf, dst_buf_size, out_written, pd);
}

bool xx_ppmd8_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                      int64_t uncomp_size,
                                      int order, uint32_t mem_mb, int restore_method,
                                      xx_io_device *dst_dev, xx_pd_struct *pd)
{
    if (!src_buf || !dst_dev) return false;
    if (comp_size == 0 || uncomp_size == 0) return true;
    ppmd8_range_dec rd;
    xx_rt_memset(&rd, 0, sizeof(rd));
    rd.mem = (const uint8_t *)src_buf;
    rd.mem_size = comp_size;
    rd.remaining = (int64_t)comp_size;
    return xx_ppmd8_decompress_stream(&rd, uncomp_size, order, mem_mb, restore_method,
                                      dst_dev, NULL, 0, NULL, pd);
}

bool xx_ppmd8_decompress_memory(const void *src_buf, size_t src_size,
                                int order, uint32_t mem_mb, int restore_method,
                                void *dst_buf, size_t dst_buf_size, size_t *out_written)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) return false;
    if (src_size == 0) {
        if (out_written) *out_written = 0;
        return true;
    }
    ppmd8_range_dec rd;
    xx_rt_memset(&rd, 0, sizeof(rd));
    rd.mem = (const uint8_t *)src_buf;
    rd.mem_size = src_size;
    rd.remaining = (int64_t)src_size;
    return xx_ppmd8_decompress_stream(&rd, (int64_t)dst_buf_size, order, mem_mb, restore_method,
                                      NULL, (uint8_t *)dst_buf, dst_buf_size, out_written, NULL);
}

/* =========================================================================
 * PPMd8 Compression APIs
 * ========================================================================= */

bool xx_ppmd8_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                          xx_io_device *dst_dev, int order, uint32_t mem_mb, int restore_method,
                          bool write_zip_header, xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev) return false;
    if (uncomp_size == 0) return true;
    if (src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    }
    ppmd8_range_enc re;
    ppmd8_re_init(NULL, &re, dst_dev, NULL, 0);
    return xx_ppmd8_pack_stream(&re, src_dev, NULL, 0, uncomp_size,
                                order, mem_mb, restore_method, write_zip_header, pd);
}

bool xx_ppmd8_pack_source(xx_io_device *src_dev, const char *src_file_path,
                          int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                          xx_io_device *dst_dev, int order, uint32_t mem_mb, int restore_method,
                          bool write_zip_header, xx_pd_struct *pd)
{
    if (!dst_dev) return false;
    xx_io_device *dev = src_dev;
    bool own_dev = false;
    if (!dev && src_file_path) {
        dev = xx_io_file_open(src_file_path, "rb");
        if (!dev) return false;
        own_dev = true;
    }
    if (!dev) return false;

    int64_t uncomp = xx_io_size(dev);
    if (uncomp < 0) {
        if (own_dev) xx_io_close(dev);
        return false;
    }
    if (out_uncomp_size) *out_uncomp_size = uncomp;

    if (out_crc32) {
        if (xx_io_seek(dev, 0, SEEK_SET) != 0) {
            if (own_dev) xx_io_close(dev);
            return false;
        }
        size_t buf_size = xx_get_file_buffer_size();
        if (!buf_size) buf_size = XX_DEFAULT_FILE_BUFFER_SIZE;
        uint8_t *buf = (uint8_t *)xx_mem_alloc(buf_size);
        if (!buf) { if (own_dev) xx_io_close(dev); return false; }
        int64_t rem = uncomp; uint32_t crc = 0; bool rd_ok = true;
        while (rem > 0) {
            size_t want = rem > (int64_t)buf_size ? buf_size : (size_t)rem;
            ssize_t got = xx_io_read(dev, buf, want);
            if (got <= 0) { rd_ok = false; break; }
            crc = xx_crc32_calc(crc, buf, (size_t)got);
            rem -= got;
        }
        xx_mem_free(buf);
        if (!rd_ok) { if (own_dev) xx_io_close(dev); return false; }
        *out_crc32 = crc;
        if (xx_io_seek(dev, 0, SEEK_SET) != 0) {
            if (own_dev) xx_io_close(dev);
            return false;
        }
    }

    ppmd8_range_enc re;
    ppmd8_re_init(NULL, &re, dst_dev, NULL, 0);
    bool ok = xx_ppmd8_pack_stream(&re, dev, NULL, 0, uncomp,
                                   order, mem_mb, restore_method, write_zip_header, pd);
    if (own_dev) xx_io_close(dev);

    if (ok && out_comp_size) {
        *out_comp_size = re.total_written;
    }
    return ok;
}

bool xx_ppmd8_pack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                  const char *dst_file_path, int order, uint32_t mem_mb, int restore_method,
                                  bool write_zip_header, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path) return false;
    xx_io_device *out = xx_io_file_open(dst_file_path, "wb");
    if (!out) return false;
    bool ok = xx_ppmd8_pack_device(src_dev, src_offset, uncomp_size, out,
                                   order, mem_mb, restore_method, write_zip_header, pd);
    xx_io_close(out);
    if (!ok) xx_io_file_remove_a(dst_file_path);
    return ok;
}

bool xx_ppmd8_pack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                    const wchar_t *dst_file_path_w, int order, uint32_t mem_mb, int restore_method,
                                    bool write_zip_header, xx_pd_struct *pd)
{
    if (!src_dev || !dst_file_path_w) return false;
    char *utf8 = xx_str_unicode_to_utf8(dst_file_path_w);
    if (!utf8) return false;
    bool ok = xx_ppmd8_pack_device_to_file(src_dev, src_offset, uncomp_size, utf8,
                                           order, mem_mb, restore_method, write_zip_header, pd);
    xx_str_free(utf8);
    return ok;
}

bool xx_ppmd8_pack_memory_to_device(const void *src_buf, size_t uncomp_size,
                                    xx_io_device *dst_dev, int order, uint32_t mem_mb, int restore_method,
                                    bool write_zip_header, xx_pd_struct *pd)
{
    if (!src_buf || !dst_dev) return false;
    if (uncomp_size == 0) return true;
    ppmd8_range_enc re;
    ppmd8_re_init(NULL, &re, dst_dev, NULL, 0);
    return xx_ppmd8_pack_stream(&re, NULL, (const uint8_t *)src_buf, uncomp_size,
                                (int64_t)uncomp_size, order, mem_mb, restore_method, write_zip_header, pd);
}

bool xx_ppmd8_compress_memory(const void *src_buf, size_t src_size,
                              int order, uint32_t mem_mb, int restore_method,
                              bool write_zip_header,
                              void *dst_buf, size_t dst_buf_size, size_t *out_written)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) return false;
    if (src_size == 0) {
        if (out_written) *out_written = 0;
        return true;
    }
    ppmd8_range_enc re;
    ppmd8_re_init(NULL, &re, NULL, (uint8_t *)dst_buf, dst_buf_size);
    bool ok = xx_ppmd8_pack_stream(&re, NULL, (const uint8_t *)src_buf, src_size,
                                   (int64_t)src_size, order, mem_mb, restore_method, write_zip_header, NULL);
    if (ok && out_written)
        *out_written = (size_t)re.total_written;
    return ok;
}
