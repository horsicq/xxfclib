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
#include "xxfclib/formats/7zip/xx_7zip.h"
#include "xx_7zip_branch.h"
#include "xx_7zip_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/lz5/xx_lz5.h"
#include "xxfclib/algo/lizard/xx_lizard.h"
#include "xxfclib/algo/brotli/xx_brotli.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/algo/ppmd7/xx_ppmd7.h"
#include "xxfclib/algo/aes/xx_aes.h"

/* Native separator, reserved device names and OS entropy. */
#include "../../io/platforms/xx_io_platform.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define XX_7ZIP_MAX_HEADER_SIZE  (64U * 1024U * 1024U)
#define XX_7ZIP_MAX_ITEMS        262144U
#define XX_7ZIP_MAX_CODERS       64U
#define XX_7ZIP_MAX_FOLDER_OUTPUT (512U * 1024U * 1024U)
#define XX_7ZIP_MAX_PROPERTIES   64U

typedef struct xx_7zip_reader_s {
    const uint8_t *data;
    size_t size;
    size_t pos;
} xx_7zip_reader;

typedef struct xx_7zip_coder_s {
    uint64_t method;
    uint64_t num_in;
    uint64_t num_out;
    uint8_t properties[XX_7ZIP_MAX_PROPERTIES];
    size_t properties_size;
} xx_7zip_coder;

typedef struct xx_7zip_folder_s {
    xx_7zip_coder *coders;
    uint64_t *bind_inputs;
    uint64_t *bind_outputs;
    uint64_t *packed_indices;
    uint64_t *unpack_sizes;
    size_t coder_count;
    size_t bind_count;
    uint64_t total_in;
    uint64_t total_out;
    uint64_t final_output_index;
    uint64_t packed_count;
    uint64_t first_pack_index;
    uint64_t unpack_size;
    uint64_t num_substreams;
    uint64_t first_substream;
    uint32_t crc;
    bool crc_defined;
    bool encrypted;
    bool supported;
    uint64_t method;
} xx_7zip_folder;

typedef struct xx_7zip_streams_s {
    uint64_t pack_pos;
    uint64_t num_packs;
    uint64_t *pack_sizes;
    uint32_t *pack_crcs;
    uint8_t *pack_crc_defined;
    xx_7zip_folder *folders;
    uint64_t num_folders;
    uint64_t num_substreams;
    uint64_t *sub_sizes;
    uint32_t *sub_crcs;
    uint8_t *sub_crc_defined;
    bool substreams_ready;
} xx_7zip_streams;

typedef struct xx_7zip_file_s {
    wchar_t *name;
    uint64_t size;
    uint64_t offset_in_folder;
    int64_t folder_index;
    uint32_t crc;
    uint32_t attributes;
    uint64_t mtime;
    bool crc_defined;
    bool attributes_defined;
    bool mtime_defined;
    bool empty_stream;
    bool empty_file;
    bool anti;
    bool is_folder;
} xx_7zip_file;

typedef struct xx_7zip_private_s {
    xx_7zip_streams streams;
    xx_7zip_file *files;
    uint64_t num_files;
    int64_t physical_data_end;
} xx_7zip_private;

typedef struct xx_7zip_folder_decode_s {
    Abstractformat *format;
    const xx_7zip_streams *streams;
    const xx_7zip_folder *folder;
    xx_pd_struct *pd;
    uint8_t **input_data;
    size_t *input_sizes;
    uint8_t **output_data;
    size_t *output_sizes;
    uint8_t *output_busy;
} xx_7zip_folder_decode;

typedef struct xx_7zip_buffer_device_s {
    xx_io_device device;
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    size_t position;
} xx_7zip_buffer_device;

static void xx_7zip_vtable_destroy(Abstractformat *self);
static bool xx_7zip_read_exact_at(xx_io_device *device, int64_t offset,
                                  void *buffer, size_t size);
static bool xx_7zip_get_password_utf16le(const Abstractformat *self,
                                         uint8_t **password,
                                         size_t *password_size);

static ssize_t xx_7zip_buffer_read(xx_io_device *device, void *buffer, size_t size) {
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    size_t available;
    if (!sink || (!buffer && size)) return -1;
    available = sink->length - sink->position;
    if (size > available) size = available;
    if (size) xx_mem_copy(buffer, sink->buffer + sink->position, size);
    sink->position += size;
    return (ssize_t)size;
}

static ssize_t xx_7zip_buffer_write(xx_io_device *device, const void *buffer, size_t size) {
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    if (!sink || (!buffer && size) || size > sink->capacity - sink->position) return -1;
    if (size) xx_mem_copy(sink->buffer + sink->position, buffer, size);
    sink->position += size;
    if (sink->position > sink->length) sink->length = sink->position;
    return (ssize_t)size;
}

static int xx_7zip_buffer_seek(xx_io_device *device, long offset, int origin) {
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

static int64_t xx_7zip_buffer_size(xx_io_device *device) {
    xx_7zip_buffer_device *sink = device ? (xx_7zip_buffer_device *)device->priv : NULL;
    return sink ? (int64_t)sink->length : -1;
}

static void xx_7zip_buffer_device_init(xx_7zip_buffer_device *sink,
                                       uint8_t *buffer, size_t capacity) {
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

static uint32_t xx_7zip_read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xx_7zip_read_le64(const uint8_t *p) {
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8; ++i) value |= ((uint64_t)p[i]) << (8U * i);
    return value;
}

static bool xx_7zip_u64_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out || UINT64_MAX - a < b) return false;
    *out = a + b;
    return true;
}

static bool xx_7zip_size_multiply(uint64_t count, size_t size, size_t *out) {
    if (!out || count > (uint64_t)(SIZE_MAX / (size ? size : 1U))) return false;
    *out = (size_t)count * size;
    return true;
}

static void *xx_7zip_alloc_array(uint64_t count, size_t size) {
    size_t bytes;
    if (count == 0) return NULL;
    if (count > XX_7ZIP_MAX_ITEMS || !xx_7zip_size_multiply(count, size, &bytes)) return NULL;
    return xx_mem_calloc(1, bytes);
}

static bool xx_7zip_reader_u8(xx_7zip_reader *r, uint8_t *value) {
    if (!r || !value || r->pos >= r->size) return false;
    *value = r->data[r->pos++];
    return true;
}

static bool xx_7zip_reader_u32(xx_7zip_reader *r, uint32_t *value) {
    if (!r || !value || r->size - r->pos < 4U) return false;
    *value = xx_7zip_read_le32(r->data + r->pos);
    r->pos += 4U;
    return true;
}

static bool xx_7zip_reader_u64_real(xx_7zip_reader *r, uint64_t *value) {
    if (!r || !value || r->size - r->pos < 8U) return false;
    *value = xx_7zip_read_le64(r->data + r->pos);
    r->pos += 8U;
    return true;
}

