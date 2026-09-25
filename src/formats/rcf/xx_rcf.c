/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RCF 1.0 installer archives.
 *
 *   header, 10 bytes at offset 0:
 *     0x00  03 F7 E8 EB   "\x03RCF" with 0xA5 added to each of the tag bytes
 *     0x04  03 '1' '.' '0'   length-prefixed version string, always "1.0"
 *     0x08  00 00            reserved, always zero
 *
 *   member payloads start immediately at 0x0A and are packed back to back
 *   with no per-member header, so a member's offset is only knowable by
 *   summing the packed sizes of the members before it.
 *
 *   trailer:
 *     the last 2 bytes are a u16 LE member count; the directory is the
 *     count * 17 bytes directly before them.
 *
 *   directory entry, 17 bytes:
 *     0x00  u8    name length, 1..12
 *     0x01  name, 12 bytes, only the first `name length` are meaningful
 *     0x0D  i32 LE packed size
 *
 * Every member is PKWARE DCL "implode"; there is no method field, and no
 * uncompressed size is stored anywhere in the container. The plaintext
 * length has to be recovered by walking the token stream, which this reader
 * does with xx_dcl_scan_memory at parse time so that a listing can report a
 * real size and the decoder can be handed an exact output capacity.
 *
 * The four byte tag is weak on its own. What actually identifies an RCF is
 * that the packed sizes tile the span between the header and the directory
 * EXACTLY: the running offset must land on the directory offset to the byte
 * after the last member.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rcf/xx_rcf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_RCF_COPY_CHUNK (64 * 1024)

typedef struct xx_rcf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_rcf_member;

