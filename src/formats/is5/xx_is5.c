/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for InstallShield 5 (and later) cabinets, magic "ISc(".
 *
 * The layout follows unshield's description of the format (libunshield's
 * CommonHeader / CabDescriptor / FileDescriptor, and its chunked inflate in
 * uncompress_old / unshield_uncompress), re-derived field by field against
 * the 108 samples in F:\ARC\ARC\IS5 so that the pre-5 descriptor without an
 * MD5 and the version 6/7 descriptor are both covered.
 *
 *   Common header (20 bytes)
 *     0x00 u32 signature 0x28635349 "ISc("
 *     0x04 u32 version; major = (version >> 12) & 0xF when the top byte is 1
 *     0x08 u32 volume info
 *     0x0C u32 cabinet descriptor offset
 *     0x10 u32 cabinet descriptor size
 *
 *   Cabinet descriptor, at the offset above
 *     +0x0C u32 file table offset, relative to the descriptor
 *     +0x14 u32 file table size
 *     +0x18 u32 file table size (repeated)
 *     +0x1C u32 directory count
 *     +0x28 u32 file count
 *     +0x2C u32 second file table offset, relative to the file table
 *
 *   File table.  The first "directory count" u32 entries are offsets of
 *   directory names.  For major version 5 and below the next "file count"
 *   u32 entries are offsets of file descriptors; for 6 and above the
 *   descriptors are a flat array of 0x57 bytes each at the second file table
 *   offset.  All these offsets are relative to the file table.
 *
 *   File descriptor, major version <= 5 (0x2A bytes, or 0x3A with an MD5)
 *     +0x00 u32 name offset       +0x04 u32 directory index
 *     +0x08 u16 flags             +0x0A u32 expanded size
 *     +0x0E u32 compressed size   +0x26 u32 data offset
 *
 *   File descriptor, major version >= 6 (0x57 bytes)
 *     +0x00 u16 flags             +0x02 u64 expanded size
 *     +0x0A u64 compressed size   +0x12 u64 data offset
 *     +0x1A md5[16]               +0x3A u32 name offset
 *     +0x3E u16 directory index
 *
 * A compressed member is stored in one of TWO layouts and the descriptor does
 * not say which: either a run of raw deflate chunks each preceded by a u16
 * byte count, or one continuous raw deflate stream over the whole extent.
 * Both are tried and the stored expanded size decides.  Either way the packer
 * ends its deflate streams without a final block, so a strict decoder reports
 * failure after emitting every byte it had - which is why the return value is
 * not the acceptance test here, the expanded size is.
 *
 * The 0x0002 "obfuscated" flag is not reliable: writers set it on members
 * that were never scrambled.  It therefore selects a SECOND decode attempt
 * (descramble, then expand again) rather than the only one, and the expanded
 * size says which attempt was right.  A STORED member offers no such anchor,
 * so the flag is not acted on there at all.
 *
 *   Volume header, immediately after the common header at 0x14.  The first
 *   ten u32 for major version <= 5, sixteen (each value followed by its high
 *   dword) for 6 and above; only the first four fields are used here and
 *   they sit at the same place in both:
 *     +0x00 u32 data_offset        where THIS volume's member data begins
 *     +0x08 u32 first_file_index   first descriptor whose data is here
 *     +0x0C u32 last_file_index    last descriptor whose data is here
 *
 * WHICH MEMBERS ARE ACTUALLY IN THIS FILE.  A descriptor's data_offset is an
 * offset into the volume that holds it, which is NOT necessarily this one:
 * an installation is a header (data1.hdr) plus one or more data volumes
 * (data1.cab, data2.cab, ...), and unshield resolves the volume from the
 * descriptor's index before it reads a byte.  A path-less xx_io_device
 * cannot open a sibling, so the only question here is whether a member is in
 * THIS file, and the answer has to come from the volume header rather than
 * from arithmetic:
 *
 *   - data_offset >= file size means the volume carries no member data at
 *     all.  Every .hdr in the corpus says exactly this, and so do the
 *     header-only .cab files some writers emit.  Without this test a few
 *     descriptors per header happen to point at bytes that are inside the
 *     file - the header's own bytes - and the decoder then "succeeds" into
 *     the caller's output file with the cabinet header and a run of zeroes.
 *     That is the worst possible outcome and this test is what prevents it.
 *   - a descriptor outside [first_file_index, last_file_index] belongs to
 *     another volume; its offsets are meaningless here even when they land
 *     inside this file.  The range is only trusted when last_file_index is
 *     non-zero, because a zeroed volume header says nothing.
 *
 * A member that is not in this file is still listed - it is part of the
 * installation and the caller may want to see it - but with data_offset -1,
 * and extracting it fails rather than inventing bytes.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/is5/xx_is5.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_IS5 exists in the enum. */
