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
#include "xx_7zip_datastructs_internal.h"
#include "xxfclib/formats/archive/7zip/xx_7zip.h"
#include "xx_7zip_defs.h"
#include "xx_7zip_branch.h"
#include "xx_7zip_stream.h"
#include "xx_7zip_writer.h"
#include "xxfclib/io/xx_io.h"
#include "../../../io/xx_io_policy.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/ppmd7/xx_ppmd7.h"

/* Native separator, reserved device names and OS entropy. */
#include "../../../io/platforms/xx_io_platform.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "xxfclib/data/xx_data.h"

#define XX_7ZIP_MAX_HEADER_SIZE (64U * 1024U * 1024U)
#define XX_7ZIP_MAX_ITEMS 262144U
#define XX_7ZIP_MAX_CODERS 64U
#define XX_7ZIP_MAX_PROPERTIES 64U


typedef struct xx_7zip_reader_s {
    const uint8_t *data;
    size_t size;
    size_t pos;
} xx_7zip_reader;

typedef struct xx_7zip_extract_cache_s {
    xx_io_device *device;
    uint64_t size;
    uint64_t folder_index;
    /* Test: the folder whose members were checked as it was decoded
     * (xx_7zip_verify_folder), and a verdict per file: 1 sound, 2 not. */
    uint64_t verified_folder;
    uint8_t *verdicts;
    uint64_t extracted_folder;
    uint8_t *extracted_verdicts;
} xx_7zip_extract_cache;

static void xx_7zip_extract_cache_clear(xx_7zip_extract_cache *cache)
{
    if (!cache) return;
    if (cache->device) xx_io_close(cache->device);
    cache->device = NULL;
    cache->size = 0U;
    cache->folder_index = UINT64_MAX;
    cache->extracted_folder = UINT64_MAX;
}

static void xx_7zip_extract_cache_free(void *ptr)
{
    xx_7zip_extract_cache *cache = (xx_7zip_extract_cache *)ptr;
    if (!cache) return;
    xx_7zip_extract_cache_clear(cache);
    xx_mem_free(cache->verdicts);
    xx_mem_free(cache->extracted_verdicts);
    xx_mem_free(cache);
}

typedef struct xx_7zip_folder_decode_s {
    Abstractformat *format;
    const xx_7zip_streams *streams;
    const xx_7zip_folder *folder;
    xx_pd_struct *pd;
    xx_io_device **input_data;
    uint64_t *input_sizes;
    xx_io_device **output_data;
    uint64_t *output_sizes;
    uint8_t *output_busy;
    xx_io_device *destination;
    uint8_t *password;
    size_t password_size;
    /* The level that shows how far the folder is decoded, in bytes of the
     * folder (-1: none), from the bytes all its coders have written of the
     * `written_total` they write. */
    int level;
    uint64_t written, written_total;
    uint32_t final_crc;
    bool has_final_crc;
} xx_7zip_folder_decode;

/* A coder's output: the bytes written to it count towards the level. */
typedef struct xx_7zip_progress_sink_s {
    xx_io_device device;
    xx_io_device *target;
    xx_7zip_folder_decode *context;
} xx_7zip_progress_sink;

typedef struct xx_7zip_buffer_device_s {
    xx_io_device device;
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    size_t position;
} xx_7zip_buffer_device;

static void xx_7zip_vtable_destroy(Abstractformat *self);
static bool xx_7zip_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size);
static bool xx_7zip_get_password_utf16le(const Abstractformat *self, uint8_t **password, size_t *password_size);

static ssize_t xx_7zip_buffer_read(xx_io_device *device, void *buffer, size_t size)
{
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    size_t available;
    if (!sink || (!buffer && size)) return -1;
    available = sink->length - sink->position;
    if (size > available) size = available;
    if (size) xx_mem_copy(buffer, sink->buffer + sink->position, size);
    sink->position += size;
    return (ssize_t)size;
}

static ssize_t xx_7zip_buffer_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    if (!sink || (!buffer && size) || size > sink->capacity - sink->position) return -1;
    if (size) xx_mem_copy(sink->buffer + sink->position, buffer, size);
    sink->position += size;
    if (sink->position > sink->length) sink->length = sink->position;
    return (ssize_t)size;
}

static int xx_7zip_buffer_seek(xx_io_device *device, long offset, int origin)
{
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    int64_t base;
    int64_t next;
    if (!sink) return -1;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = (int64_t)sink->position;
    else if (origin == SEEK_END) base = (int64_t)sink->length;
    else return -1;
    next = base + offset;
    if (next < 0 || (uint64_t)next > sink->capacity) return -1;
    sink->position = (size_t)next;
    return 0;
}

static int64_t xx_7zip_buffer_size(xx_io_device *device)
{
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    return sink ? (int64_t)sink->length : -1;
}

static void xx_7zip_buffer_device_init(xx_7zip_buffer_device *sink, uint8_t *buffer, size_t capacity)
{
    xx_mem_zero(sink, sizeof(*sink));
    sink->buffer = buffer;
    sink->capacity = capacity;
    sink->device.read = xx_7zip_buffer_read;
    sink->device.write = xx_7zip_buffer_write;
    sink->device.seek = xx_7zip_buffer_seek;
    sink->device.total_size = xx_7zip_buffer_size;
    sink->device.get_total_size = xx_7zip_buffer_size;
    sink->device.size = xx_7zip_buffer_size;
    sink->device.priv = sink;
}

static int xx_7zip_buffer_seek64(xx_io_device *device, int64_t offset, int origin)
{
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    int64_t base, next;
    if (!sink) return -1;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = (int64_t)sink->position;
    else if (origin == SEEK_END) base = (int64_t)sink->length;
    else return -1;
    next = base + offset;
    if (next < 0 || (uint64_t)next > sink->capacity) return -1;
    sink->position = (size_t)next;
    return 0;
}

static int xx_7zip_buffer_close(xx_io_device *device)
{
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    if (sink) {
        if (sink->buffer) xx_mem_free(sink->buffer);
        xx_mem_free(sink);
    }
    return 0;
}

static xx_io_device *xx_7zip_mem_temp_open(size_t capacity)
{
    xx_7zip_buffer_device *sink = (xx_7zip_buffer_device *)xx_mem_calloc(1U, sizeof(*sink));
    if (!sink) return NULL;
    sink->buffer = (uint8_t *)xx_mem_alloc(capacity ? capacity : 1U);
    if (!sink->buffer) {
        xx_mem_free(sink);
        return NULL;
    }
    xx_7zip_buffer_device_init(sink, sink->buffer, capacity);
    sink->device.seek64 = xx_7zip_buffer_seek64;
    sink->device.close = xx_7zip_buffer_close;
    return &sink->device;
}

static bool xx_7zip_u64_add(uint64_t a, uint64_t b, uint64_t *out)
{
    if (!out || UINT64_MAX - a < b) return false;
    *out = a + b;
    return true;
}

static bool xx_7zip_size_multiply(uint64_t count, size_t size, size_t *out)
{
    if (!out || count > (uint64_t)(SIZE_MAX / (size ? size : 1U))) return false;
    *out = (size_t)count * size;
    return true;
}

static void *xx_7zip_alloc_array(uint64_t count, size_t size)
{
    size_t bytes;
    if (count == 0) return NULL;
    if (count > XX_7ZIP_MAX_ITEMS || !xx_7zip_size_multiply(count, size, &bytes)) return NULL;
    return xx_mem_calloc(1, bytes);
}

static bool xx_7zip_reader_u8(xx_7zip_reader *r, uint8_t *value)
{
    if (!r || !value || r->pos >= r->size) return false;
    *value = r->data[r->pos++];
    return true;
}

static bool xx_7zip_reader_u32(xx_7zip_reader *r, uint32_t *value)
{
    if (!r || !value || r->size - r->pos < 4U) return false;
    *value = xx_data_get_u32(r->data + r->pos, 4, 0, false);
    r->pos += 4U;
    return true;
}

static bool xx_7zip_reader_u64_real(xx_7zip_reader *r, uint64_t *value)
{
    if (!r || !value || r->size - r->pos < 8U) return false;
    *value = xx_data_get_u64(r->data + r->pos, 8, 0, false);
    r->pos += 8U;
    return true;
}

static bool xx_7zip_reader_number(xx_7zip_reader *r, uint64_t *value)
{
    uint8_t first;
    uint8_t mask = 0x80U;
    uint64_t result = 0;
    unsigned i;
    if (!xx_7zip_reader_u8(r, &first)) return false;
    for (i = 0; i < 8; ++i) {
        uint8_t next;
        if ((first & mask) == 0) {
            result |= ((uint64_t)(first & (uint8_t)(mask - 1U))) << (8U * i);
            *value = result;
            return true;
        }
        if (!xx_7zip_reader_u8(r, &next)) return false;
        result |= ((uint64_t)next) << (8U * i);
        mask >>= 1;
    }
    *value = result;
    return true;
}

static bool xx_7zip_reader_bytes(xx_7zip_reader *r, void *dst, size_t size)
{
    if (!r || size > r->size - r->pos) return false;
    if (dst && size) xx_mem_copy(dst, r->data + r->pos, size);
    r->pos += size;
    return true;
}

static bool xx_7zip_reader_skip(xx_7zip_reader *r, uint64_t size)
{
    if (!r || size > (uint64_t)(r->size - r->pos)) return false;
    r->pos += (size_t)size;
    return true;
}

static bool xx_7zip_reader_subreader(xx_7zip_reader *r, uint64_t size, xx_7zip_reader *sub)
{
    if (!r || !sub || size > (uint64_t)(r->size - r->pos)) return false;
    sub->data = r->data + r->pos;
    sub->size = (size_t)size;
    sub->pos = 0;
    r->pos += (size_t)size;
    return true;
}

static bool xx_7zip_bit(const uint8_t *bits, uint64_t index)
{
    return bits && (bits[index >> 3] & (uint8_t)(0x80U >> (index & 7U))) != 0;
}

static void xx_7zip_folder_cleanup(xx_7zip_folder *folder)
{
    if (!folder) return;
    if (folder->coders) xx_mem_free(folder->coders);
    if (folder->bind_inputs) xx_mem_free(folder->bind_inputs);
    if (folder->bind_outputs) xx_mem_free(folder->bind_outputs);
    if (folder->packed_indices) xx_mem_free(folder->packed_indices);
    if (folder->unpack_sizes) xx_mem_free(folder->unpack_sizes);
    xx_mem_zero(folder, sizeof(*folder));
}

static void xx_7zip_streams_cleanup(xx_7zip_streams *streams)
{
    uint64_t i;
    if (!streams) return;
    if (streams->folders) {
        for (i = 0; i < streams->num_folders; ++i) xx_7zip_folder_cleanup(&streams->folders[i]);
        xx_mem_free(streams->folders);
    }
    if (streams->pack_sizes) xx_mem_free(streams->pack_sizes);
    if (streams->pack_crcs) xx_mem_free(streams->pack_crcs);
    if (streams->pack_crc_defined) xx_mem_free(streams->pack_crc_defined);
    if (streams->sub_sizes) xx_mem_free(streams->sub_sizes);
    if (streams->sub_crcs) xx_mem_free(streams->sub_crcs);
    if (streams->sub_crc_defined) xx_mem_free(streams->sub_crc_defined);
    xx_mem_zero(streams, sizeof(*streams));
}

static void xx_7zip_private_free(xx_7zip_private *priv)
{
    uint64_t i;
    if (!priv) return;
    xx_7zip_streams_cleanup(&priv->streams);
    if (priv->files) {
        for (i = 0; i < priv->num_files; ++i) {
            if (priv->files[i].name) xx_str_wfree(priv->files[i].name);
        }
        xx_mem_free(priv->files);
    }
    xx_mem_free(priv);
}

static bool xx_7zip_parse_digests(xx_7zip_reader *r, uint64_t count, uint8_t *defined, uint32_t *crcs)
{
    uint8_t all_defined;
    uint8_t *bits = NULL;
    size_t bit_bytes = 0;
    uint64_t i;
    if (!xx_7zip_reader_u8(r, &all_defined)) return false;
    if (!all_defined && count) {
        if (count > UINT64_MAX - 7U) return false;
        bit_bytes = (size_t)((count + 7U) / 8U);
        bits = (uint8_t *)xx_mem_alloc(bit_bytes);
        if (!bits || !xx_7zip_reader_bytes(r, bits, bit_bytes)) {
            if (bits) xx_mem_free(bits);
            return false;
        }
    }
    for (i = 0; i < count; ++i) {
        bool is_defined = all_defined != 0 || xx_7zip_bit(bits, i);
        uint32_t crc = 0;
        if (is_defined && !xx_7zip_reader_u32(r, &crc)) {
            if (bits) xx_mem_free(bits);
            return false;
        }
        if (defined) defined[i] = is_defined ? 1U : 0U;
        if (crcs) crcs[i] = crc;
    }
    if (bits) xx_mem_free(bits);
    return true;
}

static bool xx_7zip_parse_pack_info(xx_7zip_reader *r, xx_7zip_streams *streams)
{
    uint64_t count;
    uint8_t nid;
    bool have_sizes = false;
    uint64_t i;
    if (!xx_7zip_reader_number(r, &streams->pack_pos) || !xx_7zip_reader_number(r, &count) || count > XX_7ZIP_MAX_ITEMS) return false;
    streams->num_packs = count;
    if (count) {
        streams->pack_sizes = (uint64_t *)xx_7zip_alloc_array(count, sizeof(uint64_t));
        streams->pack_crcs = (uint32_t *)xx_7zip_alloc_array(count, sizeof(uint32_t));
        streams->pack_crc_defined = (uint8_t *)xx_7zip_alloc_array(count, sizeof(uint8_t));
        if (!streams->pack_sizes || !streams->pack_crcs || !streams->pack_crc_defined) return false;
    }
    for (;;) {
        if (!xx_7zip_reader_u8(r, &nid)) return false;
        if (nid == XX_7ZIP_NID_END) break;
        if (nid == XX_7ZIP_NID_SIZE) {
            if (have_sizes) return false;
            for (i = 0; i < count; ++i) {
                if (!xx_7zip_reader_number(r, &streams->pack_sizes[i])) return false;
            }
            have_sizes = true;
        } else if (nid == XX_7ZIP_NID_CRC) {
            if (!xx_7zip_parse_digests(r, count, streams->pack_crc_defined, streams->pack_crcs)) return false;
        } else {
            return false;
        }
    }
    return count == 0 || have_sizes;
}

static bool xx_7zip_aes_properties_supported(const xx_7zip_coder *coder)
{
    size_t salt_size;
    size_t iv_size;
    uint8_t first;
    uint8_t second;
    if (!coder || coder->method != XX_7ZIP_METHOD_AES || coder->properties_size < 1U) return false;
    first = coder->properties[0];
    if ((first & 0x3FU) > 24U && (first & 0x3FU) != 0x3FU) return false;
    if ((first & 0xC0U) == 0U) return coder->properties_size == 1U;
    if (coder->properties_size < 2U) return false;
    second = coder->properties[1];
    salt_size = (size_t)((first >> 7U) & 1U) + (size_t)(second >> 4U);
    iv_size = (size_t)((first >> 6U) & 1U) + (size_t)(second & 0x0FU);
    return salt_size <= 16U && iv_size <= 16U && coder->properties_size == 2U + salt_size + iv_size;
}

