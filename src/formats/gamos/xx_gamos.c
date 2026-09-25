/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GAMOS PACKED FILE (.GPF, .EGA, .VGA, .SND) - the resource container of the
 * Russian studio Gamos.
 *
 *   header, 0x21 bytes at the base address:
 *     0x00  0x12 bytes  1A "GAMOS PACKED FILE"
 *     0x12  u8          0
 *     0x13  u8          1
 *     0x14  u16 LE      0
 *     0x16  u8          1
 *     0x17  u16 LE      number of members, never 0
 *     0x19  8 bytes     zero in every known file
 *
 *   directory, exactly that many 22-byte records back to back at 0x21:
 *     0x00  char[13]    NUL-padded 8.3 name
 *     0x0d  u8          compression method
 *     0x0e  i32 LE      offset of the member's data
 *     0x12  u16 LE      stored (compressed) size
 *     0x14  u16 LE      uncompressed size
 *
 * The directory ends exactly where the first member's data begins, so the
 * container is header, directory, payload with no separate index.  That
 * tiling check plus the 18-byte magic is the false-positive defence.
 *
 * Method 1 is the Gamos LZSS variant (algo/gamos); method 2 is stored, and its
 * +0x14 field is 0 - the reference reports the stored size as the member size
 * in that case.  Any other method byte is listed but has no decoder: the
 * record is published and extraction refuses, rather than being decoded with
 * the wrong codec.
 *
 * The reference treats the +0x0e field as an absolute file offset because it
 * has no notion of an embedded container.  Here it is taken as relative to the
 * format's base address, which is the same number for a stand-alone file and
 * is the only reading that lets the container be nested.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gamos/xx_gamos.h"

#include "xxfclib/algo/gamos/xx_gamos.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Pending registration in xxfc_defs.h.  Once the enumerator XX_FILE_TYPE_GAMOS
 * and its short alias GAMOS are added there this fallback switches itself
 * off. */
#ifndef GAMOS
#define XX_FILE_TYPE_GAMOS XX_FILE_TYPE_UNKNOWN
#endif

#define XX_GAMOS_MAGIC_SIZE 18
#define XX_GAMOS_HEADER_SIZE 0x21
#define XX_GAMOS_RECORD_SIZE 22
#define XX_GAMOS_NAME_SIZE 13
#define XX_GAMOS_METHOD_LZSS 1U
#define XX_GAMOS_METHOD_STORE 2U
/* The member count is a u16, so this is the format's own ceiling, not a
 * reader-imposed one. */
#define XX_GAMOS_MAX_MEMBERS 0xffff

typedef struct xx_gamos_member_s {
    char *name;
    int64_t record_offset;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint8_t method;
} xx_gamos_member;

typedef struct xx_gamos_stream_s {
    xx_gamos_member *items;
    size_t count;
    size_t index;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t archive_size;
} xx_gamos_stream;