static bool xx_7zip_reader_number(xx_7zip_reader *r, uint64_t *value) {
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

static bool xx_7zip_reader_bytes(xx_7zip_reader *r, void *dst, size_t size) {
    if (!r || size > r->size - r->pos) return false;
    if (dst && size) xx_mem_copy(dst, r->data + r->pos, size);
    r->pos += size;
    return true;
}

static bool xx_7zip_reader_skip(xx_7zip_reader *r, uint64_t size) {
    if (!r || size > (uint64_t)(r->size - r->pos)) return false;
    r->pos += (size_t)size;
    return true;
}

static bool xx_7zip_reader_subreader(xx_7zip_reader *r, uint64_t size, xx_7zip_reader *sub) {
    if (!r || !sub || size > (uint64_t)(r->size - r->pos)) return false;
    sub->data = r->data + r->pos;
    sub->size = (size_t)size;
    sub->pos = 0;
    r->pos += (size_t)size;
    return true;
}

static bool xx_7zip_bit(const uint8_t *bits, uint64_t index) {
    return bits && (bits[index >> 3] & (uint8_t)(0x80U >> (index & 7U))) != 0;
}

static void xx_7zip_folder_cleanup(xx_7zip_folder *folder) {
    if (!folder) return;
    if (folder->coders) xx_mem_free(folder->coders);
    if (folder->bind_inputs) xx_mem_free(folder->bind_inputs);
    if (folder->bind_outputs) xx_mem_free(folder->bind_outputs);
    if (folder->packed_indices) xx_mem_free(folder->packed_indices);
    if (folder->unpack_sizes) xx_mem_free(folder->unpack_sizes);
    xx_mem_zero(folder, sizeof(*folder));
}

static void xx_7zip_streams_cleanup(xx_7zip_streams *streams) {
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

static void xx_7zip_private_free(xx_7zip_private *priv) {
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

static bool xx_7zip_parse_digests(xx_7zip_reader *r, uint64_t count,
                                  uint8_t *defined, uint32_t *crcs) {
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

static bool xx_7zip_parse_pack_info(xx_7zip_reader *r, xx_7zip_streams *streams) {
    uint64_t count;
    uint8_t nid;
    bool have_sizes = false;
    uint64_t i;
    if (!xx_7zip_reader_number(r, &streams->pack_pos) ||
        !xx_7zip_reader_number(r, &count) || count > XX_7ZIP_MAX_ITEMS) return false;
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

static bool xx_7zip_aes_properties_supported(const xx_7zip_coder *coder) {
    size_t salt_size;
    size_t iv_size;
    uint8_t first;
    uint8_t second;
    if (!coder || coder->method != XX_7ZIP_METHOD_AES ||
        coder->properties_size < 1U) return false;
    first = coder->properties[0];
    if ((first & 0x3FU) > 24U && (first & 0x3FU) != 0x3FU) return false;
    if ((first & 0xC0U) == 0U) return coder->properties_size == 1U;
    if (coder->properties_size < 2U) return false;
    second = coder->properties[1];
    salt_size = (size_t)((first >> 7U) & 1U) + (size_t)(second >> 4U);
    iv_size = (size_t)((first >> 6U) & 1U) + (size_t)(second & 0x0FU);
    return salt_size <= 16U && iv_size <= 16U &&
           coder->properties_size == 2U + salt_size + iv_size;
}

static bool xx_7zip_coder_supported(const xx_7zip_coder *coder) {
    uint32_t memory_size;
    if (!coder) return false;
    if (coder->method == XX_7ZIP_METHOD_COPY)
        return coder->properties_size == 0U;
    if (coder->method == XX_7ZIP_METHOD_LZMA)
        return coder->properties_size == XX_LZMA_PROPS_SIZE;
    if (coder->method == XX_7ZIP_METHOD_LZMA2)
        return coder->properties_size == XX_LZMA2_PROPS_SIZE &&
               coder->properties[0] <= 40U;
    if (coder->method == XX_7ZIP_METHOD_BZIP2)
        return coder->properties_size == 0U;
    if (coder->method == XX_7ZIP_METHOD_PPMD7) {
        memory_size = coder->properties_size == 5U
            ? xx_7zip_read_le32(coder->properties + 1U) : 0U;
        return coder->properties_size == 5U && coder->properties[0] >= 2U &&
               coder->properties[0] <= 64U &&
               memory_size >= XX_PPMD7_MIN_MEM_SIZE &&
               memory_size <= XX_PPMD7_MAX_MEM_SIZE;
    }
    if (coder->method == XX_7ZIP_METHOD_ZSTD)
        return coder->properties_size == 1U || coder->properties_size == 3U ||
               coder->properties_size == 5U;
    if (coder->method == XX_7ZIP_METHOD_BROTLI ||
        coder->method == XX_7ZIP_METHOD_LIZARD)
        return coder->properties_size == 3U;
    if (coder->method == XX_7ZIP_METHOD_LZ4 ||
        coder->method == XX_7ZIP_METHOD_LZ5)
        return coder->properties_size == 5U;
    if (coder->method == XX_7ZIP_METHOD_BCJ ||
        coder->method == XX_7ZIP_METHOD_PPC ||
        coder->method == XX_7ZIP_METHOD_IA64 ||
        coder->method == XX_7ZIP_METHOD_ARM ||
        coder->method == XX_7ZIP_METHOD_ARMT ||
        coder->method == XX_7ZIP_METHOD_SPARC ||
        coder->method == XX_7ZIP_METHOD_ARM64 ||
        coder->method == XX_7ZIP_METHOD_RISCV)
        return xx_7zip_branch_method_supported(coder->method, coder->properties_size);
    return xx_7zip_aes_properties_supported(coder);
}

static bool xx_7zip_parse_folder(xx_7zip_reader *r, xx_7zip_folder *folder) {
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
    if (!xx_7zip_reader_number(r, &num_coders) || num_coders == 0 ||
        num_coders > XX_7ZIP_MAX_CODERS) return false;
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
            if (!xx_7zip_reader_number(r, &coder->num_in) ||
                !xx_7zip_reader_number(r, &coder->num_out) ||
                coder->num_in == 0 || coder->num_out == 0) return false;
        }
        if (!xx_7zip_u64_add(total_in, coder->num_in, &total_in) ||
            !xx_7zip_u64_add(total_out, coder->num_out, &total_out) ||
            total_in > XX_7ZIP_MAX_ITEMS || total_out > XX_7ZIP_MAX_ITEMS) return false;
        if ((flags & 0x20U) != 0) {
            if (!xx_7zip_reader_number(r, &prop_size) || prop_size > XX_7ZIP_MAX_PROPERTIES ||
                !xx_7zip_reader_bytes(r, coder->properties, (size_t)prop_size)) return false;
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
        if (!xx_7zip_reader_number(r, &in_index) || !xx_7zip_reader_number(r, &out_index) ||
            in_index >= total_in || out_index >= total_out || xx_7zip_bit(bound_inputs, in_index) ||
            xx_7zip_bit(bound_outputs, out_index)) {
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
            if (!xx_7zip_reader_number(r, &index) || index >= total_in ||
                xx_7zip_bit(bound_inputs, index) || xx_7zip_bit(seen, index)) {
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
                if (coder->num_in != 4U || coder->num_out != 1U ||
                    !xx_7zip_branch_properties_supported(coder->properties_size)) all_supported = false;
            } else if (coder->num_in != 1U || coder->num_out != 1U ||
                       !xx_7zip_coder_supported(coder)) {
                all_supported = false;
            }
        }
        if (folder->method == XX_7ZIP_METHOD_AES) {
            folder->method = XX_7ZIP_METHOD_COPY;
        }
        folder->supported = all_supported && aes_count <= 1U &&
            ((bcj2_count == 0U && packed_count == 1U &&
              total_in == (uint64_t)folder->coder_count &&
              total_out == (uint64_t)folder->coder_count) ||
             (bcj2_count == 1U && packed_count == 4U));
    }
    return true;
}

static bool xx_7zip_parse_unpack_info(xx_7zip_reader *r, xx_7zip_streams *streams) {
    uint8_t nid, external;
    uint64_t count;
    uint64_t pack_cursor = 0;
    uint64_t i, j;
    if (!xx_7zip_reader_u8(r, &nid) || nid != XX_7ZIP_NID_FOLDER ||
        !xx_7zip_reader_number(r, &count) || count > XX_7ZIP_MAX_ITEMS ||
        !xx_7zip_reader_u8(r, &external) || external != 0) return false;
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
    if (pack_cursor != streams->num_packs ||
        !xx_7zip_reader_u8(r, &nid) || nid != XX_7ZIP_NID_CODERS_UNPACK_SIZE) return false;
    for (i = 0; i < count; ++i) {
        xx_7zip_folder *folder = &streams->folders[i];
        uint64_t output_size = 0;
        folder->unpack_sizes = (uint64_t *)xx_7zip_alloc_array(
            folder->total_out, sizeof(uint64_t));
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

static bool xx_7zip_prepare_substreams(xx_7zip_streams *streams) {
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

static bool xx_7zip_parse_substreams_info(xx_7zip_reader *r, xx_7zip_streams *streams) {
    uint8_t nid;
    uint64_t i, j;
    bool have_sizes = false;
    bool have_crcs = false;
    for (i = 0; i < streams->num_folders; ++i) streams->folders[i].num_substreams = 1;
    if (!xx_7zip_reader_u8(r, &nid)) return false;
    if (nid == XX_7ZIP_NID_NUM_UNPACK_STREAM) {
        for (i = 0; i < streams->num_folders; ++i) {
            if (!xx_7zip_reader_number(r, &streams->folders[i].num_substreams) ||
                streams->folders[i].num_substreams > XX_7ZIP_MAX_ITEMS) return false;
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
                if (!xx_7zip_reader_number(r, &value) || !xx_7zip_u64_add(sum, value, &sum) ||
                    sum > folder->unpack_size) return false;
                streams->sub_sizes[index] = value;
            }
            if (folder->num_substreams) {
                streams->sub_sizes[folder->first_substream + folder->num_substreams - 1U] =
                    folder->unpack_size - sum;
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
            if (!(folder->num_substreams == 1 && folder->crc_defined) &&
                !xx_7zip_u64_add(digest_count, folder->num_substreams, &digest_count)) return false;
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

static bool xx_7zip_parse_streams_info(xx_7zip_reader *r, xx_7zip_streams *streams) {
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
            if (!have_unpack || streams->substreams_ready ||
                !xx_7zip_parse_substreams_info(r, streams)) return false;
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

static wchar_t *xx_7zip_read_utf16_name(xx_7zip_reader *r) {
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

static bool xx_7zip_parse_bool_vector(xx_7zip_reader *r, uint64_t count, uint8_t *values) {
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

static bool xx_7zip_parse_optional_vector(xx_7zip_reader *r, uint64_t count, uint8_t *defined) {
    uint8_t all_defined;
    if (!xx_7zip_reader_u8(r, &all_defined)) return false;
    if (all_defined) {
        if (count) xx_rt_memset(defined, 1, (size_t)count);
        return true;
    }
    return xx_7zip_parse_bool_vector(r, count, defined);
}

static bool xx_7zip_parse_file_times(xx_7zip_reader *prop, xx_7zip_file *files, uint64_t count) {
    uint8_t *defined;
    uint8_t external;
    uint64_t i;
    defined = (uint8_t *)xx_7zip_alloc_array(count, sizeof(uint8_t));
    if (count && !defined) return false;
    if (!xx_7zip_parse_optional_vector(prop, count, defined) ||
        !xx_7zip_reader_u8(prop, &external) || external != 0) {
        if (defined) xx_mem_free(defined);
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (defined[i]) {
            if (!xx_7zip_reader_u64_real(prop, &files[i].mtime)) {
                xx_mem_free(defined);
                return false;
            }
            files[i].mtime_defined = true;
        }
    }
    if (defined) xx_mem_free(defined);
    return true;
}

static bool xx_7zip_parse_file_attributes(xx_7zip_reader *prop, xx_7zip_file *files, uint64_t count) {
    uint8_t *defined;
    uint8_t external;
    uint64_t i;
    defined = (uint8_t *)xx_7zip_alloc_array(count, sizeof(uint8_t));
    if (count && !defined) return false;
    if (!xx_7zip_parse_optional_vector(prop, count, defined) ||
        !xx_7zip_reader_u8(prop, &external) || external != 0) {
        if (defined) xx_mem_free(defined);
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (defined[i]) {
            if (!xx_7zip_reader_u32(prop, &files[i].attributes)) {
                xx_mem_free(defined);
                return false;
            }
            files[i].attributes_defined = true;
        }
    }
    if (defined) xx_mem_free(defined);
    return true;
}

static bool xx_7zip_map_files_to_streams(xx_7zip_private *priv) {
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
                if (sub_index >= folder->first_substream &&
                    sub_index < folder->first_substream + folder->num_substreams) {
                    uint64_t s;
                    file->folder_index = (int64_t)f;
                    file->offset_in_folder = 0;
                    for (s = folder->first_substream; s < sub_index; ++s) {
                        if (!xx_7zip_u64_add(file->offset_in_folder, streams->sub_sizes[s],
                                            &file->offset_in_folder)) return false;
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

static bool xx_7zip_parse_files_info(xx_7zip_reader *r, xx_7zip_private *priv) {
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
        if (!xx_7zip_reader_number(r, &property_size) ||
            !xx_7zip_reader_subreader(r, property_size, &prop)) goto cleanup;
        if (type == XX_7ZIP_NID_NAME) {
            uint8_t external;
            if (!xx_7zip_reader_u8(&prop, &external) || external != 0) goto cleanup;
            for (i = 0; i < count; ++i) {
                if (priv->files[i].name || !(priv->files[i].name = xx_7zip_read_utf16_name(&prop))) goto cleanup;
            }
        } else if (type == XX_7ZIP_NID_EMPTY_STREAM) {
            if (have_empty_stream ||
                !xx_7zip_parse_bool_vector(&prop, count, empty_stream)) goto cleanup;
            have_empty_stream = true;
        } else if (type == XX_7ZIP_NID_EMPTY_FILE || type == XX_7ZIP_NID_ANTI) {
            uint8_t **target = type == XX_7ZIP_NID_EMPTY_FILE ? &empty_file : &anti;
            bool *seen = type == XX_7ZIP_NID_EMPTY_FILE ? &have_empty_file : &have_anti;
            if (*seen) goto cleanup;
            *seen = true;
            empty_count = 0;
            for (i = 0; i < count; ++i) if (empty_stream[i]) ++empty_count;
            if (empty_count) {
                *target = (uint8_t *)xx_7zip_alloc_array(empty_count, sizeof(uint8_t));
                if (!*target || !xx_7zip_parse_bool_vector(&prop, empty_count, *target)) goto cleanup;
            }
        } else if (type == XX_7ZIP_NID_MTIME) {
            if (!xx_7zip_parse_file_times(&prop, priv->files, count)) goto cleanup;
        } else if (type == XX_7ZIP_NID_WIN_ATTRIBUTES) {
            if (!xx_7zip_parse_file_attributes(&prop, priv->files, count)) goto cleanup;
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

static bool xx_7zip_skip_archive_properties(xx_7zip_reader *r) {
    for (;;) {
        uint8_t type;
        uint64_t size;
        if (!xx_7zip_reader_u8(r, &type)) return false;
        if (type == XX_7ZIP_NID_END) return true;
        if (!xx_7zip_reader_number(r, &size) || !xx_7zip_reader_skip(r, size)) return false;
    }
}

static bool xx_7zip_parse_plain_header(const uint8_t *data, size_t size,
                                       xx_7zip_private *priv) {
    xx_7zip_reader r;
    uint8_t nid;
    bool have_streams = false;
    bool have_files = false;
    if (!data || !priv) return false;
    r.data = data;
    r.size = size;
    r.pos = 0;
    if (!xx_7zip_reader_u8(&r, &nid) || nid != XX_7ZIP_NID_HEADER) return false;
    for (;;) {
        if (!xx_7zip_reader_u8(&r, &nid)) return false;
        if (nid == XX_7ZIP_NID_END) break;
        if (nid == XX_7ZIP_NID_ARCHIVE_PROPERTIES) {
            if (!xx_7zip_skip_archive_properties(&r)) return false;
        } else if (nid == XX_7ZIP_NID_ADDITIONAL_STREAMS_INFO) {
            xx_7zip_streams additional;
            xx_mem_zero(&additional, sizeof(additional));
            if (!xx_7zip_parse_streams_info(&r, &additional)) {
                xx_7zip_streams_cleanup(&additional);
                return false;
            }
            xx_7zip_streams_cleanup(&additional);
        } else if (nid == XX_7ZIP_NID_MAIN_STREAMS_INFO) {
            if (have_streams || !xx_7zip_parse_streams_info(&r, &priv->streams)) return false;
            have_streams = true;
        } else if (nid == XX_7ZIP_NID_FILES_INFO) {
            if (have_files || !xx_7zip_parse_files_info(&r, priv)) return false;
            have_files = true;
        } else {
            return false;
        }
    }
    if (r.pos != r.size || !have_files) return false;
    if (!have_streams) {
        /* A valid empty-only archive omits MainStreamsInfo. */
        if (!xx_7zip_prepare_substreams(&priv->streams)) return false;
    }
    return true;
}

static bool xx_7zip_pack_offset(const Abstractformat *self, const xx_7zip_streams *streams,
                                uint64_t pack_index, int64_t *offset, uint64_t *size) {
    uint64_t relative;
    uint64_t i;
    uint64_t absolute;
    int64_t device_size;
    if (!self || !streams || !offset || !size || pack_index >= streams->num_packs) return false;
    relative = streams->pack_pos;
    for (i = 0; i < pack_index; ++i) {
        if (!xx_7zip_u64_add(relative, streams->pack_sizes[i], &relative)) return false;
    }
    if (!xx_7zip_u64_add((uint64_t)XX_7ZIP_SIGNATURE_HEADER_SIZE, relative, &relative) ||
        self->base_address < 0 || !xx_7zip_u64_add((uint64_t)self->base_address, relative, &absolute) ||
        absolute > (uint64_t)INT64_MAX) return false;
    device_size = xx_io_total_size(self->device);
    if (device_size < 0 || absolute > (uint64_t)device_size ||
        streams->pack_sizes[pack_index] > (uint64_t)device_size - absolute ||
        streams->pack_sizes[pack_index] > (uint64_t)INT64_MAX) return false;
    *offset = (int64_t)absolute;
    *size = streams->pack_sizes[pack_index];
    return true;
}

static bool xx_7zip_decode_coder_memory(Abstractformat *self,
                                         const xx_7zip_coder *coder,
                                         const uint8_t *input, size_t input_size,
                                         uint8_t *output, size_t output_size,
                                         xx_pd_struct *pd) {
    xx_7zip_buffer_device source;
    xx_7zip_buffer_device sink;
    size_t written = 0U;
    bool result = false;
    if (!self || !coder || (!input && input_size) || (!output && output_size)) return false;
    xx_7zip_buffer_device_init(&source, (uint8_t *)input, input_size);
    source.length = input_size;
    xx_7zip_buffer_device_init(&sink, output, output_size);

    if (coder->method == XX_7ZIP_METHOD_COPY) {
        result = input_size == output_size;
        if (result && output_size) xx_mem_copy(output, input, output_size);
        if (result) sink.position = sink.length = output_size;
    } else if (coder->method == XX_7ZIP_METHOD_LZMA) {
        result = xx_lzma_unpack_device(&source.device, 0, (int64_t)input_size,
                                       coder->properties, coder->properties_size,
                                       (int64_t)output_size, &sink.device, pd);
    } else if (coder->method == XX_7ZIP_METHOD_LZMA2) {
        result = xx_lzma2_unpack_device(&source.device, 0, (int64_t)input_size,
                                        coder->properties[0], &sink.device, pd);
    } else if (coder->method == XX_7ZIP_METHOD_BZIP2) {
        result = xx_bzip2_unpack_device(&source.device, 0, (int64_t)input_size,
                                        &sink.device, pd);
    } else if (coder->method == XX_7ZIP_METHOD_PPMD7) {
        uint32_t memory_size = xx_7zip_read_le32(coder->properties + 1U);
        result = xx_ppmd7_unpack_device_to_memory_bytes(
                     &source.device, 0, (int64_t)input_size,
                     coder->properties[0], memory_size,
                     output, output_size, &written, pd) &&
                 written == output_size;
        if (result) sink.position = sink.length = written;
    } else if (coder->method == XX_7ZIP_METHOD_ZSTD) {
        result = xx_zstd_decompress_memory(input, input_size, output,
                                           output_size, &written) &&
                 written == output_size;
        if (result) sink.position = sink.length = written;
    } else if (coder->method == XX_7ZIP_METHOD_BROTLI) {
        result = xx_brotli_decompress_memory(input, input_size, output,
                                             output_size, &written) &&
                 written == output_size;
        if (result) sink.position = sink.length = written;
    } else if (coder->method == XX_7ZIP_METHOD_LZ4) {
        result = xx_lz4_decompress_memory(input, input_size, output,
                                          output_size, &written) &&
                 written == output_size;
        if (result) sink.position = sink.length = written;
    } else if (coder->method == XX_7ZIP_METHOD_LZ5) {
        result = xx_lz5_decompress_memory(input, input_size, output,
                                          output_size, &written) &&
                 written == output_size;
        if (result) sink.position = sink.length = written;
    } else if (coder->method == XX_7ZIP_METHOD_LIZARD) {
        result = xx_lizard_decompress_memory(input, input_size, output,
                                             output_size, &written) &&
                 written == output_size;
        if (result) sink.position = sink.length = written;
    } else if (coder->method == XX_7ZIP_METHOD_BCJ ||
               coder->method == XX_7ZIP_METHOD_PPC ||
               coder->method == XX_7ZIP_METHOD_IA64 ||
               coder->method == XX_7ZIP_METHOD_ARM ||
               coder->method == XX_7ZIP_METHOD_ARMT ||
               coder->method == XX_7ZIP_METHOD_SPARC ||
               coder->method == XX_7ZIP_METHOD_ARM64 ||
               coder->method == XX_7ZIP_METHOD_RISCV) {
        result = xx_7zip_branch_decode(coder->method, coder->properties,
                                       coder->properties_size, input, input_size,
                                       output, output_size);
        if (result) sink.position = sink.length = output_size;
    } else if (coder->method == XX_7ZIP_METHOD_AES) {
        uint8_t *password = NULL;
        size_t password_size = 0U;
        result = xx_7zip_get_password_utf16le(self, &password,
                                              &password_size) &&
                 xx_7zip_aes_decrypt(input, input_size, password,
                                     password_size, coder->properties,
                                     coder->properties_size, output,
                                     output_size, output_size);
        if (password) {
            xx_mem_zero(password, password_size);
            xx_mem_free(password);
        }
        if (result) sink.position = sink.length = output_size;
    }
    return result && sink.length == output_size;
}

static bool xx_7zip_folder_decode_output(xx_7zip_folder_decode *context,
                                          uint64_t output_index,
                                          const uint8_t **data, size_t *size);

static bool xx_7zip_folder_decode_input(xx_7zip_folder_decode *context,
                                         uint64_t input_index,
                                         const uint8_t **data, size_t *size) {
    uint64_t bind;
    uint64_t packed;
    int64_t offset;
    uint64_t packed_size;
    if (!context || !data || !size || input_index >= context->folder->total_in) return false;
    for (bind = 0U; bind < context->folder->bind_count; ++bind) {
        if (context->folder->bind_inputs[bind] == input_index) {
            return xx_7zip_folder_decode_output(context,
                context->folder->bind_outputs[bind], data, size);
        }
    }
    for (packed = 0U; packed < context->folder->packed_count; ++packed) {
        if (context->folder->packed_indices[packed] == input_index) break;
    }
    if (packed == context->folder->packed_count ||
        context->folder->first_pack_index > UINT64_MAX - packed) return false;
    if (!context->input_data[input_index]) {
        uint64_t global_pack = context->folder->first_pack_index + packed;
        if (!xx_7zip_pack_offset(context->format, context->streams, global_pack,
                                 &offset, &packed_size) || packed_size > SIZE_MAX ||
            (context->streams->pack_crc_defined[global_pack] &&
             !xx_crc_verify_device(context->format->device, offset, (int64_t)packed_size,
                                   XX_CRC_TYPE_CRC32,
                                   context->streams->pack_crcs[global_pack], context->pd))) {
            return false;
        }
        context->input_data[input_index] = (uint8_t *)xx_mem_alloc(
            packed_size ? (size_t)packed_size : 1U);
        if (!context->input_data[input_index] ||
            !xx_7zip_read_exact_at(context->format->device, offset,
                                   context->input_data[input_index], (size_t)packed_size)) {
            if (context->input_data[input_index]) {
                xx_mem_free(context->input_data[input_index]);
                context->input_data[input_index] = NULL;
            }
            return false;
        }
        context->input_sizes[input_index] = (size_t)packed_size;
    }
    *data = context->input_data[input_index];
    *size = context->input_sizes[input_index];
    return true;
}

static bool xx_7zip_folder_decode_output(xx_7zip_folder_decode *context,
                                          uint64_t output_index,
                                          const uint8_t **data, size_t *size) {
    uint64_t input_base = 0U;
    uint64_t output_base = 0U;
    size_t coder_index;
    const xx_7zip_coder *coder = NULL;
    const uint8_t *input;
    size_t input_size;
    uint64_t expected64;
    if (!context || !data || !size || output_index >= context->folder->total_out) return false;
    if (context->output_data[output_index]) {
        *data = context->output_data[output_index];
        *size = context->output_sizes[output_index];
        return true;
    }
    if (context->output_busy[output_index]) return false;
    for (coder_index = 0U; coder_index < context->folder->coder_count; ++coder_index) {
        const xx_7zip_coder *candidate = &context->folder->coders[coder_index];
        if (output_index >= output_base && output_index < output_base + candidate->num_out) {
            coder = candidate;
            break;
        }
        input_base += candidate->num_in;
        output_base += candidate->num_out;
    }
    if (!coder || coder->num_in != 1U || coder->num_out != 1U ||
        coder->method == XX_7ZIP_METHOD_BCJ2 ||
        output_index != output_base) return false;
    expected64 = context->folder->unpack_sizes[output_index];
    if (expected64 > SIZE_MAX || expected64 > XX_7ZIP_MAX_FOLDER_OUTPUT ||
        !xx_7zip_folder_decode_input(context, input_base, &input, &input_size)) return false;
    context->output_busy[output_index] = 1U;
    context->output_data[output_index] = (uint8_t *)xx_mem_alloc(
        expected64 ? (size_t)expected64 : 1U);
    if (!context->output_data[output_index] ||
        !xx_7zip_decode_coder_memory(context->format, coder, input, input_size,
                                     context->output_data[output_index],
                                     (size_t)expected64, context->pd)) {
        if (context->output_data[output_index]) {
            xx_mem_free(context->output_data[output_index]);
            context->output_data[output_index] = NULL;
        }
        context->output_busy[output_index] = 0U;
        return false;
    }
    context->output_busy[output_index] = 0U;
    context->output_sizes[output_index] = (size_t)expected64;
    *data = context->output_data[output_index];
    *size = context->output_sizes[output_index];
    return true;
}

static void xx_7zip_folder_decode_cleanup(xx_7zip_folder_decode *context) {
    uint64_t i;
    if (!context || !context->folder) return;
    for (i = 0U; i < context->folder->total_in; ++i) {
        if (context->input_data && context->input_data[i]) {
            xx_mem_zero(context->input_data[i], context->input_sizes[i]);
            xx_mem_free(context->input_data[i]);
        }
    }
    for (i = 0U; i < context->folder->total_out; ++i) {
        if (context->output_data && context->output_data[i]) {
            xx_mem_zero(context->output_data[i], context->output_sizes[i]);
            xx_mem_free(context->output_data[i]);
        }
    }
    if (context->input_data) xx_mem_free(context->input_data);
    if (context->input_sizes) xx_mem_free(context->input_sizes);
    if (context->output_data) xx_mem_free(context->output_data);
    if (context->output_sizes) xx_mem_free(context->output_sizes);
    if (context->output_busy) xx_mem_free(context->output_busy);
    xx_mem_zero(context, sizeof(*context));
}

static bool xx_7zip_decode_bcj2_folder(Abstractformat *self,
                                        const xx_7zip_streams *streams,
                                        const xx_7zip_folder *folder,
                                        xx_io_device *destination,
                                        xx_pd_struct *pd) {
    xx_7zip_folder_decode context;
    const xx_7zip_coder *bcj2 = NULL;
    const uint8_t *inputs[4];
    size_t input_sizes[4];
    uint64_t input_base = 0U;
    uint64_t output_base = 0U;
    size_t coder_index;
    uint8_t *output = NULL;
    bool result = false;
    if (!self || !streams || !folder || !destination || folder->unpack_size > SIZE_MAX ||
        folder->unpack_size > XX_7ZIP_MAX_FOLDER_OUTPUT) return false;
    xx_mem_zero(&context, sizeof(context));
    context.format = self; context.streams = streams; context.folder = folder; context.pd = pd;
    context.input_data = (uint8_t **)xx_7zip_alloc_array(folder->total_in, sizeof(uint8_t *));
    context.input_sizes = (size_t *)xx_7zip_alloc_array(folder->total_in, sizeof(size_t));
    context.output_data = (uint8_t **)xx_7zip_alloc_array(folder->total_out, sizeof(uint8_t *));
    context.output_sizes = (size_t *)xx_7zip_alloc_array(folder->total_out, sizeof(size_t));
    context.output_busy = (uint8_t *)xx_7zip_alloc_array(folder->total_out, sizeof(uint8_t));
    if (!context.input_data || !context.input_sizes || !context.output_data ||
        !context.output_sizes || !context.output_busy) goto cleanup;
    for (coder_index = 0U; coder_index < folder->coder_count; ++coder_index) {
        const xx_7zip_coder *candidate = &folder->coders[coder_index];
        if (candidate->method == XX_7ZIP_METHOD_BCJ2) {
            bcj2 = candidate;
            break;
        }
        input_base += candidate->num_in;
        output_base += candidate->num_out;
    }
    if (!bcj2 || bcj2->num_in != 4U || bcj2->num_out != 1U ||
        output_base != folder->final_output_index) goto cleanup;
    for (coder_index = 0U; coder_index < 4U; ++coder_index) {
        if (!xx_7zip_folder_decode_input(&context, input_base + coder_index,
                                         &inputs[coder_index], &input_sizes[coder_index])) goto cleanup;
    }
    output = (uint8_t *)xx_mem_alloc(folder->unpack_size ? (size_t)folder->unpack_size : 1U);
    if (!output || !xx_7zip_bcj2_decode(inputs, input_sizes, bcj2->properties,
                                         bcj2->properties_size, output,
                                         (size_t)folder->unpack_size) ||
        (folder->unpack_size && xx_io_write(destination, output,
                                            (size_t)folder->unpack_size) !=
                               (ssize_t)folder->unpack_size) ||
        xx_io_total_size(destination) != (int64_t)folder->unpack_size ||
        (folder->crc_defined && !xx_crc_verify_device(destination, 0,
            (int64_t)folder->unpack_size, XX_CRC_TYPE_CRC32, folder->crc, pd))) goto cleanup;
    result = true;
cleanup:
    if (output) { xx_mem_zero(output, (size_t)folder->unpack_size); xx_mem_free(output); }
    xx_7zip_folder_decode_cleanup(&context);
    return result;
}

static bool xx_7zip_decode_folder(Abstractformat *self, const xx_7zip_streams *streams,
                                  uint64_t folder_index, xx_io_device *destination,
                                  xx_pd_struct *pd) {
    const xx_7zip_folder *folder;
    uint8_t *current = NULL;
    size_t current_size;
    uint64_t input_index;
    uint64_t visited = 0U;
    size_t stages = 0U;
    int64_t source_offset;
    uint64_t compressed_size;
    bool result = false;

    if (!self || !self->device || !streams || !destination ||
        folder_index >= streams->num_folders) return false;
    folder = &streams->folders[folder_index];
    if (!folder->supported ||
        !folder->packed_indices || !folder->unpack_sizes ||
        folder->coder_count == 0U || folder->coder_count > 63U ||
        folder->unpack_size > (uint64_t)INT64_MAX) return false;
    for (input_index = 0U; input_index < folder->coder_count; ++input_index) {
        if (folder->coders[input_index].method == XX_7ZIP_METHOD_BCJ2) {
            return xx_7zip_decode_bcj2_folder(self, streams, folder, destination, pd);
        }
    }
    if (folder->packed_count != 1U ||
        !xx_7zip_pack_offset(self, streams, folder->first_pack_index,
                             &source_offset, &compressed_size) ||
        compressed_size > SIZE_MAX || compressed_size > XX_7ZIP_MAX_FOLDER_OUTPUT + 16U ||
        folder->unpack_size > (uint64_t)INT64_MAX) return false;
    if (streams->pack_crc_defined[folder->first_pack_index] &&
        !xx_crc_verify_device(self->device, source_offset, (int64_t)compressed_size,
                              XX_CRC_TYPE_CRC32,
                              streams->pack_crcs[folder->first_pack_index], pd)) return false;

    current_size = (size_t)compressed_size;
    current = (uint8_t *)xx_mem_alloc(current_size ? current_size : 1U);
    if (!current || !xx_7zip_read_exact_at(self->device, source_offset,
                                           current, current_size)) goto cleanup;
    input_index = folder->packed_indices[0];

    while (stages < folder->coder_count) {
        const xx_7zip_coder *coder = NULL;
        size_t coder_index;
        uint64_t in_base = 0U;
        uint64_t out_base = 0U;
        uint64_t output_index = UINT64_MAX;
        uint64_t next_input = UINT64_MAX;
        uint64_t expected64;
        uint8_t *next;
        size_t expected;
        size_t bond;

        for (coder_index = 0U; coder_index < folder->coder_count; ++coder_index) {
            const xx_7zip_coder *candidate = &folder->coders[coder_index];
            if (input_index >= in_base && input_index < in_base + candidate->num_in) {
                if (candidate->num_in != 1U || candidate->num_out != 1U ||
                    (visited & (UINT64_C(1) << coder_index)) != 0U) goto cleanup;
                coder = candidate;
                output_index = out_base;
                visited |= UINT64_C(1) << coder_index;
                break;
            }
            in_base += candidate->num_in;
            out_base += candidate->num_out;
        }
        if (!coder || output_index >= folder->total_out) goto cleanup;
        expected64 = folder->unpack_sizes[output_index];
        if (expected64 > SIZE_MAX || expected64 > XX_7ZIP_MAX_FOLDER_OUTPUT) goto cleanup;
        expected = (size_t)expected64;
        next = (uint8_t *)xx_mem_alloc(expected ? expected : 1U);
        if (!next || !xx_7zip_decode_coder_memory(self, coder,
                                                   current, current_size,
                                                   next, expected, pd)) {
            if (next) xx_mem_free(next);
            goto cleanup;
        }
        xx_mem_zero(current, current_size);
        xx_mem_free(current);
        current = next;
        current_size = expected;
        ++stages;
        if (output_index == folder->final_output_index) break;
        for (bond = 0U; bond < folder->bind_count; ++bond) {
            if (folder->bind_outputs[bond] == output_index) {
                next_input = folder->bind_inputs[bond];
                break;
            }
        }
        if (next_input == UINT64_MAX) goto cleanup;
        input_index = next_input;
    }
    if (stages != folder->coder_count || current_size != (size_t)folder->unpack_size ||
        (current_size && xx_io_write(destination, current, current_size) !=
                             (ssize_t)current_size) ||
        xx_io_total_size(destination) != (int64_t)folder->unpack_size) goto cleanup;
    if (folder->crc_defined &&
        !xx_crc_verify_device(destination, 0, (int64_t)folder->unpack_size,
                              XX_CRC_TYPE_CRC32, folder->crc, pd)) goto cleanup;
    result = true;

cleanup:
    if (current) {
        xx_mem_zero(current, current_size);
        xx_mem_free(current);
    }
    return result;
}

static bool xx_7zip_decode_encoded_header(Abstractformat *self,
                                          const uint8_t *data, size_t size,
                                          uint8_t **decoded, size_t *decoded_size) {
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
    if (!xx_7zip_reader_u8(&r, &nid) || nid != XX_7ZIP_NID_ENCODED_HEADER ||
        !xx_7zip_parse_streams_info(&r, &streams) || r.pos != r.size ||
        streams.num_folders != 1 || streams.num_substreams != 1 ||
        streams.folders[0].unpack_size == 0 ||
        streams.folders[0].unpack_size > XX_7ZIP_MAX_HEADER_SIZE ||
        streams.folders[0].unpack_size > SIZE_MAX) goto cleanup;
    buffer = (uint8_t *)xx_mem_alloc((size_t)streams.folders[0].unpack_size);
    if (!buffer) goto cleanup;
    xx_7zip_buffer_device_init(&destination, buffer, (size_t)streams.folders[0].unpack_size);
    if (!xx_7zip_decode_folder(self, &streams, 0, &destination.device, NULL)) goto cleanup;
    if (streams.sub_crc_defined[0] &&
        xx_crc32(XX_CRC_TYPE_CRC32, buffer, (size_t)streams.sub_sizes[0]) != streams.sub_crcs[0]) goto cleanup;
    *decoded = buffer;
    *decoded_size = (size_t)streams.folders[0].unpack_size;
    buffer = NULL;
    result = true;

cleanup:
    if (buffer) xx_mem_free(buffer);
    xx_7zip_streams_cleanup(&streams);
    return result;
}

static bool xx_7zip_read_exact_at(xx_io_device *device, int64_t offset,
                                  void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0;
    if (!device || (!buffer && size) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, bytes + done, size - done);
        if (got <= 0) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_7zip_read_start_header(Abstractformat *self, uint8_t header[XX_7ZIP_SIGNATURE_HEADER_SIZE],
                                      int64_t *next_offset, uint64_t *next_size,
                                      uint32_t *next_crc) {
    int64_t device_size;
    uint64_t relative_offset;
    uint64_t absolute_offset;
    uint64_t absolute_end;
    uint32_t start_crc;
    if (!self || !self->device || !header || self->base_address < 0) return false;
    device_size = xx_io_total_size(self->device);
    if (device_size < 0 || (uint64_t)self->base_address > (uint64_t)device_size ||
        (uint64_t)device_size - (uint64_t)self->base_address < XX_7ZIP_SIGNATURE_HEADER_SIZE ||
        !xx_7zip_read_exact_at(self->device, self->base_address, header,
                               XX_7ZIP_SIGNATURE_HEADER_SIZE) ||
        xx_rt_memcmp(header, XX_7ZIP_SIGNATURE, XX_7ZIP_SIGNATURE_SIZE) != 0 ||
        header[6] != 0) return false;
    start_crc = xx_7zip_read_le32(header + 8);
    if (xx_crc32(XX_CRC_TYPE_CRC32, header + 12, 20U) != start_crc) return false;
    relative_offset = xx_7zip_read_le64(header + 12);
    *next_size = xx_7zip_read_le64(header + 20);
    *next_crc = xx_7zip_read_le32(header + 28);
    if (*next_size > XX_7ZIP_MAX_HEADER_SIZE ||
        (*next_size == 0 && (relative_offset != 0 || *next_crc != 0)) ||
        !xx_7zip_u64_add(XX_7ZIP_SIGNATURE_HEADER_SIZE, relative_offset, &absolute_offset) ||
        !xx_7zip_u64_add((uint64_t)self->base_address, absolute_offset, &absolute_offset) ||
        !xx_7zip_u64_add(absolute_offset, *next_size, &absolute_end) ||
        absolute_offset > INT64_MAX || absolute_end > (uint64_t)device_size) return false;
    *next_offset = (int64_t)absolute_offset;
    return true;
}

void xx_7zip_init(xx_7zip *archive, xx_io_device *dev, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, dev, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_7ZIP;
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
    archive->format.data_struct_id_to_string = xx_7zip_data_struct_id_to_string;
    archive->format.data_struct_string_to_id = xx_7zip_data_struct_string_to_id;
    archive->format.create_data_structs_reading = xx_7zip_create_data_structs_reading;
    archive->format.get_current_data_struct = xx_7zip_get_current_data_struct;
    archive->format.data_struct_move_to_next = xx_7zip_data_struct_move_to_next;
    archive->format.free_data_structs_reading = xx_7zip_free_data_structs_reading;
    archive->format.create_data_struct_records_reading = xx_7zip_create_data_struct_records_reading;
    archive->format.get_current_data_struct_record = xx_7zip_get_current_data_struct_record;
    archive->format.data_struct_record_move_to_next = xx_7zip_data_struct_record_move_to_next;
    archive->format.free_data_struct_records_reading = xx_7zip_free_data_struct_records_reading;
    archive->format.destroy = xx_7zip_vtable_destroy;
    archive->next_header_offset = -1;
}

xx_7zip *xx_7zip_create(xx_io_device *dev, int64_t base_address) {
    xx_7zip *archive = (xx_7zip *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_7zip_init(archive, dev, base_address);
    return archive;
}

static bool xx_7zip_get_password_utf16le(const Abstractformat *self,
                                         uint8_t **password,
                                         size_t *password_size) {
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
    if (value->type == XX_VAR_TYPE_STRING ||
        value->type == XX_VAR_TYPE_STRING_VIEW ||
        value->type == XX_VAR_TYPE_BYTES ||
        value->type == XX_VAR_TYPE_BYTES_VIEW) {
        const uint8_t *source;
        size_t source_size;
        char *utf8;
        if (value->type == XX_VAR_TYPE_BYTES ||
            value->type == XX_VAR_TYPE_BYTES_VIEW) {
            source = (const uint8_t *)xx_var_get_bytes(value, &source_size);
        } else {
            source = (const uint8_t *)xx_var_get_str(value);
            source_size = value->val.str.len;
        }
        if ((!source && source_size != 0U) || source_size == SIZE_MAX ||
            (source_size != 0U && xx_rt_memchr(source, 0, source_size))) {
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
    } else if (value->type == XX_VAR_TYPE_WSTRING ||
               value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        const wchar_t *source = xx_var_get_wstr(value);
        length = value->val.wstr.len;
        if ((!source && length != 0U) ||
            length > (SIZE_MAX / sizeof(wchar_t)) - 1U) {
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

void xx_7zip_destroy(xx_7zip *archive) {
    if (!archive) return;
    if (archive->internal) {
        xx_7zip_private_free((xx_7zip_private *)archive->internal);
        archive->internal = NULL;
    }
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void xx_7zip_vtable_destroy(Abstractformat *self) {
    if (self) xx_7zip_destroy((xx_7zip *)self);
}

void xx_7zip_free(xx_7zip *archive) {
    if (!archive) return;
    xx_7zip_destroy(archive);
    xx_mem_free(archive);
}

bool xx_7zip_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t header[XX_7ZIP_SIGNATURE_HEADER_SIZE];
    int64_t next_offset;
    uint64_t next_size;
    uint32_t next_crc;
    uint64_t calculated;
    (void)pd;
    if (!xx_7zip_read_start_header(self, header, &next_offset, &next_size, &next_crc)) return false;
    if (next_size == 0) return next_crc == 0;
    return xx_crc_calculate_device_by_type(self->device, next_offset, (int64_t)next_size,
                                           XX_CRC_TYPE_CRC32, NULL, &calculated) &&
           (uint32_t)calculated == next_crc;
}

bool xx_7zip_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
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
    if (!xx_7zip_read_start_header(self, signature_header, &next_offset, &next_size, &next_crc) ||
        next_size > SIZE_MAX) return false;
    if (next_size == 0) {
        if (next_crc != 0) return false;
    } else if (!xx_crc_calculate_device_by_type(self->device, next_offset,
                                                (int64_t)next_size,
                                                XX_CRC_TYPE_CRC32, NULL,
                                                &calculated) ||
               (uint32_t)calculated != next_crc) {
        return false;
    }
    priv = (xx_7zip_private *)xx_mem_calloc(1, sizeof(*priv));
    if (!priv) goto cleanup;
    archive->is_header_encoded = false;
    if (next_size != 0) {
        stored_header = (uint8_t *)xx_mem_alloc((size_t)next_size);
        if (!stored_header ||
            !xx_7zip_read_exact_at(self->device, next_offset, stored_header,
                                   (size_t)next_size)) goto cleanup;
        archive->is_header_encoded = stored_header[0] == XX_7ZIP_NID_ENCODED_HEADER;
        if (archive->is_header_encoded) {
            if (!xx_7zip_decode_encoded_header(self, stored_header, (size_t)next_size,
                                               &decoded_header, &plain_size)) goto cleanup;
            plain_header = decoded_header;
        } else {
            plain_header = stored_header;
            plain_size = (size_t)next_size;
        }
        if (!xx_7zip_parse_plain_header(plain_header, plain_size, priv)) goto cleanup;
    }

    physical_end = (uint64_t)next_offset + next_size;
    for (i = 0; i < priv->streams.num_packs; ++i) {
        int64_t pack_offset;
        uint64_t pack_size;
        uint64_t pack_end;
        if (!xx_7zip_pack_offset(self, &priv->streams, i, &pack_offset, &pack_size) ||
            !xx_7zip_u64_add((uint64_t)pack_offset, pack_size, &pack_end)) goto cleanup;
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
    self->is_crypted = false;
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

int64_t xx_7zip_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self) return -1;
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) return -1;
    return self->format_size;
}

uint64_t xx_7zip_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) {
    if (!self) return 0;
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) return 0;
    return ((xx_7zip *)self)->number_of_records;
}

uint64_t xx_7zip_get_number_of_records(const xx_7zip *archive) {
    return archive ? archive->number_of_records : 0;
}

int64_t xx_7zip_get_next_header_offset(const xx_7zip *archive) {
    return archive ? archive->next_header_offset : -1;
}

uint64_t xx_7zip_get_next_header_size(const xx_7zip *archive) {
    return archive ? archive->next_header_size : 0;
}

bool xx_7zip_is_header_encoded(const xx_7zip *archive) {
    return archive ? archive->is_header_encoded : false;
}

static bool xx_7zip_copy_options(xx_archive_record_state *state, const xx_list_s *options) {
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

static const xx_var *xx_7zip_find_option(const xx_list_s *options, uint32_t meta_id) {
    size_t i;
    if (!options) return NULL;
    for (i = 0; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

static bool xx_7zip_apply_password_option(Abstractformat *format,
                                           const xx_list_s *options) {
    const xx_var *value = xx_7zip_find_option(options, XX_META_ID_OPT_PASSWORD);
    if (!value) return true;
    return xx_format_set_extra_parameter(format, XX_META_ID_OPT_PASSWORD,
                                         value);
}

static bool xx_7zip_folder_pack_region(Abstractformat *self, const xx_7zip_streams *streams,
                                       const xx_7zip_folder *folder, int64_t *offset,
                                       uint64_t *total_size) {
    uint64_t i;
    uint64_t sum = 0;
    uint64_t ignored;
    if (!folder || folder->packed_count == 0 ||
        !xx_7zip_pack_offset(self, streams, folder->first_pack_index, offset, &ignored)) return false;
    for (i = 0; i < folder->packed_count; ++i) {
        uint64_t index = folder->first_pack_index + i;
        if (index >= streams->num_packs || !xx_7zip_u64_add(sum, streams->pack_sizes[index], &sum)) return false;
    }
    *total_size = sum;
    return true;
}

static bool xx_7zip_populate_archive_record(Abstractformat *self, uint64_t index,
                                             xx_archive_record *record) {
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
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, file->is_folder) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, encrypted)) {
        xx_archive_record_cleanup(record);
        return false;
    }
    if (file->crc_defined && !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, file->crc)) {
        xx_archive_record_cleanup(record);
        return false;
    }
    if (file->attributes_defined &&
        (!xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, file->attributes) ||
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

xx_archive_record_state *xx_7zip_create_archive_records_reading(Abstractformat *self,
                                                                 const xx_list_s *options,
                                                                 xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_7zip *archive;
    if (!self || !self->device) return NULL;
    archive = (xx_7zip *)self;
    if (!xx_7zip_apply_password_option(self, options)) return NULL;
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) return NULL;
    if (!self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_7zip_copy_options(state, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->total_records = archive->number_of_records <= INT64_MAX ?
                           (int64_t)archive->number_of_records : INT64_MAX;
    if (archive->number_of_records && xx_7zip_populate_archive_record(self, 0, &state->current_record)) {
        state->current_index = 0;
        state->has_record = true;
    } else if (archive->number_of_records) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

const xx_archive_record *xx_7zip_get_current_archive_record(Abstractformat *self,
                                                             xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_7zip_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
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

static bool xx_7zip_validate_member_name(const wchar_t *name) {
    const wchar_t *segment;
    const wchar_t *p;
    if (!name || !name[0] || name[0] == L'/' || name[0] == L'\\') return false;
    segment = name;
    for (p = name; ; ++p) {
        if (*p == L':') return false;
        if (*p == L'/' || *p == L'\\' || *p == L'\0') {
            size_t length = (size_t)(p - segment);
            if (length == 2U && segment[0] == L'.' && segment[1] == L'.') return false;
            if (*p == L'\0') break;
            segment = p + 1;
        }
    }
    return true;
}

static wchar_t *xx_7zip_normalized_name(const wchar_t *name) {
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

static bool xx_7zip_extract_file_to_device(Abstractformat *self,
                                           const xx_7zip_file *file,
                                           xx_io_device *output,
                                           xx_pd_struct *pd) {
    xx_7zip_private *priv = (xx_7zip_private *)((xx_7zip *)self)->internal;
    xx_7zip_folder *folder;
    uint8_t *buffer = NULL;
    xx_7zip_buffer_device memory;
    uint64_t end;
    bool result = false;
    if (!priv || !file || file->is_folder || file->anti) return false;
    if (file->empty_stream) return file->size == 0;
    if (file->folder_index < 0 || (uint64_t)file->folder_index >= priv->streams.num_folders) return false;
    folder = &priv->streams.folders[file->folder_index];
    if (!folder->supported || folder->unpack_size > XX_7ZIP_MAX_FOLDER_OUTPUT ||
        folder->unpack_size > SIZE_MAX ||
        !xx_7zip_u64_add(file->offset_in_folder, file->size, &end) || end > folder->unpack_size) return false;
    buffer = (uint8_t *)xx_mem_alloc((size_t)(folder->unpack_size ? folder->unpack_size : 1U));
    if (!buffer) return false;
    xx_7zip_buffer_device_init(&memory, buffer, (size_t)folder->unpack_size);
    if (!xx_7zip_decode_folder(self, &priv->streams, (uint64_t)file->folder_index,
                               &memory.device, pd)) goto cleanup;
    if (file->crc_defined &&
        xx_crc32(XX_CRC_TYPE_CRC32, buffer + (size_t)file->offset_in_folder,
                 (size_t)file->size) != file->crc) goto cleanup;
    if (output && file->size &&
        xx_io_write(output, buffer + (size_t)file->offset_in_folder, (size_t)file->size) != (ssize_t)file->size) goto cleanup;
    result = true;

cleanup:
    if (buffer) xx_mem_free(buffer);
    return result;
}

bool xx_7zip_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_7zip_private *priv;
    const xx_7zip_file *file;
    const xx_var *path_option;
    const wchar_t *base_w = NULL;
    const char *base_a = NULL;
    wchar_t *base_copy = NULL;
    wchar_t *name = NULL;
    wchar_t *full_path = NULL;
    char *utf8_path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record || state->current_index < 0) return false;
    priv = (xx_7zip_private *)((xx_7zip *)self)->internal;
    if (!priv || (uint64_t)state->current_index >= priv->num_files) return false;
    file = &priv->files[state->current_index];
    path_option = xx_7zip_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (path_option) {
        if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW)
            base_w = xx_var_get_wstr(path_option);
        else if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
            base_a = xx_var_get_str(path_option);
    }
    if (!base_w && !base_a) {
        if (file->is_folder) return true;
        return xx_7zip_extract_file_to_device(self, file, NULL, pd);
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
        xx_io_device *output;
        if (!xx_store_create_dirs_w(full_path, false)) goto cleanup;
        utf8_path = xx_str_unicode_to_utf8(full_path);
        if (!utf8_path) goto cleanup;
        output = xx_io_file_open(utf8_path, "w+b");
        created = output != NULL;
        if (!output) goto cleanup;
        result = xx_7zip_extract_file_to_device(self, file, output, pd);
        if (xx_io_close(output) != 0) result = false;
        if (!result && created) xx_rt_remove(utf8_path);
    }

cleanup:
    if (utf8_path) xx_str_free(utf8_path);
    if (full_path) xx_str_wfree(full_path);
    if (name) xx_str_wfree(name);
    if (base_copy) xx_str_wfree(base_copy);
    return result;
}

void xx_7zip_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

typedef struct xx_7zip_ds_state_s {
    xx_data_struct *items;
    size_t count;
} xx_7zip_ds_state;

static void xx_7zip_ds_state_free(void *ptr) {
    xx_7zip_ds_state *internal = (xx_7zip_ds_state *)ptr;
    if (!internal) return;
    if (internal->items) xx_mem_free(internal->items);
    xx_mem_free(internal);
}

const char *xx_7zip_data_struct_id_to_string(Abstractformat *self, uint32_t id) {
    (void)self;
    switch ((xx_7zip_data_struct_id_t)id) {
        case XX_7ZIP_DS_SIGNATURE_HEADER: return "SIGNATURE_HEADER";
        case XX_7ZIP_DS_PACKED_DATA: return "PACKED_DATA";
        case XX_7ZIP_DS_NEXT_HEADER: return "NEXT_HEADER";
        default: return "UNKNOWN";
    }
}

uint32_t xx_7zip_data_struct_string_to_id(Abstractformat *self, const char *name) {
    (void)self;
    if (!name) return XX_7ZIP_DS_UNKNOWN;
    if (xx_str_cmp(name, "SIGNATURE_HEADER") == 0) return XX_7ZIP_DS_SIGNATURE_HEADER;
    if (xx_str_cmp(name, "PACKED_DATA") == 0) return XX_7ZIP_DS_PACKED_DATA;
    if (xx_str_cmp(name, "NEXT_HEADER") == 0) return XX_7ZIP_DS_NEXT_HEADER;
    return XX_7ZIP_DS_UNKNOWN;
}

xx_data_struct_state *xx_7zip_create_data_structs_reading(Abstractformat *self,
                                                           xx_pd_struct *pd) {
    xx_7zip *archive;
    xx_7zip_private *priv;
    xx_data_struct_state *state = NULL;
    xx_7zip_ds_state *internal = NULL;
    size_t capacity;
    uint64_t i;
    if (!self || !self->device) return NULL;
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) return NULL;
    archive = (xx_7zip *)self;
    priv = (xx_7zip_private *)archive->internal;
    if (!self->is_valid || !priv || priv->streams.num_packs > SIZE_MAX - 2U) return NULL;
    capacity = (size_t)priv->streams.num_packs + 2U;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    internal = (xx_7zip_ds_state *)xx_mem_calloc(1, sizeof(*internal));
    if (!state || !internal) goto fail;
    xx_data_struct_state_init(state, self);
    internal->items = (xx_data_struct *)xx_mem_calloc(capacity, sizeof(xx_data_struct));
    if (!internal->items) goto fail;
    {
        xx_data_struct *item = &internal->items[internal->count++];
        item->id = XX_7ZIP_DS_SIGNATURE_HEADER;
        item->offset = self->base_address;
        item->address = self->is_mapped ? self->base_address : -1;
        item->entry_size = XX_7ZIP_SIGNATURE_HEADER_SIZE;
        item->total_size = XX_7ZIP_SIGNATURE_HEADER_SIZE;
        item->count = 1;
        item->type = XX_DATA_STRUCT_TYPE_STRUCT;
    }
    for (i = 0; i < priv->streams.num_packs; ++i) {
        int64_t offset;
        uint64_t size;
        xx_data_struct *item;
        if (!xx_7zip_pack_offset(self, &priv->streams, i, &offset, &size) || size > INT64_MAX) goto fail;
        item = &internal->items[internal->count++];
        item->id = XX_7ZIP_DS_PACKED_DATA;
        item->offset = offset;
        item->address = self->is_mapped ? offset : -1;
        item->entry_size = (int64_t)size;
        item->total_size = (int64_t)size;
        item->count = 1;
        item->type = XX_DATA_STRUCT_TYPE_RAW_DATA;
    }
    {
        xx_data_struct *item = &internal->items[internal->count++];
        item->id = XX_7ZIP_DS_NEXT_HEADER;
        item->offset = archive->next_header_offset;
        item->address = self->is_mapped ? archive->next_header_offset : -1;
        item->entry_size = archive->next_header_size <= INT64_MAX ? (int64_t)archive->next_header_size : -1;
        item->total_size = item->entry_size;
        item->count = 1;
        item->type = XX_DATA_STRUCT_TYPE_FOOTER;
    }
    state->internal_state = internal;
    state->free_internal = xx_7zip_ds_state_free;
    state->total_structs = (int64_t)internal->count;
    state->current_index = 0;
    state->current_struct = internal->items[0];
    state->has_struct = true;
    return state;

fail:
    if (internal) xx_7zip_ds_state_free(internal);
    if (state) xx_data_struct_state_free(state);
    return NULL;
}

const xx_data_struct *xx_7zip_get_current_data_struct(Abstractformat *self,
                                                       xx_data_struct_state *state) {
    return self && state && state->format == self && state->has_struct
               ? &state->current_struct
               : NULL;
}

bool xx_7zip_data_struct_move_to_next(Abstractformat *self,
                                      xx_data_struct_state *state,
                                      xx_pd_struct *pd) {
    xx_7zip_ds_state *internal;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_struct || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) return false;
    internal = (xx_7zip_ds_state *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= internal->count) {
        state->has_struct = false;
        return false;
    }
    state->current_index = next;
    state->current_struct = internal->items[next];
    state->has_struct = true;
    return true;
}

void xx_7zip_free_data_structs_reading(Abstractformat *self,
                                       xx_data_struct_state *state) {
    (void)self;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc XX_7ZIP_SIGNATURE_FIELDS[] = {
    {L"signature_part_1", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"signature_part_2", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"major_version", L"uint8", 6, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"minor_version", L"uint8", 7, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"start_header_crc", L"uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"next_header_offset", L"uint64", 12, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
    {L"next_header_size", L"uint64", 20, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"next_header_crc", L"uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE}
};

static const xx_data_struct_field_desc XX_7ZIP_NEXT_HEADER_FIELDS[] = {
    {L"nid", L"uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

typedef struct xx_7zip_record_state_s {
    const xx_data_struct_field_desc *fields;
    size_t count;
} xx_7zip_record_state;

static void xx_7zip_record_state_free(void *ptr) {
    if (ptr) xx_mem_free(ptr);
}

xx_data_struct_record_state *xx_7zip_create_data_struct_records_reading(Abstractformat *self,
                                                                         const xx_data_struct *ds,
                                                                         xx_pd_struct *pd) {
    xx_data_struct_record_state *state;
    xx_7zip_record_state *internal;
    const xx_data_struct_field_desc *fields;
    size_t count;
    (void)pd;
    if (!self || !self->device || !ds) return NULL;
    if (ds->id == XX_7ZIP_DS_SIGNATURE_HEADER) {
        fields = XX_7ZIP_SIGNATURE_FIELDS;
        count = sizeof(XX_7ZIP_SIGNATURE_FIELDS) / sizeof(XX_7ZIP_SIGNATURE_FIELDS[0]);
    } else if (ds->id == XX_7ZIP_DS_NEXT_HEADER) {
        fields = XX_7ZIP_NEXT_HEADER_FIELDS;
        count = sizeof(XX_7ZIP_NEXT_HEADER_FIELDS) / sizeof(XX_7ZIP_NEXT_HEADER_FIELDS[0]);
    } else {
        return NULL;
    }
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_data_struct_record_state_init(state, self, ds);
    internal = (xx_7zip_record_state *)xx_mem_alloc(sizeof(*internal));
    if (!internal) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    internal->fields = fields;
    internal->count = count;
    state->internal_state = internal;
    state->free_internal = xx_7zip_record_state_free;
    state->total_records = (int64_t)count;
    state->current_index = 0;
    state->has_record = xx_data_struct_record_populate(&state->current_record, self->device,
                                                       ds->offset, &fields[0], false);
    if (!state->has_record) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    return state;
}

const xx_data_struct_record *xx_7zip_get_current_data_struct_record(Abstractformat *self,
                                                                    xx_data_struct_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_7zip_data_struct_record_move_to_next(Abstractformat *self,
                                             xx_data_struct_record_state *state,
                                             xx_pd_struct *pd) {
    xx_7zip_record_state *internal;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) return false;
    internal = (xx_7zip_record_state *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= internal->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_data_struct_record_populate(&state->current_record, self->device,
                                        state->parent_struct.offset,
                                        &internal->fields[next], false)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    state->has_record = true;
    return true;
}

void xx_7zip_free_data_struct_records_reading(Abstractformat *self,
                                               xx_data_struct_record_state *state) {
    (void)self;
    xx_data_struct_record_state_free(state);
}
