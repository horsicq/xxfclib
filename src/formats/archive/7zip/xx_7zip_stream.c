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
#include "../../../algo/deflate/xx_deflate_internal.h"
#include "../../../algo/ppmd7/xx_ppmd7_internal.h"
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

static ssize_t xx_7zip_stream_read(xx_io_device *device, void *buffer, size_t size)
{
    xx_7zip_stream_window *window = (xx_7zip_stream_window *)device->priv;
    ssize_t count;
    uint64_t remaining;
    if (!window || (!buffer && size) || size > (size_t)PTRDIFF_MAX || xx_pd_is_stopped(window->pd)) return -1;
    remaining = window->size - window->position;
    if ((uint64_t)size > remaining) size = (size_t)remaining;
    if (size == 0U) return 0;
    if (xx_io_seek64(window->source, window->offset + (int64_t)window->position, SEEK_SET) != 0) return -1;
    count = xx_io_read(window->source, buffer, size);
    if (count < 0 || (size_t)count > size) return -1;
    window->position += (uint64_t)count;
    return count;
}

static int xx_7zip_stream_seek64(xx_io_device *device, int64_t offset, int origin)
{
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

static int xx_7zip_stream_seek(xx_io_device *device, long offset, int origin)
{
    return xx_7zip_stream_seek64(device, (int64_t)offset, origin);
}

static int64_t xx_7zip_stream_tell(xx_io_device *device)
{
    xx_7zip_stream_window *window = (xx_7zip_stream_window *)device->priv;
    return window ? (int64_t)window->position : -1;
}

static int64_t xx_7zip_stream_size(xx_io_device *device)
{
    xx_7zip_stream_window *window = (xx_7zip_stream_window *)device->priv;
    return window ? (int64_t)window->size : -1;
}

static bool xx_7zip_stream_window_init(xx_7zip_stream_window *window, xx_io_device *source, int64_t offset, uint64_t size, xx_pd_struct *pd)
{
    if (!window || !source || offset < 0 || size > (uint64_t)INT64_MAX - (uint64_t)offset) return false;
    xx_mem_zero(window, sizeof(*window));
    window->source = source;
    window->offset = offset;
    window->size = size;
    window->pd = pd;
    window->device.priv = window;
    window->device.read = xx_7zip_stream_read;
    window->device.seek = xx_7zip_stream_seek;
    window->device.seek64 = xx_7zip_stream_seek64;
    window->device.tell = xx_7zip_stream_tell;
    window->device.total_size = xx_7zip_stream_size;
    return true;
}

static ssize_t xx_7zip_stream_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_stream_sink *sink = (xx_7zip_stream_sink *)device->priv;
    ssize_t count;
    if (!sink || (!buffer && size) || size > (size_t)PTRDIFF_MAX || (uint64_t)size > sink->size - sink->position || xx_pd_is_stopped(sink->pd)) return -1;
    count = xx_io_write(sink->target, buffer, size);
    if (count < 0 || (size_t)count > size) return -1;
    sink->position += (uint64_t)count;
    return count;
}

static int64_t xx_7zip_stream_output_size(xx_io_device *device)
{
    xx_7zip_stream_sink *sink = (xx_7zip_stream_sink *)device->priv;
    return sink ? (int64_t)sink->position : -1;
}

static void xx_7zip_stream_sink_init(xx_7zip_stream_sink *sink, xx_io_device *target, uint64_t size, xx_pd_struct *pd)
{
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->size = size;
    sink->pd = pd;
    sink->device.priv = sink;
    sink->device.write = xx_7zip_stream_write;
    sink->device.total_size = xx_7zip_stream_output_size;
}

static bool xx_7zip_stream_read_exact(xx_io_device *source, uint8_t *buffer, size_t size)
{
    while (size != 0U) {
        size_t request = size < XX_7ZIP_STREAM_BUFFER ? size : XX_7ZIP_STREAM_BUFFER;
        ssize_t count = xx_io_read(source, buffer, request);
        if (count <= 0 || (size_t)count > request) return false;
        buffer += count;
        size -= (size_t)count;
    }
    return true;
}

static bool xx_7zip_stream_write_exact(xx_io_device *destination, const uint8_t *buffer, size_t size)
{
    while (size != 0U) {
        ssize_t count = xx_io_write(destination, buffer, size);
        if (count <= 0 || (size_t)count > size) return false;
        buffer += count;
        size -= (size_t)count;
    }
    return true;
}

/* Length-preserving filters (Copy, Delta, Swap2/4 and the branch converters)
 * run on one owned buffer: XX_7ZIP_FILTER_HISTORY bytes of Delta history,
 * then up to XX_7ZIP_FILTER_CHUNK bytes of input. Each full buffer is
 * converted in place, its converted prefix is written on, and the
 * unconverted suffix (at most 15 bytes: an incomplete instruction or swap
 * group) is kept for the next chunk. The same state machine serves the pull
 * loop (input read from a device) and the push device that a decoder writes
 * its output into, so a decoder and its filter run without an intermediate. */
#define XX_7ZIP_FILTER_CHUNK (256U * 1024U)
#define XX_7ZIP_FILTER_HISTORY 256U
#define XX_7ZIP_FILTER_SUFFIX 16U

typedef struct xx_7zip_filter_sink_s {
    xx_io_device device;
    xx_io_device *target;
    xx_pd_struct *pd;
    uint64_t method;
    uint64_t expected_size, accepted, emitted;
    xx_7zip_branch_state branch;
    uint8_t *memory; /**< History, then the data buffer. */
    uint8_t *data;
    size_t used;
    unsigned distance, swap;
    bool error, finished;
} xx_7zip_filter_sink;

#if defined(_M_X64) || defined(__x86_64__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__SSE2__)
#define XX_7ZIP_FILTER_SSE2 1
#include <emmintrin.h>
#endif

/* y[i] = x[i] + y[i - distance]; data[-XX_7ZIP_FILTER_HISTORY..-1] holds the
 * previous decoded bytes (zeros at stream start). */
static void xx_7zip_delta_decode(uint8_t *data, size_t size, unsigned distance)
{
    size_t i = 0U;
#if defined(XX_7ZIP_FILTER_SSE2)
    if (distance >= 16U) {
        for (; i + 16U <= size; i += 16U) {
            __m128i x = _mm_loadu_si128((const __m128i *)(const void *)(data + i));
            __m128i y = _mm_loadu_si128((const __m128i *)(const void *)(data + i - distance));
            _mm_storeu_si128((__m128i *)(void *)(data + i), _mm_add_epi8(x, y));
        }
    } else if (distance == 1U || distance == 2U || distance == 4U || distance == 8U) {
        /* Prefix sums with stride `distance` inside 16 bytes, plus the last
         * `distance` decoded bytes repeated (distance divides 16). */
        __m128i previous = _mm_loadu_si128((const __m128i *)(const void *)(data - 16));
        for (; i + 16U <= size; i += 16U) {
            __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(data + i));
            __m128i carry;
            if (distance == 1U) {
                v = _mm_add_epi8(v, _mm_slli_si128(v, 1));
                v = _mm_add_epi8(v, _mm_slli_si128(v, 2));
                v = _mm_add_epi8(v, _mm_slli_si128(v, 4));
                v = _mm_add_epi8(v, _mm_slli_si128(v, 8));
                carry = _mm_unpackhi_epi8(previous, previous);
                carry = _mm_shufflehi_epi16(carry, 0xFF);
                carry = _mm_shuffle_epi32(carry, 0xFF);
            } else if (distance == 2U) {
                v = _mm_add_epi8(v, _mm_slli_si128(v, 2));
                v = _mm_add_epi8(v, _mm_slli_si128(v, 4));
                v = _mm_add_epi8(v, _mm_slli_si128(v, 8));
                carry = _mm_shufflehi_epi16(previous, 0xFF);
                carry = _mm_shuffle_epi32(carry, 0xFF);
            } else if (distance == 4U) {
                v = _mm_add_epi8(v, _mm_slli_si128(v, 4));
                v = _mm_add_epi8(v, _mm_slli_si128(v, 8));
                carry = _mm_shuffle_epi32(previous, 0xFF);
            } else {
                v = _mm_add_epi8(v, _mm_slli_si128(v, 8));
                carry = _mm_unpackhi_epi64(previous, previous);
            }
            previous = _mm_add_epi8(v, carry);
            _mm_storeu_si128((__m128i *)(void *)(data + i), previous);
        }
    }
