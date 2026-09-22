/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Raw LZW15V members: MS-style truncated-extension install payloads
 * (WIPEOUT.EX_, BILLBD.DL_, *.??_ generally).
 *
 * THIS CONTAINER HAS NO CONTAINER.  There is no magic, no size field, no
 * checksum and no member table: offset 0 is already the first 9-bit code of a
 * Mark Nelson LZW15V stream, and the file ends where the stream's END code
 * does.  So the reader publishes exactly one member, names it with a
 * placeholder (nothing stores a name), and its ONLY detector is a complete
 * decode of the entire file under the codec's strict grammar - every opening
 * code a literal, BUMP never past 15 bits, no code more than one past the next
 * assignable one, an explicit END, and the input consumed to the last byte.
 *
 * Three consequences follow, and all three are deliberate:
 *   * detection is O(file), so the input is CAPPED rather than sampled - a
 *     sampled decode cannot tell a real stream from a coincidence, because
 *     there is nothing else to check;
 *   * a stream that does not EXPAND is rejected, because a compressor that
 *     produced one would have stored the file instead, and a short "valid"
 *     decode of arbitrary bytes is exactly what a headerless grammar throws
 *     off by chance;
 *   * listing and extraction each run the codec once - the measuring pass
 *     cannot hand its output on, since it deliberately allocates no output
 *     buffer at all.
 *
 * The codec lives in xx_lzw15v_scan_memory() / xx_lzw15v_decode_memory() and
 * is NOT duplicated here; this file is the (absent) container only.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzw15v/xx_lzw15v.h"

#include "xxfclib/algo/lzw15v/xx_lzw15v.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* REGISTRATION PENDING.  xxfc_defs.h carries no XX_FILE_TYPE_RAW_LZW15V yet
 * and this port must not edit that shared header.  Delete this block when the
 * enum is added - until then the reader reports itself as plain binary. */

/* Nothing shorter can carry a code stream plus its END code. */
#define XX_LZW15V_MIN_FILE_SIZE ((int64_t)16)
/* Detection decodes the WHOLE file, so the input is capped rather than the
 * decode being sampled.  The reference corpus tops out at 136 KB. */
#define XX_LZW15V_MAX_FILE_SIZE ((int64_t)16 * 1024 * 1024)
/* LZW can legitimately expand a lot, but a "valid" decode running into the
 * hundreds of megabytes is a runaway, not a member. */
#define XX_LZW15V_MAX_OUTPUT_SIZE ((int64_t)64 * 1024 * 1024)
/* A stream this short cannot be told apart from a coincidence. */
#define XX_LZW15V_MIN_OUTPUT_SIZE ((int64_t)64)
#define XX_LZW15V_METHOD_LZW15V 1U

/* Nothing in the stream stores a name; the reference falls back to the
 * archive file's own name, and a reader here is handed a device, not a path,
 * so the fallback is all that is left. */
#define XX_LZW15V_MEMBER_NAME "rawlzw15v.bin"

typedef struct xx_lzw15v_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    bool is_folder;
} xx_lzw15v_member;

