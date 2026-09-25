/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Squeeze It (HLSQZ) archives.
 *
 *   archive banner, 8 bytes at the base address:
 *     +0x00  "HLSQZ"
 *     +0x05  u8   version; MUST be 0x20..0x7e
 *     +0x06  two bytes the format does not constrain
 *
 * then a chain of records, each introduced by a u8 LENGTH byte.  The length
 * byte is a three-way selector, which is the one thing about this format that
 * has to be read carefully:
 *
 *   0        end of archive.  The chain is closed by a single zero byte and
 *            the archive ends one byte later.
 *   1..18    an opaque, length-prefixed ARCHIVE BLOCK: a comment, the HLSQZ
 *            postfix, and so on.  These are skipped, not listed.
 *   >= 19    a MEMBER record; the byte is the header length.
 *
 * A member header is (length + 2) bytes:
 *
 *     +0x00  u8   the length byte itself
 *     +0x01  u8   additive check over every byte from +0x02 to the end
 *     +0x02  u8   flags; the low nibble is the method, 0..4
 *     +0x03  u32  compressed size
 *     +0x07  u32  uncompressed size
 *     +0x0b  u16  DOS time
 *     +0x0d  u16  DOS date
 *     +0x0f  u8   DOS attributes
 *     +0x10  u32  CRC-32
 *     +0x14  the name, (length - 18) bytes, NOT NUL terminated
 *
 * so a member's name length is what is left of the header length past the
 * eighteen fixed bytes, and the payload follows the header directly.
 *
 * The additive check over the header is the only integrity field available
 * before the payload is read, and it is what stops a plausible length byte
 * from starting a bogus member.  There is no other framing: get the skip
 * wrong for one archive block and the walk lands in the middle of a payload.
 *
 * SKIPPING AN ARCHIVE BLOCK IS NOT UNIFORM, and this is the subtle part.  The
 * common shape is a u16 extra-size at +0x01 followed by the payload, i.e. a
 * three-byte header -- but a type-1 comment block carries a SECOND u16 at
 * +0x03 that REPLACES the first, with five more bytes behind it (a ten-byte
 * header), and a type-4 block has two extra bytes (a five-byte header).
 * Using the three-byte rule for all of them lands inside a comment payload
 * and everything after it is lost.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sqz/xx_sqz.h"

#include "xxfclib/algo/sqz/xx_sqz.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

typedef struct xx_sqz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint64_t timestamp;
    bool is_folder;
} xx_sqz_member;

typedef struct xx_sqz_stream_s {
    xx_sqz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t version;
} xx_sqz_stream;