static bool xx_7zip_coder_supported(const xx_7zip_coder *coder)
{
    uint32_t memory_size;
    if (!coder) return false;
    if (coder->method == XX_7ZIP_METHOD_COPY) return coder->properties_size == 0U;
    if (coder->method == XX_7ZIP_METHOD_LZMA) return coder->properties_size == XX_LZMA_PROPS_SIZE;
    if (coder->method == XX_7ZIP_METHOD_LZMA2) return coder->properties_size == XX_LZMA2_PROPS_SIZE && coder->properties[0] <= 40U;
    if (coder->method == XX_7ZIP_METHOD_BZIP2) return coder->properties_size == 0U;
    if (coder->method == XX_7ZIP_METHOD_DEFLATE || coder->method == XX_7ZIP_METHOD_DEFLATE64) return coder->properties_size == 0U;
    if (coder->method == XX_7ZIP_METHOD_DELTA) return coder->properties_size == 1U;
    if (coder->method == XX_7ZIP_METHOD_SWAP2 || coder->method == XX_7ZIP_METHOD_SWAP4) return coder->properties_size == 0U;
    if (coder->method == XX_7ZIP_METHOD_PPMD7) {
        memory_size = coder->properties_size == 5U ? xx_data_get_u32(coder->properties + 1U, 4, 0, false) : 0U;
        return coder->properties_size == 5U && coder->properties[0] >= 2U && coder->properties[0] <= 64U && memory_size >= XX_PPMD7_MIN_MEM_SIZE &&
               memory_size <= XX_PPMD7_MAX_MEM_SIZE;
    }
    if (coder->method == XX_7ZIP_METHOD_ZSTD) return coder->properties_size == 1U || coder->properties_size == 3U || coder->properties_size == 5U;
    if (coder->method == XX_7ZIP_METHOD_BROTLI || coder->method == XX_7ZIP_METHOD_LIZARD) return coder->properties_size == 3U;
    if (coder->method == XX_7ZIP_METHOD_LZ4 || coder->method == XX_7ZIP_METHOD_LZ5) return coder->properties_size == 5U;
    if (coder->method == XX_7ZIP_METHOD_BCJ || coder->method == XX_7ZIP_METHOD_PPC || coder->method == XX_7ZIP_METHOD_IA64 || coder->method == XX_7ZIP_METHOD_ARM ||
        coder->method == XX_7ZIP_METHOD_ARMT || coder->method == XX_7ZIP_METHOD_SPARC || coder->method == XX_7ZIP_METHOD_ARM64 || coder->method == XX_7ZIP_METHOD_RISCV)
        return xx_7zip_branch_method_supported(coder->method, coder->properties_size);
    return xx_7zip_aes_properties_supported(coder);
}

static bool xx_7zip_parse_folder(xx_7zip_reader *r, xx_7zip_folder *folder)
{
    uint64_t num_coders;
    uint64_t total_in = 0;
    uint64_t total_out = 0;
    uint64_t bind_count;
    uint64_t packed_count;
    uint8_t *bound_inputs = NULL;
    uint8_t *bound_outputs = NULL;
    size_t bound_bytes = 0;
    size_t output_bound_bytes = 0;
    uint64_t i;
    if (!xx_7zip_reader_number(r, &num_coders) || num_coders == 0 || num_coders > XX_7ZIP_MAX_CODERS) return false;
    folder->coders = (xx_7zip_coder *)xx_7zip_alloc_array(num_coders, sizeof(xx_7zip_coder));
    if (!folder->coders) return false;
    folder->coder_count = (size_t)num_coders;
    for (i = 0; i < num_coders; ++i) {
        xx_7zip_coder *coder = &folder->coders[i];
        uint8_t flags;
        size_t id_size;
        size_t j;
        uint64_t prop_size = 0;
        if (!xx_7zip_reader_u8(r, &flags) || (flags & 0xC0U) != 0) return false;
        id_size = (size_t)(flags & 0x0FU);
        if (id_size == 0 || id_size > 8U || id_size > r->size - r->pos) return false;
        coder->method = 0;
        for (j = 0; j < id_size; ++j) coder->method = (coder->method << 8) | r->data[r->pos++];
        coder->num_in = 1;
        coder->num_out = 1;
        if ((flags & 0x10U) != 0) {
            if (!xx_7zip_reader_number(r, &coder->num_in) || !xx_7zip_reader_number(r, &coder->num_out) || coder->num_in == 0 || coder->num_out == 0) return false;
        }
        if (!xx_7zip_u64_add(total_in, coder->num_in, &total_in) || !xx_7zip_u64_add(total_out, coder->num_out, &total_out) || total_in > XX_7ZIP_MAX_ITEMS ||
            total_out > XX_7ZIP_MAX_ITEMS)
            return false;
        if ((flags & 0x20U) != 0) {
            if (!xx_7zip_reader_number(r, &prop_size) || prop_size > XX_7ZIP_MAX_PROPERTIES || !xx_7zip_reader_bytes(r, coder->properties, (size_t)prop_size))
                return false;
            coder->properties_size = (size_t)prop_size;
        }
        if (coder->method == XX_7ZIP_METHOD_AES) folder->encrypted = true;
    }
    if (total_out == 0) return false;
    bind_count = total_out - 1U;
    if (total_in < bind_count) return false;
    packed_count = total_in - bind_count;
    if (packed_count == 0 || packed_count > XX_7ZIP_MAX_ITEMS) return false;
    folder->bind_count = (size_t)bind_count;
    if (bind_count) {
        folder->bind_inputs = (uint64_t *)xx_7zip_alloc_array(bind_count, sizeof(uint64_t));
        folder->bind_outputs = (uint64_t *)xx_7zip_alloc_array(bind_count, sizeof(uint64_t));
        if (!folder->bind_inputs || !folder->bind_outputs) return false;
    }
    folder->packed_indices = (uint64_t *)xx_7zip_alloc_array(packed_count, sizeof(uint64_t));
    if (!folder->packed_indices) return false;
    if (total_in) {
        bound_bytes = (size_t)((total_in + 7U) / 8U);
        bound_inputs = (uint8_t *)xx_mem_calloc(1, bound_bytes);
        if (!bound_inputs) return false;
    }
    output_bound_bytes = (size_t)((total_out + 7U) / 8U);
    bound_outputs = (uint8_t *)xx_mem_calloc(1, output_bound_bytes);
    if (!bound_outputs) {
        xx_mem_free(bound_inputs);
        return false;
    }
    for (i = 0; i < bind_count; ++i) {
        uint64_t in_index, out_index;
        if (!xx_7zip_reader_number(r, &in_index) || !xx_7zip_reader_number(r, &out_index) || in_index >= total_in || out_index >= total_out ||
            xx_7zip_bit(bound_inputs, in_index) || xx_7zip_bit(bound_outputs, out_index)) {
            xx_mem_free(bound_outputs);
            xx_mem_free(bound_inputs);
            return false;
        }
        bound_inputs[in_index >> 3] |= (uint8_t)(0x80U >> (in_index & 7U));
        bound_outputs[out_index >> 3] |= (uint8_t)(0x80U >> (out_index & 7U));
        folder->bind_inputs[i] = in_index;
        folder->bind_outputs[i] = out_index;
    }
    if (packed_count > 1) {
        uint8_t *seen = (uint8_t *)xx_mem_calloc(1, bound_bytes);
        if (!seen) {
            xx_mem_free(bound_outputs);
            xx_mem_free(bound_inputs);
            return false;
        }
        for (i = 0; i < packed_count; ++i) {
            uint64_t index;
            if (!xx_7zip_reader_number(r, &index) || index >= total_in || xx_7zip_bit(bound_inputs, index) || xx_7zip_bit(seen, index)) {
                xx_mem_free(seen);
                xx_mem_free(bound_outputs);
                xx_mem_free(bound_inputs);
                return false;
            }
            seen[index >> 3] |= (uint8_t)(0x80U >> (index & 7U));
            folder->packed_indices[i] = index;
        }
        xx_mem_free(seen);
    } else {
        for (i = 0; i < total_in; ++i) {
            if (!xx_7zip_bit(bound_inputs, i)) {
                folder->packed_indices[0] = i;
                break;
            }
        }
        if (i == total_in) {
            xx_mem_free(bound_outputs);
            xx_mem_free(bound_inputs);
            return false;
        }
    }
    folder->final_output_index = UINT64_MAX;
    for (i = 0; i < total_out; ++i) {
        if (!xx_7zip_bit(bound_outputs, i)) {
            if (folder->final_output_index != UINT64_MAX) {
                xx_mem_free(bound_outputs);
                xx_mem_free(bound_inputs);
                return false;
            }
            folder->final_output_index = i;
        }
    }
    xx_mem_free(bound_outputs);
    xx_mem_free(bound_inputs);
    if (folder->final_output_index == UINT64_MAX) return false;
    folder->total_in = total_in;
    folder->total_out = total_out;
    folder->packed_count = packed_count;
    folder->method = folder->coders[0].method;
    {
        size_t coder_index;
        size_t aes_count = 0U;
        size_t bcj2_count = 0U;
        bool all_supported = true;
        for (coder_index = 0U; coder_index < folder->coder_count; ++coder_index) {
            const xx_7zip_coder *coder = &folder->coders[coder_index];
            if (coder->method == XX_7ZIP_METHOD_AES) {
                ++aes_count;
            } else {
                folder->method = coder->method;
            }
            if (coder->method == XX_7ZIP_METHOD_BCJ2) {
                ++bcj2_count;
                if (coder->num_in != 4U || coder->num_out != 1U || !xx_7zip_branch_properties_supported(coder->properties_size)) all_supported = false;
            } else if (coder->num_in != 1U || coder->num_out != 1U || !xx_7zip_coder_supported(coder)) {
                all_supported = false;
            }
        }
        if (folder->method == XX_7ZIP_METHOD_AES) {
            folder->method = XX_7ZIP_METHOD_COPY;
        }
        /* BCJ2 has four independent packed branches. 7-Zip encrypts each
         * branch separately, so a passworded BCJ2 folder contains four AES
         * coders rather than the single AES coder used by linear chains. */
        folder->supported = all_supported && aes_count <= (bcj2_count == 1U ? 4U : 1U) &&
                            ((bcj2_count == 0U && packed_count == 1U && total_in == (uint64_t)folder->coder_count && total_out == (uint64_t)folder->coder_count) ||
                             (bcj2_count == 1U && packed_count == 4U));
    }
    return true;
}

static bool xx_7zip_parse_unpack_info(xx_7zip_reader *r, xx_7zip_streams *streams)
{
    uint8_t nid, external;
    uint64_t count;
    uint64_t pack_cursor = 0;
    uint64_t i, j;
    if (!xx_7zip_reader_u8(r, &nid) || nid != XX_7ZIP_NID_FOLDER || !xx_7zip_reader_number(r, &count) || count > XX_7ZIP_MAX_ITEMS || !xx_7zip_reader_u8(r, &external) ||
        external != 0)
        return false;
    streams->num_folders = count;
    if (count) {
        streams->folders = (xx_7zip_folder *)xx_7zip_alloc_array(count, sizeof(xx_7zip_folder));
        if (!streams->folders) return false;
    }
    for (i = 0; i < count; ++i) {
        xx_7zip_folder *folder = &streams->folders[i];
        if (!xx_7zip_parse_folder(r, folder)) return false;
        folder->first_pack_index = pack_cursor;
        if (!xx_7zip_u64_add(pack_cursor, folder->packed_count, &pack_cursor)) return false;
    }
    if (pack_cursor != streams->num_packs || !xx_7zip_reader_u8(r, &nid) || nid != XX_7ZIP_NID_CODERS_UNPACK_SIZE) return false;
    for (i = 0; i < count; ++i) {
        xx_7zip_folder *folder = &streams->folders[i];
        uint64_t output_size = 0;
        folder->unpack_sizes = (uint64_t *)xx_7zip_alloc_array(folder->total_out, sizeof(uint64_t));
        if (!folder->unpack_sizes) return false;
        for (j = 0; j < folder->total_out; ++j) {
            uint64_t value;
            if (!xx_7zip_reader_number(r, &value)) return false;
            folder->unpack_sizes[j] = value;
            if (j == folder->final_output_index) output_size = value;
        }
        folder->unpack_size = output_size;
    }
    if (!xx_7zip_reader_u8(r, &nid)) return false;
    if (nid == XX_7ZIP_NID_CRC) {
        uint8_t *defined = NULL;
        uint32_t *crcs = NULL;
        if (count) {
            defined = (uint8_t *)xx_7zip_alloc_array(count, sizeof(uint8_t));
            crcs = (uint32_t *)xx_7zip_alloc_array(count, sizeof(uint32_t));
            if (!defined || !crcs) {
                if (defined) xx_mem_free(defined);
                if (crcs) xx_mem_free(crcs);
                return false;
            }
        }
        if (!xx_7zip_parse_digests(r, count, defined, crcs)) {
            if (defined) xx_mem_free(defined);
            if (crcs) xx_mem_free(crcs);
            return false;
        }
        for (i = 0; i < count; ++i) {
            streams->folders[i].crc_defined = defined[i] != 0;
            streams->folders[i].crc = crcs[i];
        }
        if (defined) xx_mem_free(defined);
        if (crcs) xx_mem_free(crcs);
        if (!xx_7zip_reader_u8(r, &nid)) return false;
    }
    return nid == XX_7ZIP_NID_END;
}

static bool xx_7zip_prepare_substreams(xx_7zip_streams *streams)
{
    uint64_t total = 0;
    uint64_t i;
    if (streams->substreams_ready) return true;
    for (i = 0; i < streams->num_folders; ++i) {
        xx_7zip_folder *folder = &streams->folders[i];
        folder->first_substream = total;
        if (!xx_7zip_u64_add(total, folder->num_substreams, &total) || total > XX_7ZIP_MAX_ITEMS) return false;
    }
    streams->num_substreams = total;
    if (total) {
        streams->sub_sizes = (uint64_t *)xx_7zip_alloc_array(total, sizeof(uint64_t));
        streams->sub_crcs = (uint32_t *)xx_7zip_alloc_array(total, sizeof(uint32_t));
        streams->sub_crc_defined = (uint8_t *)xx_7zip_alloc_array(total, sizeof(uint8_t));
        if (!streams->sub_sizes || !streams->sub_crcs || !streams->sub_crc_defined) return false;
    }
    for (i = 0; i < streams->num_folders; ++i) {
        xx_7zip_folder *folder = &streams->folders[i];
        if (folder->num_substreams == 1) {
            uint64_t index = folder->first_substream;
            streams->sub_sizes[index] = folder->unpack_size;
            if (folder->crc_defined) {
                streams->sub_crc_defined[index] = 1;
                streams->sub_crcs[index] = folder->crc;
            }
        }
    }
    streams->substreams_ready = true;
    return true;
}