static void xx_gamos_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_gamos_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_gamos_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_gamos_read_at(Abstractformat *self, int64_t offset,
                             uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_gamos_range_within(int64_t total, int64_t offset,
                                  int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_gamos_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 1U && cursor[0] == '.') return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_gamos_stream_free(void *pointer) {
    xx_gamos_stream *stream = (xx_gamos_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member; the caller has already handed over ownership of the name
 * inside @p member. */
static bool xx_gamos_add(xx_gamos_stream *stream,
                         const xx_gamos_member *member) {
    xx_gamos_member *grown;

    if (!stream || !member || stream->count >= XX_GAMOS_MAX_MEMBERS) {
        return false;
    }
    grown = (xx_gamos_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Build the member name out of the 13-byte NUL-padded field.  The reference
 * takes the bytes verbatim; bytes that are legal in a DOS directory but not in
 * an output file name are escaped as %XX here, because escaping is reversible
 * and cannot collapse two distinct members onto one file. */
static char *xx_gamos_name_string(const uint8_t *field, size_t length) {
    static const char digits[] = "0123456789ABCDEF";
    /* Worst case every byte escapes to "%XX". */
    char buffer[XX_GAMOS_NAME_SIZE * 3 + 1];
    size_t used = 0U;
    size_t index;

    if (!field || length == 0U || length > XX_GAMOS_NAME_SIZE) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t character = field[index];
        if (character > 0x20U && character < 0x7fU && character != '%' &&
            character != '/' && character != '\\' && character != ':' &&
            character != '*' && character != '?' && character != '"' &&
            character != '<' && character != '>' && character != '|') {
            buffer[used++] = (char)character;
        } else {
            buffer[used++] = '%';
            buffer[used++] = digits[(character >> 4) & 0x0f];
            buffer[used++] = digits[character & 0x0f];
        }
    }
    buffer[used] = '\0';
    if (used == 0U) return NULL;
    return xx_str_dup(buffer);
}

static xx_gamos_stream *xx_gamos_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    /* 0x1a then "GAMOS PACKED FILE"; 18 fixed bytes. */
    static const uint8_t magic[XX_GAMOS_MAGIC_SIZE] = {
        0x1a, 'G', 'A', 'M', 'O', 'S', ' ', 'P', 'A',
        'C',  'K', 'E', 'D', ' ', 'F', 'I', 'L', 'E'};
    xx_gamos_stream *stream = NULL;
    uint8_t header[XX_GAMOS_HEADER_SIZE];
    uint8_t *directory = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t archive_end;
    int32_t count;
    int32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_GAMOS_HEADER_SIZE + XX_GAMOS_RECORD_SIZE) return NULL;
    if (!xx_gamos_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;
    /* The three constant bytes and the zero word the reference checks; they
     * cost nothing and sharpen the 18-byte magic considerably. */
    if (header[0x12] != 0U || header[0x13] != 1U || header[0x16] != 1U) {
        return NULL;
    }
    if (xx_gamos_le16(header + 0x14) != 0U) return NULL;

    count = (int32_t)xx_gamos_le16(header + 0x17);
    if (count <= 0 || count > XX_GAMOS_MAX_MEMBERS) return NULL;

    directory_offset = XX_GAMOS_HEADER_SIZE;
    directory_size = (int64_t)count * XX_GAMOS_RECORD_SIZE;
    if (!xx_gamos_range_within(span, directory_offset, directory_size)) {
        return NULL;
    }
    /* Bounded by the u16 count: at most 0xffff * 22 == ~1.4 MiB. */
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_gamos_read_at(self, self->base_address + directory_offset,
                          directory, (size_t)directory_size)) {
        goto fail;
    }

    stream = (xx_gamos_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->directory_offset = directory_offset;
    stream->directory_size = directory_size;

    archive_end = directory_offset + directory_size;
    for (index = 0; index < count; ++index) {
        const uint8_t *record =
            directory + (size_t)index * XX_GAMOS_RECORD_SIZE;
        xx_gamos_member member;
        size_t name_length = 0U;
        size_t position;
        char *name;

        if (pd && xx_pd_is_stopped(pd)) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.record_offset =
            self->base_address + directory_offset +
            (int64_t)index * XX_GAMOS_RECORD_SIZE;
        member.method = record[0x0d];
        /* Signed on purpose: the field is an i32 and a negative value is a
         * corrupt record, not a four-gigabyte offset. */
        member.data_offset = (int64_t)(int32_t)xx_gamos_le32(record + 0x0e);
        member.compressed_size = (int64_t)xx_gamos_le16(record + 0x12);
        member.uncompressed_size = (int64_t)xx_gamos_le16(record + 0x14);
        /* The reference abandons the archive on a non-positive data offset
         * rather than skipping the record. */
        if (member.data_offset < 1) goto fail;
        if (!xx_gamos_range_within(span, member.data_offset,
                                   member.compressed_size)) {
            goto fail;
        }

        while (name_length < XX_GAMOS_NAME_SIZE &&
               record[name_length] != 0U) {
            ++name_length;
        }
        if (name_length == 0U) goto fail;
        for (position = 0U; position < name_length; ++position) {
            /* 8.3 names written by a DOS tool; a control byte here means the
             * directory is not a directory. */
            if (record[position] < 0x20U) goto fail;
        }
        name = xx_gamos_name_string(record, name_length);
        if (!name) goto fail;
        if (!xx_gamos_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }
        member.name = name;

        /* The stored size is the size on disk for both methods; only method 1
         * has a second, larger size. */
        if (member.method == XX_GAMOS_METHOD_STORE) {
            member.uncompressed_size = member.compressed_size;
        }
        /* Absolute file offsets live in the record; make them device
         * offsets. */
        member.data_offset += self->base_address;

        if (member.data_offset - self->base_address + member.compressed_size >
            archive_end) {
            archive_end =
                member.data_offset - self->base_address + member.compressed_size;
        }
        if (!xx_gamos_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }

    /* The directory is immediately followed by the payload, so the first
     * member's data offset has to land exactly on the end of the directory.
     * Together with the magic this is what makes the format safe to detect. */
    if (stream->count == 0U ||
        stream->items[0].data_offset !=
            self->base_address + directory_offset + directory_size) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = archive_end;
    xx_mem_free(directory);
    return stream;

fail:
    xx_mem_free(directory);
    xx_gamos_stream_free(stream);
    return NULL;
}

/* Materialise one member.  Refuses any method the reader has no decoder for
 * instead of falling back to a copy, which would write compressed bytes out
 * as if they were data. */
static bool xx_gamos_decode(Abstractformat *self,
                            const xx_gamos_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t plain_size;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member || (pd && xx_pd_is_stopped(pd))) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    /* Both size fields are u16, so this is the format's own ceiling. */
    if (member->compressed_size > 0xffff ||
        member->uncompressed_size > 0xffff) {
        return false;
    }
    if (member->method != XX_GAMOS_METHOD_STORE &&
        member->method != XX_GAMOS_METHOD_LZSS) {
        return false;
    }
    if (member->compressed_size == 0) {
        /* Nothing on disk cannot expand to anything. */
        if (member->uncompressed_size != 0) return false;
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_gamos_read_at(self, member->data_offset, input,
                          (size_t)member->compressed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(input);
        return false;
    }

    /* The reference's dispatch: method 2 is stored, and so is method 1 with a
     * zero uncompressed size - there is no plaintext length for the codec to
     * aim at, so the stored bytes are the member. */
    if (member->method == XX_GAMOS_METHOD_STORE ||
        member->uncompressed_size == 0) {
        *out = input;
        *out_size = (size_t)member->compressed_size;
        return true;
    }

    plain_size = (size_t)member->uncompressed_size;
    output = (uint8_t *)xx_mem_alloc(plain_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_gamos_decode_memory(input, (size_t)member->compressed_size,
                                output, plain_size, &written) ||
        written != plain_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = plain_size;
    return true;
}

static bool xx_gamos_copy_options(xx_list_s *target,
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

static const xx_var *xx_gamos_get_option(const xx_list_s *options,
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

static bool xx_gamos_set_record(xx_archive_record *record,
                                const xx_gamos_member *member) {
    if (!record || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->record_offset;
    record->header_size = XX_GAMOS_RECORD_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           /* The container's own method number, stored unchanged so a listing
            * shows what the archive actually says. */
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           /* The format has no directory entries and no timestamps. */
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_gamos_init(xx_gamos *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_GAMOS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gamos");
    xx_format_set_extension(&archive->format, "gpf");
    archive->format.check_is_valid = xx_gamos_check_is_valid;
    archive->format.handle_base_info = xx_gamos_handle_base_info;
    archive->format.get_format_size = xx_gamos_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gamos_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gamos_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gamos_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gamos_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gamos_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gamos_free_archive_records_reading;
    archive->format.destroy = xx_gamos_vtable_destroy;
    archive->archive_size = -1;
}

xx_gamos *xx_gamos_create(xx_io_device *device, int64_t base_address) {
    xx_gamos *archive = (xx_gamos *)xx_mem_alloc(sizeof(*archive));

    if (archive) xx_gamos_init(archive, device, base_address);
    return archive;
}

void xx_gamos_destroy(xx_gamos *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->directory_offset = 0;
    archive->directory_size = 0;
    archive->archive_size = -1;
}

static void xx_gamos_vtable_destroy(Abstractformat *self) {
    xx_gamos_destroy((xx_gamos *)self);
}

void xx_gamos_free(xx_gamos *archive) {
    if (!archive) return;
    xx_gamos_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_gamos_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_gamos_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_gamos_parse(self, pd);
    if (!stream) return false;
    xx_gamos_stream_free(stream);
    return true;
}

bool xx_gamos_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_gamos *archive = (xx_gamos *)self;
    xx_gamos_stream *stream;
    int64_t total;

    if (!self) return false;
    stream = xx_gamos_parse(self, pd);
    if (!stream) {
        archive->number_of_records = 0U;
        archive->directory_offset = 0;
        archive->directory_size = 0;
        archive->archive_size = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive->number_of_records = stream->count;
    archive->directory_offset = stream->directory_offset;
    archive->directory_size = stream->directory_size;
    archive->archive_size = stream->archive_size;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->archive_size) {
        self->overlay_offset = self->base_address + stream->archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_FILE_TYPE_GAMOS;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_gamos_stream_free(stream);
    return true;
}

int64_t xx_gamos_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_gamos_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_gamos *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

xx_archive_record_state *xx_gamos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_gamos_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_gamos_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_gamos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_gamos_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_gamos_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_gamos_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_gamos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gamos_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_gamos_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_gamos_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_gamos_set_record(&state->current_record,
                            &stream->items[stream->index]);
    return state->has_record;
}

bool xx_gamos_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_gamos_stream *stream;
    const xx_gamos_member *member;
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
    stream = (xx_gamos_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_gamos_path_safe(member->name)) return false;

    path_option = xx_gamos_get_option(&state->options,
                                      XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_gamos_decode(self, member, &plain, &plain_size, pd);
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

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_gamos_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_gamos_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

uint64_t xx_gamos_get_number_of_records(const xx_gamos *archive) {
    return archive ? archive->number_of_records : 0U;
}

int64_t xx_gamos_get_archive_size(const xx_gamos *archive) {
    return archive ? archive->archive_size : -1;
}