static void xx_sqz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_sqz_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_sqz_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_sqz_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_sqz_stream_free(void *pointer) {
    xx_sqz_stream *stream = (xx_sqz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member; the caller keeps ownership of @p member->name on failure. */
static bool xx_sqz_add(xx_sqz_stream *stream, const xx_sqz_member *member) {
    xx_sqz_member *grown = (xx_sqz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define XX_SQZ_BANNER_SIZE 8
#define XX_SQZ_MAGIC_SIZE 5
#define XX_SQZ_OFFSET_VERSION 5
#define XX_SQZ_MIN_HEADER_LENGTH 19
#define XX_SQZ_FIXED_HEADER_BYTES 18
/* The largest a member header can be: the length byte tops out at 0xff and
 * two more bytes sit in front of the counted area. */
#define XX_SQZ_MAX_HEADER_SIZE 257
#define XX_SQZ_MAX_NAME (0xFF - XX_SQZ_FIXED_HEADER_BYTES)
#define XX_SQZ_MAX_MEMBERS 100000
#define XX_SQZ_METHOD_STORE 0U
#define XX_SQZ_MAX_METHOD 4U
/* Bounds only what a corrupt size field may ask an extraction to allocate;
 * the walk itself is bounded by the file. */
#define XX_SQZ_MAX_MEMBER_SIZE ((int64_t)0x10000000)

/* The one archive-block type with a fixed shape: the documented HLSQZ
 * postfix, five bytes of payload spelling the banner again. */
#define XX_SQZ_BLOCK_COMMENT 1U
#define XX_SQZ_BLOCK_POSTFIX 3U
#define XX_SQZ_BLOCK_TYPE4 4U
#define XX_SQZ_POSTFIX_PAYLOAD 5

static const uint8_t XX_SQZ_MAGIC[XX_SQZ_MAGIC_SIZE] = {'H', 'L', 'S', 'Q',
                                                        'Z'};

static uint16_t xx_sqz_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_sqz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The reference normalises backslashes to forward slashes and then rejects
 * anything that is not a plain relative path. The same rules are applied here
 * in place, over Latin-1 bytes, so that a member whose name cannot be written
 * safely is refused while the archive is being walked rather than at the far
 * end of an extraction. */
static bool xx_sqz_clean_name(const uint8_t *raw, size_t length,
                              char *out /* [XX_SQZ_MAX_NAME + 1] */) {
    size_t index;
    size_t part_start = 0U;

    if (length == 0U || length > (size_t)XX_SQZ_MAX_NAME) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = raw[index];

        /* Control bytes and the DOS drive separator are out: a name carrying
         * either is not something this reader should hand on as a path. An
         * embedded NUL in particular would silently truncate the name. */
        if (value < 0x20U || value == 0x7FU || value == (uint8_t)':') {
            return false;
        }
        out[index] = (value == (uint8_t)'\\') ? '/' : (char)value;
    }
    out[length] = '\0';

    if (out[0] == '/') return false;
    /* No empty, "." or ".." component anywhere, which also rules out a
     * trailing slash and a doubled one. */
    for (index = 0U; index <= length; ++index) {
        if (index == length || out[index] == '/') {
            size_t part = index - part_start;

            if (part == 0U) return false;
            if (part == 1U && out[part_start] == '.') return false;
            if (part == 2U && out[part_start] == '.' &&
                out[part_start + 1U] == '.') {
                return false;
            }
            part_start = index + 1U;
        }
    }
    return true;
}

/* Skip one opaque archive block, advancing @p offset past it.
 *
 * @p block holds the bytes at @p offset, @p available of them (at most ten,
 * fewer near the end of the file). Returns false when the block is malformed,
 * which is a structural error rather than an end of chain. */
static bool xx_sqz_skip_block(uint8_t type, const uint8_t *block,
                              int64_t available, int64_t span, int64_t *offset,
                              bool *saw_postfix) {
    int64_t skip = 3;
    int64_t extra;

    if (available < 3) return false;
    extra = (int64_t)xx_sqz_le16(block + 1);
    if (type == XX_SQZ_BLOCK_COMMENT) {
        /* The second u16 REPLACES the first; five more bytes follow it. */
        if (available < 10) return false;
        extra = (int64_t)xx_sqz_le16(block + 3);
        skip = 10;
    } else if (type == XX_SQZ_BLOCK_TYPE4) {
        skip = 5;
    }
    /* Written this way rather than as offset + skip + extra <= span so that a
     * block header longer than what is left of the file -- where span -
     * *offset - skip goes negative -- is refused instead of wrapping. */
    if (extra > span - *offset - skip) return false;

    if (type == XX_SQZ_BLOCK_POSTFIX) {
        /* At most one postfix, and its payload is the banner again. This is
         * the only archive block whose contents the format pins down, so it
         * is worth checking: it is free detection strength. */
        if (*saw_postfix || extra != (int64_t)XX_SQZ_POSTFIX_PAYLOAD) {
            return false;
        }
        if (xx_rt_memcmp(block + 3, XX_SQZ_MAGIC, XX_SQZ_MAGIC_SIZE) != 0) {
            return false;
        }
        *saw_postfix = true;
    }
    *offset += skip + extra;
    return true;
}

static xx_sqz_stream *xx_sqz_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_sqz_stream *stream;
    uint8_t banner[XX_SQZ_BANNER_SIZE];
    uint8_t header[XX_SQZ_MAX_HEADER_SIZE];
    char name[XX_SQZ_MAX_NAME + 1];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t archive_size = 0;
    uint32_t version;
    bool saw_postfix = false;
    bool terminated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_SQZ_BANNER_SIZE + 1) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_sqz_read_at(self, self->base_address, banner, sizeof(banner)) ||
        xx_rt_memcmp(banner, XX_SQZ_MAGIC, XX_SQZ_MAGIC_SIZE) != 0) {
        return NULL;
    }
    version = banner[XX_SQZ_OFFSET_VERSION];
    if (version < 0x20U || version > 0x7EU) return NULL;

    stream = (xx_sqz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->version = version;

    offset = XX_SQZ_BANNER_SIZE;
    while (!terminated) {
        uint8_t length_byte;
        uint8_t block[10];
        int64_t available;
        int64_t header_size;
        int64_t data_offset;
        size_t name_length;
        size_t check_index;
        uint8_t check = 0U;
        xx_sqz_member member;
        uint32_t compressed;
        uint32_t uncompressed;
        uint32_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* The chain has to be closed by a terminator inside the file; running
         * off the end means the archive is not whole. */
        if (offset < XX_SQZ_BANNER_SIZE || offset >= span) goto fail;
        if (!xx_sqz_read_at(self, self->base_address + offset, &length_byte,
                            1U)) {
            goto fail;
        }

        if (length_byte == 0U) {
            archive_size = offset + 1;
            terminated = true;
            break;
        }

        if (length_byte < XX_SQZ_MIN_HEADER_LENGTH) {
            available = span - offset;
            if (available > (int64_t)sizeof(block)) {
                available = (int64_t)sizeof(block);
            }
            if (!xx_sqz_read_at(self, self->base_address + offset, block,
                                (size_t)available)) {
                goto fail;
            }
            if (!xx_sqz_skip_block(length_byte, block, available, span, &offset,
                                   &saw_postfix)) {
                goto fail;
            }
            continue;
        }

        if (stream->count >= (size_t)XX_SQZ_MAX_MEMBERS) goto fail;

        header_size = (int64_t)length_byte + 2;
        if (header_size > span - offset) goto fail;
        if (!xx_sqz_read_at(self, self->base_address + offset, header,
                            (size_t)header_size)) {
            goto fail;
        }

        /* Additive check over everything past the length and check bytes. */
        for (check_index = 2U; check_index < (size_t)header_size;
             ++check_index) {
            check = (uint8_t)(check + header[check_index]);
        }
        if (check != header[1]) goto fail;

        name_length = (size_t)length_byte - XX_SQZ_FIXED_HEADER_BYTES;
        if (!xx_sqz_clean_name(header + 20, name_length, name)) goto fail;

        method = (uint32_t)(header[2] & 0x0FU);
        if (method > XX_SQZ_MAX_METHOD) goto fail;
        compressed = xx_sqz_le32(header + 3);
        uncompressed = xx_sqz_le32(header + 7);
        /* A stored member that claims two different lengths was not written
         * by this format. */
        if (method == XX_SQZ_METHOD_STORE && compressed != uncompressed) {
            goto fail;
        }

        data_offset = offset + header_size;
        if (!xx_sqz_range_within(span, data_offset, (int64_t)compressed)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = header_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = (int64_t)compressed;
        member.uncompressed_size = (int64_t)uncompressed;
        member.method = method;
        member.attributes = header[15];
        member.crc32 = xx_sqz_le32(header + 16);
        /* The DOS stamp is published the way the reference packs it: date in
         * the high half, time in the low half. */
        member.timestamp =
            ((uint64_t)xx_sqz_le16(header + 13) << 16) |
            (uint64_t)xx_sqz_le16(header + 11);
        member.is_folder = false;
        if (!xx_sqz_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset = data_offset + (int64_t)compressed;
    }

    if (!terminated || archive_size <= 0 || stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = archive_size;
    return stream;

fail:
    xx_sqz_stream_free(stream);
    return NULL;
}

/* Method 0 is a verbatim copy; 1..4 are the four SQZ bitstream variants,
 * which share one decoder behind a method selector. An unknown method must
 * fail rather than fall back to a copy: handing a caller compressed bytes
 * labelled as the file is exactly what the caller cannot detect. */
static bool xx_sqz_decode(Abstractformat *self, const xx_sqz_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method > XX_SQZ_MAX_METHOD) return false;
    if (member->compressed_size < 0 ||
        member->compressed_size > XX_SQZ_MAX_MEMBER_SIZE) {
        return false;
    }
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_SQZ_MAX_MEMBER_SIZE) {
        return false;
    }

    /* Zero-length allocations are avoided so that a NULL return stays a
     * failure rather than an empty member. */
    input = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) return false;
    if (member->compressed_size != 0 &&
        !xx_sqz_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_SQZ_METHOD_STORE) {
        /* Parse only publishes this method when the two lengths agree. */
        ok = member->compressed_size == member->uncompressed_size;
        if (ok) {
            if (member->uncompressed_size != 0) {
                xx_rt_memcpy(output, input, (size_t)member->uncompressed_size);
            }
            written = (size_t)member->uncompressed_size;
        }
    } else {
        ok = xx_sqz_decode_memory(input, (size_t)member->compressed_size,
                                  member->method, output,
                                  (size_t)member->uncompressed_size, &written);
    }

    xx_mem_free(input);
    if (!ok || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_sqz_init(xx_sqz *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SQZ_FILE_TYPE_ID;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sqz");
    xx_format_set_extension(&archive->format, "sqz");
    archive->format.check_is_valid = xx_sqz_check_is_valid;
    archive->format.handle_base_info = xx_sqz_handle_base_info;
    archive->format.get_format_size = xx_sqz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sqz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sqz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sqz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sqz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sqz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sqz_free_archive_records_reading;
    archive->format.destroy = xx_sqz_vtable_destroy;
}

xx_sqz *xx_sqz_create(xx_io_device *device, int64_t base_address) {
    xx_sqz *archive = (xx_sqz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_sqz_init(archive, device, base_address);
    return archive;
}

void xx_sqz_destroy(xx_sqz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->version = 0U;
}

void xx_sqz_free(xx_sqz *archive) {
    if (!archive) return;
    xx_sqz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_sqz_vtable_destroy(Abstractformat *self) {
    xx_sqz_destroy((xx_sqz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_sqz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sqz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_sqz_parse(self, pd);
    if (!stream) return false;
    xx_sqz_stream_free(stream);
    return true;
}

bool xx_sqz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sqz *archive = (xx_sqz *)self;
    xx_sqz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_sqz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->version = stream->version;
    xx_sqz_stream_free(stream);
    return true;
}

int64_t xx_sqz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_sqz_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_sqz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_sqz_set_record(xx_archive_record *record,
                              const xx_sqz_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_sqz_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_sqz_get_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_sqz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sqz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_sqz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_sqz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_sqz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_sqz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_sqz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sqz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sqz_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_sqz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sqz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_sqz_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_sqz_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_sqz_stream *stream;
    const xx_sqz_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sqz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_sqz_path_safe(member->name)) return false;

    path_option = xx_sqz_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_sqz_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_sqz_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_sqz_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
