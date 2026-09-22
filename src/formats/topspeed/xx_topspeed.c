/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TopSpeed (Clarion / JPI TopSpeed) installer members.
 *
 * THIS CONTAINER HAS NO SIGNATURE, NO HEADER AND NO DIRECTORY.  The whole
 * file is one member, and the only thing that distinguishes it from arbitrary
 * bytes is that the entire file parses as a chain of blocks:
 *
 *   +0x00  u16  additive checksum over the block payload
 *   +0x02  u16  plaintext size
 *   +0x04  u16  compressed size
 *   +0x06  min(plaintext, compressed) payload bytes
 *
 * A block whose compressed size is not smaller than its plaintext size is
 * STORED; otherwise the payload is a 12-bit LZW stream with a private
 * dictionary reset at every block.
 *
 * So validation IS the walk: xx_topspeed_scan_memory() verifies every block
 * header and every block checksum and requires the chain to land exactly on
 * the end of the file.  That is what this reader calls, and it is the reason
 * check_is_valid() is expensive here in a way it is not for a format with a
 * magic -- the whole file has to be read before anything can be claimed.  A
 * detector must therefore probe this format LAST, and only on a device, never
 * from a magic prefilter.
 *
 * The scan also produces the plaintext length, which the container does not
 * store anywhere; it is the sum of the per-block plaintext sizes.  No LZW
 * decoding happens during the scan.
 *
 * The member carries NO NAME.  The reference falls back to the archive's own
 * file name, which xx_io_device does not expose, so a fixed name is used
 * instead and the caller is expected to rename on extraction if it cares.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/topspeed/xx_topspeed.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/topspeed/xx_topspeed.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

typedef struct xx_topspeed_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_topspeed_member;

