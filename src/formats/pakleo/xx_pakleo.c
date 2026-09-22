/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PAKLEO (.PLL) archives, by Leonardus Leonardi, 1993.
 *
 *   banner, 0x25 bytes at offset 0, verbatim:
 *     "LEOLZW - (c) Leonardus Leonardi 1993\x1a"
 *
 *   record, 0x1a bytes, one per member:
 *     0x00  2 bytes  unused by the reference walk
 *     0x02  5 bytes  method tag, "-llN-" where N is an ASCII digit; the
 *                    layout is LHA's, and N is the method number
 *     0x07  i32 LE   compressed size
 *     0x0b  i32 LE   uncompressed size
 *     0x0f  u16 LE   DOS time
 *     0x11  u16 LE   DOS date
 *     0x13  2 bytes  unused
 *     0x15  u32 LE   CRC of the plaintext
 *     0x19  u8       name length, in bytes, of the field that follows
 *
 *   name: name_length bytes immediately after the record. Backslashes are
 *     path separators and are rewritten to '/'.
 *
 *   payload: compressed_size bytes immediately after the name.
 *
 * Methods: 0 stored, 1 LEOLZW (classic LZW, 9-bit initial width, explicit
 * widen and clear codes). Anything else is refused at extraction rather than
 * guessed at.
 *
 * There is no member count and no central directory: the next record sits at
 * data_offset + compressed_size. The walk ends gracefully - as the reference
 * does - on the first record that does not carry a well-formed method tag or
 * does not fit, which is how trailing slack after the last member is
 * tolerated. The 37-byte banner is what makes that tolerance safe.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pakleo/xx_pakleo.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/pakleo/xx_pakleo.h"

#include <stdio.h>

#define XX_PAKLEO_COPY_CHUNK (64 * 1024)

typedef struct xx_pakleo_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_pakleo_member;

