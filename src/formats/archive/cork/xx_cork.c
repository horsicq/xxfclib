/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CORK single-file compressed installer members (Omega Logic's installer,
 * as used by Landmark Research's WinProbe: WINPROBE.HL$, VWPD.38$ ...).
 * One payload per file, carrying the original name:
 *
 *   header, 33 bytes at offset 0, little endian:
 *     0x00  4  "CORK"
 *     0x04  2  u16  version, 1
 *     0x06 13  char original name, NUL terminated (8.3, CP437)
 *     0x13  2  u16  CRC-16/ARC of the plaintext
 *     0x15  2  u16  MS-DOS date
 *     0x17  2  u16  MS-DOS time
 *     0x19  4  u32  unpacked size
 *     0x1D  4  u32  packed size (file size minus 33)
 *     0x21  n  the stream
 *
 * The stream is ARC's method 8 ("crunched"): one byte holding the maximum
 * code width (12), then LSB-first LZW with a clear code 256, widths growing
 * from 9 to the maximum, codes packed in groups of eight so that a clear
 * code discards the rest of its group, and an RLE90 run filter over the
 * LZW output.  The plaintext CRC is verified, which makes the format safely
 * identifiable beyond its four magic bytes.
 *
 * Recovered from the samples and verified over 55 of 57 files; the other two
 * are bit-damaged copies of files that also exist intact.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/archive/cork/xx_cork.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_CORK_FILE_TYPE XX_FILE_TYPE_CORK

#define XX_CORK_HEADER_SIZE 33
#define XX_CORK_NAME_FIELD 13U
#define XX_CORK_VERSION 1U
/* The width byte plus at least one nine-bit code. */
#define XX_CORK_MIN_PACKED_SIZE 3
#define XX_CORK_MIN_WIDTH 9U
#define XX_CORK_MAX_WIDTH 16U
#define XX_CORK_CLEAR_CODE 256U
#define XX_CORK_FIRST_CODE 257U
#define XX_CORK_TABLE_SIZE 65536U
#define XX_CORK_METHOD_CRUNCHED 8U
#define XX_CORK_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* LZW can expand by a code's worth of phrase per nine bits and RLE90 by 255
 * bytes per two; neither reaches 4096x over a whole file. */
#define XX_CORK_MAX_RATIO 4096
#define XX_CORK_RATIO_SLACK 64
#define XX_CORK_PLACEHOLDER_NAME "cork_data"

typedef struct xx_cork_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t dos_stamp;
    uint16_t crc;
} xx_cork_member;

