/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CMS XEDIT PACK / COPYFILE PACK containers.
 *
 *   0x00  0x00 0x01           fixed
 *   0x02  0x40                EBCDIC blank
 *   0x03  0xc6 or 0xe5        EBCDIC 'F' or 'V', the record format
 *   0x04  four bytes          not interpreted by the reference
 *   0x08  the packed stream, running to end-of-file
 *
 * There is ONE member, no member table, no name, no timestamp and no CRC.
 * The packed size is simply the file size minus eight.
 *
 * The codec is a plain run/literal byte coder - no dictionary, no bit
 * packing. Every count is biased by one, the 16-bit counts are BIG endian
 * (mainframe order), and 0xFF ends the stream. The output stays in EBCDIC;
 * the reference applies no code-page translation, so neither does this.
 *
 * THE CONTAINER NEVER STORES THE PLAINTEXT LENGTH. parse measures it with
 * xx_xeditpack_scan_memory(), which runs the very same walk as the decoder
 * with the output discarded, so the two can never disagree about where a
 * damaged stream ends.
 *
 * A TRUNCATED STREAM IS NOT AN ERROR in this format: a literal copy that runs
 * past the end still emits the bytes that ARE present and only then ends the
 * walk, while a run opcode with incomplete operands emits nothing. That is
 * why measuring and decoding must come from one implementation - a reader
 * that measured with its own walk would produce a length the decoder cannot
 * reach, and every damaged member would fail extraction.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xeditpack/xx_xeditpack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/xeditpack/xx_xeditpack.h"

#include <stdio.h>

#define XX_XEDITPACK_COPY_CHUNK (64 * 1024)

typedef struct xx_xeditpack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_xeditpack_member;

