/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Delrina WinFax LSZ libraries.
 *
 *   header, 6 bytes at offset 0:
 *     0x00  u32 LE magic, 0xFFFFF037  (bytes 37 F0 FF FF)
 *     0x04  u16 LE version, 0x0300    (bytes 00 03)
 *
 *   record, 51 bytes, one per member, immediately followed by its payload:
 *     0x00  u32 LE tag, either 0xFFFF037F or 0x00000000
 *     0x04  name, 13 bytes, NUL terminated and NUL padded (8.3)
 *     0x11  u16 LE reserved, zero in every known member
 *     0x13  i32 LE uncompressed size
 *     0x17  i32 LE compressed size
 *     0x1b  u32 LE checksum, not verifiable from the container
 *     0x1f  u16 LE DOS time
 *     0x21  u16 LE DOS date
 *     0x23  u16 LE attributes
 *     0x25  u16 LE method: 1 = stored, 2 = PKWARE DCL implode,
 *                          6 = stored empty file
 *     0x27  12 reserved bytes, zero to the end of the record
 *
 * The tag has two spellings because two writers produced this container: one
 * repeats a byte-rotated form of the container magic, the other leaves the
 * field zero. Both appear inside a single corpus, so both are accepted and
 * nothing else is.
 *
 * There is no member count, no central directory and no terminator. The
 * record chain starts at offset 6 and each member's payload is contiguous
 * with its record, so the next record sits at data_offset + compressed_size.
 * The chain landing exactly on end-of-file is this format's only self-check;
 * slack or overrun there means the records do not describe the payload.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lsz/xx_lsz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_LSZ_COPY_CHUNK (64 * 1024)

typedef struct xx_lsz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_lsz_member;

