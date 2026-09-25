/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GLU archives. There is NO HEADER AND NO MAGIC of any kind - the file is
 * nothing but a chain of records starting at offset 0:
 *
 *   name     NUL-terminated DOS 8.3 name, 1..12 characters
 *   stream   LZW15V code stream, self-delimiting: it ends at its own END
 *            code, and its length is stored nowhere
 *
 * repeated until the file is exhausted. Neither the packed size nor the
 * plaintext size is recorded for any member, so BOTH have to be measured by
 * walking the code stream; xx_lzw15v_scan_memory() does that walk without
 * materialising the output, and it also reports where the stream ended,
 * which is where the next name begins.
 *
 * The codec is Mark Nelson's variable-width LZW, 9..15 bits, MSB-first, and
 * it differs from Unix compress and GIF in the rule that matters most: the
 * width changes ONLY on an explicit BUMP code (0x101), never implicitly when
 * the next code reaches 1 << width. END is 0x100, CLEAR is 0x102, and the
 * first assignable code is 0x103.
 *
 * DETECTION IS THE WALK. With no magic, no version and no count, the only
 * thing separating a GLU archive from an arbitrary file is that the whole
 * container decomposes into valid name/stream pairs and the last stream's
 * END code lands on the very last byte, with no trailing slack. That is why
 * the scan entry point's strict grammar is used rather than a plain decode:
 * it additionally demands that every segment opens on a literal, that BUMP
 * never fires past fifteen bits, that a used code is never more than one
 * past the next assignable one, and that the stream ends on an explicit END.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/glu/xx_glu.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzw15v/xx_lzw15v.h"

#include <stdio.h>

#define XX_GLU_COPY_CHUNK (64 * 1024)

typedef struct xx_glu_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_glu_member;

