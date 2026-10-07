/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_7zip_stream.h"
#include "xx_7zip_defs.h"
#include "xx_7zip_branch.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/brotli/xx_brotli.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/lz5/xx_lz5.h"
#include "xxfclib/algo/lizard/xx_lizard.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/algo/aes/xx_aes.h"
#include "../../algo/deflate/xx_deflate_internal.h"
#include "../../algo/ppmd7/xx_ppmd7_internal.h"
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_7ZIP_STREAM_BUFFER (64U * 1024U)

typedef struct xx_7zip_stream_window_s {
    xx_io_device device;
    xx_io_device *source;
    int64_t offset;
    uint64_t size, position;
    xx_pd_struct *pd;
} xx_7zip_stream_window;

typedef struct xx_7zip_stream_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t size, position;
    xx_pd_struct *pd;
} xx_7zip_stream_sink;

static ssize_t xx_7zip_stream_read(xx_io_device *device, void *buffer, size_t size) {
    xx_7zip_stream_window *window = (xx_7zip_stream_window *)device->priv;
    ssize_t count;
    uint64_t remaining;
    if (!window || (!buffer && size) || size > (size_t)PTRDIFF_MAX ||
        xx_pd_is_stopped(window->pd)) return -1;
    remaining = window->size - window->position;
    if ((uint64_t)size > remaining) size = (size_t)remaining;
    if (size == 0U) return 0;
    if (xx_io_seek64(window->source, window->offset + (int64_t)window->position, SEEK_SET) != 0)
        return -1;
    count = xx_io_read(window->source, buffer, size);
    if (count < 0 || (size_t)count > size) return -1;
    window->position += (uint64_t)count;
    return count;
}

static int xx_7zip_stream_seek64(xx_io_device *device, int64_t offset, int origin) {
    xx_7zip_stream_window *window = (xx_7zip_stream_window *)device->priv;
    uint64_t base, next;
    if (!window) return -1;
    if (origin == SEEK_SET) base = 0U;
    else if (origin == SEEK_CUR) base = window->position;
    else if (origin == SEEK_END) base = window->size;
    else return -1;
    if (offset < 0) {
        uint64_t backwards = (uint64_t)(-(offset + 1)) + 1U;
        if (backwards > base) return -1;
        next = base - backwards;
    } else {
        if ((uint64_t)offset > window->size - base) return -1;
        next = base + (uint64_t)offset;
    }
    if (next > window->size) return -1;
    window->position = next;
    return 0;
}

static int xx_7zip_stream_seek(xx_io_device *device, long offset, int origin) {
    return xx_7zip_stream_seek64(device, (int64_t)offset, origin);
}

static int64_t xx_7zip_stream_tell(xx_io_device *device) {
    xx_7zip_stream_window *window = (xx_7zip_stream_window *)device->priv;
    return window ? (int64_t)window->position : -1;
}

static int64_t xx_7zip_stream_size(xx_io_device *device) {
    xx_7zip_stream_window *window = (xx_7zip_stream_window *)device->priv;
    return window ? (int64_t)window->size : -1;
}

static bool xx_7zip_stream_window_init(xx_7zip_stream_window *window,
                                       xx_io_device *source, int64_t offset,
                                       uint64_t size, xx_pd_struct *pd) {
    if (!window || !source || offset < 0 || size > (uint64_t)INT64_MAX - (uint64_t)offset)
        return false;
    xx_mem_zero(window, sizeof(*window));
    window->source = source; window->offset = offset; window->size = size; window->pd = pd;
    window->device.priv = window;
    window->device.read = xx_7zip_stream_read;
    window->device.seek = xx_7zip_stream_seek;
    window->device.seek64 = xx_7zip_stream_seek64;
    window->device.tell = xx_7zip_stream_tell;
    window->device.total_size = xx_7zip_stream_size;
    return true;
}

static ssize_t xx_7zip_stream_write(xx_io_device *device, const void *buffer, size_t size) {
    xx_7zip_stream_sink *sink = (xx_7zip_stream_sink *)device->priv;
    ssize_t count;
    if (!sink || (!buffer && size) || size > (size_t)PTRDIFF_MAX ||
        (uint64_t)size > sink->size - sink->position || xx_pd_is_stopped(sink->pd)) return -1;
    count = xx_io_write(sink->target, buffer, size);
    if (count < 0 || (size_t)count > size) return -1;
    sink->position += (uint64_t)count;
    return count;
}

