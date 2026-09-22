/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM Personal Communications for OS/2 (PCOMM 4.x) install-diskette payload.
 *
 * There is no header of ANY kind - no magic, no size, no checksum, no name.
 * The packer mangles the last character of the 8.3 name to '_' on disk and
 * writes the raw token stream from byte 0, so the whole container is one
 * member and the only structure that exists is the token grammar itself.
 *
 *   the stream is a chain of independent blocks, each at most 16384
 *   plaintext bytes; a match source never crosses a block boundary, which is
 *   what lets a far match spend an absolute 16-bit offset. Control byte C:
 *
 *     C >= 0xe1   literal run of (C - 0xe0) bytes, 1..31
 *     C == 0xe0   end of the current block; the next block starts empty
 *     C >= 0x20   two-byte token: length (C >> 5) + 2, i.e. 3..8, and
 *                 distance back ((C & 0x1f) << 8 | B1) + 1, i.e. 1..8192
 *     C <  0x20   three-byte token: length C + 4, i.e. 4..35, source at the
 *                 ABSOLUTE offset B1 | (B2 << 8) from the start of the
 *                 current block - a distance back decodes a few dozen bytes
 *                 and then dies on an impossible offset
 *
 *   a complete file ends with two 0xe0 bytes: one closing the last data
 *   block and one closing an empty terminator block, and every block before
 *   the last data block is exactly 16384 bytes.
 *
 * Because nothing is stored, detection IS the decode: the structural walk in
 * xx_pcommos2_scan_memory() is the only thing standing between this reader
 * and every other headerless binary on disk.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pcommos2/xx_pcommos2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/pcommos2/xx_pcommos2.h"

#include <stdio.h>

#define XX_PCOMMOS2_COPY_CHUNK (64 * 1024)

typedef struct xx_pcommos2_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_pcommos2_member;