#endif
    /* The first `distance` bytes add the history kept in front of `data`; form
     * that address with pointer arithmetic (data + i - distance), not with an
     * unsigned index i - distance that would wrap below zero. */
    for (; i < size; ++i) data[i] = (uint8_t)(data[i] + *(data + i - distance));
}

/* Reverses each complete group of `swap` (2 or 4) bytes; returns the bytes
 * covered. */
static size_t xx_7zip_swap_decode(uint8_t *data, size_t size, unsigned swap)
{
    size_t processed = size & ~(size_t)(swap - 1U);
    size_t i = 0U;
#if defined(XX_7ZIP_FILTER_SSE2)
    for (; i + 16U <= processed; i += 16U) {
        __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(data + i));
        v = _mm_or_si128(_mm_slli_epi16(v, 8), _mm_srli_epi16(v, 8));
        if (swap == 4U) {
            v = _mm_shufflelo_epi16(v, _MM_SHUFFLE(2, 3, 0, 1));
            v = _mm_shufflehi_epi16(v, _MM_SHUFFLE(2, 3, 0, 1));
        }
        _mm_storeu_si128((__m128i *)(void *)(data + i), v);
    }
#endif
    for (; i < processed; i += swap) {
        uint8_t byte = data[i];
        if (swap == 2U) {
            data[i] = data[i + 1U];
            data[i + 1U] = byte;
        } else {
            data[i] = data[i + 3U];
            data[i + 3U] = byte;
            byte = data[i + 1U];
            data[i + 1U] = data[i + 2U];
            data[i + 2U] = byte;
        }
    }
    return processed;
}

static bool xx_7zip_filter_sink_init(xx_7zip_filter_sink *sink, uint64_t method, const uint8_t *properties, size_t properties_size, uint64_t expected_size,
                                     xx_io_device *destination, xx_pd_struct *pd);
static void xx_7zip_filter_sink_cleanup(xx_7zip_filter_sink *sink);

/* Converts the buffered bytes (all of them when `final`), writes the
 * converted prefix and keeps the rest. */
static bool xx_7zip_filter_sink_run(xx_7zip_filter_sink *sink, bool final)
{
    size_t processed;
    if (sink->error) return false;
    if (sink->method == XX_7ZIP_METHOD_DELTA) {
        xx_7zip_delta_decode(sink->data, sink->used, sink->distance);
        processed = sink->used;
    } else if (sink->swap != 0U) {
        processed = xx_7zip_swap_decode(sink->data, sink->used, sink->swap);
        if (final) processed = sink->used;
    } else if (sink->method == XX_7ZIP_METHOD_COPY) {
        processed = sink->used;
    } else {
        processed = xx_7zip_branch_process(&sink->branch, sink->data, sink->used, final);
        if (processed == SIZE_MAX || processed > sink->used) goto failed;
    }
    if ((uint64_t)processed > sink->expected_size - sink->emitted || !xx_7zip_stream_write_exact(sink->target, sink->data, processed)) goto failed;
    sink->emitted += processed;
    sink->used -= processed;
    if (sink->used > XX_7ZIP_FILTER_SUFFIX || (processed == 0U && !final)) goto failed;
    if (sink->method == XX_7ZIP_METHOD_DELTA) {
        /* The last decoded bytes become the history of the next chunk. */
        xx_rt_memmove(sink->memory, sink->memory + processed, XX_7ZIP_FILTER_HISTORY);
    } else if (sink->used) {
        xx_rt_memmove(sink->data, sink->data + processed, sink->used);
    }
    return true;
failed:
    sink->error = true;
    return false;
}

static ssize_t xx_7zip_filter_sink_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_filter_sink *sink = (xx_7zip_filter_sink *)device->priv;
    const uint8_t *input = (const uint8_t *)buffer;
    size_t left = size;
    if (!sink || sink->error || sink->finished || (!buffer && size) || size > (size_t)PTRDIFF_MAX || (uint64_t)size > sink->expected_size - sink->accepted ||
        xx_pd_is_stopped(sink->pd))
        return -1;
    while (left != 0U) {
        size_t take = XX_7ZIP_FILTER_CHUNK - sink->used;
        if (take > left) take = left;
        xx_mem_copy(sink->data + sink->used, input, take);
        sink->used += take;
        sink->accepted += take;
        input += take;
        left -= take;
        if (sink->used == XX_7ZIP_FILTER_CHUNK && !xx_7zip_filter_sink_run(sink, false)) return -1;
    }
    return (ssize_t)size;
}

static int64_t xx_7zip_filter_sink_size(xx_io_device *device)
{
    xx_7zip_filter_sink *sink = (xx_7zip_filter_sink *)device->priv;
    return sink ? (int64_t)sink->accepted : -1;
}

static bool xx_7zip_filter_sink_init(xx_7zip_filter_sink *sink, uint64_t method, const uint8_t *properties, size_t properties_size, uint64_t expected_size,
                                     xx_io_device *destination, xx_pd_struct *pd)
{
    xx_mem_zero(sink, sizeof(*sink));
    if (!destination || (!properties && properties_size) || expected_size > INT64_MAX) return false;
    if (method == XX_7ZIP_METHOD_DELTA) {
        if (properties_size != 1U) return false;
        sink->distance = (unsigned)properties[0] + 1U;
    } else if (method == XX_7ZIP_METHOD_COPY || method == XX_7ZIP_METHOD_SWAP2 || method == XX_7ZIP_METHOD_SWAP4) {
        if (properties_size != 0U) return false;
        if (method == XX_7ZIP_METHOD_SWAP2) sink->swap = 2U;
        else if (method == XX_7ZIP_METHOD_SWAP4) sink->swap = 4U;
    } else if (!xx_7zip_branch_state_init(&sink->branch, method, properties, properties_size)) {
        return false;
    }
    sink->memory = (uint8_t *)xx_mem_alloc(XX_7ZIP_FILTER_HISTORY + XX_7ZIP_FILTER_CHUNK + XX_7ZIP_FILTER_SUFFIX);
    if (!sink->memory) return false;
    xx_mem_zero(sink->memory, XX_7ZIP_FILTER_HISTORY);
    sink->data = sink->memory + XX_7ZIP_FILTER_HISTORY;
    sink->method = method;
    sink->expected_size = expected_size;
    sink->target = destination;
    sink->pd = pd;
    sink->device.priv = sink;
    sink->device.write = xx_7zip_filter_sink_write;
    sink->device.total_size = xx_7zip_filter_sink_size;
    return true;
}

