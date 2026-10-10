/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/algo/wavpack/xx_wavpack.h"
#include "xxfclib/memory/xx_memory.h"
#include "xx_wavpack_internal.h"
#include "xxfclib/data/xx_data.h"

typedef struct xx_wv_reader_context {
    const uint8_t *source;
    size_t size;
    size_t position;
} xx_wv_reader_context;

static int32_t xx_wv_read(void *id, void *data, int32_t count)
{
    xx_wv_reader_context *reader = (xx_wv_reader_context *)id;
    size_t amount;
    if (count <= 0) return 0;
    amount = (size_t)count;
    if (amount > reader->size - reader->position) amount = reader->size - reader->position;
    xx_mem_copy(data, reader->source + reader->position, amount);
    reader->position += amount;
    return (int32_t)amount;
}
static int64_t xx_wv_position(void *id)
{
    return (int64_t)((xx_wv_reader_context *)id)->position;
}
static int xx_wv_seek(void *id, int64_t position)
{
    xx_wv_reader_context *reader = (xx_wv_reader_context *)id;
    if (position < 0 || (uint64_t)position > reader->size) return -1;
    reader->position = (size_t)position;
    return 0;
}
static int xx_wv_seek_relative(void *id, int64_t delta, int mode)
{
    xx_wv_reader_context *reader = (xx_wv_reader_context *)id;
    int64_t base;
    if (mode == 0) base = 0;
    else if (mode == 1) base = (int64_t)reader->position;
    else if (mode == 2) base = (int64_t)reader->size;
    else return -1;
    if (delta > INT64_MAX - base || delta < -base) return -1;
    return xx_wv_seek(id, base + delta);
}
static int xx_wv_pushback(void *id, int value)
{
    xx_wv_reader_context *reader = (xx_wv_reader_context *)id;
    if (!reader->position || reader->source[reader->position - 1] != (uint8_t)value) return -1;
    --reader->position;
    return value & 255;
}
static int64_t xx_wv_length(void *id)
{
    return (int64_t)((xx_wv_reader_context *)id)->size;
}
static int xx_wv_can_seek(void *id)
{
    (void)id;
    return 1;
}
static int xx_wv_close(void *id)
{
    (void)id;
    return 0;
}
static xx_wv_StreamReader64 xx_wv_reader = {xx_wv_read,     NULL,         xx_wv_position, xx_wv_seek, xx_wv_seek_relative,
                                            xx_wv_pushback, xx_wv_length, xx_wv_can_seek, NULL,       xx_wv_close};

/* Reject truncated or oversized blocks before the vendor decoder allocates them.
 * ZIP method 97 contains a complete sequence of WavPack blocks, without tags. */
static bool xx_wv_validate_blocks(const uint8_t *data, size_t size)
{
    size_t position = 0;
    if (size < 32) return false;
    while (position < size) {
        size_t block_size;
        if (size - position < 32 || xx_mem_compare(data + position, "wvpk", 4)) return false;
        block_size = (size_t)xx_data_get_u32(data + position + 4, 4, 0, false);
        if (block_size < 24 || block_size > size - position - 8) return false;
        position += block_size + 8;
    }
    return true;
}

static bool xx_wv_append(uint8_t *destination, size_t size, size_t *position, const void *source, size_t amount)
{
    if (amount > size - *position) return false;
    if (amount) xx_mem_copy(destination + *position, source, amount);
    *position += amount;
    return true;
}

bool xx_wavpack_decompress_memory(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written)
{
    xx_wv_reader_context reader;
    xx_wv_Context *context = NULL;
    int32_t *samples = NULL;
    uint8_t *bytes = NULL;
    uint8_t *output = (uint8_t *)destination;
    size_t position = 0;
    int channels, width;
    bool result = false;
    uint64_t unpacked_total = 0;
    int64_t expected_samples;
    char error[81];
    if (out_written) *out_written = 0;
    if (!source || (!destination && destination_size) || source_size > INT64_MAX || !xx_wv_validate_blocks((const uint8_t *)source, source_size)) return false;
    reader.source = (const uint8_t *)source;
    reader.size = source_size;
    reader.position = 0;
    context = xx_wv_OpenFileInputEx64(&xx_wv_reader, &reader, NULL, error, OPEN_WRAPPER, 0);
    if (!context) goto cleanup;
    channels = xx_wv_GetNumChannels(context);
    width = xx_wv_GetBytesPerSample(context);
    expected_samples = xx_wv_GetNumSamples64(context);
    if (!(xx_wv_GetMode(context) & MODE_LOSSLESS) || channels < 1 || channels > 256 || width < 1 || width > 4 || expected_samples < 0) goto cleanup;
    /* A stored RIFF header is required; synthesizing one would change the file. */
    if (xx_wv_GetWrapperBytes(context) < 12 || xx_mem_compare(xx_wv_GetWrapperData(context), "RIFF", 4) || xx_mem_compare(xx_wv_GetWrapperData(context) + 8, "WAVE", 4))
        goto cleanup;
    if (!xx_wv_append(output, destination_size, &position, xx_wv_GetWrapperData(context), xx_wv_GetWrapperBytes(context))) goto cleanup;
    xx_wv_FreeWrapper(context);
    samples = (int32_t *)xx_mem_alloc(4096U * (size_t)channels * sizeof(*samples));
    bytes = (uint8_t *)xx_mem_alloc(4096U * (size_t)channels * (size_t)width);
    if (!samples || !bytes) goto cleanup;
    for (;;) {
        uint32_t count = xx_wv_UnpackSamples(context, samples, 4096);
        size_t values = (size_t)count * (size_t)channels, i;
        if (!count) break;
        unpacked_total += count;
        if (unpacked_total > (uint64_t)expected_samples) goto cleanup;
        for (i = 0; i < values; ++i) {
            uint32_t value = (uint32_t)samples[i];
            int byte;
            if (width == 1) value += 128;
            for (byte = 0; byte < width; ++byte) bytes[i * (size_t)width + (size_t)byte] = (uint8_t)(value >> (8 * byte));
        }
        if (!xx_wv_append(output, destination_size, &position, bytes, values * (size_t)width)) goto cleanup;
    }
    if (unpacked_total != (uint64_t)expected_samples || xx_wv_GetNumErrors(context)) goto cleanup;
    if (!xx_wv_append(output, destination_size, &position, xx_wv_GetWrapperData(context), xx_wv_GetWrapperBytes(context))) goto cleanup;
    if (position != destination_size) goto cleanup;
    result = true;
    if (out_written) *out_written = position;
cleanup:
    xx_mem_free(samples);
    xx_mem_free(bytes);
    if (context) xx_wv_CloseFile(context);
    return result;
}
