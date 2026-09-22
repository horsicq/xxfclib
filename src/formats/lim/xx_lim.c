/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LIM archives.
 *
 *   header, 8 bytes at offset 0:
 *     0x00  4 bytes  magic, "LM" 0x1a 0x08
 *     0x04  u8       must be 0
 *     0x05  u8       unused
 *     0x06  u16 LE   must be 0x0010
 *
 *   chunk head, 4 bytes, repeated to end of archive:
 *     0x00  u16 LE   tag, 0xd180 directory or 0xf123 file
 *     0x02  i16 LE   chunk size, must be at least 4; the walk does not use
 *                    it to advance - the body's own layout does
 *
 *   directory chunk body: a NUL-terminated name. It sets the path prefix
 *     applied to every file chunk that follows, until the next directory
 *     chunk replaces it.
 *
 *   file chunk body: a 0x15-byte record, then a NUL-terminated name, then
 *     the packed bytes.
 *
 *     record:
 *       0x00  2 bytes  unused
 *       0x02  u16 LE   DOS time
 *       0x04  u16 LE   DOS date
 *       0x06  u8       attributes; 0x10 marks a directory entry
 *       0x07  1 byte   unused
 *       0x08  u8       method, 0 stored or 1 LIM (Huffman + LZ77)
 *       0x09  i32 LE   original size
 *       0x0d  i32 LE   packed size
 *       0x11  u32 LE   CRC of the plaintext
 *
 * There is no member count and no central directory: the next chunk head
 * sits at data_offset + packed_size. The walk ends gracefully - as the
 * reference does - on the first chunk whose tag is neither of the two known
 * ones, which is how trailing slack is tolerated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lim/xx_lim.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lim/xx_lim.h"

#include <stdio.h>

#define XX_LIM_COPY_CHUNK (64 * 1024)

typedef struct xx_lim_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_lim_member;