typedef struct xx_pakleo_stream_s {
    xx_pakleo_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_pakleo_stream;

static void xx_pakleo_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_pakleo_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_pakleo_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_pakleo_path_safe(const char *name) {
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

static void xx_pakleo_stream_free(void *pointer) {
    xx_pakleo_stream *stream = (xx_pakleo_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_pakleo_add(xx_pakleo_stream *stream,
                          const xx_pakleo_member *member) {
    xx_pakleo_member *grown = (xx_pakleo_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_PAKLEO_MAX_MEMBERS 100000
#define XX_PAKLEO_NAME_FIELD 255
#define XX_PAKLEO_NAME_BUFFER (XX_PAKLEO_NAME_FIELD * 3 + 1)
#define XX_PAKLEO_BANNER_SIZE 0x25
#define XX_PAKLEO_RECORD_SIZE 0x1a
#define XX_PAKLEO_METHOD_STORED 0U
#define XX_PAKLEO_METHOD_LEOLZW 1U
#define XX_PAKLEO_MAX_COMPRESSED ((int64_t)0x7fffffff)
#define XX_PAKLEO_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_pakleo_le16(const uint8_t *data);
static uint32_t xx_pakleo_le32(const uint8_t *data);
static bool xx_pakleo_name_string(const uint8_t *field, size_t length, size_t member_index, char **out_name);
static xx_pakleo_stream *xx_pakleo_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_pakleo_decode(Abstractformat *self, const xx_pakleo_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* No member count is stored, so this is a runaway guard, not a format
 * limit; it matches the reference's own ceiling. */

/* Worst case every name byte escapes to "%XX". */

static uint16_t xx_pakleo_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_pakleo_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Build the member name. Backslash is PAKLEO's path separator and becomes
 * '/'; anything else outside printable ASCII is escaped as %XX rather than
 * folded to '_', because escaping is reversible and cannot collapse two
 * distinct members onto one output file. */
static bool xx_pakleo_name_string(const uint8_t *field, size_t length,
                                  size_t member_index, char **out_name) {
    char buffer[XX_PAKLEO_NAME_BUFFER];
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0U;
    size_t index;
    uint8_t character;
    char *name;

    *out_name = NULL;
    /* DOS tooling pads with spaces; they are not part of the name. */
    while (length > 0U && (field[length - 1U] == 0x20U ||
                           field[length - 1U] == 0x00U)) {
        --length;
    }

    for (index = 0U; index < length; ++index) {
        character = field[index];
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

    /* The name length field may legitimately be zero; a positional stand-in
     * beats dropping the member. */
    if (used == 0U) {
        if (xx_rt_snprintf(buffer, sizeof(buffer), "record%u",
                           (unsigned)member_index) <= 0) {
            return false;
        }
    }

    name = xx_str_dup(buffer);
    if (!name) return false;
    *out_name = name;
    return true;
}

static xx_pakleo_stream *xx_pakleo_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    /* The banner is 36 printable bytes plus a DOS EOF terminator. */
    static const uint8_t banner[XX_PAKLEO_BANNER_SIZE] = {
        'L', 'E', 'O', 'L', 'Z', 'W', ' ', '-', ' ', '(', 'c', ')', ' ',
        'L', 'e', 'o', 'n', 'a', 'r', 'd', 'u', 's', ' ', 'L', 'e', 'o',
        'n', 'a', 'r', 'd', 'i', ' ', '1', '9', '9', '3', 0x1a};
    xx_pakleo_stream *stream = NULL;
    uint8_t header[XX_PAKLEO_BANNER_SIZE];
    uint8_t record[XX_PAKLEO_RECORD_SIZE];
    uint8_t name_field[XX_PAKLEO_NAME_FIELD];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Banner plus at least one whole record. */
    if (span < XX_PAKLEO_BANNER_SIZE + XX_PAKLEO_RECORD_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_pakleo_read_at(self, self->base_address, header,
                           sizeof(header))) {
        return NULL;
    }
    /* Thirty-seven fixed bytes. This is the format's whole false-positive
     * defence and the reason the record walk below is allowed to end
     * gracefully instead of failing the archive: nothing else reaches this
     * point by accident. */
    if (xx_rt_memcmp(header, banner, sizeof(banner)) != 0) return NULL;

    stream = (xx_pakleo_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_PAKLEO_BANNER_SIZE;
    while (XX_PAKLEO_RECORD_SIZE <= span - offset) {
        xx_pakleo_member member;
        char *name;
        size_t name_length;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t name_offset;
        int64_t data_offset;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_PAKLEO_MAX_MEMBERS) break;
        if (!xx_pakleo_read_at(self, self->base_address + offset, record,
                               sizeof(record))) {
            goto fail;
        }

        /* The "-llN-" tag is the only per-record signature there is, and it
         * is what tells the walk it has reached slack rather than another
         * member. Breaking here - not failing - is how the reference
         * tolerates trailing bytes after the last payload. */
        if (record[2] != (uint8_t)'-' || record[3] != (uint8_t)'l' ||
            record[4] != (uint8_t)'l' || record[6] != (uint8_t)'-') {
            break;
        }
        if (record[5] < (uint8_t)'0' || record[5] > (uint8_t)'9') break;

        /* Signed on purpose: a size with the top bit set is a corrupt field,
         * not a two-gigabyte member. */
        compressed_size = (int64_t)(int32_t)xx_pakleo_le32(record + 0x07);
        uncompressed_size = (int64_t)(int32_t)xx_pakleo_le32(record + 0x0b);
        if (compressed_size < 0 || uncompressed_size < 0) break;
        if (compressed_size > XX_PAKLEO_MAX_COMPRESSED ||
            uncompressed_size > XX_PAKLEO_MAX_DECODED) {
            break;
        }

        name_length = (size_t)record[0x19];
        name_offset = offset + XX_PAKLEO_RECORD_SIZE;
        /* Name and payload must both lie inside the container; a member whose
         * extent runs past EOF ends the walk rather than being published. */
        if (!xx_pakleo_range_within(span, name_offset,
                                    (int64_t)name_length)) {
            break;
        }
        data_offset = name_offset + (int64_t)name_length;
        if (!xx_pakleo_range_within(span, data_offset, compressed_size)) {
            break;
        }
        if (name_length > 0U &&
            !xx_pakleo_read_at(self, self->base_address + name_offset,
                               name_field, name_length)) {
            goto fail;
        }

        if (!xx_pakleo_name_string(name_field, name_length, (size_t)count,
                                   &name)) {
            goto fail;
        }
        if (!xx_pakleo_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        /* The name field is part of the header, not of the data. */
        member.header_size = XX_PAKLEO_RECORD_SIZE + (int64_t)name_length;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The container's own number, straight from the ASCII digit. */
        member.method = (uint32_t)(record[5] - (uint8_t)'0');
        /* DOS date/time; the record stores time first, date second. */
        member.timestamp =
            ((uint64_t)xx_pakleo_le16(record + 0x11) << 16) |
            (uint64_t)xx_pakleo_le16(record + 0x0f);
        /* The format has no directory entries; a path is expressed entirely
         * by backslashes inside a member name. */
        member.is_folder = false;

        if (!xx_pakleo_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        ++count;
        offset = data_offset + compressed_size;
    }

    /* A banner with no well-formed record behind it is not an archive. */
    if (stream->count == 0U) goto fail;
    stream->archive_size = offset <= span ? offset : span;
    return stream;

fail:
    xx_pakleo_stream_free(stream);
    return NULL;
}



/* The container's own method numbers, taken from the ASCII digit in the
 * "-llN-" tag and stored unchanged so a listing shows what the archive
 * actually says. */

/* Sizes are stored as i32 and the reference already refuses negatives, so
 * these only bound the allocation a corrupt field could ask for. */

/* Stored members and LEOLZW members. A method digit the format defines but
 * this reader does not implement must fail here: treating it as stored would
 * write out compressed bytes that look like data. */
static bool xx_pakleo_decode(Abstractformat *self,
                             const xx_pakleo_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_PAKLEO_METHOD_STORED &&
        member->method != XX_PAKLEO_METHOD_LEOLZW) {
        return false;
    }
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_PAKLEO_MAX_COMPRESSED ||
        member->uncompressed_size > XX_PAKLEO_MAX_DECODED) {
        return false;
    }
    /* A stored member whose two sizes disagree is corrupt: there is no
     * transformation that could account for the difference. */
    if (member->method == XX_PAKLEO_METHOD_STORED &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }
    /* An empty member is legal and decodes to nothing; allocate one byte so
     * the caller always gets a freeable pointer. */
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
    if (!xx_pakleo_read_at(self, member->data_offset, input,
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
    if (member->method == XX_PAKLEO_METHOD_STORED) {
        xx_rt_memcpy(output, input, (size_t)member->uncompressed_size);
        written = (size_t)member->uncompressed_size;
    } else if (!xx_pakleo_decode_memory(input, (size_t)member->compressed_size,
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

void xx_pakleo_init(xx_pakleo *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PAKLEO;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pakleo");
    xx_format_set_extension(&archive->format, "pll");
    archive->format.check_is_valid = xx_pakleo_check_is_valid;
    archive->format.handle_base_info = xx_pakleo_handle_base_info;
    archive->format.get_format_size = xx_pakleo_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pakleo_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pakleo_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pakleo_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pakleo_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pakleo_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pakleo_free_archive_records_reading;
    archive->format.destroy = xx_pakleo_vtable_destroy;
}

xx_pakleo *xx_pakleo_create(xx_io_device *device, int64_t base_address) {
    xx_pakleo *archive = (xx_pakleo *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pakleo_init(archive, device, base_address);
    return archive;
}

void xx_pakleo_destroy(xx_pakleo *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pakleo_free(xx_pakleo *archive) {
    if (!archive) return;
    xx_pakleo_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pakleo_vtable_destroy(Abstractformat *self) {
    xx_pakleo_destroy((xx_pakleo *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pakleo_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pakleo_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_pakleo_parse(self, pd);
    if (!stream) return false;
    xx_pakleo_stream_free(stream);
    return true;
}

bool xx_pakleo_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pakleo *archive = (xx_pakleo *)self;
    xx_pakleo_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_pakleo_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_pakleo_stream_free(stream);
    return true;
}

int64_t xx_pakleo_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pakleo_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_pakleo *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_pakleo_set_record(xx_archive_record *record,
                                 const xx_pakleo_member *member) {
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

static bool xx_pakleo_copy_options(xx_list_s *target,
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

static const xx_var *xx_pakleo_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_pakleo_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_pakleo_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_pakleo_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_pakleo_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pakleo_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_pakleo_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_pakleo_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_pakleo_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pakleo_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_pakleo_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pakleo_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_pakleo_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pakleo_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_pakleo_stream *stream;
    const xx_pakleo_member *member;
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
    stream = (xx_pakleo_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_pakleo_path_safe(member->name)) return false;

    path_option = xx_pakleo_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_pakleo_decode(self, member, &plain, &plain_size, pd);
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
        !xx_pakleo_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_pakleo_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
