/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Qualitas install disk images (*.1, *.2, *.3 - the extension is the disk
 * number).
 *
 *   header, 14 bytes at offset 0:
 *     0x00  4 bytes, not constrained
 *     0x04  u16 LE 0x000E  header size; the only fixed bytes in the format
 *     0x06  u16 LE directory size, in bytes, not counting this header
 *     0x08  2 bytes, not constrained
 *     0x0A  u16 LE number of files advertised by the directory
 *     0x0C  2 bytes, not constrained
 *
 *   directory at 0x0E, `directory size` bytes. Records are VARIABLE length:
 *   26 fixed bytes followed by a NUL-terminated name, so the next record
 *   starts after that NUL rather than at a fixed stride.
 *
 *   record, 26 bytes + name:
 *     0x00  i32 LE absolute file offset of the NEXT record
 *     0x04  2 bytes, unused
 *     0x06  i32 LE data offset, from the start of the file
 *     0x0A  u16 LE DOS time
 *     0x0C  u16 LE DOS date
 *     0x0E  i32 LE uncompressed size
 *     0x12  i32 LE compressed size, INCLUDING the 4 byte CRC word
 *     0x16  u16 LE DOS attributes
 *     0x18  u8  disk number - 1 means "the data is in this file"
 *     0x19  u8  method
 *     0x1A  name, NUL terminated, 1..255 bytes
 *
 *   member payload at `data offset`: a 4 byte CRC word, then the PKWARE DCL
 *   implode stream. The stream is therefore `compressed size - 4` bytes.
 *
 * There is no magic. Detection rests on three things working together: the
 * constant 0x000E header-size word, the directory arithmetic (every
 * advertised file must fit in the declared directory size), and above all
 * the self-chaining next-record field - each record states the absolute
 * offset of its successor, which must be exactly where the walk has arrived.
 *
 * Both method bytes seen in the corpus (0 and 1) carry a DCL stream; the
 * byte is not a codec selector. It is published unchanged so a listing shows
 * what the archive actually says, and the decode switch refuses anything
 * else rather than guessing.
 *
 * A disk-1 image legitimately carries directory records for members that
 * live on the later disks. Those records END the walk - they are not
 * malformed - which is why the disk-number and method tests break out of
 * the loop instead of failing the parse.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qualitas/xx_qualitas.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_QUALITAS_COPY_CHUNK (64 * 1024)

typedef struct xx_qualitas_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_qualitas_member;

