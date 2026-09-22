/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Quarterdeck QP install packages (*.qip, *.qif).
 *
 *   file header, 0x10 bytes at offset 0:
 *     0x00  "QP", 2 bytes
 *     0x02  u16 LE member count (records of kind 0 only)
 *     0x04  u32 LE index size, in bytes
 *     0x08  u16 LE version; only 2 exists
 *     0x0a  6 bytes, zero in every known sample
 *
 *   index at 0x10, `index size` bytes, entries of a fixed 0x10 stride:
 *     0x00  u32 LE absolute offset of the member record it names
 *     0x04  char[12] member name, NUL padded; bytes after the first NUL are
 *           stale producer-buffer content and must be ignored
 *
 *   record chain, from 0x10 + index size to EOF. Every record opens with
 *   "QD" and a u16 LE kind.
 *
 *   kind 1, install-destination path, 0x0e bytes plus the path:
 *     0x00  "QD", u16 LE 1
 *     0x04  u32 LE length of the name blob that follows
 *     0x08  u16 LE sequence
 *     0x0a  u32 LE reserved
 *     0x0e  the NUL-terminated destination path
 *   A path record sets the install destination of the members after it. The
 *   reference extractor writes its output flat, so the path is not folded
 *   into the member name here either; the record is parsed, validated and
 *   stepped over.
 *
 *   kind 0, member, 0x24 bytes plus the stream:
 *     0x00  "QD", u16 LE 0
 *     0x04  u32 LE compressed size
 *     0x08  u16 LE sequence
 *     0x0a  u32 LE CRC-32 (EDB88320) of the UNPACKED member
 *     0x0e  u8  DOS attributes
 *     0x0f  u16 LE DOS time
 *     0x11  u16 LE DOS date
 *     0x13  u32 LE uncompressed size
 *     0x17  char[13] name, NUL padded. Thirteen, not twelve: a name that
 *           fills the whole 8.3 field ("PRINTMAN.EXE") puts its terminator
 *           in the last byte, so reading twelve would reject the record.
 *     0x24  the compressed stream
 *
 * The stream is a complete PKWARE DCL implode stream, its 2-byte prelude
 * (literal mode, dictionary bits) included, so no codec is introduced here.
 * The container names no method anywhere; this reader records the one method
 * the format has.
 *
 * "QP" is two bytes and worth nothing on its own. Detection rests on four
 * things together: the version word and the six zero bytes behind it, the
 * index size being exactly count * 0x10, every index entry naming the record
 * it points at in order, and the record chain landing precisely on EOF with
 * every counted member found.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/quarterdeckqp/xx_quarterdeckqp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_QUARTERDECKQP_COPY_CHUNK (64 * 1024)

typedef struct xx_quarterdeckqp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_quarterdeckqp_member;

