/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CauseWay Compressor (CWC) data files, the output of CWC.EXE -d from
 * Michael Devore's CauseWay DOS extender toolchain, as shipped with the
 * assets of Chronicles of the Sword (1996).  One payload per file:
 *
 *   header, 12 bytes at offset 0, little endian:
 *     0x00  3  "CWC"
 *     0x03  1  u8   position width in bits (12 in every known file)
 *     0x04  4  u32  unpacked size
 *     0x08  4  u32  packed size (file size minus 12)
 *     0x0C  n  the stream
 *
 * The stream interleaves 32-bit little-endian control words, consumed from
 * bit 31 down, with byte-aligned operands.  The first control word is the
 * first four payload bytes; the next one is loaded the moment the previous
 * one is used up.  Token prefixes:
 *
 *   1        literal byte
 *   01 ll    u8 position p; length L = ll + 2.  p == 0: u8 value, a run of
 *            L + 1 copies.  Otherwise a copy of L bytes from distance
 *            (p - 1) + L.
 *   001      u16 word: L = (w & 15) + 2, p = w >> 4; same rules.
 *   000      u24 word: L = (w & mask) + 2, p = w >> bits (mask = 2^bits - 1).
 *            p > 0: copy as above.  p == 0: L >= 18 a run of L + 1 copies of
 *            the next byte; L == 2 end of stream; L == 3 and 4 are no-ops
 *            (the DOS loader's refill and flush markers); L == 5 a literal
 *            string of n bytes where n is the next byte.
 *
 * Copies may overlap their own output (that is how short repeats are
 * coded).  The format has no checksum: the gate is that the trial walk
 * reproduces exactly the declared unpacked size.
 *
 * The grammar was recovered from the CauseWay sources (cwc.asm, decstub.asm)
 * and verified over 2,520 files, every one reproducing its declared length.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/cwc/xx_cwc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_CWC_FILE_TYPE XX_FILE_TYPE_CWC

#define XX_CWC_HEADER_SIZE 12
/* One control word plus the three bytes of an end-of-stream token. */
#define XX_CWC_MIN_PACKED_SIZE 7
#define XX_CWC_MIN_BITS 9U
#define XX_CWC_MAX_BITS 16U
#define XX_CWC_MAX_MEMBERS 1
#define XX_CWC_METHOD_LZ 1U
#define XX_CWC_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* The densest token is a long run: three control bits, a three byte operand
 * and one value byte for up to 4098 plaintext bytes, a hair over 1024x. */
#define XX_CWC_MAX_RATIO 1100
#define XX_CWC_RATIO_SLACK 64
#define XX_CWC_PLACEHOLDER_NAME "cwc_data"

typedef struct xx_cwc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    unsigned bits;
} xx_cwc_member;