/* After the producer succeeded: converts the rest and checks every byte
 * went through. */
static bool xx_7zip_filter_sink_finish(xx_7zip_filter_sink *sink)
{
    if (sink->error || sink->accepted != sink->expected_size) return false;
    if (!sink->finished) {
        if (!xx_7zip_filter_sink_run(sink, true)) return false;
        sink->finished = true;
    }
    return sink->used == 0U && sink->emitted == sink->expected_size && !xx_pd_is_stopped(sink->pd);
}

static void xx_7zip_filter_sink_cleanup(xx_7zip_filter_sink *sink)
{
    if (sink->memory) {
        xx_mem_zero(sink->memory, XX_7ZIP_FILTER_HISTORY + XX_7ZIP_FILTER_CHUNK + XX_7ZIP_FILTER_SUFFIX);
        xx_mem_free(sink->memory);
    }
    xx_mem_zero(sink, sizeof(*sink));
}

static bool xx_7zip_stream_filter(uint64_t method, const uint8_t *properties, size_t properties_size, xx_7zip_stream_window *source, xx_io_device *destination,
                                  uint64_t expected_size)
{
    xx_7zip_filter_sink sink;
    bool success = false;
    if (source->size != expected_size || !xx_7zip_filter_sink_init(&sink, method, properties, properties_size, expected_size, destination, source->pd)) return false;
    for (;;) {
        size_t added = XX_7ZIP_FILTER_CHUNK - sink.used;
        uint64_t remaining = source->size - source->position;
        bool final;
        if (xx_pd_is_stopped(source->pd)) goto done;
        if ((uint64_t)added > remaining) added = (size_t)remaining;
        if (!xx_7zip_stream_read_exact(&source->device, sink.data + sink.used, added)) goto done;
        sink.used += added;
        sink.accepted += added;
        final = source->position == source->size;
        if (final) break;
        if (!xx_7zip_filter_sink_run(&sink, false)) goto done;
    }
    success = xx_7zip_filter_sink_finish(&sink);
done:
    xx_7zip_filter_sink_cleanup(&sink);
    return success;
}

static bool xx_7zip_stream_deflate(xx_io_device *source, uint64_t size, xx_io_device *destination, bool deflate64, xx_pd_struct *pd)
{
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

static bool xx_7zip_stream_memory(uint64_t method, xx_io_device *source, uint64_t input_size, uint64_t output_size, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t *input = NULL, *output = NULL;
    size_t written = 0U;
    bool result = false;
    if (input_size > SIZE_MAX || output_size > SIZE_MAX) return false;
    input = (uint8_t *)xx_mem_alloc(input_size ? (size_t)input_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size ? (size_t)output_size : 1U);
    if (!input || !output || !xx_7zip_stream_read_exact(source, input, (size_t)input_size)) goto done;
    if (method == XX_7ZIP_METHOD_ZSTD) result = xx_zstd_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_BROTLI) result = xx_brotli_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_LZ4) result = xx_lz4_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_LZ5) result = xx_lz5_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    else if (method == XX_7ZIP_METHOD_LIZARD) result = xx_lizard_decompress_memory(input, (size_t)input_size, output, (size_t)output_size, &written);
    result = result && written == (size_t)output_size && !xx_pd_is_stopped(pd);
    if (result) {
        size_t at = 0U;
        while (at < written) {
            size_t chunk = written - at;
            if (chunk > XX_7ZIP_STREAM_BUFFER) chunk = XX_7ZIP_STREAM_BUFFER;
            if (!xx_7zip_stream_write_exact(destination, output + at, chunk)) {
                result = false;
                break;
            }
            at += chunk;
        }
    }
done:
    if (input) {
        xx_mem_zero(input, (size_t)input_size);
        xx_mem_free(input);
    }
    if (output) {
        xx_mem_zero(output, (size_t)output_size);
        xx_mem_free(output);
    }
    return result;
}

bool xx_7zip_stream_decode(uint64_t method, const uint8_t *properties, size_t properties_size, xx_io_device *source, int64_t source_offset, uint64_t compressed_size,
                           uint64_t expected_size, xx_io_device *destination, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd)
{
    xx_7zip_stream_window input;
    xx_7zip_stream_sink output;
    bool result = false;
    if (!destination || (!properties && properties_size) || expected_size > INT64_MAX || xx_pd_is_stopped(pd) ||
        !xx_7zip_stream_window_init(&input, source, source_offset, compressed_size, pd))
        return false;
    xx_7zip_stream_sink_init(&output, destination, expected_size, pd);
    if (method == XX_7ZIP_METHOD_LZMA) {
        result = xx_lzma_unpack_device(&input.device, 0, (int64_t)compressed_size, properties, properties_size, (int64_t)expected_size, &output.device, pd);
    } else if (method == XX_7ZIP_METHOD_LZMA2 && properties_size == 1U) {
        result = xx_lzma2_unpack_device(&input.device, 0, (int64_t)compressed_size, properties[0], &output.device, pd);
    } else if (method == XX_7ZIP_METHOD_BZIP2 && properties_size == 0U) {
        result = xx_bzip2_unpack_device(&input.device, 0, (int64_t)compressed_size, &output.device, pd);
    } else if ((method == XX_7ZIP_METHOD_DEFLATE || method == XX_7ZIP_METHOD_DEFLATE64) && properties_size == 0U) {
        result = xx_7zip_stream_deflate(&input.device, compressed_size, &output.device, method == XX_7ZIP_METHOD_DEFLATE64, pd);
    } else if (method == XX_7ZIP_METHOD_PPMD7 && properties_size == 5U) {
        ppmd7_range_dec decoder;
        if (compressed_size == 0U) result = expected_size == 0U;
        else
            result = ppmd7_rd_init(&decoder, &input.device, NULL, 0U, (int64_t)compressed_size) &&
                     xx_ppmd7_decompress_stream_sized(&decoder, properties[0], xx_data_get_u32(properties + 1U, 4, 0, false), &output.device, expected_size, pd);
    } else if (method == XX_7ZIP_METHOD_AES) {
        result = xx_7zip_aes_decrypt_device(&input.device, 0, (int64_t)compressed_size, password_utf16le, password_size, properties, properties_size,
                                            (int64_t)expected_size, &output.device, pd);
    } else if (method == XX_7ZIP_METHOD_ZSTD || method == XX_7ZIP_METHOD_BROTLI || method == XX_7ZIP_METHOD_LZ4 || method == XX_7ZIP_METHOD_LZ5 ||
               method == XX_7ZIP_METHOD_LIZARD) {
        result = xx_7zip_stream_memory(method, &input.device, compressed_size, expected_size, &output.device, pd);
    } else {
        result = xx_7zip_stream_filter(method, properties, properties_size, &input, &output.device, expected_size);
    }
    return result && output.position == expected_size && !xx_pd_is_stopped(pd);
}

typedef struct xx_7zip_bcj2_stream_input_s {
    xx_7zip_stream_window window;
    uint8_t *buffer;
    size_t length, at;
    uint64_t consumed;
} xx_7zip_bcj2_stream_input;

