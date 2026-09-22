/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Recognita OCR distribution archive (.CMP).  Layout
 * ported from XArchive's documents/xrecognita.cpp.
 *
 * There is no archive header at all: the file is a chain of members, each
 * introduced by a 25-byte record and followed immediately by its payload.
 *
 *   0x00  13  DOS 8.3 file name, NUL terminated
 *   0x0D  4   uncompressed size (LE, signed in the producer)
 *   0x11  2   DOS time
 *   0x13  2   DOS date
 *   0x15  4   absolute file offset of the NEXT member header; for the last
 *             member this is end of file, which is what terminates the chain
 *
 * The payload is a PKWARE DCL imploded stream, so it opens with the two DCL
 * parameter bytes: literal mode 0x00 and dictionary size 0x06 (4 KiB).  Since
 * the container carries no magic of its own, detection rests on the first
 * record only - a strict 8.3 name, a positive size, a valid DOS date/time and
 * that DCL parameter pair - plus the requirement that the chain lands exactly
 * on end of file.  Later records are deliberately held to the looser rule:
 * applying the name test to every record rejected real archives over a member
 * with a two-character extension.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/recognita/xx_recognita.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef RECOGNITA
#define XX_RECOGNITA_FILE_TYPE XX_FILE_TYPE_RECOGNITA
#else
#define XX_RECOGNITA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RECOGNITA_HEADER_SIZE 25
#define RECOGNITA_NAME_SIZE 13
#define RECOGNITA_DCL_LITERAL 0x00U
#define RECOGNITA_DCL_DICT 0x06U
#define RECOGNITA_MAX_MEMBERS 65536U
/* The corpus tops out at a 1.2 MB archive expanding to a few megabytes; this
 * only bounds a corrupt size field. */
#define RECOGNITA_MAX_UNPACKED INT64_C(0x10000000)

typedef struct recognita_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint16_t dos_time;
    uint16_t dos_date;
} recognita_member;

typedef struct recognita_stream_s {
    recognita_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} recognita_stream;

static uint16_t recognita_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t recognita_le32(const uint8_t *bytes) {
    return (uint32_t)recognita_le16(bytes) |
           ((uint32_t)recognita_le16(bytes + 2U) << 16U);
}

