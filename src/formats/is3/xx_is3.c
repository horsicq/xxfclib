/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the InstallShield 3 installer data files.
 *
 * Layout was established from the corpora F:\ARC\ARC\IS3 (157 samples) and
 * F:\ARC\ARC\IS3 INST (27 samples) and cross-checked against the published
 * descriptions of the format in Deark's "is_z" module and in the notes that
 * ship with unshield (which itself only covers version 5 and later, so the
 * version 3 cabinet had to be confirmed field by field against the samples).
 *
 * Cabinet (".z", "DATA.n", "_INST32I" payload of a version 3 setup):
 *
 *   0x00 u32  signature 0x8C655D13
 *   0x04 u16  version (0x013A in every observed sample)
 *   0x06 u16  reserved
 *   0x0C u16  number of files in the whole (possibly multi volume) archive
 *   0x0E u32  archive timestamp
 *   0x12 u32  total archive size across all volumes
 *   0x16 u32  total uncompressed size of all members
 *   0x1A u32  offset of the first compressed byte (0xFF in every sample)
 *   0x29 u32  directory table offset
 *   0x2D u32  directory table size
 *   0x31 u16  directory count
 *   0x33 u32  file table offset
 *   0x37 u32  file table size
 *
 * Directory entry:  u16 file count, u16 entry size, u16 name length, name.
 * File entry:       u8 flags, u16 reserved, u32 uncompressed size,
 *                   u32 compressed size, u32 data offset, u32 timestamp,
 *                   u32 attributes, u16 entry size, u16 volume,
 *                   u8 name length, name.
 * Files are handed out in directory order: the first directory owns the first
 * "file count" entries of the file table, and so on.
 *
 * Payload archive ("_INST32I.EX_", "_INST16.EX_"):
 *
 *   0x00 u32  signature 0xD879AB2A
 *   0x04 u32  0x00000100
 *   0x08 char[70] "Copyright (c) 1990-1995 Stirling Technologies, Inc. ..."
 *   0x4E u32  number of files
 *   entry:    u32 data offset, u32 compressed size, u32 uncompressed size,
 *             u32 reserved, u32 reserved, u16 name length, name,
 *             u16 second name length, second name.
 *
 * A member is stored when its compressed and uncompressed sizes agree and
 * PKWARE DCL imploded otherwise; the DCL decoder already in the library is
 * used rather than a private copy.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/is3/xx_is3.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_IS3 exists in the enum. */
#ifdef IS3
#define XX_IS3_FILE_TYPE XX_FILE_TYPE_IS3
#else
#define XX_IS3_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IS3_SIGNATURE UINT32_C(0x8C655D13)
#define IS3_INST_SIGNATURE UINT32_C(0xD879AB2A)
#define IS3_INST_VERSION UINT32_C(0x00000100)

#define IS3_HEADER_SIZE 59U       /* through the file table size field */
#define IS3_DIR_FIXED_SIZE 6U
#define IS3_FILE_FIXED_SIZE 30U
#define IS3_INST_HEADER_SIZE 82U  /* through the file count field */
#define IS3_INST_FIXED_SIZE 22U   /* fields plus the first name length */
/* Largest entry the payload table can hold: the fixed part, the second name
 * length, and two maximal names. */
#define IS3_INST_MAX_ENTRY (IS3_INST_FIXED_SIZE + 2U + 2U * IS3_MAX_NAME)

/* The cabinet counts files in a 16-bit field, so 65535 is a hard ceiling for
 * that variant; the payload archive uses a 32-bit field and is held to the
 * same figure so that a corrupt count cannot drive a large allocation. */
#define IS3_MAX_MEMBERS 65535U
#define IS3_MAX_DIRECTORIES 65535U
#define IS3_MAX_NAME 1024U
/* Refuse to materialise a member larger than this in one piece. */
#define IS3_MAX_MEMBER_SIZE UINT64_C(0x40000000)

#define IS3_METHOD_STORE 0U
#define IS3_METHOD_DCL 1U

typedef struct is3_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset; /**< -1 when the payload lives on another volume. */
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t dos_time;
    uint32_t attributes;
    uint16_t volume;
    uint8_t method;
    bool present;
} is3_member;

typedef struct is3_stream_s {
    is3_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    bool payload_variant;
} is3_stream;

static uint16_t is3_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t is3_le32(const uint8_t *bytes) {
    return (uint32_t)is3_le16(bytes) | ((uint32_t)is3_le16(bytes + 2U) << 16U);
}