static inline bool xx_7zip_bcj2_stream_byte(xx_7zip_bcj2_stream_input *input, uint8_t *value)
{
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

static inline bool xx_7zip_bcj2_stream_bit(xx_7zip_bcj2_stream_input *input, uint32_t *range, uint32_t *code, uint16_t *probability, bool *bit)
{
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

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_7ZIP_BCJ2_SSE2 1
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

static inline unsigned xx_7zip_bcj2_ctz(unsigned value)
{
#if defined(_MSC_VER)
    unsigned long bit_idx;
    _BitScanForward(&bit_idx, (unsigned long)value);
    return (unsigned)bit_idx;
#else
    return (unsigned)__builtin_ctz(value);
#endif
}
#endif

#define XX_7ZIP_BCJ2_CHUNK (2048U * 1024U)

typedef struct xx_bcj2_filter_device_s {
    xx_io_device device;
    xx_io_device *target;
    uint8_t *out_buf;
    size_t out_used;
    uint64_t total_written;
    uint64_t expected_size;

    uint8_t *mem1;
    uint8_t *mem2;
    uint8_t *mem3;
    const uint8_t *p1;
    const uint8_t *p1_end;
    const uint8_t *p2;
    const uint8_t *p2_end;
    const uint8_t *p3;
    const uint8_t *p3_end;
    size_t size1;
    size_t size2;
    size_t size3;

    uint32_t range;
    uint32_t code;
    uint32_t ip;
    uint8_t previous;
    uint16_t probabilities[258];
    bool error;
    xx_pd_struct *pd;
} xx_bcj2_filter_device;

static void xx_bcj2_filter_cleanup(xx_bcj2_filter_device *f)
{
    if (!f) return;
    /* Owned buffers hold decoded data (decrypted, for passworded archives): wipe
     * them before freeing.  Borrowed secondary streams leave mem1..3 NULL. */
    if (f->mem1) {
        xx_mem_zero(f->mem1, f->size1 ? f->size1 : 1U);
        xx_mem_free(f->mem1);
    }
    if (f->mem2) {
        xx_mem_zero(f->mem2, f->size2 ? f->size2 : 1U);
        xx_mem_free(f->mem2);
    }
    if (f->mem3) {
        xx_mem_zero(f->mem3, f->size3 ? f->size3 : 1U);
        xx_mem_free(f->mem3);
    }
    if (f->out_buf) {
        xx_mem_zero(f->out_buf, XX_7ZIP_BCJ2_CHUNK);
        xx_mem_free(f->out_buf);
    }
    xx_mem_zero(f, sizeof(*f));
}

static bool xx_bcj2_filter_flush(xx_bcj2_filter_device *f)
{
    if (f->out_used == 0U) return true;
    size_t done = 0U;
    while (done < f->out_used) {
        ssize_t written = xx_io_write(f->target, f->out_buf + done, f->out_used - done);
        if (written <= 0 || (size_t)written > f->out_used - done) {
            f->error = true;
            return false;
        }
        done += (size_t)written;
    }
    f->total_written += f->out_used;
    f->out_used = 0U;
    return true;
}

static ssize_t xx_bcj2_filter_write_cb(xx_io_device *device, const void *buffer, size_t size)
{
    xx_bcj2_filter_device *f = (xx_bcj2_filter_device *)device->priv;
    if (f->error || (f->pd && xx_pd_is_stopped(f->pd))) return -1;
    const uint8_t *in0_ptr = (const uint8_t *)buffer;
    const uint8_t *in0_limit = in0_ptr + size;

    uint32_t range = f->range;
    uint32_t code = f->code;
    uint32_t ip = f->ip;
    uint8_t previous = f->previous;
    const uint8_t *p1 = f->p1, *p1_end = f->p1_end;
    const uint8_t *p2 = f->p2, *p2_end = f->p2_end;
    const uint8_t *p3 = f->p3, *p3_end = f->p3_end;
    uint16_t *probabilities = f->probabilities;
    uint8_t *out_buf = f->out_buf;
    size_t out_used = f->out_used;

#if defined(XX_7ZIP_BCJ2_SSE2)
    const __m128i v_e8 = _mm_set1_epi8((char)-24); /* 0xE8 */
    const __m128i v_fe = _mm_set1_epi8((char)-2);  /* 0xFE */
    const __m128i v_0f = _mm_set1_epi8((char)0x0F);
    const __m128i v_f0 = _mm_set1_epi8((char)-16);  /* 0xF0 */
    const __m128i v_80 = _mm_set1_epi8((char)-128); /* 0x80 */
#endif

    while (in0_ptr < in0_limit) {
        if (XX_7ZIP_BCJ2_CHUNK - out_used <= 64U) {
            f->out_used = out_used;
            if (!xx_bcj2_filter_flush(f)) return -1;
            out_used = 0U;
        }

        if (previous != 0x0FU) {
            size_t avail_in = (size_t)(in0_limit - in0_ptr);
            size_t avail_out = (XX_7ZIP_BCJ2_CHUNK - 64U) - out_used;
            size_t max_run = avail_in < avail_out ? avail_in : avail_out;
            uint8_t *dst = out_buf + out_used;
            const uint8_t *src = in0_ptr;
            size_t i = 0U;
#if defined(XX_7ZIP_BCJ2_SSE2)
            /* Ignore 0x0F unless followed by 0x80..0x8F; a match on 0x0F 0x8x
             * advances past 0x0F so the next byte triggers the branch directly. */
            while (i + 17U <= max_run) {
                __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
                __m128i d1 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 1U));
                __m128i m_e = _mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8);
                __m128i m_0f = _mm_and_si128(_mm_cmpeq_epi8(d0, v_0f), _mm_cmpeq_epi8(_mm_and_si128(d1, v_f0), v_80));
                unsigned hits = (unsigned)_mm_movemask_epi8(m_e) | ((unsigned)_mm_movemask_epi8(m_0f) << 1U);
                _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
                if (hits != 0U) {
                    i += (size_t)xx_7zip_bcj2_ctz(hits);
                    goto hit;
                }
                i += 16U;
            }
#endif
            while (i < max_run) {
                uint8_t b = src[i];
                if (b == 0xE8U || b == 0xE9U || b == 0x0FU) goto hit;
                dst[i] = b;
                ++i;
            }
            out_used += max_run;
            in0_ptr += max_run;
            ip += (uint32_t)max_run;
            previous = in0_ptr[-1];
            if (in0_ptr >= in0_limit) break;
            f->out_used = out_used;
            if (!xx_bcj2_filter_flush(f)) return -1;
            out_used = 0U;
            continue;
        hit:
            if (i > 0U) {
                out_used += i;
                in0_ptr += i;
                ip += (uint32_t)i;
                previous = in0_ptr[-1];
            }
        }

        uint8_t opcode = *in0_ptr++;
        bool branch = (opcode == 0xE8U || opcode == 0xE9U || (previous == 0x0FU && (opcode & 0xF0U) == 0x80U));
        out_buf[out_used++] = opcode;
        ++ip;

        if (branch) {
            uint16_t *probability = opcode == 0xE8U ? &probabilities[2U + previous] : &probabilities[opcode == 0xE9U ? 1U : 0U];
            if (range < (UINT32_C(1) << 24U)) {
                if (p3 >= p3_end) {
                    f->error = true;
                    return -1;
                }
                range <<= 8U;
                code = (code << 8U) | (*p3++);
            }
            uint32_t bound = (range >> 11U) * (uint32_t)(*probability);
            if (code < bound) {
                range = bound;
                *probability = (uint16_t)(*probability + ((2048U - *probability) >> 5U));
                previous = opcode;
            } else {
                range -= bound;
                code -= bound;
                *probability = (uint16_t)(*probability - (*probability >> 5U));

                const uint8_t *sel_ptr = (opcode == 0xE8U) ? p1 : p2;
                const uint8_t *sel_end = (opcode == 0xE8U) ? p1_end : p2_end;
                if ((size_t)(sel_end - sel_ptr) < 4U) {
                    f->error = true;
                    return -1;
                }

#if defined(_M_IX86) || defined(_M_X64)
                uint32_t target = _byteswap_ulong(*(const uint32_t *)(const void *)sel_ptr);
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                uint32_t target = __builtin_bswap32(*(const uint32_t *)(const void *)sel_ptr);
#else
                uint32_t target = ((uint32_t)sel_ptr[0] << 24U) | ((uint32_t)sel_ptr[1] << 16U) | ((uint32_t)sel_ptr[2] << 8U) | (uint32_t)sel_ptr[3];
#endif
                if (opcode == 0xE8U) p1 += 4U;
                else p2 += 4U;

                ip += 4U;
                target -= ip;

#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                *(uint32_t *)(void *)(out_buf + out_used) = target;
                out_used += 4U;
                previous = (uint8_t)(target >> 24U);
#else
                out_buf[out_used++] = (uint8_t)target;
                out_buf[out_used++] = (uint8_t)(target >> 8U);
                out_buf[out_used++] = (uint8_t)(target >> 16U);
                out_buf[out_used++] = (uint8_t)(target >> 24U);
                previous = (uint8_t)(target >> 24U);
#endif
                continue;
            }
        } else {
            previous = opcode;
        }
    }

    f->range = range;
    f->code = code;
    f->ip = ip;
    f->previous = previous;
    f->p1 = p1;
    f->p1_end = p1_end;
    f->p2 = p2;
    f->p3 = p3;
    f->out_used = out_used;
    return (ssize_t)size;
}

