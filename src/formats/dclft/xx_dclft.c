/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Raw PKWARE Data Compression Library streams -- "implode"/"explode", the
 * standalone format PKWARE shipped as a linkable library and which dozens of
 * DOS-era tools embedded. This is the naked stream with no container at all:
 *
 *   0x00  u8  literal mode: 0 = fixed literals, 1 = coded literals
 *   0x01  u8  dictionary size selector: 4 (1 KiB), 5 (2 KiB) or 6 (4 KiB)
 *   0x02  n   the bit stream, ending in the 0x0106 end-of-stream code
 *
 * There is no magic, no name, no stored length and no checksum. Two bytes
 * whose values are drawn from {0,1} and {4,5,6} are six bits of evidence,
 * which is nothing: they match one file in a hundred by chance. So the
 * prelude is only a cheap pre-filter here, and VALIDATION IS THE DECODE --
 * the stream must walk to its end-of-stream code and land exactly on the end
 * of the file. That is a strong test precisely because the decoder's input
 * position at the end marker is not something an unrelated file can hit.
 *
 * The decoder is the library's own xx_dcl_scan_memory()/xx_dcl_decode_memory()
 * pair; scan measures the plaintext length that this container does not
 * store, and reports how much input the stream occupied.
 *
 * Because the format has no identity of its own, this reader must be
 * dispatched late -- after every format that does have a magic has had its
 * turn. A file that is really a DCL-compressed member of some container
 * should be recognised as that container, not as a bare stream.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dclft/xx_dclft.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef DCLFT
#define XX_DCLFT_FILE_TYPE XX_FILE_TYPE_DCLFT
#else
#define XX_DCLFT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Prelude plus at least one byte of the end-of-stream code. */
#define XX_DCLFT_MIN_SIZE 3
#define XX_DCLFT_MAX_LITERAL_MODE 1U
#define XX_DCLFT_MIN_DICT_BITS 4U
#define XX_DCLFT_MAX_DICT_BITS 6U
#define XX_DCLFT_MAX_INPUT ((int64_t)256 * 1024 * 1024)
/* Decompression-bomb guard. DCL tops out near 259 plaintext bytes per coded
 * byte; this ceiling is on the plaintext itself, so a few hundred bytes of
 * input can still never drive a gigabyte allocation. */
#define XX_DCLFT_MAX_OUTPUT ((int64_t)512 * 1024 * 1024)
#define XX_DCLFT_MAX_MEMBERS 1
#define XX_DCLFT_METHOD_DCL 1U
/* The stream stores no name, and this reader cannot see the container's own
 * file name. Deliberately extension-less: inventing one would be a claim
 * about content the stream never makes. */
#define XX_DCLFT_PLACEHOLDER_NAME "dcl_data"

typedef struct xx_dclft_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    bool is_folder;
} xx_dclft_member;

