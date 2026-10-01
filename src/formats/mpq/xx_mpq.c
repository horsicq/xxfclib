/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Blizzard MoPaQ (MPQ) game archive.
 *
 * Header (32/44/68/208 bytes for format versions 0/1/2/3), at the caller's
 * base offset. Self-extracting overlays are outside this reader's scope.
 *   +0x00  "MPQ\x1a"
 *   +0x04  u32  header size       +0x08  u32  archive size
 *   +0x0c  u16  format version    +0x0e  u16  sector size shift
 *   +0x10  u32  hash table offset  +0x14  u32  block table offset
 *   +0x18  u32  hash table entries +0x1c  u32  block table entries
 *   +0x20  u64  high block table offset (version 1)
 *   +0x28  u16  hash offset high   +0x2a  u16  block offset high
 *   +0x2c  u64  archive size (versions 2/3)
 *
 * Both tables are encrypted with Storm's fixed crypt table -- 0x500 words
 * from the LCG seed*125+3 mod 0x2aaaab -- under the keys for the literal
 * strings "(hash table)" and "(block table)", which are the constants
 * 0xc3af3770 and 0xec83b3a3.  A hash slot is {u32 hashA, u32 hashB, u16
 * locale, u8 platform, u8 reserved, u32 block index}; 0xffffffff means free
 * and 0xfffffffe deleted.  A block entry is {u32 offset, u32 packed size,
 * u32 size, u32 flags}.
 *
 * MEMBER NAMES ARE NOT STORED.  The hash table holds only hashes; the real
 * names live in the "(listfile)" member, which is itself usually compressed.
 * Members are therefore published by block index.
 *
 * Ported from XArchive games/xmpq.cpp; U3 implements the same format as
 * archive/509 (class qdb, VMT 0x00626298).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mpq/xx_mpq.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xx_mpq_huffman_native.h"
#include "xx_mpq_adpcm_native.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef MPQ
#define XX_MPQ_FILE_TYPE XX_FILE_TYPE_MPQ
#else
#define XX_MPQ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MPQ_MAX_MEMBERS 524288U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct mpq_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint64_t timestamp;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint32_t flags;
    bool has_crc;
    bool encrypted;
    bool folder;
} mpq_member;

typedef struct mpq_stream_s {
    mpq_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} mpq_stream;

static uint16_t mpq_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t mpq_le32(const uint8_t *b) {
    return (uint32_t)mpq_le16(b) | ((uint32_t)mpq_le16(b + 2U) << 16U);
}

static uint64_t mpq_le64(const uint8_t *b) {
    return (uint64_t)mpq_le32(b) | ((uint64_t)mpq_le32(b + 4U) << 32U);
}

static uint32_t mpq_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t mpq_be64(const uint8_t *b) {
    return ((uint64_t)mpq_be32(b) << 32U) | (uint64_t)mpq_be32(b + 4U);
}

static bool mpq_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool mpq_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!device) return true; /* verify-only pass: nothing is materialized */
    while (done < size) {
        ssize_t amount;
        size_t want = size - done;
        if (want > 0x8000U) want = 0x8000U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             want);
        if (amount <= 0 || (size_t)amount > want) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy a run of source bytes straight through to the destination. */
static bool mpq_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *buffer = NULL;
    bool buffer_result = false;
    uint64_t left = size;
    int64_t saved_cursor = -1;
    if (!source || offset < 0) { buffer_result = (false); goto buffer_done; }
    saved_cursor = xx_io_tell(source);
    if (saved_cursor < 0) goto buffer_done;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) { buffer_result = (false); goto buffer_done; }
    if (capacity == 0U) capacity = 4096U;
    if (capacity > (SIZE_MAX >> 1U)) capacity = SIZE_MAX >> 1U;
    if (left) { if(capacity>left) capacity=(size_t)left; buffer = (uint8_t *)xx_mem_alloc(capacity); if (!buffer) { buffer_result = false; goto buffer_done; } }
    while (left != 0U) {
        size_t want = left < capacity ? (size_t)left : capacity;
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) { buffer_result = (false); goto buffer_done; }
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) { buffer_result = (false); goto buffer_done; }
            done += (size_t)amount;
        }
        if (!mpq_write_all(destination, buffer, want, pd)) { buffer_result = (false); goto buffer_done; }
        left -= want;
    }
    { buffer_result = (true); goto buffer_done; }

buffer_done:
    if (saved_cursor >= 0 &&
        xx_io_seek64(source, saved_cursor, SEEK_SET) != 0)
        buffer_result = false;
    xx_mem_free(buffer);
    return buffer_result;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool mpq_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *buffer = NULL;
    bool buffer_result = false;
    uint64_t left = size;
    if (!destination) { buffer_result = (true); goto buffer_done; }
    if (capacity > (SIZE_MAX >> 1U)) capacity = SIZE_MAX >> 1U;
    if (left) { if(capacity>left) capacity=(size_t)left; buffer = (uint8_t *)xx_mem_alloc(capacity); if (!buffer) { buffer_result = false; goto buffer_done; } }
    if (!left) { buffer_result = true; goto buffer_done; }
    xx_mem_zero(buffer, capacity);
    while (left != 0U) {
        size_t want = left < capacity ? (size_t)left : capacity;
        if (!mpq_write_all(destination, buffer, want, pd)) { buffer_result = (false); goto buffer_done; }
        left -= want;
    }
    { buffer_result = (true); goto buffer_done; }

