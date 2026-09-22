/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Sea Data asset bundles.
 *
 *   0x00  u32 LE magic 0x12213443
 *   0x04  the first record, which must start immediately
 *
 * each record, at its own offset:
 *
 *   0x00  u32 LE tag 0x23324554 ("TE2#"); a zero word closes the chain
 *   0x04  i32 LE offset of the next record (== end of this record's data)
 *   0x08  i32 LE name length, not counting the terminator
 *   0x0C  name bytes, then one NUL
 *         data, stored verbatim, to the announced next offset
 *
 * There is no member count and no size field: a member's size is derived
 * from the gap between the previous record's end and this record's announced
 * next offset, minus the 13 bytes of overhead (tag, next offset, name length,
 * NUL) and the name. That derivation is also the container's self-check --
 * the computed data end must land exactly on the announced next offset -- and
 * it is what keeps a stray magic from parsing as a directory.
 *
 * Every member is stored verbatim; the format has no compression.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/seadata/xx_seadata.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SEADATA_COPY_CHUNK (64 * 1024)

typedef struct xx_seadata_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_seadata_member;

typedef struct xx_seadata_stream_s {
    xx_seadata_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_seadata_stream;

static void xx_seadata_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_seadata_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_seadata_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_seadata_path_safe(const char *name) {
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

static void xx_seadata_stream_free(void *pointer) {
    xx_seadata_stream *stream = (xx_seadata_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_seadata_add(xx_seadata_stream *stream,
                          const xx_seadata_member *member) {
    xx_seadata_member *grown = (xx_seadata_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_seadata_decode(Abstractformat *self,
                             const xx_seadata_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_seadata_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_SEADATA_MAGIC 0x12213443U
#define XX_SEADATA_RECORD_TAG 0x23324554U /* "TE2#" */
#define XX_SEADATA_MAGIC_SIZE 4
/* tag + next offset + name length + the name's NUL terminator */
#define XX_SEADATA_RECORD_OVERHEAD 13
#define XX_SEADATA_RECORD_FIXED 12
#define XX_SEADATA_MAX_NAME 255
#define XX_SEADATA_MAX_MEMBERS 1000000

static uint32_t xx_seadata_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The name field is raw producer-chosen bytes, and the reference corpus does
 * contain members whose names are not printable ASCII, so bytes outside the
 * safe set are not a rejection here. They are escaped as %XX instead of being
 * folded to '_': escaping is reversible and, unlike folding, cannot collapse
 * two distinct members onto one output file. */
static char *xx_seadata_name_dup(const uint8_t *raw, int64_t length) {
    static const char digits[] = "0123456789ABCDEF";
    char text[XX_SEADATA_MAX_NAME * 3 + 1];
    int64_t source;
    size_t target = 0U;

    for (source = 0; source < length; ++source) {
        uint8_t character = raw[source];
        bool safe = character > 0x20U && character < 0x7FU &&
                    character != (uint8_t)'%' && character != (uint8_t)'/' &&
                    character != (uint8_t)'\\' && character != (uint8_t)':' &&
                    character != (uint8_t)'*' && character != (uint8_t)'?' &&
                    character != (uint8_t)'"' && character != (uint8_t)'<' &&
                    character != (uint8_t)'>' && character != (uint8_t)'|';
        if (safe) {
            text[target++] = (char)character;
        } else {
            text[target++] = '%';
            text[target++] = digits[(character >> 4) & 0x0FU];
            text[target++] = digits[character & 0x0FU];
        }
    }
    text[target] = '\0';
    return xx_str_dup(text);
}

static xx_seadata_stream *xx_seadata_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_seadata_stream *stream = NULL;
    uint8_t header[XX_SEADATA_MAGIC_SIZE + 4];
    uint8_t fixed[8];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t previous_end;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The smallest possible bundle is the magic plus one empty-data record. */
    if (span < XX_SEADATA_MAGIC_SIZE + XX_SEADATA_RECORD_OVERHEAD) return NULL;

    if (!xx_seadata_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_seadata_le32(header) != XX_SEADATA_MAGIC) return NULL;
    /* The first record must follow the magic immediately: four magic bytes
     * alone are weak, and this second word is what stops an unrelated file
     * that happens to open with 0x12213443 from being walked as a chain. */
    if (xx_seadata_le32(header + 4) != XX_SEADATA_RECORD_TAG) return NULL;

    stream = (xx_seadata_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_SEADATA_MAGIC_SIZE;
    previous_end = XX_SEADATA_MAGIC_SIZE;
    index = 0;
    while (offset < span) {
        xx_seadata_member member;
        uint8_t raw[XX_SEADATA_MAX_NAME + 1];
        char *name;
        uint32_t tag;
        int64_t next_offset;
        int64_t name_length;
        int64_t data_offset;
        int64_t data_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (index >= XX_SEADATA_MAX_MEMBERS) goto fail;

        if (!xx_seadata_range_within(span, offset, 4) ||
            !xx_seadata_read_at(self, self->base_address + offset, fixed, 4U)) {
            goto fail;
        }
        tag = xx_seadata_le32(fixed);
        /* A zero word closes the chain; whatever follows is padding, not a
         * record, and is not an error. */
        if (tag == 0U) break;
        if (tag != XX_SEADATA_RECORD_TAG) goto fail;

        if (!xx_seadata_range_within(span, offset, XX_SEADATA_RECORD_FIXED) ||
            !xx_seadata_read_at(self, self->base_address + offset + 4, fixed,
                                sizeof(fixed))) {
            goto fail;
        }
        next_offset = (int64_t)(int32_t)xx_seadata_le32(fixed);
        name_length = (int64_t)(int32_t)xx_seadata_le32(fixed + 4);

        if (name_length < 1 || name_length > XX_SEADATA_MAX_NAME) goto fail;
        /* The chain only ever moves forward, and never past the end of the
         * span; without this a crafted next offset could loop the walk. */
        if (next_offset < previous_end || next_offset > span) goto fail;
        if (!xx_seadata_range_within(
                span, offset, XX_SEADATA_RECORD_FIXED + name_length + 1)) {
            goto fail;
        }
        if (!xx_seadata_read_at(self,
                                self->base_address + offset +
                                    XX_SEADATA_RECORD_FIXED,
                                raw, (size_t)(name_length + 1))) {
            goto fail;
        }
        /* The announced length must be matched by a terminator exactly there;
         * a name that is not NUL terminated at its stated length means the
         * field was never a name. */
        if (raw[name_length] != 0U) goto fail;

        data_offset = offset + XX_SEADATA_RECORD_FIXED + name_length + 1;
        /* No size field: the member's size is the gap the record spans, less
         * its own overhead. */
        data_size = (next_offset - previous_end) - name_length -
                    XX_SEADATA_RECORD_OVERHEAD;
        if (data_size < 0) goto fail;
        if (!xx_seadata_range_within(span, data_offset, data_size)) goto fail;
        /* The record's own end must land exactly on the announced next
         * offset. That agreement between two independently stored numbers is
         * the self-check the container is built on, and the main defence
         * against a false positive past the magic. */
        if (data_offset + data_size != next_offset) goto fail;

        name = xx_seadata_name_dup(raw, name_length);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = data_offset - offset;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        if (!xx_seadata_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        previous_end = next_offset;
        offset = next_offset;
        ++index;
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_seadata_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_seadata_init(xx_seadata *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SEADATA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sea-data");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_seadata_check_is_valid;
    archive->format.handle_base_info = xx_seadata_handle_base_info;
    archive->format.get_format_size = xx_seadata_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_seadata_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_seadata_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_seadata_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_seadata_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_seadata_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_seadata_free_archive_records_reading;
    archive->format.destroy = xx_seadata_vtable_destroy;
}

xx_seadata *xx_seadata_create(xx_io_device *device, int64_t base_address) {
    xx_seadata *archive = (xx_seadata *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_seadata_init(archive, device, base_address);
    return archive;
}

void xx_seadata_destroy(xx_seadata *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_seadata_free(xx_seadata *archive) {
    if (!archive) return;
    xx_seadata_destroy(archive);
    xx_mem_free(archive);
}

static void xx_seadata_vtable_destroy(Abstractformat *self) {
    xx_seadata_destroy((xx_seadata *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_seadata_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_seadata_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_seadata_parse(self, pd);
    if (!stream) return false;
    xx_seadata_stream_free(stream);
    return true;
}

bool xx_seadata_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_seadata *archive = (xx_seadata *)self;
    xx_seadata_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_seadata_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_seadata_stream_free(stream);
    return true;
}

int64_t xx_seadata_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_seadata_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_seadata *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_seadata_set_record(xx_archive_record *record,
                                 const xx_seadata_member *member) {
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

static bool xx_seadata_copy_options(xx_list_s *target,
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

static const xx_var *xx_seadata_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_seadata_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_seadata_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_seadata_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_seadata_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_seadata_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_seadata_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_seadata_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_seadata_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_seadata_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_seadata_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_seadata_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_seadata_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_seadata_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_seadata_stream *stream;
    const xx_seadata_member *member;
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
    stream = (xx_seadata_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_seadata_path_safe(member->name)) return false;

    path_option = xx_seadata_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_seadata_decode(self, member, &plain, &plain_size, pd);
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
        !xx_seadata_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_seadata_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