typedef struct xx_dclft_stream_s {
    xx_dclft_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_dclft_stream;

static void xx_dclft_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_dclft_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_dclft_path_safe(const char *name) {
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

static void xx_dclft_stream_free(void *pointer) {
    xx_dclft_stream *stream = (xx_dclft_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_dclft_add(xx_dclft_stream *stream,
                         const xx_dclft_member *member) {
    xx_dclft_member *grown = (xx_dclft_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* ------------------------------------------------------------- parsing -- */

static xx_dclft_stream *xx_dclft_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_dclft_stream *stream;
    xx_dclft_member member;
    uint8_t *payload = NULL;
    char *name = NULL;
    int64_t total, span;
    size_t consumed = 0U;
    size_t produced = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_DCLFT_MIN_SIZE || span > XX_DCLFT_MAX_INPUT) return NULL;

    payload = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!payload) return NULL;
    if (!xx_dclft_read_at(self, self->base_address, payload, (size_t)span)) {
        goto fail;
    }
    /* Cheap prelude check first, so a stray file costs two byte compares
     * rather than a decode. */
    if (payload[0] > XX_DCLFT_MAX_LITERAL_MODE ||
        payload[1] < XX_DCLFT_MIN_DICT_BITS ||
        payload[1] > XX_DCLFT_MAX_DICT_BITS) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* THE test. A stream that does not reach its end-of-stream code, or that
     * reaches it before the end of the file, is not a bare DCL file: it is
     * either something else entirely or a DCL member inside a container that
     * some other reader owns. */
    if (!xx_dcl_scan_memory(payload, (size_t)span,
                            (size_t)XX_DCLFT_MAX_OUTPUT, &consumed,
                            &produced)) {
        goto fail;
    }
    if ((int64_t)consumed != span) goto fail;
    /* A zero-length plaintext would make extraction write an empty file and
     * call it success. */
    if (produced == 0U || (int64_t)produced > XX_DCLFT_MAX_OUTPUT) goto fail;
    xx_mem_free(payload);
    payload = NULL;

    stream = (xx_dclft_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_DCLFT_PLACEHOLDER_NAME);
    if (!name || !xx_dclft_path_safe(name)) goto fail_stream;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = 2;
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_DCLFT_METHOD_DCL;
    member.is_folder = false;
    if (!xx_dclft_add(stream, &member)) goto fail_stream;
    name = NULL;
    if (stream->count != (size_t)XX_DCLFT_MAX_MEMBERS) goto fail_stream;
    stream->archive_size = span;
    return stream;

fail_stream:
    xx_str_free(name);
    xx_dclft_stream_free(stream);
    return NULL;
fail:
    xx_mem_free(payload);
    return NULL;
}

/* The whole file is one DCL stream whose plaintext length parse has already
 * measured with a trial scan, so the allocation below is bounded by a length
 * the decoder itself produced rather than by anything a header claims. */
static bool xx_dclft_decode(Abstractformat *self,
                            const xx_dclft_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_DCLFT_METHOD_DCL) return false;
    if (member->compressed_size < XX_DCLFT_MIN_SIZE ||
        member->compressed_size > XX_DCLFT_MAX_INPUT) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_DCLFT_MAX_OUTPUT) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_dclft_read_at(self, member->data_offset, input,
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
    /* Exactly the measured plaintext length or nothing: with no checksum
     * anywhere, this equality is the whole of extraction's correctness
     * check. */
    if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_dclft_init(xx_dclft *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DCLFT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pkware-dcl");
    xx_format_set_extension(&archive->format, "dcl");
    archive->format.check_is_valid = xx_dclft_check_is_valid;
    archive->format.handle_base_info = xx_dclft_handle_base_info;
    archive->format.get_format_size = xx_dclft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dclft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dclft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dclft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dclft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dclft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dclft_free_archive_records_reading;
    archive->format.destroy = xx_dclft_vtable_destroy;
}

xx_dclft *xx_dclft_create(xx_io_device *device, int64_t base_address) {
    xx_dclft *archive = (xx_dclft *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_dclft_init(archive, device, base_address);
    return archive;
}

void xx_dclft_destroy(xx_dclft *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_dclft_free(xx_dclft *archive) {
    if (!archive) return;
    xx_dclft_destroy(archive);
    xx_mem_free(archive);
}

static void xx_dclft_vtable_destroy(Abstractformat *self) {
    xx_dclft_destroy((xx_dclft *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_dclft_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dclft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_dclft_parse(self, pd);
    if (!stream) return false;
    xx_dclft_stream_free(stream);
    return true;
}

bool xx_dclft_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dclft *archive = (xx_dclft *)self;
    xx_dclft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_dclft_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_dclft_stream_free(stream);
    return true;
}

int64_t xx_dclft_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_dclft_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_dclft *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_dclft_set_record(xx_archive_record *record,
                                const xx_dclft_member *member) {
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

static bool xx_dclft_copy_options(xx_list_s *target,
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

static const xx_var *xx_dclft_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_dclft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_dclft_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_dclft_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_dclft_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_dclft_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_dclft_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_dclft_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_dclft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dclft_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_dclft_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dclft_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_dclft_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_dclft_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_dclft_stream *stream;
    const xx_dclft_member *member;
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
    stream = (xx_dclft_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_dclft_path_safe(member->name)) return false;

    path_option =
        xx_dclft_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_dclft_decode(self, member, &plain, &plain_size, pd);
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
        !xx_dclft_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_dclft_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