static int64_t xx_bcj2_filter_size_cb(xx_io_device *device)
{
    xx_bcj2_filter_device *f = (xx_bcj2_filter_device *)device->priv;
    return f ? (int64_t)(f->total_written + f->out_used) : -1;
}

static bool xx_bcj2_filter_load_stream(xx_io_device *source, size_t size, const uint8_t *borrowed, uint8_t **owned, const uint8_t **begin,
                                       const uint8_t **end)
{
    static const uint8_t k_empty = 0U;
    if (size == 0U) {
        *owned = NULL;
        *begin = &k_empty;
        *end = &k_empty;
        return true;
    }
    if (borrowed) {
        *owned = NULL;
        *begin = borrowed;
        *end = borrowed + size;
        return true;
    }
    *owned = (uint8_t *)xx_mem_alloc(size);
    if (!*owned || xx_io_seek64(source, 0, SEEK_SET) != 0 || !xx_7zip_stream_read_exact(source, *owned, size)) return false;
    *begin = *owned;
    *end = *owned + size;
    return true;
}

static bool xx_bcj2_filter_init(xx_bcj2_filter_device *f, xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3],
                                const uint8_t *const secondary_ptrs[3], const uint8_t *properties, size_t properties_size, uint64_t expected_size,
                                xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t first;
    size_t i;
    xx_mem_zero(f, sizeof(*f));
    if (!secondary_sources || !secondary_sizes || !destination || (!properties && properties_size) || !xx_7zip_branch_properties_supported(properties_size) ||
        expected_size > INT64_MAX || secondary_sizes[2] < 5U || (secondary_sizes[0] & 3U) || (secondary_sizes[1] & 3U) || xx_pd_is_stopped(pd))
        return false;

    if (secondary_sizes[0] > 64U * 1024U * 1024U || secondary_sizes[1] > 64U * 1024U * 1024U || secondary_sizes[2] > 64U * 1024U * 1024U) return false;

    f->size1 = (size_t)secondary_sizes[0];
    f->size2 = (size_t)secondary_sizes[1];
    f->size3 = (size_t)secondary_sizes[2];

    f->out_buf = (uint8_t *)xx_mem_alloc(XX_7ZIP_BCJ2_CHUNK);
    if (!f->out_buf ||
        !xx_bcj2_filter_load_stream(secondary_sources[0], f->size1, secondary_ptrs ? secondary_ptrs[0] : NULL, &f->mem1, &f->p1, &f->p1_end) ||
        !xx_bcj2_filter_load_stream(secondary_sources[1], f->size2, secondary_ptrs ? secondary_ptrs[1] : NULL, &f->mem2, &f->p2, &f->p2_end) ||
        !xx_bcj2_filter_load_stream(secondary_sources[2], f->size3, secondary_ptrs ? secondary_ptrs[2] : NULL, &f->mem3, &f->p3, &f->p3_end)) {
        xx_bcj2_filter_cleanup(f);
        return false;
    }

    f->ip = properties_size == 4U ? xx_data_get_u32(properties, 4, 0, false) : 0U;
    for (i = 0U; i < 258U; ++i) f->probabilities[i] = 1024U;

    first = *f->p3++;
    if (first != 0U) {
        xx_bcj2_filter_cleanup(f);
        return false;
    }
    for (i = 0U; i < 4U; ++i) {
        if (f->p3 >= f->p3_end) {
            xx_bcj2_filter_cleanup(f);
            return false;
        }
        f->code = (f->code << 8U) | (*f->p3++);
    }
    if (f->code == UINT32_MAX) {
        xx_bcj2_filter_cleanup(f);
        return false;
    }
    f->range = UINT32_MAX;

    f->target = destination;
    f->expected_size = expected_size;
    f->pd = pd;
    f->device.priv = f;
    f->device.write = xx_bcj2_filter_write_cb;
    f->device.total_size = xx_bcj2_filter_size_cb;
    return true;
}

static bool xx_bcj2_filter_finish(xx_bcj2_filter_device *f)
{
    if (f->error) return false;
    if (!xx_bcj2_filter_flush(f)) return false;
    if (f->p1 != f->p1_end || f->p2 != f->p2_end || f->code != 0U || f->total_written != f->expected_size) return false;
    if (f->pd && xx_pd_is_stopped(f->pd)) return false;
    return true;
}

bool xx_7zip_stream_bcj2(xx_io_device *const sources[4], const uint64_t sizes[4], const uint8_t *properties, size_t properties_size, uint64_t expected_size,
                         xx_io_device *destination, xx_pd_struct *pd)
{
    xx_bcj2_filter_device filter;
    xx_7zip_stream_window in0_window;
    uint8_t *in0_buffer = NULL;
    uint64_t in0_consumed = 0U;
    bool success = false;

    if (!sources || !sizes || !xx_bcj2_filter_init(&filter, &sources[1], &sizes[1], NULL, properties, properties_size, expected_size, destination, pd))
        return false;

    in0_buffer = (uint8_t *)xx_mem_alloc(XX_7ZIP_BCJ2_CHUNK);
    if (!in0_buffer || !xx_7zip_stream_window_init(&in0_window, sources[0], 0, sizes[0], pd)) goto done;

    while (in0_consumed < sizes[0]) {
        if (xx_pd_is_stopped(pd)) goto done;
        size_t want_in = XX_7ZIP_BCJ2_CHUNK;
        uint64_t in_left = sizes[0] - in0_consumed;
        if ((uint64_t)want_in > in_left) want_in = (size_t)in_left;

        if (!xx_7zip_stream_read_exact(&in0_window.device, in0_buffer, want_in)) goto done;
        if (xx_io_write(&filter.device, in0_buffer, want_in) != (ssize_t)want_in) goto done;
        in0_consumed += want_in;
    }

    success = xx_bcj2_filter_finish(&filter);

done:
    if (in0_buffer) {
        xx_mem_zero(in0_buffer, XX_7ZIP_BCJ2_CHUNK);
        xx_mem_free(in0_buffer);
    }
    xx_bcj2_filter_cleanup(&filter);
    return success;
}

