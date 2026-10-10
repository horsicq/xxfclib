/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LZMA-alone streams (.lzma): the container written by the LZMA SDK's
 * `lzma e` / LzmaUtil, lzma-utils, `xz --format=lzma` and liblzma's
 * FORMAT_ALONE.
 *
 *   +0   properties byte  (pb * 5 + lp) * 9 + lc, below 225
 *   +1   u32 LE           dictionary size
 *   +5   u64 LE           uncompressed size; all ones = unknown
 *   +13  range-coded LZMA data
 *
 * With a known size the data normally stops after exactly that many output
 * bytes with no end marker (the SDK default); an end marker may still follow
 * it.  With an unknown size the end marker is what ends the data.  Either
 * way a properly flushed range coder leaves its code register at zero, which
 * is what makes the end of a stream, and so its exact compressed length,
 * checkable.  A stream that decodes but does not finish that way is refused.
 * A stream without an end marker must also carry a dictionary size an
 * encoder writes (7-Zip's and xz's test), and data consisting only of zero
 * bytes -- which ends cleanly at any declared size -- is only taken for the
 * tiny whole files encoders really write that way.
 *
 * 7-Zip decodes consecutive streams as one payload, and so does this reader:
 * after the first stream, another one is followed while its header passes
 * 7-Zip's own header test and it decodes to a clean finish (up to
 * XX_LZMA_MAX_STREAMS streams and XX_LZMA_MODEL_BUDGET of model setup).
 * Anything else after the last stream is reported as overlay.
 *
 * The stream decoder in algo/lzma provides known-size streams without an
 * end marker, exact compressed-byte counts, bounded memory and range-coder
 * completion checks. This layer parses the .lzma header and applies the
 * archive detection, concatenation and overlay policies.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzma/xx_lzma.h"
#include "../../algo/lzma/xx_lzma_stream_internal.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_LZMA_PAYLOAD_NAME "payload"

#define XX_LZMA_HEADER_SIZE 13U
#define XX_LZMA_RC_INIT_SIZE 5U
#define XX_LZMA_MIN_STREAM (XX_LZMA_HEADER_SIZE + XX_LZMA_RC_INIT_SIZE)
/* 7-Zip refuses a declared size of 2^56 or more; so does this reader. */
#define XX_LZMA_SIZE_LIMIT ((uint64_t)1 << 56)
#define XX_LZMA_MAX_STREAMS 65536U
/* Every stream starts a fresh probability model of up to 3 M entries
 * (lc + lp = 12).  Streams after the first are followed only while the
 * models set up so far stay below this many entries, so a file of many
 * tiny streams cannot turn model setup into minutes of work. */
#define XX_LZMA_MODEL_BUDGET ((uint64_t)64U * 1024U * 1024U)
/* Output decoded by check_is_valid before it takes a stream as plausible. */
#define XX_LZMA_PROBE_OUTPUT ((uint64_t)16U * 1024U)
#define XX_LZMA_ZERO_DATA_OUTPUT 2U

static void xx_lzma_vtable_destroy(Abstractformat *self);

/* The header grammar every stream must satisfy. */
static bool xx_lzma_parse_header(const uint8_t *data, xx_lzma_stream_props *header)
{
    unsigned value;
    if (!data || !header || data[0] >= 9U * 5U * 5U) return false;
    value = data[0];
    header->lc = value % 9U;
    value /= 9U;
    header->lp = value % 5U;
    header->pb = value / 5U;
    header->dictionary_size = xx_data_get_u32(data + 1U, 4, 0, false);
    header->declared_size = xx_data_get_u64(data + 5U, 8, 0, false);
    return header->declared_size == XX_LZMA_STREAM_UNKNOWN_SIZE || header->declared_size < XX_LZMA_SIZE_LIMIT;
}

/* 7-Zip's dictionary test (2^n or 3 * 2^n, 1, or all ones), plus the whole
 * MiB multiples the LZMA SDK encoder writes for dictionaries of 2 MiB and
 * more.  Only used to decide whether data after a stream opens another. */