static bool xx_7zip_parse_substreams_info(xx_7zip_reader *r, xx_7zip_streams *streams)
{
    uint8_t nid;
    uint64_t i, j;
    bool have_sizes = false;
    bool have_crcs = false;
    for (i = 0; i < streams->num_folders; ++i) streams->folders[i].num_substreams = 1;
    if (!xx_7zip_reader_u8(r, &nid)) return false;
    if (nid == XX_7ZIP_NID_NUM_UNPACK_STREAM) {
        for (i = 0; i < streams->num_folders; ++i) {
            if (!xx_7zip_reader_number(r, &streams->folders[i].num_substreams) || streams->folders[i].num_substreams > XX_7ZIP_MAX_ITEMS) return false;
        }
        if (!xx_7zip_reader_u8(r, &nid)) return false;
    }
    if (!xx_7zip_prepare_substreams(streams)) return false;
    if (nid == XX_7ZIP_NID_SIZE) {
        for (i = 0; i < streams->num_folders; ++i) {
            xx_7zip_folder *folder = &streams->folders[i];
            uint64_t sum = 0;
            for (j = 0; j + 1U < folder->num_substreams; ++j) {
                uint64_t value;
                uint64_t index = folder->first_substream + j;
                if (!xx_7zip_reader_number(r, &value) || !xx_7zip_u64_add(sum, value, &sum) || sum > folder->unpack_size) return false;
                streams->sub_sizes[index] = value;
            }
            if (folder->num_substreams) {
                streams->sub_sizes[folder->first_substream + folder->num_substreams - 1U] = folder->unpack_size - sum;
            }
        }
        have_sizes = true;
        if (!xx_7zip_reader_u8(r, &nid)) return false;
    }
    for (i = 0; i < streams->num_folders; ++i) {
        if (streams->folders[i].num_substreams > 1 && !have_sizes) return false;
    }
    if (nid == XX_7ZIP_NID_CRC) {
        uint64_t digest_count = 0;
        uint8_t *defined = NULL;
        uint32_t *crcs = NULL;
        uint64_t digest_index = 0;
        for (i = 0; i < streams->num_folders; ++i) {
            xx_7zip_folder *folder = &streams->folders[i];
            if (!(folder->num_substreams == 1 && folder->crc_defined) && !xx_7zip_u64_add(digest_count, folder->num_substreams, &digest_count)) return false;
        }
        if (digest_count) {
            defined = (uint8_t *)xx_7zip_alloc_array(digest_count, sizeof(uint8_t));
            crcs = (uint32_t *)xx_7zip_alloc_array(digest_count, sizeof(uint32_t));
            if (!defined || !crcs) {
                if (defined) xx_mem_free(defined);
                if (crcs) xx_mem_free(crcs);
                return false;
            }
        }
        if (!xx_7zip_parse_digests(r, digest_count, defined, crcs)) {
            if (defined) xx_mem_free(defined);
            if (crcs) xx_mem_free(crcs);
            return false;
        }
        for (i = 0; i < streams->num_folders; ++i) {
            xx_7zip_folder *folder = &streams->folders[i];
            if (folder->num_substreams == 1 && folder->crc_defined) continue;
            for (j = 0; j < folder->num_substreams; ++j) {
                uint64_t sub_index = folder->first_substream + j;
                streams->sub_crc_defined[sub_index] = defined[digest_index];
                streams->sub_crcs[sub_index] = crcs[digest_index++];
            }
        }
        if (defined) xx_mem_free(defined);
        if (crcs) xx_mem_free(crcs);
        have_crcs = true;
        if (!xx_7zip_reader_u8(r, &nid)) return false;
    }
    (void)have_crcs;
    return nid == XX_7ZIP_NID_END;
}

static bool xx_7zip_parse_streams_info(xx_7zip_reader *r, xx_7zip_streams *streams)
{
    uint8_t nid;
    bool have_pack = false;
    bool have_unpack = false;
    if (!r || !streams) return false;
    for (;;) {
        if (!xx_7zip_reader_u8(r, &nid)) return false;
        if (nid == XX_7ZIP_NID_END) break;
        if (nid == XX_7ZIP_NID_PACK_INFO) {
            if (have_pack || !xx_7zip_parse_pack_info(r, streams)) return false;
            have_pack = true;
        } else if (nid == XX_7ZIP_NID_UNPACK_INFO) {
            if (have_unpack || !have_pack || !xx_7zip_parse_unpack_info(r, streams)) return false;
            have_unpack = true;
        } else if (nid == XX_7ZIP_NID_SUBSTREAMS_INFO) {
            if (!have_unpack || streams->substreams_ready || !xx_7zip_parse_substreams_info(r, streams)) return false;
        } else {
            return false;
        }
    }
    if (have_pack != have_unpack) return false;
    if (have_unpack && !streams->substreams_ready) {
        uint64_t i;
        for (i = 0; i < streams->num_folders; ++i) streams->folders[i].num_substreams = 1;
        if (!xx_7zip_prepare_substreams(streams)) return false;
    }
    return true;
}

static wchar_t *xx_7zip_read_utf16_name(xx_7zip_reader *r)
{
    size_t scan = r ? r->pos : 0;
    size_t units = 0;
    size_t out_units = 0;
    wchar_t *result;
    size_t i;
    if (!r) return NULL;
    while (scan + 1U < r->size) {
        uint16_t ch = (uint16_t)(r->data[scan] | ((uint16_t)r->data[scan + 1U] << 8));
        scan += 2U;
        if (ch == 0) break;
        ++units;
    }
    if (scan > r->size || scan < 2U || r->data[scan - 2U] != 0 || r->data[scan - 1U] != 0) return NULL;
    result = (wchar_t *)xx_mem_alloc((units + 1U) * sizeof(wchar_t));
    if (!result) return NULL;
    for (i = 0; i < units; ++i) {
        uint16_t ch = (uint16_t)(r->data[r->pos] | ((uint16_t)r->data[r->pos + 1U] << 8));
        r->pos += 2U;
#if WCHAR_MAX > 0xFFFF
        if (ch >= 0xD800U && ch <= 0xDBFFU && i + 1U < units) {
            uint16_t low = (uint16_t)(r->data[r->pos] | ((uint16_t)r->data[r->pos + 1U] << 8));
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                result[out_units++] = (wchar_t)(0x10000U + (((uint32_t)ch - 0xD800U) << 10) + ((uint32_t)low - 0xDC00U));
                r->pos += 2U;
                ++i;
                continue;
            }
        }
#endif
        result[out_units++] = (wchar_t)ch;
    }
    r->pos += 2U;
    result[out_units] = L'\0';
    return result;
}

static bool xx_7zip_parse_bool_vector(xx_7zip_reader *r, uint64_t count, uint8_t *values)
{
    size_t bytes;
    uint8_t *bits;
    uint64_t i;
    if (count == 0) return true;
    bytes = (size_t)((count + 7U) / 8U);
    bits = (uint8_t *)xx_mem_alloc(bytes);
    if (!bits || !xx_7zip_reader_bytes(r, bits, bytes)) {
        if (bits) xx_mem_free(bits);
        return false;
    }
    for (i = 0; i < count; ++i) values[i] = xx_7zip_bit(bits, i) ? 1U : 0U;
    xx_mem_free(bits);
    return true;
}

static bool xx_7zip_parse_optional_vector(xx_7zip_reader *r, uint64_t count, uint8_t *defined)
{
    uint8_t all_defined;
    if (!xx_7zip_reader_u8(r, &all_defined)) return false;
    if (all_defined) {
        if (count) xx_rt_memset(defined, 1, (size_t)count);
        return true;
    }
    return xx_7zip_parse_bool_vector(r, count, defined);
}

/* Decoded AdditionalStreamsInfo folders, one buffer per folder.
 *
 * Archives written by the earliest releases of the format (start header
 * version 0.01 / 0.02) do not compress the header as a whole. They keep it
 * plain and move the bulky file properties out of it instead: a property
 * (names, attributes, times) starts with an "external" byte, and when that
 * byte is 1 a 7z number follows that selects one of the additional streams;
 * the property's values are then read from that decoded stream rather than
 * from the header. Each additional stream is an ordinary folder (LZMA in
 * every archive seen) packed right behind the main streams. */
typedef struct xx_7zip_external_s {
    uint8_t **data;
    size_t *sizes;
    uint64_t count;
} xx_7zip_external;

static void xx_7zip_external_cleanup(xx_7zip_external *external)
{
    uint64_t i;
    if (!external) return;
    if (external->data) {
        for (i = 0; i < external->count; ++i)
            if (external->data[i]) xx_mem_free(external->data[i]);
        xx_mem_free(external->data);
    }
    if (external->sizes) xx_mem_free(external->sizes);
    xx_mem_zero(external, sizeof(*external));
}

/* Read a property's external byte and name the reader that holds its values:
 * the property itself, or `storage` set over the selected additional stream.
 * NULL when the selector is malformed or names a stream that does not exist. */
static xx_7zip_reader *xx_7zip_property_values(xx_7zip_reader *prop, const xx_7zip_external *streams, xx_7zip_reader *storage)
{
    uint8_t external;
    uint64_t index;
    if (!xx_7zip_reader_u8(prop, &external)) return NULL;
    if (external == 0) return prop;
    if (external != 1 || !streams || !xx_7zip_reader_number(prop, &index) || index >= streams->count || !streams->data[index]) return NULL;
    storage->data = streams->data[index];
    storage->size = streams->sizes[index];
    storage->pos = 0;
    return storage;
}

/* An external stream holds exactly one property: nothing may be left over. */
static bool xx_7zip_property_values_done(const xx_7zip_reader *prop, const xx_7zip_reader *values)
{
    return values == prop || values->pos == values->size;
}

static bool xx_7zip_parse_file_times(xx_7zip_reader *prop, xx_7zip_file *files, uint64_t count, const xx_7zip_external *streams)
{
    uint8_t *defined;
    xx_7zip_reader storage;
    xx_7zip_reader *values;
    uint64_t i;
    defined = (uint8_t *)xx_7zip_alloc_array(count, sizeof(uint8_t));
    if (count && !defined) return false;
    if (!xx_7zip_parse_optional_vector(prop, count, defined) || !(values = xx_7zip_property_values(prop, streams, &storage))) {
        if (defined) xx_mem_free(defined);
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (defined[i]) {
            if (!xx_7zip_reader_u64_real(values, &files[i].mtime)) {
                xx_mem_free(defined);
                return false;
            }
            files[i].mtime_defined = true;
        }
    }
    if (defined) xx_mem_free(defined);
    return xx_7zip_property_values_done(prop, values);
}

static bool xx_7zip_parse_file_attributes(xx_7zip_reader *prop, xx_7zip_file *files, uint64_t count, const xx_7zip_external *streams)
{
    uint8_t *defined;
    xx_7zip_reader storage;
    xx_7zip_reader *values;
    uint64_t i;
    defined = (uint8_t *)xx_7zip_alloc_array(count, sizeof(uint8_t));
    if (count && !defined) return false;
    if (!xx_7zip_parse_optional_vector(prop, count, defined) || !(values = xx_7zip_property_values(prop, streams, &storage))) {
        if (defined) xx_mem_free(defined);
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (defined[i]) {
            if (!xx_7zip_reader_u32(values, &files[i].attributes)) {
                xx_mem_free(defined);
                return false;
            }
            files[i].attributes_defined = true;
        }
    }
    if (defined) xx_mem_free(defined);
    return xx_7zip_property_values_done(prop, values);
}

static bool xx_7zip_map_files_to_streams(xx_7zip_private *priv)
{
    xx_7zip_streams *streams = &priv->streams;
    uint64_t sub_index = 0;
    uint64_t i;
    for (i = 0; i < priv->num_files; ++i) {
        xx_7zip_file *file = &priv->files[i];
        if (file->empty_stream) {
            file->folder_index = -1;
            file->size = 0;
            file->is_folder = !file->empty_file && !file->anti;
        } else {
            uint64_t f;
            if (sub_index >= streams->num_substreams) return false;
            file->size = streams->sub_sizes[sub_index];
            file->crc_defined = streams->sub_crc_defined[sub_index] != 0;
            file->crc = streams->sub_crcs[sub_index];
            file->folder_index = -1;
            for (f = 0; f < streams->num_folders; ++f) {
                xx_7zip_folder *folder = &streams->folders[f];
                if (sub_index >= folder->first_substream && sub_index < folder->first_substream + folder->num_substreams) {
                    uint64_t s;
                    file->folder_index = (int64_t)f;
                    file->offset_in_folder = 0;
                    for (s = folder->first_substream; s < sub_index; ++s) {
                        if (!xx_7zip_u64_add(file->offset_in_folder, streams->sub_sizes[s], &file->offset_in_folder)) return false;
                    }
                    break;
                }
            }
            if (file->folder_index < 0) return false;
            ++sub_index;
        }
        if (file->attributes_defined && (file->attributes & 0x10U) != 0) file->is_folder = true;
        if (file->name) {
            size_t len = xx_str_wlen(file->name);
            if (len && (file->name[len - 1U] == L'/' || file->name[len - 1U] == L'\\')) file->is_folder = true;
        }
    }
    return sub_index == streams->num_substreams;
}

static bool xx_7zip_parse_files_info(xx_7zip_reader *r, xx_7zip_private *priv, const xx_7zip_external *streams)
{
    uint64_t count;
    uint8_t *empty_stream = NULL;
    uint8_t *empty_file = NULL;
    uint8_t *anti = NULL;
    uint64_t empty_count = 0;
    uint64_t i;
    bool have_empty_stream = false;
    bool have_empty_file = false;
    bool have_anti = false;
    bool ok = false;
    if (!xx_7zip_reader_number(r, &count) || count > XX_7ZIP_MAX_ITEMS) return false;
    priv->num_files = count;
    if (count) {
        priv->files = (xx_7zip_file *)xx_7zip_alloc_array(count, sizeof(xx_7zip_file));
        empty_stream = (uint8_t *)xx_7zip_alloc_array(count, sizeof(uint8_t));
        if (!priv->files || !empty_stream) goto cleanup;
        for (i = 0; i < count; ++i) priv->files[i].folder_index = -1;
    }
    for (;;) {
        uint8_t type;
        uint64_t property_size;
        xx_7zip_reader prop;
        if (!xx_7zip_reader_u8(r, &type)) goto cleanup;
        if (type == XX_7ZIP_NID_END) break;
        if (!xx_7zip_reader_number(r, &property_size) || !xx_7zip_reader_subreader(r, property_size, &prop)) goto cleanup;
        if (type == XX_7ZIP_NID_NAME) {
            xx_7zip_reader storage;
            xx_7zip_reader *values = xx_7zip_property_values(&prop, streams, &storage);
            if (!values) goto cleanup;
            for (i = 0; i < count; ++i) {
                if (priv->files[i].name || !(priv->files[i].name = xx_7zip_read_utf16_name(values))) goto cleanup;
            }
            if (!xx_7zip_property_values_done(&prop, values)) goto cleanup;
        } else if (type == XX_7ZIP_NID_EMPTY_STREAM) {
            if (have_empty_stream || !xx_7zip_parse_bool_vector(&prop, count, empty_stream)) goto cleanup;
            have_empty_stream = true;
        } else if (type == XX_7ZIP_NID_EMPTY_FILE || type == XX_7ZIP_NID_ANTI) {
            uint8_t **target = type == XX_7ZIP_NID_EMPTY_FILE ? &empty_file : &anti;
            bool *seen = type == XX_7ZIP_NID_EMPTY_FILE ? &have_empty_file : &have_anti;
            if (*seen) goto cleanup;
            *seen = true;
            empty_count = 0;
            for (i = 0; i < count; ++i)
                if (empty_stream[i]) ++empty_count;
            if (empty_count) {
                *target = (uint8_t *)xx_7zip_alloc_array(empty_count, sizeof(uint8_t));
                if (!*target || !xx_7zip_parse_bool_vector(&prop, empty_count, *target)) goto cleanup;
            }
        } else if (type == XX_7ZIP_NID_MTIME) {
            if (!xx_7zip_parse_file_times(&prop, priv->files, count, streams)) goto cleanup;
        } else if (type == XX_7ZIP_NID_WIN_ATTRIBUTES) {
            if (!xx_7zip_parse_file_attributes(&prop, priv->files, count, streams)) goto cleanup;
        } else {
            /* Unknown file properties are length-delimited and safely ignored. */
            prop.pos = prop.size;
        }
        if (prop.pos != prop.size) goto cleanup;
    }
    empty_count = 0;
    for (i = 0; i < count; ++i) {
        priv->files[i].empty_stream = empty_stream && empty_stream[i] != 0;
        if (priv->files[i].empty_stream) {
            priv->files[i].empty_file = empty_file && empty_file[empty_count] != 0;
            priv->files[i].anti = anti && anti[empty_count] != 0;
            ++empty_count;
        }
        if (!priv->files[i].name) {
            priv->files[i].name = xx_str_wdup(L"");
            if (!priv->files[i].name) goto cleanup;
        }
    }
    ok = xx_7zip_map_files_to_streams(priv);

cleanup:
    if (empty_stream) xx_mem_free(empty_stream);
    if (empty_file) xx_mem_free(empty_file);
    if (anti) xx_mem_free(anti);
    return ok;
}

