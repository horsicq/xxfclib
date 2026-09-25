/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Symantec NPack containers.
 *
 *   header, 5 bytes at offset 0:
 *     0x00  5 bytes  magic, "MSTSM"
 *
 *   payload: everything from offset 5 to end of file; one Stac LZS block,
 *     an MSB-first bit stream over a 2048-byte zero-filled history window.
 *
 * That is the entire container. There is no member count, no name, no size
 * field, no timestamp and no method byte - five ASCII bytes and a bit
 * stream. Detection therefore cannot lean on the header at all: the only
 * real evidence is that the bit stream walks cleanly to its stop code and
 * that the stop code falls inside the last byte of the file.
 *
 * Because no plaintext length is stored, parse measures the block with
 * xx_npack_scan_memory() and publishes the measured value. That scan runs
 * the identical core routine as the decoder, so the measure and the later
 * decode can never disagree.
 *
 * The container keeps no name of its own: the installer encodes it by
 * replacing the last character of the original file name with '$', which is
 * why the canonical extension is "$". The single member is listed as
 * "npack_data", matching the reference extractor's fallback.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/npack/xx_npack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/npack/xx_npack.h"

#include <stdio.h>

#define XX_NPACK_COPY_CHUNK (64 * 1024)

typedef struct xx_npack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_npack_member;