typedef struct xx_lzw15v_stream_s {
    xx_lzw15v_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lzw15v_stream;

static void xx_lzw15v_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lzw15v_read_at(Abstractformat *self, int64_t offset,
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

/* Refuse anything that would escape the extraction directory.  The name is a
 * constant here, but the check stays: it is what makes that a property of the
 * extraction path rather than of the constant. */
static bool xx_lzw15v_path_safe(const char *name) {
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

static void xx_lzw15v_stream_free(void *pointer) {
    xx_lzw15v_stream *stream = (xx_lzw15v_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p member->name. */
static bool xx_lzw15v_add(xx_lzw15v_stream *stream,
                          const xx_lzw15v_member *member) {
    xx_lzw15v_member *grown = (xx_lzw15v_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Read the whole file in.  Both the measuring pass and the real decode need
 * the stream as one contiguous block, and the size cap above is what keeps
 * this allocation bounded. */
static uint8_t *xx_lzw15v_load(Abstractformat *self, int64_t offset,
                               int64_t size) {
    uint8_t *buffer;

    if (size <= 0 || size > XX_LZW15V_MAX_FILE_SIZE) return NULL;
    buffer = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!buffer) return NULL;
    if (!xx_lzw15v_read_at(self, offset, buffer, (size_t)size)) {
        xx_mem_free(buffer);
        return NULL;
    }
    return buffer;
}

/* --------------------------------------------------------------- parse -- */

static xx_lzw15v_stream *xx_lzw15v_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_lzw15v_stream *stream;
    xx_lzw15v_member member;
    uint8_t *packed;
    char *name;
    int64_t total;
    int64_t span;
    size_t consumed = 0U;
    size_t produced = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_LZW15V_MIN_FILE_SIZE || span > XX_LZW15V_MAX_FILE_SIZE) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    packed = xx_lzw15v_load(self, self->base_address, span);
    if (!packed) return NULL;

    /* The only detector this format has. */
    if (!xx_lzw15v_scan_memory(packed, (size_t)span,
                               (size_t)XX_LZW15V_MAX_OUTPUT_SIZE, &consumed,
                               &produced)) {
        xx_mem_free(packed);
        return NULL;
    }
    xx_mem_free(packed);
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    /* The codec's bit pump only ever advances on a byte boundary, so "the
     * stream occupies exactly the file" is a meaningful test - and it is the
     * one that rejects a file which merely STARTS with something decodable. */
    if (consumed != (size_t)span) return NULL;
    /* A stream that does not expand is not a compressed member: a compressor
     * that produced one would have stored the file instead. */
    if ((int64_t)produced < XX_LZW15V_MIN_OUTPUT_SIZE) return NULL;
    if ((int64_t)produced <= span) return NULL;

    name = xx_str_dup(XX_LZW15V_MEMBER_NAME);
    if (!name) return NULL;
    stream = (xx_lzw15v_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_str_free(name);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    /* There is no header part at all: byte 0 is already payload. */
    member.header_offset = self->base_address;
    member.header_size = 0;
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_LZW15V_METHOD_LZW15V;
    member.is_folder = false;

    if (!xx_lzw15v_add(stream, &member)) {
        xx_str_free(name);
        xx_lzw15v_stream_free(stream);
        return NULL;
    }
    stream->archive_size = span;
    return stream;
}

/* -------------------------------------------------------------- decode -- */

/* Decode the single member.  The plaintext size comes from the measuring pass
 * in xx_lzw15v_parse(), which is the only place it exists - nothing in the
 * file records it. */
static bool xx_lzw15v_decode(Abstractformat *self,
                             const xx_lzw15v_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool decoded;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0) {
        return false;
    }
    if (member->compressed_size > XX_LZW15V_MAX_FILE_SIZE) return false;
    if (member->uncompressed_size > XX_LZW15V_MAX_OUTPUT_SIZE) return false;

    input = xx_lzw15v_load(self, member->data_offset, member->compressed_size);
    if (!input) return false;
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    decoded = xx_lzw15v_decode_memory(input, (size_t)member->compressed_size,
                                      output,
                                      (size_t)member->uncompressed_size,
                                      &written);
    /* The codec already demands an exact fill; the second half of this test is
     * what makes that a property of this reader rather than of the codec. */
    if (!decoded || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);

    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_lzw15v_init(xx_lzw15v *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_RAW_LZW15V;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzw15v");
    /* These members keep the DOS "last extension character replaced by _"
     * habit, so there is no extension of their own. */
    xx_format_set_extension(&archive->format, "_");
    archive->format.check_is_valid = xx_lzw15v_check_is_valid;
    archive->format.handle_base_info = xx_lzw15v_handle_base_info;
    archive->format.get_format_size = xx_lzw15v_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lzw15v_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lzw15v_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lzw15v_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lzw15v_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lzw15v_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lzw15v_free_archive_records_reading;
    archive->format.destroy = xx_lzw15v_vtable_destroy;
    archive->uncompressed_size = -1;
}

xx_lzw15v *xx_lzw15v_create(xx_io_device *device, int64_t base_address) {
    xx_lzw15v *archive = (xx_lzw15v *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lzw15v_init(archive, device, base_address);
    return archive;
}

void xx_lzw15v_destroy(xx_lzw15v *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lzw15v_free(xx_lzw15v *archive) {
    if (!archive) return;
    xx_lzw15v_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lzw15v_vtable_destroy(Abstractformat *self) {
    xx_lzw15v_destroy((xx_lzw15v *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lzw15v_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzw15v_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lzw15v_parse(self, pd);
    if (!stream) return false;
    xx_lzw15v_stream_free(stream);
    return true;
}

bool xx_lzw15v_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzw15v *archive = (xx_lzw15v *)self;
    xx_lzw15v_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lzw15v_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->uncompressed_size = stream->items[0].uncompressed_size;
    xx_lzw15v_stream_free(stream);
    return true;
}

int64_t xx_lzw15v_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lzw15v_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lzw15v *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lzw15v_set_record(xx_archive_record *record,
                                 const xx_lzw15v_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_lzw15v_copy_options(xx_list_s *target,
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

static const xx_var *xx_lzw15v_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lzw15v_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lzw15v_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lzw15v_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lzw15v_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lzw15v_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lzw15v_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lzw15v_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lzw15v_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lzw15v_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lzw15v_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lzw15v_stream *)state->internal_state;
    /* There is only ever one member, so this always ends the walk; it is
     * written as the general move so the state is torn down the same way. */
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lzw15v_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lzw15v_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lzw15v_stream *stream;
    const xx_lzw15v_member *member;
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
    stream = (xx_lzw15v_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lzw15v_path_safe(member->name)) return false;

    path_option =
        xx_lzw15v_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_lzw15v_decode(self, member, &plain, &plain_size, pd);
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
        !xx_lzw15v_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_lzw15v_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
