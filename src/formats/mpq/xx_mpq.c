/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Blizzard MoPaQ (MPQ) game archive.
 *
 * Header (32 bytes, or 44 for format version 1), at offset 0 or on any
 * 0x200 boundary
 *   +0x00  "MPQ\x1a"
 *   +0x04  u32  header size       +0x08  u32  archive size
 *   +0x0c  u16  format version    +0x0e  u16  sector size shift
 *   +0x10  u32  hash table offset  +0x14  u32  block table offset
 *   +0x18  u32  hash table entries +0x1c  u32  block table entries
 *   +0x20  u64  high block table offset (version 1)
 *   +0x28  u16  hash offset high   +0x2a  u16  block offset high
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
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
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
    if (!device) return true; /* verify-only pass: nothing is materialized */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy a run of source bytes straight through to the destination. */
static bool mpq_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!mpq_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool mpq_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!mpq_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
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
static bool mpq_parse(Abstractformat *format, mpq_stream **result) {
    uint8_t header[MPQ_HEADER_SIZE_V1];
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
        size_t want = available < MPQ_HEADER_SIZE_V1
                          ? (size_t)available
                          : (size_t)MPQ_HEADER_SIZE_V1;
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
        if (version > 1U || header_size < MPQ_HEADER_SIZE ||
            (uint64_t)header_size > archive_size || archive_size > available ||
            sector_shift > 15U || hash_entries == 0U ||
            hash_entries > MPQ_MAX_TABLE_ENTRIES ||
            (hash_entries & (hash_entries - 1U)) != 0U ||
            block_entries == 0U || block_entries > MPQ_MAX_TABLE_ENTRIES)
            continue;
        if (version == 1U) {
            if (header_size < MPQ_HEADER_SIZE_V1 || want < MPQ_HEADER_SIZE_V1)
                continue;
            hash_offset |= (uint64_t)mpq_le16(header + 40U) << 32U;
            block_offset |= (uint64_t)mpq_le16(header + 42U) << 32U;
        }
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

static bool mpq_write_member(Abstractformat *format, mpq_stream *stream,
                             const mpq_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    (void)stream;
    if (!format || !member) return false;
    /* An encrypted member's key is derived from its NAME, and names live in
     * the "(listfile)" member which is itself usually compressed, so there is
     * nothing to derive a key from here.  A compressed member needs the Storm
     * multi-codec chain (PKWARE implode, zlib, bzip2, huffman, ADPCM), which
     * is not wired up in this reader.  Both fail closed. */
    if (member->encrypted || member->method != 0U) return false;
    if ((uint64_t)member->packed_size != member->unpacked_size) return false;
    return mpq_copy_range(format->device, member->data_offset,
                          member->unpacked_size, destination, pd);
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

static const xx_var *mpq_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
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

bool xx_mpq_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    mpq_stream *stream;
    mpq_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mpq_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!mpq_safe_output_name(member->name)) return false;
    path_option = mpq_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return mpq_write_member(format, stream, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = mpq_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_mpq_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
