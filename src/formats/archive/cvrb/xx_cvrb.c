/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CVRB single-file compressed installer members (Caere Corporation: OmniPage
 * Pro, OmniScan, Scan Manager, BitFax; `*.DL$`, `*.EX$`, `*.SY$`, `*.HL$`).
 * One payload per file:
 *
 *   header, 19 bytes at offset 0, little endian:
 *     0x00  4  "CVRB"
 *     0x04  4  u32  MS-DOS stamp (time in the low word, date in the high word)
 *     0x08  4  u32  unpacked size
 *     0x0C  4  u32  packed size, the whole file length
 *     0x10  2  u16  CRC-16/KERMIT of the plaintext
 *     0x12  1  u8   method, 0
 *     0x13  n  the stream
 *
 * The stream is LSB-first LZW: one leading flag bit (always 1) is skipped,
 * then codes of 9 to 12 bits with 256 = clear, 257 = end of information and
 * 258 the first phrase; the width grows when the next free code reaches
 * 2^width.  The plaintext CRC is verified, which makes the format safely
 * identifiable beyond its four magic bytes.
 *
 * Recovered from the samples and verified over 158 of 158 files.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/cvrb/xx_cvrb.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_CVRB_FILE_TYPE XX_FILE_TYPE_CVRB

#define XX_CVRB_HEADER_SIZE 19
/* The flag bit plus one nine-bit code. */
#define XX_CVRB_MIN_PACKED_SIZE 2
#define XX_CVRB_METHOD_LZW 0U
#define XX_CVRB_MIN_WIDTH 9U
#define XX_CVRB_MAX_WIDTH 12U
#define XX_CVRB_CLEAR_CODE 256U
#define XX_CVRB_END_CODE 257U
#define XX_CVRB_FIRST_CODE 258U
#define XX_CVRB_TABLE_SIZE 4096U
#define XX_CVRB_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* A twelve-bit code can stand for a phrase of up to 3838 bytes. */
#define XX_CVRB_MAX_RATIO 4096
#define XX_CVRB_RATIO_SLACK 64
#define XX_CVRB_PLACEHOLDER_NAME "cvrb_data"

typedef struct xx_cvrb_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t dos_stamp;
    uint16_t crc;
} xx_cvrb_member;

