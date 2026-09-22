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

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"

#define _XX_STORE_BUFFER_SIZE xx_get_file_buffer_size()

/* ========================================================================= */
/* --- Directory and File Attribute Helpers (Stubs forwarding to xx_io)   --- */
/* ========================================================================= */

bool xx_store_create_dirs_w(const wchar_t *path, bool is_dir) {
    return xx_io_create_dirs_w(path, is_dir);
}

bool xx_store_create_dirs_a(const char *path, bool is_dir) {
    return xx_io_create_dirs_a(path, is_dir);
}

bool xx_store_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    return xx_io_apply_dos_time_and_attrs_w(path, dos_date, dos_time, attrs);
}

bool xx_store_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    return xx_io_apply_dos_time_and_attrs_a(path, dos_date, dos_time, attrs);
}



/* ========================================================================= */
/* --- STORE Unpacking Implementation                                    --- */
/* ========================================================================= */

bool xx_store_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                            xx_io_device *dst_dev, xx_pd_struct *pd) {
    if (!src_dev || !dst_dev || size < 0) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    if (src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
            return false;
        }
    }

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, (uint64_t)size, "Unpacking STORE");
    }

    size_t buf_size = _XX_STORE_BUFFER_SIZE;
    if (buf_size == 0) {
        buf_size = XX_DEFAULT_FILE_BUFFER_SIZE;
    }
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buf_size);
    if (!buffer) {
        if (pd && pd_level >= 0) {
            xx_pd_leave_level(pd, pd_level);
        }
        return false;
    }

    int64_t remaining = size;
    int64_t processed = 0;
    bool success = true;

    while (remaining > 0) {
        if (pd && xx_pd_is_stopped(pd)) {
            success = false;
            break;
        }

        size_t chunk = (remaining > (int64_t)buf_size) ? buf_size : (size_t)remaining;
        ssize_t n_read = xx_io_read(src_dev, buffer, chunk);
        if (n_read <= 0) {
            success = false;
            break;
        }

        ssize_t n_written = xx_io_write(dst_dev, buffer, (size_t)n_read);
        if (n_written != n_read) {
            success = false;
            break;
        }

        remaining -= n_read;
        processed += n_read;

        if (pd && pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, (uint64_t)processed);
        }
    }

    xx_mem_free(buffer);

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    return success && (remaining == 0);
}

bool xx_store_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                                    const char *dst_file_path, xx_pd_struct *pd) {
    if (!src_dev || !dst_file_path || size < 0) {
        return false;
    }

    xx_io_device *out_file = xx_io_file_open(dst_file_path, "wb");
    if (!out_file) {
        return false;
    }

    bool success = xx_store_unpack_device(src_dev, src_offset, size, out_file, pd);
    xx_io_close(out_file);
    return success;
}

bool xx_store_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                                      const wchar_t *dst_file_path_w, xx_pd_struct *pd) {
    if (!src_dev || !dst_file_path_w || size < 0) {
        return false;
    }
    char *utf8_path = xx_str_unicode_to_utf8(dst_file_path_w);
    if (!utf8_path) {
        return false;
    }
    bool success = xx_store_unpack_device_to_file(src_dev, src_offset, size, utf8_path, pd);
    xx_str_free(utf8_path);
    return success;
}

bool xx_store_unpack_memory_to_device(const void *src_buf, size_t size,
                                      xx_io_device *dst_dev, xx_pd_struct *pd) {
    if (!src_buf && size > 0) {
        return false;
    }
    if (!dst_dev) {
        return false;
    }

    xx_io_device *mem_dev = xx_io_mem_open_ro(src_buf, size);
    if (!mem_dev) {
        return false;
    }

    bool success = xx_store_unpack_device(mem_dev, 0, (int64_t)size, dst_dev, pd);
    xx_io_close(mem_dev);
    return success;
}

bool xx_store_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, size_t size,
                                      void *dst_buf, size_t dst_buf_size,
                                      size_t *out_written, xx_pd_struct *pd) {
    if (!src_dev || (!dst_buf && size > 0) || size > dst_buf_size) {
        return false;
    }

    xx_io_device *mem_dev = xx_io_mem_open(dst_buf, dst_buf_size);
    if (!mem_dev) {
        return false;
    }

    bool success = xx_store_unpack_device(src_dev, src_offset, (int64_t)size, mem_dev, pd);
    xx_io_close(mem_dev);

    if (success && out_written) {
        *out_written = size;
    }
    return success;
}


/* ========================================================================= */
/* --- STORE Packing Implementation                                      --- */
/* ========================================================================= */

bool xx_store_prepare_source(xx_io_device *src_dev, const char *src_file_path,
                             int64_t *out_size, uint32_t *out_crc32, xx_pd_struct *pd) {
    if (!out_size || !out_crc32) {
        return false;
    }

    *out_size = 0;
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

    *out_size = total;
    if (total == 0) {
        if (owned_dev) xx_io_close(owned_dev);
        return true;
    }

    if (xx_io_seek(target_dev, 0, SEEK_SET) != 0) {
        if (owned_dev) xx_io_close(owned_dev);
        return false;
    }

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, (uint64_t)total, "Preparing STORE source (CRC32)");
    }

    size_t buf_size = _XX_STORE_BUFFER_SIZE;
    if (buf_size == 0) {
        buf_size = XX_DEFAULT_FILE_BUFFER_SIZE;
    }
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buf_size);
    if (!buffer) {
        if (pd && pd_level >= 0) {
            xx_pd_leave_level(pd, pd_level);
        }
        if (owned_dev) {
            xx_io_close(owned_dev);
        }
        return false;
    }

    int64_t remaining = total;
    int64_t processed = 0;
    uint32_t running_crc = 0;
    bool success = true;

    while (remaining > 0) {
        if (pd && xx_pd_is_stopped(pd)) {
            success = false;
            break;
        }

        size_t chunk = (remaining > (int64_t)buf_size) ? buf_size : (size_t)remaining;
        ssize_t n_read = xx_io_read(target_dev, buffer, chunk);
        if (n_read <= 0) {
            success = false;
            break;
        }

        running_crc = xx_crc32_calc(running_crc, buffer, (size_t)n_read);
        remaining -= n_read;
        processed += n_read;

        if (pd && pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, (uint64_t)processed);
        }
    }

    xx_mem_free(buffer);

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    /* Rewind seek position back to 0 so caller can stream data immediately */
    xx_io_seek(target_dev, 0, SEEK_SET);

    if (owned_dev) {
        xx_io_close(owned_dev);
    }

    if (success && remaining == 0) {
        *out_crc32 = running_crc;
        return true;
    }
    return false;
}

bool xx_store_pack_source(xx_io_device *src_dev, const char *src_file_path,
                          int64_t size, xx_io_device *dst_dev, xx_pd_struct *pd) {
    if (!dst_dev || size < 0) {
        return false;
    }
    if (size == 0) {
        return true;
    }

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

    bool success = xx_store_unpack_device(target_dev, 0, size, dst_dev, pd);

    if (owned_dev) {
        xx_io_close(owned_dev);
    }
    return success;
}