typedef struct xx_lsz_stream_s {
    xx_lsz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lsz_stream;

static void xx_lsz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lsz_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_lsz_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_lsz_path_safe(const char *name) {
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

static void xx_lsz_stream_free(void *pointer) {
    xx_lsz_stream *stream = (xx_lsz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_lsz_add(xx_lsz_stream *stream,
                          const xx_lsz_member *member) {
    xx_lsz_member *grown = (xx_lsz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_LSZ_MIN_DCL_SIZE 3
#define XX_LSZ_DCL_MAX_LITERAL_MODE 1U
#define XX_LSZ_DCL_MIN_DICT_BITS 4U
#define XX_LSZ_DCL_MAX_DICT_BITS 6U
#define XX_LSZ_MAX_MEMBERS 65535
#define XX_LSZ_HEADER_SIZE 6
#define XX_LSZ_RECORD_SIZE 51
#define XX_LSZ_NAME_SIZE 13
#define XX_LSZ_MAGIC 0xFFFFF037U
#define XX_LSZ_VERSION 0x0300U
#define XX_LSZ_RECORD_TAG_A 0xFFFF037FU
#define XX_LSZ_RECORD_TAG_B 0x00000000U
#define XX_LSZ_METHOD_STORED 1U
#define XX_LSZ_METHOD_IMPLODE 2U
#define XX_LSZ_METHOD_EMPTY 6U
#define XX_LSZ_MAX_UNCOMPRESSED ((int64_t)512 * 1024 * 1024)
#define XX_LSZ_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_lsz_le16(const uint8_t *data);
static uint32_t xx_lsz_le32(const uint8_t *data);
static bool xx_lsz_name_field(const uint8_t *field, char **out_name);
static bool xx_lsz_dcl_prelude(const uint8_t *prelude);
static xx_lsz_stream *xx_lsz_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_lsz_decode(Abstractformat *self, const xx_lsz_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* A DCL stream cannot be shorter than its two prelude bytes plus one byte
 * holding the start of the end-of-stream code. */
/* No member count is stored, so this is a runaway guard, not a format limit.
 * The largest reference archive holds 67 members. */

static uint16_t xx_lsz_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_lsz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The 13-byte field is a fixed, NUL-padded 8.3 buffer. Read the FULL field
 * and stop at the first NUL; a stale byte behind the terminator would mean
 * this is not the buffer the parser assumes, so reject rather than guess. */
static bool xx_lsz_name_field(const uint8_t *field, char **out_name) {
    char buffer[XX_LSZ_NAME_SIZE + 1];
    size_t terminator = 0U;
    size_t start;
    size_t end;
    size_t index;
    size_t length;
    uint8_t character;
    char *name;

    *out_name = NULL;
    while (terminator < (size_t)XX_LSZ_NAME_SIZE && field[terminator] != 0U) {
        ++terminator;
    }
    /* The terminator must be inside the field and must not be byte zero: a
     * field with no NUL, or one starting with NUL, is not a name. */
    if (terminator == 0U || terminator >= (size_t)XX_LSZ_NAME_SIZE) {
        return false;
    }
    for (index = terminator; index < (size_t)XX_LSZ_NAME_SIZE; ++index) {
        if (field[index] != 0U) return false;
    }
    for (index = 0U; index < terminator; ++index) {
        character = field[index];
        /* 8.3 names are plain ASCII here; the format grants no exemption. */
        if (character < 0x20U || character > 0x7EU) return false;
        if (character == '/' || character == '\\' || character == ':' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|') {
            return false;
        }
    }

    /* Trailing pad spaces are common; a name that is nothing but spaces is
     * not. */
    start = 0U;
    end = terminator;
    while (start < end && field[start] == 0x20U) ++start;
    while (end > start && field[end - 1U] == 0x20U) --end;
    length = end - start;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        buffer[index] = (char)field[start + index];
    }
    buffer[length] = '\0';
    if (length == 1U && buffer[0] == '.') return false;
    if (length == 2U && buffer[0] == '.' && buffer[1] == '.') return false;

    name = xx_str_dup(buffer);
    if (!name) return false;
    *out_name = name;
    return true;
}

/* The two bytes a DCL stream opens with. Cheap, and the only content check
 * available for a method 2 payload at parse time. */
static bool xx_lsz_dcl_prelude(const uint8_t *prelude) {
    return prelude[0] <= XX_LSZ_DCL_MAX_LITERAL_MODE &&
           prelude[1] >= XX_LSZ_DCL_MIN_DICT_BITS &&
           prelude[1] <= XX_LSZ_DCL_MAX_DICT_BITS;
}

static xx_lsz_stream *xx_lsz_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_lsz_stream *stream;
    uint8_t header[XX_LSZ_HEADER_SIZE];
    uint8_t record[XX_LSZ_RECORD_SIZE];
    uint8_t prelude[2];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with no room for even one record describes nothing. */
    if (span < XX_LSZ_HEADER_SIZE + XX_LSZ_RECORD_SIZE) return NULL;
    if (!xx_lsz_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* Six bytes of magic and version are weak on their own; what actually
     * keeps a stray 37 F0 FF FF 00 03 out is the record chain below. */
    if (xx_lsz_le32(header) != XX_LSZ_MAGIC ||
        xx_lsz_le16(header + 4) != XX_LSZ_VERSION) {
        return NULL;
    }

    stream = (xx_lsz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_LSZ_HEADER_SIZE;
    while (offset < span) {
        xx_lsz_member member;
        char *name;
        uint32_t tag;
        uint16_t method;
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t data_offset;
        size_t index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_LSZ_MAX_MEMBERS) goto fail;
        if (XX_LSZ_RECORD_SIZE > span - offset) goto fail;
        if (!xx_lsz_read_at(self, self->base_address + offset, record,
                            sizeof(record))) {
            goto fail;
        }

        tag = xx_lsz_le32(record);
        if (tag != XX_LSZ_RECORD_TAG_A && tag != XX_LSZ_RECORD_TAG_B) {
            goto fail;
        }
        /* Reserved fields are zero in every member of the reference corpus;
         * together with the tag they are the cheapest way to keep a random
         * six-byte header hit from walking into a bogus record chain. */
        if (xx_lsz_le16(record + 0x11) != 0U) goto fail;
        for (index = 0x27U; index < (size_t)XX_LSZ_RECORD_SIZE; ++index) {
            if (record[index] != 0U) goto fail;
        }

        /* Signed on purpose: a size with the top bit set is a corrupt field,
         * not a four-gigabyte member. */
        uncompressed_size = (int64_t)(int32_t)xx_lsz_le32(record + 0x13);
        compressed_size = (int64_t)(int32_t)xx_lsz_le32(record + 0x17);
        if (uncompressed_size < 0 || compressed_size < 0) goto fail;
        method = xx_lsz_le16(record + 0x25);

        if (method == (uint16_t)XX_LSZ_METHOD_STORED) {
            /* Stored means the two sizes are the same number twice. */
            if (compressed_size != uncompressed_size) goto fail;
        } else if (method == (uint16_t)XX_LSZ_METHOD_IMPLODE) {
            if (compressed_size < XX_LSZ_MIN_DCL_SIZE ||
                uncompressed_size < 1 ||
                uncompressed_size > XX_LSZ_MAX_UNCOMPRESSED) {
                goto fail;
            }
        } else if (method == (uint16_t)XX_LSZ_METHOD_EMPTY) {
            /* Directory-only archives are built entirely out of these; both
             * sizes must be zero or this is not a method 6 record. */
            if (compressed_size != 0 || uncompressed_size != 0) goto fail;
        } else {
            goto fail;
        }

        data_offset = offset + XX_LSZ_RECORD_SIZE;
        /* A member extending past EOF is a rejection, not a short read. */
        if (!xx_lsz_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        if (method == (uint16_t)XX_LSZ_METHOD_IMPLODE) {
            if (!xx_lsz_read_at(self, self->base_address + data_offset,
                                prelude, sizeof(prelude))) {
                goto fail;
            }
            if (!xx_lsz_dcl_prelude(prelude)) goto fail;
        }

        if (!xx_lsz_name_field(record + 4, &name)) goto fail;
        if (!xx_lsz_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_LSZ_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The container's own method number, unchanged, so a listing shows
         * what the archive actually says. */
        member.method = (uint32_t)method;
        /* DOS date/time, packed date-high / time-low. The record stores the
         * time word first and the date word second, the reverse of the
         * obvious order; swapping them yields plausible nonsense rather than
         * an error. */
        member.timestamp = ((uint64_t)xx_lsz_le16(record + 0x21) << 16) |
                           (uint64_t)xx_lsz_le16(record + 0x1f);
        /* The format has no directory entries: method 6 is an empty file. */
        member.is_folder = false;
        /* The checksum word at 0x1b is zero for most members and the
         * algorithm behind the non-zero ones is not recoverable from the
         * container, so it is not published as a verifiable CRC. */

        if (!xx_lsz_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        ++count;
        offset = data_offset + compressed_size;
    }

    /* No count, no directory, no terminator: the chain landing exactly on
     * end-of-file is this format's only self-check, and slack there means the
     * records do not describe the payload. This is the check a later reader
     * will be tempted to loosen, and loosening it makes the format match
     * almost anything that starts with the six magic bytes. */
    if (offset != span) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_lsz_stream_free(stream);
    return NULL;
}


/* Two writers, two spellings of the same per-record tag. */


/* The plaintext length of a DCL member is driven by the bitstream, so both
 * the container's claim and this reader's allocation must stay bounded. The
 * container-level limit matches the reference implementation; the decode
 * ceiling is the smaller number this reader is willing to allocate. */

/* Method 1 and method 6 are byte copies; method 2 is a complete DCL stream,
 * prelude bytes included, whose plaintext length the record supplies. */
static bool xx_lsz_decode(Abstractformat *self, const xx_lsz_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    /* Both sizes are attacker-controlled; refuse rather than attempt the
     * allocation. */
    if (member->compressed_size > XX_LSZ_MAX_DECODED ||
        member->uncompressed_size > XX_LSZ_MAX_DECODED) {
        return false;
    }

    if (member->method == XX_LSZ_METHOD_EMPTY) {
        /* A method 6 member is a real, empty file, not a failure. */
        if (member->compressed_size != 0 || member->uncompressed_size != 0) {
            return false;
        }
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    /* Anything the format defines but this reader does not implement must
     * fail here: treating an unknown method as stored yields garbage that is
     * indistinguishable from data. */
    if (member->method != XX_LSZ_METHOD_STORED &&
        member->method != XX_LSZ_METHOD_IMPLODE) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) return false;
    if (member->compressed_size != 0 &&
        !xx_lsz_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_LSZ_METHOD_STORED) {
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(input);
            return false;
        }
        *out = input;
        *out_size = (size_t)member->compressed_size;
        return true;
    }

    if (member->uncompressed_size < 1) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* Exactly the stored plaintext length, or nothing: a short decode
     * reported as success is the one failure the caller cannot detect. */
    if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_lsz_init(xx_lsz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LSZ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-winfax-lsz");
    xx_format_set_extension(&archive->format, "lsz");
    archive->format.check_is_valid = xx_lsz_check_is_valid;
    archive->format.handle_base_info = xx_lsz_handle_base_info;
    archive->format.get_format_size = xx_lsz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lsz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lsz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lsz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lsz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lsz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lsz_free_archive_records_reading;
    archive->format.destroy = xx_lsz_vtable_destroy;
}

xx_lsz *xx_lsz_create(xx_io_device *device, int64_t base_address) {
    xx_lsz *archive = (xx_lsz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lsz_init(archive, device, base_address);
    return archive;
}

void xx_lsz_destroy(xx_lsz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lsz_free(xx_lsz *archive) {
    if (!archive) return;
    xx_lsz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lsz_vtable_destroy(Abstractformat *self) {
    xx_lsz_destroy((xx_lsz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lsz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lsz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lsz_parse(self, pd);
    if (!stream) return false;
    xx_lsz_stream_free(stream);
    return true;
}

bool xx_lsz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lsz *archive = (xx_lsz *)self;
    xx_lsz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lsz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lsz_stream_free(stream);
    return true;
}

int64_t xx_lsz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lsz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lsz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lsz_set_record(xx_archive_record *record,
                                 const xx_lsz_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_lsz_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
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

static const xx_var *xx_lsz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lsz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lsz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lsz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lsz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lsz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lsz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lsz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lsz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lsz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lsz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lsz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lsz_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lsz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lsz_stream *stream;
    const xx_lsz_member *member;
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
    stream = (xx_lsz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lsz_path_safe(member->name)) return false;

    path_option = xx_lsz_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lsz_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
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
        !xx_lsz_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
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

void xx_lsz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