static bool xx_7zip_skip_archive_properties(xx_7zip_reader *r)
{
    for (;;) {
        uint8_t type;
        uint64_t size;
        if (!xx_7zip_reader_u8(r, &type)) return false;
        if (type == XX_7ZIP_NID_END) return true;
        if (!xx_7zip_reader_number(r, &size) || !xx_7zip_reader_skip(r, size)) return false;
    }
}

static bool xx_7zip_decode_folder(Abstractformat *self, const xx_7zip_streams *streams, uint64_t folder_index, xx_io_device *destination, xx_pd_struct *pd,
                                  uint32_t *out_final_crc, bool *out_has_crc);

/* Decode every AdditionalStreamsInfo folder into memory. The folders use the
 * same pack-stream addressing as the main streams and the same coder graph
 * decoder; a folder that cannot be decoded is left NULL and only fails the
 * archive when a file property actually refers to it. The total is held to
 * the header ceiling, since these streams are header material. */
static bool xx_7zip_decode_additional_streams(Abstractformat *self, const xx_7zip_streams *additional, xx_7zip_external *external)
{
    uint64_t i;
    uint64_t total = 0;
    if (!self || !additional || !external || external->data || additional->num_folders > XX_7ZIP_MAX_ITEMS) return false;
    if (additional->num_folders == 0) return true;
    external->data = (uint8_t **)xx_7zip_alloc_array(additional->num_folders, sizeof(uint8_t *));
    external->sizes = (size_t *)xx_7zip_alloc_array(additional->num_folders, sizeof(size_t));
    if (!external->data || !external->sizes) return false;
    external->count = additional->num_folders;
    for (i = 0; i < additional->num_folders; ++i) {
        const xx_7zip_folder *folder = &additional->folders[i];
        xx_7zip_buffer_device destination;
        uint8_t *buffer;
        if (!folder->supported || folder->encrypted || folder->unpack_size == 0 || folder->unpack_size > XX_7ZIP_MAX_HEADER_SIZE ||
            !xx_7zip_u64_add(total, folder->unpack_size, &total) || total > XX_7ZIP_MAX_HEADER_SIZE)
            continue;
        buffer = (uint8_t *)xx_mem_alloc((size_t)folder->unpack_size);
        if (!buffer) return false;
        xx_7zip_buffer_device_init(&destination, buffer, (size_t)folder->unpack_size);
        if (!xx_7zip_decode_folder(self, additional, i, &destination.device, NULL, NULL, NULL)) {
            xx_mem_free(buffer);
            continue;
        }
        external->data[i] = buffer;
        external->sizes[i] = (size_t)folder->unpack_size;
    }
    return true;
}

static bool xx_7zip_parse_plain_header(Abstractformat *self, const uint8_t *data, size_t size, xx_7zip_private *priv)
{
    xx_7zip_reader r;
    xx_7zip_external external;
    uint8_t nid;
    bool have_streams = false;
    bool have_files = false;
    bool ok = false;
    if (!data || !priv) return false;
    xx_mem_zero(&external, sizeof(external));
    r.data = data;
    r.size = size;
    r.pos = 0;
    if (!xx_7zip_reader_u8(&r, &nid) || nid != XX_7ZIP_NID_HEADER) return false;
    for (;;) {
        if (!xx_7zip_reader_u8(&r, &nid)) goto cleanup;
        if (nid == XX_7ZIP_NID_END) break;
        if (nid == XX_7ZIP_NID_ARCHIVE_PROPERTIES) {
            if (!xx_7zip_skip_archive_properties(&r)) goto cleanup;
        } else if (nid == XX_7ZIP_NID_ADDITIONAL_STREAMS_INFO) {
            xx_7zip_streams additional;
            bool decoded;
            xx_mem_zero(&additional, sizeof(additional));
            /* A later block replaces an earlier one, as it always did. */
            xx_7zip_external_cleanup(&external);
            decoded = xx_7zip_parse_streams_info(&r, &additional) && xx_7zip_decode_additional_streams(self, &additional, &external);
            xx_7zip_streams_cleanup(&additional);
            if (!decoded) goto cleanup;
        } else if (nid == XX_7ZIP_NID_MAIN_STREAMS_INFO) {
            if (have_streams || !xx_7zip_parse_streams_info(&r, &priv->streams)) goto cleanup;
            have_streams = true;
        } else if (nid == XX_7ZIP_NID_FILES_INFO) {
            if (have_files || !xx_7zip_parse_files_info(&r, priv, &external)) goto cleanup;
            have_files = true;
        } else {
            goto cleanup;
        }
    }
    if (r.pos != r.size || !have_files) goto cleanup;
    if (!have_streams) {
        /* A valid empty-only archive omits MainStreamsInfo. */
        if (!xx_7zip_prepare_substreams(&priv->streams)) goto cleanup;
    }
    ok = true;
cleanup:
    xx_7zip_external_cleanup(&external);
    return ok;
}

bool xx_7zip_pack_offset(const Abstractformat *self, const xx_7zip_streams *streams, uint64_t pack_index, int64_t *offset, uint64_t *size)
{
    uint64_t relative;
    uint64_t i;
    uint64_t absolute;
    int64_t device_size;
    if (!self || !streams || !offset || !size || pack_index >= streams->num_packs) return false;
    relative = streams->pack_pos;
    for (i = 0; i < pack_index; ++i) {
        if (!xx_7zip_u64_add(relative, streams->pack_sizes[i], &relative)) return false;
    }
    if (!xx_7zip_u64_add((uint64_t)XX_7ZIP_SIGNATURE_HEADER_SIZE, relative, &relative) || self->base_address < 0 ||
        !xx_7zip_u64_add((uint64_t)self->base_address, relative, &absolute) || absolute > (uint64_t)INT64_MAX)
        return false;
    device_size = xx_io_total_size(self->device);
    if (device_size < 0 || absolute > (uint64_t)device_size || streams->pack_sizes[pack_index] > (uint64_t)device_size - absolute ||
        streams->pack_sizes[pack_index] > (uint64_t)INT64_MAX)
        return false;
    *offset = (int64_t)absolute;
    *size = streams->pack_sizes[pack_index];
    return true;
}

/* Decoded folders and graph intermediates use seekable temporary files.
 * This keeps working RAM independent of folder size, including solid archives
 * and BCJ2 branch graphs. Signed 64-bit device offsets remain the IO boundary. */
static bool xx_7zip_copy_range(xx_io_device *source, uint64_t offset, uint64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t buffer[131072];
    bool result = false;
    if (!source || !destination || offset > INT64_MAX || size > INT64_MAX || offset > (uint64_t)INT64_MAX - size || xx_io_seek64(source, (int64_t)offset, SEEK_SET) != 0)
        goto cleanup;
    while (size) {
        size_t count = size > sizeof(buffer) ? sizeof(buffer) : (size_t)size;
        size_t at = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        while (at < count) {
            ssize_t read = xx_io_read(source, buffer + at, count - at);
            if (read <= 0 || (size_t)read > count - at) goto cleanup;
            at += (size_t)read;
        }
        if (xx_io_write(destination, buffer, count) != (ssize_t)count) goto cleanup;
        size -= count;
    }
    result = !(pd && xx_pd_is_stopped(pd));
cleanup:
    xx_mem_zero(buffer, sizeof(buffer));
    return result;
}

static bool xx_7zip_copy_range_crc(xx_io_device *source, uint64_t offset, uint64_t size, xx_io_device *destination, uint32_t *crc_out, xx_pd_struct *pd)
{
    uint8_t buffer[131072];
    uint32_t crc = 0;
    bool result = false;
    if (!source || offset > INT64_MAX || size > INT64_MAX || offset > (uint64_t)INT64_MAX - size || xx_io_seek64(source, (int64_t)offset, SEEK_SET) != 0)
        goto cleanup;
    while (size) {
        size_t count = size > sizeof(buffer) ? sizeof(buffer) : (size_t)size;
        size_t at = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        while (at < count) {
            ssize_t read = xx_io_read(source, buffer + at, count - at);
            if (read <= 0 || (size_t)read > count - at) goto cleanup;
            at += (size_t)read;
        }
        crc = xx_crc32_calc(crc, buffer, count);
        if (destination && xx_io_write(destination, buffer, count) != (ssize_t)count) goto cleanup;
        size -= count;
    }
    if (crc_out) *crc_out = crc;
    result = !(pd && xx_pd_is_stopped(pd));
cleanup:
    xx_mem_zero(buffer, sizeof(buffer));
    return result;
}

static bool xx_7zip_folder_decode_output(xx_7zip_folder_decode *context, uint64_t output_index, xx_io_device **data, uint64_t *size);
static ssize_t xx_7zip_verify_write(xx_io_device *device, const void *buffer, size_t size);

static ssize_t xx_7zip_progress_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_progress_sink *sink = (xx_7zip_progress_sink *)device->priv;
    xx_7zip_folder_decode *context = sink->context;
    ssize_t count = xx_io_write(sink->target, buffer, size);
    if (count > 0) {
        /* File-only CRCs also need this result: a staging file is write-only. */
        if (sink->target == context->destination && context->destination->write != xx_7zip_verify_write) {
            context->final_crc = xx_crc32_calc(context->final_crc, buffer, (size_t)count);
            context->has_final_crc = true;
        }
        if (context->level >= 0) {
            long double done;
            context->written += (uint64_t)count;
            done = (long double)context->written * (long double)context->folder->unpack_size / (long double)context->written_total;
            xx_pd_set_current(context->pd, context->level, done < (long double)context->folder->unpack_size ? (uint64_t)done : context->folder->unpack_size);
        }
    }
    return count;
}

static int64_t xx_7zip_progress_size(xx_io_device *device)
{
    return xx_io_total_size(((xx_7zip_progress_sink *)device->priv)->target);
}

static void xx_7zip_progress_sink_init(xx_7zip_progress_sink *sink, xx_io_device *target, xx_7zip_folder_decode *context)
{
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->context = context;
    sink->device.priv = sink;
    sink->device.write = xx_7zip_progress_write;
    sink->device.total_size = xx_7zip_progress_size;
}

static bool xx_7zip_folder_decode_input(xx_7zip_folder_decode *context, uint64_t input_index, xx_io_device **data, uint64_t *size)
{
    uint64_t bind, packed, packed_size, global_pack;
    int64_t offset;
    if (!context || !data || !size || input_index >= context->folder->total_in) return false;
    for (bind = 0U; bind < context->folder->bind_count; ++bind) {
        if (context->folder->bind_inputs[bind] == input_index) return xx_7zip_folder_decode_output(context, context->folder->bind_outputs[bind], data, size);
    }
    for (packed = 0U; packed < context->folder->packed_count; ++packed)
        if (context->folder->packed_indices[packed] == input_index) break;
    if (packed == context->folder->packed_count || context->folder->first_pack_index > UINT64_MAX - packed) return false;
    if (!context->input_data[input_index]) {
        global_pack = context->folder->first_pack_index + packed;
        if (!xx_7zip_pack_offset(context->format, context->streams, global_pack, &offset, &packed_size) ||
            (context->streams->pack_crc_defined[global_pack] &&
             !xx_crc_verify_device(context->format->device, offset, (int64_t)packed_size, XX_CRC_TYPE_CRC32, context->streams->pack_crcs[global_pack], context->pd)))
            return false;
        context->input_data[input_index] = xx_io_sub_open_ro(context->format->device, offset, (int64_t)packed_size);
        if (!context->input_data[input_index]) return false;
        context->input_sizes[input_index] = packed_size;
    }
    *data = context->input_data[input_index];
    *size = context->input_sizes[input_index];
    return true;
}