buffer_done:
    xx_mem_free(buffer);
    return buffer_result;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *mpq_make_name(const char *prefix, int64_t index,
                           const char *suffix) {
    char buffer[96];
    size_t used = 0U;
    size_t at;
    char *result;
    for (at = 0U; prefix && prefix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[at];
    }
    if (index >= 0) {
        char digits[24];
        size_t count = 0U;
        int64_t value = index;
        do {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 4U && count < sizeof(digits)) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    for (at = 0U; suffix && suffix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[at];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

/* Names that DO come from the container are normalized here: separators are
 * unified, traversal components are removed and anything a filesystem would
 * choke on becomes '_'. */
static char *mpq_clean_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool mpq_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void mpq_stream_free(void *opaque) {
    mpq_stream *stream = (mpq_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool mpq_add_member(mpq_stream *stream, const mpq_member *member) {
    mpq_member *grown;
    if (!stream || !member || stream->count >= MPQ_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (mpq_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define MPQ_HEADER_SIZE 32U
#define MPQ_HEADER_SIZE_V1 44U
#define MPQ_HEADER_SIZE_V2 68U
#define MPQ_HEADER_SIZE_V3 208U
#define MPQ_SEARCH_LIMIT (16U * 1024U * 1024U)
#define MPQ_MAX_TABLE_ENTRIES 0x00080000U
#define MPQ_HASH_ENTRY_FREE 0xffffffffU
#define MPQ_HASH_ENTRY_DELETED 0xfffffffeU
#define MPQ_HASH_TABLE_KEY 0xc3af3770U
#define MPQ_BLOCK_TABLE_KEY 0xec83b3a3U
#define MPQ_FILE_IMPLODE 0x00000100U
#define MPQ_FILE_COMPRESS 0x00000200U
#define MPQ_FILE_ENCRYPTED 0x00010000U
#define MPQ_FILE_SINGLE_UNIT 0x01000000U
#define MPQ_FILE_DELETE_MARKER 0x02000000U
#define MPQ_FILE_EXISTS 0x80000000U
#define MPQ_FILE_SECTOR_CRC 0x04000000U
#define MPQ_FILE_PATCH 0x00100000U
#define MPQ_DECODE_LIMIT (64U * 1024U * 1024U)
#define MPQ_COMPRESSION_HUFFMAN 0x01U
#define MPQ_COMPRESSION_ZLIB 0x02U
#define MPQ_COMPRESSION_PKWARE 0x08U
#define MPQ_COMPRESSION_BZIP2 0x10U
#define MPQ_COMPRESSION_SPARSE 0x20U
#define MPQ_COMPRESSION_ADPCM_MONO 0x40U
#define MPQ_COMPRESSION_ADPCM_STEREO 0x80U
#define MPQ_COMPRESSION_LZMA 0x12U /* Special method, not 0x10 | 0x02. */

/* Storm's crypt table: 0x500 words from a fixed LCG.  It is the key schedule
 * for the hash and block tables, so it has to be reproduced bit for bit. */
static void mpq_build_crypt_table(uint32_t *table) {
    uint32_t seed = 0x00100001U;
    uint32_t i, j, index;
    for (i = 0U; i < 0x100U; ++i) {
        index = i;
        for (j = 0U; j < 5U; ++j, index += 0x100U) {
            uint32_t high;
            seed = (seed * 125U + 3U) % 0x002aaaabU;
            high = (seed & 0xffffU) << 16U;
            seed = (seed * 125U + 3U) % 0x002aaaabU;
            table[index] = high | (seed & 0xffffU);
        }
    }
}

static void mpq_decrypt_block(const uint32_t *table, uint8_t *data,
                              size_t size, uint32_t key) {
    uint32_t seed = 0xeeeeeeeeU;
    size_t at;
    /* Storm encryption works on whole DWORDs; a trailing partial word is
     * stored verbatim and must be left alone. */
    for (at = 0U; at + 4U <= size; at += 4U) {
        uint32_t value;
        seed += table[0x400U + (key & 0xffU)];
        value = mpq_le32(data + at) ^ (key + seed);
        key = ((~key << 21U) + 0x11111111U) | (key >> 11U);
        seed = value + seed + (seed << 5U) + 3U;
        data[at] = (uint8_t)(value & 0xffU);
        data[at + 1U] = (uint8_t)((value >> 8U) & 0xffU);
        data[at + 2U] = (uint8_t)((value >> 16U) & 0xffU);
        data[at + 3U] = (uint8_t)((value >> 24U) & 0xffU);
    }
}

static bool mpq_range_within(uint64_t limit, uint64_t offset, uint64_t size) {
    return offset <= limit && size <= limit - offset;
}

/* Header at offset 0, then the encrypted hash and block tables.  A hash slot
 * names a block index; the block entry carries the offset, the two sizes and
 * the flags. */
static bool mpq_parse_inner(Abstractformat *format, mpq_stream **result) {
    uint8_t header[MPQ_HEADER_SIZE_V3];
    uint32_t *crypt = NULL;
    uint8_t *hash_table = NULL;
    uint8_t *block_table = NULL;
    mpq_stream *stream = NULL;
    int64_t total, size, candidate;
    int64_t found = -1;
    uint64_t archive_size = 0U, hash_offset = 0U, block_offset = 0U;
    uint32_t hash_entries = 0U, block_entries = 0U;
    uint32_t header_size = 0U, sector_size = 0U;
    uint16_t version = 0U, sector_shift = 0U;
    uint64_t hash_bytes, block_bytes;
    uint32_t at;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)MPQ_HEADER_SIZE) return false;

    /* The header is taken at offset 0 only.  Scanning 0x200 boundaries the way
     * a general MPQ opener does would also claim self-extracting MPQ
     * executables, and a self-extracting container is deliberately out of
     * scope for this library. */
    candidate = 0;
    do {
        uint64_t available = (uint64_t)(size - candidate);
        size_t want = available < MPQ_HEADER_SIZE_V3
                          ? (size_t)available
                          : (size_t)MPQ_HEADER_SIZE_V3;
        xx_mem_zero(header, sizeof(header));
        if (!mpq_read_at(format->device, format->base_address + candidate,
                         header, want))
            return false;
        if (xx_rt_memcmp(header, "MPQ\x1a", 4U) != 0) continue;
        header_size = mpq_le32(header + 4U);
        archive_size = mpq_le32(header + 8U);
        version = mpq_le16(header + 12U);
        sector_shift = mpq_le16(header + 14U);
        hash_offset = mpq_le32(header + 16U);
        block_offset = mpq_le32(header + 20U);
        hash_entries = mpq_le32(header + 24U);
        block_entries = mpq_le32(header + 28U);
        if (version > 3U || header_size < MPQ_HEADER_SIZE ||
            (uint64_t)header_size > archive_size || archive_size > available ||
            sector_shift > 15U || hash_entries == 0U ||
            hash_entries > MPQ_MAX_TABLE_ENTRIES ||
            (hash_entries & (hash_entries - 1U)) != 0U ||
            block_entries == 0U || block_entries > MPQ_MAX_TABLE_ENTRIES)
            continue;
        if (version >= 1U) {
            if (header_size < MPQ_HEADER_SIZE_V1 || want < MPQ_HEADER_SIZE_V1)
                continue;
            hash_offset |= (uint64_t)mpq_le16(header + 40U) << 32U;
            block_offset |= (uint64_t)mpq_le16(header + 42U) << 32U;
        }
        if (version >= 2U) {
            /* Narrow V3/V4 support uses their complete legacy tables and
             * 32-bit block offsets. HET/BET-only or >4 GiB layouts need a
             * separate index/offset implementation. */
            if (header_size < MPQ_HEADER_SIZE_V2 ||
                want < MPQ_HEADER_SIZE_V2 ||
                mpq_le64(header + 44U) != archive_size ||
                mpq_le64(header + 32U) != 0U ||
                hash_offset > UINT32_MAX || block_offset > UINT32_MAX)
                continue;
        }
        if (version == 3U &&
            (header_size < MPQ_HEADER_SIZE_V3 ||
             want < MPQ_HEADER_SIZE_V3 ||
             mpq_le64(header + 68U) != (uint64_t)hash_entries * 16U ||
             mpq_le64(header + 76U) != (uint64_t)block_entries * 16U))
            continue;
        hash_bytes = (uint64_t)hash_entries * 16U;
        block_bytes = (uint64_t)block_entries * 16U;
        if (!mpq_range_within(archive_size, hash_offset, hash_bytes) ||
            !mpq_range_within(archive_size, block_offset, block_bytes))
            continue;
        sector_size = 512U << sector_shift;
        found = candidate;
    } while (0);
    if (found < 0) return false;

    hash_bytes = (uint64_t)hash_entries * 16U;
    block_bytes = (uint64_t)block_entries * 16U;
    crypt = (uint32_t *)xx_mem_alloc(0x500U * sizeof(uint32_t));
    hash_table = (uint8_t *)xx_mem_alloc((size_t)hash_bytes);
    block_table = (uint8_t *)xx_mem_alloc((size_t)block_bytes);
    if (!crypt || !hash_table || !block_table) goto fail;
    mpq_build_crypt_table(crypt);
    if (!mpq_read_at(format->device,
                     format->base_address + found + (int64_t)hash_offset,
                     hash_table, (size_t)hash_bytes) ||
        !mpq_read_at(format->device,
                     format->base_address + found + (int64_t)block_offset,
                     block_table, (size_t)block_bytes))
        goto fail;
    mpq_decrypt_block(crypt, hash_table, (size_t)hash_bytes,
                      MPQ_HASH_TABLE_KEY);
    mpq_decrypt_block(crypt, block_table, (size_t)block_bytes,
                      MPQ_BLOCK_TABLE_KEY);

    stream = (mpq_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->aux0 = sector_size;
    stream->aux1 = (uint64_t)found;
    stream->aux2 = archive_size;

    for (at = 0U; at < hash_entries; ++at) {
        const uint8_t *slot = hash_table + (size_t)at * 16U;
        uint32_t block_index = mpq_le32(slot + 12U);
        const uint8_t *entry;
        mpq_member member;
        uint64_t file_offset, packed, unpacked;
        uint32_t flags;
        if (block_index == MPQ_HASH_ENTRY_FREE ||
            block_index == MPQ_HASH_ENTRY_DELETED)
            continue;
        if (block_index >= block_entries) goto fail;
        entry = block_table + (size_t)block_index * 16U;
        file_offset = mpq_le32(entry);
        packed = mpq_le32(entry + 4U);
        unpacked = mpq_le32(entry + 8U);
        flags = mpq_le32(entry + 12U);
        if ((flags & MPQ_FILE_EXISTS) == 0U ||
            (flags & MPQ_FILE_DELETE_MARKER) != 0U)
            continue;
        if (!mpq_range_within(archive_size, file_offset, packed)) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = mpq_make_name("file_", (int64_t)block_index, ".bin");
        if (!member.name) goto fail;
        member.header_offset =
            format->base_address + found + (int64_t)block_offset +
            (int64_t)((uint64_t)block_index * 16U);
        member.header_size = 16;
        member.data_offset = format->base_address + found +
                             (int64_t)file_offset;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = unpacked;
        member.flags = flags;
        member.encrypted = (flags & MPQ_FILE_ENCRYPTED) != 0U;
        member.method = flags & (MPQ_FILE_IMPLODE | MPQ_FILE_COMPRESS);
        member.attributes = mpq_le16(slot + 8U); /* locale */
        member.aux0 = block_index;
        if (!mpq_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
    }
    if (stream->count == 0U) goto fail;

    xx_mem_free(crypt);
    xx_mem_free(hash_table);
    xx_mem_free(block_table);
    stream->archive_size = found + (int64_t)archive_size;
    *result = stream;
    return true;
fail:
    if (crypt) xx_mem_free(crypt);
    if (hash_table) xx_mem_free(hash_table);
    if (block_table) xx_mem_free(block_table);
    mpq_stream_free(stream);
    return false;
}

static bool mpq_parse(Abstractformat *format, mpq_stream **result) {
    int64_t cursor;
    bool parsed;
    if (!format || !format->device || !result) return false;
    *result = NULL;
    cursor = xx_io_tell(format->device);
    if (cursor < 0) return false;
    parsed = mpq_parse_inner(format, result);
    if (xx_io_seek64(format->device, cursor, SEEK_SET) != 0) {
        if (parsed) {
            mpq_stream_free(*result);
            *result = NULL;
        }
        return false;
    }
    return parsed;
}

/* Normal, complete sector tables only. Protected tables with gaps and patch
 * prefixes are declined. The final offset authenticates
 * the framing, not the contents: MPQ has no mandatory member checksum.
 * Primary semantics: StormLib SBaseCommon.cpp / SFileReadFile.cpp. */
static bool mpq_sector_table_valid(const uint8_t *offsets, size_t count,
                                    size_t table_size, size_t packed_size,
                                    size_t raw_size, size_t sector_size,
                                    bool has_checksums) {
    size_t i;
    if (mpq_le32(offsets) != table_size) return false;
    for (i = 0U; i < count; ++i) {
        uint32_t begin = mpq_le32(offsets + i * 4U);
        uint32_t end = mpq_le32(offsets + (i + 1U) * 4U);
        size_t want = raw_size - i * sector_size;
        if (want > sector_size) want = sector_size;
        if (begin > packed_size || end > packed_size || end <= begin ||
            (size_t)(end - begin) > want) return false;
    }
    if (has_checksums) {
        uint32_t begin = mpq_le32(offsets + count * 4U);
        uint32_t end = mpq_le32(offsets + (count + 1U) * 4U);
        /* A compressed checksum table needs an additional codec chain.
         * Support the uncompressed table and verify each non-sentinel sum. */
        return end == packed_size && end >= begin &&
               (size_t)(end - begin) == count * 4U;
    }
    return mpq_le32(offsets + count * 4U) == packed_size;
}

/* Recover only uniquely validated keys, with no filename/key dictionary.
 * Offset-table encryption uses file-key minus one; data sector i uses
 * file-key plus i. All arithmetic intentionally wraps at 32 bits. */
static bool mpq_sector_key(const uint32_t *crypt, const uint8_t *packed,
                            size_t count, size_t table_size,
                            size_t packed_size, size_t raw_size,
                            size_t sector_size, bool has_checksums,
                            uint8_t *offsets, uint32_t *file_key) {
    unsigned i;
    unsigned matches = 0U;
    uint32_t cipher = mpq_le32(packed);
    for (i = 0U; i < 256U; ++i) {
        uint32_t key = (cipher ^ (uint32_t)table_size) -
                       (0xeeeeeeeeU + crypt[0x400U + i]);
        if ((key & 255U) != i) continue;
        xx_mem_copy(offsets, packed, table_size);
        mpq_decrypt_block(crypt, offsets, table_size, key);
        if (mpq_sector_table_valid(offsets, count, table_size, packed_size,
                                   raw_size, sector_size, has_checksums)) {
            *file_key = key + 1U;
            if (++matches > 1U) return false;
        }
    }
    if (matches != 1U) return false;
    xx_mem_copy(offsets, packed, table_size);
    mpq_decrypt_block(crypt, offsets, table_size, *file_key - 1U);
    return true;
}

static bool mpq_wave_key(const uint32_t *crypt, const uint8_t *packed,
                          size_t packed_size, size_t raw_size,
                          uint32_t *file_key) {
    unsigned i;
    unsigned matches = 0U;
    uint8_t first[12];
    uint32_t cipher;
    if (packed_size < sizeof(first) || raw_size < sizeof(first)) return false;
    cipher = mpq_le32(packed);
    for (i = 0U; i < 256U; ++i) {
        uint32_t key = (cipher ^ 0x46464952U) -
                       (0xeeeeeeeeU + crypt[0x400U + i]);
        if ((key & 255U) != i) continue;
        xx_mem_copy(first, packed, sizeof(first));
        mpq_decrypt_block(crypt, first, sizeof(first), key);
        if (mpq_le32(first) == 0x46464952U &&
            mpq_le32(first + 4U) == raw_size - 8U &&
            mpq_le32(first + 8U) == 0x45564157U) {
            *file_key = key;
            if (++matches > 1U) return false;
        }
    }
    return matches == 1U;
}

/* Old sector CRC arrays actually contain Adler-32 over decrypted compressed
 * sectors, using seed zero (StormLib adler32(0,...)), before decompression. */
static uint32_t mpq_sector_adler(const uint8_t *data, size_t size) {
    uint32_t a = 0U, b = 0U;
    size_t i;
    for (i = 0U; i < size; ++i) {
        a = (a + data[i]) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

/* StormLib src/sparse/sparse.cpp: a BE32 declared output length followed by
 * literal runs (high bit, length 1..128) or zero runs (length 3..130). The
 * original reader clips overruns; require exact coverage and input here. */
static bool mpq_sparse_decode(const uint8_t *source, size_t source_size,
                              uint8_t *target, size_t target_size,
                              xx_pd_struct *pd) {
    size_t at = 4U, written = 0U;
    uint32_t declared;
    if (!source || !target || source_size < 5U || target_size > UINT32_MAX)
        return false;
    declared = ((uint32_t)source[0] << 24U) |
               ((uint32_t)source[1] << 16U) |
               ((uint32_t)source[2] << 8U) | (uint32_t)source[3];
    if (declared != target_size) return false;
    while (at < source_size) {
        uint8_t token = source[at++];
        size_t run = (token & 0x80U) ? (size_t)(token & 0x7fU) + 1U
                                       : (size_t)token + 3U;
        if ((pd && xx_pd_is_stopped(pd)) || run > target_size - written)
            return false;
        if (token & 0x80U) {
            if (run > source_size - at) return false;
            xx_mem_copy(target + written, source + at, run);
            at += run;
        } else {
            xx_mem_zero(target + written, run);
        }
        written += run;
    }
    return written == target_size;
}

static bool mpq_zlib_decode(const uint8_t *source, size_t source_size,
                            uint8_t *target, size_t capacity,
                            size_t *written, xx_pd_struct *pd) {
    xx_io_device *out;
    size_t consumed = 0U;
    int64_t actual;
    bool result;
    if (!source || !target || !written || source_size < 6U ||
        !xx_zlib_stream_header_is_valid(source, source_size)) return false;
    out = xx_io_mem_open(target, capacity);
    if (!out) return false;
    result = xx_deflate_unpack_memory_to_device_ex(
        source + 2U, source_size - 6U, out, &consumed, false, pd);
    actual = xx_io_tell(out);
    if (xx_io_close(out) != 0) result = false;
    result = result && consumed == source_size - 6U && actual >= 0 &&
             (uint64_t)actual <= capacity &&
             xx_zlib_stream_trailer_matches(source, source_size, target,
                                            (size_t)actual);
    if (result) *written = (size_t)actual;
    return result;
}

static bool mpq_bzip2_decode(const uint8_t *source, size_t source_size,
                             uint8_t *target, size_t capacity,
                             size_t *written, xx_pd_struct *pd) {
    xx_io_device *out;
    int64_t actual;
    bool result;
    if (!source || !target || !written || source_size < 4U ||
        source[0] != (uint8_t)'B' || source[1] != (uint8_t)'Z' ||
        source[2] != (uint8_t)'h' || source[3] < (uint8_t)'1' ||
        source[3] > (uint8_t)'9') return false;
    out = xx_io_mem_open(target, capacity);
    if (!out) return false;
    result = xx_bzip2_unpack_memory_to_device(source, source_size, out, pd);
    actual = xx_io_tell(out);
    if (xx_io_close(out) != 0) result = false;
    if (!result || actual < 0 || (uint64_t)actual > capacity) return false;
    *written = (size_t)actual;
    return true;
}

/* The StormLib V1 writer only records a compression bit when its stage
 * actually shrinks the input by at least two bytes. Its intermediate frames
 * therefore fit in the uncompressed sector's allocation. Keep the accepted
 * combinations explicit: each has an original-writer sector oracle. */
static bool mpq_decode_verified_chain(unsigned mask,
                                      const uint8_t *source,
                                      size_t source_size,
                                      uint8_t *plain,size_t plain_size,
                                      xx_pd_struct *pd) {
    unsigned stages[5];size_t stage_count=0U,i,produced=0U;
    uint8_t *scratch;
    bool ok=true;
    switch(mask) {
    case 0x03U: case 0x09U: case 0x0aU: case 0x11U: case 0x18U:
    case 0x21U: case 0x23U: case 0x29U: case 0x31U: break;
    default: return false;
    }
    if(!source || !source_size || !plain || !plain_size ||
       (pd && xx_pd_is_stopped(pd)))return false;
    if(mask & MPQ_COMPRESSION_BZIP2)stages[stage_count++]=MPQ_COMPRESSION_BZIP2;
    if(mask & MPQ_COMPRESSION_PKWARE)stages[stage_count++]=MPQ_COMPRESSION_PKWARE;
    if(mask & MPQ_COMPRESSION_ZLIB)stages[stage_count++]=MPQ_COMPRESSION_ZLIB;
    if(mask & MPQ_COMPRESSION_HUFFMAN)stages[stage_count++]=MPQ_COMPRESSION_HUFFMAN;
    if(mask & MPQ_COMPRESSION_SPARSE)stages[stage_count++]=MPQ_COMPRESSION_SPARSE;
    if(stage_count<2U || stage_count>5U)return false;
    scratch=(uint8_t *)xx_mem_alloc(plain_size);
    if(!scratch)return false;
    for(i=0U;i<stage_count && ok;++i) {
        unsigned stage=stages[i];
        uint8_t *target=((stage_count-i)&1U)?plain:scratch;
        produced=0U;
        if(stage==MPQ_COMPRESSION_BZIP2)
            ok=mpq_bzip2_decode(source,source_size,target,plain_size,
                                  &produced,pd);
        else if(stage==MPQ_COMPRESSION_PKWARE) {
            size_t consumed=0U,scanned=0U;
            ok=xx_dcl_scan_memory(source,source_size,plain_size,
                                  &consumed,&scanned) &&
               consumed==source_size && scanned>0U &&
               xx_dcl_decode_memory(source,source_size,target,
                                    scanned,&produced) &&
               produced==scanned;
        } else if(stage==MPQ_COMPRESSION_ZLIB)
            ok=mpq_zlib_decode(source,source_size,target,plain_size,
                                &produced,pd);
        else if(stage==MPQ_COMPRESSION_HUFFMAN)
            ok=mpq_huff_decode(source,source_size,target,plain_size,
                                &produced,pd);
        else if(stage==MPQ_COMPRESSION_SPARSE) {
            ok=i+1U==stage_count &&
               mpq_sparse_decode(source,source_size,target,plain_size,pd);
            if(ok)produced=plain_size;
        } else ok=false;
        if(!ok || produced==0U || produced>plain_size ||
           (pd && xx_pd_is_stopped(pd)))break;
        source=target;source_size=produced;
    }
    xx_mem_free(scratch);
    return ok && i==stage_count && produced==plain_size &&
           (!pd || !xx_pd_is_stopped(pd));
}

/* MPQ_FILE_COMPRESS sectors carry a method byte. 0x12 is Blizzard's special
 * LZMA method. Only original-producer-verified chain masks are dispatched;
 * arbitrary bit combinations are not inferred from their individual codecs. */
static bool mpq_decode_multi_sector(const uint8_t *packed, size_t packed_size,
                                    uint8_t *plain, size_t plain_size,
                                    xx_pd_struct *pd) {
    const uint8_t *body;
    size_t body_size;
    size_t written = 0U;
    uint8_t *intermediate = NULL;
    bool ok = false;
    if (!packed || !plain || packed_size < 2U || plain_size == 0U ||
        (pd && xx_pd_is_stopped(pd))) return false;
    body = packed + 1U;
    body_size = packed_size - 1U;
    if (packed[0] == MPQ_COMPRESSION_HUFFMAN) {
        ok = mpq_huff_decode(body, body_size, plain, plain_size,
                             &written, pd);
    } else if (packed[0] == MPQ_COMPRESSION_ADPCM_MONO ||
               packed[0] == MPQ_COMPRESSION_ADPCM_STEREO) {
        unsigned channels = packed[0] == MPQ_COMPRESSION_ADPCM_STEREO
                                ? 2U : 1U;
        ok = mpq_adpcm_decode(body, body_size, plain, plain_size,
                              channels, pd);
        if (ok) written = plain_size;
    } else if (packed[0] == (MPQ_COMPRESSION_HUFFMAN |
                             MPQ_COMPRESSION_ADPCM_MONO) ||
               packed[0] == (MPQ_COMPRESSION_HUFFMAN |
                             MPQ_COMPRESSION_ADPCM_STEREO)) {
        unsigned channels = packed[0] & MPQ_COMPRESSION_ADPCM_STEREO ? 2U : 1U;
        intermediate = (uint8_t *)xx_mem_alloc(plain_size);
        if (!intermediate) return false;
        ok = mpq_huff_decode(body, body_size, intermediate, plain_size,
                              &written, pd) &&
             mpq_adpcm_decode(intermediate, written, plain, plain_size,
                               channels, pd);
        if (ok) written = plain_size;
    } else if (packed[0] == MPQ_COMPRESSION_ZLIB) {
        ok = mpq_zlib_decode(body, body_size, plain, plain_size,
                             &written, pd);
    } else if (packed[0] == MPQ_COMPRESSION_BZIP2) {
        ok = mpq_bzip2_decode(body, body_size, plain, plain_size,
                              &written, pd);
    } else if (packed[0] == MPQ_COMPRESSION_PKWARE) {
        size_t consumed = 0U, produced = 0U;
        ok = xx_dcl_scan_memory(body, body_size, plain_size,
                                &consumed, &produced) &&
             consumed == body_size && produced == plain_size &&
             xx_dcl_decode_memory(body, body_size, plain,
                                  plain_size, &written);
    } else if (packed[0] == MPQ_COMPRESSION_SPARSE) {
        ok = mpq_sparse_decode(body, body_size, plain, plain_size, pd);
        written = ok ? plain_size : 0U;
    } else if (packed[0] == (MPQ_COMPRESSION_SPARSE | MPQ_COMPRESSION_ZLIB) ||
               packed[0] == (MPQ_COMPRESSION_SPARSE | MPQ_COMPRESSION_BZIP2)) {
        /* Successful StormLib sparse compression always shrinks its input.
         * Thus the intermediate frame cannot exceed the plain sector size. */
        intermediate = (uint8_t *)xx_mem_alloc(plain_size);
        if (!intermediate) return false;
        ok = packed[0] == (MPQ_COMPRESSION_SPARSE | MPQ_COMPRESSION_ZLIB)
                 ? mpq_zlib_decode(body, body_size, intermediate, plain_size,
                                    &written, pd)
                 : mpq_bzip2_decode(body, body_size, intermediate, plain_size,
                                     &written, pd);
        ok = ok && mpq_sparse_decode(intermediate, written, plain,
                                      plain_size, pd);
        if (ok) written = plain_size;
    } else if (packed[0] == (MPQ_COMPRESSION_SPARSE | MPQ_COMPRESSION_PKWARE)) {
        size_t consumed = 0U, produced = 0U;
        intermediate = (uint8_t *)xx_mem_alloc(plain_size);
        if (!intermediate) return false;
        ok = xx_dcl_scan_memory(body, body_size, plain_size,
                                &consumed, &produced) &&
             consumed == body_size && produced >= 5U &&
             produced <= plain_size &&
             xx_dcl_decode_memory(body, body_size, intermediate,
                                  produced, &written) &&
             written == produced &&
             mpq_sparse_decode(intermediate, written, plain,
                               plain_size, pd);
        if (ok) written = plain_size;
    } else if (packed[0] == MPQ_COMPRESSION_LZMA) {
        uint64_t declared;
        xx_io_device *out;
        int64_t actual;
        if (body_size < 15U || body[0] != 0U) return false;
        declared = (uint64_t)mpq_le32(body + 6U) |
                   ((uint64_t)mpq_le32(body + 10U) << 32U);
        if (declared != plain_size) return false;
        out = xx_io_mem_open(plain, plain_size);
        if (!out) return false;
        ok = xx_lzma_unpack_memory_to_device(body + 14U, body_size - 14U,
                                              body + 1U, 5U,
                                              (int64_t)plain_size, out, pd);
        actual = xx_io_tell(out);
        if (xx_io_close(out) != 0) ok = false;
        if (ok && actual >= 0) written = (size_t)actual;
    } else {
        ok=mpq_decode_verified_chain(packed[0],body,body_size,
                                     plain,plain_size,pd);
        if(ok)written=plain_size;
    }
    if (intermediate) xx_mem_free(intermediate);
    return ok && written == plain_size &&
           (!pd || !xx_pd_is_stopped(pd));
}

static bool mpq_write_member(Abstractformat *format, mpq_stream *stream,
                             const mpq_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *packed = NULL, *plain = NULL, *offsets = NULL;
    uint32_t crypt[0x500], key = 0U;
    size_t packed_size, raw_size, sector_size, count, table_size = 0U, i;
    bool compressed, single, checksums, result = false;
    int64_t source_cursor = -1;
    int error = XXFC_ERR_INVALID_ARG;
    const char *reason = "Invalid MPQ sector framing or compressed data";
    if (!format || !format->device || !stream || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if ((member->flags & MPQ_FILE_PATCH) != 0U ||
        (member->method != 0U && member->method != MPQ_FILE_IMPLODE &&
         member->method != MPQ_FILE_COMPRESS)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        "MPQ patch or compression method unsupported");
        return false;
    }
    if (!member->encrypted && member->method == 0U) {
        if ((uint64_t)member->packed_size != member->unpacked_size) return false;
        return mpq_copy_range(format->device, member->data_offset,
                              member->unpacked_size, destination, pd);
    }
    if (member->packed_size < 0 || member->packed_size > MPQ_DECODE_LIMIT ||
        member->unpacked_size > MPQ_DECODE_LIMIT) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "MPQ decode allocation limit");
        return false;
    }
    packed_size = (size_t)member->packed_size;
    raw_size = (size_t)member->unpacked_size;
    if (raw_size == 0U) return packed_size == 0U;
    sector_size = (size_t)stream->aux0;
    if (sector_size == 0U || packed_size == 0U) return false;
    compressed = member->method != 0U;
    single = (member->flags & MPQ_FILE_SINGLE_UNIT) != 0U;
    checksums = (member->flags & MPQ_FILE_SECTOR_CRC) != 0U;
    if (checksums && (!compressed || single)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "MPQ checksum flag without sector table");
        return false;
    }
    count = single ? 1U : (raw_size + sector_size - 1U) / sector_size;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    plain = (uint8_t *)xx_mem_alloc(raw_size);
    if (!packed || !plain) { error = XXFC_ERR_OUT_OF_MEMORY; reason = "MPQ decode allocation failed"; goto done; }
    source_cursor = xx_io_tell(format->device);
    if (source_cursor < 0) { error = XXFC_ERR_IO; goto done; }
    if (!mpq_read_at(format->device, member->data_offset, packed, packed_size)) {
        error = XXFC_ERR_IO; reason = "MPQ member read failed"; goto done;
    }
    mpq_build_crypt_table(crypt);
    if (compressed && !single) {
        table_size = (count + (checksums ? 2U : 1U)) * 4U;
        if (table_size > packed_size) goto done;
        offsets = (uint8_t *)xx_mem_alloc(table_size);
        if (!offsets) { error = XXFC_ERR_OUT_OF_MEMORY; goto done; }
        if (member->encrypted) {
            if (!mpq_sector_key(crypt, packed, count, table_size, packed_size,
                                 raw_size, sector_size, checksums, offsets, &key)) {
                reason = "MPQ encrypted sector table has no unique valid key"; goto done;
            }
        } else {
            xx_mem_copy(offsets, packed, table_size);
            if (!mpq_sector_table_valid(offsets, count, table_size, packed_size,
                                        raw_size, sector_size, checksums)) goto done;
        }
    } else {
        if (member->encrypted &&
            (compressed || !mpq_wave_key(crypt, packed, packed_size, raw_size, &key))) {
            error = XXFC_ERR_INVALID_ARG;
            reason = "MPQ encrypted member has no recoverable WAVE or sector-table key"; goto done;
        }
        if ((!compressed && packed_size != raw_size) || packed_size > raw_size) goto done;
    }
    for (i = 0U; i < count; ++i) {
        size_t raw_at = single ? 0U : i * sector_size;
        size_t want = raw_size - raw_at;
        size_t begin, end, size;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!single && want > sector_size) want = sector_size;
        begin = offsets ? mpq_le32(offsets + i * 4U) : raw_at;
        end = offsets ? mpq_le32(offsets + (i + 1U) * 4U) :
                         (single ? packed_size : raw_at + want);
        if (begin > end || end > packed_size) goto done;
        size = end - begin;
        if (member->encrypted) mpq_decrypt_block(crypt, packed + begin, size, key + (uint32_t)i);
        if (checksums) {
            size_t crc_at = mpq_le32(offsets + count * 4U) + i * 4U;
            uint32_t expected = mpq_le32(packed + crc_at);
            if (expected != 0U && expected != UINT32_MAX &&
                expected != mpq_sector_adler(packed + begin, size)) {
                reason = "MPQ sector Adler-32 checksum mismatch"; goto done;
            }
        }
        if (size == want) xx_mem_copy(plain + raw_at, packed + begin, want);
        else if (member->method == MPQ_FILE_COMPRESS) {
            if (size > want ||
                !mpq_decode_multi_sector(packed + begin, size,
                                         plain + raw_at, want, pd)) goto done;
        } else {
            size_t consumed = 0U, produced = 0U, written = 0U;
            if (!compressed || size > want ||
                !xx_dcl_scan_memory(packed + begin, size, want, &consumed, &produced) ||
                consumed != size || produced != want ||
                !xx_dcl_decode_memory(packed + begin, size, plain + raw_at, want, &written) ||
                written != want) goto done;
        }
    }
    result = mpq_write_all(destination, plain, raw_size, pd);
    if (!result) { error = XXFC_ERR_IO; reason = "MPQ output write failed"; }
done:
    if (source_cursor >= 0 && xx_io_seek64(format->device, source_cursor, SEEK_SET) != 0) {
        result = false; error = XXFC_ERR_IO; reason = "MPQ source cursor restore failed";
    }
    if (!result && !(pd && xx_pd_is_stopped(pd))) xx_pd_set_error(pd, error, reason);
    if (offsets) xx_mem_free(offsets);
    if (plain) xx_mem_free(plain);
    if (packed) xx_mem_free(packed);
    return result;
}

static bool mpq_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *mpq_option(Abstractformat *format,
                                const xx_list_s *options, uint32_t id) {
    return xx_format_resolve_extra_parameter(format, options, id);
}

static bool mpq_same_path_folded(const char *a, const char *b) {
    if (!a || !b) return false;
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= (unsigned char)'A' && x <= (unsigned char)'Z')
            x = (unsigned char)(x + (unsigned char)('a' - 'A'));
        if (y >= (unsigned char)'A' && y <= (unsigned char)'Z')
            y = (unsigned char)(y + (unsigned char)('a' - 'A'));
        if (x != y) return false;
        if (x == 0U) return true;
    }
}

static bool mpq_set_record(xx_archive_record *record,
                           const mpq_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_mpq_init(xx_mpq *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MPQ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mpq");
    xx_format_set_extension(&archive->format, "mpq");
    archive->format.check_is_valid = xx_mpq_check_is_valid;
    archive->format.handle_base_info = xx_mpq_handle_base_info;
    archive->format.get_format_size = xx_mpq_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mpq_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mpq_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mpq_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mpq_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mpq_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mpq_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_mpq *xx_mpq_create(xx_io_device *device, int64_t base_address) {
    xx_mpq *archive = (xx_mpq *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_mpq_init(archive, device, base_address);
    return archive;
}

void xx_mpq_destroy(xx_mpq *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_mpq_free(xx_mpq *archive) {
    if (!archive) return;
    xx_mpq_destroy(archive);
    xx_mem_free(archive);
}

bool xx_mpq_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    mpq_stream *stream;
    (void)pd;
    if (!mpq_parse(format, &stream)) return false;
    mpq_stream_free(stream);
    return true;
}

bool xx_mpq_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    mpq_stream *stream;
    xx_mpq *archive;
    (void)pd;
    if (!format || !mpq_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_mpq *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_MPQ_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    mpq_stream_free(stream);
    return true;
}

int64_t xx_mpq_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_mpq_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_mpq_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_mpq_handle_base_info(format, pd))
               ? ((xx_mpq *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_mpq_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    mpq_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!mpq_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mpq_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = mpq_stream_free;
    state->total_records = stream->count;
    if (!mpq_copy_options(&state->options, options) ||
        !mpq_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_mpq_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mpq_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    mpq_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (mpq_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        mpq_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mpq_unpack_current_archive_record_to_device(
    Abstractformat *format, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd) {
    mpq_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mpq_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    return mpq_write_member(format, stream, &stream->items[stream->index],
                            destination, pd);
}

bool xx_mpq_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    mpq_stream *stream;
    mpq_member *member;
    const xx_var *path_option;
    const xx_var *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    char *stage_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool overwrite = false;
    size_t prefix = 0U, index;
    unsigned attempt;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mpq_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!mpq_safe_output_name(member->name)) return false;
    path_option = mpq_option(format, &state->options,
                             XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_mpq_unpack_current_archive_record_to_device(
            format, state, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = mpq_option(format, &state->options,
                                   XX_META_ID_OPT_OVERWRITE);
    if (overwrite_option) overwrite = xx_var_get_bool(overwrite_option);
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if ((!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    for (index = 0U; path[index]; ++index)
        if (path[index] == '/' || path[index] == '\\') prefix = index + 1U;
    stage_path = (char *)xx_mem_alloc(prefix + 50U);
    if (!stage_path) goto done;
    xx_mem_copy(stage_path, path, prefix);
    for (attempt = 0U; attempt < 128U && !(pd && xx_pd_is_stopped(pd));
         ++attempt) {
        int written = xx_rt_snprintf(stage_path + prefix, 50U,
                                     ".xxfc-mpq-%u-%u.tmp",
                                     (unsigned)stream->index, attempt);
        if (written <= 0 || written >= 50) goto done;
        if (mpq_same_path_folded(stage_path, path)) continue;
        destination = xx_io_file_open(stage_path, "wbx");
        if (destination) break;
    }
    if (!destination) goto done;
    result = xx_mpq_unpack_current_archive_record_to_device(
        format, state, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
    if (result && !(pd && xx_pd_is_stopped(pd)))
        result = xx_io_file_replace_a(stage_path, path, overwrite);
    else result = false;
done:
    if (destination) (void)xx_io_close(destination);
    if (!result && stage_path) (void)xx_io_file_remove_a(stage_path);
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_mpq_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
