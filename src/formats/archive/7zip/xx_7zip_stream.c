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

static bool xx_7zip_stream_filter(uint64_t method, const uint8_t *properties, size_t properties_size, xx_7zip_stream_window *source, xx_io_device *destination,
                                  uint64_t expected_size)
{
    xx_7zip_branch_state branch;
    uint8_t delta[256];
    uint8_t *buffer;
    size_t used = 0U;
    unsigned delta_at = 0U, distance = 0U, swap = 0U;
    bool simple = method == XX_7ZIP_METHOD_COPY || method == XX_7ZIP_METHOD_DELTA || method == XX_7ZIP_METHOD_SWAP2 || method == XX_7ZIP_METHOD_SWAP4;
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
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

static inline size_t xx_7zip_bcj2_scan_and_copy(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    const __m128i v_e8 = _mm_set1_epi8((char)-24); /* 0xE8 */
    const __m128i v_fe = _mm_set1_epi8((char)-2);  /* 0xFE */
    const __m128i v_0f = _mm_set1_epi8((char)0x0F);

    while (i + 64 <= len) {
        __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
        __m128i m0 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8), _mm_cmpeq_epi8(d0, v_0f));
        int mask0 = _mm_movemask_epi8(m0);
        if (mask0 != 0) {
            _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask0);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask0);
#endif
            return i + (size_t)bit_idx;
        }

        __m128i d1 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 16));
        __m128i m1 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d1, v_fe), v_e8), _mm_cmpeq_epi8(d1, v_0f));
        int mask1 = _mm_movemask_epi8(m1);
        _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
        if (mask1 != 0) {
            _mm_storeu_si128((__m128i *)(void *)(dst + i + 16), d1);
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask1);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask1);
#endif
            return i + 16 + (size_t)bit_idx;
        }

        __m128i d2 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 32));
        __m128i m2 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d2, v_fe), v_e8), _mm_cmpeq_epi8(d2, v_0f));
        int mask2 = _mm_movemask_epi8(m2);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 16), d1);
        if (mask2 != 0) {
            _mm_storeu_si128((__m128i *)(void *)(dst + i + 32), d2);
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask2);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask2);
#endif
            return i + 32 + (size_t)bit_idx;
        }

        __m128i d3 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 48));
        __m128i m3 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d3, v_fe), v_e8), _mm_cmpeq_epi8(d3, v_0f));
        int mask3 = _mm_movemask_epi8(m3);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 32), d2);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 48), d3);
        if (mask3 != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask3);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask3);
#endif
            return i + 48 + (size_t)bit_idx;
        }
        i += 64;
    }
    while (i + 32 <= len) {
        __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
        __m128i m0 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8), _mm_cmpeq_epi8(d0, v_0f));
        int mask0 = _mm_movemask_epi8(m0);
        if (mask0 != 0) {
            _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask0);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask0);
#endif
            return i + (size_t)bit_idx;
        }

        __m128i d1 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 16));
        __m128i m1 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d1, v_fe), v_e8), _mm_cmpeq_epi8(d1, v_0f));
        int mask1 = _mm_movemask_epi8(m1);
        _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 16), d1);
        if (mask1 != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask1);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask1);
#endif
            return i + 16 + (size_t)bit_idx;
        }
        i += 32;
    }
    while (i + 16 <= len) {
        __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
        __m128i m0 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8), _mm_cmpeq_epi8(d0, v_0f));
        int mask = _mm_movemask_epi8(m0);
        _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
        if (mask != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask);
#endif
            return i + (size_t)bit_idx;
        }
        i += 16;
    }
    while (i < len) {
        uint8_t b = src[i];
        if (b == 0xE8U || b == 0xE9U || b == 0x0FU) return i;
        dst[i] = b;
        i++;
    }
    return len;
}
#else
static inline size_t xx_7zip_bcj2_scan_and_copy(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint8_t b = src[i];
        if (b == 0xE8U || b == 0xE9U || b == 0x0FU) return i;
        dst[i] = b;
        i++;
    }
    return len;
}
#endif