static bool xx_7zip_folder_decode_output(xx_7zip_folder_decode *context, uint64_t output_index, xx_io_device **data, uint64_t *size)
{
    uint64_t input_base = 0U, output_base = 0U, expected, input_size = 0U;
    size_t coder_index, i;
    const xx_7zip_coder *coder = NULL;
    xx_io_device *input = NULL, *inputs[4], *output = NULL;
    uint64_t input_sizes[4];
    xx_7zip_progress_sink counted;
    bool result;
    if (!context || !data || !size || output_index >= context->folder->total_out || context->output_busy[output_index]) return false;
    if (context->output_data[output_index]) {
        *data = context->output_data[output_index];
        *size = context->output_sizes[output_index];
        return true;
    }
    for (coder_index = 0U; coder_index < context->folder->coder_count; ++coder_index) {
        const xx_7zip_coder *candidate = &context->folder->coders[coder_index];
        if (output_index >= output_base && output_index < output_base + candidate->num_out) {
            coder = candidate;
            break;
        }
        input_base += candidate->num_in;
        output_base += candidate->num_out;
    }
    if (!coder || coder->num_out != 1U || output_index != output_base || (coder->method == XX_7ZIP_METHOD_BCJ2 ? coder->num_in != 4U : coder->num_in != 1U)) return false;
    expected = context->folder->unpack_sizes[output_index];
    if (expected > INT64_MAX || (context->pd && xx_pd_is_stopped(context->pd))) return false;
    bool bcj2_streamed = false;
    uint64_t u_out_idx = UINT64_MAX;
    const xx_7zip_coder *u_coder = NULL;
    uint64_t u_in_base = 0U, u_out_base = 0U;
    xx_io_device *u_input = NULL;
    uint64_t u_input_size = 0U;
    uint64_t u_expected = 0U;

    context->output_busy[output_index] = 1U;
    if (coder->method == XX_7ZIP_METHOD_BCJ2) {
        uint64_t bind;
        for (bind = 0U; bind < context->folder->bind_count; ++bind) {
            if (context->folder->bind_inputs[bind] == input_base + 0U) {
                u_out_idx = context->folder->bind_outputs[bind];
                break;
            }
        }
        if (u_out_idx != UINT64_MAX && !context->output_data[u_out_idx] && !context->output_busy[u_out_idx]) {
            size_t c_idx;
            for (c_idx = 0U; c_idx < context->folder->coder_count; ++c_idx) {
                const xx_7zip_coder *cand = &context->folder->coders[c_idx];
                if (u_out_idx >= u_out_base && u_out_idx < u_out_base + cand->num_out) {
                    if (cand->num_in == 1U && cand->num_out == 1U) {
                        u_coder = cand;
                    }
                    break;
                }
                u_in_base += cand->num_in;
                u_out_base += cand->num_out;
            }
        }

        for (i = 1U; i < 4U; ++i) {
            if (!xx_7zip_folder_decode_input(context, input_base + i, &inputs[i], &input_sizes[i])) goto failed;
        }

        if (u_coder && xx_7zip_folder_decode_input(context, u_in_base, &u_input, &u_input_size)) {
            u_expected = context->folder->unpack_sizes[u_out_idx];
            bcj2_streamed = true;
        } else {
            if (!xx_7zip_folder_decode_input(context, input_base + 0U, &inputs[0], &input_sizes[0])) goto failed;
        }
    } else if (!xx_7zip_folder_decode_input(context, input_base, &input, &input_size)) goto failed;

    if (output_index == context->folder->final_output_index) {
        output = context->destination;
    } else if (expected <= 512U * 1024U * 1024U) {
        output = xx_7zip_mem_temp_open((size_t)expected);
        if (!output) output = xx_io_temp_open();
    } else {
        output = xx_io_temp_open();
    }
    if (!output) goto failed;
    /* The decoders only write their output. */
    xx_7zip_progress_sink_init(&counted, output, context);

    if (bcj2_streamed) {
        result = xx_7zip_stream_bcj2_streaming(&inputs[1], &input_sizes[1], coder->properties, coder->properties_size,
                                               u_expected, expected, u_input, 0, u_input_size,
                                               u_coder->method, u_coder->properties, u_coder->properties_size,
                                               &counted.device, context->password, context->password_size, context->pd);
        if (result) {
            context->output_data[u_out_idx] = xx_io_mem_open(NULL, 0);
            context->output_sizes[u_out_idx] = u_expected;
            context->output_busy[u_out_idx] = 0U;
            if (context->level >= 0) context->written += u_expected;
        }
    } else if (coder->method == XX_7ZIP_METHOD_BCJ2) {
        result = xx_7zip_stream_bcj2(inputs, input_sizes, coder->properties, coder->properties_size, expected, &counted.device, context->pd);
    } else {
        result = xx_7zip_stream_decode(coder->method, coder->properties, coder->properties_size, input, 0, input_size, expected, &counted.device, context->password,
                                       context->password_size, context->pd);
    }
    if (!result || xx_io_total_size(output) != (int64_t)expected) goto failed;
    context->output_busy[output_index] = 0U;
    context->output_data[output_index] = output;
    context->output_sizes[output_index] = expected;
    *data = output;
    *size = expected;
    return true;
failed:
    if (output && output != context->destination) xx_io_close(output);
    context->output_busy[output_index] = 0U;
    return false;
}

static void xx_7zip_folder_decode_cleanup(xx_7zip_folder_decode *context)
{
    uint64_t i;
    if (!context || !context->folder) return;
    for (i = 0U; i < context->folder->total_in; ++i)
        if (context->input_data && context->input_data[i]) xx_io_close(context->input_data[i]);
    for (i = 0U; i < context->folder->total_out; ++i)
        if (context->output_data && context->output_data[i] && context->output_data[i] != context->destination) xx_io_close(context->output_data[i]);
    xx_mem_free(context->input_data);
    xx_mem_free(context->input_sizes);
    xx_mem_free(context->output_data);
    xx_mem_free(context->output_sizes);
    xx_mem_free(context->output_busy);
    if (context->password) {
        xx_mem_zero(context->password, context->password_size);
        xx_mem_free(context->password);
    }
    xx_mem_zero(context, sizeof(*context));
}

/* Test of a folder: its decoded output is checked as it is written, against
 * the folder CRC and the CRC of each member file it holds, and not kept, so
 * a folder of any size is tested without a copy of it in the 256 MiB that
 * Test may use.  Its files' substreams follow one another in file order. */
typedef struct xx_7zip_verify_sink_s {
    xx_io_device device;
    const xx_7zip_private *priv;
    uint8_t *verdicts;
    uint64_t *members; /**< The folder's files, in offset order. */
    uint64_t member_count;
    uint64_t current;
    uint64_t position;
    uint32_t folder_crc;
    uint32_t member_crc;
    bool folder_crc_defined;
} xx_7zip_verify_sink;

static ssize_t xx_7zip_verify_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_verify_sink *sink = (xx_7zip_verify_sink *)device->priv;
    const uint8_t *data = (const uint8_t *)buffer;
    uint64_t position;
    size_t left = size;
    if (!sink || (!buffer && size) || size > (size_t)PTRDIFF_MAX) return -1;
    position = sink->position;
    if (sink->folder_crc_defined) {
        sink->folder_crc = xx_crc32_calc(sink->folder_crc, data, size);
    }
    while (sink->current < sink->member_count) {
        const xx_7zip_file *file = &sink->priv->files[sink->members[sink->current]];
        uint64_t end = file->offset_in_folder + file->size;
        size_t take = (uint64_t)left < end - position ? left : (size_t)(end - position);
        if (position < file->offset_in_folder) return -1;
        sink->member_crc = xx_crc32_calc(sink->member_crc, data, take);
        data += take;
        left -= take;
        position += take;
        if (position < end) break;
        sink->verdicts[sink->members[sink->current]] = !file->crc_defined || sink->member_crc == file->crc ? 1U : 2U;
        sink->member_crc = 0U;
        ++sink->current;
    }
    sink->position += size;
    return (ssize_t)size;
}

static int64_t xx_7zip_verify_size(xx_io_device *device)
{
    xx_7zip_verify_sink *sink = (xx_7zip_verify_sink *)device->priv;
    return sink && sink->position <= (uint64_t)INT64_MAX ? (int64_t)sink->position : -1;
}

static ssize_t xx_7zip_extract_sink_write(xx_io_device *device, const void *buffer, size_t size);

static bool xx_7zip_decode_folder(Abstractformat *self, const xx_7zip_streams *streams, uint64_t folder_index, xx_io_device *destination, xx_pd_struct *pd,
                                  uint32_t *out_final_crc, bool *out_has_crc);

/* The verdicts on the files of `folder_index`, in cache->verdicts: false
 * when the folder cannot be checked this way (its files' substreams are not
 * in order) or memory runs out, with nothing decoded. */
static bool xx_7zip_verify_folder(Abstractformat *self, xx_7zip_private *priv, xx_7zip_extract_cache *cache, uint64_t folder_index, xx_pd_struct *pd)
{
    xx_7zip_verify_sink sink;
    uint64_t i, count = 0U, next = 0U;
    bool decoded;
    if (!cache->verdicts && !(cache->verdicts = (uint8_t *)xx_mem_calloc(priv->num_files ? (size_t)priv->num_files : 1U, 1U))) return false;
    for (i = 0U; i < priv->num_files; ++i)
        if (!priv->files[i].empty_stream && priv->files[i].folder_index == (int64_t)folder_index) ++count;
    xx_mem_zero(&sink, sizeof(sink));
    sink.members = (uint64_t *)xx_7zip_alloc_array(count ? count : 1U, sizeof(uint64_t));
    if (!sink.members) return false;
    for (i = 0U; i < priv->num_files; ++i) {
        const xx_7zip_file *file = &priv->files[i];
        if (file->empty_stream || file->folder_index != (int64_t)folder_index) continue;
        if (file->offset_in_folder != next || !xx_7zip_u64_add(next, file->size, &next)) {
            xx_mem_free(sink.members);
            return false;
        }
        sink.members[sink.member_count++] = i;
        cache->verdicts[i] = 0U;
    }
    xx_7zip_extract_cache_clear(cache);
    sink.priv = priv;
    sink.verdicts = cache->verdicts;
    sink.folder_crc_defined = priv->streams.folders[folder_index].crc_defined;
    sink.device.priv = &sink;
    sink.device.write = xx_7zip_verify_write;
    sink.device.total_size = xx_7zip_verify_size;
    decoded = xx_7zip_decode_folder(self, &priv->streams, folder_index, &sink.device, pd, NULL, NULL);
    for (i = 0U; i < sink.member_count; ++i) {
        uint8_t *verdict = &cache->verdicts[sink.members[i]];
        /* A file of no bytes at the end is reached by no write. */
        if (*verdict == 0U && decoded && priv->files[sink.members[i]].size == 0U && sink.position == next) *verdict = 1U;
        if (!decoded || *verdict == 0U) *verdict = 2U;
    }
    xx_mem_free(sink.members);
    cache->verified_folder = folder_index;
    return true;
}

static bool xx_7zip_decode_folder(Abstractformat *self, const xx_7zip_streams *streams, uint64_t folder_index, xx_io_device *destination, xx_pd_struct *pd,
                                  uint32_t *out_final_crc, bool *out_has_crc)
{
    const xx_7zip_folder *folder;
    xx_7zip_folder_decode context;
    xx_io_device *output;
    uint64_t output_size, i;
    int level = -1;
    bool result = false;
    if (!self || !self->device || !streams || !destination || folder_index >= streams->num_folders) return false;
    folder = &streams->folders[folder_index];
    if (!folder->supported || !folder->packed_indices || !folder->unpack_sizes || !folder->coder_count || folder->coder_count > XX_7ZIP_MAX_CODERS ||
        folder->unpack_size > INT64_MAX)
        return false;
    xx_mem_zero(&context, sizeof(context));
    context.format = self;
    context.streams = streams;
    context.folder = folder;
    context.pd = pd;
    context.destination = destination;
    /* A folder is decoded whole before any of its files (Test checks them
     * as it goes): its bytes show the progress meanwhile. A filter's input
     * is another coder's output, so every coder's bytes count. */
    for (i = 0U; i < folder->total_out; ++i)
        if (!xx_7zip_u64_add(context.written_total, folder->unpack_sizes[i], &context.written_total)) context.written_total = UINT64_MAX;
    if (pd && folder->unpack_size && context.written_total)
        level = xx_pd_enter_level(pd, folder->unpack_size, folder->num_substreams > 1U ? "Decoding solid block" : "Decoding 7z member");
    context.level = level;
    context.input_data = (xx_io_device **)xx_7zip_alloc_array(folder->total_in, sizeof(xx_io_device *));
    context.input_sizes = (uint64_t *)xx_7zip_alloc_array(folder->total_in, sizeof(uint64_t));
    context.output_data = (xx_io_device **)xx_7zip_alloc_array(folder->total_out, sizeof(xx_io_device *));
    context.output_sizes = (uint64_t *)xx_7zip_alloc_array(folder->total_out, sizeof(uint64_t));
    context.output_busy = (uint8_t *)xx_7zip_alloc_array(folder->total_out, sizeof(uint8_t));
    if (!context.input_data || !context.input_sizes || !context.output_data || !context.output_sizes || !context.output_busy ||
        (folder->encrypted && !xx_7zip_get_password_utf16le(self, &context.password, &context.password_size)))
        goto cleanup;
    if (!xx_7zip_folder_decode_output(&context, folder->final_output_index, &output, &output_size) || output_size != folder->unpack_size) goto cleanup;
    /* Every declared coder must belong to the decoded graph; disconnected
     * cycles must not disappear just because the final stream is reachable. */
    for (i = 0U; i < folder->total_out; ++i)
        if (!context.output_data[i]) goto cleanup;
    if (folder->unpack_size == 0) {
        context.has_final_crc = true;
        context.final_crc = 0;
    }
    if (folder->crc_defined &&
        (destination->write == xx_7zip_verify_write ? ((const xx_7zip_verify_sink *)destination->priv)->folder_crc != folder->crc
         : context.has_final_crc ? context.final_crc != folder->crc
         : !xx_crc_verify_device(destination, 0, (int64_t)folder->unpack_size, XX_CRC_TYPE_CRC32, folder->crc, pd)))
        goto cleanup;
    result = !(pd && xx_pd_is_stopped(pd));
    if (result) {
        if (out_final_crc) *out_final_crc = context.final_crc;
        if (out_has_crc) *out_has_crc = context.has_final_crc;
    }
cleanup:
    xx_7zip_folder_decode_cleanup(&context);
    if (level >= 0) xx_pd_leave_level(pd, level);
    return result;
}

static bool xx_7zip_decode_encoded_header(Abstractformat *self, const uint8_t *data, size_t size, uint8_t **decoded, size_t *decoded_size)
{
    xx_7zip_reader r;
    xx_7zip_streams streams;
    xx_7zip_buffer_device destination;
    uint8_t nid;
    uint8_t *buffer = NULL;
    bool result = false;
    if (!self || !data || !decoded || !decoded_size) return false;
    *decoded = NULL;
    *decoded_size = 0;
    xx_mem_zero(&streams, sizeof(streams));
    r.data = data;
    r.size = size;
    r.pos = 0;
    if (!xx_7zip_reader_u8(&r, &nid) || nid != XX_7ZIP_NID_ENCODED_HEADER || !xx_7zip_parse_streams_info(&r, &streams) || r.pos != r.size || streams.num_folders != 1 ||
        streams.num_substreams != 1 || streams.folders[0].unpack_size == 0 || streams.folders[0].unpack_size > XX_7ZIP_MAX_HEADER_SIZE ||
        streams.folders[0].unpack_size > SIZE_MAX)
        goto cleanup;
    if (streams.folders[0].encrypted) {
        uint64_t i;
        int64_t total = xx_io_total_size(self->device);
        /* The encoded header has already passed its outer CRC. Authenticate
         * the complete stream grammar and bounded pack ranges before
         * exposing encryption when a missing password prevents decoding. */
        if (total < 0 || !streams.folders[0].supported) goto cleanup;
        for (i = 0; i < streams.num_packs; ++i) {
            int64_t offset;
            uint64_t bytes;
            if (!xx_7zip_pack_offset(self, &streams, i, &offset, &bytes) || offset < 0 || offset > total || bytes > (uint64_t)(total - offset)) goto cleanup;
        }
        self->is_crypted = true;
    }
    buffer = (uint8_t *)xx_mem_alloc((size_t)streams.folders[0].unpack_size);
    if (!buffer) goto cleanup;
    xx_7zip_buffer_device_init(&destination, buffer, (size_t)streams.folders[0].unpack_size);
    if (!xx_7zip_decode_folder(self, &streams, 0, &destination.device, NULL, NULL, NULL)) goto cleanup;
    if (streams.sub_crc_defined[0] && xx_crc32(XX_CRC_TYPE_CRC32, buffer, (size_t)streams.sub_sizes[0]) != streams.sub_crcs[0]) goto cleanup;
    *decoded = buffer;
    *decoded_size = (size_t)streams.folders[0].unpack_size;
    buffer = NULL;
    result = true;

cleanup:
    if (buffer) xx_mem_free(buffer);
    xx_7zip_streams_cleanup(&streams);
    return result;
}