static bool is3_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Normalize only the filesystem-facing representation.  InstallShield names
 * are ANSI byte strings, so non-ASCII bytes are retained verbatim for the
 * caller's configured code page while separators and traversal components are
 * made harmless. */
static char *is3_normalize_name(const uint8_t *bytes, size_t size) {
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

static bool is3_safe_output_name(const char *name) {
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

static void is3_stream_free(void *opaque) {
    is3_stream *stream = (is3_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool is3_add_member(is3_stream *stream, const is3_member *member) {
    is3_member *grown;
    if (!stream || !member || stream->count >= IS3_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (is3_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Join a directory prefix and a member name into one normalized path.  Both
 * pieces are already bounded by the caller. */
static char *is3_join_name(const uint8_t *directory, size_t directory_size,
                           const uint8_t *file, size_t file_size) {
    uint8_t staging[IS3_MAX_NAME * 2U + 2U];
    size_t used = 0U;
    if (directory_size > IS3_MAX_NAME || file_size > IS3_MAX_NAME) return NULL;
    if (directory_size != 0U) {
        xx_mem_copy(staging, directory, directory_size);
        used = directory_size;
        staging[used++] = '/';
    }
    if (file_size != 0U) {
        xx_mem_copy(staging + used, file, file_size);
        used += file_size;
    }
    return is3_normalize_name(staging, used);
}

/* Record the data extent of one member.  A multi-volume cabinet lists every
 * file of the set in every volume's table, so an offset that runs past this
 * volume is normal and only means the payload is not here. */
static void is3_set_extent(is3_member *member, int64_t base, int64_t size,
                           uint32_t offset, uint32_t packed) {
    member->present = false;
    member->data_offset = -1;
    member->packed_size = (int64_t)packed;
    if ((int64_t)offset > size || (int64_t)packed > size - (int64_t)offset ||
        base > INT64_MAX - (int64_t)offset)
        return;
    member->data_offset = base + (int64_t)offset;
    member->present = true;
}

static bool is3_parse_cabinet(Abstractformat *format, const uint8_t *header,
                              int64_t size, is3_stream *stream) {
    uint8_t *directories = NULL;
    uint8_t *files = NULL;
    uint32_t directory_offset, directory_size, file_offset, file_size;
    uint32_t cursor_directory = 0U, cursor_file = 0U;
    uint16_t file_count, directory_count;
    uint32_t assigned = 0U;
    uint16_t index;

    file_count = is3_le16(header + 0x0CU);
    directory_offset = is3_le32(header + 0x29U);
    directory_size = is3_le32(header + 0x2DU);
    directory_count = is3_le16(header + 0x31U);
    file_offset = is3_le32(header + 0x33U);
    file_size = is3_le32(header + 0x37U);

    /* Both tables have to be wholly inside this volume before a single byte
     * of them is read; the file count is a 16-bit field and cannot overflow
     * the member ceiling. */
    if (directory_count == 0U || file_count == 0U ||
        (int64_t)directory_offset > size ||
        (int64_t)directory_size > size - (int64_t)directory_offset ||
        (int64_t)file_offset > size ||
        (int64_t)file_size > size - (int64_t)file_offset ||
        directory_size < IS3_DIR_FIXED_SIZE ||
        file_size < IS3_FILE_FIXED_SIZE ||
        directory_count > IS3_MAX_DIRECTORIES)
        return false;

    directories = (uint8_t *)xx_mem_alloc(directory_size);
    files = (uint8_t *)xx_mem_alloc(file_size);
    if (!directories || !files ||
        !is3_read_at(format->device, format->base_address + directory_offset,
                     directories, directory_size) ||
        !is3_read_at(format->device, format->base_address + file_offset, files,
                     file_size))
        goto fail;

    for (index = 0U; index < directory_count; ++index) {
        uint16_t owned, entry_size, name_size;
        uint16_t owned_index;
        if (directory_size - cursor_directory < IS3_DIR_FIXED_SIZE) goto fail;
        owned = is3_le16(directories + cursor_directory);
        entry_size = is3_le16(directories + cursor_directory + 2U);
        name_size = is3_le16(directories + cursor_directory + 4U);
        if (entry_size < IS3_DIR_FIXED_SIZE ||
            (uint32_t)entry_size > directory_size - cursor_directory ||
            (uint32_t)name_size > (uint32_t)entry_size - IS3_DIR_FIXED_SIZE ||
            name_size > IS3_MAX_NAME || owned > file_count - assigned)
            goto fail;
        for (owned_index = 0U; owned_index < owned; ++owned_index) {
            is3_member member;
            uint16_t entry_bytes;
            uint8_t member_name_size;
            const uint8_t *entry;
            if (file_size - cursor_file < IS3_FILE_FIXED_SIZE) goto fail;
            entry = files + cursor_file;
            entry_bytes = is3_le16(entry + 23U);
            member_name_size = entry[29];
            if (entry_bytes < IS3_FILE_FIXED_SIZE ||
                (uint32_t)entry_bytes > file_size - cursor_file ||
                (uint32_t)member_name_size >
                    (uint32_t)entry_bytes - IS3_FILE_FIXED_SIZE)
                goto fail;
            xx_mem_zero(&member, sizeof(member));
            member.unpacked_size = is3_le32(entry + 3U);
            member.dos_time = is3_le32(entry + 15U);
            member.attributes = is3_le32(entry + 19U);
            member.volume = is3_le16(entry + 25U);
            is3_set_extent(&member, format->base_address, size,
                           is3_le32(entry + 11U), is3_le32(entry + 7U));
            member.method = (member.unpacked_size ==
                             (uint64_t)member.packed_size)
                                ? IS3_METHOD_STORE
                                : IS3_METHOD_DCL;
            member.header_offset = format->base_address +
                                   (int64_t)file_offset + (int64_t)cursor_file;
            member.header_size = (int64_t)entry_bytes;
            member.name = is3_join_name(
                directories + cursor_directory + IS3_DIR_FIXED_SIZE, name_size,
                entry + IS3_FILE_FIXED_SIZE, member_name_size);
            if (!member.name || !is3_add_member(stream, &member)) {
                if (member.name) xx_str_free(member.name);
                goto fail;
            }
            cursor_file += entry_bytes;
            ++assigned;
        }
        cursor_directory += entry_size;
    }
    if (assigned != file_count) goto fail;
    xx_mem_free(directories);
    xx_mem_free(files);
    stream->archive_size = size;
    return true;
fail:
    if (directories) xx_mem_free(directories);
    if (files) xx_mem_free(files);
    return false;
}

static bool is3_parse_payload(Abstractformat *format, const uint8_t *header,
                              int64_t size, is3_stream *stream) {
    uint8_t *table = NULL;
    uint32_t count, cursor = 0U, table_size;
    uint32_t index;
    int64_t first_data = -1;

    count = is3_le32(header + 0x4EU);
    if (count == 0U || count > IS3_MAX_MEMBERS) return false;
    /* The table runs from the header to the first payload byte.  Its extent is
     * not stored, so the rest of the volume is the only safe upper bound and
     * every entry is re-checked against it while walking. */
    if (size <= (int64_t)IS3_INST_HEADER_SIZE) return false;
    {
        uint64_t available = (uint64_t)(size - (int64_t)IS3_INST_HEADER_SIZE);
        uint64_t ceiling = (uint64_t)count * IS3_INST_MAX_ENTRY;
        if (ceiling < available) available = ceiling;
        if (available > UINT32_MAX) available = UINT32_MAX;
        table_size = (uint32_t)available;
    }
    if ((uint64_t)count * IS3_INST_FIXED_SIZE > table_size) return false;
    table = (uint8_t *)xx_mem_alloc(table_size);
    if (!table ||
        !is3_read_at(format->device,
                     format->base_address + (int64_t)IS3_INST_HEADER_SIZE,
                     table, table_size))
        goto fail;

    for (index = 0U; index < count; ++index) {
        is3_member member;
        uint16_t name_size, alias_size;
        uint32_t entry_start = cursor;
        if (table_size - cursor < IS3_INST_FIXED_SIZE) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.unpacked_size = is3_le32(table + cursor + 8U);
        is3_set_extent(&member, format->base_address, size,
                       is3_le32(table + cursor), is3_le32(table + cursor + 4U));
        member.method = (member.unpacked_size == (uint64_t)member.packed_size)
                            ? IS3_METHOD_STORE
                            : IS3_METHOD_DCL;
        name_size = is3_le16(table + cursor + (IS3_INST_FIXED_SIZE - 2U));
        cursor += IS3_INST_FIXED_SIZE;
        if (name_size > IS3_MAX_NAME || (uint32_t)name_size > table_size - cursor)
            goto fail;
        member.name = is3_normalize_name(table + cursor, name_size);
        cursor += name_size;
        if (!member.name) goto fail;
        if (table_size - cursor < 2U) {
            xx_str_free(member.name);
            goto fail;
        }
        alias_size = is3_le16(table + cursor);
        cursor += 2U;
        if (alias_size > IS3_MAX_NAME || (uint32_t)alias_size > table_size - cursor) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += alias_size;
        member.header_offset =
            format->base_address + (int64_t)IS3_INST_HEADER_SIZE +
            (int64_t)entry_start;
        member.header_size = (int64_t)(cursor - entry_start);
        if (!is3_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        if (member.present && (first_data < 0 || member.data_offset < first_data))
            first_data = member.data_offset;
    }
    /* The table must end exactly where the first payload begins.  Without a
     * table size in the header this is the only structural anchor the format
     * offers, and it is a strong one. */
    if (first_data !=
        format->base_address + (int64_t)IS3_INST_HEADER_SIZE + (int64_t)cursor)
        goto fail;
    xx_mem_free(table);
    stream->archive_size = size;
    stream->payload_variant = true;
    return true;
fail:
    if (table) xx_mem_free(table);
    return false;
}

static bool is3_parse(Abstractformat *format, is3_stream **result) {
    uint8_t header[IS3_INST_HEADER_SIZE];
    is3_stream *stream = NULL;
    int64_t total, size;
    uint32_t signature;
    bool parsed;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)IS3_HEADER_SIZE ||
        !is3_read_at(format->device, format->base_address, header,
                     size < (int64_t)sizeof(header) ? IS3_HEADER_SIZE
                                                    : sizeof(header)))
        return false;
    signature = is3_le32(header);
    if (signature != IS3_SIGNATURE && signature != IS3_INST_SIGNATURE)
        return false;
    if (signature == IS3_INST_SIGNATURE &&
        (size < (int64_t)IS3_INST_HEADER_SIZE ||
         is3_le32(header + 4U) != IS3_INST_VERSION))
        return false;
    stream = (is3_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    parsed = (signature == IS3_SIGNATURE)
                 ? is3_parse_cabinet(format, header, size, stream)
                 : is3_parse_payload(format, header, size, stream);
    if (!parsed || stream->count == 0U) {
        is3_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool is3_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *is3_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool is3_set_record(xx_archive_record *record, const is3_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_DISK_NUMBER_START,
                                          member->volume) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool is3_decode_member(Abstractformat *format, const is3_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || !member->present ||
        member->packed_size < 0 ||
        member->unpacked_size > IS3_MAX_MEMBER_SIZE ||
        member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size
                                         : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !is3_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)))
        goto fail;
    if (member->method == IS3_METHOD_STORE) {
        if (output_size != (size_t)member->packed_size) goto fail;
        if (output_size != 0U) xx_mem_copy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else {
        decoded = xx_dcl_decode_memory(packed, (size_t)member->packed_size,
                                       output, output_size, &written);
    }
    /* The stored plaintext length is the only integrity anchor the format
     * offers; a decode that does not reach it exactly is rejected. */
    if (!decoded || written != output_size) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_is3_init(xx_is3 *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IS3_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-installshield");
    xx_format_set_extension(&archive->format, "z");
    archive->format.check_is_valid = xx_is3_check_is_valid;
    archive->format.handle_base_info = xx_is3_handle_base_info;
    archive->format.get_format_size = xx_is3_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_is3_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_is3_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_is3_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_is3_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_is3_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_is3_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_is3 *xx_is3_create(xx_io_device *device, int64_t base_address) {
    xx_is3 *archive = (xx_is3 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_is3_init(archive, device, base_address);
    return archive;
}

void xx_is3_destroy(xx_is3 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_is3_free(xx_is3 *archive) {
    if (!archive) return;
    xx_is3_destroy(archive);
    xx_mem_free(archive);
}

bool xx_is3_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    is3_stream *stream;
    (void)pd;
    if (!is3_parse(format, &stream)) return false;
    is3_stream_free(stream);
    return true;
}

bool xx_is3_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    is3_stream *stream;
    xx_is3 *archive;
    (void)pd;
    if (!format || !is3_parse(format, &stream)) return false;
    archive = (xx_is3 *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    is3_stream_free(stream);
    return true;
}

int64_t xx_is3_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is3_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_is3_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is3_handle_base_info(format, pd))
               ? ((xx_is3 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_is3_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is3_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!is3_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is3_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is3_stream_free;
    state->total_records = stream->count;
    if (!is3_copy_options(&state->options, options) ||
        !is3_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_is3_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_is3_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    is3_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is3_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        is3_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_is3_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    is3_stream *stream;
    is3_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is3_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!is3_safe_output_name(member->name) ||
        !is3_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = is3_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_is3_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