static bool xx_lzma_dictionary_is_canonical(uint32_t size)
{
    unsigned shift;
    if (size == 1U || size == UINT32_C(0xFFFFFFFF)) return true;
    for (shift = 0U; shift <= 30U; ++shift) {
        if (size == (UINT32_C(2) << shift) || size == (UINT32_C(3) << shift)) {
            return true;
        }
    }
    return size >= (UINT32_C(1) << 21U) && (size & UINT32_C(0xFFFFF)) == 0U;
}

/* Cheap tests on the header and the first range-coder bytes that every
 * encoder satisfies: the coder's first byte is always zero, and when the
 * stream is known not to be empty its first symbol is a literal, which
 * keeps the top bit of the initial code clear.
 *
 * A declared size is also held against the data available for it.  Every
 * binary decision costs at least -log2(2017/2048) bits, and the cheapest
 * way to produce output is a 273-byte repeat taking 13 decisions, so one
 * data byte can never yield more than about 7630 output bytes;
 * XX_LZMA_MAX_RATIO leaves a margin over that.  (Real encoders stay below
 * 7000: 1 GiB of zeros packs to ~150 KiB.)  A header claiming more than
 * the rest of the file could hold is refused before any decoding. */
#define XX_LZMA_MAX_RATIO 8192U

static bool xx_lzma_stream_start_ok(const uint8_t *data, xx_lzma_stream_props *header, int64_t available)
{
    uint64_t data_size;
    if (!xx_lzma_parse_header(data, header) || data[XX_LZMA_HEADER_SIZE] != 0U || available < (int64_t)XX_LZMA_MIN_STREAM) {
        return false;
    }
    if (header->declared_size == XX_LZMA_STREAM_UNKNOWN_SIZE) return true;
    data_size = (uint64_t)available - (uint64_t)XX_LZMA_HEADER_SIZE;
    return header->declared_size / XX_LZMA_MAX_RATIO <= data_size && (header->declared_size == 0U || (data[XX_LZMA_HEADER_SIZE + 1U] & 0x80U) == 0U);
}

typedef struct xx_lzma_walk_s {
    uint64_t output_size;
    int64_t end;
    uint64_t streams;
} xx_lzma_walk_info;

/*
 * Walk the chain of streams starting at `start`.
 *   stop_at < 0:  measuring.  The first stream must decode to a clean
 *                 finish; further streams are followed while they pass the
 *                 7-Zip header test and decode cleanly; the walk ends
 *                 before the first thing that does not.
 *   stop_at >= 0: replaying a measured chain up to exactly stop_at.
 * probe_output > 0 (measuring only) accepts a first stream that is still
 * decoding cleanly after that much output, without finishing it.
 */