typedef struct xx_npack_stream_s {
    xx_npack_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_npack_stream;

static void xx_npack_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_npack_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_npack_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_npack_path_safe(const char *name) {
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

static void xx_npack_stream_free(void *pointer) {
    xx_npack_stream *stream = (xx_npack_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_npack_add(xx_npack_stream *stream,
                          const xx_npack_member *member) {
    xx_npack_member *grown = (xx_npack_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_NPACK_MAX_MEMBERS 1
#define XX_NPACK_MIN_PAYLOAD 2
#define XX_NPACK_MAGIC_SIZE 5
#define XX_NPACK_METHOD_LZS 0U
#define XX_NPACK_MAX_COMPRESSED ((int64_t)0x4000000)
#define XX_NPACK_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static xx_npack_stream *xx_npack_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_npack_decode(Abstractformat *self, const xx_npack_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The container holds exactly one block; the cap exists only so the shared
 * shape of these readers is preserved. */

/* The shortest legal block is a bare stop code, one byte. A two-byte floor
 * keeps a 7-byte "MSTSM" + junk blob from probing as a container: with one
 * payload byte far too many random values walk to a stop code immediately. */

static xx_npack_stream *xx_npack_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    static const uint8_t magic[XX_NPACK_MAGIC_SIZE] = {
        (uint8_t)'M', (uint8_t)'S', (uint8_t)'T', (uint8_t)'S', (uint8_t)'M'};
    xx_npack_stream *stream = NULL;
    xx_npack_member member;
    uint8_t header[XX_NPACK_MAGIC_SIZE];
    uint8_t *payload = NULL;
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t payload_size;
    size_t consumed = 0U;
    size_t produced = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_NPACK_MAGIC_SIZE + XX_NPACK_MIN_PAYLOAD) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_npack_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    payload_size = span - XX_NPACK_MAGIC_SIZE;
    /* The block must be measured before a member can be published, and
     * measuring means buffering the payload. The reference falls back to a
     * prefix-only probe above 8 MiB, but that path yields no plaintext
     * length, and a member without one cannot be listed - so an oversized
     * container is refused outright rather than listed with a guessed size. */
    if (payload_size > XX_NPACK_MAX_COMPRESSED) return NULL;

    payload = (uint8_t *)xx_mem_alloc((size_t)payload_size);
    if (!payload) return NULL;
    if (!xx_npack_read_at(self, self->base_address + XX_NPACK_MAGIC_SIZE,
                          payload, (size_t)payload_size)) {
        xx_mem_free(payload);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(payload);
        return NULL;
    }

    /* This walk is the whole of the format's identity. Five ASCII bytes match
     * far too much; what rejects a false positive is that the bit stream
     * parses to its stop code AND that the stop code lands inside the final
     * byte of the file. Both halves matter: dropping the stop-code
     * requirement accepts any prefix, and dropping the "consumed == payload"
     * requirement accepts any file that merely BEGINS with a valid block. */
    if (!xx_npack_scan_memory(payload, (size_t)payload_size,
                              (size_t)XX_NPACK_MAX_DECODED, &consumed,
                              &produced)) {
        xx_mem_free(payload);
        return NULL;
    }
    xx_mem_free(payload);
    payload = NULL;
    /* consumed is rounded up to a byte boundary, so equality here is exactly
     * "the block ends in the last byte of the container". */
    if ((int64_t)consumed != payload_size) return NULL;
    /* A block that decodes to nothing is a stop code and nothing else. */
    if (produced == 0U || (int64_t)produced > XX_NPACK_MAX_DECODED) {
        return NULL;
    }

    stream = (xx_npack_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The container stores no name at all. The reference extractor reuses the
     * container's own file name; that is not available here, so its fallback
     * name is used unconditionally. */
    name = xx_str_dup("npack_data");
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_NPACK_MAGIC_SIZE;
    member.data_offset = self->base_address + XX_NPACK_MAGIC_SIZE;
    member.compressed_size = payload_size;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_NPACK_METHOD_LZS;
    /* No timestamp anywhere in the container. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_npack_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_npack_stream_free(stream);
    return NULL;
}



/* The container has no method field: the payload is always Stac LZS. Zero is
 * the only value parse publishes, and decode refuses anything else so that a
 * future method cannot be silently decoded as this one. */

/* The whole payload is buffered twice - once to measure, once to decode - so
 * the packed ceiling bounds a real allocation, not just a size field. The
 * largest known sample is 380 KB. */

/* One LZS block covering the whole payload, whose decoded length parse
 * measured. The decoder is output-driven and additionally requires the block
 * to reach its stop code, so a stream that would decode short is reported as
 * a failure rather than as a partially filled buffer. */
static bool xx_npack_decode(Abstractformat *self,
                            const xx_npack_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_NPACK_METHOD_LZS) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_NPACK_MAX_COMPRESSED ||
        member->uncompressed_size > XX_NPACK_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    /* data_offset already points past the magic; the codec is documented to
     * take the payload only. */
    if (!xx_npack_read_at(self, member->data_offset, input,
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
    if (!xx_npack_decode_memory(input, (size_t)member->compressed_size,
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

void xx_npack_init(xx_npack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_NPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-npack");
    xx_format_set_extension(&archive->format, "$");
    archive->format.check_is_valid = xx_npack_check_is_valid;
    archive->format.handle_base_info = xx_npack_handle_base_info;
    archive->format.get_format_size = xx_npack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_npack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_npack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_npack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_npack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_npack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_npack_free_archive_records_reading;
    archive->format.destroy = xx_npack_vtable_destroy;
}

xx_npack *xx_npack_create(xx_io_device *device, int64_t base_address) {
    xx_npack *archive = (xx_npack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_npack_init(archive, device, base_address);
    return archive;
}

void xx_npack_destroy(xx_npack *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_npack_free(xx_npack *archive) {
    if (!archive) return;
    xx_npack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_npack_vtable_destroy(Abstractformat *self) {
    xx_npack_destroy((xx_npack *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_npack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_npack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_npack_parse(self, pd);
    if (!stream) return false;
    xx_npack_stream_free(stream);
    return true;
}

bool xx_npack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_npack *archive = (xx_npack *)self;
    xx_npack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_npack_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_npack_stream_free(stream);
    return true;
}

int64_t xx_npack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_npack_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_npack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_npack_set_record(xx_archive_record *record,
                                 const xx_npack_member *member) {
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

static bool xx_npack_copy_options(xx_list_s *target,
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

static const xx_var *xx_npack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_npack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_npack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_npack_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_npack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_npack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_npack_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_npack_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_npack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_npack_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_npack_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_npack_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_npack_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_npack_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_npack_stream *stream;
    const xx_npack_member *member;
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
    stream = (xx_npack_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_npack_path_safe(member->name)) return false;

    path_option = xx_npack_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_npack_decode(self, member, &plain, &plain_size, pd);
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
        !xx_npack_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_npack_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