typedef struct xx_xeditpack_stream_s {
    xx_xeditpack_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_xeditpack_stream;

static void xx_xeditpack_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_xeditpack_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_xeditpack_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_xeditpack_path_safe(const char *name) {
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

static void xx_xeditpack_stream_free(void *pointer) {
    xx_xeditpack_stream *stream = (xx_xeditpack_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_xeditpack_add(xx_xeditpack_stream *stream,
                          const xx_xeditpack_member *member) {
    xx_xeditpack_member *grown = (xx_xeditpack_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_XEDITPACK_HEADER_SIZE 8
#define XX_XEDITPACK_MIN_SIZE 9
#define XX_XEDITPACK_MAX_MEMBERS 1
#define XX_XEDITPACK_BLANK 0x40U
#define XX_XEDITPACK_FORMAT_FIXED 0xc6U
#define XX_XEDITPACK_FORMAT_VARIABLE 0xe5U
#define XX_XEDITPACK_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_XEDITPACK_MEMBER_NAME "xeditpack_data"

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static xx_xeditpack_stream *xx_xeditpack_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_xeditpack_decode(Abstractformat *self, const xx_xeditpack_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The header plus at least one opcode. */
/* Exactly one member; the cap exists only for shape. */

/* EBCDIC 'F' and 'V' - the record format, published unchanged as the
 * member's method because it is the only per-container field the format
 * carries. */

/* The walk's own ceiling, XX_XEDITPACK_MAX_OUTPUT (0x10000000), is what the
 * reference refuses a packed stream against; this is the allocation cap on
 * the measured result, and the smaller of the two always wins. */

/* The container stores no name at all - the reference takes one from the
 * device's file name - so this placeholder is the only truthful thing to
 * publish. */

static xx_xeditpack_stream *xx_xeditpack_parse(Abstractformat *self,
                                               xx_pd_struct *pd) {
    xx_xeditpack_stream *stream;
    xx_xeditpack_member member;
    uint8_t header[XX_XEDITPACK_HEADER_SIZE];
    uint8_t *packed;
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    size_t produced = 0U;
    bool measured;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with no opcode behind it describes nothing. */
    if (span < XX_XEDITPACK_MIN_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_xeditpack_read_at(self, self->base_address, header,
                              sizeof(header))) {
        return NULL;
    }

    /* FOUR BYTES ARE THE WHOLE HEADER CHECK. 0x00 0x01 0x40 and one of two
     * record-format bytes is everything the format offers - no magic string,
     * no length, no checksum - and the remaining four header bytes are not
     * interpreted even by the reference. Anything loosened here (accepting
     * any byte at 0x03, say) turns this into a two-byte signature that any
     * file beginning 00 01 would answer to. The second line of defence is
     * the measurement below. */
    if (header[0] != 0x00U || header[1] != 0x01U ||
        header[2] != (uint8_t)XX_XEDITPACK_BLANK) {
        return NULL;
    }
    if (header[3] != (uint8_t)XX_XEDITPACK_FORMAT_FIXED &&
        header[3] != (uint8_t)XX_XEDITPACK_FORMAT_VARIABLE) {
        return NULL;
    }

    compressed_size = span - XX_XEDITPACK_HEADER_SIZE;
    if (!xx_xeditpack_range_within(span, XX_XEDITPACK_HEADER_SIZE,
                                   compressed_size)) {
        return NULL;
    }
    /* The reference refuses a packed stream larger than the walk's own
     * output ceiling before it reads it; the same limit is applied here so a
     * crafted file cannot make this reader buffer more than that. */
    if (compressed_size > (int64_t)XX_XEDITPACK_MAX_OUTPUT ||
        compressed_size > XX_XEDITPACK_MAX_DECODED) {
        return NULL;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    if (!packed) return NULL;
    if (!xx_xeditpack_read_at(self,
                              self->base_address + XX_XEDITPACK_HEADER_SIZE,
                              packed, (size_t)compressed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return NULL;
    }
    /* The plaintext length is stored nowhere, so it has to be measured, and
     * the measurement doubles as the detector: four header bytes alone would
     * match too often, while a payload that does not walk as run/literal
     * opcodes is not an XEDIT PACK stream whatever the header says. The
     * reference's ceiling is passed unchanged so the walk stops where the
     * reference's walk stops. */
    measured = xx_xeditpack_scan_memory(packed, (size_t)compressed_size,
                                        XX_XEDITPACK_MAX_OUTPUT, NULL,
                                        &produced);
    xx_mem_free(packed);
    if (!measured) return NULL;
    /* The scan already refuses an empty result; this repeats it because a
     * zero-length member would be published with a size the decode path
     * treats as a failure. */
    if (produced == 0U || (int64_t)produced > XX_XEDITPACK_MAX_DECODED) {
        return NULL;
    }
    /* Deliberately NOT checked: that the scan consumed the whole payload. A
     * stream ends at its own 0xFF and trailing slack after it is normal in
     * this format, unlike in a stream that ends only at end of input. */

    stream = (xx_xeditpack_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_XEDITPACK_MEMBER_NAME);
    if (!name) goto fail;
    if (!xx_xeditpack_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_XEDITPACK_HEADER_SIZE;
    member.data_offset = self->base_address + XX_XEDITPACK_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = (int64_t)produced;
    /* The container's own record-format byte, unchanged. */
    member.method = (uint32_t)header[3];
    /* Neither a timestamp nor a CRC exists anywhere in the container. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_xeditpack_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_xeditpack_stream_free(stream);
    return NULL;
}


static bool xx_xeditpack_decode(Abstractformat *self,
                                const xx_xeditpack_member *member,
                                uint8_t **out, size_t *out_size,
                                xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The header's record-format byte is published as the method. Only the
     * two the format defines exist; anything else means the member did not
     * come from this parse, and copying it through as stored would hand the
     * caller opcodes and call them text. */
    if (member->method != XX_XEDITPACK_FORMAT_FIXED &&
        member->method != XX_XEDITPACK_FORMAT_VARIABLE) {
        return false;
    }
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_XEDITPACK_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_XEDITPACK_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_xeditpack_read_at(self, member->data_offset, input,
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
    /* The measured length is both the capacity and the exact expected
     * output: the entry point fails rather than truncating when the stream
     * encodes more than the buffer holds, and the equality below rejects a
     * stream that produced less than parse measured. Between them, a member
     * can never be reported as decoded when it is short. */
    if (!xx_xeditpack_decode_memory(input, (size_t)member->compressed_size,
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

void xx_xeditpack_init(xx_xeditpack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_XEDITPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-xedit-pack");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_xeditpack_check_is_valid;
    archive->format.handle_base_info = xx_xeditpack_handle_base_info;
    archive->format.get_format_size = xx_xeditpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_xeditpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_xeditpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_xeditpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_xeditpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_xeditpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_xeditpack_free_archive_records_reading;
    archive->format.destroy = xx_xeditpack_vtable_destroy;
}

xx_xeditpack *xx_xeditpack_create(xx_io_device *device, int64_t base_address) {
    xx_xeditpack *archive = (xx_xeditpack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_xeditpack_init(archive, device, base_address);
    return archive;
}

void xx_xeditpack_destroy(xx_xeditpack *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_xeditpack_free(xx_xeditpack *archive) {
    if (!archive) return;
    xx_xeditpack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_xeditpack_vtable_destroy(Abstractformat *self) {
    xx_xeditpack_destroy((xx_xeditpack *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_xeditpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_xeditpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_xeditpack_parse(self, pd);
    if (!stream) return false;
    xx_xeditpack_stream_free(stream);
    return true;
}

bool xx_xeditpack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_xeditpack *archive = (xx_xeditpack *)self;
    xx_xeditpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_xeditpack_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_xeditpack_stream_free(stream);
    return true;
}

int64_t xx_xeditpack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_xeditpack_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_xeditpack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_xeditpack_set_record(xx_archive_record *record,
                                 const xx_xeditpack_member *member) {
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

static bool xx_xeditpack_copy_options(xx_list_s *target,
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

static const xx_var *xx_xeditpack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_xeditpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_xeditpack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_xeditpack_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_xeditpack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_xeditpack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_xeditpack_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_xeditpack_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_xeditpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_xeditpack_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_xeditpack_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_xeditpack_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_xeditpack_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_xeditpack_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_xeditpack_stream *stream;
    const xx_xeditpack_member *member;
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
    stream = (xx_xeditpack_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_xeditpack_path_safe(member->name)) return false;

    path_option = xx_xeditpack_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_xeditpack_decode(self, member, &plain, &plain_size, pd);
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
        !xx_xeditpack_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_xeditpack_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