static bool xx_lzma_walk(xx_io_device *device, int64_t start, int64_t stop_at, xx_io_device *destination, uint64_t probe_output, xx_pd_struct *pd,
                         xx_lzma_walk_info *info)
{
    xx_lzma_stream_decoder *decoder;
    int64_t total_size;
    int64_t position = start;
    int64_t limit;
    uint64_t output_size = 0U;
    uint64_t streams = 0U;
    uint64_t model_work = 0U;
    bool ok = false;

    if (!device || !info || start < 0) return false;
    total_size = xx_io_total_size(device);
    limit = stop_at >= 0 ? stop_at : total_size;
    if (total_size < 0 || limit > total_size || limit < start || limit - start < (int64_t)XX_LZMA_MIN_STREAM) {
        return false;
    }
    decoder = xx_lzma_stream_decoder_create(device, destination, pd);
    if (!decoder) return false;

    while (streams < XX_LZMA_MAX_STREAMS) {
        uint8_t head[XX_LZMA_MIN_STREAM];
        xx_lzma_stream_props header;
        xx_lzma_stream_result result;
        xx_lzma_stream_info stream;
        int64_t next;
        bool first = streams == 0U;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (stop_at >= 0 && position == stop_at) break;
        if (limit - position < (int64_t)XX_LZMA_MIN_STREAM || !xx_lzma_stream_read_exact_at(device, position, head, sizeof(head), pd) ||
            !xx_lzma_stream_start_ok(head, &header, limit - position) ||
            (!first && stop_at < 0 && (!xx_lzma_dictionary_is_canonical(header.dictionary_size) || header.declared_size == 0U))) {
            if (first || stop_at >= 0) goto done;
            break;
        }
        {
            uint64_t model = (uint64_t)xx_lzma_stream_model_entries(&header);
            if (!first && stop_at < 0 && model > XX_LZMA_MODEL_BUDGET - model_work) {
                break;
            }
            model_work += model;
        }
        result = xx_lzma_stream_decode(decoder, &header, position + (int64_t)XX_LZMA_HEADER_SIZE, limit, first && stop_at < 0 ? probe_output : 0U, &stream);
        if (result == XX_LZMA_STREAM_PARTIAL) {
            /* Probing: the first stream looks right so far. */
            info->output_size = stream.produced;
            info->end = -1;
            info->streams = 1U;
            ok = true;
            goto done;
        }
        if (result != XX_LZMA_STREAM_FINISHED || stream.consumed > (uint64_t)(limit - position) - (uint64_t)XX_LZMA_HEADER_SIZE ||
            stream.produced > UINT64_MAX - output_size) {
            /* Running out of memory or being stopped says nothing about
             * where the streams end: fail rather than measure short. */
            if (first || stop_at >= 0 || stream.allocation_failed || (pd && xx_pd_is_stopped(pd))) {
                goto done;
            }
            break;
        }
        next = position + (int64_t)XX_LZMA_HEADER_SIZE + (int64_t)stream.consumed;
        /* A stream that stopped at its declared size without an end marker
         * is vouched for only by the coder's final state.  Ask of its
         * header what 7-Zip and xz ask before they take a file as .lzma,
         * and believe all-zero data (which ends cleanly at any size) only
         * where encoders really write it: a whole file of at most two
         * bytes, or an empty one.  Eighteen zero bytes are not a file. */
        if (stop_at < 0 && !stream.end_marker &&
            (!xx_lzma_dictionary_is_canonical(header.dictionary_size) ||
             (stream.zero_data && (!first || stream.produced > XX_LZMA_ZERO_DATA_OUTPUT || next != total_size)))) {
            if (first) goto done;
            break;
        }
        position = next;
        output_size += stream.produced;
        ++streams;
        /* A probe only vouches for the first stream. */
        if (probe_output != 0U) break;
    }
    if (streams == 0U || (stop_at >= 0 && position != stop_at) || (pd && xx_pd_is_stopped(pd))) {
        goto done;
    }
    info->output_size = output_size;
    info->end = position;
    info->streams = streams;
    ok = true;
done:
    xx_lzma_stream_decoder_free(decoder);
    return ok;
}

/* ------------------------------------------------------------ the reader */