typedef struct xx_cwc_stream_s {
    xx_cwc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_cwc_stream;

static void xx_cwc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_cwc_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 || xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received = xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_cwc_path_safe(const char *name)
{
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

static void xx_cwc_stream_free(void *pointer)
{
    xx_cwc_stream *stream = (xx_cwc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- codec -- */

typedef struct xx_cwc_cursor_s {
    const uint8_t *input;
    size_t size;
    size_t position;
    uint32_t control;
    unsigned left;
} xx_cwc_cursor;

/* The next control bit.  A reload that finds fewer than four bytes yields an
 * all-zero word without moving, which is what the original decoder does; a
 * stream that then needs operands it does not have fails on those. */
static unsigned xx_cwc_bit(xx_cwc_cursor *c)
{
    unsigned bit = (unsigned)(c->control >> 31);

    c->control <<= 1;
    if (--c->left == 0U) {
        if (c->size - c->position >= 4U) {
            c->control = xx_data_get_u32(c->input + c->position, 4, 0, false);
            c->position += 4U;
        } else {
            c->control = 0U;
        }
        c->left = 32U;
    }
    return bit;
}

/* Emit @p count copies of the byte @p distance back, overlapping allowed. */
static bool xx_cwc_copy(uint8_t *output, size_t *written, size_t limit, size_t distance, size_t count)
{
    size_t index;

    if (distance == 0U || distance > *written || count > limit - *written) return false;
    for (index = 0U; index < count; ++index) {
        if (output) output[*written] = output[*written - distance];
        ++*written;
    }
    return true;
}

static bool xx_cwc_run(uint8_t *output, size_t *written, size_t limit, uint8_t value, size_t count)
{
    size_t index;

    if (count > limit - *written) return false;
    for (index = 0U; index < count; ++index) {
        if (output) output[*written] = value;
        ++*written;
    }
    return true;
}

/* One pass over the stream.  @p output may be NULL to measure only; @p limit
 * is the declared plaintext length, so the decoder cannot write past the
 * buffer the caller sized from that field.  Returns false on a malformed
 * stream; the caller checks that exactly @p limit bytes came out. */
static bool xx_cwc_walk(const uint8_t *input, size_t input_size, unsigned bits, uint8_t *output, size_t limit, size_t *consumed, size_t *produced)
{
    xx_cwc_cursor c;
    size_t written = 0U;
    uint32_t mask = (UINT32_C(1) << bits) - 1U;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input || input_size < 4U) return false;
    c.input = input;
    c.size = input_size;
    c.control = xx_data_get_u32(input, 4, 0, false);
    c.position = 4U;
    c.left = 32U;

    while (written < limit) {
        size_t length;
        size_t position;

        if (xx_cwc_bit(&c)) {
            if (c.position >= c.size) return false;
            if (output) output[written] = c.input[c.position];
            ++c.position;
            ++written;
            continue;
        }
        if (xx_cwc_bit(&c)) {
            unsigned high = xx_cwc_bit(&c);
            unsigned low = xx_cwc_bit(&c);

            length = (size_t)((high << 1) | low) + 2U;
            if (c.position >= c.size) return false;
            position = c.input[c.position++];
        } else if (xx_cwc_bit(&c)) {
            uint32_t word;

            if (c.size - c.position < 2U) return false;
            word = xx_data_get_u16(c.input + c.position, 2, 0, false);
            c.position += 2U;
            length = (size_t)(word & 0x0FU) + 2U;
            position = (size_t)(word >> 4);
        } else {
            uint32_t word;

            if (c.size - c.position < 3U) return false;
            word = (uint32_t)c.input[c.position] | ((uint32_t)c.input[c.position + 1U] << 8) | ((uint32_t)c.input[c.position + 2U] << 16);
            c.position += 3U;
            length = (size_t)(word & mask) + 2U;
            position = (size_t)(word >> bits);
            if (position == 0U) {
                if (length >= 18U) {
                    if (c.position >= c.size) return false;
                    if (!xx_cwc_run(output, &written, limit, c.input[c.position++], length + 1U)) return false;
                } else if (length == 2U) {
                    break;
                } else if (length == 3U || length == 4U) {
                    /* Refill and flush markers of the chunked DOS loader. */
                } else if (length == 5U) {
                    size_t count;

                    if (c.position >= c.size) return false;
                    count = c.input[c.position++];
                    if (c.size - c.position < count || count > limit - written) return false;
                    if (output) xx_rt_memcpy(output + written, c.input + c.position, count);
                    c.position += count;
                    written += count;
                } else {
                    return false;
                }
                continue;
            }
        }
        if (position == 0U) {
            if (c.position >= c.size) return false;
            if (!xx_cwc_run(output, &written, limit, c.input[c.position++], length + 1U)) return false;
        } else if (!xx_cwc_copy(output, &written, limit, (position - 1U) + length, length)) {
            return false;
        }
    }
    if (consumed) *consumed = c.position;
    if (produced) *produced = written;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_cwc_stream *xx_cwc_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cwc_stream *stream;
    xx_cwc_member *member;
    uint8_t header[XX_CWC_HEADER_SIZE];
    uint8_t *payload;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    unsigned bits;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool measured;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CWC_HEADER_SIZE + XX_CWC_MIN_PACKED_SIZE) return NULL;
    if (!xx_cwc_read_at(self, self->base_address, header, sizeof(header))) return NULL;
    if (header[0] != (uint8_t)'C' || header[1] != (uint8_t)'W' || header[2] != (uint8_t)'C') return NULL;
    bits = header[3];
    if (bits < XX_CWC_MIN_BITS || bits > XX_CWC_MAX_BITS) return NULL;

    uncompressed_size = (int64_t)xx_data_get_u32(header + 4, 4, 0, false);
    compressed_size = (int64_t)xx_data_get_u32(header + 8, 4, 0, false);
    /* A zero plaintext would make extraction write an empty file and call it
     * success; it is a reject, not an empty member. */
    if (uncompressed_size < 1 || uncompressed_size > XX_CWC_MAX_DECODED) return NULL;
    if (compressed_size < XX_CWC_MIN_PACKED_SIZE || compressed_size > span - XX_CWC_HEADER_SIZE) return NULL;
    if (uncompressed_size > compressed_size * XX_CWC_MAX_RATIO + XX_CWC_RATIO_SLACK) return NULL;

    payload = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    if (!payload) return NULL;
    if (!xx_cwc_read_at(self, self->base_address + XX_CWC_HEADER_SIZE, payload, (size_t)compressed_size)) {
        xx_mem_free(payload);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(payload);
        return NULL;
    }
    /* Measure only: nothing is allocated for the plaintext until the stream
     * has proven it produces exactly the declared length. */
    measured = xx_cwc_walk(payload, (size_t)compressed_size, bits, NULL, (size_t)uncompressed_size, &consumed, &produced);
    xx_mem_free(payload);
    if (!measured || (int64_t)produced != uncompressed_size) return NULL;
    if (consumed < 4U || (int64_t)consumed > compressed_size) return NULL;

    stream = (xx_cwc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_cwc_member *)xx_mem_alloc(sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        return NULL;
    }
    member = &stream->items[0];
    xx_mem_zero(member, sizeof(*member));
    member->name = xx_str_dup(XX_CWC_PLACEHOLDER_NAME);
    if (!member->name || !xx_cwc_path_safe(member->name)) {
        xx_str_free(member->name);
        xx_mem_free(stream->items);
        xx_mem_free(stream);
        return NULL;
    }
    member->header_offset = self->base_address;
    member->header_size = XX_CWC_HEADER_SIZE;
    member->data_offset = self->base_address + XX_CWC_HEADER_SIZE;
    member->compressed_size = compressed_size;
    member->uncompressed_size = uncompressed_size;
    member->method = XX_CWC_METHOD_LZ;
    member->bits = bits;
    stream->count = 1U;
    stream->index = 0U;
    stream->archive_size = (int64_t)XX_CWC_HEADER_SIZE + compressed_size;
    return stream;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_cwc_decode(Abstractformat *self, const xx_cwc_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_CWC_METHOD_LZ) return false;
    if (member->compressed_size < XX_CWC_MIN_PACKED_SIZE || member->uncompressed_size < 1) return false;
    if (member->compressed_size > XX_CWC_MAX_DECODED || member->uncompressed_size > XX_CWC_MAX_DECODED) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_cwc_read_at(self, member->data_offset, input, (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* Exactly the declared plaintext length, or nothing: the format has no
     * checksum, so this equality is the whole of extraction's check. */
    if (!xx_cwc_walk(input, (size_t)member->compressed_size, member->bits, output, (size_t)member->uncompressed_size, NULL, &written) ||
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

void xx_cwc_init(xx_cwc *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CWC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cwc");
    xx_format_set_extension(&archive->format, "cwc");
    archive->format.check_is_valid = xx_cwc_check_is_valid;
    archive->format.handle_base_info = xx_cwc_handle_base_info;
    archive->format.get_format_size = xx_cwc_get_format_size;
    archive->format.get_number_of_archive_records = xx_cwc_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_cwc_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_cwc_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_cwc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_cwc_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_cwc_free_archive_records_reading;
    archive->format.destroy = xx_cwc_vtable_destroy;
}

xx_cwc *xx_cwc_create(xx_io_device *device, int64_t base_address)
{
    xx_cwc *archive = (xx_cwc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cwc_init(archive, device, base_address);
    return archive;
}

void xx_cwc_destroy(xx_cwc *archive)
{
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cwc_free(xx_cwc *archive)
{
    if (!archive) return;
    xx_cwc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cwc_vtable_destroy(Abstractformat *self)
{
    xx_cwc_destroy((xx_cwc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_cwc_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cwc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cwc_parse(self, pd);
    if (!stream) return false;
    xx_cwc_stream_free(stream);
    return true;
}

bool xx_cwc_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cwc *archive = (xx_cwc *)self;
    xx_cwc_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_cwc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->archive_size) {
        self->overlay_offset = self->base_address + stream->archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    xx_cwc_stream_free(stream);
    return true;
}

int64_t xx_cwc_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cwc_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return self->is_valid ? ((xx_cwc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_cwc_set_record(xx_archive_record *record, const xx_cwc_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_cwc_copy_options(xx_list_s *target, const xx_list_s *options)
{
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) || !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_cwc_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_cwc_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_cwc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cwc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cwc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cwc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cwc_copy_options(&state->options, options) || (stream->count != 0U && !xx_cwc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cwc_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_cwc_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cwc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cwc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_cwc_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cwc_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cwc_stream *stream;
    const xx_cwc_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cwc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cwc_path_safe(member->name)) return false;

    path_option = xx_cwc_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member. */
        result = xx_cwc_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;
    if (!xx_store_create_dirs_a(target_path, false) || !xx_cwc_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        created = output != NULL;
        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_cwc_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
