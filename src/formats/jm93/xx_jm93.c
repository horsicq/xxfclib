/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JM93 compressed files.
 *
 *   header, 0x49 bytes, fixed layout:
 *     0x00  "JM93", 4 bytes
 *     0x04  u8 0 - a NUL that is part of the signature, not padding
 *     0x05  name, 60 bytes: raw bytes, NUL padded to the full width
 *     0x41  u32 LE compressed size
 *     0x45  u32 LE uncompressed size
 *
 *   data, `compressed size` bytes, at 0x49: a raw PKWARE DCL implode stream
 *   whose first two bytes are the literal mode (0 or 1) and the dictionary
 *   exponent (4, 5 or 6).
 *
 *   The archive ends at 0x49 + compressed size.
 *
 * The container holds EXACTLY ONE member - there is no count field and no
 * directory - so the member list this reader publishes always has one entry.
 * No method number is stored either: the payload is always DCL imploded, so
 * the reader records method 10 (the ZIP numbering for PKWARE DCL implode) to
 * keep a listing honest about what the decode switch does.
 *
 * The name is a DOS 8.3 name across the whole reference corpus, but the
 * field is raw bytes. Path separators and the Windows reserved punctuation
 * are escaped as %XX rather than folded to '_': escaping is reversible and
 * cannot collapse two distinct names onto one output file.
 *
 * "JM93" plus a NUL is five bytes, which is short. What carries the
 * detection is the shape of the name field - non-empty, printable, and NUL
 * padded with no byte after the first NUL - together with the two sizes
 * agreeing with the file length and the DCL preamble at 0x49.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jm93/xx_jm93.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_JM93_COPY_CHUNK (64 * 1024)

typedef struct xx_jm93_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_jm93_member;

