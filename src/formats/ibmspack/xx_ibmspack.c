/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM install-diskette packed files (the ".??#" members of IBM's DOS and
 * OS/2 installation diskettes).
 *
 *   0x00  u8  0x53 ('S'), the FLS-LZ stream tag
 *   0x01  ... the rest of the adaptive-phrase stream, to end-of-file
 *
 * That is the entire format. There is no header, no member table, no name,
 * no timestamp and - the part that shapes this reader - NO UNCOMPRESSED SIZE
 * FIELD anywhere. The 'S' byte is part of the codec stream, not a container
 * header, so the member extent is the whole file starting at offset 0.
 *
 * Because there is no size field, the decoded length has to be measured.
 * The reference reader does that by bisecting on a declared size, running
 * the codec up to ~96 times over a tail-exact device view. This reader does
 * not: xx_flslz_scan_memory() runs the same decoder once with the output
 * discarded and reports both the bytes the stream occupies and the bytes it
 * produces. One pass replaces the search, and the answer is exact rather
 * than bracketed.
 *
 * One byte of tag is not a magic number, so detection IS the trial decode.
 * The stream must reach its end code and must occupy the file exactly: the
 * scan's consumed count has to equal the file size, not merely approach it.
 *
 * The original file name is not recoverable. The packer replaced the last
 * character of the source extension with '#' and stored nothing, so members
 * are published under a fixed placeholder name.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ibmspack/xx_ibmspack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/flslz/xx_flslz.h"

#include <stdio.h>

#define XX_IBMSPACK_COPY_CHUNK (64 * 1024)

typedef struct xx_ibmspack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ibmspack_member;

