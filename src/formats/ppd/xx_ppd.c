/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Paranoid Productions' PPD resource bundle, as used by the VPITROD and
 * Rewind demos.  The format has no magic: a count and variable-length names
 * lead to absolute packed offsets.  The last packed member ends at EOF.
 *
 * The decompressor was recovered from the original Rewind.exe (routine at
 * VA 0x408410) and independently checked against all 133 VPITROD members:
 * 25,236,862 decoded bytes matched the reference extraction byte for byte.  All
 * 187 Rewind members also decoded to their declared sizes and consumed their
 * exact packed spans.  Its bit grammar resembles aPack but differs from a
 * generic aPLib stream: the first byte is a tag, and gamma bits are read as
 * continuation followed by data.
 */

#include "xxfclib/formats/ppd/xx_ppd.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define PPD_MAX_ARCHIVE (512U * 1024U * 1024U)
#define PPD_MAX_MEMBER (128U * 1024U * 1024U)
#define PPD_MAX_TOTAL (512U * 1024U * 1024U)
#define PPD_MAX_MEMBERS 4096U
#define PPD_MAX_NAME_FIELD 95U

#ifdef PPD
#define PPD_FILE_TYPE XX_FILE_TYPE_PPD
#else
#define PPD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct ppd_row_s {
    char name[96];
    uint32_t raw_size;
    uint32_t offset;
} ppd_row;

typedef struct ppd_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t tag;
    unsigned remaining;
} ppd_bits;

static bool ppd_byte(ppd_bits *bits, uint8_t *value)
{
    if (bits->position >= bits->size) return false;
    *value = bits->data[bits->position++];
    return true;
}

static bool ppd_bit(ppd_bits *bits, unsigned *value)
{
    if (bits->remaining == 0U) {
        if (!ppd_byte(bits, &bits->tag)) return false;
        bits->remaining = 8U;
    }
    *value = (unsigned)((bits->tag >> 7U) & 1U);
    bits->tag = (uint8_t)(bits->tag << 1U);
    --bits->remaining;
    return true;
}

/* The original stream reads the continuation bit before the data bit. */
static bool ppd_gamma(ppd_bits *bits, uint32_t *value)
{
    uint32_t result = 1U;
    unsigned continuation, digit;
    do {
        if (!ppd_bit(bits, &continuation) || !ppd_bit(bits, &digit) || result > (UINT32_MAX - digit) / 2U) return false;
        result = result * 2U + digit;
    } while (continuation != 0U);
    *value = result;
    return true;
}

static bool ppd_copy(uint8_t *output, size_t output_size, size_t *position, size_t distance, size_t length)
{
    size_t index;
    if (distance == 0U || distance > *position || length > output_size - *position) return false;
    for (index = 0U; index < length; ++index) {
        output[*position] = output[*position - distance];
        ++*position;
    }
    return true;
}

static bool ppd_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, xx_pd_struct *pd)
{
    ppd_bits bits;
    size_t produced = 0U;
    size_t last_distance = 0U;
    unsigned code;
    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    bits.tag = 0U;
    bits.remaining = 0U;
    for (;;) {
        uint8_t byte;
        uint32_t gamma;
        size_t distance, length;
        if ((pd && xx_pd_is_stopped(pd)) || !ppd_bit(&bits, &code)) return false;
        if (code == 0U) {
            if (produced == output_size || !ppd_byte(&bits, &byte)) return false;
            output[produced++] = byte;
            continue;
        }
        if (!ppd_bit(&bits, &code)) return false;
        if (code == 0U) { /* 10: gamma long match */
            if (!ppd_gamma(&bits, &gamma) || gamma < 2U) return false;
            gamma -= 2U;
            if (gamma == 0U) {
                distance = last_distance;
                if (!ppd_gamma(&bits, &gamma)) return false;
                length = (size_t)gamma;
            } else {
                if (!ppd_byte(&bits, &byte)) return false;
                if (((uint64_t)(gamma - 1U) << 8U) > SIZE_MAX) return false;
                distance = (((size_t)gamma - 1U) << 8U) | (size_t)byte;
                if (!ppd_gamma(&bits, &gamma)) return false;
                length = (size_t)gamma;
                if (distance <= 127U) {
                    if (length > SIZE_MAX - 2U) return false;
                    length += 2U;
                }
                last_distance = distance;
            }
            if (!ppd_copy(output, output_size, &produced, distance, length)) return false;
            continue;
        }
        if (!ppd_bit(&bits, &code)) return false;
        if (code == 0U) { /* 110: short match / end */
            if (!ppd_byte(&bits, &byte)) return false;
            distance = (size_t)(byte >> 1U);
            if (distance == 0U) return produced == output_size && bits.position == input_size;
            length = 2U + (size_t)(byte & 1U);
            if (!ppd_copy(output, output_size, &produced, distance, length)) return false;
            continue;
        }
        /* 111: four-bit one-byte backward copy; zero means a zero byte. */
        distance = 0U;
        for (unsigned index = 0U; index < 4U; ++index) {
            if (!ppd_bit(&bits, &code)) return false;
            distance = (distance << 1U) | (size_t)code;
        }
        if (produced == output_size) return false;
        if (distance == 0U) {
            output[produced++] = 0U;
        } else if (!ppd_copy(output, output_size, &produced, distance, 1U)) {
            return false;
        }
    }
}