typedef struct xx_jm93_stream_s {
    xx_jm93_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_jm93_stream;

static void xx_jm93_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_jm93_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_jm93_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_jm93_path_safe(const char *name) {
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

static void xx_jm93_stream_free(void *pointer) {
    xx_jm93_stream *stream = (xx_jm93_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_jm93_add(xx_jm93_stream *stream,
                          const xx_jm93_member *member) {
    xx_jm93_member *grown = (xx_jm93_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_JM93_HEADER_SIZE 0x49
#define XX_JM93_NAME_OFFSET 0x05
#define XX_JM93_NAME_SIZE 60
#define XX_JM93_MAX_SIZE ((int64_t)1 << 32)
#define XX_JM93_MAX_NAME_OUT (XX_JM93_NAME_SIZE * 3 + 1)
#define XX_JM93_MAX_MEMBERS 1
#define XX_JM93_METHOD_DCL 10U
#define XX_JM93_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_jm93_le32(const uint8_t *data);
static bool xx_jm93_name_field_valid(const uint8_t *field);
static bool xx_jm93_name_escape(const uint8_t *field, char *out);
static xx_jm93_stream *xx_jm93_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_jm93_decode(Abstractformat *self, const xx_jm93_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Nothing this tool ever produced comes close; the cap only keeps a corrupt
 * header from asking for an absurd allocation. */
/* Worst case every one of the 60 name bytes needs a three-character %XX
 * escape, plus the terminator. */
/* The container carries exactly one member. The cap exists so the shape of
 * this reader matches every other one, not because a count is read. */

static uint32_t xx_jm93_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The 60-byte field is NUL padded. A NUL followed by a non-NUL never happens
 * in a real header, and refusing that shape is what keeps a five-byte
 * signature from firing on unrelated data. */
static bool xx_jm93_name_field_valid(const uint8_t *field) {
    bool padding = false;
    int64_t cursor;

    if (field[0] == 0U) return false;
    for (cursor = 0; cursor < XX_JM93_NAME_SIZE; ++cursor) {
        if (field[cursor] == 0U) {
            padding = true;
        } else if (padding) {
            return false;
        } else if (field[cursor] < 0x20U || field[cursor] > 0x7EU) {
            return false;
        }
    }
    return true;
}

/* Raw name -> output name. Path separators and the Windows reserved
 * punctuation become %XX: the escaping is reversible, so two distinct stored
 * names can never collapse onto one output file the way '_' folding would. */
static bool xx_jm93_name_escape(const uint8_t *field, char *out) {
    static const char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    int64_t length;
    int64_t cursor;
    int64_t written;

    length = 0;
    while (length < XX_JM93_NAME_SIZE && field[length] != 0U) ++length;
    /* 8.3 names arrive space padded inside the field. */
    while (length > 0 && field[length - 1] == (uint8_t)' ') --length;

    written = 0;
    for (cursor = 0; cursor < length; ++cursor) {
        uint8_t character = field[cursor];
        bool safe = (character > 0x20U) && (character < 0x7FU) &&
                    (character != (uint8_t)'%') && (character != (uint8_t)'/') &&
                    (character != (uint8_t)'\\') &&
                    (character != (uint8_t)':') && (character != (uint8_t)'*') &&
                    (character != (uint8_t)'?') && (character != (uint8_t)'"') &&
                    (character != (uint8_t)'<') && (character != (uint8_t)'>') &&
                    (character != (uint8_t)'|');
        if (safe) {
            out[written++] = (char)character;
        } else {
            out[written++] = '%';
            out[written++] = hex[(character >> 4) & 0x0f];
            out[written++] = hex[character & 0x0f];
        }
    }
    out[written] = '\0';
    /* A field of nothing but spaces escapes to nothing; that is a header
     * whose name is absent, not one to invent a placeholder for. */
    return written > 0;
}

static xx_jm93_stream *xx_jm93_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[4] = {'J', 'M', '9', '3'};
    xx_jm93_stream *stream;
    xx_jm93_member member;
    uint8_t header[XX_JM93_HEADER_SIZE + 2];
    char name_out[XX_JM93_MAX_NAME_OUT];
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    int64_t name_length;
    int64_t cursor;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header plus the two-byte implode preamble plus at least one byte of
     * code. */
    if (span < XX_JM93_HEADER_SIZE + 3) return NULL;
    /* Read the header and the payload's first two bytes in one go - the
     * preamble is checked below as part of the format gate. */
    if (!xx_jm93_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;
    /* The NUL at 0x04 is part of the signature; a non-zero byte there is a
     * different format that happens to start with these four characters. */
    if (header[4] != 0U) return NULL;
    if (!xx_jm93_name_field_valid(header + XX_JM93_NAME_OFFSET)) return NULL;

    compressed_size = (int64_t)xx_jm93_le32(header + 0x41);
    uncompressed_size = (int64_t)xx_jm93_le32(header + 0x45);
    /* Neither size is ever zero in a real container; an empty member is not
     * something this writer emits. */
    if (compressed_size == 0 || uncompressed_size == 0) return NULL;
    if (compressed_size > XX_JM93_MAX_SIZE ||
        uncompressed_size > XX_JM93_MAX_SIZE) {
        return NULL;
    }
    /* A member whose extent runs past EOF is a rejection, not a short read.
     * Coupling the stored size to the actual file length is the main thing
     * standing between this five-byte signature and a false positive. */
    if (!xx_jm93_range_within(span, XX_JM93_HEADER_SIZE, compressed_size)) {
        return NULL;
    }
    /* A stream this short cannot even carry the preamble and an end code. */
    if (compressed_size < 3) return NULL;

    /* The payload is a raw DCL stream whose first two bytes are plain text:
     * literal mode 0 or 1, then the dictionary exponent, which the decoder
     * only accepts as 4, 5 or 6. Two bytes that rule out a container
     * carrying a foreign payload. */
    if (header[XX_JM93_HEADER_SIZE] > 1U) return NULL;
    if (header[XX_JM93_HEADER_SIZE + 1] < 4U ||
        header[XX_JM93_HEADER_SIZE + 1] > 6U) {
        return NULL;
    }

    if (!xx_jm93_name_escape(header + XX_JM93_NAME_OFFSET, name_out)) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    stream = (xx_jm93_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name_length = (int64_t)xx_rt_strlen(name_out);
    name = (char *)xx_mem_alloc((size_t)name_length + 1U);
    if (!name) goto fail;
    for (cursor = 0; cursor < name_length; ++cursor) {
        name[cursor] = name_out[cursor];
    }
    name[name_length] = '\0';
    /* The escaping above already removed '/' and '\\', so this can only
     * catch a name that is entirely dots; keep it anyway - the check is the
     * contract, not an optimisation. */
    if (!xx_jm93_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_JM93_HEADER_SIZE;
    member.data_offset = self->base_address + XX_JM93_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = XX_JM93_METHOD_DCL;
    /* The header carries no date or time field at all. */
    member.timestamp = 0U;
    member.is_folder = false;
    if (!xx_jm93_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }

    /* The stream is the last thing in the container; anything past it is
     * overlay, not part of the archive. */
    stream->archive_size = XX_JM93_HEADER_SIZE + compressed_size;
    return stream;

fail:
    xx_jm93_stream_free(stream);
    return NULL;
}


/* JM93 stores no method number: the payload is always a raw PKWARE DCL
 * implode stream. Recorded as 10, the ZIP numbering for that codec, so a
 * listing shows the method the switch below actually dispatches on. */
/* The stored uncompressed size is attacker-controlled; refuse rather than
 * attempt an allocation the container merely claims to need. */

static bool xx_jm93_decode(Abstractformat *self, const xx_jm93_member *member,
                           uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    /* Any method other than the one this reader implements fails here:
     * silently treating an unknown method as stored produces garbage that
     * looks like data. */
    if (member->method != XX_JM93_METHOD_DCL) return false;
    if (member->compressed_size < 3 || member->uncompressed_size <= 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_JM93_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_JM93_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_jm93_read_at(self, member->data_offset, packed,
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
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    /* Exactly the promised length or nothing: a partially decoded member
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

void xx_jm93_init(xx_jm93 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_JM93;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-jm93");
    xx_format_set_extension(&archive->format, "cmp");
    archive->format.check_is_valid = xx_jm93_check_is_valid;
    archive->format.handle_base_info = xx_jm93_handle_base_info;
    archive->format.get_format_size = xx_jm93_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_jm93_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_jm93_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_jm93_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_jm93_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_jm93_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_jm93_free_archive_records_reading;
    archive->format.destroy = xx_jm93_vtable_destroy;
}

xx_jm93 *xx_jm93_create(xx_io_device *device, int64_t base_address) {
    xx_jm93 *archive = (xx_jm93 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_jm93_init(archive, device, base_address);
    return archive;
}

void xx_jm93_destroy(xx_jm93 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_jm93_free(xx_jm93 *archive) {
    if (!archive) return;
    xx_jm93_destroy(archive);
    xx_mem_free(archive);
}

static void xx_jm93_vtable_destroy(Abstractformat *self) {
    xx_jm93_destroy((xx_jm93 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_jm93_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_jm93_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_jm93_parse(self, pd);
    if (!stream) return false;
    xx_jm93_stream_free(stream);
    return true;
}

bool xx_jm93_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jm93 *archive = (xx_jm93 *)self;
    xx_jm93_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_jm93_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_jm93_stream_free(stream);
    return true;
}

int64_t xx_jm93_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_jm93_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_jm93 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_jm93_set_record(xx_archive_record *record,
                                 const xx_jm93_member *member) {
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

static bool xx_jm93_copy_options(xx_list_s *target,
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

static const xx_var *xx_jm93_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_jm93_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_jm93_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_jm93_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_jm93_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_jm93_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_jm93_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_jm93_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_jm93_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_jm93_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_jm93_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jm93_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_jm93_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_jm93_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_jm93_stream *stream;
    const xx_jm93_member *member;
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
    stream = (xx_jm93_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_jm93_path_safe(member->name)) return false;

    path_option = xx_jm93_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_jm93_decode(self, member, &plain, &plain_size, pd);
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
        !xx_jm93_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_jm93_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