static bool recognita_read_at(xx_io_device *device, int64_t offset,
                              void *buffer, size_t size) {
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

/* The reference extractor's own name test: a strict DOS 8.3 name, at least
 * one character before the single dot and exactly three after it.  This is
 * the main thing standing between a headerless container and a false
 * positive, so it stays strict. */
static bool recognita_is_valid_raw_name(const uint8_t *raw) {
    int before_dot = 0, dots = 0, after_dot = 0;
    int index;
    for (index = 0; index < RECOGNITA_NAME_SIZE; ++index) {
        uint8_t c = raw[index];
        if (index == 12) {
            /* A 12-character name would leave no room for the terminator. */
            if (c != 0U) return false;
            break;
        }
        if (c == 0U) break;
        if (c < 0x20U || c > 0x7FU) return false;
        if (c == (uint8_t)'.') ++dots;
        else if (dots == 0) ++before_dot;
        else ++after_dot;
    }
    return before_dot >= 1 && dots == 1 && after_dot == 3;
}

static bool recognita_is_valid_dos_date_time(uint16_t dos_date,
                                             uint16_t dos_time) {
    static const uint32_t days_in_month[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                               31U, 31U, 30U, 31U, 30U, 31U};
    uint32_t hour, minute, second, month, day, year, limit;
    if (dos_date == 0U && dos_time == 0U) return false;
    hour = (uint32_t)(dos_time >> 11U);
    minute = (uint32_t)((dos_time >> 5U) & 0x3FU);
    second = (uint32_t)((dos_time & 0x1FU) * 2U);
    if (hour >= 24U || minute >= 60U || second >= 60U) return false;
    month = (uint32_t)((dos_date >> 5U) & 0x0FU);
    day = (uint32_t)(dos_date & 0x1FU);
    if (month < 1U || month > 12U || day < 1U || day > 31U) return false;
    year = (uint32_t)(dos_date >> 9U) + 1980U;
    limit = days_in_month[month - 1U];
    if (month == 2U) {
        bool leap = (year % 4U) == 0U &&
                    ((year % 100U) != 0U || (year % 400U) == 0U);
        if (leap) limit = 29U;
    }
    return day <= limit;
}

/* Bytes that cannot appear in a path are percent escaped, exactly as the
 * reference names its output files. */
static char *recognita_name_to_string(const uint8_t *raw, size_t index) {
    static const char hex[] = "0123456789ABCDEF";
    char *name;
    size_t length = 0U, at, output = 0U;
    while (length < (size_t)RECOGNITA_NAME_SIZE && raw[length] != 0U)
        ++length;
    while (length != 0U && raw[length - 1U] == (uint8_t)' ') --length;
    name = (char *)xx_mem_alloc((size_t)RECOGNITA_NAME_SIZE * 3U + 16U);
    if (!name) return NULL;
    for (at = 0U; at < length; ++at) {
        uint8_t c = raw[at];
        bool safe = c > 0x20U && c < 0x7FU && c != (uint8_t)'%' &&
                    c != (uint8_t)'/' && c != (uint8_t)'\\' &&
                    c != (uint8_t)':' && c != (uint8_t)'*' &&
                    c != (uint8_t)'?' && c != (uint8_t)'"' &&
                    c != (uint8_t)'<' && c != (uint8_t)'>' &&
                    c != (uint8_t)'|';
        if (safe) {
            name[output++] = (char)c;
        } else {
            name[output++] = '%';
            name[output++] = hex[(c >> 4U) & 0x0FU];
            name[output++] = hex[c & 0x0FU];
        }
    }
    if (output == 0U) {
        static const char prefix[] = "record";
        char digits[24];
        size_t count = 0U, value = index, back;
        xx_rt_memcpy(name, prefix, sizeof(prefix) - 1U);
        output = sizeof(prefix) - 1U;
        do {
            digits[count++] = (char)('0' + (int)(value % 10U));
            value /= 10U;
        } while (value != 0U && count < sizeof(digits));
        for (back = 0U; back < count; ++back)
            name[output++] = digits[count - 1U - back];
    }
    name[output] = 0;
    return name;
}

static bool recognita_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '/' || c == '\\' || c < 0x20U)
            return false;
    }
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return false;
    return true;
}