typedef struct xx_quarterdeckqp_stream_s {
    xx_quarterdeckqp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_quarterdeckqp_stream;

static void xx_quarterdeckqp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_quarterdeckqp_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_quarterdeckqp_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_quarterdeckqp_path_safe(const char *name) {
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

static void xx_quarterdeckqp_stream_free(void *pointer) {
    xx_quarterdeckqp_stream *stream = (xx_quarterdeckqp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_quarterdeckqp_add(xx_quarterdeckqp_stream *stream,
                          const xx_quarterdeckqp_member *member) {
    xx_quarterdeckqp_member *grown = (xx_quarterdeckqp_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_QUARTERDECKQP_HEADER_SIZE 0x10
#define XX_QUARTERDECKQP_INDEX_ENTRY_SIZE 0x10
#define XX_QUARTERDECKQP_INDEX_NAME_SIZE 12
#define XX_QUARTERDECKQP_VERSION_2 2U
#define XX_QUARTERDECKQP_RECORD_MAGIC_SIZE 4
#define XX_QUARTERDECKQP_RECORD_FILE 0U
#define XX_QUARTERDECKQP_RECORD_PATH 1U
#define XX_QUARTERDECKQP_FILE_HEADER_SIZE 0x24
#define XX_QUARTERDECKQP_FILE_NAME_OFFSET 0x17
#define XX_QUARTERDECKQP_FILE_NAME_FIELD 13
#define XX_QUARTERDECKQP_PATH_HEADER_SIZE 0x0e
#define XX_QUARTERDECKQP_PATH_NAME_MAX 256
#define XX_QUARTERDECKQP_MAX_MEMBERS 65535
#define XX_QUARTERDECKQP_MAX_UNCOMPRESSED 0x10000000
#define XX_QUARTERDECKQP_MIN_PAYLOAD_SIZE 3
#define XX_QUARTERDECKQP_DCL_MAX_LITERAL_MODE 1U
#define XX_QUARTERDECKQP_DCL_MIN_DICT_BITS 4U
#define XX_QUARTERDECKQP_DCL_MAX_DICT_BITS 6U
#define XX_QUARTERDECKQP_MAX_DECODED (256 * 1024 * 1024)
#define XX_QUARTERDECKQP_METHOD_DCL 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_quarterdeckqp_le16(const uint8_t *data);
static uint32_t xx_quarterdeckqp_le32(const uint8_t *data);
static size_t xx_quarterdeckqp_field_length(const uint8_t *field, size_t width);
static bool xx_quarterdeckqp_name_valid(const uint8_t *name, size_t length);
static xx_quarterdeckqp_stream *xx_quarterdeckqp_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_quarterdeckqp_decode(Abstractformat *self, const xx_quarterdeckqp_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* 8.3 plus the terminator: a twelve-character name puts its NUL in the
 * thirteenth byte, so the field must be read at its full width. */
/* The count field is a u16, so this is the format's own ceiling. */
/* Literal mode byte, dictionary-bits byte, and at least one coded byte. */

static uint16_t xx_quarterdeckqp_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_quarterdeckqp_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Length of a fixed-width NUL-padded field: the value is everything up to
 * the first terminator, and whatever follows it is stale producer-buffer
 * content (index entries carry visible garbage there). */
static size_t xx_quarterdeckqp_field_length(const uint8_t *field,
                                            size_t width) {
    size_t length = 0U;

    while (length < width && field[length] != 0U) ++length;
    return length;
}

/* DOS 8.3 member names: printable ASCII, and none of the separators that
 * would turn a name into a path. The format genuinely allows a space inside
 * a name, so only bytes below 0x20 and above 0x7E are refused. */
static bool xx_quarterdeckqp_name_valid(const uint8_t *name, size_t length) {
    size_t index;

    if (length == 0U || length > 12U) return false;
    for (index = 0U; index < length; ++index) {
        if (name[index] < 0x20U || name[index] > 0x7EU) return false;
        if (name[index] == '/' || name[index] == '\\' || name[index] == ':') {
            return false;
        }
    }
    return true;
}

static xx_quarterdeckqp_stream *xx_quarterdeckqp_parse(Abstractformat *self,
                                                       xx_pd_struct *pd) {
    static const uint8_t file_magic[2] = {'Q', 'P'};
    static const uint8_t record_magic[2] = {'Q', 'D'};
    xx_quarterdeckqp_stream *stream = NULL;
    uint8_t header[XX_QUARTERDECKQP_HEADER_SIZE];
    uint8_t record[XX_QUARTERDECKQP_FILE_HEADER_SIZE];
    uint8_t index_entry[XX_QUARTERDECKQP_INDEX_ENTRY_SIZE];
    uint8_t path_field[XX_QUARTERDECKQP_PATH_NAME_MAX];
    uint8_t prelude[2];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index_size;
    int64_t offset;
    size_t cursor;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_QUARTERDECKQP_HEADER_SIZE +
                   XX_QUARTERDECKQP_INDEX_ENTRY_SIZE +
                   XX_QUARTERDECKQP_FILE_HEADER_SIZE +
                   XX_QUARTERDECKQP_MIN_PAYLOAD_SIZE) {
        return NULL;
    }
    if (!xx_quarterdeckqp_read_at(self, self->base_address, header,
                                  sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, file_magic, sizeof(file_magic)) != 0) return NULL;

    count = (int64_t)xx_quarterdeckqp_le16(header + 2);
    index_size = (int64_t)xx_quarterdeckqp_le32(header + 4);
    /* Only version 2 was ever shipped. */
    if (xx_quarterdeckqp_le16(header + 8) != XX_QUARTERDECKQP_VERSION_2) {
        return NULL;
    }
    /* Together with the version word, these six zero bytes are what stops a
     * stray "QP" pair from matching; a two-byte magic decides nothing. */
    for (cursor = 10U; cursor < (size_t)XX_QUARTERDECKQP_HEADER_SIZE;
         ++cursor) {
        if (header[cursor] != 0U) return NULL;
    }
    if (count < 1 || count > XX_QUARTERDECKQP_MAX_MEMBERS) return NULL;
    /* Fixed stride, so the index size is fully determined by the count. */
    if (index_size != count * XX_QUARTERDECKQP_INDEX_ENTRY_SIZE) return NULL;
    if (!xx_quarterdeckqp_range_within(span, XX_QUARTERDECKQP_HEADER_SIZE,
                                       index_size)) {
        return NULL;
    }

    stream = (xx_quarterdeckqp_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_QUARTERDECKQP_HEADER_SIZE + index_size;

    while (offset != span) {
        xx_quarterdeckqp_member member;
        char *name;
        uint16_t kind;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t data_offset;
        int64_t name_blob_size;
        int64_t index_offset;
        size_t name_length;
        size_t index_name_length;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_quarterdeckqp_range_within(
                span, offset, XX_QUARTERDECKQP_RECORD_MAGIC_SIZE)) {
            goto fail;
        }
        if (!xx_quarterdeckqp_read_at(self, self->base_address + offset,
                                      record,
                                      XX_QUARTERDECKQP_RECORD_MAGIC_SIZE)) {
            goto fail;
        }
        /* Every record in the chain must be a "QD" record: anything else
         * means the previous record's size was wrong, which for this format
         * is a rejection rather than a place to resynchronise. */
        if (xx_rt_memcmp(record, record_magic, sizeof(record_magic)) != 0) {
            goto fail;
        }
        kind = xx_quarterdeckqp_le16(record + 2);

        if (kind == XX_QUARTERDECKQP_RECORD_PATH) {
            if (!xx_quarterdeckqp_range_within(
                    span, offset, XX_QUARTERDECKQP_PATH_HEADER_SIZE)) {
                goto fail;
            }
            if (!xx_quarterdeckqp_read_at(
                    self, self->base_address + offset, record,
                    XX_QUARTERDECKQP_PATH_HEADER_SIZE)) {
                goto fail;
            }
            name_blob_size = (int64_t)xx_quarterdeckqp_le32(record + 4);
            if (name_blob_size < 1 ||
                name_blob_size > XX_QUARTERDECKQP_PATH_NAME_MAX) {
                goto fail;
            }
            if (!xx_quarterdeckqp_range_within(
                    span, offset + XX_QUARTERDECKQP_PATH_HEADER_SIZE,
                    name_blob_size)) {
                goto fail;
            }
            if (!xx_quarterdeckqp_read_at(
                    self,
                    self->base_address + offset +
                        XX_QUARTERDECKQP_PATH_HEADER_SIZE,
                    path_field, (size_t)name_blob_size)) {
                goto fail;
            }
            name_length = xx_quarterdeckqp_field_length(
                path_field, (size_t)name_blob_size);
            for (cursor = 0U; cursor < name_length; ++cursor) {
                /* A path record is a DOS destination directory; a byte
                 * outside printable ASCII means this is not one. */
                if (path_field[cursor] < 0x20U || path_field[cursor] > 0x7EU) {
                    goto fail;
                }
            }
            /* The install destination is metadata, not part of a member
             * name - the reference extractor writes its output flat - so
             * the record is validated and stepped over. */
            offset += XX_QUARTERDECKQP_PATH_HEADER_SIZE + name_blob_size;
            continue;
        }
        if (kind != XX_QUARTERDECKQP_RECORD_FILE) goto fail;

        /* More member records than the header counted: the two halves of
         * the container disagree, so neither can be trusted. */
        if ((int64_t)stream->count >= count) goto fail;
        if (!xx_quarterdeckqp_range_within(
                span, offset, XX_QUARTERDECKQP_FILE_HEADER_SIZE)) {
            goto fail;
        }
        if (!xx_quarterdeckqp_read_at(self, self->base_address + offset,
                                      record,
                                      XX_QUARTERDECKQP_FILE_HEADER_SIZE)) {
            goto fail;
        }

        compressed_size = (int64_t)xx_quarterdeckqp_le32(record + 4);
        uncompressed_size = (int64_t)xx_quarterdeckqp_le32(record + 0x13);
        data_offset = offset + XX_QUARTERDECKQP_FILE_HEADER_SIZE;

        name_length = xx_quarterdeckqp_field_length(
            record + XX_QUARTERDECKQP_FILE_NAME_OFFSET,
            (size_t)XX_QUARTERDECKQP_FILE_NAME_FIELD);
        if (!xx_quarterdeckqp_name_valid(
                record + XX_QUARTERDECKQP_FILE_NAME_OFFSET, name_length)) {
            goto fail;
        }

        /* Below the DCL prelude there is no stream at all. */
        if (compressed_size < XX_QUARTERDECKQP_MIN_PAYLOAD_SIZE) goto fail;
        if (uncompressed_size <= 0 ||
            uncompressed_size > XX_QUARTERDECKQP_MAX_UNCOMPRESSED) {
            goto fail;
        }
        if (!xx_quarterdeckqp_range_within(span, data_offset,
                                           compressed_size)) {
            goto fail;
        }

        /* The container states no method, so the stream's own prelude -
         * literal mode 0..1, dictionary bits 4..6 - is the only place the
         * payload encoding is ever asserted. Dropping this check would let
         * any two bytes pass as an imploded stream. */
        if (!xx_quarterdeckqp_read_at(self, self->base_address + data_offset,
                                      prelude, sizeof(prelude))) {
            goto fail;
        }
        if (prelude[0] > XX_QUARTERDECKQP_DCL_MAX_LITERAL_MODE) goto fail;
        if (prelude[1] < XX_QUARTERDECKQP_DCL_MIN_DICT_BITS ||
            prelude[1] > XX_QUARTERDECKQP_DCL_MAX_DICT_BITS) {
            goto fail;
        }

        /* Every index entry must name the record it points at, in order.
         * This is the check that ties the header half of the container to
         * the record chain; without it the index is decoration. */
        if (!xx_quarterdeckqp_read_at(
                self,
                self->base_address + XX_QUARTERDECKQP_HEADER_SIZE +
                    (int64_t)stream->count *
                        XX_QUARTERDECKQP_INDEX_ENTRY_SIZE,
                index_entry, sizeof(index_entry))) {
            goto fail;
        }
        index_offset = (int64_t)xx_quarterdeckqp_le32(index_entry);
        if (index_offset != offset) goto fail;
        index_name_length = xx_quarterdeckqp_field_length(
            index_entry + 4, (size_t)XX_QUARTERDECKQP_INDEX_NAME_SIZE);
        if (index_name_length != name_length) goto fail;
        if (xx_rt_memcmp(index_entry + 4,
                         record + XX_QUARTERDECKQP_FILE_NAME_OFFSET,
                         name_length) != 0) {
            goto fail;
        }

        name = (char *)xx_mem_alloc(name_length + 1U);
        if (!name) goto fail;
        for (cursor = 0U; cursor < name_length; ++cursor) {
            name[cursor] = (char)record[XX_QUARTERDECKQP_FILE_NAME_OFFSET +
                                        cursor];
        }
        name[name_length] = '\0';
        if (!xx_quarterdeckqp_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_QUARTERDECKQP_FILE_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = XX_QUARTERDECKQP_METHOD_DCL;
        /* DOS date in the high half, DOS time in the low half, exactly as
         * the record stores the two words. */
        member.timestamp =
            ((uint64_t)xx_quarterdeckqp_le16(record + 0x11) << 16) |
            (uint64_t)xx_quarterdeckqp_le16(record + 0x0f);
        member.is_folder = false;
        if (!xx_quarterdeckqp_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    if (pd && xx_pd_is_stopped(pd)) goto fail;
    /* The chain carries no terminator record: it ends by landing exactly on
     * EOF with every counted member found. Relaxing either half - accepting
     * trailing bytes, or fewer members than the header claims - is what
     * would turn this reader into a two-byte magic match. */
    if (offset != span) goto fail;
    if ((int64_t)stream->count != count) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_quarterdeckqp_stream_free(stream);
    return NULL;
}


/* The uncompressed size is attacker-controlled; refuse rather than attempt
 * an allocation the record merely claims to need. */

/* The container carries no method field. Every member is a DCL implode
 * stream, and parse proves it by checking the stream prelude, so this is the
 * single value that can reach the switch below. */

static bool xx_quarterdeckqp_decode(Abstractformat *self,
                                    const xx_quarterdeckqp_member *member,
                                    uint8_t **out, size_t *out_size,
                                    xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A method this reader does not implement must fail here: silently
     * treating it as stored would hand back compressed bytes dressed up as
     * data, which the caller has no way to notice. */
    if (member->method != XX_QUARTERDECKQP_METHOD_DCL) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0 ||
        member->uncompressed_size > XX_QUARTERDECKQP_MAX_DECODED ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_quarterdeckqp_read_at(self, member->data_offset, packed,
                                  (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Exactly the recorded length or nothing: a short decode reported as
     * success is the one failure mode a caller cannot detect. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_quarterdeckqp_init(xx_quarterdeckqp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_QUARTERDECKQP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-quarterdeck-qip");
    xx_format_set_extension(&archive->format, "qip");
    archive->format.check_is_valid = xx_quarterdeckqp_check_is_valid;
    archive->format.handle_base_info = xx_quarterdeckqp_handle_base_info;
    archive->format.get_format_size = xx_quarterdeckqp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_quarterdeckqp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_quarterdeckqp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_quarterdeckqp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_quarterdeckqp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_quarterdeckqp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_quarterdeckqp_free_archive_records_reading;
    archive->format.destroy = xx_quarterdeckqp_vtable_destroy;
}

xx_quarterdeckqp *xx_quarterdeckqp_create(xx_io_device *device, int64_t base_address) {
    xx_quarterdeckqp *archive = (xx_quarterdeckqp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_quarterdeckqp_init(archive, device, base_address);
    return archive;
}

void xx_quarterdeckqp_destroy(xx_quarterdeckqp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_quarterdeckqp_free(xx_quarterdeckqp *archive) {
    if (!archive) return;
    xx_quarterdeckqp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_quarterdeckqp_vtable_destroy(Abstractformat *self) {
    xx_quarterdeckqp_destroy((xx_quarterdeckqp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_quarterdeckqp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_quarterdeckqp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_quarterdeckqp_parse(self, pd);
    if (!stream) return false;
    xx_quarterdeckqp_stream_free(stream);
    return true;
}

bool xx_quarterdeckqp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_quarterdeckqp *archive = (xx_quarterdeckqp *)self;
    xx_quarterdeckqp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_quarterdeckqp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_quarterdeckqp_stream_free(stream);
    return true;
}

int64_t xx_quarterdeckqp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_quarterdeckqp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_quarterdeckqp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_quarterdeckqp_set_record(xx_archive_record *record,
                                 const xx_quarterdeckqp_member *member) {
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

static bool xx_quarterdeckqp_copy_options(xx_list_s *target,
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

static const xx_var *xx_quarterdeckqp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_quarterdeckqp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_quarterdeckqp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_quarterdeckqp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_quarterdeckqp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_quarterdeckqp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_quarterdeckqp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_quarterdeckqp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_quarterdeckqp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_quarterdeckqp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_quarterdeckqp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_quarterdeckqp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_quarterdeckqp_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_quarterdeckqp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_quarterdeckqp_stream *stream;
    const xx_quarterdeckqp_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_quarterdeckqp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_quarterdeckqp_path_safe(member->name)) return false;

    path_option = xx_quarterdeckqp_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_quarterdeckqp_decode(self, member, &plain, &plain_size, pd);
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
        !xx_quarterdeckqp_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_quarterdeckqp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