static bool xx_7zip_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0;
    if (!device || (!buffer && size) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, bytes + done, size - done);
        if (got <= 0) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_7zip_read_start_header(Abstractformat *self, uint8_t header[XX_7ZIP_SIGNATURE_HEADER_SIZE], int64_t *next_offset, uint64_t *next_size, uint32_t *next_crc)
{
    int64_t device_size;
    uint64_t relative_offset;
    uint64_t absolute_offset;
    uint64_t absolute_end;
    uint32_t start_crc;
    if (!self || !self->device || !header || self->base_address < 0) return false;
    device_size = xx_io_total_size(self->device);
    if (device_size < 0 || (uint64_t)self->base_address > (uint64_t)device_size || (uint64_t)device_size - (uint64_t)self->base_address < XX_7ZIP_SIGNATURE_HEADER_SIZE ||
        !xx_7zip_read_exact_at(self->device, self->base_address, header, XX_7ZIP_SIGNATURE_HEADER_SIZE) ||
        xx_rt_memcmp(header, XX_7ZIP_SIGNATURE, XX_7ZIP_SIGNATURE_SIZE) != 0 || header[6] != 0)
        return false;
    start_crc = xx_data_get_u32(header + 8, 4, 0, false);
    if (xx_crc32(XX_CRC_TYPE_CRC32, header + 12, 20U) != start_crc) return false;
    relative_offset = xx_data_get_u64(header + 12, 8, 0, false);
    *next_size = xx_data_get_u64(header + 20, 8, 0, false);
    *next_crc = xx_data_get_u32(header + 28, 4, 0, false);
    if (*next_size > XX_7ZIP_MAX_HEADER_SIZE || (*next_size == 0 && (relative_offset != 0 || *next_crc != 0)) ||
        !xx_7zip_u64_add(XX_7ZIP_SIGNATURE_HEADER_SIZE, relative_offset, &absolute_offset) ||
        !xx_7zip_u64_add((uint64_t)self->base_address, absolute_offset, &absolute_offset) || !xx_7zip_u64_add(absolute_offset, *next_size, &absolute_end) ||
        absolute_offset > INT64_MAX || absolute_end > (uint64_t)device_size)
        return false;
    *next_offset = (int64_t)absolute_offset;
    return true;
}

void xx_7zip_init(xx_7zip *archive, xx_io_device *dev, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, dev, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_7ZIP;
    archive->format.handle_type = XX_HANDLE_TYPE_7ZIP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-7z-compressed");
    xx_format_set_extension(&archive->format, "7z");
    archive->format.check_is_valid = xx_7zip_check_is_valid;
    archive->format.handle_base_info = xx_7zip_handle_base_info;
    archive->format.get_format_size = xx_7zip_get_format_size;
    archive->format.get_number_of_archive_records = xx_7zip_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_7zip_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_7zip_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_7zip_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_7zip_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_7zip_free_archive_records_reading;
    archive->format.create_archive_records_writing = xx_7zip_create_archive_records_writing;
    archive->format.pack_archive_record = xx_7zip_pack_archive_record;
    archive->format.finalize_archive_records_writing = xx_7zip_finalize_archive_records_writing;
    archive->format.free_archive_records_writing = xx_7zip_free_archive_records_writing;
    archive->format.destroy = xx_7zip_vtable_destroy;
    archive->next_header_offset = -1;
}

xx_7zip *xx_7zip_create(xx_io_device *dev, int64_t base_address)
{
    xx_7zip *archive = (xx_7zip *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_7zip_init(archive, dev, base_address);
    return archive;
}

static bool xx_7zip_get_password_utf16le(const Abstractformat *self, uint8_t **password, size_t *password_size)
{
    const xx_var *value;
    wchar_t *wide = NULL;
    uint8_t *encoded = NULL;
    size_t units = 0U;
    size_t length = 0U;
    size_t index;
    size_t out = 0U;
    if (!self || !password || !password_size) return false;
    *password = NULL;
    *password_size = 0U;
    value = xx_format_find_extra_parameter(self, XX_META_ID_OPT_PASSWORD);
    if (!value) return false;
    if (value->type == XX_VAR_TYPE_STRING || value->type == XX_VAR_TYPE_STRING_VIEW || value->type == XX_VAR_TYPE_BYTES || value->type == XX_VAR_TYPE_BYTES_VIEW) {
        const uint8_t *source;
        size_t source_size;
        char *utf8;
        if (value->type == XX_VAR_TYPE_BYTES || value->type == XX_VAR_TYPE_BYTES_VIEW) {
            source = (const uint8_t *)xx_var_get_bytes(value, &source_size);
        } else {
            source = (const uint8_t *)xx_var_get_str(value);
            source_size = value->val.str.len;
        }
        if ((!source && source_size != 0U) || source_size == SIZE_MAX || (source_size != 0U && xx_rt_memchr(source, 0, source_size))) {
            return false;
        }
        utf8 = (char *)xx_mem_alloc(source_size + 1U);
        if (!utf8) return false;
        if (source_size != 0U) xx_mem_copy(utf8, source, source_size);
        utf8[source_size] = '\0';
        wide = xx_str_utf8_to_unicode(utf8);
        xx_mem_zero(utf8, source_size);
        xx_mem_free(utf8);
        if (!wide) return false;
        length = xx_str_wlen(wide);
    } else if (value->type == XX_VAR_TYPE_WSTRING || value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        const wchar_t *source = xx_var_get_wstr(value);
        length = value->val.wstr.len;
        if ((!source && length != 0U) || length > (SIZE_MAX / sizeof(wchar_t)) - 1U) {
            return false;
        }
        wide = (wchar_t *)xx_mem_alloc((length + 1U) * sizeof(wchar_t));
        if (!wide) return false;
        if (length != 0U) {
            xx_mem_copy(wide, source, length * sizeof(wchar_t));
        }
        wide[length] = L'\0';
    } else {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (wide[index] == L'\0') goto fail;
#if WCHAR_MAX <= 0xFFFFU
        {
            if (units == SIZE_MAX) goto fail;
            ++units;
        }
#else
        uint32_t code_point = (uint32_t)wide[index];
        if (code_point <= 0xFFFFU) {
            if (code_point >= 0xD800U && code_point <= 0xDFFFU) goto fail;
            if (units == SIZE_MAX) goto fail;
            ++units;
        } else if (code_point <= 0x10FFFFU) {
            if (units > SIZE_MAX - 2U) goto fail;
            units += 2U;
        } else {
            goto fail;
        }
#endif
    }
    if (units > SIZE_MAX / 2U) goto fail;
    if (units > 0U) {
        encoded = (uint8_t *)xx_mem_alloc(units * 2U);
        if (!encoded) goto fail;
    }
    for (index = 0U; index < length; ++index) {
        uint32_t code_point = (uint32_t)wide[index];
#if WCHAR_MAX > 0xFFFFU
        if (code_point > 0xFFFFU) {
            uint32_t adjusted = code_point - 0x10000U;
            uint16_t high = (uint16_t)(0xD800U + (adjusted >> 10U));
            uint16_t low = (uint16_t)(0xDC00U + (adjusted & 0x3FFU));
            encoded[out++] = (uint8_t)high;
            encoded[out++] = (uint8_t)(high >> 8U);
            encoded[out++] = (uint8_t)low;
            encoded[out++] = (uint8_t)(low >> 8U);
        } else {
#endif
            uint16_t unit = (uint16_t)code_point;
            encoded[out++] = (uint8_t)unit;
            encoded[out++] = (uint8_t)(unit >> 8U);
#if WCHAR_MAX > 0xFFFFU
        }
#endif
    }
    xx_mem_zero(wide, (length + 1U) * sizeof(wchar_t));
    xx_str_wfree(wide);
    *password = encoded;
    *password_size = units * 2U;
    return true;

fail:
    if (encoded) {
        xx_mem_zero(encoded, units * 2U);
        xx_mem_free(encoded);
    }
    if (wide) {
        xx_mem_zero(wide, (length + 1U) * sizeof(wchar_t));
        xx_str_wfree(wide);
    }
    return false;
}

void xx_7zip_destroy(xx_7zip *archive)
{
    if (!archive) return;
    if (archive->internal) {
        xx_7zip_private_free((xx_7zip_private *)archive->internal);
        archive->internal = NULL;
    }
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void xx_7zip_vtable_destroy(Abstractformat *self)
{
    if (self) xx_7zip_destroy((xx_7zip *)self);
}

void xx_7zip_free(xx_7zip *archive)
{
    if (!archive) return;
    xx_7zip_destroy(archive);
    xx_mem_free(archive);
}

bool xx_7zip_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t header[XX_7ZIP_SIGNATURE_HEADER_SIZE];
    int64_t next_offset;
    uint64_t next_size;
    uint32_t next_crc;
    uint64_t calculated;
    (void)pd;
    if (!xx_7zip_read_start_header(self, header, &next_offset, &next_size, &next_crc)) return false;
    if (next_size == 0) return next_crc == 0;
    return xx_crc_calculate_device_by_type(self->device, next_offset, (int64_t)next_size, XX_CRC_TYPE_CRC32, NULL, &calculated) && (uint32_t)calculated == next_crc;
}

bool xx_7zip_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_7zip *archive;
    xx_7zip_private *priv = NULL;
    uint8_t signature_header[XX_7ZIP_SIGNATURE_HEADER_SIZE];
    uint8_t *stored_header = NULL;
    uint8_t *decoded_header = NULL;
    const uint8_t *plain_header = NULL;
    size_t plain_size = 0;
    uint64_t next_size;
    uint32_t next_crc;
    int64_t next_offset;
    uint64_t calculated;
    uint64_t physical_end;
    uint64_t i;
    bool ok = false;
    (void)pd;
    if (!self || !self->device) return false;
    archive = (xx_7zip *)self;
    if (archive->internal) {
        xx_7zip_private_free((xx_7zip_private *)archive->internal);
        archive->internal = NULL;
    }
    self->is_valid = false;
    self->is_crypted = false;
    if (!xx_7zip_read_start_header(self, signature_header, &next_offset, &next_size, &next_crc) || next_size > SIZE_MAX) return false;
    if (next_size == 0) {
        if (next_crc != 0) return false;
    } else if (!xx_crc_calculate_device_by_type(self->device, next_offset, (int64_t)next_size, XX_CRC_TYPE_CRC32, NULL, &calculated) ||
               (uint32_t)calculated != next_crc) {
        return false;
    }
    priv = (xx_7zip_private *)xx_mem_calloc(1, sizeof(*priv));
    if (!priv) goto cleanup;
    archive->is_header_encoded = false;
    if (next_size != 0) {
        stored_header = (uint8_t *)xx_mem_alloc((size_t)next_size);
        if (!stored_header || !xx_7zip_read_exact_at(self->device, next_offset, stored_header, (size_t)next_size)) goto cleanup;
        archive->is_header_encoded = stored_header[0] == XX_7ZIP_NID_ENCODED_HEADER;
        if (archive->is_header_encoded) {
            if (!xx_7zip_decode_encoded_header(self, stored_header, (size_t)next_size, &decoded_header, &plain_size)) goto cleanup;
            plain_header = decoded_header;
        } else {
            plain_header = stored_header;
            plain_size = (size_t)next_size;
        }
        if (!xx_7zip_parse_plain_header(self, plain_header, plain_size, priv)) goto cleanup;
    }

    physical_end = (uint64_t)next_offset + next_size;
    for (i = 0; i < priv->streams.num_packs; ++i) {
        int64_t pack_offset;
        uint64_t pack_size;
        uint64_t pack_end;
        if (!xx_7zip_pack_offset(self, &priv->streams, i, &pack_offset, &pack_size) || !xx_7zip_u64_add((uint64_t)pack_offset, pack_size, &pack_end)) goto cleanup;
        if (pack_end > physical_end) physical_end = pack_end;
    }
    if (physical_end > (uint64_t)INT64_MAX) goto cleanup;
    priv->physical_data_end = (int64_t)physical_end;
    archive->number_of_records = priv->num_files;
    archive->next_header_offset = next_offset;
    archive->next_header_size = next_size;
    archive->next_header_crc = next_crc;
    archive->internal = priv;
    priv = NULL;
    self->format_size = (int64_t)(physical_end - (uint64_t)self->base_address);
    {
        int64_t device_size = xx_io_total_size(self->device);
        if (device_size > (int64_t)physical_end) {
            self->overlay_offset = (int64_t)physical_end;
            self->overlay_size = device_size - (int64_t)physical_end;
        } else {
            self->overlay_offset = -1;
            self->overlay_size = 0;
        }
    }
    self->file_type = XX_FILE_TYPE_7ZIP;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->number_of_archive_records = archive->number_of_records;
    for (i = 0; i < ((xx_7zip_private *)archive->internal)->streams.num_folders; ++i) {
        if (((xx_7zip_private *)archive->internal)->streams.folders[i].encrypted) {
            self->is_crypted = true;
            break;
        }
    }
    self->is_valid = true;
    self->base_info_handled = true;
    ok = true;

cleanup:
    if (stored_header) xx_mem_free(stored_header);
    if (decoded_header) xx_mem_free(decoded_header);
    if (priv) xx_7zip_private_free(priv);
    return ok;
}

int64_t xx_7zip_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self) return -1;
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) return -1;
    return self->format_size;
}

uint64_t xx_7zip_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self) return 0;
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) return 0;
    return ((xx_7zip *)self)->number_of_records;
}

uint64_t xx_7zip_get_number_of_records(const xx_7zip *archive)
{
    return archive ? archive->number_of_records : 0;
}

int64_t xx_7zip_get_next_header_offset(const xx_7zip *archive)
{
    return archive ? archive->next_header_offset : -1;
}

uint64_t xx_7zip_get_next_header_size(const xx_7zip *archive)
{
    return archive ? archive->next_header_size : 0;
}

bool xx_7zip_is_header_encoded(const xx_7zip *archive)
{
    return archive ? archive->is_header_encoded : false;
}