static int64_t xx_7zip_stream_output_size(xx_io_device *device) {
    xx_7zip_stream_sink *sink = (xx_7zip_stream_sink *)device->priv;
    return sink ? (int64_t)sink->position : -1;
}

static void xx_7zip_stream_sink_init(xx_7zip_stream_sink *sink, xx_io_device *target,
                                     uint64_t size, xx_pd_struct *pd) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target; sink->size = size; sink->pd = pd;
    sink->device.priv = sink;
    sink->device.write = xx_7zip_stream_write;
    sink->device.total_size = xx_7zip_stream_output_size;
}

static bool xx_7zip_stream_read_exact(xx_io_device *source, uint8_t *buffer, size_t size) {
    while (size != 0U) {
        size_t request = size < XX_7ZIP_STREAM_BUFFER ? size : XX_7ZIP_STREAM_BUFFER;
        ssize_t count = xx_io_read(source, buffer, request);
        if (count <= 0 || (size_t)count > request) return false;
        buffer += count;
        size -= (size_t)count;
    }
    return true;
}

static bool xx_7zip_stream_write_exact(xx_io_device *destination,
                                       const uint8_t *buffer, size_t size) {
    while (size != 0U) {
        ssize_t count = xx_io_write(destination, buffer, size);
        if (count <= 0 || (size_t)count > size) return false;
        buffer += count;
        size -= (size_t)count;
    }
    return true;
}

static bool xx_7zip_stream_filter(uint64_t method, const uint8_t *properties,
                                  size_t properties_size, xx_7zip_stream_window *source,
                                  xx_io_device *destination, uint64_t expected_size) {
    xx_7zip_branch_state branch;
    uint8_t delta[256];
    uint8_t *buffer;
    size_t used = 0U;
    unsigned delta_at = 0U, distance = 0U, swap = 0U;
    bool simple = method == XX_7ZIP_METHOD_COPY || method == XX_7ZIP_METHOD_DELTA ||
                  method == XX_7ZIP_METHOD_SWAP2 || method == XX_7ZIP_METHOD_SWAP4;
    bool success = false;
    if (source->size != expected_size) return false;
    if (method == XX_7ZIP_METHOD_DELTA) {
        if (properties_size != 1U || !properties) return false;
        distance = (unsigned)properties[0] + 1U;
        xx_mem_zero(delta, sizeof(delta));
    } else if (simple) {
        if (properties_size != 0U) return false;
        if (method == XX_7ZIP_METHOD_SWAP2) swap = 2U;
        else if (method == XX_7ZIP_METHOD_SWAP4) swap = 4U;
    } else if (!xx_7zip_branch_state_init(&branch, method, properties, properties_size)) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(XX_7ZIP_STREAM_BUFFER + 16U);
    if (!buffer) return false;
    do {
        size_t added = XX_7ZIP_STREAM_BUFFER;
        size_t processed, i;
        bool final;
        uint64_t remaining = source->size - source->position;
        if ((uint64_t)added > remaining) added = (size_t)remaining;
        if (!xx_7zip_stream_read_exact(&source->device, buffer + used, added)) goto done;
        used += added;
        final = source->position == source->size;
        if (method == XX_7ZIP_METHOD_DELTA) {
            for (i = 0U; i < used; ++i) {
                buffer[i] = (uint8_t)(buffer[i] + delta[delta_at]);
                delta[delta_at] = buffer[i];
                if (++delta_at == distance) delta_at = 0U;
            }
            processed = used;
        } else if (swap != 0U) {
            processed = used & ~(size_t)(swap - 1U);
            for (i = 0U; i < processed; i += swap) {
                unsigned j;
                for (j = 0U; j < swap / 2U; ++j) {
                    uint8_t byte = buffer[i + j];
                    buffer[i + j] = buffer[i + swap - 1U - j];
                    buffer[i + swap - 1U - j] = byte;
                }
            }
            if (final) processed = used;
        } else if (method == XX_7ZIP_METHOD_COPY) {
            processed = used;
        } else {
            processed = xx_7zip_branch_process(&branch, buffer, used, final);
            if (processed == SIZE_MAX || processed > used) goto done;
        }
        if (!xx_7zip_stream_write_exact(destination, buffer, processed)) goto done;
        used -= processed;
        if (used > 16U || (processed == 0U && !final)) goto done;
        if (used) xx_rt_memmove(buffer, buffer + processed, used);
        if (final) break;
    } while (!xx_pd_is_stopped(source->pd));
    success = source->position == source->size && used == 0U && !xx_pd_is_stopped(source->pd);
done:
    xx_mem_zero(buffer, XX_7ZIP_STREAM_BUFFER + 16U);
    xx_mem_zero(delta, sizeof(delta));
    xx_mem_free(buffer);
    return success;
}