typedef struct xx_ibmspack_stream_s {
    xx_ibmspack_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ibmspack_stream;

static void xx_ibmspack_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ibmspack_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ibmspack_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ibmspack_path_safe(const char *name) {
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

static void xx_ibmspack_stream_free(void *pointer) {
    xx_ibmspack_stream *stream = (xx_ibmspack_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ibmspack_add(xx_ibmspack_stream *stream,
                          const xx_ibmspack_member *member) {
    xx_ibmspack_member *grown = (xx_ibmspack_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_IBMSPACK_STREAM_TAG 0x53U
#define XX_IBMSPACK_MIN_SIZE 4
#define XX_IBMSPACK_MAX_MEMBERS 1
#define XX_IBMSPACK_MAX_INPUT ((int64_t)64 * 1024 * 1024)
#define XX_IBMSPACK_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_IBMSPACK_MAX_EXPANSION 250
#define XX_IBMSPACK_FALLBACK_NAME "ibm_spack.bin"
#define XX_IBMSPACK_METHOD_FLSLZ 0U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static xx_ibmspack_stream *xx_ibmspack_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ibmspack_decode(Abstractformat *self, const xx_ibmspack_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


static xx_ibmspack_stream *xx_ibmspack_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_ibmspack_stream *stream;
    xx_ibmspack_member member;
    uint8_t *input;
    char *name;
    int64_t total;
    int64_t span;
    int64_t ceiling;
    size_t consumed = 0U;
    size_t produced = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_IBMSPACK_MIN_SIZE || span > XX_IBMSPACK_MAX_INPUT) {
        return NULL;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!input) return NULL;
    if (!xx_ibmspack_read_at(self, self->base_address, input, (size_t)span)) {
        xx_mem_free(input);
        return NULL;
    }
    /* One byte, and a common one at that: this rejects most files for free
     * but is emphatically not a magic number. The trial decode below is the
     * real test. */
    if (input[0] != XX_IBMSPACK_STREAM_TAG) {
        xx_mem_free(input);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return NULL;
    }

    /* Bound the measuring pass by what this input could possibly expand to,
     * so a tiny hostile file cannot buy a 256 MiB decode. */
    ceiling = XX_IBMSPACK_MAX_DECODED;
    if (span < ceiling / XX_IBMSPACK_MAX_EXPANSION) {
        ceiling = span * XX_IBMSPACK_MAX_EXPANSION;
    }

    /* THE false-positive defence. The format has no magic, no length and no
     * checksum, so the only evidence that this file is an FLS-LZ stream is
     * that the decoder reaches its end code AND that the stream occupies the
     * file exactly. xx_flslz_scan_memory deliberately allows the stream to
     * end early, which is why the consumed count is compared here: accepting
     * a stream that ends before EOF would make this reader claim any file
     * that happens to start with 'S' and contain a decodable prefix. Do not
     * loosen this to "consumed <= span". */
    if (!xx_flslz_scan_memory(input, (size_t)span, (size_t)ceiling, &consumed,
                              &produced) ||
        consumed != (size_t)span || produced == 0U) {
        xx_mem_free(input);
        return NULL;
    }
    xx_mem_free(input);
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if ((int64_t)produced > ceiling) return NULL;

    stream = (xx_ibmspack_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_IBMSPACK_FALLBACK_NAME);
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    /* No header region: the single 'S' byte belongs to the codec stream, so
     * the member's data starts at offset 0 and a header record would cover
     * the same byte twice. */
    member.header_offset = self->base_address;
    member.header_size = 0;
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_IBMSPACK_METHOD_FLSLZ;
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_ibmspack_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_ibmspack_stream_free(stream);
    return NULL;
}


/* The tag is part of the codec stream; the decoder reads it itself. */

/* The tag plus a class header plus an end code cannot fit in fewer bytes. */

/* This container has exactly one member and no member table at all; the cap
 * exists only so the shape matches every other reader in this tree. */

/* Detection is a full decode, so the input has to be bounded or a large file
 * turns probing into an unbounded amount of work. The reference corpus tops
 * out at 448310 bytes packed and 2154496 unpacked. */

/* No code in this codec is shorter than eight bits and no dictionary phrase
 * is longer than 250 bytes, so a stream can never expand by more than this.
 * Applying it keeps a crafted four-byte file from costing a full
 * XX_IBMSPACK_MAX_DECODED measuring pass. */

/* The packer overwrote the last extension character with '#' and stored
 * nothing else, so no truthful name can be recovered from the container. */


static bool xx_ibmspack_decode(Abstractformat *self,
                               const xx_ibmspack_member *member,
                               uint8_t **out, size_t *out_size,
                               xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The format defines exactly one method. Anything else reaching here
     * would mean the parse published something this decode does not
     * understand, and treating it as stored would emit garbage. */
    if (member->method != XX_IBMSPACK_METHOD_FLSLZ) return false;
    if (member->compressed_size < XX_IBMSPACK_MIN_SIZE ||
        member->compressed_size > XX_IBMSPACK_MAX_INPUT) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_IBMSPACK_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_ibmspack_read_at(self, member->data_offset, input,
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
    /* The size the parse published came from scanning this very stream, so
     * the decode must reproduce it byte for byte; xx_flslz_decode_memory
     * additionally insists the whole extent be consumed. */
    if (!xx_flslz_decode_memory(input, (size_t)member->compressed_size, output,
                                (size_t)member->uncompressed_size, &written) ||
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

void xx_ibmspack_init(xx_ibmspack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_IBMSPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ibm-spack");
    xx_format_set_extension(&archive->format, "#");
    archive->format.check_is_valid = xx_ibmspack_check_is_valid;
    archive->format.handle_base_info = xx_ibmspack_handle_base_info;
    archive->format.get_format_size = xx_ibmspack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ibmspack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ibmspack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ibmspack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ibmspack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ibmspack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ibmspack_free_archive_records_reading;
    archive->format.destroy = xx_ibmspack_vtable_destroy;
}

xx_ibmspack *xx_ibmspack_create(xx_io_device *device, int64_t base_address) {
    xx_ibmspack *archive = (xx_ibmspack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ibmspack_init(archive, device, base_address);
    return archive;
}

void xx_ibmspack_destroy(xx_ibmspack *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ibmspack_free(xx_ibmspack *archive) {
    if (!archive) return;
    xx_ibmspack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ibmspack_vtable_destroy(Abstractformat *self) {
    xx_ibmspack_destroy((xx_ibmspack *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ibmspack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ibmspack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ibmspack_parse(self, pd);
    if (!stream) return false;
    xx_ibmspack_stream_free(stream);
    return true;
}

bool xx_ibmspack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ibmspack *archive = (xx_ibmspack *)self;
    xx_ibmspack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ibmspack_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ibmspack_stream_free(stream);
    return true;
}

int64_t xx_ibmspack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ibmspack_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ibmspack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ibmspack_set_record(xx_archive_record *record,
                                 const xx_ibmspack_member *member) {
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

static bool xx_ibmspack_copy_options(xx_list_s *target,
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

static const xx_var *xx_ibmspack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ibmspack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ibmspack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ibmspack_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ibmspack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ibmspack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ibmspack_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ibmspack_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ibmspack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ibmspack_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ibmspack_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ibmspack_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ibmspack_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ibmspack_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ibmspack_stream *stream;
    const xx_ibmspack_member *member;
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
    stream = (xx_ibmspack_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ibmspack_path_safe(member->name)) return false;

    path_option = xx_ibmspack_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ibmspack_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ibmspack_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ibmspack_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