bool xx_7zip_stream_bcj2_streaming_spans(xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3], const uint8_t *const secondary_ptrs[3],
                                         const uint8_t *properties, size_t properties_size, uint64_t in0_size, uint64_t expected_size, xx_io_device *u_source,
                                         int64_t u_source_offset, uint64_t u_compressed_size, uint64_t u_method, const uint8_t *u_properties, size_t u_properties_size,
                                         xx_io_device *destination, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd)
{
    xx_bcj2_filter_device filter;
    bool ok;

    if (!xx_bcj2_filter_init(&filter, secondary_sources, secondary_sizes, secondary_ptrs, properties, properties_size, expected_size, destination, pd))
        return false;

    ok = xx_7zip_stream_decode(u_method, u_properties, u_properties_size, u_source, u_source_offset, u_compressed_size, in0_size, &filter.device, password_utf16le,
                               password_size, pd);
    if (ok) {
        ok = xx_bcj2_filter_finish(&filter);
    }
    xx_bcj2_filter_cleanup(&filter);
    return ok;
}

bool xx_7zip_stream_bcj2_streaming(xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3], const uint8_t *properties, size_t properties_size,
                                   uint64_t in0_size, uint64_t expected_size, xx_io_device *u_source, int64_t u_source_offset, uint64_t u_compressed_size,
                                   uint64_t u_method, const uint8_t *u_properties, size_t u_properties_size, xx_io_device *destination, const uint8_t *password_utf16le,
                                   size_t password_size, xx_pd_struct *pd)
{
    return xx_7zip_stream_bcj2_streaming_spans(secondary_sources, secondary_sizes, NULL, properties, properties_size, in0_size, expected_size, u_source,
                                               u_source_offset, u_compressed_size, u_method, u_properties, u_properties_size, destination, password_utf16le,
                                               password_size, pd);
}

bool xx_7zip_stream_filter_streaming(uint64_t method, const uint8_t *properties, size_t properties_size, uint64_t expected_size, xx_io_device *u_source,
                                     int64_t u_source_offset, uint64_t u_compressed_size, uint64_t u_method, const uint8_t *u_properties, size_t u_properties_size,
                                     xx_io_device *destination, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd)
{
    xx_7zip_filter_sink sink;
    bool ok;
    if (!xx_7zip_filter_sink_init(&sink, method, properties, properties_size, expected_size, destination, pd)) return false;
    ok = xx_7zip_stream_decode(u_method, u_properties, u_properties_size, u_source, u_source_offset, u_compressed_size, expected_size, &sink.device, password_utf16le,
                               password_size, pd);
    if (ok) ok = xx_7zip_filter_sink_finish(&sink);
    xx_7zip_filter_sink_cleanup(&sink);
    return ok;
}

bool xx_7zip_stream_filter_method(uint64_t method)
{
    return method == XX_7ZIP_METHOD_COPY || method == XX_7ZIP_METHOD_DELTA || method == XX_7ZIP_METHOD_SWAP2 || method == XX_7ZIP_METHOD_SWAP4 ||
           xx_7zip_branch_method_supported(method, 0U);
}

/* BCJ2's main stream decoded on a worker thread. The worker's decoder writes
 * into a ring of chunks; the calling thread runs BCJ2, the CRC and the
 * destination writes on them, so both run at once, and the calling thread
 * may decode the call/jump/range streams while the worker starts. The worker
 * has its own xx_pd_struct (the caller's progress and observer stay on the
 * calling thread): a stop or a failure there stops it and the ring. The
 * worker reads only `source`; the caller must not touch that device or its
 * parent until xx_7zip_pipe_bcj2_finish or xx_7zip_pipe_abort returns. */
#include "xxfclib/threads/xx_threads.h"

#define XX_7ZIP_PIPE_CHUNK (512U * 1024U)
/* 128 x 512 KiB: the same 64 MiB ring budget as before the smaller chunks. */
#define XX_7ZIP_PIPE_SLOTS 128U
#define XX_7ZIP_PIPE_POLL_MS 50U

struct xx_7zip_pipe_s {
    xx_io_device device; /**< The worker's decoder writes here. */
    xx_mutex_t *mutex;
    xx_cond_t *data_ready, *space_ready;
    xx_thread_t *thread;
    uint8_t *slots[XX_7ZIP_PIPE_SLOTS];
    size_t sizes[XX_7ZIP_PIPE_SLOTS];
    unsigned head, filled; /**< Guarded by mutex. */
    unsigned tail;         /**< Worker only: the slot being filled. */
    size_t tail_used;      /**< Worker only. */
    uint64_t written;      /**< Worker only. */
    bool done, ok, abort;  /**< Guarded by mutex. */
    xx_pd_struct worker_pd; /**< The worker's own: stopped on abort. */
    /* The worker's decode. */
    uint64_t method;
    const uint8_t *properties;
    size_t properties_size;
    xx_io_device *source;
    uint64_t compressed_size, expected_size;
    const uint8_t *password;
    size_t password_size;
};

/* Publishes the worker's current slot; false when aborted. Mutex held. */
static bool xx_7zip_pipe_publish_locked(xx_7zip_pipe *pipe)
{
    if (pipe->abort) return false;
    pipe->sizes[pipe->tail] = pipe->tail_used;
    pipe->tail = (pipe->tail + 1U) % XX_7ZIP_PIPE_SLOTS;
    pipe->tail_used = 0U;
    ++pipe->filled;
    xx_cond_signal(pipe->data_ready);
    return true;
}

static ssize_t xx_7zip_pipe_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_pipe *pipe = (xx_7zip_pipe *)device->priv;
    const uint8_t *input = (const uint8_t *)buffer;
    size_t left = size;
    if (!pipe || (!buffer && size) || size > (size_t)PTRDIFF_MAX || (uint64_t)size > pipe->expected_size - pipe->written) return -1;
    while (left != 0U) {
        size_t take;
        if (pipe->tail_used == 0U) {
            /* A free slot: one the consumer has not got. */
            bool aborted;
            xx_mutex_lock(pipe->mutex);
            while (pipe->filled == XX_7ZIP_PIPE_SLOTS && !pipe->abort) (void)xx_cond_wait(pipe->space_ready, pipe->mutex, XX_7ZIP_PIPE_POLL_MS);
            aborted = pipe->abort;
            xx_mutex_unlock(pipe->mutex);
            if (aborted) return -1;
            if (!pipe->slots[pipe->tail] && !(pipe->slots[pipe->tail] = (uint8_t *)xx_mem_alloc(XX_7ZIP_PIPE_CHUNK))) return -1;
        }
        take = XX_7ZIP_PIPE_CHUNK - pipe->tail_used;
        if (take > left) take = left;
        xx_rt_memcpy(pipe->slots[pipe->tail] + pipe->tail_used, input, take);
        pipe->tail_used += take;
        input += take;
        left -= take;
        if (pipe->tail_used == XX_7ZIP_PIPE_CHUNK) {
            bool published;
            xx_mutex_lock(pipe->mutex);
            published = xx_7zip_pipe_publish_locked(pipe);
            xx_mutex_unlock(pipe->mutex);
            if (!published) return -1;
        }
    }
    pipe->written += size;
    return (ssize_t)size;
}