static void recognita_stream_free(void *opaque) {
    recognita_stream *stream = (recognita_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool recognita_add_member(recognita_stream *stream,
                                 const recognita_member *member) {
    recognita_member *grown;
    if (!stream || !member || stream->count >= RECOGNITA_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (recognita_member *)xx_mem_realloc(stream->items,
                                               (stream->count + 1U) *
                                                   sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool recognita_parse(Abstractformat *format,
                            recognita_stream **result) {
    recognita_stream *stream = NULL;
    int64_t total, size, offset;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* Header plus the two DCL parameter bytes is the shortest archive. */
    if (size < RECOGNITA_HEADER_SIZE + 2) return false;
    stream = (recognita_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    offset = 0;
    while (offset < size) {
        uint8_t header[RECOGNITA_HEADER_SIZE + 2];
        recognita_member member;
        int64_t unpacked, next_offset, data_offset;
        uint16_t dos_time, dos_date;
        bool first = stream->count == 0U;
        if (size - offset < (int64_t)sizeof(header) ||
            !recognita_read_at(format->device, format->base_address + offset,
                               header, sizeof(header))) goto fail;
        unpacked = (int64_t)(int32_t)recognita_le32(header + 13U);
        dos_time = recognita_le16(header + 17U);
        dos_date = recognita_le16(header + 19U);
        next_offset = (int64_t)(int32_t)recognita_le32(header + 21U);
        /* These are the DETECTION rules and belong to the first record
         * only. */
        if (first) {
            if (!recognita_is_valid_raw_name(header) || unpacked <= 0 ||
                !recognita_is_valid_dos_date_time(dos_date, dos_time) ||
                header[25] != RECOGNITA_DCL_LITERAL ||
                header[26] != RECOGNITA_DCL_DICT) goto fail;
        } else if (unpacked < 0) {
            goto fail;
        }
        if (unpacked > RECOGNITA_MAX_UNPACKED) goto fail;
        data_offset = offset + RECOGNITA_HEADER_SIZE;
        /* The next-member pointer is the only thing sizing the payload, so
         * it is bounded against the real file before it is used. */
        if (next_offset <= data_offset || next_offset > size) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = recognita_name_to_string(header, stream->count);
        if (!member.name) goto fail;
        member.header_offset = format->base_address + offset;
        member.data_offset = format->base_address + data_offset;
        member.packed_size = next_offset - data_offset;
        member.unpacked_size = (uint64_t)unpacked;
        member.dos_time = dos_time;
        member.dos_date = dos_date;
        if (!recognita_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        offset = next_offset;
    }
    /* The chain has to end exactly at end of file. */
    if (stream->count == 0U || offset != size) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    recognita_stream_free(stream);
    return false;
}

static bool recognita_copy_options(xx_list_s *destination,
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

static const xx_var *recognita_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool recognita_set_record(xx_archive_record *record,
                                 const recognita_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = RECOGNITA_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_TIMESTAMP,
               ((uint64_t)member->dos_date << 16U) | member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool recognita_decode(Abstractformat *format,
                             const recognita_member *member, uint8_t **plain,
                             size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U, output_size;
    if (!format || !member || !plain || !plain_size ||
        member->packed_size <= 0 ||
        member->unpacked_size > (uint64_t)SIZE_MAX) return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        !recognita_read_at(format->device, member->data_offset, packed,
                           (size_t)member->packed_size)) goto fail;
    if (!xx_dcl_decode_memory(packed, (size_t)member->packed_size, output,
                              output_size, &written) ||
        written != output_size) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_recognita_init(xx_recognita *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RECOGNITA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-recognita");
    xx_format_set_extension(&archive->format, "cmp");
    archive->format.check_is_valid = xx_recognita_check_is_valid;
    archive->format.handle_base_info = xx_recognita_handle_base_info;
    archive->format.get_format_size = xx_recognita_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_recognita_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_recognita_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_recognita_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_recognita_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_recognita_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_recognita_free_archive_records_reading;
}

xx_recognita *xx_recognita_create(xx_io_device *device, int64_t base_address) {
    xx_recognita *archive = (xx_recognita *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_recognita_init(archive, device, base_address);
    return archive;
}

void xx_recognita_destroy(xx_recognita *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_recognita_free(xx_recognita *archive) {
    if (!archive) return;
    xx_recognita_destroy(archive);
    xx_mem_free(archive);
}

bool xx_recognita_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    recognita_stream *stream;
    (void)pd;
    if (!recognita_parse(format, &stream)) return false;
    recognita_stream_free(stream);
    return true;
}

bool xx_recognita_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    recognita_stream *stream;
    xx_recognita *archive;
    (void)pd;
    if (!format || !recognita_parse(format, &stream)) return false;
    archive = (xx_recognita *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    recognita_stream_free(stream);
    return true;
}

int64_t xx_recognita_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_recognita_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_recognita_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_recognita_handle_base_info(format, pd))
               ? ((xx_recognita *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_recognita_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    recognita_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!recognita_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        recognita_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = recognita_stream_free;
    state->total_records = stream->count;
    if (!recognita_copy_options(&state->options, options) ||
        !recognita_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_recognita_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_recognita_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    recognita_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (recognita_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = recognita_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_recognita_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    recognita_stream *stream;
    recognita_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (recognita_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!recognita_safe_output_name(member->name) ||
        !recognita_decode(format, member, &plain, &plain_size)) goto done;
    path_option = recognita_option(&state->options,
                                   XX_META_ID_OPT_UNPACK_PATH);
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
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
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

void xx_recognita_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
