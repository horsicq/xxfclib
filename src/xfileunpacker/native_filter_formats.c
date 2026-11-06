/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "native_filter_formats.h"

#include <xxfclib/algo/adler32/xx_adler32.h>
#include <xxfclib/algo/lzma/xx_lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILTER_CHUNK 65536U

static bool put(xx_io_device *output, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    while (size) {
        ssize_t amount = xx_io_write(output, bytes, size);
        if (amount <= 0 || (size_t)amount > size) return false;
        bytes += amount;
        size -= (size_t)amount;
    }
    return true;
}

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZ4) || defined(XFU_SUPPORT_FORMAT_LZIP)
static bool put_u32le(xx_io_device *output, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)
    };
    return put(output, bytes, sizeof(bytes));
}

#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZOP)
static bool put_u32be(xx_io_device *output, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)(value >> 24), (unsigned char)(value >> 16),
        (unsigned char)(value >> 8), (unsigned char)value
    };
    return put(output, bytes, sizeof(bytes));
}

#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZIP)
static bool put_u64le(xx_io_device *output, uint64_t value)
{
    unsigned char bytes[8];
    unsigned i;
    for (i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
    return put(output, bytes, sizeof(bytes));
}

#endif

static bool stopped(const xfu_request *request, xx_pd_struct *pd)
{
    if (request->callbacks.cancelled &&
        request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZ4)
/* The frame descriptor is only two bytes, so XXH32 needs its short-input
 * branch. Stored blocks are legal LZ4 frames and need no external codec. */
static uint32_t lz4_header_hash(const unsigned char descriptor[2])
{
    const uint32_t prime1 = UINT32_C(2654435761);
    const uint32_t prime2 = UINT32_C(2246822519);
    const uint32_t prime3 = UINT32_C(3266489917);
    const uint32_t prime5 = UINT32_C(374761393);
    uint32_t hash = prime5 + 2U;
    unsigned i;
    for (i = 0; i < 2; ++i) {
        hash += (uint32_t)descriptor[i] * prime5;
        hash = ((hash << 11) | (hash >> 21)) * prime1;
    }
    hash ^= hash >> 15;
    hash *= prime2;
    hash ^= hash >> 13;
    hash *= prime3;
    hash ^= hash >> 16;
    return hash;
}

static bool encode_lz4(const xfu_request *request, xx_io_device *input,
                       xx_io_device *output, xx_pd_struct *pd)
{
    const unsigned char descriptor[2] = {0x60, 0x40};
    const unsigned char magic[4] = {0x04, 0x22, 0x4d, 0x18};
    unsigned char checksum = (unsigned char)(lz4_header_hash(descriptor) >> 8);
    unsigned char buffer[FILTER_CHUNK];
    ssize_t amount;
    if (!put(output, magic, sizeof(magic)) ||
        !put(output, descriptor, sizeof(descriptor)) ||
        !put(output, &checksum, 1)) return false;
    while ((amount = xx_io_read(input, buffer, sizeof(buffer))) > 0) {
        if (stopped(request, pd) ||
            !put_u32le(output, (uint32_t)amount | UINT32_C(0x80000000)) ||
            !put(output, buffer, (size_t)amount)) return false;
    }
    return amount == 0 && !stopped(request, pd) && put_u32le(output, 0);
}

#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZOP)
static bool encode_lzop(const xfu_request *request, const char *stream_name,
                        xx_io_device *input, xx_io_device *output, xx_pd_struct *pd)
{
    static const unsigned char magic[9] =
        {0x89, 'L', 'Z', 'O', 0, 0x0d, 0x0a, 0x1a, 0x0a};
    unsigned char header[25] = {
        0x09, 0x40, 0x09, 0x40, 0x09, 0x40, /* versions */
        1, 1,                         /* method and level */
        0, 0, 0, 1,                   /* data Adler checksum */
        0, 0, 0, 0,                   /* mode */
        0, 0, 0, 0, 0, 0, 0, 0,       /* timestamps */
        0                             /* filename length */
    };
    unsigned char buffer[FILTER_CHUNK];
    size_t name_length = 0;
    uint32_t checksum;
    ssize_t amount;
    /* The header name is Latin-1 in lzop(1): an ASCII name is stored. */
    if (stream_name) {
        for (name_length = 0; stream_name[name_length]; ++name_length)
            if ((unsigned char)stream_name[name_length] >= 0x80U || name_length >= 255U) {
                name_length = 0;
                break;
            }
    }
    header[24] = (unsigned char)name_length;
    checksum = xx_adler32_update(1, header, sizeof(header));
    if (name_length) checksum = xx_adler32_update(checksum, (const unsigned char *)stream_name, name_length);
    if (!put(output, magic, sizeof(magic)) ||
        !put(output, header, sizeof(header)) ||
        (name_length && !put(output, stream_name, name_length)) ||
        !put_u32be(output, checksum))
        return false;
    while ((amount = xx_io_read(input, buffer, sizeof(buffer))) > 0) {
        if (stopped(request, pd) ||
            !put_u32be(output, (uint32_t)amount) ||
            !put_u32be(output, (uint32_t)amount) ||
            !put_u32be(output, xx_adler32_update(1, buffer, (size_t)amount)) ||
            !put(output, buffer, (size_t)amount)) return false;
    }
    return amount == 0 && !stopped(request, pd) && put_u32be(output, 0);
}

#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_UNIX_COMPRESS)
/* compress(1): LZW codes of 9 to 16 bits in block mode, as ncompress writes
 * them. Codes are packed from the low bit up; the width grows by one when
 * the next table entry needs it, and a CLEAR (256) starts again at 9 bits
 * with an empty table. Readers take codes in groups of eight (n_bits bytes,
 * counted from the end of the 3-byte header, or from the last width change
 * or CLEAR): after a CLEAR the writer pads the group with zero bits, which a
 * width change made without a CLEAR never needs (2^(n-1) codes of n bits
 * fill whole groups). As compress does, a full table is kept while the
 * compression ratio holds and cleared when it drops, checked every 10000
 * input bytes: the output is byte for byte what ncompress writes. */
#define COMPRESS_MAX_BITS 16U
#define COMPRESS_CLEAR 256U
#define COMPRESS_FIRST 257U
#define COMPRESS_TABLE_BITS 17U /* 2^17 slots for at most 2^16 entries. */
#define COMPRESS_CHECK_GAP 10000U

typedef struct compress_writer {
    xx_io_device *output;
    uint64_t bits; /* Pending bits, the oldest lowest. */
    unsigned available; /* How many of them. */
    unsigned width; /* The current code width. */
    uint64_t section; /* Bits written since the last width change or CLEAR. */
    uint64_t written; /* Bytes handed to output. */
    unsigned char buffer[FILTER_CHUNK];
    size_t used;
} compress_writer;

static bool compress_flush(compress_writer *writer)
{
    if (writer->used && !put(writer->output, writer->buffer, writer->used)) return false;
    writer->written += writer->used;
    writer->used = 0U;
    return true;
}

static bool compress_bits_out(compress_writer *writer, uint32_t value, unsigned width)
{
    writer->bits |= (uint64_t)value << writer->available;
    writer->available += width;
    writer->section += width;
    while (writer->available >= 8U) {
        if (writer->used == sizeof(writer->buffer) && !compress_flush(writer)) return false;
        writer->buffer[writer->used++] = (unsigned char)writer->bits;
        writer->bits >>= 8U;
        writer->available -= 8U;
    }
    return true;
}

/* Zero bits up to the end of the current group of eight codes; the next
 * section starts there, byte aligned. */
static bool compress_pad(compress_writer *writer)
{
    const uint64_t group = (uint64_t)writer->width * 8U;
    uint64_t padding = (group - writer->section % group) % group;
    while (padding) {
        unsigned step = padding > 32U ? 32U : (unsigned)padding;
        if (!compress_bits_out(writer, 0U, step)) return false;
        padding -= step;
    }
    writer->section = 0U;
    return true;
}

static bool encode_compress(const xfu_request *request, xx_io_device *input,
                            xx_io_device *output, xx_pd_struct *pd)
{
    static const unsigned char header[3] = {0x1f, 0x9d, 0x80 | COMPRESS_MAX_BITS};
    const uint32_t table_size = UINT32_C(1) << COMPRESS_TABLE_BITS;
    const uint32_t limit = UINT32_C(1) << COMPRESS_MAX_BITS; /* No entry reaches it. */
    unsigned char chunk[FILTER_CHUNK];
    compress_writer *writer = (compress_writer *)calloc(1U, sizeof(*writer));
    uint32_t *keys = (uint32_t *)calloc(table_size, sizeof(*keys)); /* (prefix << 8 | byte) + 1 */
    uint16_t *codes = (uint16_t *)malloc(table_size * sizeof(*codes));
    uint32_t next = COMPRESS_FIRST, prefix = 0U;
    uint64_t consumed = 0U, checkpoint = COMPRESS_CHECK_GAP, ratio = 0U;
    bool started = false, success = false;
    ssize_t amount;
    if (!writer || !keys || !codes) goto done;
    writer->output = output;
    writer->width = 9U;
    if (!put(output, header, sizeof(header))) goto done;
    while ((amount = xx_io_read(input, chunk, sizeof(chunk))) > 0) {
        size_t i = 0U;
        if (stopped(request, pd)) goto done;
        if (!started) {
            prefix = chunk[i++];
            started = true;
        }
        for (; i < (size_t)amount; ++i) {
            const uint32_t key = (prefix << 8U | chunk[i]) + 1U;
            uint32_t slot = (uint32_t)((key * UINT32_C(2654435761)) >> (32U - COMPRESS_TABLE_BITS));
            while (keys[slot] && keys[slot] != key) slot = (slot + 1U) & (table_size - 1U);
            if (keys[slot]) {
                prefix = codes[slot];
                continue;
            }
            if (!compress_bits_out(writer, prefix, writer->width)) goto done;
            /* The reader widens once the entry after this one needs it. */
            if (next > (UINT32_C(1) << writer->width) - 1U && writer->width < COMPRESS_MAX_BITS) {
                if (!compress_pad(writer)) goto done;
                ++writer->width;
            }
            prefix = chunk[i];
            if (next < limit) {
                keys[slot] = key;
                codes[slot] = (uint16_t)next++;
            }
            if (next == limit && consumed + i + 1U >= checkpoint) {
                /* Input bytes per output byte (the header's and the whole
                 * ones so far), with 8 fractional bits, as ncompress counts. */
                const uint64_t in = consumed + i + 1U;
                const uint64_t out = 3U + writer->written + writer->used;
                const uint64_t now = in > UINT64_C(0x7fffff) ?
                    ((out >> 8U) ? in / (out >> 8U) : UINT64_C(0x7fffffff)) : (in << 8U) / out;
                checkpoint = in + COMPRESS_CHECK_GAP;
                if (now >= ratio) ratio = now;
                else {
                    if (!compress_bits_out(writer, COMPRESS_CLEAR, writer->width) ||
                        !compress_pad(writer)) goto done;
                    writer->width = 9U;
                    memset(keys, 0, table_size * sizeof(*keys));
                    next = COMPRESS_FIRST;
                    ratio = 0U;
                }
            }
        }
        consumed += (uint64_t)amount;
    }
    if (amount != 0 || stopped(request, pd)) goto done;
    if (started && !compress_bits_out(writer, prefix, writer->width)) goto done;
    if (writer->available) {
        /* The last bits, in a byte of their own. */
        if (writer->used == sizeof(writer->buffer) && !compress_flush(writer)) goto done;
        writer->buffer[writer->used++] = (unsigned char)writer->bits;
        writer->available = 0U;
    }
    success = compress_flush(writer);
done:
    free(writer);
    free(keys);
    free(codes);
    return success;
}

#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZIP)
static bool copy_device(const xfu_request *request, xx_io_device *input,
                        xx_io_device *output, xx_pd_struct *pd)
{
    unsigned char buffer[FILTER_CHUNK];
    ssize_t amount;
    if (xx_io_seek64(input, 0, SEEK_SET) != 0) return false;
    while ((amount = xx_io_read(input, buffer, sizeof(buffer))) > 0) {
        if (stopped(request, pd) || !put(output, buffer, (size_t)amount)) return false;
    }
    return amount == 0 && !stopped(request, pd);
}

static bool encode_lzip(const xfu_request *request, xx_io_device *input,
                        xx_io_device *output, xx_pd_struct *pd)
{
    xx_io_device *compressed = NULL;
    int64_t input_size, output_size, packed_size;
    uint32_t crc;
    uint8_t properties[5];
    size_t properties_size = sizeof(properties);
    uint32_t dictionary;
    unsigned exponent = 12;
    unsigned char header[6] = {'L', 'Z', 'I', 'P', 1, 0};
    bool success = false;
    int level = request->compression_level_set ? request->compression_level : 6;
    input_size = xx_io_total_size(input);
    if (input_size < 0 || input_size > INT64_C(0xffffffff) - 1 ||
        !(compressed = xx_io_temp_open()) ||
        !xx_lzma_pack_source(input, NULL, &output_size, &packed_size, &crc,
                             compressed, level, properties, &properties_size, pd) ||
        properties_size != sizeof(properties) || properties[0] != 0x5d ||
        output_size != input_size || packed_size < 5) goto done;
    dictionary = (uint32_t)properties[1] |
        ((uint32_t)properties[2] << 8) |
        ((uint32_t)properties[3] << 16) |
        ((uint32_t)properties[4] << 24);
    while (exponent <= 29 && (UINT32_C(1) << exponent) != dictionary) ++exponent;
    if (exponent > 29 || (uint64_t)packed_size > UINT64_MAX - 26U) goto done;
    header[5] = (unsigned char)exponent;
    success = put(output, header, sizeof(header)) &&
        copy_device(request, compressed, output, pd) &&
        put_u32le(output, crc) &&
        put_u64le(output, (uint64_t)input_size) &&
        put_u64le(output, (uint64_t)packed_size + 26U);
done:
    if (compressed) xx_io_close(compressed);
    return success && !stopped(request, pd);
}

#endif

#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_UUE) || defined(XFU_SUPPORT_FORMAT_BASE64)
static bool encode_text(const xfu_request *request, const char *name,
                        const char *stream_name, xx_io_device *input,
                        xx_io_device *output, xx_pd_struct *pd)
{
    static const char base64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char data[57];
    char line[82];
    bool b64 = !strcmp(name, "b64encode");
    ssize_t amount;
    if (!stream_name || !*stream_name) stream_name = "payload";
    if (!put(output, b64 ? "begin-base64 644 " : "begin 644 ", b64 ? 17U : 10U) ||
        !put(output, stream_name, strlen(stream_name)) || !put(output, "\n", 1U)) return false;
    while ((amount = xx_io_read(input, data, b64 ? 57U : 45U)) > 0) {
        size_t index, used = 0;
        if (stopped(request, pd)) return false;
        if (!b64) line[used++] = (char)(((unsigned)amount & 63U) + 32U);
        for (index = 0; index < (size_t)amount; index += 3U) {
            unsigned a = data[index];
            unsigned b = index + 1U < (size_t)amount ? data[index + 1U] : 0U;
            unsigned c = index + 2U < (size_t)amount ? data[index + 2U] : 0U;
            unsigned values[4] = {a >> 2, ((a & 3U) << 4) | (b >> 4),
                                  ((b & 15U) << 2) | (c >> 6), c & 63U};
            unsigned j;
            for (j = 0; j < 4; ++j) {
                if (b64) line[used++] = base64[values[j]];
                else line[used++] = (char)(values[j] ? values[j] + 32U : '`');
            }
            if (b64) {
                if (index + 1U >= (size_t)amount) line[used - 2U] = '=';
                if (index + 2U >= (size_t)amount) line[used - 1U] = '=';
            }
        }
        line[used++] = '\n';
        if (!put(output, line, used)) return false;
    }
    if (amount != 0 || stopped(request, pd)) return false;
    return b64 ? put(output, "====\n", 5U) : put(output, "`\nend\n", 6U);
}

#endif

/* The encoders this build has: all of them in the complete engine and the
 * TAR component, else those of the selected XFU_SUPPORT_FORMAT_* flags. */
bool xfu_native_filter_can_encode(const char *name)
{
    if (!name) return false;
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZ4)
    if (!strcmp(name, "lz4")) return true;
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZOP)
    if (!strcmp(name, "lzop")) return true;
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZIP)
    if (!strcmp(name, "lzip")) return true;
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_UNIX_COMPRESS)
    if (!strcmp(name, "compress")) return true;
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_UUE)
    if (!strcmp(name, "uuencode")) return true;
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_BASE64)
    if (!strcmp(name, "b64encode")) return true;
#endif
    return false;
}

bool xfu_native_filter_encode(const xfu_request *request, const char *name,
                              const char *stream_name,
                              xx_io_device *input, xx_io_device *output,
                              xx_pd_struct *pd)
{
    if (!request || !name || !input || !output || !pd ||
        xx_io_seek64(input, 0, SEEK_SET) != 0) return false;
    (void)stream_name;
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZ4)
    if (!strcmp(name, "lz4")) return encode_lz4(request, input, output, pd);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZOP)
    if (!strcmp(name, "lzop")) return encode_lzop(request, stream_name, input, output, pd);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_LZIP)
    if (!strcmp(name, "lzip")) return encode_lzip(request, input, output, pd);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_UNIX_COMPRESS)
    if (!strcmp(name, "compress")) return encode_compress(request, input, output, pd);
#endif
#if XFU_FORMAT_SUPPORT_FULL || defined(XFU_SUPPORT_FORMAT_UUE) || defined(XFU_SUPPORT_FORMAT_BASE64)
    if (!strcmp(name, "uuencode") || !strcmp(name, "b64encode"))
        return encode_text(request, name, stream_name, input, output, pd);
#endif
    return false;
}