typedef struct xx_cvrb_stream_s {
    xx_cvrb_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_cvrb_stream;

static void xx_cvrb_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_cvrb_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
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

static bool xx_cvrb_path_safe(const char *name)
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

static void xx_cvrb_stream_free(void *pointer)
{
    xx_cvrb_stream *stream = (xx_cvrb_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* CRC-16/KERMIT: polynomial 0x1021 reflected (0x8408), init 0, no final xor. */
static uint16_t xx_cvrb_crc16_kermit(const uint8_t *data, size_t size)
{
    uint16_t crc = 0U;
    size_t index;
    unsigned bit;

    for (index = 0U; index < size; ++index) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0x8408U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

/* --------------------------------------------------------------- codec -- */

typedef struct xx_cvrb_lzw_s {
    uint16_t prefix[XX_CVRB_TABLE_SIZE];
    uint8_t suffix[XX_CVRB_TABLE_SIZE];
    uint8_t stack[XX_CVRB_TABLE_SIZE];
} xx_cvrb_lzw;

/* Push the phrase of @p code onto the stack, last byte first.  Returns its
 * length, 0 on a broken chain. */
static size_t xx_cvrb_phrase(xx_cvrb_lzw *t, uint32_t code, uint32_t next_code)
{
    size_t length = 0U;

    while (code >= XX_CVRB_FIRST_CODE) {
        if (code >= next_code || length >= XX_CVRB_TABLE_SIZE) return 0U;
        t->stack[length++] = t->suffix[code];
        code = t->prefix[code];
    }
    if (code > 255U || length >= XX_CVRB_TABLE_SIZE) return 0U;
    t->stack[length++] = (uint8_t)code;
    return length;
}

/* The LZW stream to plaintext.  Exactly @p limit bytes must come out. */
static bool xx_cvrb_lzw_run(xx_cvrb_lzw *t, const uint8_t *input, size_t input_size, uint8_t *output, size_t limit)
{
    uint64_t total_bits = (uint64_t)input_size * 8U;
    uint64_t bit_position = 1U; /* the leading flag bit */
    uint32_t width = XX_CVRB_MIN_WIDTH;
    uint32_t next_code = XX_CVRB_FIRST_CODE;
    uint32_t old_code = 0U;
    bool have_old = false;
    size_t written = 0U;

    while (written < limit) {
        uint32_t code;
        size_t length;
        size_t index;
        uint8_t first;

        if (bit_position + width > total_bits) return false;
        {
            size_t byte = (size_t)(bit_position >> 3);
            unsigned shift = (unsigned)(bit_position & 7U);
            uint32_t window = 0U;
            unsigned n;

            for (n = 0U; n < 4U && byte + n < input_size; ++n) window |= (uint32_t)input[byte + n] << (8U * n);
            code = (window >> shift) & ((UINT32_C(1) << width) - 1U);
        }
        bit_position += width;

        if (code == XX_CVRB_CLEAR_CODE) {
            width = XX_CVRB_MIN_WIDTH;
            next_code = XX_CVRB_FIRST_CODE;
            have_old = false;
            continue;
        }
        if (code == XX_CVRB_END_CODE) break;
        if (!have_old) {
            if (code > 255U) return false;
            output[written++] = (uint8_t)code;
            old_code = code;
            have_old = true;
            continue;
        }
        if (code < next_code) {
            length = xx_cvrb_phrase(t, code, next_code);
            if (length == 0U) return false;
            first = t->stack[length - 1U];
        } else if (code == next_code) {
            /* KwKwK: the phrase being defined, old phrase plus its first byte. */
            length = xx_cvrb_phrase(t, old_code, next_code);
            if (length == 0U || length >= XX_CVRB_TABLE_SIZE) return false;
            first = t->stack[length - 1U];
            for (index = length; index > 0U; --index) t->stack[index] = t->stack[index - 1U];
            t->stack[0] = first;
            ++length;
        } else {
            return false;
        }
        if (length > limit - written) return false;
        for (index = 0U; index < length; ++index) output[written + index] = t->stack[length - 1U - index];
        written += length;
        if (next_code < XX_CVRB_TABLE_SIZE) {
            t->prefix[next_code] = (uint16_t)old_code;
            t->suffix[next_code] = first;
            ++next_code;
            if (next_code >= (UINT32_C(1) << width) && width < XX_CVRB_MAX_WIDTH) ++width;
        }
        old_code = code;
    }
    return written == limit;
}

static bool xx_cvrb_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t limit, uint16_t crc, xx_pd_struct *pd)
{
    xx_cvrb_lzw *table;
    bool result;

    if (!input || !output || input_size < (size_t)XX_CVRB_MIN_PACKED_SIZE || limit == 0U) return false;
    table = (xx_cvrb_lzw *)xx_mem_alloc(sizeof(*table));
    if (!table) return false;
    result = xx_cvrb_lzw_run(table, input, input_size, output, limit);
    xx_mem_free(table);
    if (result && pd && xx_pd_is_stopped(pd)) result = false;
    if (result) result = xx_cvrb_crc16_kermit(output, limit) == crc;
    return result;
}

/* --------------------------------------------------------------- parse -- */

static xx_cvrb_stream *xx_cvrb_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cvrb_stream *stream;
    xx_cvrb_member *member;
    uint8_t header[XX_CVRB_HEADER_SIZE];
    uint8_t *payload;
    uint8_t *plain;
    int64_t total;
    int64_t span;
    int64_t packed_total;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint16_t crc;
    bool decoded;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CVRB_HEADER_SIZE + XX_CVRB_MIN_PACKED_SIZE) return NULL;
    if (!xx_cvrb_read_at(self, self->base_address, header, sizeof(header))) return NULL;
    if (header[0] != (uint8_t)'C' || header[1] != (uint8_t)'V' || header[2] != (uint8_t)'R' || header[3] != (uint8_t)'B') return NULL;
    if (header[18] != XX_CVRB_METHOD_LZW) return NULL;
    uncompressed_size = (int64_t)xx_data_get_u32(header + 8, 4, 0, false);
    packed_total = (int64_t)xx_data_get_u32(header + 12, 4, 0, false);
    crc = (uint16_t)xx_data_get_u16(header + 16, 2, 0, false);
    /* The packed size counts the header; it is the whole file. */
    if (packed_total < XX_CVRB_HEADER_SIZE + XX_CVRB_MIN_PACKED_SIZE || packed_total > span) return NULL;
    compressed_size = packed_total - XX_CVRB_HEADER_SIZE;
    if (uncompressed_size < 1 || uncompressed_size > XX_CVRB_MAX_DECODED) return NULL;
    if (uncompressed_size > compressed_size * XX_CVRB_MAX_RATIO + XX_CVRB_RATIO_SLACK) return NULL;

    payload = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)uncompressed_size);
    if (!payload || !plain) {
        xx_mem_free(payload);
        xx_mem_free(plain);
        return NULL;
    }
    decoded = xx_cvrb_read_at(self, self->base_address + XX_CVRB_HEADER_SIZE, payload, (size_t)compressed_size) &&
              xx_cvrb_decode_memory(payload, (size_t)compressed_size, plain, (size_t)uncompressed_size, crc, pd);
    xx_mem_free(payload);
    xx_mem_free(plain);
    if (!decoded) return NULL;

    stream = (xx_cvrb_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_cvrb_member *)xx_mem_alloc(sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        return NULL;
    }
    member = &stream->items[0];
    xx_mem_zero(member, sizeof(*member));
    member->name = xx_str_dup(XX_CVRB_PLACEHOLDER_NAME);
    if (!member->name || !xx_cvrb_path_safe(member->name)) {
        xx_str_free(member->name);
        xx_mem_free(stream->items);
        xx_mem_free(stream);
        return NULL;
    }
    member->header_offset = self->base_address;
    member->header_size = XX_CVRB_HEADER_SIZE;
    member->data_offset = self->base_address + XX_CVRB_HEADER_SIZE;
    member->compressed_size = compressed_size;
    member->uncompressed_size = uncompressed_size;
    member->method = XX_CVRB_METHOD_LZW;
    member->crc = crc;
    /* Stored time-low, date-high; published date-high as every DOS stamp. */
    member->dos_stamp = xx_data_get_u32(header + 4, 4, 0, false);
    stream->count = 1U;
    stream->index = 0U;
    stream->archive_size = packed_total;
    return stream;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_cvrb_decode(Abstractformat *self, const xx_cvrb_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input;
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_CVRB_METHOD_LZW) return false;
    if (member->compressed_size < XX_CVRB_MIN_PACKED_SIZE || member->uncompressed_size < 1) return false;
    if (member->compressed_size > XX_CVRB_MAX_DECODED || member->uncompressed_size > XX_CVRB_MAX_DECODED) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!input || !output) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    if (!xx_cvrb_read_at(self, member->data_offset, input, (size_t)member->compressed_size) ||
        !xx_cvrb_decode_memory(input, (size_t)member->compressed_size, output, (size_t)member->uncompressed_size, member->crc, pd)) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_cvrb_init(xx_cvrb *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CVRB_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cvrb");
    xx_format_set_extension(&archive->format, "cvrb");
    archive->format.check_is_valid = xx_cvrb_check_is_valid;
    archive->format.handle_base_info = xx_cvrb_handle_base_info;
    archive->format.get_format_size = xx_cvrb_get_format_size;
    archive->format.get_number_of_archive_records = xx_cvrb_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_cvrb_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_cvrb_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_cvrb_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_cvrb_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_cvrb_free_archive_records_reading;
    archive->format.destroy = xx_cvrb_vtable_destroy;
}