typedef struct xx_cork_stream_s {
    xx_cork_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_cork_stream;

static void xx_cork_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_cork_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
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

static bool xx_cork_path_safe(const char *name)
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

static void xx_cork_stream_free(void *pointer)
{
    xx_cork_stream *stream = (xx_cork_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* The stored name, with anything that is not a plain file name character
 * replaced, and a placeholder when the field is empty. */
static char *xx_cork_member_name(const uint8_t *field)
{
    char buffer[XX_CORK_NAME_FIELD + 1U];
    size_t length = 0U;
    size_t index;

    while (length < XX_CORK_NAME_FIELD && field[length] != 0U) ++length;
    for (index = 0U; index < length; ++index) {
        uint8_t c = field[index];
        buffer[index] = (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') ? '_' : (char)c;
    }
    buffer[length] = '\0';
    while (length > 0U && (buffer[length - 1U] == ' ' || buffer[length - 1U] == '.')) buffer[--length] = '\0';
    if (length == 0U || (length <= 2U && buffer[0] == '.' && (length == 1U || buffer[1] == '.'))) {
        return xx_str_dup(XX_CORK_PLACEHOLDER_NAME);
    }
    return xx_str_dup(buffer);
}

/* --------------------------------------------------------------- codec -- */

typedef struct xx_cork_lzw_s {
    uint16_t prefix[XX_CORK_TABLE_SIZE];
    uint8_t suffix[XX_CORK_TABLE_SIZE];
    uint8_t stack[XX_CORK_TABLE_SIZE];
} xx_cork_lzw;

/* Push the phrase of @p code onto the table's stack, root first at the top.
 * Returns its length, or 0 when the chain is broken. */
static size_t xx_cork_phrase(xx_cork_lzw *t, uint32_t code, uint32_t next_code)
{
    size_t length = 0U;

    while (code >= XX_CORK_FIRST_CODE) {
        if (code >= next_code || length >= XX_CORK_TABLE_SIZE) return 0U;
        t->stack[length++] = t->suffix[code];
        code = t->prefix[code];
    }
    if (code > 255U || length >= XX_CORK_TABLE_SIZE) return 0U;
    t->stack[length++] = (uint8_t)code;
    return length;
}

/* Layer one: the LZW stream after the width byte, into @p output.  Returns
 * false on a malformed stream or when more than @p limit bytes come out. */
static bool xx_cork_lzw_run(xx_cork_lzw *t, const uint8_t *input, size_t input_size, uint8_t max_width, uint8_t *output, size_t limit, size_t *produced)
{
    uint64_t total_bits = (uint64_t)input_size * 8U;
    uint64_t bit_position = 0U;
    uint32_t width = XX_CORK_MIN_WIDTH;
    uint32_t next_code = XX_CORK_FIRST_CODE;
    uint32_t capacity = UINT32_C(1) << max_width;
    uint32_t old_code = 0U;
    bool have_old = false;
    unsigned group = 0U;
    size_t written = 0U;

    *produced = 0U;
    for (;;) {
        uint32_t code;
        size_t length;
        size_t index;
        uint8_t first;

        if (bit_position + width > total_bits) break;
        {
            size_t byte = (size_t)(bit_position >> 3);
            unsigned shift = (unsigned)(bit_position & 7U);
            uint32_t window = 0U;
            unsigned n;

            for (n = 0U; n < 4U && byte + n < input_size; ++n) window |= (uint32_t)input[byte + n] << (8U * n);
            code = (window >> shift) & ((UINT32_C(1) << width) - 1U);
        }
        bit_position += width;
        group = (group + 1U) & 7U;

        if (code == XX_CORK_CLEAR_CODE) {
            /* The rest of the eight-code group is padding. */
            if (group != 0U) {
                bit_position += (uint64_t)(8U - group) * width;
                group = 0U;
            }
            width = XX_CORK_MIN_WIDTH;
            next_code = XX_CORK_FIRST_CODE;
            have_old = false;
            continue;
        }
        if (!have_old) {
            if (code > 255U) return false;
            if (written >= limit) return false;
            output[written++] = (uint8_t)code;
            old_code = code;
            have_old = true;
            continue;
        }
        if (code < next_code) {
            length = xx_cork_phrase(t, code, next_code);
            if (length == 0U) return false;
            first = t->stack[length - 1U];
        } else if (code == next_code) {
            /* The KwKwK case: the phrase being defined, old phrase plus its
             * own first byte. */
            length = xx_cork_phrase(t, old_code, next_code);
            if (length == 0U || length >= XX_CORK_TABLE_SIZE) return false;
            first = t->stack[length - 1U];
            /* Shift the stack so the extra byte lands last. */
            for (index = length; index > 0U; --index) t->stack[index] = t->stack[index - 1U];
            t->stack[0] = first;
            ++length;
        } else {
            return false;
        }
        if (length > limit - written) return false;
        for (index = 0U; index < length; ++index) output[written + index] = t->stack[length - 1U - index];
        written += length;
        if (next_code < capacity) {
            t->prefix[next_code] = (uint16_t)old_code;
            t->suffix[next_code] = first;
            ++next_code;
            if (next_code > ((UINT32_C(1) << width) - 1U) && width < max_width) ++width;
        }
        old_code = code;
    }
    *produced = written;
    return true;
}

/* Layer two: ARC's 0x90 run filter.  Exactly @p limit bytes must come out. */
static bool xx_cork_rle90(const uint8_t *input, size_t input_size, uint8_t *output, size_t limit)
{
    size_t position = 0U;
    size_t written = 0U;
    uint8_t last = 0U;

    while (position < input_size && written < limit) {
        uint8_t value = input[position++];

        if (value != 0x90U) {
            output[written++] = value;
            last = value;
            continue;
        }
        if (position >= input_size) break;
        value = input[position++];
        if (value == 0U) {
            output[written++] = 0x90U;
            last = 0x90U;
        } else {
            size_t count = (size_t)value - 1U;
            size_t index;

            if (count > limit - written) return false;
            for (index = 0U; index < count; ++index) output[written++] = last;
        }
    }
    return written == limit;
}

/* The whole payload to plaintext: width byte, LZW, RLE90, and the CRC. */
static bool xx_cork_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t limit, uint16_t crc, xx_pd_struct *pd)
{
    xx_cork_lzw *table;
    uint8_t *middle;
    size_t middle_limit;
    size_t middle_size = 0U;
    uint8_t width;
    bool result;

    if (!input || !output || input_size < (size_t)XX_CORK_MIN_PACKED_SIZE || limit == 0U) return false;
    width = (uint8_t)(input[0] & 0x1FU);
    if (width < XX_CORK_MIN_WIDTH || width > XX_CORK_MAX_WIDTH) return false;
    /* RLE90 encoding never more than doubles its input, so the LZW output
     * is bounded by twice the plaintext plus a run token. */
    if (limit > ((size_t)XX_CORK_MAX_DECODED) / 2U) return false;
    middle_limit = limit * 2U + 2U;
    table = (xx_cork_lzw *)xx_mem_alloc(sizeof(*table));
    middle = (uint8_t *)xx_mem_alloc(middle_limit);
    if (!table || !middle) {
        xx_mem_free(table);
        xx_mem_free(middle);
        return false;
    }
    result = xx_cork_lzw_run(table, input + 1, input_size - 1U, width, middle, middle_limit, &middle_size);
    xx_mem_free(table);
    if (result && pd && xx_pd_is_stopped(pd)) result = false;
    if (result) result = xx_cork_rle90(middle, middle_size, output, limit);
    xx_mem_free(middle);
    if (result) result = xx_crc16_arc_calc(0U, output, limit) == crc;
    return result;
}

/* --------------------------------------------------------------- parse -- */

static xx_cork_stream *xx_cork_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cork_stream *stream;
    xx_cork_member *member;
    uint8_t header[XX_CORK_HEADER_SIZE];
    uint8_t *payload;
    uint8_t *plain;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint16_t crc;
    bool decoded;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CORK_HEADER_SIZE + XX_CORK_MIN_PACKED_SIZE) return NULL;
    if (!xx_cork_read_at(self, self->base_address, header, sizeof(header))) return NULL;
    if (header[0] != (uint8_t)'C' || header[1] != (uint8_t)'O' || header[2] != (uint8_t)'R' || header[3] != (uint8_t)'K') return NULL;
    if (xx_data_get_u16(header + 4, 2, 0, false) != XX_CORK_VERSION) return NULL;
    crc = (uint16_t)xx_data_get_u16(header + 19, 2, 0, false);
    uncompressed_size = (int64_t)xx_data_get_u32(header + 25, 4, 0, false);
    compressed_size = (int64_t)xx_data_get_u32(header + 29, 4, 0, false);
    if (uncompressed_size < 1 || uncompressed_size > XX_CORK_MAX_DECODED) return NULL;
    if (compressed_size < XX_CORK_MIN_PACKED_SIZE || compressed_size > span - XX_CORK_HEADER_SIZE) return NULL;
    if (uncompressed_size > compressed_size * XX_CORK_MAX_RATIO + XX_CORK_RATIO_SLACK) return NULL;

    payload = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)uncompressed_size);
    if (!payload || !plain) {
        xx_mem_free(payload);
        xx_mem_free(plain);
        return NULL;
    }
    decoded = xx_cork_read_at(self, self->base_address + XX_CORK_HEADER_SIZE, payload, (size_t)compressed_size) &&
              xx_cork_decode_memory(payload, (size_t)compressed_size, plain, (size_t)uncompressed_size, crc, pd);
    xx_mem_free(payload);
    xx_mem_free(plain);
    if (!decoded) return NULL;

    stream = (xx_cork_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_cork_member *)xx_mem_alloc(sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        return NULL;
    }
    member = &stream->items[0];
    xx_mem_zero(member, sizeof(*member));
    member->name = xx_cork_member_name(header + 6);
    if (!member->name || !xx_cork_path_safe(member->name)) {
        xx_str_free(member->name);
        xx_mem_free(stream->items);
        xx_mem_free(stream);
        return NULL;
    }
    member->header_offset = self->base_address;
    member->header_size = XX_CORK_HEADER_SIZE;
    member->data_offset = self->base_address + XX_CORK_HEADER_SIZE;
    member->compressed_size = compressed_size;
    member->uncompressed_size = uncompressed_size;
    member->method = XX_CORK_METHOD_CRUNCHED;
    member->crc = crc;
    member->dos_stamp = ((uint32_t)xx_data_get_u16(header + 21, 2, 0, false) << 16) | (uint32_t)xx_data_get_u16(header + 23, 2, 0, false);
    stream->count = 1U;
    stream->index = 0U;
    stream->archive_size = (int64_t)XX_CORK_HEADER_SIZE + compressed_size;
    return stream;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_cork_decode(Abstractformat *self, const xx_cork_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input;
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_CORK_METHOD_CRUNCHED) return false;
    if (member->compressed_size < XX_CORK_MIN_PACKED_SIZE || member->uncompressed_size < 1) return false;
    if (member->compressed_size > XX_CORK_MAX_DECODED || member->uncompressed_size > XX_CORK_MAX_DECODED) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!input || !output) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    if (!xx_cork_read_at(self, member->data_offset, input, (size_t)member->compressed_size) ||
        !xx_cork_decode_memory(input, (size_t)member->compressed_size, output, (size_t)member->uncompressed_size, member->crc, pd)) {
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

void xx_cork_init(xx_cork *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CORK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cork");
    xx_format_set_extension(&archive->format, "cork");
    archive->format.check_is_valid = xx_cork_check_is_valid;
    archive->format.handle_base_info = xx_cork_handle_base_info;
    archive->format.get_format_size = xx_cork_get_format_size;
    archive->format.get_number_of_archive_records = xx_cork_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_cork_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_cork_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_cork_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_cork_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_cork_free_archive_records_reading;
    archive->format.destroy = xx_cork_vtable_destroy;
}

xx_cork *xx_cork_create(xx_io_device *device, int64_t base_address)
{
    xx_cork *archive = (xx_cork *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cork_init(archive, device, base_address);
    return archive;
}

void xx_cork_destroy(xx_cork *archive)
{
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cork_free(xx_cork *archive)
{
    if (!archive) return;
    xx_cork_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cork_vtable_destroy(Abstractformat *self)
{
    xx_cork_destroy((xx_cork *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_cork_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cork_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cork_parse(self, pd);
    if (!stream) return false;
    xx_cork_stream_free(stream);
    return true;
}

bool xx_cork_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cork *archive = (xx_cork *)self;
    xx_cork_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_cork_parse(self, pd);
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
    xx_cork_stream_free(stream);
    return true;
}

int64_t xx_cork_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cork_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return self->is_valid ? ((xx_cork *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_cork_set_record(xx_archive_record *record, const xx_cork_member *member)
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

static bool xx_cork_copy_options(xx_list_s *target, const xx_list_s *options)
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

static const xx_var *xx_cork_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_cork_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_cork_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cork_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cork_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cork_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cork_copy_options(&state->options, options) || (stream->count != 0U && !xx_cork_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cork_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_cork_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cork_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cork_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_cork_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cork_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cork_stream *stream;
    const xx_cork_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cork_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cork_path_safe(member->name)) return false;

    path_option = xx_cork_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_cork_decode(self, member, &plain, &plain_size, pd);
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
    if (!xx_store_create_dirs_a(target_path, false) || !xx_cork_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_cork_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