static bool ppd_name(char output[96], const uint8_t *input, size_t size)
{
    size_t length = 0U;
    if (size < 2U || size > PPD_MAX_NAME_FIELD) return false;
    while (length < size && input[length] != 0U) {
        unsigned char c = input[length];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        output[length] = (char)c;
        ++length;
    }
    if (length == 0U || length == size || output[length - 1U] == '.' || output[length - 1U] == ' ') return false;
    output[length] = 0;
    return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    int64_t span = pm_available(format);
    uint8_t field[4];
    ppd_row *rows = NULL;
    uint64_t total = 0U;
    uint32_t count, index;
    int64_t position = 4;
    bool ok = false;
    if (span < 22 || span > PPD_MAX_ARCHIVE || !pm_read(format, 0, field, sizeof(field))) return false;
    count = xx_data_get_u32(field, 4, 0, false);
    if (count == 0U || count > PPD_MAX_MEMBERS || (uint64_t)count > ((uint64_t)span - 4U) / 18U) return false;
    rows = (ppd_row *)xx_mem_alloc((size_t)count * sizeof(*rows));
    if (!rows) return false;
    for (index = 0U; index < count; ++index) {
        uint8_t name[PPD_MAX_NAME_FIELD];
        uint8_t tail[12];
        uint32_t name_size;
        if ((pd && xx_pd_is_stopped(pd)) || span - position < 4 || !pm_read(format, position, field, sizeof(field))) goto done;
        name_size = xx_data_get_u32(field, 4, 0, false);
        position += 4;
        if (name_size < 2U || name_size > PPD_MAX_NAME_FIELD || span - position < (int64_t)name_size + 12 || !pm_read(format, position, name, name_size) ||
            !ppd_name(rows[index].name, name, name_size))
            goto done;
        position += name_size;
        if (!pm_read(format, position, tail, sizeof(tail))) goto done;
        rows[index].raw_size = xx_data_get_u32(tail, 4, 0, false);
        rows[index].offset = xx_data_get_u32(tail + 4U, 4, 0, false);
        position += sizeof(tail);
        if (rows[index].raw_size == 0U || rows[index].raw_size > PPD_MAX_MEMBER || total > PPD_MAX_TOTAL - rows[index].raw_size || rows[index].offset >= (uint64_t)span)
            goto done;
        total += rows[index].raw_size;
    }
    if ((int64_t)rows[0].offset != position) goto done;
    for (index = 0U; index < count; ++index) {
        int64_t end = index + 1U < count ? (int64_t)rows[index + 1U].offset : span;
        int64_t packed_size = end - (int64_t)rows[index].offset;
        uint8_t *packed = NULL;
        uint8_t *plain = NULL;
        pm_member *member;
        if ((pd && xx_pd_is_stopped(pd)) || packed_size <= 0 || packed_size > PPD_MAX_MEMBER ||
            !pm_add(format, stream, rows[index].name, (int64_t)rows[index].offset, packed_size))
            goto done;
        member = &stream->items[stream->count - 1U];
        xx_rt_snprintf(member->name, sizeof(member->name), "%s", rows[index].name);
        member->size = rows[index].raw_size;
        member->packed_size = packed_size;
        packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
        plain = (uint8_t *)xx_mem_alloc(rows[index].raw_size);
        if (!packed || !plain || !pm_read(format, rows[index].offset, packed, (size_t)packed_size) ||
            !ppd_decode(packed, (size_t)packed_size, plain, rows[index].raw_size, pd)) {
            if (packed) xx_mem_free(packed);
            if (plain) xx_mem_free(plain);
            goto done;
        }
        xx_mem_free(packed);
        member->memory = plain;
    }
    stream->size = span;
    ok = true;
done:
    xx_mem_free(rows);
    return ok;
}

static bool ppd_record(xx_archive_record_state *state)
{
    pm_stream *stream = (pm_stream *)state->internal_state;
    const pm_member *member = &stream->items[stream->index];
    if (!pm_record(state)) return false;
    state->current_record.header_offset = state->format->base_address;
    state->current_record.header_size = (int64_t)stream->items[0].offset - state->format->base_address;
    return xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_COMPRESSION_METHOD, 1U) &&
           xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size);
}

static xx_archive_record_state *ppd_create_records(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state = pm_create_records(format, options, pd);
    if (state && state->has_record && !ppd_record(state)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

static bool ppd_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!pm_next(format, state, pd)) return false;
    state->has_record = ppd_record(state);
    return state->has_record;
}

void xx_ppd_init(xx_ppd *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, PPD_FILE_TYPE, "ppd");
    archive->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_mime_type(&archive->format, "application/x-paranoid-productions-ppd");
    archive->format.create_archive_records_reading = ppd_create_records;
    archive->format.archive_record_move_to_next = ppd_next;
}

xx_ppd *xx_ppd_create(xx_io_device *device, int64_t base_address)
{
    xx_ppd *archive = (xx_ppd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ppd_init(archive, device, base_address);
    return archive;
}

void xx_ppd_destroy(xx_ppd *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ppd_free(xx_ppd *archive)
{
    if (!archive) return;
    xx_ppd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ppd_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_valid(format, pd);
}

bool xx_ppd_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_handle(format, pd);
}