static int64_t xx_7zip_pipe_size(xx_io_device *device)
{
    xx_7zip_pipe *pipe = (xx_7zip_pipe *)device->priv;
    return pipe ? (int64_t)pipe->written : -1;
}

static int xx_7zip_pipe_worker(void *arg)
{
    xx_7zip_pipe *pipe = (xx_7zip_pipe *)arg;
    bool ok = xx_7zip_stream_decode(pipe->method, pipe->properties, pipe->properties_size, pipe->source, 0, pipe->compressed_size, pipe->expected_size,
                                    &pipe->device, pipe->password, pipe->password_size, &pipe->worker_pd);
    xx_mutex_lock(pipe->mutex);
    if (ok && pipe->tail_used != 0U) ok = xx_7zip_pipe_publish_locked(pipe);
    pipe->ok = ok && pipe->written == pipe->expected_size;
    pipe->done = true;
    xx_cond_broadcast(pipe->data_ready);
    xx_mutex_unlock(pipe->mutex);
    return 0;
}

static void xx_7zip_pipe_free(xx_7zip_pipe *pipe)
{
    unsigned i;
    for (i = 0U; i < XX_7ZIP_PIPE_SLOTS; ++i) {
        if (pipe->slots[i]) {
            xx_mem_zero(pipe->slots[i], XX_7ZIP_PIPE_CHUNK);
            xx_mem_free(pipe->slots[i]);
        }
    }
    if (pipe->data_ready) xx_cond_destroy(pipe->data_ready);
    if (pipe->space_ready) xx_cond_destroy(pipe->space_ready);
    if (pipe->mutex) xx_mutex_destroy(pipe->mutex);
    xx_mem_zero(pipe, sizeof(*pipe));
    xx_mem_free(pipe);
}

xx_7zip_pipe *xx_7zip_pipe_start(uint64_t method, const uint8_t *properties, size_t properties_size, xx_io_device *source, uint64_t compressed_size,
                                 uint64_t expected_size, const uint8_t *password_utf16le, size_t password_size)
{
    xx_7zip_pipe *pipe;
    if (!source || (!properties && properties_size) || expected_size > INT64_MAX) return NULL;
    pipe = (xx_7zip_pipe *)xx_mem_calloc(1U, sizeof(*pipe));
    if (!pipe) return NULL;
    pipe->mutex = xx_mutex_create();
    pipe->data_ready = xx_cond_create();
    pipe->space_ready = xx_cond_create();
    if (!pipe->mutex || !pipe->data_ready || !pipe->space_ready) {
        xx_7zip_pipe_free(pipe);
        return NULL;
    }
    pipe->worker_pd = xx_pd_init();
    pipe->method = method;
    pipe->properties = properties;
    pipe->properties_size = properties_size;
    pipe->source = source;
    pipe->compressed_size = compressed_size;
    pipe->expected_size = expected_size;
    pipe->password = password_utf16le;
    pipe->password_size = password_size;
    pipe->device.priv = pipe;
    pipe->device.write = xx_7zip_pipe_write;
    pipe->device.total_size = xx_7zip_pipe_size;
    pipe->thread = xx_thread_create(xx_7zip_pipe_worker, pipe);
    if (!pipe->thread) {
        xx_7zip_pipe_free(pipe);
        return NULL;
    }
    return pipe;
}

/* Stops the worker, waits for it and frees the pipe. */
static void xx_7zip_pipe_stop(xx_7zip_pipe *pipe)
{
    xx_mutex_lock(pipe->mutex);
    pipe->abort = true;
    xx_cond_broadcast(pipe->space_ready);
    xx_mutex_unlock(pipe->mutex);
    xx_pd_stop(&pipe->worker_pd);
    (void)xx_thread_join(pipe->thread, NULL);
    xx_7zip_pipe_free(pipe);
}

void xx_7zip_pipe_abort(xx_7zip_pipe *pipe)
{
    if (pipe) xx_7zip_pipe_stop(pipe);
}

bool xx_7zip_pipe_bcj2_finish_spans(xx_7zip_pipe *pipe, xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3],
                                    const uint8_t *const secondary_ptrs[3], const uint8_t *properties, size_t properties_size, uint64_t expected_size,
                                    xx_io_device *destination, xx_pd_struct *pd)
{
    xx_bcj2_filter_device filter;
    bool ok = false, worker_ok = false;
    if (!pipe) return false;
    if (!xx_bcj2_filter_init(&filter, secondary_sources, secondary_sizes, secondary_ptrs, properties, properties_size, expected_size, destination, pd)) {
        xx_7zip_pipe_stop(pipe);
        return false;
    }
    for (;;) {
        uint8_t *chunk = NULL;
        size_t size = 0;
        bool have = false, ended = false;
        if (xx_pd_is_stopped(pd)) goto done;
        xx_mutex_lock(pipe->mutex);
        if (pipe->filled == 0U && !pipe->done) (void)xx_cond_wait(pipe->data_ready, pipe->mutex, XX_7ZIP_PIPE_POLL_MS);
        if (pipe->filled != 0U) {
            /* Only a published slot is read: the producer may still be
             * allocating the slot at head while the ring is empty. */
            have = true;
            chunk = pipe->slots[pipe->head];
            size = pipe->sizes[pipe->head];
        } else if (pipe->done) {
            ended = true;
            worker_ok = pipe->ok;
        }
        xx_mutex_unlock(pipe->mutex);
        if (!have) {
            if (!ended) continue;
            if (worker_ok) break;
            goto done;
        }
        if (xx_io_write(&filter.device, chunk, size) != (ssize_t)size) goto done;
        xx_mutex_lock(pipe->mutex);
        pipe->head = (pipe->head + 1U) % XX_7ZIP_PIPE_SLOTS;
        --pipe->filled;
        xx_cond_signal(pipe->space_ready);
        xx_mutex_unlock(pipe->mutex);
    }
    ok = xx_bcj2_filter_finish(&filter);
done:
    xx_7zip_pipe_stop(pipe);
    xx_bcj2_filter_cleanup(&filter);
    return ok;
}

bool xx_7zip_pipe_bcj2_finish(xx_7zip_pipe *pipe, xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3], const uint8_t *properties,
                              size_t properties_size, uint64_t expected_size, xx_io_device *destination, xx_pd_struct *pd)
{
    return xx_7zip_pipe_bcj2_finish_spans(pipe, secondary_sources, secondary_sizes, NULL, properties, properties_size, expected_size, destination, pd);
}

/* A coder's whole output decoded into memory on a worker thread, readable
 * (and seekable) while it is being written: a read waits until the bytes it
 * asks for are there. Used for 7zAES ahead of LZMA2, so that decryption and
 * decompression run at once. The worker reads only `source`, has its own
 * xx_pd_struct, and is stopped when the device is closed. */
#define XX_7ZIP_ASYNC_POLL_MS 50U

typedef struct xx_7zip_async_s {
    xx_io_device device; /**< The reader's view. */
    xx_io_device writer; /**< The worker's decoder writes here. */
    xx_mutex_t *mutex;
    xx_cond_t *grown;
    xx_thread_t *thread;
    uint8_t *buffer;
    uint64_t size;      /**< Expected output size: the buffer's. */
    uint64_t available; /**< Guarded by mutex: bytes written so far. */
    uint64_t produced;  /**< Worker only. */
    uint64_t position;  /**< Reader only. */
    bool done, ok, abort, joined;
    xx_pd_struct *pd; /**< The reader's (calling thread's). */
    xx_pd_struct worker_pd; /**< The worker's own: stopped on abort. */
    uint64_t method;
    const uint8_t *properties;
    size_t properties_size;
    xx_io_device *source;
    uint64_t compressed_size;
    const uint8_t *password;
    size_t password_size;
} xx_7zip_async;