#ifdef IS5
#define XX_IS5_FILE_TYPE XX_FILE_TYPE_IS5
#else
#define XX_IS5_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IS5_SIGNATURE UINT32_C(0x28635349)
#define IS5_COMMON_HEADER_SIZE 20U
/* Only the first four u32 of the volume header are read; they sit at the
 * same offsets in the version <= 5 and the version >= 6 layouts. */
#define IS5_VOLUME_HEADER_SIZE 16U
#define IS5_DESCRIPTOR_MIN_SIZE 0x30U
#define IS5_DESCRIPTOR_READ_SIZE 0x40U
#define IS5_OLD_DESCRIPTOR_SIZE 0x2AU
#define IS5_NEW_DESCRIPTOR_SIZE 0x57U

#define IS5_FLAG_SPLIT 0x0001U
#define IS5_FLAG_OBFUSCATED 0x0002U
#define IS5_FLAG_COMPRESSED 0x0004U
#define IS5_FLAG_INVALID 0x0008U

#define IS5_MAX_MEMBERS 200000U
#define IS5_MAX_NAME 1024U
#define IS5_MAX_FILE_TABLE UINT32_C(0x4000000) /* 64 MiB */
#define IS5_MAX_MEMBER_SIZE UINT64_C(0x40000000)

typedef struct is5_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset; /**< -1 when the payload is not in this file. */
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t directory_index;
    uint16_t flags;
    bool present;
} is5_member;

typedef struct is5_stream_s {
    is5_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t major_version;
} is5_stream;

static uint16_t is5_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t is5_le32(const uint8_t *bytes) {
    return (uint32_t)is5_le16(bytes) | ((uint32_t)is5_le16(bytes + 2U) << 16U);
}

static uint64_t is5_le64(const uint8_t *bytes) {
    return (uint64_t)is5_le32(bytes) | ((uint64_t)is5_le32(bytes + 4U) << 32U);
}

static bool is5_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Normalize only the filesystem-facing representation.  Cabinet names are
 * ANSI byte strings, so non-ASCII bytes are retained verbatim while
 * separators and traversal components are made harmless. */