static bool xx_7zip_copy_options(xx_archive_record_state *state, const xx_list_s *options)
{
    size_t i;
    if (!state || !options) return true;
    for (i = 0; i < options->count; ++i) {
        const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (!source) continue;
        xx_meta_init(&copy, source->meta_id);
        if (!xx_var_copy(&copy.var, &source->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_7zip_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

static bool xx_7zip_apply_password_option(Abstractformat *format, const xx_list_s *options)
{
    const xx_var *value = xx_7zip_find_option(options, XX_META_ID_OPT_PASSWORD);
    if (!value) return true;
    return xx_format_set_extra_parameter(format, XX_META_ID_OPT_PASSWORD, value);
}

static bool xx_7zip_folder_pack_region(Abstractformat *self, const xx_7zip_streams *streams, const xx_7zip_folder *folder, int64_t *offset, uint64_t *total_size)
{
    uint64_t i;
    uint64_t sum = 0;
    uint64_t ignored;
    if (!folder || folder->packed_count == 0 || !xx_7zip_pack_offset(self, streams, folder->first_pack_index, offset, &ignored)) return false;
    for (i = 0; i < folder->packed_count; ++i) {
        uint64_t index = folder->first_pack_index + i;
        if (index >= streams->num_packs || !xx_7zip_u64_add(sum, streams->pack_sizes[index], &sum)) return false;
    }
    *total_size = sum;
    return true;
}

static bool xx_7zip_populate_archive_record(Abstractformat *self, uint64_t index, xx_archive_record *record)
{
    xx_7zip *archive = (xx_7zip *)self;
    xx_7zip_private *priv = (xx_7zip_private *)archive->internal;
    const xx_7zip_file *file;
    uint64_t compressed_size = 0;
    uint64_t method = XX_7ZIP_METHOD_COPY;
    bool encrypted = false;
    int64_t data_offset = -1;
    if (!priv || index >= priv->num_files || !record) return false;
    file = &priv->files[index];
    xx_archive_record_init(record);
    record->header_offset = archive->next_header_offset;
    record->header_size = archive->next_header_size <= INT64_MAX ? (int64_t)archive->next_header_size : 0;
    if (file->folder_index >= 0 && (uint64_t)file->folder_index < priv->streams.num_folders) {
        const xx_7zip_folder *folder = &priv->streams.folders[file->folder_index];
        method = folder->method;
        encrypted = folder->encrypted;
        if (!xx_7zip_folder_pack_region(self, &priv->streams, folder, &data_offset, &compressed_size)) {
            xx_archive_record_cleanup(record);
            return false;
        }
    }
    record->data_offset = data_offset;
    record->compressed_size = compressed_size <= INT64_MAX ? (int64_t)compressed_size : -1;
    if (!xx_archive_record_set_original_name_w(record, file->name ? file->name : L"") ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, file->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, compressed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, method) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, file->is_folder) || !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, encrypted)) {
        xx_archive_record_cleanup(record);
        return false;
    }
    if (file->crc_defined && !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, file->crc)) {
        xx_archive_record_cleanup(record);
        return false;
    }
    if (file->attributes_defined && (!xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, file->attributes) ||
                                     !xx_archive_record_set_meta_u64(record, XX_META_ID_EXTERNAL_ATTRS, file->attributes))) {
        xx_archive_record_cleanup(record);
        return false;
    }
    if (file->mtime_defined && !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, file->mtime)) {
        xx_archive_record_cleanup(record);
        return false;
    }
    return true;
}

xx_archive_record_state *xx_7zip_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_7zip_extract_cache *cache;
    xx_7zip *archive;
    if (!self || !self->device) return NULL;
    archive = (xx_7zip *)self;
    if (!xx_7zip_apply_password_option(self, options)) return NULL;
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) return NULL;
    if (!self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    cache = (xx_7zip_extract_cache *)xx_mem_calloc(1U, sizeof(*cache));
    if (!cache) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    cache->folder_index = UINT64_MAX;
    cache->verified_folder = UINT64_MAX;
    cache->extracted_folder = UINT64_MAX;
    state->internal_state = cache;
    state->free_internal = xx_7zip_extract_cache_free;
    if (!xx_7zip_copy_options(state, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->total_records = archive->number_of_records <= INT64_MAX ? (int64_t)archive->number_of_records : INT64_MAX;
    if (archive->number_of_records && xx_7zip_populate_archive_record(self, 0, &state->current_record)) {
        state->current_index = 0;
        state->has_record = true;
    } else if (archive->number_of_records) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

const xx_archive_record *xx_7zip_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_7zip_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_7zip *archive;
    uint64_t next;
    if (!self || !state || state->format != self || !state->has_record) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    archive = (xx_7zip *)self;
    next = (uint64_t)state->current_index + 1U;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    if (next >= archive->number_of_records) return false;
    if (!xx_7zip_populate_archive_record(self, next, &state->current_record)) return false;
    state->current_index = (int64_t)next;
    state->has_record = true;
    return true;
}

/* ':' names a drive or a file's stream on Windows only; elsewhere it is an
 * ordinary character of a member name ("9:00-notes.txt"). */
static bool xx_7zip_validate_member_name(const wchar_t *name)
{
    const wchar_t *segment;
    const wchar_t *p;
    if (!name || !name[0] || name[0] == L'/' || name[0] == L'\\') return false;
    segment = name;
    for (p = name;; ++p) {
#ifdef _WIN32
        if (*p == L':') return false;
#endif
        if (*p == L'/' || *p == L'\\' || *p == L'\0') {
            size_t length = (size_t)(p - segment);
            if (length == 2U && segment[0] == L'.' && segment[1] == L'.') return false;
            if (*p == L'\0') break;
            segment = p + 1;
        }
    }
    return true;
}

static wchar_t *xx_7zip_normalized_name(const wchar_t *name)
{
    wchar_t *copy;
    size_t i;
    const wchar_t separator = xx_io_platform_wseparator();
    if (!xx_7zip_validate_member_name(name)) return NULL;
    copy = xx_str_wdup(name);
    if (!copy) return NULL;
    for (i = 0; copy[i]; ++i) {
        if (copy[i] == L'/' || copy[i] == L'\\') {
            copy[i] = separator;
        }
    }
    return copy;
}

typedef struct xx_7zip_slice_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t limit;
    uint64_t written;
    uint32_t crc;
    bool error;
} xx_7zip_slice_sink;

static ssize_t xx_7zip_slice_sink_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_slice_sink *sink = (xx_7zip_slice_sink *)device->priv;
    if (!sink || sink->error) return -1;
    if (sink->written < sink->limit) {
        uint64_t remaining = sink->limit - sink->written;
        size_t take = (uint64_t)size < remaining ? size : (size_t)remaining;
        if (sink->target) {
            ssize_t w = xx_io_write(sink->target, buffer, take);
            if (w != (ssize_t)take) {
                sink->error = true;
                return -1;
            }
        }
        sink->crc = xx_crc32_calc(sink->crc, buffer, take);
        sink->written += take;
    }
    return (ssize_t)size;
}

static int64_t xx_7zip_slice_sink_size(xx_io_device *device)
{
    xx_7zip_slice_sink *sink = (xx_7zip_slice_sink *)device->priv;
    return sink && sink->written <= (uint64_t)INT64_MAX ? (int64_t)sink->written : -1;
}

static bool xx_7zip_extract_file_to_device(Abstractformat *self, const xx_7zip_file *file, xx_7zip_extract_cache *cache, xx_io_device *output, xx_pd_struct *pd)
{
    xx_7zip_private *priv = (xx_7zip_private *)((xx_7zip *)self)->internal;
    xx_7zip_folder *folder;
    xx_io_device *decoded = NULL;
    uint64_t end;
    bool keep, result = false;
    if (!priv || !file || !cache || file->is_folder || file->anti || (pd && xx_pd_is_stopped(pd))) return false;
    if (file->empty_stream) return file->size == 0;
    if (file->folder_index < 0 || (uint64_t)file->folder_index >= priv->streams.num_folders) return false;
    folder = &priv->streams.folders[file->folder_index];
    if (!folder->supported || folder->unpack_size > INT64_MAX || !xx_7zip_u64_add(file->offset_in_folder, file->size, &end) || end > folder->unpack_size) return false;
    if (!output) {
        /* Test: the folder's files are checked as it is decoded, once. */
        uint64_t index = (uint64_t)(file - priv->files);
        if ((cache->verified_folder == (uint64_t)file->folder_index && cache->verdicts) || xx_7zip_verify_folder(self, priv, cache, (uint64_t)file->folder_index, pd))
            return cache->verdicts[index] == 1U && !(pd && xx_pd_is_stopped(pd));
    }
    keep = folder->num_substreams > 1U;
    if (!keep && output && file->offset_in_folder == 0 && file->size <= folder->unpack_size) {
        /* Fast path: decode directly into destination without temporary file */
        uint32_t final_crc = 0;
        bool has_crc = false;
        xx_7zip_extract_cache_clear(cache);
        if (file->size == folder->unpack_size) {
            bool dec_ok = xx_7zip_decode_folder(self, &priv->streams, (uint64_t)file->folder_index, output, pd, &final_crc, &has_crc);
            if (!dec_ok) return false;
            if (file->crc_defined && !folder->crc_defined) {
                if (has_crc) {
                    if (final_crc != file->crc) return false;
                } else {
                    if (!xx_crc_verify_device(output, 0, (int64_t)file->size, XX_CRC_TYPE_CRC32, file->crc, pd)) return false;
                }
            }
        } else {
            xx_7zip_slice_sink slice;
            xx_mem_zero(&slice, sizeof(slice));
            slice.target = output;
            slice.limit = file->size;
            slice.device.priv = &slice;
            slice.device.write = xx_7zip_slice_sink_write;
            slice.device.total_size = xx_7zip_slice_sink_size;
            bool dec_ok = xx_7zip_decode_folder(self, &priv->streams, (uint64_t)file->folder_index, &slice.device, pd, NULL, NULL);
            if (!dec_ok || slice.error || slice.written != file->size) return false;
            if (file->crc_defined && slice.crc != file->crc) return false;
        }
        return !(pd && xx_pd_is_stopped(pd));
    }
    if (cache->device && cache->folder_index == (uint64_t)file->folder_index) decoded = cache->device;
    else {
        xx_7zip_extract_cache_clear(cache);
        decoded = xx_io_temp_open();
        if (!decoded || !xx_7zip_decode_folder(self, &priv->streams, (uint64_t)file->folder_index, decoded, pd, NULL, NULL)) goto cleanup;
        if (keep) {
            cache->device = decoded;
            cache->size = folder->unpack_size;
            cache->folder_index = (uint64_t)file->folder_index;
        }
    }
    uint32_t file_crc = 0;
    result = xx_7zip_copy_range_crc(decoded, file->offset_in_folder, file->size, output, file->crc_defined ? &file_crc : NULL, pd);
    if (result && file->crc_defined && file_crc != file->crc) result = false;
    if (pd && xx_pd_is_stopped(pd)) result = false;
cleanup:
    if (decoded && decoded != cache->device) xx_io_close(decoded);
    return result;
}

/* A stage beside `destination`, in its folder (so that the final rename
 * stays on one filesystem), whose name is `suffix` alone: a member name of
 * up to 255 bytes leaves no room for a suffix after it. */
static char *xx_7zip_stage_candidate(const char *destination, const char *suffix)
{
    size_t folder = 0U;
    size_t index;
    size_t length = xx_str_len(suffix);
    char *candidate;
    for (index = 0U; destination[index]; ++index) {
        if (destination[index] == '/' || destination[index] == '\\') folder = index + 1U;
    }
    candidate = (char *)xx_mem_alloc(folder + length + 1U);
    if (!candidate) return NULL;
    xx_rt_memcpy(candidate, destination, folder);
    xx_rt_memcpy(candidate + folder, suffix, length + 1U);
    return candidate;
}

static xx_io_device *xx_7zip_open_stage(const char *destination, char **path)
{
    static const char hex[] = "0123456789abcdef";
    static uint32_t s_stage_counter = 0;
    unsigned int attempt;
    *path = NULL;
    for (attempt = 0U; attempt < 8U; ++attempt) {
        char suffix[46] = ".xxfclib.tmp.";
        uint32_t seq = ++s_stage_counter;
        uint32_t rnd = (uint32_t)xx_rt_rand();
        size_t i;
        char *candidate;
        xx_io_device *device;
        bool retry;
        for (i = 0U; i < 8U; ++i) {
            uint8_t byte = (uint8_t)((seq >> (i * 4U)) ^ (rnd >> ((7U - i) * 4U)));
            suffix[13U + 2U * i] = hex[byte >> 4U];
            suffix[14U + 2U * i] = hex[byte & 15U];
        }
        for (i = 8U; i < 16U; ++i) {
            uint8_t byte = (uint8_t)((rnd >> ((i - 8U) * 4U)) + (uint8_t)i);
            suffix[13U + 2U * i] = hex[byte >> 4U];
            suffix[14U + 2U * i] = hex[byte & 15U];
        }
        suffix[45] = '\0';
        candidate = xx_7zip_stage_candidate(destination, suffix);
        if (!candidate) return NULL;
        device = xx_io_stage_open(destination, candidate, &retry);
        if (device) {
            *path = candidate;
            return device;
        }
        xx_str_free(candidate);
        if (!retry) return NULL;
    }
    return NULL;
}

typedef struct xx_7zip_extract_member_s {
    uint64_t file_index;
    uint64_t offset;
    uint64_t size;
    uint32_t expected_crc;
    bool crc_defined;
    wchar_t *path_w;
    char *utf8_path;
} xx_7zip_extract_member;

typedef struct xx_7zip_extract_sink_s {
    xx_io_device device;
    xx_7zip_extract_member *members;
    uint64_t member_count;
    uint64_t current;
    uint64_t position;
    uint32_t member_crc;
    uint32_t folder_crc;
    xx_io_device *cur_dev;
    char *cur_stage_path;
    bool overwrite;
    bool error;
    xx_pd_struct *pd;
} xx_7zip_extract_sink;