typedef struct xx_pcommos2_stream_s {
    xx_pcommos2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_pcommos2_stream;

static void xx_pcommos2_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_pcommos2_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_pcommos2_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_pcommos2_path_safe(const char *name) {
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

static void xx_pcommos2_stream_free(void *pointer) {
    xx_pcommos2_stream *stream = (xx_pcommos2_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_pcommos2_add(xx_pcommos2_stream *stream,
                          const xx_pcommos2_member *member) {
    xx_pcommos2_member *grown = (xx_pcommos2_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_PCOMMOS2_MAX_MEMBERS 1
#define XX_PCOMMOS2_METHOD_LZ77 0U
#define XX_PCOMMOS2_MIN_FILE_SIZE ((int64_t)4)
#define XX_PCOMMOS2_MAX_FILE_SIZE ((int64_t)32 * 1024 * 1024)
#define XX_PCOMMOS2_MAX_DECODED ((int64_t)0x10000000)
#define XX_PCOMMOS2_FALLBACK_NAME "pcomm_data"

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static xx_pcommos2_stream *xx_pcommos2_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_pcommos2_decode(Abstractformat *self, const xx_pcommos2_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The whole device is the member, always. */

/* The container stores no method number; there is exactly one codec. 0 is the
 * only value parse ever publishes and the only one decode accepts. */

/* The shortest legal file: a one-byte literal run (two bytes) plus the two
 * block terminators. */

/* Detection has to read the candidate WHOLE - there is no header to reject it
 * on - so this ceiling is what keeps a probe of a large unrelated file from
 * turning into a large allocation. Install-diskette payloads are floppy
 * sized; the largest of the 582-file reference corpus is barely over 1 MiB. */

/* Separate, larger ceiling for the plaintext: the packer is an LZ77, so a
 * 32 MiB stream can legitimately expand well past its own size. */

/* No name is stored anywhere in the stream, and the mangled character of the
 * on-disk name is not recoverable from it, so a fixed placeholder is the only
 * honest answer this layer can give. */

static xx_pcommos2_stream *xx_pcommos2_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_pcommos2_stream *stream = NULL;
    xx_pcommos2_member member;
    uint8_t *source = NULL;
    uint8_t gate[2];
    int64_t total;
    int64_t span;
    size_t consumed = 0U;
    size_t produced = 0U;
    char *name = NULL;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_PCOMMOS2_MIN_FILE_SIZE) return NULL;
    if (span > XX_PCOMMOS2_MAX_FILE_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    /* Three single-byte reads before the candidate is pulled into memory. A
     * block must open with a literal-run control because nothing has been
     * emitted yet to match against, and a complete file must end with the two
     * block terminators. Over 18806 files of other formats this gate alone
     * left nothing for the full walk to reject - it is the cheap half of the
     * format's only defence and must not be dropped as "redundant" just
     * because the scan below repeats the work. */
    if (!xx_pcommos2_read_at(self, self->base_address, gate, 1U)) return NULL;
    if (gate[0] < 0xe1U) return NULL;
    if (!xx_pcommos2_read_at(self, self->base_address + span - 2, gate, 2U)) {
        return NULL;
    }
    if ((gate[0] != 0xe0U) || (gate[1] != 0xe0U)) return NULL;

    source = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!source) return NULL;
    if (!xx_pcommos2_read_at(self, self->base_address, source, (size_t)span)) {
        xx_mem_free(source);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(source);
        return NULL;
    }

    /* The expensive half of the defence, and the whole detector. The walk
     * produces no output but proves the token stream self-consistent, that no
     * block overruns 16384 bytes, that every block but the last is exactly
     * full, and - the part most easily lost in a rewrite - that the input is
     * consumed to the last byte. Accepting a stream that stops short would
     * make this format match the prefix of arbitrary binaries. */
    if (!xx_pcommos2_scan_memory(source, (size_t)span,
                                 (size_t)XX_PCOMMOS2_MAX_DECODED, &consumed,
                                 &produced)) {
        xx_mem_free(source);
        return NULL;
    }
    xx_mem_free(source);
    source = NULL;

    if (consumed != (size_t)span) return NULL;
    if ((produced == 0U) || ((int64_t)produced > XX_PCOMMOS2_MAX_DECODED)) {
        return NULL;
    }

    stream = (xx_pcommos2_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (!xx_pcommos2_range_within(span, 0, span)) goto fail;
    if (stream->count >= (size_t)XX_PCOMMOS2_MAX_MEMBERS) goto fail;

    name = xx_str_dup(XX_PCOMMOS2_FALLBACK_NAME);
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    /* No header exists, so the member's header span is empty rather than
     * pointing at the first bytes of the payload. */
    member.header_offset = self->base_address;
    member.header_size = 0;
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_PCOMMOS2_METHOD_LZ77;
    /* The format records no timestamp at all. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_pcommos2_add(stream, &member)) goto fail;
    name = NULL;

    /* The token stream runs to the last byte - the trailing 0xe0 0xe0 pair is
     * part of it - so there is never a trailer or an overlay. */
    stream->archive_size = span;
    return stream;

fail:
    if (name) xx_str_free(name);
    xx_pcommos2_stream_free(stream);
    return NULL;
}


/* A decode that re-walks the same stream the parse measured. The scan is not
 * repeated here: the parse already proved the stream well formed and recorded
 * the size it produces, and the codec refuses to publish anything that does
 * not reproduce that size exactly. */
static bool xx_pcommos2_decode(Abstractformat *self,
                               const xx_pcommos2_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The container defines exactly one codec. Refusing anything else keeps a
     * later reader from routing an invented method through it as if stored. */
    if (member->method != XX_PCOMMOS2_METHOD_LZ77) return false;
    if (member->compressed_size < XX_PCOMMOS2_MIN_FILE_SIZE) return false;
    if (member->compressed_size > XX_PCOMMOS2_MAX_FILE_SIZE) return false;
    if (member->uncompressed_size < 1) return false;
    if (member->uncompressed_size > XX_PCOMMOS2_MAX_DECODED) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_pcommos2_read_at(self, member->data_offset, input,
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
    if (!xx_pcommos2_decode_memory(input, (size_t)member->compressed_size,
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

void xx_pcommos2_init(xx_pcommos2 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PCOMMOS2;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "pcomm");
    archive->format.check_is_valid = xx_pcommos2_check_is_valid;
    archive->format.handle_base_info = xx_pcommos2_handle_base_info;
    archive->format.get_format_size = xx_pcommos2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pcommos2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pcommos2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pcommos2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pcommos2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pcommos2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pcommos2_free_archive_records_reading;
    archive->format.destroy = xx_pcommos2_vtable_destroy;
}

xx_pcommos2 *xx_pcommos2_create(xx_io_device *device, int64_t base_address) {
    xx_pcommos2 *archive = (xx_pcommos2 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pcommos2_init(archive, device, base_address);
    return archive;
}

void xx_pcommos2_destroy(xx_pcommos2 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pcommos2_free(xx_pcommos2 *archive) {
    if (!archive) return;
    xx_pcommos2_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pcommos2_vtable_destroy(Abstractformat *self) {
    xx_pcommos2_destroy((xx_pcommos2 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pcommos2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcommos2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_pcommos2_parse(self, pd);
    if (!stream) return false;
    xx_pcommos2_stream_free(stream);
    return true;
}

bool xx_pcommos2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcommos2 *archive = (xx_pcommos2 *)self;
    xx_pcommos2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_pcommos2_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_pcommos2_stream_free(stream);
    return true;
}

int64_t xx_pcommos2_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pcommos2_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_pcommos2 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_pcommos2_set_record(xx_archive_record *record,
                                 const xx_pcommos2_member *member) {
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

static bool xx_pcommos2_copy_options(xx_list_s *target,
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

static const xx_var *xx_pcommos2_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_pcommos2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_pcommos2_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_pcommos2_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_pcommos2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pcommos2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_pcommos2_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_pcommos2_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_pcommos2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pcommos2_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_pcommos2_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pcommos2_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_pcommos2_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pcommos2_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_pcommos2_stream *stream;
    const xx_pcommos2_member *member;
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
    stream = (xx_pcommos2_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_pcommos2_path_safe(member->name)) return false;

    path_option = xx_pcommos2_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_pcommos2_decode(self, member, &plain, &plain_size, pd);
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
        !xx_pcommos2_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_pcommos2_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