static bool xx_7zip_stream_deflate(xx_io_device *source, uint64_t size,
                                   xx_io_device *destination, bool deflate64,
                                   xx_pd_struct *pd) {
    xx_bit_reader reader;
    bool result;
    uint64_t used, unused;
    if (!xx_br_init(&reader, source, NULL, 0U, (int64_t)size)) return false;
    result = xx_deflate_decompress_stream(&reader, destination, NULL, 0U, NULL, deflate64, pd);
    used = size - (uint64_t)reader.remaining_input;
    unused = (uint64_t)(reader.buffer_len - reader.buffer_pos);
    if (reader.bit_count < 0 || used < unused) result = false;
    else {
        used -= unused;
        unused = (uint64_t)(reader.bit_count / 8);
        if (used < unused || used - unused != size) result = false;
    }
    xx_br_free(&reader);
    return result;
}

static bool xx_7zip_stream_memory(uint64_t method, xx_io_device *source,
                                  uint64_t input_size, uint64_t output_size,
                                  xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *input = NULL, *output = NULL;
    size_t written = 0U;
    bool result = false;
    if (input_size > SIZE_MAX || output_size > SIZE_MAX) return false;
    input = (uint8_t *)xx_mem_alloc(input_size ? (size_t)input_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size ? (size_t)output_size : 1U);
    if (!input || !output || !xx_7zip_stream_read_exact(source, input, (size_t)input_size)) goto done;
    if (method == XX_7ZIP_METHOD_ZSTD)
        result = xx_zstd_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_BROTLI)
        result = xx_brotli_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_LZ4)
        result = xx_lz4_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_LZ5)
        result = xx_lz5_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_LIZARD)
        result = xx_lizard_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    result = result && written == (size_t)output_size && !xx_pd_is_stopped(pd);
    if (result) {
        size_t at = 0U;
        while (at < written) {
            size_t chunk = written - at;
            if (chunk > XX_7ZIP_STREAM_BUFFER) chunk = XX_7ZIP_STREAM_BUFFER;
            if (!xx_7zip_stream_write_exact(destination, output + at, chunk)) { result = false; break; }
            at += chunk;
        }
    }
done:
    if (input) { xx_mem_zero(input, (size_t)input_size); xx_mem_free(input); }
    if (output) { xx_mem_zero(output, (size_t)output_size); xx_mem_free(output); }
    return result;
}

