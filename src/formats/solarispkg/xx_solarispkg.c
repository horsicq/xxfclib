/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SVR4 / Solaris package datastream, as produced by pkgtrans and consumed by
 * pkgadd (.pkg, .img and extension-less "package" files).
 *
 *   +0x000  "# PaCkAgE DaTaStReAm\n", then "<abbrev> <nparts> <nblocks>\n"
 *           and "# end of header\n".  The whole leading block is 512 bytes and
 *           is skipped wholesale -- its ASCII counters are advisory and a
 *           hand-built stream routinely disagrees with them.
 *   +0x200  a chain of complete cpio archives, one per package part.  Each
 *           ends with its own TRAILER!!! record and the next starts at the
 *           following 512-byte boundary.
 *
 * Nothing in this variant is compressed, so every member is stored.  The
 * sibling "# PaCkAgE DaTaStReAm:zip\n" magic is a different format and is
 * rejected here: the newline at +0x14 is part of the accepted magic.
 *
 * This is NOT the block-compressed Solaris install-media wrapper that the
 * cpio reader handles (19 9E 'T' 'L'); the two share no magic.  It is also
 * not plain cpio: a bare cpio file starts with a cpio header, this one starts
 * with the ASCII banner, so the two readers cannot collide on dispatch.
 *
 * Layout ported from XArchive packages/xsolarispackage.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/solarispkg/xx_solarispkg.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef SOLARISPKG
#define XX_SOLARISPKG_FILE_TYPE XX_FILE_TYPE_SOLARISPKG
#else
#define XX_SOLARISPKG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SOLPKG_MAGIC_SIZE 21U
#define SOLPKG_BLOCK_SIZE 512
#define SOLPKG_NEWC_HEADER_SIZE 110U
#define SOLPKG_ODC_HEADER_SIZE 76U
#define SOLPKG_BINARY_HEADER_SIZE 26U
#define SOLPKG_MAX_HEADER_SIZE 110U
#define SOLPKG_MAX_NAMESIZE 0x800
#define SOLPKG_MAX_NAME_READ 0x103
#define SOLPKG_MAX_MEMBERS 500000U
#define SOLPKG_MAX_PARTS 8192
#define SOLPKG_MAX_RECORDS_PER_PART 500000
#define SOLPKG_COPY_BUFFER 65536U
#define SOLPKG_S_IFMT 0170000U
#define SOLPKG_S_IFDIR 0040000U

typedef enum solpkg_dialect_e {
    SOLPKG_DIALECT_UNKNOWN = 0,
    SOLPKG_DIALECT_NEWC,
    SOLPKG_DIALECT_CRC,
    SOLPKG_DIALECT_ODC,
    SOLPKG_DIALECT_BINARY_BE,
    SOLPKG_DIALECT_BINARY_LE
} solpkg_dialect;

typedef struct solpkg_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    uint64_t mtime;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t part;
    solpkg_dialect dialect;
} solpkg_member;

typedef struct solpkg_stream_s {
    solpkg_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} solpkg_stream;

typedef struct solpkg_raw_s {
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t align_mask;
    uint64_t mtime;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    solpkg_dialect dialect;
    char *name;
} solpkg_raw;

