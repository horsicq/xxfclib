/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Knowledge Dynamics Corp. ".LIF" installer library.
 *
 * This is NOT Hewlett-Packard's LIF disk image format; nothing here looks at
 * an 0x8000 magic word.  A KDC LIF file has no archive header at all.  It is
 * a flat chain of members, each one a 54-byte header followed immediately by
 * that member's packed bytes:
 *
 *   offset size meaning
 *     0     8   ASCII hex, MS-DOS packed date/time (high 16 date, low 16 time)
 *     8     8   ASCII hex, packed (stored) size in bytes
 *    16     8   ASCII hex, original (uncompressed) size in bytes
 *    24     4   ASCII hex, CRC-16/CCITT-FALSE of the PACKED bytes
 *    28     4   ASCII hex, CRC-16/CCITT-FALSE of the ORIGINAL bytes
 *    32     2   ASCII hex, method: 1 = stored, 2 = LZD
 *    34    20   member name, ASCII, NUL padded
 *    54   ...   packed data, exactly "packed size" bytes
 *
 * The first 34 bytes are therefore a hex transcription of a 17-byte binary
 * header, read big-endian.  The next header starts at offset + 54 + packed
 * size with no padding of any kind.
 *
 * There is no signature, no directory and no end marker, so identification
 * has to be structural: every header in the chain must be well formed and the
 * chain must land exactly on end of file.  A file that ends mid-member is
 * rejected outright rather than truncated to the last good member -- a
 * partial match on a format this weakly marked is far more likely to be some
 * other file that happens to start with hex digits.
 *
 * Method 2 is Rahul Dhesi's LZD, the same variable-width LZW that ZOO uses
 * for its own method 1, so it forwards to xx_zoo_lzd_decode_memory().  See
 * lifkd_decode_member() for the one packaging difference.  Both CRC-16 fields
 * were confirmed against the corpus (1835 of 1836 members; the single holdout
 * is a genuinely damaged member whose packed CRC disagrees as well), so the
 * plaintext CRC is used as the unpack anchor.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lifkd/xx_lifkd.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zoo/xx_zoo.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The file type enumerator is added by the coordinator, not by this file, so
 * the constant is supplied locally until it lands.  Delete this block once
 * XX_FILE_TYPE_LIFKD exists in the enum. */
#ifdef LIFKD
#define XX_LIFKD_FILE_TYPE XX_FILE_TYPE_LIFKD
#else
#define XX_LIFKD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LIFKD_HEADER_SIZE 54
#define LIFKD_HEX_SIZE 34U
#define LIFKD_NAME_OFFSET 34U
#define LIFKD_NAME_SIZE 20U
#define LIFKD_METHOD_STORED 1U
#define LIFKD_METHOD_LZD 2U
/* A crafted file cannot be made to iterate forever: the walk rejects a zero
 * length step and stops at this many members regardless. */
#define LIFKD_MAX_MEMBERS 200000U

typedef struct lifkd_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t dos_time;
    uint16_t crc_packed;
    uint16_t crc_plain;
    uint8_t method;
} lifkd_member;

typedef struct lifkd_stream_s {
    lifkd_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} lifkd_stream;

static bool lifkd_read_at(xx_io_device *device, int64_t offset, void *buffer,
                          size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* All samples write lowercase, but nothing in the format forbids uppercase,
 * so both are accepted.  Anything else makes the header invalid. */
static bool lifkd_hex_digit(uint8_t c, uint32_t *value) {
    if (c >= '0' && c <= '9') {
        *value = (uint32_t)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *value = (uint32_t)(c - 'a') + 10U;
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *value = (uint32_t)(c - 'A') + 10U;
        return true;
    }
    return false;
}

static bool lifkd_hex_field(const uint8_t *bytes, unsigned count,
                            uint32_t *result) {
    uint32_t value = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index) {
        uint32_t digit;
        if (!lifkd_hex_digit(bytes[index], &digit)) return false;
        value = (value << 4U) | digit;
    }
    *result = value;
    return true;
}