bool xx_7zip_stream_decode(uint64_t method, const uint8_t *properties,
                            size_t properties_size, xx_io_device *source,
                            int64_t source_offset, uint64_t compressed_size,
                            uint64_t expected_size, xx_io_device *destination,
                            const uint8_t *password_utf16le, size_t password_size,
                            xx_pd_struct *pd) {
    xx_7zip_stream_window input;
    xx_7zip_stream_sink output;
    bool result = false;
    if (!destination || (!properties && properties_size) || expected_size > INT64_MAX ||
        xx_pd_is_stopped(pd) ||
        !xx_7zip_stream_window_init(&input, source, source_offset, compressed_size, pd)) return false;
    xx_7zip_stream_sink_init(&output, destination, expected_size, pd);
    if (method == XX_7ZIP_METHOD_LZMA) {
        result = xx_lzma_unpack_device(&input.device, 0, (int64_t)compressed_size,
                     properties, properties_size, (int64_t)expected_size, &output.device, pd);
    } else if (method == XX_7ZIP_METHOD_LZMA2 && properties_size == 1U) {
        result = xx_lzma2_unpack_device(&input.device, 0, (int64_t)compressed_size,
                     properties[0], &output.device, pd);
    } else if (method == XX_7ZIP_METHOD_BZIP2 && properties_size == 0U) {
        result = xx_bzip2_unpack_device(&input.device, 0, (int64_t)compressed_size, &output.device, pd);
    } else if ((method == XX_7ZIP_METHOD_DEFLATE || method == XX_7ZIP_METHOD_DEFLATE64) && properties_size == 0U) {
        result = xx_7zip_stream_deflate(&input.device, compressed_size, &output.device,
                                       method == XX_7ZIP_METHOD_DEFLATE64, pd);
    } else if (method == XX_7ZIP_METHOD_PPMD7 && properties_size == 5U) {
        ppmd7_range_dec decoder;
        if (compressed_size == 0U) result = expected_size == 0U;
        else result = ppmd7_rd_init(&decoder, &input.device, NULL, 0U, (int64_t)compressed_size) &&
                      xx_ppmd7_decompress_stream_sized(&decoder, properties[0],
                          xx_data_get_u32(properties + 1U, 4, 0, false), &output.device, expected_size, pd);
    } else if (method == XX_7ZIP_METHOD_AES) {
        result = xx_7zip_aes_decrypt_device(&input.device, 0, (int64_t)compressed_size,
                     password_utf16le, password_size, properties, properties_size,
                     (int64_t)expected_size, &output.device, pd);
    } else if (method == XX_7ZIP_METHOD_ZSTD || method == XX_7ZIP_METHOD_BROTLI ||
               method == XX_7ZIP_METHOD_LZ4 || method == XX_7ZIP_METHOD_LZ5 || method == XX_7ZIP_METHOD_LIZARD) {
        result = xx_7zip_stream_memory(method, &input.device, compressed_size,
                                       expected_size, &output.device, pd);
    } else {
        result = xx_7zip_stream_filter(method, properties, properties_size,
                                       &input, &output.device, expected_size);
    }
    return result && output.position == expected_size && !xx_pd_is_stopped(pd);
}

typedef struct xx_7zip_bcj2_stream_input_s {
    xx_7zip_stream_window window;
    uint8_t *buffer;
    size_t length, at;
    uint64_t consumed;
} xx_7zip_bcj2_stream_input;

static inline bool xx_7zip_bcj2_stream_byte(xx_7zip_bcj2_stream_input *input,
                                           uint8_t *value) {
    if (input->consumed == input->window.size) return false;
    if (input->at == input->length) {
        ssize_t count;
        size_t request = XX_7ZIP_STREAM_BUFFER;
        uint64_t left = input->window.size - input->window.position;
        if ((uint64_t)request > left) request = (size_t)left;
        count = xx_io_read(&input->window.device, input->buffer, request);
        if (count <= 0 || (size_t)count > request) return false;
        input->length = (size_t)count;
        input->at = 0U;
    }
    *value = input->buffer[input->at++];
    ++input->consumed;
    return true;
}

static inline bool xx_7zip_bcj2_stream_bit(xx_7zip_bcj2_stream_input *input,
                                           uint32_t *range, uint32_t *code,
                                           uint16_t *probability, bool *bit) {
    uint32_t bound;
    if (*range < (UINT32_C(1) << 24U)) {
        uint8_t next;
        if (!xx_7zip_bcj2_stream_byte(input, &next)) return false;
        *range <<= 8U;
        *code = (*code << 8U) | next;
    }
    bound = (*range >> 11U) * *probability;
    if (*code < bound) {
        *range = bound;
        *probability = (uint16_t)(*probability + ((2048U - *probability) >> 5U));
        *bit = false;
    } else {
        *range -= bound;
        *code -= bound;
        *probability = (uint16_t)(*probability - (*probability >> 5U));
        *bit = true;
    }
    return true;
}