typedef struct xx_topspeed_stream_s {
    xx_topspeed_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_topspeed_stream;

static void xx_topspeed_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_topspeed_read_at(Abstractformat *self, int64_t offset,
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

/* Refuse anything that would escape the extraction directory. */
static bool xx_topspeed_path_safe(const char *name) {
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

static void xx_topspeed_stream_free(void *pointer) {
    xx_topspeed_stream *stream = (xx_topspeed_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_topspeed_add(xx_topspeed_stream *stream,
                            const xx_topspeed_member *member) {
    xx_topspeed_member *grown = (xx_topspeed_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* A block header plus a full payload; anything smaller cannot be one block. */
#define XX_TOPSPEED_MIN_SIZE 7
#define XX_TOPSPEED_HEADER_SIZE 6
/* The reference's ceiling on the WHOLE FILE. It is not a limit on how much a
 * member may decode to -- that is the separate output cap below -- but on how
 * much this reader is willing to pull into memory to answer "is this one?".
 * Without it every unrecognised file on a disk would be read end to end. */
#define XX_TOPSPEED_MAX_SIZE ((int64_t)256 * 1024 * 1024)
/* Bounds what a chain of block headers may ask an extraction to allocate. */
#define XX_TOPSPEED_MAX_OUTPUT ((int64_t)256 * 1024 * 1024)
/* The container stores no name; see the file comment. */
#define XX_TOPSPEED_MEMBER_NAME "topspeed"
/* One method, published so a listing has something to show. */
#define XX_TOPSPEED_METHOD_LZW 1U

/* Pull the whole span in and hand it to the codec's scanner, which is the
 * only validator this format has. The buffer is freed before returning: the
 * sizes are all a listing needs, and holding a quarter of a gigabyte for the
 * lifetime of the reader to save one re-read at extraction time is the wrong
 * trade. */
static bool xx_topspeed_scan(Abstractformat *self, int64_t span,
                             int64_t *uncompressed, xx_pd_struct *pd) {
    uint8_t *whole;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool ok;

    *uncompressed = 0;
    whole = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!whole) return false;
    if (!xx_topspeed_read_at(self, self->base_address, whole, (size_t)span)) {
        xx_mem_free(whole);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(whole);
        return false;
    }
    ok = xx_topspeed_scan_memory(whole, (size_t)span,
                                 (size_t)XX_TOPSPEED_MAX_OUTPUT, &consumed,
                                 &produced);
    xx_mem_free(whole);
    /* The scanner already requires the whole input to be consumed; the check
     * is repeated because a member that stops short would otherwise be
     * published with a plaintext length that covers only part of it. */
    if (!ok || consumed != (size_t)span) return false;
    if (produced > (size_t)XX_TOPSPEED_MAX_OUTPUT) return false;
    *uncompressed = (int64_t)produced;
    return true;
}

static xx_topspeed_stream *xx_topspeed_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_topspeed_stream *stream;
    xx_topspeed_member member;
    int64_t total;
    int64_t span;
    int64_t uncompressed = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TOPSPEED_MIN_SIZE || span > XX_TOPSPEED_MAX_SIZE) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_topspeed_scan(self, span, &uncompressed, pd)) return NULL;

    stream = (xx_topspeed_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup(XX_TOPSPEED_MEMBER_NAME);
    if (!member.name) goto fail;
    /* The first block header stands in for a container header so that a
     * region map has something to show; the stream deliberately starts at the
     * base address all the same, because the codec is handed the block chain
     * from its very first header. */
    member.header_offset = self->base_address;
    member.header_size = XX_TOPSPEED_HEADER_SIZE;
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = uncompressed;
    member.method = XX_TOPSPEED_METHOD_LZW;
    member.timestamp = 0U;
    member.is_folder = false;
    if (!xx_topspeed_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_topspeed_stream_free(stream);
    return NULL;
}

static bool xx_topspeed_decode(Abstractformat *self,
                               const xx_topspeed_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < XX_TOPSPEED_MIN_SIZE ||
        member->compressed_size > XX_TOPSPEED_MAX_SIZE) {
        return false;
    }
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_TOPSPEED_MAX_OUTPUT) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_topspeed_read_at(self, member->data_offset, input,
                             (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    /* The plaintext size is an input to the decoder, not a hint: it checks
     * the chain against it and refuses a mismatch. */
    ok = xx_topspeed_decode_memory(input, (size_t)member->compressed_size,
                                   output, (size_t)member->uncompressed_size,
                                   &written);
    xx_mem_free(input);
    if (!ok || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_topspeed_init(xx_topspeed *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TOPSPEED_FILE_TYPE_ID;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-topspeed");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_topspeed_check_is_valid;
    archive->format.handle_base_info = xx_topspeed_handle_base_info;
    archive->format.get_format_size = xx_topspeed_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_topspeed_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_topspeed_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_topspeed_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_topspeed_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_topspeed_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_topspeed_free_archive_records_reading;
    archive->format.destroy = xx_topspeed_vtable_destroy;
}

xx_topspeed *xx_topspeed_create(xx_io_device *device, int64_t base_address) {
    xx_topspeed *archive = (xx_topspeed *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_topspeed_init(archive, device, base_address);
    return archive;
}

void xx_topspeed_destroy(xx_topspeed *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->uncompressed_size = 0U;
}

void xx_topspeed_free(xx_topspeed *archive) {
    if (!archive) return;
    xx_topspeed_destroy(archive);
    xx_mem_free(archive);
}

static void xx_topspeed_vtable_destroy(Abstractformat *self) {
    xx_topspeed_destroy((xx_topspeed *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_topspeed_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_topspeed_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_topspeed_parse(self, pd);
    if (!stream) return false;
    xx_topspeed_stream_free(stream);
    return true;
}

bool xx_topspeed_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_topspeed *archive = (xx_topspeed *)self;
    xx_topspeed_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_topspeed_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->uncompressed_size =
        stream->count != 0U ? (uint64_t)stream->items[0].uncompressed_size : 0U;
    xx_topspeed_stream_free(stream);
    return true;
}

int64_t xx_topspeed_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_topspeed_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_topspeed *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_topspeed_set_record(xx_archive_record *record,
                                   const xx_topspeed_member *member) {
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

static bool xx_topspeed_copy_options(xx_list_s *target,
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

static const xx_var *xx_topspeed_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_topspeed_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_topspeed_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_topspeed_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_topspeed_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_topspeed_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_topspeed_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_topspeed_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_topspeed_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_topspeed_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_topspeed_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_topspeed_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_topspeed_set_record(&state->current_record,
                                               &stream->items[stream->index]);
    return state->has_record;
}

bool xx_topspeed_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_topspeed_stream *stream;
    const xx_topspeed_member *member;
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
    stream = (xx_topspeed_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_topspeed_path_safe(member->name)) return false;

    path_option =
        xx_topspeed_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_topspeed_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
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
        !xx_topspeed_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_topspeed_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
