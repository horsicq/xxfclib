/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LZOP framing for LZO1X blocks.  This is intentionally self-contained: it
 * validates headers, checksums, block extents, and all concatenated streams
 * before presenting bytes to the destination device.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzop/xx_lzop.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzo/xx_lzo.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

#define XX_LZOP_MIN_VERSION UINT16_C(0x0900)
#define XX_LZOP_MAX_VERSION UINT16_C(0x1040)
#define XX_LZOP_MAX_BLOCK_SIZE (UINT32_C(64) * UINT32_C(1024) * UINT32_C(1024))
#define XX_LZOP_MAX_EXTRA_SIZE (UINT32_C(16) * UINT32_C(1024) * UINT32_C(1024))

#define XX_LZOP_FLAG_ADLER_DATA UINT32_C(0x00000001)
#define XX_LZOP_FLAG_ADLER_COMPRESSED UINT32_C(0x00000002)
#define XX_LZOP_FLAG_HEADER_EXTRA UINT32_C(0x00000040)
#define XX_LZOP_FLAG_CRC_DATA UINT32_C(0x00000100)
#define XX_LZOP_FLAG_CRC_COMPRESSED UINT32_C(0x00000200)
#define XX_LZOP_FLAG_MULTIPART UINT32_C(0x00000400)
#define XX_LZOP_FLAG_FILTER UINT32_C(0x00000800)
#define XX_LZOP_FLAG_HEADER_CRC UINT32_C(0x00001000)
#define XX_LZOP_ALLOWED_FLAGS UINT32_C(0xfff03fff)

typedef struct xx_lzop_reader_s {
    xx_io_device *device;
    int64_t cursor;
    int64_t end;
} xx_lzop_reader;

typedef struct xx_lzop_checksums_s {
    uint32_t adler32;
    uint32_t crc32;
} xx_lzop_checksums;

static const uint8_t xx_lzop_magic[XX_LZOP_MAGIC_SIZE] = {
    UINT8_C(0x89), 'L', 'Z', 'O', 0U, UINT8_C(0x0d), UINT8_C(0x0a),
    UINT8_C(0x1a), UINT8_C(0x0a)};