typedef struct xx_qualitas_stream_s {
    xx_qualitas_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_qualitas_stream;

static void xx_qualitas_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_qualitas_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_qualitas_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_qualitas_path_safe(const char *name) {
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

static void xx_qualitas_stream_free(void *pointer) {
    xx_qualitas_stream *stream = (xx_qualitas_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_qualitas_add(xx_qualitas_stream *stream,
                          const xx_qualitas_member *member) {
    xx_qualitas_member *grown = (xx_qualitas_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_QUALITAS_HEADER_SIZE 14
#define XX_QUALITAS_RECORD_SIZE 26
#define XX_QUALITAS_MIN_RECORD_TOTAL 28
#define XX_QUALITAS_HEADER_TAG 0x000EU
#define XX_QUALITAS_MAX_MEMBERS 65535
#define XX_QUALITAS_MAX_NAME 255
#define XX_QUALITAS_MIN_STREAM 5
#define XX_QUALITAS_CRC_SIZE 4
#define XX_QUALITAS_THIS_DISK 1U
#define XX_QUALITAS_METHOD_MAX 1U
#define XX_QUALITAS_MAX_DECODED (256 * 1024 * 1024)
#define XX_QUALITAS_MIN_DCL 3

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_qualitas_le16(const uint8_t *data);
static uint32_t xx_qualitas_le32(const uint8_t *data);
static xx_qualitas_stream *xx_qualitas_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_qualitas_decode(Abstractformat *self, const xx_qualitas_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The reference detector budgets the 26 fixed bytes plus the shortest
 * possible name ("x" + NUL) for every advertised file. */
/* The count field is 16 bit; nothing larger can be expressed. */
/* Payload = the 4 byte CRC word + at least one implode byte. */
/* Disk 1 means "the data is in this file". */

static uint16_t xx_qualitas_le16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

static uint32_t xx_qualitas_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_qualitas_stream *xx_qualitas_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_qualitas_stream *stream = NULL;
    uint8_t header[XX_QUALITAS_HEADER_SIZE];
    uint8_t *directory = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_size;
    int64_t directory_end;
    int64_t count;
    int64_t index;
    int64_t position;
    int64_t archive_end;
    int64_t first_data_offset = -1;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_QUALITAS_HEADER_SIZE + XX_QUALITAS_MIN_RECORD_TOTAL +
                   XX_QUALITAS_MIN_STREAM) {
        return NULL;
    }
    if (!xx_qualitas_read_at(self, self->base_address, header,
                             sizeof(header))) {
        return NULL;
    }

    /* There is no magic. This constant header-size word plus the directory
     * arithmetic below is the entire gate, so every one of these checks is
     * load bearing - dropping any of them makes this reader claim files it
     * has no business claiming. */
    if (xx_qualitas_le16(header + 4) != XX_QUALITAS_HEADER_TAG) return NULL;

    directory_size = (int64_t)xx_qualitas_le16(header + 6);
    if (directory_size == 0) return NULL;

    count = (int64_t)xx_qualitas_le16(header + 10);
    if (count < 1 || count > XX_QUALITAS_MAX_MEMBERS) return NULL;
    /* Every advertised file must fit in the declared directory. */
    if (count * XX_QUALITAS_MIN_RECORD_TOTAL > directory_size) return NULL;

    directory_end = XX_QUALITAS_HEADER_SIZE + directory_size;
    if (!xx_qualitas_range_within(span, XX_QUALITAS_HEADER_SIZE,
                                  directory_size)) {
        return NULL;
    }

    /* The directory size is a u16, so this allocation is bounded at 64 KiB
     * by the field's own width. */
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_qualitas_read_at(self,
                             self->base_address + XX_QUALITAS_HEADER_SIZE,
                             directory, (size_t)directory_size)) {
        xx_mem_free(directory);
        return NULL;
    }

    stream = (xx_qualitas_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    archive_end = directory_end;
    position = 0;

    for (index = 0; index < count; ++index) {
        xx_qualitas_member member;
        const uint8_t *record;
        char *name;
        int64_t next_record;
        int64_t data_offset;
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t name_start;
        int64_t name_end;
        int64_t name_length;
        int64_t cursor;
        uint16_t dos_time;
        uint16_t dos_date;
        uint8_t disk_number;
        uint8_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (position + XX_QUALITAS_RECORD_SIZE > directory_size) break;

        record = directory + position;
        next_record = (int64_t)(int32_t)xx_qualitas_le32(record + 0);
        data_offset = (int64_t)(int32_t)xx_qualitas_le32(record + 6);
        dos_time = xx_qualitas_le16(record + 10);
        dos_date = xx_qualitas_le16(record + 12);
        uncompressed_size = (int64_t)(int32_t)xx_qualitas_le32(record + 14);
        compressed_size = (int64_t)(int32_t)xx_qualitas_le32(record + 18);
        disk_number = record[24];
        method = record[25];

        /* These END the walk rather than failing the parse: a disk-1 image
         * legitimately carries records for members stored on later disks,
         * and the reference extractor stops at the first one exactly here. */
        if (disk_number != XX_QUALITAS_THIS_DISK) break;
        if (method > XX_QUALITAS_METHOD_MAX) break;
        if (next_record < 0 || data_offset < 0 || uncompressed_size < 0 ||
            compressed_size < XX_QUALITAS_MIN_STREAM) {
            break;
        }

        name_start = position + XX_QUALITAS_RECORD_SIZE;
        name_end = name_start;
        while (name_end < directory_size && directory[name_end] != 0U) {
            ++name_end;
        }
        /* An unterminated name means the directory is cut short, not that
         * the name runs to the end of it. */
        if (name_end >= directory_size) break;
        name_length = name_end - name_start;
        if (name_length == 0 || name_length > XX_QUALITAS_MAX_NAME) break;

        position = name_end + 1;
        /* THE check. Each record carries the absolute file offset of its
         * successor, which must be exactly where this walk has arrived. In a
         * format with no magic this self-chaining is what makes arbitrary
         * data fail; loosening it to a range test gives up the only strong
         * structural evidence the container offers. */
        if (next_record != XX_QUALITAS_HEADER_SIZE + position) goto fail;

        if (!xx_qualitas_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }
        /* Payloads live behind the directory; one that points into the
         * directory is not this format. */
        if (data_offset < directory_end) goto fail;

        name = (char *)xx_mem_alloc((size_t)name_length + 1U);
        if (!name) goto fail;
        for (cursor = 0; cursor < name_length; ++cursor) {
            const uint8_t character = directory[name_start + cursor];
            /* The whole reference corpus uses plain DOS 8.3 identifiers. The
             * reference escapes anything else as %XX; this reader has no
             * escaping path, so a name outside printable ASCII - or carrying
             * a separator or DOS-reserved character - is rejected instead of
             * being folded onto some other member's output file. */
            if (character < 0x20U || character > 0x7EU ||
                character == '/' || character == '\\' || character == ':' ||
                character == '*' || character == '?' || character == '"' ||
                character == '<' || character == '>' || character == '|') {
                xx_str_free(name);
                goto fail;
            }
            name[cursor] = (char)character;
        }
        name[name_length] = '\0';
        if (!xx_qualitas_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        if (first_data_offset < 0) first_data_offset = data_offset;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset =
            self->base_address + XX_QUALITAS_HEADER_SIZE +
            (name_start - XX_QUALITAS_RECORD_SIZE);
        member.header_size =
            XX_QUALITAS_RECORD_SIZE + name_length + 1;
        /* Point past the 4 byte CRC word so the published stream is the
         * implode data alone. */
        member.data_offset =
            self->base_address + data_offset + XX_QUALITAS_CRC_SIZE;
        member.compressed_size = compressed_size - XX_QUALITAS_CRC_SIZE;
        member.uncompressed_size = uncompressed_size;
        /* The container's own byte, unchanged: a listing should show what
         * the archive says, and the mapping belongs in decode. */
        member.method = (uint32_t)method;
        member.timestamp = ((uint64_t)dos_date << 16) | (uint64_t)dos_time;
        if (!xx_qualitas_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        if (data_offset + compressed_size > archive_end) {
            archive_end = data_offset + compressed_size;
        }
    }

    xx_mem_free(directory);
    directory = NULL;

    if (stream->count == 0U) goto fail;
    /* The first member must start right behind the directory. On a
     * headerless container this adjacency is the structural check that stops
     * arbitrary data from parsing as an archive. */
    if (first_data_offset != directory_end) goto fail;
    stream->archive_size = (archive_end < span) ? archive_end : span;
    return stream;

fail:
    xx_mem_free(directory);
    xx_qualitas_stream_free(stream);
    return NULL;
}


/* Not a codec selector: every member is PKWARE DCL implode, and both bytes
 * the corpus uses are accepted. Any other value is refused rather than
 * assumed to be stored - that assumption produces garbage that looks like
 * data. */
/* The uncompressed size is an attacker-controlled i32 in the directory:
 * refuse rather than attempt the allocation it claims to need. */
/* A DCL stream is two header bytes plus at least one token byte. */

static bool xx_qualitas_decode(Abstractformat *self,
                               const xx_qualitas_member *member,
                               uint8_t **out, size_t *out_size,
                               xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t produced = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    /* The method is unsigned, so only the upper bound needs testing. */
    if (member->method > XX_QUALITAS_METHOD_MAX) return false;
    if (member->compressed_size < XX_QUALITAS_MIN_DCL ||
        member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_QUALITAS_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_QUALITAS_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* data_offset already points past the 4 byte CRC word, so these are the
     * implode bytes and nothing else. */
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_qualitas_read_at(self, member->data_offset, packed,
                             (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* xx_dcl_decode_memory refuses a zero capacity, so a member that really
     * is empty is verified by measuring it: the stream must still reach its
     * end marker, it just may not emit a byte. */
    if (member->uncompressed_size == 0) {
        if (!xx_dcl_scan_memory(packed, (size_t)member->compressed_size, 1U,
                                NULL, &produced) ||
            produced != 0U) {
            xx_mem_free(packed);
            return false;
        }
        xx_mem_free(packed);
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    /* Exactly the recorded length or nothing: a partially decoded member
     * reported as success is the one failure a caller cannot detect. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_qualitas_init(xx_qualitas *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_QUALITAS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-qualitas");
    xx_format_set_extension(&archive->format, "1");
    archive->format.check_is_valid = xx_qualitas_check_is_valid;
    archive->format.handle_base_info = xx_qualitas_handle_base_info;
    archive->format.get_format_size = xx_qualitas_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qualitas_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qualitas_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qualitas_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qualitas_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qualitas_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qualitas_free_archive_records_reading;
    archive->format.destroy = xx_qualitas_vtable_destroy;
}

xx_qualitas *xx_qualitas_create(xx_io_device *device, int64_t base_address) {
    xx_qualitas *archive = (xx_qualitas *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_qualitas_init(archive, device, base_address);
    return archive;
}

void xx_qualitas_destroy(xx_qualitas *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_qualitas_free(xx_qualitas *archive) {
    if (!archive) return;
    xx_qualitas_destroy(archive);
    xx_mem_free(archive);
}

static void xx_qualitas_vtable_destroy(Abstractformat *self) {
    xx_qualitas_destroy((xx_qualitas *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_qualitas_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_qualitas_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_qualitas_parse(self, pd);
    if (!stream) return false;
    xx_qualitas_stream_free(stream);
    return true;
}

bool xx_qualitas_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_qualitas *archive = (xx_qualitas *)self;
    xx_qualitas_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_qualitas_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_qualitas_stream_free(stream);
    return true;
}

int64_t xx_qualitas_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_qualitas_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_qualitas *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_qualitas_set_record(xx_archive_record *record,
                                 const xx_qualitas_member *member) {
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

static bool xx_qualitas_copy_options(xx_list_s *target,
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

static const xx_var *xx_qualitas_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_qualitas_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_qualitas_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_qualitas_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_qualitas_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_qualitas_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_qualitas_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_qualitas_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_qualitas_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qualitas_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_qualitas_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_qualitas_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_qualitas_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_qualitas_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_qualitas_stream *stream;
    const xx_qualitas_member *member;
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
    stream = (xx_qualitas_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_qualitas_path_safe(member->name)) return false;

    path_option = xx_qualitas_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_qualitas_decode(self, member, &plain, &plain_size, pd);
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
        !xx_qualitas_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_qualitas_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