bool xx_7zip_stream_bcj2(xx_io_device *const sources[4], const uint64_t sizes[4],
                          const uint8_t *properties, size_t properties_size,
                          uint64_t expected_size, xx_io_device *destination,
                          xx_pd_struct *pd) {
    xx_7zip_bcj2_stream_input input[4];
    xx_7zip_stream_sink output;
    uint16_t probabilities[258];
    uint8_t *buffers = NULL, *out_buffer;
    size_t i, used = 0U;
    uint32_t ip, range = UINT32_MAX, code = 0U;
    uint8_t previous = 0U, first;
    bool success = false;
    if (!sources || !sizes || !destination || (!properties && properties_size) ||
        !xx_7zip_branch_properties_supported(properties_size) || expected_size > INT64_MAX ||
        sizes[3] < 5U || (sizes[1] & 3U) || (sizes[2] & 3U) || xx_pd_is_stopped(pd)) return false;
    xx_mem_zero(input, sizeof(input));
    for (i = 0U; i < 4U; ++i)
        if (!xx_7zip_stream_window_init(&input[i].window, sources[i], 0, sizes[i], pd)) return false;
    /* Four independent bounded readers plus one bounded output buffer. The
     * branch streams themselves reside in caller-owned spill devices. */
    buffers = (uint8_t *)xx_mem_alloc(XX_7ZIP_STREAM_BUFFER * 5U);
    if (!buffers) return false;
    for (i = 0U; i < 4U; ++i) input[i].buffer = buffers + i * XX_7ZIP_STREAM_BUFFER;
    out_buffer = buffers + 4U * XX_7ZIP_STREAM_BUFFER;
    xx_7zip_stream_sink_init(&output, destination, expected_size, pd);
    ip = properties_size == 4U ? xx_data_get_u32(properties, 4, 0, false) : 0U;
    for (i = 0U; i < 258U; ++i) probabilities[i] = 1024U;
    if (!xx_7zip_bcj2_stream_byte(&input[3], &first) || first != 0U) goto done;
    for (i = 0U; i < 4U; ++i) {
        uint8_t next;
        if (!xx_7zip_bcj2_stream_byte(&input[3], &next)) goto done;
        code = (code << 8U) | next;
    }
    if (code == UINT32_MAX) goto done;
    while (input[0].consumed < sizes[0]) {
        uint8_t opcode;
        bool branch;
        if ((input[0].consumed & 4095U) == 0U && xx_pd_is_stopped(pd)) goto done;
        /* Leave room for an opcode and its optional converted four-byte
         * operand; flushing is independent of instruction boundaries. */
        if (XX_7ZIP_STREAM_BUFFER - used < 5U) {
            if (!xx_7zip_stream_write_exact(&output.device, out_buffer, used)) goto done;
            used = 0U;
        }
        if (!xx_7zip_bcj2_stream_byte(&input[0], &opcode) ||
            output.position + used >= expected_size) goto done;
        branch = opcode == 0xE8U || opcode == 0xE9U ||
                 (previous == 0x0FU && (opcode & 0xF0U) == 0x80U);
        out_buffer[used++] = opcode;
        ++ip;
        if (branch) {
            uint16_t *probability = opcode == 0xE8U ? &probabilities[2U + previous]
                                 : &probabilities[opcode == 0xE9U ? 1U : 0U];
            bool converted;
            if (!xx_7zip_bcj2_stream_bit(&input[3], &range, &code, probability, &converted)) goto done;
            if (converted) {
                unsigned selected = opcode == 0xE8U ? 1U : 2U;
                uint32_t target = 0U;
                if (expected_size - output.position - used < 4U) goto done;
                for (i = 0U; i < 4U; ++i) {
                    uint8_t next;
                    if (!xx_7zip_bcj2_stream_byte(&input[selected], &next)) goto done;
                    target = (target << 8U) | next;
                }
                ip += 4U;
                target -= ip;
                out_buffer[used++] = (uint8_t)target;
                out_buffer[used++] = (uint8_t)(target >> 8U);
                out_buffer[used++] = (uint8_t)(target >> 16U);
                out_buffer[used++] = (uint8_t)(target >> 24U);
                previous = (uint8_t)(target >> 24U);
                continue;
            }
        }
        previous = opcode;
    }
    success = input[1].consumed == sizes[1] && input[2].consumed == sizes[2] && code == 0U &&
              xx_7zip_stream_write_exact(&output.device, out_buffer, used) &&
              output.position == expected_size && !xx_pd_is_stopped(pd);
done:
    xx_mem_zero(buffers, XX_7ZIP_STREAM_BUFFER * 5U);
    xx_mem_zero(probabilities, sizeof(probabilities));
    xx_mem_free(buffers);
    return success;
}