typedef struct xx_glu_stream_s {
    xx_glu_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_glu_stream;

static void xx_glu_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_glu_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_glu_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_glu_path_safe(const char *name) {
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

static void xx_glu_stream_free(void *pointer) {
    xx_glu_stream *stream = (xx_glu_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_glu_add(xx_glu_stream *stream,
                          const xx_glu_member *member) {
    xx_glu_member *grown = (xx_glu_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_GLU_MIN_FILE_SIZE 8
#define XX_GLU_MAX_FILE_SIZE ((int64_t)16 * 1024 * 1024)
#define XX_GLU_MAX_MEMBERS 65536
#define XX_GLU_MAX_NAME_LENGTH 12
#define XX_GLU_METHOD_LZW15V 1U
#define XX_GLU_MAX_DECODED ((int64_t)64 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static bool xx_glu_name_byte_ok(uint8_t value);
static bool xx_glu_name_ok(const uint8_t *name, int64_t length);
static xx_glu_stream *xx_glu_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_glu_decode(Abstractformat *self, const xx_glu_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Shortest imaginable container: a one-character name, its NUL, and a code
 * stream carrying at least one literal plus the END code. */
/* Detection has to decode the WHOLE file - there is no header to sample - so
 * the input itself is capped rather than the walk being cut short. The
 * reference corpus tops out around 86 KB. */
/* A DOS-era name field; the whole corpus is plain 8.3. */

/* Name bytes are printable ASCII minus space and minus the characters DOS
 * never allowed in a name. Rejecting these costs nothing and cuts a large
 * slice of random byte strings - which matters more here than in any other
 * reader in this tree, because the name grammar is HALF of all the
 * structure this format has. */
static bool xx_glu_name_byte_ok(uint8_t value) {
    if (value < 0x21U || value > 0x7EU) return false;
    return value != (uint8_t)'/' && value != (uint8_t)'\\' &&
           value != (uint8_t)':' && value != (uint8_t)'*' &&
           value != (uint8_t)'?' && value != (uint8_t)'"' &&
           value != (uint8_t)'<' && value != (uint8_t)'>' &&
           value != (uint8_t)'|' && value != (uint8_t)'%';
}

static bool xx_glu_name_ok(const uint8_t *name, int64_t length) {
    int64_t index;
    int64_t dots = 0;

    if (length < 1 || length > XX_GLU_MAX_NAME_LENGTH) return false;
    for (index = 0; index < length; ++index) {
        if (!xx_glu_name_byte_ok(name[index])) return false;
        if (name[index] == (uint8_t)'.') ++dots;
    }
    /* A DOS name carries at most one extension separator and never leads
     * with it. Both rules are free and both cut random data hard. */
    if (dots > 1) return false;
    return name[0] != (uint8_t)'.';
}

static xx_glu_stream *xx_glu_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_glu_stream *stream = NULL;
    xx_glu_member member;
    uint8_t *data = NULL;
    char *name;
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    int64_t name_offset;
    int64_t name_end;
    int64_t name_length;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_GLU_MIN_FILE_SIZE || span > XX_GLU_MAX_FILE_SIZE) {
        return NULL;
    }

    /* The whole container is read at once because detection has to walk all
     * of it anyway: the scan is the detector. */
    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) return NULL;
    if (!xx_glu_read_at(self, self->base_address, data, (size_t)span)) {
        xx_mem_free(data);
        return NULL;
    }

    stream = (xx_glu_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(data);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    while (offset < span) {
        size_t consumed = 0U;
        size_t produced = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_GLU_MAX_MEMBERS) goto fail;

        name_offset = offset;
        name_end = offset;
        while (name_end < span && data[name_end] != 0U) ++name_end;
        /* An unterminated name means the chain ran off the end of the file;
         * there is no other way to notice, since nothing records how many
         * members there are. */
        if (name_end >= span) goto fail;

        name_length = name_end - name_offset;
        offset = name_end + 1;
        /* The reference skips an empty name string before starting a member.
         * Nothing in the corpus produces one, but keeping the rule costs
         * nothing and cannot loop: offset always advanced past the NUL. */
        if (name_length == 0) continue;
        if (!xx_glu_name_ok(data + name_offset, name_length)) goto fail;

        /* THE DETECTOR. With no magic anywhere, the fact that the bytes at
         * this offset form a grammatically valid LZW15V stream - opening on
         * a literal, bumping only within fifteen bits, never using a code
         * more than one past the next assignable one, and ending on an
         * explicit END - is most of what separates a GLU archive from an
         * arbitrary file. The strict scan entry point is used rather than a
         * plain decode precisely for those extra rules. */
        if (!xx_lzw15v_scan_memory(data + offset, (size_t)(span - offset),
                                   (size_t)XX_GLU_MAX_DECODED, &consumed,
                                   &produced)) {
            goto fail;
        }
        if (consumed == 0U || produced == 0U) goto fail;
        if ((int64_t)consumed > span - offset) goto fail;
        if ((int64_t)produced > XX_GLU_MAX_DECODED) goto fail;

        /* The container's own NUL terminator sits at name_end inside this
         * buffer, so the name is already a C string in place. */
        name = xx_str_dup((const char *)(data + name_offset));
        if (!name) goto fail;
        if (!xx_glu_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + name_offset;
        /* The "header" is the name and its terminator; there is nothing
         * else. */
        member.header_size = (name_end + 1) - name_offset;
        member.data_offset = self->base_address + offset;
        member.compressed_size = (int64_t)consumed;
        member.uncompressed_size = (int64_t)produced;
        member.method = XX_GLU_METHOD_LZW15V;
        /* GLU records no timestamp anywhere; reporting anything but zero
         * would be an invention. */
        member.timestamp = 0U;
        /* Flat names only: the format has no directory entries. */
        member.is_folder = false;

        if (!xx_glu_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        offset += (int64_t)consumed;
    }

    if (stream->count == 0U) goto fail;
    /* THE OTHER HALF OF THE DETECTOR, and the single check a later reader
     * will most want to loosen into "close enough". The chain must consume
     * the container EXACTLY: the last END code lands on the last byte, with
     * no trailing slack and no overlay. Allowing slack here would let any
     * file beginning with one plausible name/stream pair be claimed as a
     * GLU archive. */
    if (offset != span) goto fail;

    xx_mem_free(data);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(data);
    xx_glu_stream_free(stream);
    return NULL;
}


/* The container carries no method field - it carries no fields at all - so
 * parse stamps this one synthetic value and decode refuses anything else.
 * Treating an unrecognised value as stored would hand the caller LZW codes
 * and call them the file. */

/* LZW may legitimately expand a lot, but a member running into the hundreds
 * of megabytes is a runaway, not a file. This is both the scan ceiling in
 * parse and the allocation cap here, so the two can never disagree. */

/* A member's packed extent and plaintext length were both MEASURED by parse,
 * not read from the container, so the equality test below is not a
 * cross-check against a stored value - it is a check that the second pass
 * reproduced the first. A disagreement means the file changed underneath us
 * or the two entry points diverge; either way it is a failure. */
static bool xx_glu_decode(Abstractformat *self, const xx_glu_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_GLU_METHOD_LZW15V) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_GLU_MAX_DECODED ||
        member->uncompressed_size > XX_GLU_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_glu_read_at(self, member->data_offset, input,
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
    /* compressed_size is the exact extent the scan reported, so the decoder
     * sees this member's bytes and not the next member's name. */
    if (!xx_lzw15v_decode_memory(input, (size_t)member->compressed_size,
                                 output, (size_t)member->uncompressed_size,
                                 &written) ||
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

void xx_glu_init(xx_glu *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_GLU;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-glu");
    xx_format_set_extension(&archive->format, "glu");
    archive->format.check_is_valid = xx_glu_check_is_valid;
    archive->format.handle_base_info = xx_glu_handle_base_info;
    archive->format.get_format_size = xx_glu_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_glu_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_glu_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_glu_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_glu_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_glu_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_glu_free_archive_records_reading;
    archive->format.destroy = xx_glu_vtable_destroy;
}

xx_glu *xx_glu_create(xx_io_device *device, int64_t base_address) {
    xx_glu *archive = (xx_glu *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_glu_init(archive, device, base_address);
    return archive;
}

void xx_glu_destroy(xx_glu *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_glu_free(xx_glu *archive) {
    if (!archive) return;
    xx_glu_destroy(archive);
    xx_mem_free(archive);
}

static void xx_glu_vtable_destroy(Abstractformat *self) {
    xx_glu_destroy((xx_glu *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_glu_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_glu_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_glu_parse(self, pd);
    if (!stream) return false;
    xx_glu_stream_free(stream);
    return true;
}

bool xx_glu_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_glu *archive = (xx_glu *)self;
    xx_glu_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_glu_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_glu_stream_free(stream);
    return true;
}

int64_t xx_glu_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_glu_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_glu *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_glu_set_record(xx_archive_record *record,
                                 const xx_glu_member *member) {
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

static bool xx_glu_copy_options(xx_list_s *target,
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

static const xx_var *xx_glu_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_glu_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_glu_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_glu_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_glu_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_glu_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_glu_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_glu_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_glu_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_glu_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_glu_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_glu_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_glu_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_glu_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_glu_stream *stream;
    const xx_glu_member *member;
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
    stream = (xx_glu_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_glu_path_safe(member->name)) return false;

    path_option = xx_glu_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_glu_decode(self, member, &plain, &plain_size, pd);
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
        !xx_glu_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_glu_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