static ssize_t xx_7zip_async_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_7zip_async *async = (xx_7zip_async *)device->priv;
    bool aborted;
    if (!async || (!buffer && size) || size > (size_t)PTRDIFF_MAX || (uint64_t)size > async->size - async->produced) return -1;
    xx_mutex_lock(async->mutex);
    aborted = async->abort;
    xx_mutex_unlock(async->mutex);
    if (aborted) return -1;
    /* Bytes past `available` are the worker's alone. */
    if (size) xx_mem_copy(async->buffer + async->produced, buffer, size);
    async->produced += size;
    xx_mutex_lock(async->mutex);
    async->available = async->produced;
    xx_cond_broadcast(async->grown);
    xx_mutex_unlock(async->mutex);
    return (ssize_t)size;
}

static int64_t xx_7zip_async_written(xx_io_device *device)
{
    xx_7zip_async *async = (xx_7zip_async *)device->priv;
    return async ? (int64_t)async->produced : -1;
}

static int xx_7zip_async_worker(void *arg)
{
    xx_7zip_async *async = (xx_7zip_async *)arg;
    bool ok = xx_7zip_stream_decode(async->method, async->properties, async->properties_size, async->source, 0, async->compressed_size, async->size,
                                    &async->writer, async->password, async->password_size, &async->worker_pd);
    xx_mutex_lock(async->mutex);
    async->ok = ok && async->produced == async->size;
    async->done = true;
    xx_cond_broadcast(async->grown);
    xx_mutex_unlock(async->mutex);
    return 0;
}

static ssize_t xx_7zip_async_read(xx_io_device *device, void *buffer, size_t size)
{
    xx_7zip_async *async = (xx_7zip_async *)device->priv;
    uint64_t want, have;
    if (!async || (!buffer && size) || size > (size_t)PTRDIFF_MAX) return -1;
    if (async->position >= async->size || size == 0U) return 0;
    want = async->size - async->position;
    if ((uint64_t)size < want) want = (uint64_t)size;
    xx_mutex_lock(async->mutex);
    while (async->available < async->position + want && !async->done && !async->abort) {
        if (xx_pd_is_stopped(async->pd)) break;
        (void)xx_cond_wait(async->grown, async->mutex, XX_7ZIP_ASYNC_POLL_MS);
    }
    have = async->available;
    xx_mutex_unlock(async->mutex);
    /* A failed or stopped worker leaves the rest unreadable. */
    if (have < async->position + want) return -1;
    xx_mem_copy(buffer, async->buffer + async->position, (size_t)want);
    async->position += want;
    return (ssize_t)want;
}

static int xx_7zip_async_seek64(xx_io_device *device, int64_t offset, int origin)
{
    xx_7zip_async *async = (xx_7zip_async *)device->priv;
    int64_t base;
    if (!async) return -1;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = (int64_t)async->position;
    else if (origin == SEEK_END) base = (int64_t)async->size;
    else return -1;
    if ((offset < 0 && -(offset + 1) >= base) || (offset > 0 && offset > (int64_t)async->size - base)) return -1;
    async->position = (uint64_t)(base + offset);
    return 0;
}

static int xx_7zip_async_seek(xx_io_device *device, long offset, int origin)
{
    return xx_7zip_async_seek64(device, (int64_t)offset, origin);
}

static int64_t xx_7zip_async_tell(xx_io_device *device)
{
    xx_7zip_async *async = (xx_7zip_async *)device->priv;
    return async ? (int64_t)async->position : -1;
}

static int64_t xx_7zip_async_size(xx_io_device *device)
{
    xx_7zip_async *async = (xx_7zip_async *)device->priv;
    return async ? (int64_t)async->size : -1;
}

/* Waits for the worker (stopping it first when `stop`); its verdict. */
static bool xx_7zip_async_join(xx_7zip_async *async, bool stop)
{
    if (!async->joined) {
        if (stop) {
            xx_mutex_lock(async->mutex);
            async->abort = true;
            xx_mutex_unlock(async->mutex);
            xx_pd_stop(&async->worker_pd);
        }
        (void)xx_thread_join(async->thread, NULL);
        async->joined = true;
    }
    return async->ok && !async->abort;
}

static void xx_7zip_async_free(xx_7zip_async *async)
{
    /* Not wiped, as the memory intermediates of xx_7zip.c are not: that
     * would touch every page of up to 512 MiB once more. */
    if (async->buffer) xx_mem_free(async->buffer);
    if (async->grown) xx_cond_destroy(async->grown);
    if (async->mutex) xx_mutex_destroy(async->mutex);
    xx_mem_zero(async, sizeof(*async));
    xx_mem_free(async);
}

static int xx_7zip_async_close(xx_io_device *device)
{
    xx_7zip_async *async = device ? (xx_7zip_async *)device->priv : NULL;
    if (!async) return -1;
    (void)xx_7zip_async_join(async, true);
    xx_7zip_async_free(async);
    return 0;
}

xx_io_device *xx_7zip_async_open(uint64_t method, const uint8_t *properties, size_t properties_size, xx_io_device *source, uint64_t compressed_size,
                                 uint64_t expected_size, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd)
{
    xx_7zip_async *async;
    if (!source || (!properties && properties_size) || expected_size > (uint64_t)INT64_MAX || expected_size > (uint64_t)SIZE_MAX) return NULL;
    async = (xx_7zip_async *)xx_mem_calloc(1U, sizeof(*async));
    if (!async) return NULL;
    async->size = expected_size;
    async->buffer = (uint8_t *)xx_mem_alloc(expected_size ? (size_t)expected_size : 1U);
    async->mutex = xx_mutex_create();
    async->grown = xx_cond_create();
    if (!async->buffer || !async->mutex || !async->grown) {
        xx_7zip_async_free(async);
        return NULL;
    }
    async->pd = pd;
    async->worker_pd = xx_pd_init();
    async->method = method;
    async->properties = properties;
    async->properties_size = properties_size;
    async->source = source;
    async->compressed_size = compressed_size;
    async->password = password_utf16le;
    async->password_size = password_size;
    async->writer.priv = async;
    async->writer.write = xx_7zip_async_write;
    async->writer.total_size = xx_7zip_async_written;
    async->device.priv = async;
    async->device.read = xx_7zip_async_read;
    async->device.seek = xx_7zip_async_seek;
    async->device.seek64 = xx_7zip_async_seek64;
    async->device.tell = xx_7zip_async_tell;
    async->device.total_size = xx_7zip_async_size;
    async->device.get_total_size = xx_7zip_async_size;
    async->device.size = xx_7zip_async_size;
    async->device.close = xx_7zip_async_close;
    async->thread = xx_thread_create(xx_7zip_async_worker, async);
    if (!async->thread) {
        xx_7zip_async_free(async);
        return NULL;
    }
    return &async->device;
}

bool xx_7zip_async_is(const xx_io_device *device)
{
    return device && device->close == xx_7zip_async_close;
}

bool xx_7zip_async_finish(xx_io_device *device, bool stop)
{
    xx_7zip_async *async;
    if (!xx_7zip_async_is(device) || !(async = (xx_7zip_async *)device->priv)) return false;
    return xx_7zip_async_join(async, stop);
}