static bool solpkg_read_at(xx_io_device *device, int64_t offset, void *buffer,
                           size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool solpkg_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* cpio pads its ASCII fields with NULs and, in some odc producers, blanks;
 * both are accepted, but a digit outside the base is a hard reject so random
 * binary can never parse as a header. */
static bool solpkg_parse_unsigned(const uint8_t *field, size_t size,
                                  unsigned base, uint64_t *value) {
    size_t index = 0U;
    size_t end = size;
    uint64_t result = 0U;
    bool any = false;
    while (index < end && (field[index] == ' ' || field[index] == 0U)) ++index;
    while (end > index && (field[end - 1U] == ' ' || field[end - 1U] == 0U))
        --end;
    for (; index < end; ++index) {
        uint8_t c = field[index];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10U;
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10U;
        else return false;
        if (digit >= base) return false;
        if (result > (UINT64_MAX - digit) / base) return false;
        result = result * base + digit;
        any = true;
    }
    *value = any ? result : 0U;
    return true;
}

static solpkg_dialect solpkg_classify(const uint8_t *magic) {
    uint16_t word;
    if (xx_rt_memcmp(magic, "07070", 5U) == 0) {
        if (magic[5] == '1') return SOLPKG_DIALECT_NEWC;
        if (magic[5] == '2') return SOLPKG_DIALECT_CRC;
        if (magic[5] == '7') return SOLPKG_DIALECT_ODC;
        return SOLPKG_DIALECT_UNKNOWN;
    }
    word = (uint16_t)magic[0] | (uint16_t)((uint16_t)magic[1] << 8U);
    if (word == 0xc771U) return SOLPKG_DIALECT_BINARY_BE;
    if (word == 0x71c7U) return SOLPKG_DIALECT_BINARY_LE;
    return SOLPKG_DIALECT_UNKNOWN;
}

static const char *solpkg_dialect_name(solpkg_dialect dialect) {
    if (dialect == SOLPKG_DIALECT_NEWC) return "cpio newc (070701)";
    if (dialect == SOLPKG_DIALECT_CRC) return "cpio crc (070702)";
    if (dialect == SOLPKG_DIALECT_ODC) return "cpio odc (070707)";
    if (dialect == SOLPKG_DIALECT_BINARY_BE) return "cpio binary big-endian";
    if (dialect == SOLPKG_DIALECT_BINARY_LE) return "cpio binary little-endian";
    return "Unknown";
}

static uint32_t solpkg_binary_word(const uint8_t *raw, size_t at,
                                   bool big_endian) {
    uint32_t low = raw[at];
    uint32_t high = raw[at + 1U];
    return big_endian ? ((low << 8U) | high) : ((high << 8U) | low);
}

/* Strips "./" and leading separators so a member lands under the output root
 * instead of escaping it, and rejects control bytes outright. */
static char *solpkg_normalize_name(const uint8_t *bytes, size_t size,
                                   bool *plain_equals_trailer) {
    size_t length = 0U;
    size_t start = 0U;
    size_t index;
    char *name;
    while (length < size && bytes[length] != 0U) ++length;
    for (index = 0U; index < length; ++index)
        if (bytes[index] < 0x20U) return NULL;
    while (length > start && (bytes[length - 1U] == ' ')) --length;
    while (start < length && bytes[start] == ' ') ++start;
    if (plain_equals_trailer)
        *plain_equals_trailer = (length - start == 10U) &&
                                xx_rt_memcmp(bytes + start, "TRAILER!!!",
                                             10U) == 0;
    while (length - start > 1U) {
        if (bytes[start] == '.' &&
            (bytes[start + 1U] == '/' || bytes[start + 1U] == '\\'))
            start += 2U;
        else if (bytes[start] == '/' || bytes[start] == '\\')
            start += 1U;
        else
            break;
    }
    name = (char *)xx_mem_alloc(length - start + 1U);
    if (!name) return NULL;
    if (length > start) xx_rt_memcpy(name, bytes + start, length - start);
    name[length - start] = 0;
    return name;
}

static void solpkg_raw_cleanup(solpkg_raw *raw) {
    if (raw && raw->name) {
        xx_str_free(raw->name);
        raw->name = NULL;
    }
}

static bool solpkg_read_raw_header(xx_io_device *device, int64_t base,
                                   int64_t offset, int64_t input_size,
                                   solpkg_raw *raw, bool *is_trailer) {
    uint8_t header[SOLPKG_MAX_HEADER_SIZE];
    uint8_t name_bytes[SOLPKG_MAX_NAME_READ + 1];
    solpkg_dialect dialect;
    int64_t header_size;
    int64_t align_mask;
    int64_t name_position;
    int64_t name_read;
    int64_t name_skip = 0;
    int64_t data_offset;
    uint64_t mode = 0U, uid = 0U, gid = 0U, mtime = 0U;
    uint64_t name_size = 0U, file_size = 0U;
    if (!device || !raw || !is_trailer) return false;
    xx_mem_zero(raw, sizeof(*raw));
    *is_trailer = false;
    if (!solpkg_range_within(input_size, offset, 6)) return false;
    if (!solpkg_read_at(device, base + offset, header, 6U)) return false;
    dialect = solpkg_classify(header);
    if (dialect == SOLPKG_DIALECT_UNKNOWN) return false;
    if (dialect == SOLPKG_DIALECT_NEWC || dialect == SOLPKG_DIALECT_CRC) {
        header_size = (int64_t)SOLPKG_NEWC_HEADER_SIZE;
        align_mask = 3;
    } else if (dialect == SOLPKG_DIALECT_ODC) {
        header_size = (int64_t)SOLPKG_ODC_HEADER_SIZE;
        align_mask = 0;
    } else {
        header_size = (int64_t)SOLPKG_BINARY_HEADER_SIZE;
        align_mask = 1;
    }
    if (!solpkg_range_within(input_size, offset, header_size)) return false;
    if (!solpkg_read_at(device, base + offset, header, (size_t)header_size))
        return false;

    if (dialect == SOLPKG_DIALECT_NEWC || dialect == SOLPKG_DIALECT_CRC) {
        /* magic[6] ino[8] mode[8] uid[8] gid[8] nlink[8] mtime[8] size[8]
         * devmajor[8] devminor[8] rdevmajor[8] rdevminor[8] namesize[8]
         * check[8] */
        if (!solpkg_parse_unsigned(header + 14, 8U, 16U, &mode) ||
            !solpkg_parse_unsigned(header + 22, 8U, 16U, &uid) ||
            !solpkg_parse_unsigned(header + 30, 8U, 16U, &gid) ||
            !solpkg_parse_unsigned(header + 46, 8U, 16U, &mtime) ||
            !solpkg_parse_unsigned(header + 54, 8U, 16U, &file_size) ||
            !solpkg_parse_unsigned(header + 94, 8U, 16U, &name_size))
            return false;
    } else if (dialect == SOLPKG_DIALECT_ODC) {
        /* magic[6] dev[6] ino[6] mode[6] uid[6] gid[6] nlink[6] rdev[6]
         * mtime[11] namesize[6] filesize[11] */
        if (!solpkg_parse_unsigned(header + 18, 6U, 8U, &mode) ||
            !solpkg_parse_unsigned(header + 24, 6U, 8U, &uid) ||
            !solpkg_parse_unsigned(header + 30, 6U, 8U, &gid) ||
            !solpkg_parse_unsigned(header + 48, 11U, 8U, &mtime) ||
            !solpkg_parse_unsigned(header + 59, 6U, 8U, &name_size) ||
            !solpkg_parse_unsigned(header + 65, 11U, 8U, &file_size))
            return false;
    } else {
        bool big_endian = dialect == SOLPKG_DIALECT_BINARY_BE;
        mode = solpkg_binary_word(header, 6U, big_endian);
        uid = solpkg_binary_word(header, 8U, big_endian);
        gid = solpkg_binary_word(header, 10U, big_endian);
        mtime = ((uint64_t)solpkg_binary_word(header, 16U, big_endian) << 16U) |
                solpkg_binary_word(header, 18U, big_endian);
        name_size = solpkg_binary_word(header, 20U, big_endian);
        file_size =
            ((uint64_t)solpkg_binary_word(header, 22U, big_endian) << 16U) |
            solpkg_binary_word(header, 24U, big_endian);
    }

    if (name_size == 0U || name_size > (uint64_t)SOLPKG_MAX_NAMESIZE)
        return false;
    if (file_size > (uint64_t)input_size) return false;

    name_position = offset + header_size;
    name_read = (int64_t)name_size;
    if (name_read > SOLPKG_MAX_NAME_READ) {
        name_skip = name_read - SOLPKG_MAX_NAME_READ;
        name_read = SOLPKG_MAX_NAME_READ;
    }
    if (!solpkg_range_within(input_size, name_position, name_read + name_skip))
        return false;
    if (!solpkg_read_at(device, base + name_position, name_bytes,
                        (size_t)name_read))
        return false;
    name_bytes[name_read] = 0U;
    raw->name = solpkg_normalize_name(name_bytes, (size_t)name_read,
                                      is_trailer);
    if (!raw->name) return false;

    data_offset = name_position + name_read + name_skip;
    data_offset = (data_offset + align_mask) & ~align_mask;
    if (!solpkg_range_within(input_size, data_offset, (int64_t)file_size)) {
        solpkg_raw_cleanup(raw);
        return false;
    }
    raw->header_offset = offset;
    raw->header_size = data_offset - offset;
    raw->data_offset = data_offset;
    raw->data_size = (int64_t)file_size;
    raw->align_mask = align_mask;
    raw->mtime = mtime;
    raw->mode = (uint32_t)mode;
    raw->uid = (uint32_t)uid;
    raw->gid = (uint32_t)gid;
    raw->dialect = dialect;
    return true;
}

static void solpkg_stream_free(void *opaque) {
    solpkg_stream *stream = (solpkg_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool solpkg_add_member(solpkg_stream *stream,
                              const solpkg_member *member) {
    solpkg_member *grown;
    if (!stream || !member || stream->count >= SOLPKG_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (solpkg_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool solpkg_parse(Abstractformat *format, solpkg_stream **result,
                         xx_pd_struct *pd) {
    uint8_t header_block[SOLPKG_BLOCK_SIZE];
    solpkg_stream *stream = NULL;
    int64_t total, size, offset;
    int32_t part_index = 0;
    bool any_header = false;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)SOLPKG_BLOCK_SIZE + (int64_t)SOLPKG_BINARY_HEADER_SIZE)
        return false;
    if (!solpkg_read_at(format->device, format->base_address, header_block,
                        sizeof(header_block)))
        return false;
    if (xx_rt_memcmp(header_block, "# PaCkAgE DaTaStReAm\n",
                     (size_t)SOLPKG_MAGIC_SIZE) != 0)
        return false;

    stream = (solpkg_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;

    offset = (int64_t)SOLPKG_BLOCK_SIZE;
    while (offset < size && part_index < SOLPKG_MAX_PARTS) {
        int32_t records = 0;
        bool trailer = false;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        while (records < SOLPKG_MAX_RECORDS_PER_PART) {
            solpkg_raw raw;
            bool is_trailer = false;
            int64_t next;
            if (offset >= size) break;
            if (pd && xx_pd_is_stopped(pd)) goto fail;
            if (!solpkg_read_raw_header(format->device, format->base_address,
                                        offset, size, &raw, &is_trailer))
                break;
            any_header = true;
            ++records;
            if (is_trailer) {
                offset = raw.data_offset;
                trailer = true;
                solpkg_raw_cleanup(&raw);
                break;
            }
            if ((raw.mode & SOLPKG_S_IFMT) != SOLPKG_S_IFDIR &&
                raw.name[0] != 0) {
                solpkg_member member;
                xx_mem_zero(&member, sizeof(member));
                member.name = raw.name;
                member.header_offset = format->base_address + raw.header_offset;
                member.header_size = raw.header_size;
                member.data_offset = format->base_address + raw.data_offset;
                member.data_size = raw.data_size;
                member.mtime = raw.mtime;
                member.mode = raw.mode;
                member.uid = raw.uid;
                member.gid = raw.gid;
                member.part = (uint32_t)part_index;
                member.dialect = raw.dialect;
                if (!solpkg_add_member(stream, &member)) {
                    solpkg_raw_cleanup(&raw);
                    goto fail;
                }
                raw.name = NULL;
            }
            solpkg_raw_cleanup(&raw);
            next = raw.data_offset + raw.data_size;
            next = (next + raw.align_mask) & ~raw.align_mask;
            if (next > size) {
                offset = size;
                break;
            }
            if (next <= offset) break; /* no forward progress */
            offset = next;
        }
        ++part_index;
        if (!trailer) break;
        offset = (offset + (SOLPKG_BLOCK_SIZE - 1)) & ~(SOLPKG_BLOCK_SIZE - 1);
        if (offset >= size) break;
    }
    if (!any_header) goto fail;
    stream->archive_size = offset < size ? offset : size;
    *result = stream;
    return true;
fail:
    solpkg_stream_free(stream);
    return false;
}

static bool solpkg_safe_output_name(const char *name) {
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

static bool solpkg_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
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

static const xx_var *solpkg_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool solpkg_set_record(xx_archive_record *record,
                              const solpkg_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->mode) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->mtime) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          solpkg_dialect_name(
                                              member->dialect)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_solarispkg_init(xx_solarispkg *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SOLARISPKG_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-svr4-package");
    xx_format_set_extension(&archive->format, "pkg");
    archive->format.check_is_valid = xx_solarispkg_check_is_valid;
    archive->format.handle_base_info = xx_solarispkg_handle_base_info;
    archive->format.get_format_size = xx_solarispkg_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_solarispkg_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_solarispkg_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_solarispkg_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_solarispkg_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_solarispkg_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_solarispkg_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_solarispkg *xx_solarispkg_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_solarispkg *archive =
        (xx_solarispkg *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_solarispkg_init(archive, device, base_address);
    return archive;
}

void xx_solarispkg_destroy(xx_solarispkg *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_solarispkg_free(xx_solarispkg *archive) {
    if (!archive) return;
    xx_solarispkg_destroy(archive);
    xx_mem_free(archive);
}

bool xx_solarispkg_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    solpkg_stream *stream;
    if (!solpkg_parse(format, &stream, pd)) return false;
    solpkg_stream_free(stream);
    return true;
}

bool xx_solarispkg_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    solpkg_stream *stream;
    xx_solarispkg *archive;
    if (!format || !solpkg_parse(format, &stream, pd)) return false;
    archive = (xx_solarispkg *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    solpkg_stream_free(stream);
    return true;
}

int64_t xx_solarispkg_get_format_size(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_solarispkg_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_solarispkg_get_number_of_archive_records(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_solarispkg_handle_base_info(format, pd))
               ? ((xx_solarispkg *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_solarispkg_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    solpkg_stream *stream;
    xx_archive_record_state *state;
    if (!solpkg_parse(format, &stream, pd)) return NULL;
    if (stream->count == 0U) {
        solpkg_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        solpkg_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = solpkg_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!solpkg_copy_options(&state->options, options) ||
        !solpkg_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_solarispkg_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_solarispkg_archive_record_move_to_next(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    solpkg_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (solpkg_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = solpkg_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_solarispkg_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    solpkg_stream *stream;
    solpkg_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *buffer = NULL;
    int64_t remaining;
    bool result = false;
    xx_io_device *destination = NULL;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (solpkg_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!solpkg_safe_output_name(member->name)) return false;
    path_option = solpkg_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
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
    buffer = (uint8_t *)xx_mem_alloc(SOLPKG_COPY_BUFFER);
    destination = xx_io_file_open(path, "wb");
    if (!buffer || !destination) goto done;
    result = true;
    remaining = member->data_size;
    {
        int64_t cursor = member->data_offset;
        while (remaining > 0) {
            size_t chunk = remaining > (int64_t)SOLPKG_COPY_BUFFER
                               ? SOLPKG_COPY_BUFFER
                               : (size_t)remaining;
            size_t written = 0U;
            if (pd && xx_pd_is_stopped(pd)) {
                result = false;
                break;
            }
            if (!solpkg_read_at(format->device, cursor, buffer, chunk)) {
                result = false;
                break;
            }
            while (written < chunk) {
                ssize_t amount =
                    xx_io_write(destination, buffer + written, chunk - written);
                if (amount <= 0 || (size_t)amount > chunk - written) {
                    result = false;
                    break;
                }
                written += (size_t)amount;
            }
            if (!result) break;
            cursor += (int64_t)chunk;
            remaining -= (int64_t)chunk;
        }
    }
done:
    if (destination && xx_io_close(destination) != 0) result = false;
    if (!result && path) xx_rt_remove(path);
    if (buffer) xx_mem_free(buffer);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_solarispkg_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