static bool xx_lzma_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_lzma_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_lzma_populate_record(Abstractformat *self, xx_archive_record *record)
{
    const xx_lzma *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid || self->format_size < (int64_t)XX_LZMA_MIN_STREAM) {
        return false;
    }
    archive = (const xx_lzma *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_LZMA_HEADER_SIZE;
    record->data_offset = self->base_address + (int64_t)XX_LZMA_HEADER_SIZE;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME, XX_LZMA_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

void xx_lzma_init(xx_lzma *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LZMA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzma");
    xx_format_set_extension(&archive->format, "lzma");
    archive->format.check_is_valid = xx_lzma_check_is_valid;
    archive->format.handle_base_info = xx_lzma_handle_base_info;
    archive->format.get_format_size = xx_lzma_get_format_size;
    archive->format.get_number_of_archive_records = xx_lzma_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_lzma_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_lzma_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_lzma_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_lzma_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_lzma_free_archive_records_reading;
    archive->format.destroy = xx_lzma_vtable_destroy;
    archive->stream_end = -1;
}

xx_lzma *xx_lzma_create(xx_io_device *device, int64_t base_address)
{
    xx_lzma *archive = (xx_lzma *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lzma_init(archive, device, base_address);
    return archive;
}

void xx_lzma_destroy(xx_lzma *archive)
{
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
}

static void xx_lzma_vtable_destroy(Abstractformat *self)
{
    xx_lzma_destroy((xx_lzma *)self);
}

void xx_lzma_free(xx_lzma *archive)
{
    if (!archive) return;
    xx_lzma_destroy(archive);
    xx_mem_free(archive);
}

/* Header tests plus a trial decode of the first XX_LZMA_PROBE_OUTPUT bytes
 * (or the whole first stream, if it is shorter). */
bool xx_lzma_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t head[XX_LZMA_MIN_STREAM];
    xx_lzma_stream_props header;
    xx_lzma_walk_info info;
    int64_t total_size;
    if (!self || !self->device || self->base_address < 0) return false;
    if (self->base_info_handled) return self->is_valid;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < (int64_t)XX_LZMA_MIN_STREAM ||
        !xx_lzma_stream_read_exact_at(self->device, self->base_address, head, sizeof(head), pd) ||
        !xx_lzma_stream_start_ok(head, &header, total_size - self->base_address)) {
        return false;
    }
    return xx_lzma_walk(self->device, self->base_address, -1, NULL, XX_LZMA_PROBE_OUTPUT, pd, &info);
}

bool xx_lzma_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_lzma_walk_info info;
    int64_t total_size;
    xx_lzma *archive = (xx_lzma *)self;
    if (!self) return false;
    if (!self->device || self->base_address < 0 || !xx_lzma_walk(self->device, self->base_address, -1, NULL, 0U, pd, &info) || info.end <= self->base_address) {
        archive->uncompressed_size = 0U;
        archive->stream_end = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    total_size = xx_io_total_size(self->device);
    archive->uncompressed_size = info.output_size;
    archive->stream_end = info.end;
    self->format_size = info.end - self->base_address;
    self->number_of_archive_records = 1U;
    self->overlay_offset = info.end < total_size ? info.end : -1;
    self->overlay_size = info.end < total_size ? total_size - info.end : 0;
    self->file_type = XX_FILE_TYPE_LZMA;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_lzma_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_lzma_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_lzma_unpack_to_device(xx_lzma *archive, xx_io_device *destination, xx_pd_struct *pd)
{
    xx_lzma_walk_info info;
    if (!archive || !destination || (!archive->format.base_info_handled && !xx_format_handle_base_info(&archive->format, pd)) || !archive->format.is_valid ||
        archive->stream_end < 0) {
        return false;
    }
    return xx_lzma_walk(archive->format.device, archive->format.base_address, archive->stream_end, destination, 0U, pd, &info) && info.end == archive->stream_end &&
           info.output_size == archive->uncompressed_size;
}

xx_archive_record_state *xx_lzma_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) || !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_lzma_copy_options(&state->options, options) || !xx_lzma_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_lzma_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_lzma_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_lzma_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_lzma *archive = (xx_lzma *)self;
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value = xx_lzma_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        xx_lzma_walk_info info;
        return archive->stream_end >= 0 && xx_lzma_walk(self->device, self->base_address, archive->stream_end, NULL, 0U, pd, &info) &&
               info.output_size == archive->uncompressed_size;
    }
    if (path_value->type == XX_VAR_TYPE_STRING || path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING || path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/", XX_LZMA_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_LZMA_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_lzma_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_lzma_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_lzma_get_uncompressed_size(const xx_lzma *archive)
{
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_lzma_get_stream_end(const xx_lzma *archive)
{
    return archive ? archive->stream_end : -1;
}