#define XX_7ZIP_BCJ2_CHUNK (8192U * 1024U)

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
    if (f->mem1) { xx_mem_zero(f->mem1, f->size1 ? f->size1 : 1U); xx_mem_free(f->mem1); }
    if (f->mem2) { xx_mem_zero(f->mem2, f->size2 ? f->size2 : 1U); xx_mem_free(f->mem2); }
    if (f->mem3) { xx_mem_zero(f->mem3, f->size3 ? f->size3 : 1U); xx_mem_free(f->mem3); }
    if (f->out_buf) { xx_mem_zero(f->out_buf, XX_7ZIP_BCJ2_CHUNK); xx_mem_free(f->out_buf); }
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

    while (in0_ptr < in0_limit) {
        if (XX_7ZIP_BCJ2_CHUNK - out_used < 64U) {
            f->out_used = out_used;
            if (!xx_bcj2_filter_flush(f)) return -1;
            out_used = 0U;
        }

        if (previous != 0x0FU) {
            size_t avail_in = (size_t)(in0_limit - in0_ptr);
            size_t avail_out = (XX_7ZIP_BCJ2_CHUNK - 64U) > out_used ? ((XX_7ZIP_BCJ2_CHUNK - 64U) - out_used) : 0U;
            size_t max_run = avail_in < avail_out ? avail_in : avail_out;
            if (max_run > 0U) {
                size_t copied = xx_7zip_bcj2_scan_and_copy(out_buf + out_used, in0_ptr, max_run);
                if (copied) {
                    out_used += copied;
                    in0_ptr += copied;
                    ip += (uint32_t)copied;
                    previous = in0_ptr[-1];
                    if (in0_ptr >= in0_limit || (XX_7ZIP_BCJ2_CHUNK - out_used < 64U)) continue;
                }
            }
        }

        uint8_t opcode = *in0_ptr++;
        bool branch = (opcode == 0xE8U || opcode == 0xE9U || (previous == 0x0FU && (opcode & 0xF0U) == 0x80U));
        out_buf[out_used++] = opcode;
        ++ip;

        if (branch) {
            uint16_t *probability = opcode == 0xE8U ? &probabilities[2U + previous] : &probabilities[opcode == 0xE9U ? 1U : 0U];
            if (range < (UINT32_C(1) << 24U)) {
                if (p3 >= p3_end) { f->error = true; return -1; }
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
                if (sel_ptr + 4U > sel_end) { f->error = true; return -1; }

#if defined(_M_IX86) || defined(_M_X64)
                uint32_t target = _byteswap_ulong(*(const uint32_t *)(const void *)sel_ptr);
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                uint32_t target = __builtin_bswap32(*(const uint32_t *)(const void *)sel_ptr);
#else
                uint32_t target = ((uint32_t)sel_ptr[0] << 24U) | ((uint32_t)sel_ptr[1] << 16U) |
                                  ((uint32_t)sel_ptr[2] << 8U)  | (uint32_t)sel_ptr[3];
#endif
                if (opcode == 0xE8U) p1 += 4U; else p2 += 4U;

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

static bool xx_bcj2_filter_init(xx_bcj2_filter_device *f,
                                xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3],
                                const uint8_t *properties, size_t properties_size,
                                uint64_t expected_size, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t first;
    size_t i;
    xx_mem_zero(f, sizeof(*f));
    if (!secondary_sources || !secondary_sizes || !destination || (!properties && properties_size) ||
        !xx_7zip_branch_properties_supported(properties_size) || expected_size > INT64_MAX ||
        secondary_sizes[2] < 5U || (secondary_sizes[0] & 3U) || (secondary_sizes[1] & 3U) || xx_pd_is_stopped(pd))
        return false;

    if (secondary_sizes[0] > 64U * 1024U * 1024U || secondary_sizes[1] > 64U * 1024U * 1024U || secondary_sizes[2] > 64U * 1024U * 1024U)
        return false;

    f->size1 = (size_t)secondary_sizes[0];
    f->size2 = (size_t)secondary_sizes[1];
    f->size3 = (size_t)secondary_sizes[2];

    f->mem1 = (uint8_t *)xx_mem_alloc(f->size1 ? f->size1 : 1U);
    f->mem2 = (uint8_t *)xx_mem_alloc(f->size2 ? f->size2 : 1U);
    f->mem3 = (uint8_t *)xx_mem_alloc(f->size3 ? f->size3 : 1U);
    f->out_buf = (uint8_t *)xx_mem_alloc(XX_7ZIP_BCJ2_CHUNK);

    if (!f->mem1 || !f->mem2 || !f->mem3 || !f->out_buf) {
        xx_bcj2_filter_cleanup(f);
        return false;
    }

    if (f->size1) {
        if (xx_io_seek64(secondary_sources[0], 0, SEEK_SET) != 0 || !xx_7zip_stream_read_exact(secondary_sources[0], f->mem1, f->size1)) {
            xx_bcj2_filter_cleanup(f);
            return false;
        }
    }
    if (f->size2) {
        if (xx_io_seek64(secondary_sources[1], 0, SEEK_SET) != 0 || !xx_7zip_stream_read_exact(secondary_sources[1], f->mem2, f->size2)) {
            xx_bcj2_filter_cleanup(f);
            return false;
        }
    }
    if (f->size3) {
        if (xx_io_seek64(secondary_sources[2], 0, SEEK_SET) != 0 || !xx_7zip_stream_read_exact(secondary_sources[2], f->mem3, f->size3)) {
            xx_bcj2_filter_cleanup(f);
            return false;
        }
    }

    f->p1 = f->mem1; f->p1_end = f->mem1 + f->size1;
    f->p2 = f->mem2; f->p2_end = f->mem2 + f->size2;
    f->p3 = f->mem3; f->p3_end = f->mem3 + f->size3;

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

    if (!sources || !sizes || !xx_bcj2_filter_init(&filter, &sources[1], &sizes[1], properties, properties_size, expected_size, destination, pd))
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
    if (in0_buffer) { xx_mem_zero(in0_buffer, XX_7ZIP_BCJ2_CHUNK); xx_mem_free(in0_buffer); }
    xx_bcj2_filter_cleanup(&filter);
    return success;
}

bool xx_7zip_stream_bcj2_streaming(xx_io_device *const secondary_sources[3], const uint64_t secondary_sizes[3],
                                   const uint8_t *properties, size_t properties_size, uint64_t in0_size, uint64_t expected_size,
                                   xx_io_device *u_source, int64_t u_source_offset, uint64_t u_compressed_size,
                                   uint64_t u_method, const uint8_t *u_properties, size_t u_properties_size,
                                   xx_io_device *destination, const uint8_t *password_utf16le, size_t password_size, xx_pd_struct *pd)
{
    xx_bcj2_filter_device filter;
    bool ok;

    if (!xx_bcj2_filter_init(&filter, secondary_sources, secondary_sizes, properties, properties_size, expected_size, destination, pd))
        return false;

    ok = xx_7zip_stream_decode(u_method, u_properties, u_properties_size,
                               u_source, u_source_offset, u_compressed_size,
                               in0_size, &filter.device, password_utf16le, password_size, pd);
    if (ok) {
        ok = xx_bcj2_filter_finish(&filter);
    }
    xx_bcj2_filter_cleanup(&filter);
    return ok;
}