/* Normalize only the filesystem-facing representation.  KDC names are short
 * 8.3 DOS names in the samples, but the field is 20 bytes wide and nothing
 * guarantees what a builder put there, so separators and traversal
 * components are made harmless here rather than trusted. */
static char *lifkd_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start ||
            (end - start == 1U && bytes[start] == '.')) continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool lifkd_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void lifkd_stream_free(void *opaque) {
    lifkd_stream *stream = (lifkd_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool lifkd_add_member(lifkd_stream *stream, const lifkd_member *member) {
    lifkd_member *grown;
    if (!stream || !member || stream->count >= LIFKD_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (lifkd_member *)xx_mem_realloc(stream->items,
                                           (stream->count + 1U) *
                                               sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/*
 * Walk the whole chain.  Every declared size is checked against the real
 * remaining file size before it is used to step, so a 54-byte header can
 * never move the cursor past end of file or ask for an allocation it has not
 * earned.  Success requires landing exactly on the end and finding at least
 * one member.
 */
static bool lifkd_parse(Abstractformat *format, lifkd_stream **result) {
    lifkd_stream *stream = NULL;
    int64_t total, size, cursor = 0;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < LIFKD_HEADER_SIZE) return false;
    stream = (lifkd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    while (cursor < size) {
        uint8_t header[LIFKD_HEADER_SIZE];
        lifkd_member member;
        uint32_t dos_time, packed, unpacked, crc_packed, crc_plain, method;
        size_t name_length = 0U;
        if (size - cursor < LIFKD_HEADER_SIZE ||
            !lifkd_read_at(format->device, format->base_address + cursor,
                           header, sizeof(header)) ||
            !lifkd_hex_field(header, 8U, &dos_time) ||
            !lifkd_hex_field(header + 8U, 8U, &packed) ||
            !lifkd_hex_field(header + 16U, 8U, &unpacked) ||
            !lifkd_hex_field(header + 24U, 4U, &crc_packed) ||
            !lifkd_hex_field(header + 28U, 4U, &crc_plain) ||
            !lifkd_hex_field(header + 32U, 2U, &method))
            goto fail;
        if (method != LIFKD_METHOD_STORED && method != LIFKD_METHOD_LZD)
            goto fail;
        /* A stored member cannot shrink or grow; anything else here means the
         * header is not really a LIF header. */
        if (method == LIFKD_METHOD_STORED && packed != unpacked) goto fail;
        /* The name field is NUL padded and must hold at least one printable
         * ASCII character before that padding. */
        while (name_length < LIFKD_NAME_SIZE &&
               header[LIFKD_NAME_OFFSET + name_length] != 0U)
            ++name_length;
        if (name_length == 0U) goto fail;
        {
            size_t index;
            for (index = 0U; index < name_length; ++index) {
                uint8_t c = header[LIFKD_NAME_OFFSET + index];
                if (c < 0x20U || c >= 0x7fU) goto fail;
            }
        }
        /* Bound the declared packed size against what is really left before
         * it is used for anything at all. */
        if ((int64_t)packed > size - cursor - LIFKD_HEADER_SIZE) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.header_offset = format->base_address + cursor;
        member.data_offset = member.header_offset + LIFKD_HEADER_SIZE;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = unpacked;
        member.dos_time = dos_time;
        member.crc_packed = (uint16_t)crc_packed;
        member.crc_plain = (uint16_t)crc_plain;
        member.method = (uint8_t)method;
        member.name = lifkd_normalize_name(header + LIFKD_NAME_OFFSET,
                                           name_length);
        if (!member.name) goto fail;
        if (!lifkd_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        /* LIFKD_HEADER_SIZE is a non-zero constant, so the step is always
         * positive and the walk always terminates. */
        cursor += LIFKD_HEADER_SIZE + (int64_t)packed;
    }
    if (cursor != size || stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    lifkd_stream_free(stream);
    return false;
}

static bool lifkd_copy_options(xx_list_s *destination,
                               const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *lifkd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lifkd_set_record(xx_archive_record *record,
                             const lifkd_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = LIFKD_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           /* There is no 16-bit CRC metadata id, so the plaintext CRC-16
            * travels in the CRC32 slot zero extended, the way the other
            * CRC-16 readers in this library report theirs. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc_plain) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          (member->dos_time >> 16U) & 0xffffU) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time & 0xffffU) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/*
 * Decode one member into a freshly allocated buffer.
 *
 * Method 2 is LZD and forwards to the ZOO method 1 decoder.  One packaging
 * quirk: the KDC encoder sometimes emits a single extra zero byte after the
 * byte that carried the LZD end code, and xx_zoo_lzd_decode_memory()
 * deliberately insists that the input be consumed exactly.  Rather than
 * loosen that decoder for every caller, retry once here with the trailing
 * zero dropped.  Only a trailing ZERO is dropped and only one of them: any
 * other trailing content still fails, and the plaintext CRC-16 below is what
 * actually decides whether the decode was right.  In the corpus this affects
 * 250 of 1622 compressed members.
 */
static bool lifkd_decode_member(Abstractformat *format,
                                const lifkd_member *member, uint8_t **plain,
                                size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->packed_size < 0 ||
        member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    if (member->method == LIFKD_METHOD_STORED &&
        (uint64_t)member->packed_size != member->unpacked_size)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size
                                         : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !lifkd_read_at(format->device, member->data_offset, packed,
                        (size_t)member->packed_size)))
        goto fail;
    if (xx_crc16_ccitt_calc(0xffffU, packed, (size_t)member->packed_size) !=
        member->crc_packed)
        goto fail;
    if (member->method == LIFKD_METHOD_STORED) {
        if (output_size != 0U) xx_mem_copy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else {
        size_t input_size = (size_t)member->packed_size;
        decoded = xx_zoo_lzd_decode_memory(packed, input_size, output,
                                           output_size, &written);
        if (!decoded && input_size != 0U && packed[input_size - 1U] == 0U)
            decoded = xx_zoo_lzd_decode_memory(packed, input_size - 1U, output,
                                               output_size, &written);
    }
    if (!decoded || written != output_size ||
        xx_crc16_ccitt_calc(0xffffU, output, written) != member->crc_plain)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_lifkd_init(xx_lifkd *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* The header is ASCII hex text, so there is no endianness to speak of;
     * the decoded 17-byte header is read most significant nibble first. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_LIFKD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-kdc-lif");
    xx_format_set_extension(&archive->format, "lif");
    archive->format.check_is_valid = xx_lifkd_check_is_valid;
    archive->format.handle_base_info = xx_lifkd_handle_base_info;
    archive->format.get_format_size = xx_lifkd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lifkd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lifkd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lifkd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lifkd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lifkd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lifkd_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_lifkd *xx_lifkd_create(xx_io_device *device, int64_t base_address) {
    xx_lifkd *archive = (xx_lifkd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lifkd_init(archive, device, base_address);
    return archive;
}

void xx_lifkd_destroy(xx_lifkd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lifkd_free(xx_lifkd *archive) {
    if (!archive) return;
    xx_lifkd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lifkd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    lifkd_stream *stream;
    (void)pd;
    if (!lifkd_parse(format, &stream)) return false;
    lifkd_stream_free(stream);
    return true;
}

bool xx_lifkd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lifkd_stream *stream;
    xx_lifkd *archive;
    (void)pd;
    if (!format || !lifkd_parse(format, &stream)) return false;
    archive = (xx_lifkd *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    lifkd_stream_free(stream);
    return true;
}

int64_t xx_lifkd_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lifkd_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_lifkd_get_number_of_archive_records(Abstractformat *format,
                                                xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lifkd_handle_base_info(format, pd))
               ? ((xx_lifkd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_lifkd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lifkd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!lifkd_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lifkd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lifkd_stream_free;
    state->total_records = stream->count;
    if (!lifkd_copy_options(&state->options, options) ||
        !lifkd_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lifkd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_lifkd_archive_record_move_to_next(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    lifkd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lifkd_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = lifkd_set_record(&state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lifkd_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    lifkd_stream *stream;
    lifkd_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (lifkd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!lifkd_safe_output_name(member->name) ||
        !lifkd_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = lifkd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_lifkd_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