static char *is5_normalize_name(const uint8_t *bytes, size_t size) {
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

static bool is5_safe_output_name(const char *name) {
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

static void is5_stream_free(void *opaque) {
    is5_stream *stream = (is5_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool is5_add_member(is5_stream *stream, const is5_member *member) {
    is5_member *grown;
    if (!stream || !member || stream->count >= IS5_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (is5_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Read a NUL terminated name out of the file table image.  A name that runs
 * off the table, or is empty, or carries a control byte, makes the archive
 * invalid rather than producing a guessed name. */
static char *is5_table_name(const uint8_t *table, uint32_t table_size,
                            uint32_t offset) {
    uint32_t end;
    if (offset >= table_size) return NULL;
    for (end = offset; end < table_size && table[end] != 0U; ++end) {
        if (table[end] < 0x20U) return NULL;
    }
    if (end == table_size || end == offset || end - offset > IS5_MAX_NAME)
        return NULL;
    return is5_normalize_name(table + offset, (size_t)(end - offset));
}

static bool is5_parse(Abstractformat *format, is5_stream **result) {
    uint8_t common[IS5_COMMON_HEADER_SIZE];
    uint8_t descriptor[IS5_DESCRIPTOR_READ_SIZE];
    uint8_t *table = NULL;
    is5_stream *stream = NULL;
    int64_t total, size;
    uint8_t volume[IS5_VOLUME_HEADER_SIZE];
    uint32_t version, descriptor_offset, descriptor_size;
    uint32_t table_offset, table_size, table_offset2;
    uint32_t directory_count, file_count, major, index;
    uint32_t volume_data_offset = 0U, first_file = 0U, last_file = 0U;
    bool volume_has_data = true, volume_range_known = false;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)IS5_COMMON_HEADER_SIZE ||
        !is5_read_at(format->device, format->base_address, common,
                     sizeof(common)) ||
        is5_le32(common) != IS5_SIGNATURE)
        return false;
    /* The volume header follows the common header.  A file too small to hold
     * one is a header with no data area, which is exactly what the absent
     * volume header would have said. */
    if (size >= (int64_t)IS5_COMMON_HEADER_SIZE + IS5_VOLUME_HEADER_SIZE &&
        is5_read_at(format->device,
                    format->base_address + (int64_t)IS5_COMMON_HEADER_SIZE,
                    volume, sizeof(volume))) {
        volume_data_offset = is5_le32(volume);
        first_file = is5_le32(volume + 8U);
        last_file = is5_le32(volume + 12U);
        volume_has_data = (int64_t)volume_data_offset < size;
        /* A zeroed last_file_index carries no information; only a volume
         * header that names a real range is allowed to exclude members. */
        volume_range_known = last_file != 0U && first_file <= last_file;
    } else {
        volume_has_data = false;
    }
    version = is5_le32(common + 4U);
    descriptor_offset = is5_le32(common + 12U);
    descriptor_size = is5_le32(common + 16U);
    major = ((version >> 24U) == 1U) ? ((version >> 12U) & 0x0FU) : 0U;
    if ((int64_t)descriptor_offset > size ||
        (int64_t)descriptor_size > size - (int64_t)descriptor_offset ||
        descriptor_size < IS5_DESCRIPTOR_MIN_SIZE)
        return false;
    if (!is5_read_at(format->device,
                     format->base_address + (int64_t)descriptor_offset,
                     descriptor, IS5_DESCRIPTOR_READ_SIZE > descriptor_size
                                     ? IS5_DESCRIPTOR_MIN_SIZE
                                     : IS5_DESCRIPTOR_READ_SIZE))
        return false;

    table_offset = is5_le32(descriptor + 0x0CU);
    table_size = is5_le32(descriptor + 0x14U);
    directory_count = is5_le32(descriptor + 0x1CU);
    file_count = is5_le32(descriptor + 0x28U);
    table_offset2 = is5_le32(descriptor + 0x2CU);

    /* The file table is the one allocation this header can influence, so it
     * is bounded against the real file size and against a fixed ceiling
     * before anything is read. */
    if (file_count == 0U || file_count > IS5_MAX_MEMBERS ||
        directory_count > IS5_MAX_MEMBERS || table_size == 0U ||
        table_size > IS5_MAX_FILE_TABLE ||
        (int64_t)descriptor_offset + (int64_t)table_offset > size ||
        (int64_t)table_size >
            size - ((int64_t)descriptor_offset + (int64_t)table_offset))
        return false;
    if (major >= 6U) {
        if (table_offset2 > table_size ||
            (uint64_t)file_count * IS5_NEW_DESCRIPTOR_SIZE >
                (uint64_t)(table_size - table_offset2))
            return false;
    } else {
        uint64_t needed = ((uint64_t)directory_count + file_count) * 4U;
        if (needed > table_size) return false;
    }

    table = (uint8_t *)xx_mem_alloc(table_size);
    stream = (is5_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!table || !stream ||
        !is5_read_at(format->device,
                     format->base_address + (int64_t)descriptor_offset +
                         (int64_t)table_offset,
                     table, table_size))
        goto fail;
    stream->major_version = major;

    for (index = 0U; index < file_count; ++index) {
        is5_member member;
        const uint8_t *entry;
        uint32_t entry_offset, name_offset;
        uint64_t packed, unpacked, data_offset;

        if (major >= 6U) {
            entry_offset = table_offset2 + index * IS5_NEW_DESCRIPTOR_SIZE;
            entry = table + entry_offset;
            xx_mem_zero(&member, sizeof(member));
            member.flags = is5_le16(entry);
            unpacked = is5_le64(entry + 2U);
            packed = is5_le64(entry + 10U);
            data_offset = is5_le64(entry + 18U);
            name_offset = is5_le32(entry + 58U);
            member.directory_index = is5_le16(entry + 62U);
            member.header_size = IS5_NEW_DESCRIPTOR_SIZE;
        } else {
            entry_offset = is5_le32(table + (directory_count + index) * 4U);
            if (entry_offset > table_size ||
                table_size - entry_offset < IS5_OLD_DESCRIPTOR_SIZE)
                goto fail;
            entry = table + entry_offset;
            xx_mem_zero(&member, sizeof(member));
            name_offset = is5_le32(entry);
            member.directory_index = is5_le32(entry + 4U);
            member.flags = is5_le16(entry + 8U);
            unpacked = is5_le32(entry + 10U);
            packed = is5_le32(entry + 14U);
            data_offset = is5_le32(entry + 38U);
            member.header_size = IS5_OLD_DESCRIPTOR_SIZE;
        }
        member.header_offset = format->base_address +
                               (int64_t)descriptor_offset +
                               (int64_t)table_offset + (int64_t)entry_offset;
        member.unpacked_size = unpacked;
        member.packed_size = packed > (uint64_t)INT64_MAX ? 0
                                                          : (int64_t)packed;
        member.data_offset = -1;
        member.present = false;
        /* An entry marked invalid, or one with no payload at all, is a
         * placeholder the writer left behind.  Its name offset is garbage as
         * well, so it is dropped instead of being given an invented name. */
        if ((member.flags & IS5_FLAG_INVALID) != 0U || packed == 0U) continue;
        member.name = is5_table_name(table, table_size, name_offset);
        /* See the volume note at the top of this file: geometry alone is not
         * enough, because a descriptor belonging to another volume can name
         * an offset that happens to be inside this one.  The member is
         * published either way; only its reachability changes. */
        if (volume_has_data &&
            (!volume_range_known || (index >= first_file && index <= last_file)) &&
            data_offset >= (uint64_t)volume_data_offset &&
            data_offset <= (uint64_t)INT64_MAX &&
            (int64_t)data_offset <= size &&
            member.packed_size <= size - (int64_t)data_offset &&
            format->base_address <= INT64_MAX - (int64_t)data_offset) {
            member.data_offset = format->base_address + (int64_t)data_offset;
            member.present = true;
        }
        if (!member.name || !is5_add_member(stream, &member)) {
            if (member.name) xx_str_free(member.name);
            goto fail;
        }
    }
    /* A cabinet whose every descriptor is a placeholder is well formed and
     * simply empty; the signature and the descriptor geometry have already
     * been checked, so it stays valid with no members. */
    xx_mem_free(table);
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (table) xx_mem_free(table);
    is5_stream_free(stream);
    return false;
}

static bool is5_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *is5_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool is5_set_record(xx_archive_record *record, const is5_member *member) {
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
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               (member->flags & IS5_FLAG_COMPRESSED) != 0U ? 1U : 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_DISK_NUMBER_START,
                                          member->directory_index) &&
           xx_archive_record_set_meta_bool(
               record, XX_META_ID_IS_ENCRYPTED,
               (member->flags & IS5_FLAG_OBFUSCATED) != 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* A compressed member is a run of raw deflate chunks, each introduced by the
 * u16 count of compressed bytes that follow.  The stored expanded size is the
 * anchor: a run that does not land on it exactly is rejected. */
static bool is5_inflate_chunks(const uint8_t *packed, size_t packed_size,
                               uint8_t *output, size_t output_size,
                               size_t *written) {
    size_t read = 0U, produced = 0U;
    while (read < packed_size) {
        size_t chunk;
        size_t chunk_written = 0U;
        if (packed_size - read < 2U) return false;
        chunk = (size_t)is5_le16(packed + read);
        read += 2U;
        if (chunk == 0U || chunk > packed_size - read) return false;
        /* Each chunk is a raw deflate stream the packer ends without a final
         * block, so the decoder reports failure after emitting every byte it
         * had.  What it produced is the chunk; the run as a whole is judged
         * by the caller against the stored expanded size. */
        (void)xx_deflate_decompress_memory(packed + read, chunk,
                                           output + produced,
                                           output_size - produced,
                                           &chunk_written, false);
        if (chunk_written == 0U) return false;
        read += chunk;
        produced += chunk_written;
        if (produced > output_size) return false;
    }
    *written = produced;
    return true;
}

/* InstallShield's "obfuscation": each byte is XORed with 0xD5, rotated right
 * by two, then decremented by a counter that cycles modulo 0x47.  It is a
 * transport scramble, not encryption, and carries no key. */
static void is5_deobfuscate(uint8_t *data, size_t size) {
    size_t index;
    uint32_t seed = 0U;
    for (index = 0U; index < size; ++index) {
        uint8_t value = (uint8_t)(data[index] ^ 0xD5U);
        value = (uint8_t)((value >> 2) | (value << 6));
        data[index] = (uint8_t)(value - (uint8_t)(seed % 0x47U));
        ++seed;
    }
}

/* One decode attempt over @p packed.  Two compressed layouts exist and the
 * descriptor does not say which is in use: the older cabinets cut the member
 * into u16-prefixed deflate chunks, the later ones store one continuous raw
 * deflate stream over the whole extent.  Both are tried; the stored expanded
 * size is what decides, and the caller checks it. */
static size_t is5_expand(const uint8_t *packed, size_t packed_size,
                         uint8_t *output, size_t output_size) {
    size_t written = 0U;
    if (is5_inflate_chunks(packed, packed_size, output, output_size,
                           &written) &&
        written == output_size) {
        return written;
    }
    /* The packer ends these streams without a final block, so the decoder
     * reports failure after producing every byte it had.  The return value is
     * therefore not the test - the declared expanded size is. */
    written = 0U;
    (void)xx_deflate_decompress_memory(packed, packed_size, output,
                                       output_size, &written, false);
    return written;
}

static bool is5_decode_member(Abstractformat *format, const is5_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    if (!format || !member || !plain || !plain_size || !member->present ||
        member->packed_size <= 0 ||
        (member->flags & IS5_FLAG_SPLIT) != 0U ||
        member->unpacked_size == 0U ||
        member->unpacked_size > IS5_MAX_MEMBER_SIZE ||
        member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!packed || !output ||
        !is5_read_at(format->device, member->data_offset, packed,
                     (size_t)member->packed_size))
        goto fail;
    if ((member->flags & IS5_FLAG_COMPRESSED) != 0U) {
        written = is5_expand(packed, (size_t)member->packed_size, output,
                             output_size);
        /* Writers set the obfuscation flag on members they did not actually
         * scramble, so the flag selects a SECOND attempt rather than the only
         * one: descramble and expand again, and let the expanded size say
         * which attempt was right. */
        if (written != output_size &&
            (member->flags & IS5_FLAG_OBFUSCATED) != 0U) {
            is5_deobfuscate(packed, (size_t)member->packed_size);
            written = is5_expand(packed, (size_t)member->packed_size, output,
                                 output_size);
        }
    } else {
        /* A stored member offers no anchor that could tell a descrambled
         * copy from a scrambled one, and the flag has been seen set on
         * members that were never scrambled, so it is not acted on here: the
         * bytes are published as they are found. */
        if (output_size != (size_t)member->packed_size) goto fail;
        xx_mem_copy(output, packed, output_size);
        written = output_size;
    }
    if (written != output_size) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_is5_init(xx_is5 *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IS5_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-installshield-cab");
    xx_format_set_extension(&archive->format, "cab");
    archive->format.check_is_valid = xx_is5_check_is_valid;
    archive->format.handle_base_info = xx_is5_handle_base_info;
    archive->format.get_format_size = xx_is5_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_is5_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_is5_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_is5_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_is5_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_is5_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_is5_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_is5 *xx_is5_create(xx_io_device *device, int64_t base_address) {
    xx_is5 *archive = (xx_is5 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_is5_init(archive, device, base_address);
    return archive;
}

void xx_is5_destroy(xx_is5 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_is5_free(xx_is5 *archive) {
    if (!archive) return;
    xx_is5_destroy(archive);
    xx_mem_free(archive);
}

bool xx_is5_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    is5_stream *stream;
    (void)pd;
    if (!is5_parse(format, &stream)) return false;
    is5_stream_free(stream);
    return true;
}

bool xx_is5_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    is5_stream *stream;
    xx_is5 *archive;
    (void)pd;
    if (!format || !is5_parse(format, &stream)) return false;
    archive = (xx_is5 *)format;
    archive->number_of_records = stream->count;
    archive->major_version = stream->major_version;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    is5_stream_free(stream);
    return true;
}

int64_t xx_is5_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is5_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_is5_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is5_handle_base_info(format, pd))
               ? ((xx_is5 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_is5_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is5_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!is5_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is5_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is5_stream_free;
    state->total_records = stream->count;
    if (!is5_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count == 0U) {
        state->has_record = false;
        return state;
    }
    if (!is5_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_is5_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_is5_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    is5_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is5_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        is5_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_is5_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    is5_stream *stream;
    is5_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is5_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!is5_safe_output_name(member->name) ||
        !is5_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = is5_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_is5_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