typedef struct xx_lim_stream_s {
    xx_lim_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lim_stream;

static void xx_lim_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lim_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_lim_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_lim_path_safe(const char *name) {
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

static void xx_lim_stream_free(void *pointer) {
    xx_lim_stream *stream = (xx_lim_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_lim_add(xx_lim_stream *stream,
                          const xx_lim_member *member) {
    xx_lim_member *grown = (xx_lim_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_LIM_MAX_MEMBERS 100000
#define XX_LIM_MAX_NAME 4096
#define XX_LIM_NAME_BUFFER (XX_LIM_MAX_NAME * 3 + 1)
#define XX_LIM_ATTR_DIRECTORY 0x10U
#define XX_LIM_HEADER_SIZE 8
#define XX_LIM_CHUNK_HEAD_SIZE 4
#define XX_LIM_RECORD_SIZE 0x15
#define XX_LIM_TAG_DIRECTORY 0xd180U
#define XX_LIM_TAG_FILE 0xf123U
#define XX_LIM_METHOD_STORED 0U
#define XX_LIM_METHOD_PACKED 1U
#define XX_LIM_MAX_COMPRESSED ((int64_t)0x7fffffff)
#define XX_LIM_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_lim_le16(const uint8_t *data);
static uint32_t xx_lim_le32(const uint8_t *data);
static bool xx_lim_read_name(Abstractformat *self, int64_t span, int64_t *offset, uint8_t *raw, size_t *out_length);
static size_t xx_lim_escape(const uint8_t *raw, size_t length, char *buffer);
static xx_lim_stream *xx_lim_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_lim_decode(Abstractformat *self, const xx_lim_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* No member count is stored, so this is a runaway guard, not a format
 * limit; it matches the reference's own ceiling. */

/* Names are NUL terminated with no length field, so the walk needs its own
 * ceiling; this is the reference's. */
/* Worst case every name byte escapes to "%XX", plus a '/' joining the
 * directory prefix. */

/* Directory attribute bit in the record's attribute byte. */

static uint16_t xx_lim_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_lim_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Read a NUL-terminated name, advancing *offset past the terminator. The
 * terminator must actually be found inside the container: a name that runs
 * to EOF means the chunk chain has wandered off, and the reference treats
 * that as a hard failure rather than as the end of the walk. */
static bool xx_lim_read_name(Abstractformat *self, int64_t span,
                             int64_t *offset, uint8_t *raw,
                             size_t *out_length) {
    size_t length = 0U;
    uint8_t byte;

    *out_length = 0U;
    for (;;) {
        if (*offset >= span) return false;
        if (!xx_lim_read_at(self, self->base_address + *offset, &byte, 1U)) {
            return false;
        }
        ++(*offset);
        if (byte == 0U) break;
        if (length >= (size_t)XX_LIM_MAX_NAME) return false;
        raw[length++] = byte;
    }
    *out_length = length;
    return true;
}

/* Escape a raw name into buffer. Backslash is LIM's path separator and
 * becomes '/'; anything else outside printable ASCII is escaped as %XX
 * rather than folded to '_', because escaping is reversible and cannot
 * collapse two distinct members onto one output file. */
static size_t xx_lim_escape(const uint8_t *raw, size_t length, char *buffer) {
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0U;
    size_t index;
    uint8_t character;

    /* DOS tooling pads with spaces; they are not part of the name. */
    while (length > 0U && raw[length - 1U] == 0x20U) --length;

    for (index = 0U; index < length; ++index) {
        character = raw[index];
        if (character == (uint8_t)'\\') {
            buffer[used++] = '/';
        } else if (character > 0x20U && character < 0x7FU &&
                   character != '%' && character != ':' &&
                   character != '*' && character != '?' &&
                   character != '"' && character != '<' &&
                   character != '>' && character != '|') {
            buffer[used++] = (char)character;
        } else {
            buffer[used++] = '%';
            buffer[used++] = digits[(character >> 4) & 0x0F];
            buffer[used++] = digits[character & 0x0F];
        }
    }
    buffer[used] = '\0';
    return used;
}

static xx_lim_stream *xx_lim_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[4] = {(uint8_t)'L', (uint8_t)'M', 0x1a, 0x08};
    xx_lim_stream *stream = NULL;
    uint8_t header[XX_LIM_HEADER_SIZE];
    uint8_t chunk[XX_LIM_CHUNK_HEAD_SIZE];
    uint8_t record[XX_LIM_RECORD_SIZE];
    uint8_t *raw = NULL;
    char *escaped = NULL;
    char *directory = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_LIM_HEADER_SIZE + XX_LIM_CHUNK_HEAD_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_lim_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* Four magic bytes, two of them non-printable control codes, plus two
     * more fixed header fields. Together these are the format's whole
     * false-positive defence, since the chunk walk below is deliberately
     * tolerant; checking only "LM" would match a great deal of text. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;
    if (header[4] != 0U) return NULL;
    if (xx_lim_le16(header + 6) != 0x0010U) return NULL;

    raw = (uint8_t *)xx_mem_alloc((size_t)XX_LIM_MAX_NAME);
    if (!raw) return NULL;
    escaped = (char *)xx_mem_alloc((size_t)XX_LIM_NAME_BUFFER);
    if (!escaped) {
        xx_mem_free(raw);
        return NULL;
    }
    stream = (xx_lim_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_LIM_HEADER_SIZE;
    while (XX_LIM_CHUNK_HEAD_SIZE <= span - offset) {
        xx_lim_member member;
        char *name;
        size_t name_length;
        size_t escaped_length;
        uint16_t tag;
        int16_t chunk_size;
        uint8_t attributes;
        int64_t record_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_LIM_MAX_MEMBERS) break;
        if (!xx_lim_read_at(self, self->base_address + offset, chunk,
                            sizeof(chunk))) {
            goto fail;
        }
        tag = xx_lim_le16(chunk);
        /* Signed: the field is an i16 and a negative chunk size is the mark
         * of a chain that has left the archive. */
        chunk_size = (int16_t)xx_lim_le16(chunk + 2);
        if (chunk_size < XX_LIM_CHUNK_HEAD_SIZE) break;
        offset += XX_LIM_CHUNK_HEAD_SIZE;

        if (tag == XX_LIM_TAG_DIRECTORY) {
            if (!xx_lim_read_name(self, span, &offset, raw, &name_length)) {
                goto fail;
            }
            xx_lim_escape(raw, name_length, escaped);
            xx_str_free(directory);
            directory = NULL;
            /* An empty directory chunk clears the prefix rather than
             * inserting a leading '/'. */
            if (escaped[0] != '\0') {
                directory = xx_str_dup(escaped);
                if (!directory) goto fail;
            }
            continue;
        }
        /* Any other tag is the end of the archive, not an error: this is how
         * the reference tolerates trailing slack. */
        if (tag != XX_LIM_TAG_FILE) break;

        record_offset = offset;
        if (!xx_lim_range_within(span, offset, XX_LIM_RECORD_SIZE)) break;
        if (!xx_lim_read_at(self, self->base_address + offset, record,
                            sizeof(record))) {
            goto fail;
        }
        offset += XX_LIM_RECORD_SIZE;
        if (!xx_lim_read_name(self, span, &offset, raw, &name_length)) {
            goto fail;
        }

        attributes = record[6];
        /* Signed on purpose: a size with the top bit set is a corrupt field,
         * not a two-gigabyte member. */
        uncompressed_size = (int64_t)(int32_t)xx_lim_le32(record + 9);
        compressed_size = (int64_t)(int32_t)xx_lim_le32(record + 13);
        if (uncompressed_size < 0 || compressed_size < 0) break;
        if (uncompressed_size > XX_LIM_MAX_DECODED ||
            compressed_size > XX_LIM_MAX_COMPRESSED) {
            break;
        }
        /* A payload running past EOF ends the walk. The reference instead
         * clamps the last member's size to what is left; publishing a member
         * whose extent is not in the file would report a truncated archive
         * as a complete one. */
        if (!xx_lim_range_within(span, offset, compressed_size)) break;

        escaped_length = xx_lim_escape(raw, name_length, escaped);
        if (escaped_length == 0U) {
            /* A nameless entry still names a real member; a positional
             * stand-in beats dropping it. */
            if (xx_rt_snprintf(escaped, (size_t)XX_LIM_NAME_BUFFER,
                               "record%u", (unsigned)count) <= 0) {
                goto fail;
            }
        }
        if (directory) {
            name = xx_str_concat3(directory, "/", escaped);
        } else {
            name = xx_str_dup(escaped);
        }
        if (!name) goto fail;
        if (!xx_lim_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + record_offset;
        /* The name field is part of the header, not of the data. */
        member.header_size = offset - record_offset;
        member.data_offset = self->base_address + offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = (uint32_t)record[8];
        /* DOS date/time; the record stores time first, date second. */
        member.timestamp = ((uint64_t)xx_lim_le16(record + 4) << 16) |
                           (uint64_t)xx_lim_le16(record + 2);
        /* A directory ENTRY is a file chunk with the DOS directory
         * attribute, not the 0xd180 chunk - that one only sets the prefix. */
        member.is_folder = (attributes & XX_LIM_ATTR_DIRECTORY) != 0U;

        if (!xx_lim_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        ++count;
        offset += compressed_size;
    }

    /* A header with no file chunk behind it is not an archive. */
    if (stream->count == 0U) goto fail;
    stream->archive_size = offset <= span ? offset : span;
    xx_str_free(directory);
    xx_mem_free(escaped);
    xx_mem_free(raw);
    return stream;

fail:
    xx_str_free(directory);
    xx_mem_free(escaped);
    xx_mem_free(raw);
    xx_lim_stream_free(stream);
    return NULL;
}




/* The container's own method numbers, stored unchanged so a listing shows
 * what the archive says. */

/* Sizes are stored as i32 and negatives are already refused, so these only
 * bound the allocation a corrupt field could ask for. */

/* Stored members and LIM method-1 members. A method the container can
 * express but this reader does not implement must fail here: treating it as
 * stored would write out compressed bytes that look like data. */
static bool xx_lim_decode(Abstractformat *self, const xx_lim_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_LIM_METHOD_STORED &&
        member->method != XX_LIM_METHOD_PACKED) {
        return false;
    }
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_LIM_MAX_COMPRESSED ||
        member->uncompressed_size > XX_LIM_MAX_DECODED) {
        return false;
    }
    /* A stored member whose two sizes disagree is corrupt: nothing could
     * account for the difference. */
    if (member->method == XX_LIM_METHOD_STORED &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }
    /* An empty member is legal; hand back a freeable zero-length buffer. */
    if (member->uncompressed_size == 0) {
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_lim_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (member->method == XX_LIM_METHOD_STORED) {
        xx_rt_memcpy(output, input, (size_t)member->uncompressed_size);
        written = (size_t)member->uncompressed_size;
    } else if (!xx_lim_decode_memory(input, (size_t)member->compressed_size,
                                     output,
                                     (size_t)member->uncompressed_size,
                                     &written)) {
        written = 0U;
    }
    /* Never report success with fewer bytes than the record claims. */
    if (written != (size_t)member->uncompressed_size) {
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

void xx_lim_init(xx_lim *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LIM;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lim");
    xx_format_set_extension(&archive->format, "lim");
    archive->format.check_is_valid = xx_lim_check_is_valid;
    archive->format.handle_base_info = xx_lim_handle_base_info;
    archive->format.get_format_size = xx_lim_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lim_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lim_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lim_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lim_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lim_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lim_free_archive_records_reading;
    archive->format.destroy = xx_lim_vtable_destroy;
}

xx_lim *xx_lim_create(xx_io_device *device, int64_t base_address) {
    xx_lim *archive = (xx_lim *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lim_init(archive, device, base_address);
    return archive;
}

void xx_lim_destroy(xx_lim *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lim_free(xx_lim *archive) {
    if (!archive) return;
    xx_lim_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lim_vtable_destroy(Abstractformat *self) {
    xx_lim_destroy((xx_lim *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lim_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lim_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lim_parse(self, pd);
    if (!stream) return false;
    xx_lim_stream_free(stream);
    return true;
}

bool xx_lim_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lim *archive = (xx_lim *)self;
    xx_lim_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lim_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lim_stream_free(stream);
    return true;
}

int64_t xx_lim_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lim_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lim *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lim_set_record(xx_archive_record *record,
                                 const xx_lim_member *member) {
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

static bool xx_lim_copy_options(xx_list_s *target,
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

static const xx_var *xx_lim_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lim_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lim_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lim_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lim_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lim_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lim_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lim_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lim_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lim_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lim_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lim_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lim_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lim_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lim_stream *stream;
    const xx_lim_member *member;
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
    stream = (xx_lim_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lim_path_safe(member->name)) return false;

    path_option = xx_lim_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lim_decode(self, member, &plain, &plain_size, pd);
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
        !xx_lim_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_lim_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