static ssize_t xx_7zip_extract_sink_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_extract_sink *sink = (xx_7zip_extract_sink *)device->priv;
    const uint8_t *data = (const uint8_t *)buffer;
    size_t left = size;

    if (!sink || sink->error || (sink->pd && xx_pd_is_stopped(sink->pd))) return -1;

    while (sink->current < sink->member_count) {
        xx_7zip_extract_member *m = &sink->members[sink->current];
        uint64_t end = m->offset + m->size;

        if (sink->position < m->offset) {
            sink->error = true;
            return -1;
        }

        if (m->size == 0 && sink->position == end) {
            if (sink->overwrite) {
                sink->cur_dev = m->path_w ? xx_io_file_open_w(m->path_w, "wb") : xx_io_file_open(m->utf8_path, "wb");
                if (!sink->cur_dev) {
                    sink->error = true;
                    return -1;
                }
                xx_io_close(sink->cur_dev);
                sink->cur_dev = NULL;
                if (m->crc_defined && m->expected_crc != 0) {
                    if (m->path_w) (void)xx_io_file_remove_w(m->path_w);
                    else (void)xx_io_file_remove_a(m->utf8_path);
                    sink->error = true;
                    return -1;
                }
                sink->current++;
                continue;
            }
            if (!m->utf8_path && m->path_w) m->utf8_path = xx_str_unicode_to_utf8(m->path_w);
            sink->cur_dev = xx_7zip_open_stage(m->utf8_path, &sink->cur_stage_path);
            if (!sink->cur_dev) {
                sink->error = true;
                return -1;
            }
            xx_io_close(sink->cur_dev);
            sink->cur_dev = NULL;
            if (m->crc_defined && m->expected_crc != 0) {
                if (sink->cur_stage_path) {
                    (void)xx_io_file_remove_a(sink->cur_stage_path);
                    xx_str_free(sink->cur_stage_path);
                    sink->cur_stage_path = NULL;
                }
                sink->error = true;
                return -1;
            }
            if (!xx_io_file_replace_a(sink->cur_stage_path, m->utf8_path, sink->overwrite)) {
                if (sink->cur_stage_path) {
                    (void)xx_io_file_remove_a(sink->cur_stage_path);
                    xx_str_free(sink->cur_stage_path);
                    sink->cur_stage_path = NULL;
                }
                sink->error = true;
                return -1;
            }
            xx_str_free(sink->cur_stage_path);
            sink->cur_stage_path = NULL;
            sink->current++;
            continue;
        }

        if (left == 0) break;

        if (!sink->cur_dev) {
            if (sink->overwrite) {
                sink->cur_dev = m->path_w ? xx_io_file_open_w(m->path_w, "wb") : xx_io_file_open(m->utf8_path, "wb");
                sink->cur_stage_path = NULL;
            } else {
                if (!m->utf8_path && m->path_w) m->utf8_path = xx_str_unicode_to_utf8(m->path_w);
                sink->cur_dev = xx_7zip_open_stage(m->utf8_path, &sink->cur_stage_path);
            }
            if (!sink->cur_dev) {
                sink->error = true;
                return -1;
            }
            if (m->size > 32768U) {
                xx_io_resize(sink->cur_dev, (int64_t)m->size);
            }
            sink->member_crc = 0U;
        }

        size_t take = (uint64_t)left < (end - sink->position) ? left : (size_t)(end - sink->position);
        if (take > 0) {
            ssize_t written = xx_io_write(sink->cur_dev, data, take);
            if (written != (ssize_t)take) {
                sink->error = true;
                return -1;
            }
            if (m->crc_defined) {
                sink->member_crc = xx_crc32_calc(sink->member_crc, data, take);
            }
            data += take;
            left -= take;
            sink->position += take;
        }

        if (sink->position >= end) {
            int close_res = xx_io_close(sink->cur_dev);
            sink->cur_dev = NULL;
            if (close_res != 0) {
                if (sink->cur_stage_path) {
                    (void)xx_io_file_remove_a(sink->cur_stage_path);
                    xx_str_free(sink->cur_stage_path);
                    sink->cur_stage_path = NULL;
                } else if (m->path_w) {
                    (void)xx_io_file_remove_w(m->path_w);
                } else {
                    (void)xx_io_file_remove_a(m->utf8_path);
                }
                sink->error = true;
                return -1;
            }

            if (m->crc_defined && sink->member_crc != m->expected_crc) {
                if (sink->cur_stage_path) {
                    (void)xx_io_file_remove_a(sink->cur_stage_path);
                    xx_str_free(sink->cur_stage_path);
                    sink->cur_stage_path = NULL;
                } else if (m->path_w) {
                    (void)xx_io_file_remove_w(m->path_w);
                } else {
                    (void)xx_io_file_remove_a(m->utf8_path);
                }
                sink->error = true;
                return -1;
            }

            if (sink->cur_stage_path) {
                if (!xx_io_file_replace_a(sink->cur_stage_path, m->utf8_path, sink->overwrite)) {
                    (void)xx_io_file_remove_a(sink->cur_stage_path);
                    xx_str_free(sink->cur_stage_path);
                    sink->cur_stage_path = NULL;
                    sink->error = true;
                    return -1;
                }
                xx_str_free(sink->cur_stage_path);
                sink->cur_stage_path = NULL;
            }

            sink->member_crc = 0U;
            sink->current++;
        }
    }

    if (left > 0) {
        sink->position += left;
    }

    return (ssize_t)size;
}

static int64_t xx_7zip_extract_sink_size(xx_io_device *device)
{
    xx_7zip_extract_sink *sink = (xx_7zip_extract_sink *)device->priv;
    return sink && sink->position <= (uint64_t)INT64_MAX ? (int64_t)sink->position : -1;
}

static void xx_7zip_extract_members_cleanup(xx_7zip_extract_member *members, uint64_t count)
{
    if (!members) return;
    for (uint64_t k = 0; k < count; ++k) {
        if (members[k].path_w) xx_str_wfree(members[k].path_w);
        if (members[k].utf8_path) xx_str_free(members[k].utf8_path);
    }
}

static bool xx_7zip_extract_folder_on_the_fly(Abstractformat *self, xx_7zip_private *priv, xx_7zip_extract_cache *cache, uint64_t folder_index,
                                              const wchar_t *base_copy, bool overwrite, xx_pd_struct *pd)
{
    xx_7zip_extract_sink sink;
    uint64_t i, count = 0U, next = 0U;
    bool decoded = false;
    const wchar_t separator_text[2] = {xx_io_platform_wseparator(), L'\0'};

    if (!cache->extracted_verdicts && !(cache->extracted_verdicts = (uint8_t *)xx_mem_calloc(priv->num_files ? (size_t)priv->num_files : 1U, 1U))) return false;

    for (i = 0U; i < priv->num_files; ++i) {
        if (!priv->files[i].empty_stream && !priv->files[i].is_folder && priv->files[i].folder_index == (int64_t)folder_index) ++count;
    }
    if (count == 0) return true;

    xx_mem_zero(&sink, sizeof(sink));
    sink.members = (xx_7zip_extract_member *)xx_7zip_alloc_array(count, sizeof(xx_7zip_extract_member));
    if (!sink.members) return false;

    wchar_t *last_created_dir = NULL;
    size_t last_created_dir_len = 0;

    for (i = 0U; i < priv->num_files; ++i) {
        const xx_7zip_file *file = &priv->files[i];
        if (file->empty_stream || file->is_folder || file->folder_index != (int64_t)folder_index) continue;
        if (file->offset_in_folder != next || !xx_7zip_u64_add(next, file->size, &next)) {
            if (last_created_dir) xx_str_wfree(last_created_dir);
            xx_7zip_extract_members_cleanup(sink.members, sink.member_count);
            xx_mem_free(sink.members);
            return false;
        }

        wchar_t *norm_name = xx_7zip_normalized_name(file->name ? file->name : L"");
        if (!norm_name) {
            if (last_created_dir) xx_str_wfree(last_created_dir);
            xx_7zip_extract_members_cleanup(sink.members, sink.member_count);
            xx_mem_free(sink.members);
            return false;
        }

        size_t length = xx_str_wlen(base_copy);
        bool slash = length && base_copy[length - 1U] != L'/' && base_copy[length - 1U] != L'\\';
        wchar_t *full_path = slash ? xx_str_wconcat3(base_copy, separator_text, norm_name) : xx_str_wconcat(base_copy, norm_name);
        xx_str_wfree(norm_name);

        if (!full_path) {
            if (last_created_dir) xx_str_wfree(last_created_dir);
            xx_7zip_extract_members_cleanup(sink.members, sink.member_count);
            xx_mem_free(sink.members);
            return false;
        }

        size_t full_len = xx_str_wlen(full_path);
        size_t last_slash = 0;
        for (size_t s = 0; s < full_len; ++s) {
            if (full_path[s] == L'/' || full_path[s] == L'\\') last_slash = s;
        }
        if (last_slash > 0) {
            if (!last_created_dir || last_created_dir_len != last_slash || xx_mem_compare(last_created_dir, full_path, last_slash * sizeof(wchar_t)) != 0) {
                if (!xx_store_create_dirs_w(full_path, false)) {
                    xx_str_wfree(full_path);
                    if (last_created_dir) xx_str_wfree(last_created_dir);
                    xx_7zip_extract_members_cleanup(sink.members, sink.member_count);
                    xx_mem_free(sink.members);
                    return false;
                }
                if (last_created_dir) xx_str_wfree(last_created_dir);
                last_created_dir = (wchar_t *)xx_mem_alloc((last_slash + 1) * sizeof(wchar_t));
                if (last_created_dir) {
                    xx_mem_copy(last_created_dir, full_path, last_slash * sizeof(wchar_t));
                    last_created_dir[last_slash] = L'\0';
                    last_created_dir_len = last_slash;
                }
            }
        }

        xx_7zip_extract_member *m = &sink.members[sink.member_count++];
        m->file_index = i;
        m->offset = file->offset_in_folder;
        m->size = file->size;
        m->expected_crc = (uint32_t)file->crc;
        m->crc_defined = file->crc_defined;
        m->path_w = full_path;
        m->utf8_path = NULL;

        cache->extracted_verdicts[i] = 0U;
    }
    if (last_created_dir) xx_str_wfree(last_created_dir);

    sink.overwrite = overwrite;
    sink.pd = pd;
    sink.device.priv = &sink;
    sink.device.write = xx_7zip_extract_sink_write;
    sink.device.total_size = xx_7zip_extract_sink_size;

    decoded = xx_7zip_decode_folder(self, &priv->streams, folder_index, &sink.device, pd, NULL, NULL);

    while (!sink.error && sink.current < sink.member_count) {
        xx_7zip_extract_member *m = &sink.members[sink.current];
        if (m->size == 0 && sink.position == m->offset) {
            if (sink.overwrite) {
                sink.cur_dev = m->path_w ? xx_io_file_open_w(m->path_w, "wb") : xx_io_file_open(m->utf8_path, "wb");
                if (!sink.cur_dev) {
                    sink.error = true;
                    break;
                }
                xx_io_close(sink.cur_dev);
                sink.cur_dev = NULL;
                if (m->crc_defined && m->expected_crc != 0) {
                    if (m->path_w) (void)xx_io_file_remove_w(m->path_w);
                    else (void)xx_io_file_remove_a(m->utf8_path);
                    sink.error = true;
                    break;
                }
                sink.current++;
                continue;
            }
            if (!m->utf8_path && m->path_w) m->utf8_path = xx_str_unicode_to_utf8(m->path_w);
            sink.cur_dev = xx_7zip_open_stage(m->utf8_path, &sink.cur_stage_path);
            if (!sink.cur_dev) {
                sink.error = true;
                break;
            }
            xx_io_close(sink.cur_dev);
            sink.cur_dev = NULL;
            if (m->crc_defined && m->expected_crc != 0) {
                if (sink.cur_stage_path) {
                    (void)xx_io_file_remove_a(sink.cur_stage_path);
                    xx_str_free(sink.cur_stage_path);
                    sink.cur_stage_path = NULL;
                }
                sink.error = true;
                break;
            }
            if (!xx_io_file_replace_a(sink.cur_stage_path, m->utf8_path, sink.overwrite)) {
                if (sink.cur_stage_path) {
                    (void)xx_io_file_remove_a(sink.cur_stage_path);
                    xx_str_free(sink.cur_stage_path);
                    sink.cur_stage_path = NULL;
                }
                sink.error = true;
                break;
            }
            xx_str_free(sink.cur_stage_path);
            sink.cur_stage_path = NULL;
            sink.current++;
        } else {
            break;
        }
    }

    if (sink.cur_dev) {
        xx_io_close(sink.cur_dev);
        sink.cur_dev = NULL;
    }
    if (sink.cur_stage_path) {
        (void)xx_io_file_remove_a(sink.cur_stage_path);
        xx_str_free(sink.cur_stage_path);
        sink.cur_stage_path = NULL;
    }

    bool success = decoded && !sink.error && (sink.current == sink.member_count);
    for (i = 0U; i < sink.member_count; ++i) {
        uint64_t fidx = sink.members[i].file_index;
        cache->extracted_verdicts[fidx] = success ? 1U : 2U;
        if (sink.members[i].path_w) xx_str_wfree(sink.members[i].path_w);
        if (sink.members[i].utf8_path) xx_str_free(sink.members[i].utf8_path);
    }
    xx_mem_free(sink.members);

    cache->extracted_folder = folder_index;
    return success;
}

bool xx_7zip_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_7zip_private *priv;
    const xx_7zip_file *file;
    const xx_var *path_option;
    const wchar_t *base_w = NULL;
    const char *base_a = NULL;
    wchar_t *base_copy = NULL;
    wchar_t *name = NULL;
    wchar_t *full_path = NULL;
    char *utf8_path = NULL;
    char *stage_path = NULL;
    bool result = false;
    if (!self || !state || state->format != self || !state->has_record || state->current_index < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    priv = (xx_7zip_private *)((xx_7zip *)self)->internal;
    if (!priv || (uint64_t)state->current_index >= priv->num_files) return false;
    file = &priv->files[state->current_index];
    {
        const xx_var *limit = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
        if (limit && !file->is_folder && file->size > xx_var_get_u64(limit)) return false;
    }
    if (!file->is_folder && file->folder_index >= 0 && (uint64_t)file->folder_index < priv->streams.num_folders) {
        xx_7zip_extract_cache *cache = (xx_7zip_extract_cache *)state->internal_state;
        if (cache && cache->extracted_folder == (uint64_t)file->folder_index && cache->extracted_verdicts) {
            return (cache->extracted_verdicts[state->current_index] == 1U) && !(pd && xx_pd_is_stopped(pd));
        }
    }
    path_option = xx_7zip_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (path_option) {
        if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) base_w = xx_var_get_wstr(path_option);
        else if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base_a = xx_var_get_str(path_option);
    }
    if (!base_w && !base_a) {
        if (file->is_folder) return true;
        return xx_7zip_extract_file_to_device(self, file, (xx_7zip_extract_cache *)state->internal_state, NULL, pd);
    }
    base_copy = base_w ? xx_str_wdup(base_w) : xx_str_utf8_to_unicode(base_a);
    name = xx_7zip_normalized_name(file->name);
    if (!base_copy || !name) goto cleanup;
    {
        size_t length = xx_str_wlen(base_copy);
        bool slash = length && base_copy[length - 1U] != L'/' && base_copy[length - 1U] != L'\\';
        const wchar_t separator_text[2] = {xx_io_platform_wseparator(), L'\0'};
        full_path = slash ? xx_str_wconcat3(base_copy, separator_text, name) : xx_str_wconcat(base_copy, name);
    }
    if (!full_path) goto cleanup;
    if (file->is_folder) {
        result = xx_store_create_dirs_w(full_path, true);
    } else {
        xx_7zip_extract_cache *cache = (xx_7zip_extract_cache *)state->internal_state;
        if (file->folder_index >= 0 && (uint64_t)file->folder_index < priv->streams.num_folders) {
            const xx_7zip_folder *folder = &priv->streams.folders[file->folder_index];
            if (folder->num_substreams > 1U) {
                if (cache->extracted_folder == (uint64_t)file->folder_index && cache->extracted_verdicts) {
                    result = (cache->extracted_verdicts[state->current_index] == 1U) && !(pd && xx_pd_is_stopped(pd));
                    goto cleanup;
                }
                const xx_var *overwrite = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
                bool allow_ow = overwrite ? xx_var_get_bool(overwrite) : true;
                if (xx_7zip_extract_folder_on_the_fly(self, priv, cache, (uint64_t)file->folder_index, base_copy, allow_ow, pd)) {
                    result = (cache->extracted_verdicts && cache->extracted_verdicts[state->current_index] == 1U) && !(pd && xx_pd_is_stopped(pd));
                    goto cleanup;
                }
            }
        }
        xx_io_device *output;
        if (!xx_store_create_dirs_w(full_path, false)) goto cleanup;
        utf8_path = xx_str_unicode_to_utf8(full_path);
        if (!utf8_path) goto cleanup;
        output = xx_7zip_open_stage(utf8_path, &stage_path);
        if (!output) goto cleanup;
        result = xx_7zip_extract_file_to_device(self, file, cache, output, pd);
        if (xx_io_close(output) != 0) result = false;
        if (result && pd && xx_pd_is_stopped(pd)) result = false;
        if (result) {
            const xx_var *overwrite = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
            result = xx_io_file_replace_a(stage_path, utf8_path, overwrite ? xx_var_get_bool(overwrite) : true);
        }
    }

cleanup:
    if (stage_path) {
        (void)xx_io_file_remove_a(stage_path);
        xx_str_free(stage_path);
    }
    if (utf8_path) xx_str_free(utf8_path);
    if (full_path) xx_str_wfree(full_path);
    if (name) xx_str_wfree(name);
    if (base_copy) xx_str_wfree(base_copy);
    return result;
}

void xx_7zip_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