typedef struct xx_rcf_stream_s {
    xx_rcf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_rcf_stream;

static void xx_rcf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_rcf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_rcf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_rcf_path_safe(const char *name) {
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

static void xx_rcf_stream_free(void *pointer) {
    xx_rcf_stream *stream = (xx_rcf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_rcf_add(xx_rcf_stream *stream,
                          const xx_rcf_member *member) {
    xx_rcf_member *grown = (xx_rcf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_RCF_HEADER_SIZE 10
#define XX_RCF_ENTRY_SIZE 17
#define XX_RCF_NAME_FIELD 12
#define XX_RCF_COUNT_SIZE 2
#define XX_RCF_MAX_MEMBERS 65535
#define XX_RCF_MIN_STREAM 3
#define XX_RCF_METHOD_DCL 0U
#define XX_RCF_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_rcf_le16(const uint8_t *data);
static uint32_t xx_rcf_le32(const uint8_t *data);
static xx_rcf_stream *xx_rcf_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_rcf_decode(Abstractformat *self, const xx_rcf_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The count field is 16 bit; nothing larger can be expressed. */
/* A DCL stream is two header bytes plus at least one token byte. */

static uint16_t xx_rcf_le16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

static uint32_t xx_rcf_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_rcf_stream *xx_rcf_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_rcf_stream *stream;
    uint8_t header[XX_RCF_HEADER_SIZE + 2];
    uint8_t entry[XX_RCF_ENTRY_SIZE];
    uint8_t trailer[XX_RCF_COUNT_SIZE];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index;
    int64_t directory_size;
    int64_t directory_offset;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header, one directory entry, the count word, and at least a minimal
     * member stream must all fit before anything else is believed. */
    if (span < XX_RCF_HEADER_SIZE + XX_RCF_MIN_STREAM + XX_RCF_ENTRY_SIZE +
                   XX_RCF_COUNT_SIZE) {
        return NULL;
    }
    if (!xx_rcf_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* "\x03RCF" with 0xA5 added to each tag byte, then a length-prefixed
     * "1.0", then two reserved zero bytes. All ten are checked: four bytes of
     * tag alone are cheap to hit by accident. */
    if (header[0] != 0x03U || header[1] != 0xF7U || header[2] != 0xE8U ||
        header[3] != 0xEBU) {
        return NULL;
    }
    if (header[4] != 0x03U || header[5] != '1' || header[6] != '.' ||
        header[7] != '0') {
        return NULL;
    }
    if (header[8] != 0U || header[9] != 0U) return NULL;
    /* Bytes 10 and 11 are already the first member's DCL header: literal mode
     * is 0 or 1 and the dictionary size is 4..6 bits. Two more bytes of
     * constraint on a format whose only other fixed bytes are the tag. */
    if (header[10] > 1U) return NULL;
    if (header[11] < 4U || header[11] > 6U) return NULL;

    if (!xx_rcf_read_at(self, self->base_address + span - XX_RCF_COUNT_SIZE,
                        trailer, sizeof(trailer))) {
        return NULL;
    }
    count = (int64_t)xx_rcf_le16(trailer);
    if (count < 1 || count > XX_RCF_MAX_MEMBERS) return NULL;

    directory_size = (count * XX_RCF_ENTRY_SIZE) + XX_RCF_COUNT_SIZE;
    if (directory_size + XX_RCF_HEADER_SIZE > span) return NULL;
    directory_offset = span - directory_size;

    stream = (xx_rcf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* Members carry no offset of their own; the cursor is the running sum of
     * the packed sizes, starting right behind the header. */
    offset = XX_RCF_HEADER_SIZE;

    for (index = 0; index < count; ++index) {
        xx_rcf_member member;
        uint8_t *staging;
        char *name;
        int64_t entry_offset;
        int64_t name_length;
        int64_t packed_size;
        int64_t cursor;
        size_t produced;

        if (pd && xx_pd_is_stopped(pd)) goto fail;

        entry_offset = directory_offset + (XX_RCF_ENTRY_SIZE * index);
        if (!xx_rcf_read_at(self, self->base_address + entry_offset, entry,
                            sizeof(entry))) {
            goto fail;
        }

        name_length = (int64_t)entry[0];
        if (name_length < 1 || name_length > XX_RCF_NAME_FIELD) goto fail;
        for (cursor = 0; cursor < name_length; ++cursor) {
            const uint8_t character = entry[1 + cursor];
            /* RCF names are bare DOS file names. Non-printable bytes, or the
             * characters no DOS name may contain, mean this is misparsed data
             * rather than a directory. */
            if (character < 0x20U || character > 0x7EU) goto fail;
            if (character == '/' || character == '\\' || character == ':' ||
                character == '*' || character == '?' || character == '"' ||
                character == '<' || character == '>' || character == '|') {
                goto fail;
            }
        }

        packed_size = (int64_t)(int32_t)xx_rcf_le32(entry + 13);
        if (packed_size < XX_RCF_MIN_STREAM) goto fail;
        /* A member may not reach into the directory. */
        if (packed_size > directory_offset - offset) goto fail;
        if (!xx_rcf_range_within(span, offset, packed_size)) goto fail;

        /* No uncompressed size exists in the container, so the stream is
         * measured here. A member that will not decode is fatal: publishing a
         * guessed size would silently truncate the extraction later. */
        if ((uint64_t)packed_size > (uint64_t)SIZE_MAX) goto fail;
        staging = (uint8_t *)xx_mem_alloc((size_t)packed_size);
        if (!staging) goto fail;
        if (!xx_rcf_read_at(self, self->base_address + offset, staging,
                            (size_t)packed_size)) {
            xx_mem_free(staging);
            goto fail;
        }
        produced = 0U;
        if (!xx_dcl_scan_memory(staging, (size_t)packed_size,
                                (size_t)XX_RCF_MAX_DECODED, NULL, &produced)) {
            xx_mem_free(staging);
            goto fail;
        }
        xx_mem_free(staging);
        if (pd && xx_pd_is_stopped(pd)) goto fail;

        name = (char *)xx_mem_alloc((size_t)name_length + 1U);
        if (!name) goto fail;
        for (cursor = 0; cursor < name_length; ++cursor) {
            name[cursor] = (char)entry[1 + cursor];
        }
        name[name_length] = '\0';
        if (!xx_rcf_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = XX_RCF_ENTRY_SIZE;
        member.data_offset = self->base_address + offset;
        member.compressed_size = packed_size;
        member.uncompressed_size = (int64_t)produced;
        member.method = XX_RCF_METHOD_DCL;
        if (!xx_rcf_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset += packed_size;
    }

    /* THE check. The payloads must tile the span between the header and the
     * directory exactly - not "fit inside", exactly. Loosening this to <=
     * turns a four byte tag plus a plausible trailing u16 into a match, and
     * it is the only thing standing between this reader and a false
     * positive on arbitrary data. */
    if (offset != directory_offset) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_rcf_stream_free(stream);
    return NULL;
}


/* The container stores no method field - every member is a PKWARE DCL
 * implode stream - so parse publishes this synthetic number and the switch
 * below still refuses anything else rather than falling through to a copy. */
/* The decoded length is recovered from the stream, not read from the file,
 * but it is still derived from attacker-supplied bytes: cap it instead of
 * attempting whatever allocation the walk arrives at. */

static bool xx_rcf_decode(Abstractformat *self, const xx_rcf_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t produced = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    /* Anything this reader does not implement fails here: silently treating
     * an unknown method as stored produces garbage that looks like data. */
    if (member->method != XX_RCF_METHOD_DCL) return false;
    if (member->compressed_size < 3 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_RCF_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_RCF_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_rcf_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* xx_dcl_decode_memory refuses a zero capacity, so a member that really
     * does decode to nothing is verified by measuring it instead: the stream
     * still has to reach its end marker, it just may not emit a byte. */
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
    /* Exactly the measured length or nothing: a partially decoded member
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

void xx_rcf_init(xx_rcf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_RCF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rcf");
    xx_format_set_extension(&archive->format, "rcf");
    archive->format.check_is_valid = xx_rcf_check_is_valid;
    archive->format.handle_base_info = xx_rcf_handle_base_info;
    archive->format.get_format_size = xx_rcf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rcf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rcf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rcf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rcf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rcf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rcf_free_archive_records_reading;
    archive->format.destroy = xx_rcf_vtable_destroy;
}

xx_rcf *xx_rcf_create(xx_io_device *device, int64_t base_address) {
    xx_rcf *archive = (xx_rcf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_rcf_init(archive, device, base_address);
    return archive;
}

void xx_rcf_destroy(xx_rcf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_rcf_free(xx_rcf *archive) {
    if (!archive) return;
    xx_rcf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_rcf_vtable_destroy(Abstractformat *self) {
    xx_rcf_destroy((xx_rcf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_rcf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rcf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_rcf_parse(self, pd);
    if (!stream) return false;
    xx_rcf_stream_free(stream);
    return true;
}

bool xx_rcf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rcf *archive = (xx_rcf *)self;
    xx_rcf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_rcf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_rcf_stream_free(stream);
    return true;
}

int64_t xx_rcf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_rcf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_rcf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_rcf_set_record(xx_archive_record *record,
                                 const xx_rcf_member *member) {
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

static bool xx_rcf_copy_options(xx_list_s *target,
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

static const xx_var *xx_rcf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_rcf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_rcf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_rcf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_rcf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_rcf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_rcf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_rcf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_rcf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rcf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_rcf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rcf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_rcf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rcf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_rcf_stream *stream;
    const xx_rcf_member *member;
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
    stream = (xx_rcf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_rcf_path_safe(member->name)) return false;

    path_option = xx_rcf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_rcf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_rcf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_rcf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