static bool xx_lzop_read(xx_lzop_reader *reader, void *data, size_t size) {
    size_t done = 0U;
    if (!reader || (!data && size != 0U) || reader->cursor < 0 ||
        reader->cursor > reader->end ||
        (uint64_t)size > (uint64_t)(reader->end - reader->cursor)) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(reader->device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    reader->cursor += (int64_t)size;
    return true;
}

static uint32_t xx_lzop_adler32_update(uint32_t initial,
                                        const uint8_t *data, size_t size) {
    uint32_t a = initial & UINT32_C(0xffff);
    uint32_t b = initial >> 16U;
    while (size != 0U) {
        size_t count = size > 5552U ? 5552U : size;
        size_t index;
        for (index = 0U; index < count; ++index) {
            a += data[index];
            b += a;
        }
        a %= UINT32_C(65521);
        b %= UINT32_C(65521);
        data += count;
        size -= count;
    }
    return (b << 16U) | a;
}

static void xx_lzop_checksums_init(xx_lzop_checksums *checksums) {
    if (!checksums) return;
    checksums->adler32 = 1U;
    checksums->crc32 = 0U;
}

static void xx_lzop_checksums_update(xx_lzop_checksums *checksums,
                                     const void *data, size_t size) {
    if (!checksums || (!data && size != 0U)) return;
    checksums->adler32 = xx_lzop_adler32_update(
        checksums->adler32, (const uint8_t *)data, size);
    checksums->crc32 = xx_crc32_calc(checksums->crc32, data, size);
}

static bool xx_lzop_read_checked(xx_lzop_reader *reader, void *data,
                                 size_t size, xx_lzop_checksums *checksums) {
    if (!xx_lzop_read(reader, data, size)) return false;
    xx_lzop_checksums_update(checksums, data, size);
    return true;
}

static bool xx_lzop_read_u16be(xx_lzop_reader *reader, uint16_t *value,
                               xx_lzop_checksums *checksums) {
    uint8_t bytes[2];
    if (!value || !xx_lzop_read_checked(reader, bytes, sizeof(bytes),
                                        checksums)) {
        return false;
    }
    *value = ((uint16_t)bytes[0] << 8U) | bytes[1];
    return true;
}

static bool xx_lzop_read_u32be(xx_lzop_reader *reader, uint32_t *value,
                               xx_lzop_checksums *checksums) {
    uint8_t bytes[4];
    if (!value || !xx_lzop_read_checked(reader, bytes, sizeof(bytes),
                                        checksums)) {
        return false;
    }
    *value = ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
             ((uint32_t)bytes[2] << 8U) | bytes[3];
    return true;
}

static bool xx_lzop_read_u32be_plain(xx_lzop_reader *reader,
                                     uint32_t *value) {
    return xx_lzop_read_u32be(reader, value, NULL);
}

static bool xx_lzop_write_all(xx_io_device *destination, const void *data,
                              size_t size) {
    size_t done = 0U;
    if (!destination || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

bool xx_lzop_has_header(const uint8_t *data, size_t size) {
    return data && size >= XX_LZOP_MAGIC_SIZE &&
           xx_rt_memcmp(data, xx_lzop_magic, XX_LZOP_MAGIC_SIZE) == 0;
}

static bool xx_lzop_read_extra_field(xx_lzop_reader *reader, bool crc32) {
    uint32_t length;
    uint32_t expected;
    xx_lzop_checksums checksums;
    uint8_t buffer[4096];
    if (!reader || !xx_lzop_read_u32be_plain(reader, &length) ||
        length > XX_LZOP_MAX_EXTRA_SIZE) {
        return false;
    }
    xx_lzop_checksums_init(&checksums);
    {
        uint8_t bytes[4] = {(uint8_t)(length >> 24U),
                            (uint8_t)(length >> 16U),
                            (uint8_t)(length >> 8U), (uint8_t)length};
        xx_lzop_checksums_update(&checksums, bytes, sizeof(bytes));
    }
    while (length != 0U) {
        size_t count = length > sizeof(buffer) ? sizeof(buffer) : length;
        if (!xx_lzop_read_checked(reader, buffer, count, &checksums)) {
            return false;
        }
        length -= (uint32_t)count;
    }
    return xx_lzop_read_u32be_plain(reader, &expected) &&
           expected == (crc32 ? checksums.crc32 : checksums.adler32);
}

static bool xx_lzop_decode_stream(xx_lzop_reader *reader,
                                  xx_io_device *destination,
                                  uint64_t *total_output,
                                  xx_pd_struct *pd) {
    xx_lzop_checksums header_checksums;
    uint16_t version;
    uint16_t library_version;
    uint16_t needed_version = 0U;
    uint8_t method;
    uint8_t level = 0U;
    uint32_t flags;
    uint32_t unused;
    uint8_t filename_length;
    uint32_t expected_header_checksum;

    if (!reader || !destination || !total_output) return false;
    xx_lzop_checksums_init(&header_checksums);
    if (!xx_lzop_read_u16be(reader, &version, &header_checksums) ||
        !xx_lzop_read_u16be(reader, &library_version, &header_checksums) ||
        version < XX_LZOP_MIN_VERSION || version > XX_LZOP_MAX_VERSION) {
        return false;
    }
    if (version >= UINT16_C(0x0940) &&
        (!xx_lzop_read_u16be(reader, &needed_version, &header_checksums) ||
         needed_version < XX_LZOP_MIN_VERSION ||
         needed_version > XX_LZOP_MAX_VERSION)) {
        return false;
    }
    if (!xx_lzop_read_checked(reader, &method, 1U, &header_checksums) ||
        (method != 1U && method != 2U && method != 3U)) {
        return false;
    }
    if (version >= UINT16_C(0x0940) &&
        !xx_lzop_read_checked(reader, &level, 1U, &header_checksums)) {
        return false;
    }
    if (!xx_lzop_read_u32be(reader, &flags, &header_checksums) ||
        (flags & ~XX_LZOP_ALLOWED_FLAGS) != 0U ||
        (flags & (XX_LZOP_FLAG_FILTER | XX_LZOP_FLAG_MULTIPART)) != 0U ||
        !xx_lzop_read_u32be(reader, &unused, &header_checksums) ||
        !xx_lzop_read_u32be(reader, &unused, &header_checksums) ||
        (version >= UINT16_C(0x0940) &&
         !xx_lzop_read_u32be(reader, &unused, &header_checksums)) ||
        !xx_lzop_read_checked(reader, &filename_length, 1U,
                              &header_checksums)) {
        return false;
    }
    if (filename_length != 0U) {
        uint8_t filename[255];
        if (!xx_lzop_read_checked(reader, filename, filename_length,
                                  &header_checksums)) {
            return false;
        }
    }
    if (!xx_lzop_read_u32be_plain(reader, &expected_header_checksum) ||
        expected_header_checksum !=
            ((flags & XX_LZOP_FLAG_HEADER_CRC) != 0U
                 ? header_checksums.crc32
                 : header_checksums.adler32) ||
        ((flags & XX_LZOP_FLAG_HEADER_EXTRA) != 0U &&
         !xx_lzop_read_extra_field(
             reader, (flags & XX_LZOP_FLAG_HEADER_CRC) != 0U))) {
        return false;
    }

    for (;;) {
        uint32_t expanded_size;
        uint32_t packed_size;
        uint32_t expected_adler_data = 0U;
        uint32_t expected_crc_data = 0U;
        uint32_t expected_adler_packed = 0U;
        uint32_t expected_crc_packed = 0U;
        uint8_t *packed = NULL;
        uint8_t *expanded = NULL;
        const uint8_t *output_data;
        size_t written = 0U;
        xx_lzop_checksums checksums;
        bool result = false;

        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_lzop_read_u32be_plain(reader, &expanded_size)) {
            return false;
        }
        if (expanded_size == 0U) return true;
        if (expanded_size > XX_LZOP_MAX_BLOCK_SIZE ||
            *total_output > (uint64_t)INT64_MAX - expanded_size ||
            !xx_lzop_read_u32be_plain(reader, &packed_size) ||
            packed_size == 0U || packed_size > expanded_size) {
            return false;
        }
        if (((flags & XX_LZOP_FLAG_ADLER_DATA) != 0U &&
             !xx_lzop_read_u32be_plain(reader, &expected_adler_data)) ||
            ((flags & XX_LZOP_FLAG_CRC_DATA) != 0U &&
             !xx_lzop_read_u32be_plain(reader, &expected_crc_data)) ||
            (packed_size < expanded_size &&
             (flags & XX_LZOP_FLAG_ADLER_COMPRESSED) != 0U &&
             !xx_lzop_read_u32be_plain(reader, &expected_adler_packed)) ||
            (packed_size < expanded_size &&
             (flags & XX_LZOP_FLAG_CRC_COMPRESSED) != 0U &&
             !xx_lzop_read_u32be_plain(reader, &expected_crc_packed))) {
            return false;
        }
        packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
        if (!packed || !xx_lzop_read(reader, packed, (size_t)packed_size)) {
            xx_mem_free(packed);
            return false;
        }
        xx_lzop_checksums_init(&checksums);
        xx_lzop_checksums_update(&checksums, packed, (size_t)packed_size);
        if (packed_size < expanded_size &&
            (((flags & XX_LZOP_FLAG_ADLER_COMPRESSED) != 0U &&
              checksums.adler32 != expected_adler_packed) ||
             ((flags & XX_LZOP_FLAG_CRC_COMPRESSED) != 0U &&
              checksums.crc32 != expected_crc_packed))) {
            xx_mem_free(packed);
            return false;
        }
        if (packed_size == expanded_size) {
            output_data = packed;
        } else {
            expanded = (uint8_t *)xx_mem_alloc((size_t)expanded_size);
            if (!expanded || !xx_lzo1x_decompress(packed, (size_t)packed_size,
                                                  expanded,
                                                  (size_t)expanded_size,
                                                  &written) ||
                written != (size_t)expanded_size) {
                xx_mem_free(expanded);
                xx_mem_free(packed);
                return false;
            }
            output_data = expanded;
        }
        xx_lzop_checksums_init(&checksums);
        xx_lzop_checksums_update(&checksums, output_data, (size_t)expanded_size);
        if (((flags & XX_LZOP_FLAG_ADLER_DATA) != 0U &&
             checksums.adler32 != expected_adler_data) ||
            ((flags & XX_LZOP_FLAG_CRC_DATA) != 0U &&
             checksums.crc32 != expected_crc_data)) {
            xx_mem_free(expanded);
            xx_mem_free(packed);
            return false;
        }
        result = xx_lzop_write_all(destination, output_data,
                                   (size_t)expanded_size);
        xx_mem_free(expanded);
        xx_mem_free(packed);
        if (!result) return false;
        *total_output += expanded_size;
    }
}

bool xx_lzop_decode_device(xx_io_device *source, int64_t source_offset,
                           int64_t source_size, xx_io_device *destination,
                           int64_t *output_size, size_t *stream_count,
                           xx_pd_struct *pd) {
    xx_lzop_reader reader;
    uint8_t magic[XX_LZOP_MAGIC_SIZE];
    int64_t total_size;
    uint64_t total_output = 0U;
    size_t count = 0U;

    if (output_size) *output_size = -1;
    if (stream_count) *stream_count = 0U;
    if (!source || !destination || source_offset < 0 ||
        source_size < (int64_t)XX_LZOP_MAGIC_SIZE ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(source);
    if (total_size < source_offset || source_size > total_size - source_offset ||
        xx_io_seek64(source, source_offset, SEEK_SET) != 0) {
        return false;
    }
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.device = source;
    reader.cursor = source_offset;
    reader.end = source_offset + source_size;
    while (reader.cursor < reader.end) {
        if (count == SIZE_MAX || !xx_lzop_read(&reader, magic, sizeof(magic)) ||
            !xx_lzop_has_header(magic, sizeof(magic)) ||
            !xx_lzop_decode_stream(&reader, destination, &total_output, pd)) {
            return false;
        }
        ++count;
    }
    if (count == 0U || reader.cursor != reader.end ||
        total_output > (uint64_t)INT64_MAX) {
        return false;
    }
    if (output_size) *output_size = (int64_t)total_output;
    if (stream_count) *stream_count = count;
    return true;
}