xx_cvrb *xx_cvrb_create(xx_io_device *device, int64_t base_address)
{
    xx_cvrb *archive = (xx_cvrb *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cvrb_init(archive, device, base_address);
    return archive;
}

void xx_cvrb_destroy(xx_cvrb *archive)
{
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cvrb_free(xx_cvrb *archive)
{
    if (!archive) return;
    xx_cvrb_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cvrb_vtable_destroy(Abstractformat *self)
{
    xx_cvrb_destroy((xx_cvrb *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_cvrb_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cvrb_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cvrb_parse(self, pd);
    if (!stream) return false;
    xx_cvrb_stream_free(stream);
    return true;
}

bool xx_cvrb_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cvrb *archive = (xx_cvrb *)self;
    xx_cvrb_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_cvrb_parse(self, pd);
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
    xx_cvrb_stream_free(stream);
    return true;
}

int64_t xx_cvrb_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cvrb_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return self->is_valid ? ((xx_cvrb *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_cvrb_set_record(xx_archive_record *record, const xx_cvrb_member *member)
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->dos_stamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_cvrb_copy_options(xx_list_s *target, const xx_list_s *options)
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

static const xx_var *xx_cvrb_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_cvrb_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_cvrb_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cvrb_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cvrb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cvrb_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cvrb_copy_options(&state->options, options) || (stream->count != 0U && !xx_cvrb_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cvrb_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_cvrb_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cvrb_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cvrb_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_cvrb_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cvrb_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cvrb_stream *stream;
    const xx_cvrb_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cvrb_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cvrb_path_safe(member->name)) return false;

    path_option = xx_cvrb_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_cvrb_decode(self, member, &plain, &plain_size, pd);
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
    if (!xx_store_create_dirs_a(target_path, false) || !xx_cvrb_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_cvrb_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
