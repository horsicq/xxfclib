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

bool xx_deflate_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd)
{
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

bool xx_deflate_unpack_device_ex(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, xx_io_device *dst_dev, bool is_deflate64, size_t window_size,
                                 const void *dictionary, size_t dictionary_size, int64_t *out_consumed, xx_pd_struct *pd)
{
    return xx_deflate_unpack_device_flags(src_dev, src_offset, comp_size, dst_dev, is_deflate64, window_size, dictionary, dictionary_size, out_consumed, 0U, pd);
}

bool xx_deflate_unpack_device_flags(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, xx_io_device *dst_dev, bool is_deflate64, size_t window_size,
                                    const void *dictionary, size_t dictionary_size, int64_t *out_consumed, unsigned flags, xx_pd_struct *pd)
{
    xx_bit_reader reader;
    bool success;
    int64_t fetched, pending, consumed = 0;
    size_t window = xx_deflate_resolve_window(is_deflate64, window_size);
    if (out_consumed) *out_consumed = 0;
    if (!src_dev || !dst_dev || comp_size <= 0 || window == 0U || dictionary_size > window || (dictionary_size != 0U && !dictionary)) return false;
    if ((flags & ~XX_DEFLATE_UNPACK_ZLIB_STRICT) != 0U) return false;
    if (src_offset >= 0 && xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    if (!xx_br_init(&reader, src_dev, NULL, 0, comp_size)) return false;
    success = xx_deflate_decompress_stream_with_flags(&reader, dst_dev, NULL, 0U, NULL, is_deflate64, window, pd, (const uint8_t *)dictionary, dictionary_size,
                                                      flags);
    if (success) {
        /* Reader buffers and bit look-ahead may include following bytes.
         * Only full unused bytes are excluded; final padding stays consumed. */
        if (reader.remaining_input < 0 || reader.remaining_input > comp_size || reader.buffer_pos > reader.buffer_len || reader.bit_count < 0) {
            success = false;
        } else {
            fetched = comp_size - reader.remaining_input;
            pending = (int64_t)(reader.buffer_len - reader.buffer_pos) + reader.bit_count / 8;
            if (pending >= fetched) success = false;
            else consumed = fetched - pending;
        }
    }
    xx_br_free(&reader);
    if (success && out_consumed) *out_consumed = consumed;
    return success;
}

bool xx_deflate_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, const char *dst_file_path, bool is_deflate64, xx_pd_struct *pd)
{
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

bool xx_deflate_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, const wchar_t *dst_file_path_w, bool is_deflate64, xx_pd_struct *pd)
{
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

bool xx_deflate_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                        bool is_deflate64, xx_pd_struct *pd)
{
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

    bool success = xx_deflate_decompress_stream(&reader, NULL, (uint8_t *)dst_buf, dst_buf_size, out_written, is_deflate64, pd);
    xx_br_free(&reader);
    return success;
}

bool xx_deflate_unpack_memory_to_device(const void *src_buf, size_t comp_size, xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd)
{
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

bool xx_deflate_unpack_memory_to_device_ex(const void *src_buf, size_t comp_size, xx_io_device *dst_dev, size_t *out_consumed, bool is_deflate64, xx_pd_struct *pd)
{
    return xx_deflate_unpack_memory_to_device_flags(src_buf, comp_size, dst_dev, out_consumed, is_deflate64, 0U, pd);
}

bool xx_deflate_unpack_memory_to_device_flags(const void *src_buf, size_t comp_size, xx_io_device *dst_dev, size_t *out_consumed, bool is_deflate64, unsigned flags,
                                              xx_pd_struct *pd)
{
    xx_bit_reader reader;
    bool success;
    size_t consumed_bits;
    if (out_consumed) *out_consumed = 0U;
    if (!src_buf || !dst_dev || !out_consumed || comp_size == 0U || (flags & ~XX_DEFLATE_UNPACK_ZLIB_STRICT) != 0U) {
        return false;
    }
    if (!xx_br_init(&reader, NULL, (const uint8_t *)src_buf, comp_size, (int64_t)comp_size)) {
        return false;
    }
    success = xx_deflate_decompress_stream_with_flags(&reader, dst_dev, NULL, 0, NULL, is_deflate64, 0U, pd, NULL, 0U, flags);
    if (success && (reader.mem_pos > SIZE_MAX / 8U || reader.bit_count < 0 || (size_t)reader.bit_count > reader.mem_pos * 8U)) {
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

bool xx_deflate_decompress_memory(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, bool is_deflate64)
{
    return xx_deflate_decompress_memory_with_dictionary(src_buf, src_size, dst_buf, dst_buf_size, out_written, NULL, 0U, is_deflate64);
}

/* The body of the in-memory entry points. */
static bool xx_deflate_decompress_memory_impl(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, const void *dictionary,
                                              size_t dictionary_size, bool is_deflate64, unsigned flags)
{
    if (!src_buf || (!dst_buf && dst_buf_size > 0)) {
        return false;
    }
    if (dictionary_size != 0U && !dictionary) return false;
    if ((flags & ~XX_DEFLATE_UNPACK_ZLIB_STRICT) != 0U) return false;

    if (src_size == 0) {
        if (out_written) *out_written = 0;
        /* No stream at all: zlib waits for input and never reports an end. */
        return (flags & XX_DEFLATE_UNPACK_ZLIB_STRICT) == 0U;
    }

    xx_bit_reader reader;
    if (!xx_br_init(&reader, NULL, (const uint8_t *)src_buf, src_size, (int64_t)src_size)) {
        return false;
    }

    bool success = xx_deflate_decompress_stream_with_flags(&reader, NULL, (uint8_t *)dst_buf, dst_buf_size, out_written, is_deflate64, 0U, NULL,
                                                           (const uint8_t *)dictionary, dictionary_size, flags);
    xx_br_free(&reader);
    return success;
}

bool xx_deflate_decompress_memory_with_dictionary(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, const void *dictionary,
                                                  size_t dictionary_size, bool is_deflate64)
{
    return xx_deflate_decompress_memory_impl(src_buf, src_size, dst_buf, dst_buf_size, out_written, dictionary, dictionary_size, is_deflate64, 0U);
}

bool xx_deflate_decompress_memory_flags(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, bool is_deflate64,
                                        unsigned flags)
{
    return xx_deflate_decompress_memory_impl(src_buf, src_size, dst_buf, dst_buf_size, out_written, NULL, 0U, is_deflate64, flags);
}

/* ========================================================================= */
/* --- Deflate / Deflate64 Compression APIs                               --- */
/* ========================================================================= */

bool xx_deflate_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev, int level, bool is_deflate64, xx_pd_struct *pd)
{
    return xx_deflate_pack_device_ex(src_dev, src_offset, uncomp_size, dst_dev, level, is_deflate64, 0U, pd);
}

bool xx_deflate_pack_device_ex(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev, int level, bool is_deflate64, size_t window_size,
                               xx_pd_struct *pd)
{
    return xx_deflate_pack_device_with_options(src_dev, src_offset, uncomp_size, dst_dev, level, is_deflate64, window_size, NULL, pd);
}

bool xx_deflate_pack_device_with_options(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev,
                                         int level, bool is_deflate64, size_t window_size, const xx_deflate_pack_options *options, xx_pd_struct *pd)
{
    xx_deflate_pack_options_ex extended = {options ? options->max_candidates : 0U, 0U, false, 0U, 0U};
    return xx_deflate_pack_device_with_options_ex(src_dev, src_offset, uncomp_size, dst_dev, level, is_deflate64, window_size, &extended, pd);
}

bool xx_deflate_pack_device_with_options_ex(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev,
                                         int level, bool is_deflate64, size_t window_size, const xx_deflate_pack_options_ex *options, xx_pd_struct *pd)
{
    if (!src_dev || !dst_dev || uncomp_size < 0 || xx_deflate_resolve_window(is_deflate64, window_size) == 0U) {
        return false;
    }

    if (uncomp_size == 0) {
        return true;
    }

    xx_bit_writer writer;
    if (!xx_bw_init(&writer, dst_dev, NULL, 0)) {
        return false;
    }

    bool success = xx_deflate_compress_stream_with_options_ex(src_dev, NULL, 0, src_offset, uncomp_size, &writer, level, is_deflate64, window_size, options, pd);
    xx_bw_free(&writer);
    return success;
}

bool xx_deflate_pack_source(xx_io_device *src_dev, const char *src_file_path, int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                            xx_io_device *dst_dev, int level, bool is_deflate64, xx_pd_struct *pd)
{
    return xx_deflate_pack_source_with_options(src_dev, src_file_path, out_uncomp_size, out_comp_size, out_crc32, dst_dev, level, is_deflate64, NULL, pd);
}

bool xx_deflate_pack_source_with_options(xx_io_device *src_dev, const char *src_file_path, int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                         xx_io_device *dst_dev, int level, bool is_deflate64, const xx_deflate_pack_options *options, xx_pd_struct *pd)
{
    xx_deflate_pack_options_ex extended = {options ? options->max_candidates : 0U, 0U, false, 0U, 0U};
    return xx_deflate_pack_source_with_options_ex(src_dev, src_file_path, out_uncomp_size, out_comp_size, out_crc32, dst_dev, level, is_deflate64, &extended, pd);
}

bool xx_deflate_pack_source_with_options_ex(xx_io_device *src_dev, const char *src_file_path, int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                         xx_io_device *dst_dev, int level, bool is_deflate64, const xx_deflate_pack_options_ex *options, xx_pd_struct *pd)
{
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

    bool success = xx_deflate_compress_stream_with_options_ex(target_dev, NULL, 0, 0, total, &writer, level, is_deflate64, 0U, options, pd);
    *out_comp_size = writer.total_written;
    xx_bw_free(&writer);

    if (owned_dev) {
        xx_io_close(owned_dev);
    }

    return success;
}

bool xx_deflate_compress_memory(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, int level, bool is_deflate64)
{
    return xx_deflate_compress_memory_with_options(src_buf, src_size, dst_buf, dst_buf_size, out_written, level, is_deflate64, NULL);
}

bool xx_deflate_compress_memory_with_options(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                             int level, bool is_deflate64, const xx_deflate_pack_options *options)
{
    xx_deflate_pack_options_ex extended = {options ? options->max_candidates : 0U, 0U, false, 0U, 0U};
    return xx_deflate_compress_memory_with_options_ex(src_buf, src_size, dst_buf, dst_buf_size, out_written, level, is_deflate64, &extended);
}

bool xx_deflate_compress_memory_with_options_ex(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                             int level, bool is_deflate64, const xx_deflate_pack_options_ex *options)
{
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

    bool success = xx_deflate_compress_stream_with_options_ex(NULL, (const uint8_t *)src_buf, src_size, 0, (int64_t)src_size, &writer, level, is_deflate64, 0U, options, NULL);
    if (success && out_written) {
        *out_written = (size_t)writer.total_written;
    }
    xx_bw_free(&writer);
    return success;
}


/* =========================================================================
 * Named packing settings (shared by every writer that exposes them)
 * ========================================================================= */

static const char *const xx_deflate_option_names[XX_DEFLATE_OPTION_COUNT] = {
    "candidates", "lazy-match", "short-match-filter", "nice-match", "good-match"};

const char *xx_deflate_pack_option_name(xx_deflate_pack_option_id id)
{
    return (unsigned)id < (unsigned)XX_DEFLATE_OPTION_COUNT ? xx_deflate_option_names[id] : NULL;
}

static int xx_deflate_option_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool xx_deflate_option_equal(const char *key, size_t length, const char *name)
{
    size_t i;
    for (i = 0; i < length; ++i)
        if (!name[i] || xx_deflate_option_lower((unsigned char)key[i]) != name[i]) return false;
    return name[length] == '\0';
}

xx_deflate_pack_option_id xx_deflate_pack_option_find(const char *key, size_t key_length)
{
    unsigned id;
    if (!key) return XX_DEFLATE_OPTION_COUNT;
    for (id = 0; id < (unsigned)XX_DEFLATE_OPTION_COUNT; ++id)
        if (xx_deflate_option_equal(key, key_length, xx_deflate_option_names[id])) return (xx_deflate_pack_option_id)id;
    return XX_DEFLATE_OPTION_COUNT;
}

bool xx_deflate_pack_option_set(xx_deflate_pack_options_ex *options, xx_deflate_pack_option_id id, uint64_t value)
{
    if (!options) return false;
    if (id == XX_DEFLATE_OPTION_SHORT_MATCH_FILTER) {
        if (value > 1U) return false;
        options->short_match_filter = value != 0U;
        return true;
    }
    if (value > (uint64_t)SIZE_MAX) return false;
    switch (id) {
        case XX_DEFLATE_OPTION_CANDIDATES: options->max_candidates = (size_t)value; return true;
        case XX_DEFLATE_OPTION_LAZY_MATCH: options->lazy_match_limit = (size_t)value; return true;
        case XX_DEFLATE_OPTION_NICE_MATCH: options->nice_match = (size_t)value; return true;
        case XX_DEFLATE_OPTION_GOOD_MATCH: options->good_match = (size_t)value; return true;
        default: return false;
    }
}

bool xx_deflate_pack_option_get(const xx_deflate_pack_options_ex *options, xx_deflate_pack_option_id id, uint64_t *value)
{
    if (!options || !value) return false;
    switch (id) {
        case XX_DEFLATE_OPTION_CANDIDATES: *value = options->max_candidates; return true;
        case XX_DEFLATE_OPTION_LAZY_MATCH: *value = options->lazy_match_limit; return true;
        case XX_DEFLATE_OPTION_SHORT_MATCH_FILTER: *value = options->short_match_filter ? 1U : 0U; return true;
        case XX_DEFLATE_OPTION_NICE_MATCH: *value = options->nice_match; return true;
        case XX_DEFLATE_OPTION_GOOD_MATCH: *value = options->good_match; return true;
        default: return false;
    }
}

static bool xx_deflate_option_space(char c)
{
    return c == ' ' || c == '\t';
}

/* Decimal digits only, at most UINT64_MAX; "true"/"false" when allow_bool. */
static bool xx_deflate_option_number(const char *text, size_t length, bool allow_bool, uint64_t *value)
{
    uint64_t result = 0U;
    size_t i;
    if (allow_bool && xx_deflate_option_equal(text, length, "true")) { *value = 1U; return true; }
    if (allow_bool && xx_deflate_option_equal(text, length, "false")) { *value = 0U; return true; }
    if (!length) return false;
    for (i = 0; i < length; ++i) {
        unsigned digit;
        if (text[i] < '0' || text[i] > '9') return false;
        digit = (unsigned)(text[i] - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

bool xx_deflate_pack_options_parse(xx_deflate_pack_options_ex *options, int *level, const char *text, const char **error_key)
{
    xx_deflate_pack_options_ex parsed;
    int parsed_level;
    const char *cursor = text;
    if (error_key) *error_key = NULL;
    if (!options || !text) return false;
    parsed = *options;
    parsed_level = level ? *level : 0;
    while (*cursor) {
        const char *pair, *key, *key_end, *value, *value_end, *end;
        uint64_t number = 0U;
        while (*cursor == ';' || *cursor == ',' || xx_deflate_option_space(*cursor)) ++cursor;
        if (!*cursor) break;
        pair = cursor;
        end = cursor;
        while (*end && *end != ';' && *end != ',') ++end;
        key = pair;
        key_end = key;
        while (key_end < end && *key_end != '=') ++key_end;
        if (key_end == end) goto bad;
        value = key_end + 1;
        while (key_end > key && xx_deflate_option_space(key_end[-1])) --key_end;
        while (value < end && xx_deflate_option_space(*value)) ++value;
        value_end = end;
        while (value_end > value && xx_deflate_option_space(value_end[-1])) --value_end;
        if (level && xx_deflate_option_equal(key, (size_t)(key_end - key), "level")) {
            if (!xx_deflate_option_number(value, (size_t)(value_end - value), false, &number) || number > (uint64_t)XX_DEFLATE_LEVEL_BEST) goto bad;
            parsed_level = (int)number;
        } else {
            xx_deflate_pack_option_id id = xx_deflate_pack_option_find(key, (size_t)(key_end - key));
            if (id == XX_DEFLATE_OPTION_COUNT ||
                !xx_deflate_option_number(value, (size_t)(value_end - value), id == XX_DEFLATE_OPTION_SHORT_MATCH_FILTER, &number) ||
                !xx_deflate_pack_option_set(&parsed, id, number))
                goto bad;
        }
        cursor = end;
        continue;
    bad:
        if (error_key) *error_key = pair;
        return false;
    }
    *options = parsed;
    if (level) *level = parsed_level;
    return true;
}

size_t xx_deflate_pack_options_format(const xx_deflate_pack_options_ex *options, char *buffer, size_t capacity)
{
    char text[XX_DEFLATE_OPTION_COUNT * 48];
    size_t used = 0U;
    unsigned id;
    if (!options) return 0U;
    for (id = 0; id < (unsigned)XX_DEFLATE_OPTION_COUNT; ++id) {
        uint64_t value = 0U;
        char digits[24];
        size_t n = 0U;
        const char *name = xx_deflate_option_names[id];
        (void)xx_deflate_pack_option_get(options, (xx_deflate_pack_option_id)id, &value);
        do { digits[n++] = (char)('0' + (int)(value % 10U)); value /= 10U; } while (value);
        if (id) text[used++] = ';';
        while (*name) text[used++] = *name++;
        text[used++] = '=';
        while (n) text[used++] = digits[--n];
    }
    text[used] = '\0';
    if (buffer && capacity) {
        size_t copy = used < capacity - 1U ? used : capacity - 1U;
        size_t i;
        for (i = 0; i < copy; ++i) buffer[i] = text[i];
        buffer[copy] = '\0';
    }
    return used;
}
