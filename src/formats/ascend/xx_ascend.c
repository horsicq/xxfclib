/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ascend compressed files.
 *
 *   0..11  six u16 LE: year, month, day, hour, minute, second
 *   12..   a PKWARE DCL stream, whose first two bytes are the literal mode
 *          and the dictionary size in bits
 *
 * There is no magic, no stored name, no stored size and no checksum, so the
 * gate has to be built out of what is there. Two things must hold together:
 *
 *   the twelve bytes must be a real calendar date and time -- month 0, the
 *   thirty-first of February and hour 24 are all rejected, which is what makes
 *   twelve bytes of integers into evidence;
 *
 *   and the DCL stream must decode to its end marker having consumed exactly
 *   the bytes between offset 12 and the end of the file.
 *
 * Either alone is weak. A date is only six bounded integers, and DCL has no
 * signature of its own. Together they are specific: the reference reports this
 * pair matching all 32 members of its corpus and nothing else across a
 * 4,573-file mixed-format sweep.
 *
 * The trial decode is also the only way to learn the decoded size, since the
 * container stores none -- which is why xx_dcl_scan_memory exists.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ascend/xx_ascend.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_ASCEND_HEADER_SIZE 12
/* A DCL stream is at least its two header bytes plus one coded symbol. */
#define XX_ASCEND_MIN_STREAM_SIZE 3
#define XX_ASCEND_MAX_ARCHIVE_SIZE ((int64_t)64 * 1024 * 1024)
#define XX_ASCEND_MAX_UNPACKED_SIZE ((int64_t)512 * 1024 * 1024)
#define XX_ASCEND_MIN_YEAR 1980U
#define XX_ASCEND_MAX_YEAR 2100U
#define XX_ASCEND_MAX_LITERAL_MODE 1U
#define XX_ASCEND_MIN_DICTIONARY_BITS 4U
#define XX_ASCEND_MAX_DICTIONARY_BITS 6U
/* The member name is not stored anywhere; see the header. */
#define XX_ASCEND_MEMBER_NAME "ascend_data"

typedef struct xx_ascend_context_s {
    uint64_t uncompressed_size;
    int64_t compressed_size;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t literal_mode;
    uint8_t dictionary_bits;
} xx_ascend_context;

static void xx_ascend_vtable_destroy(Abstractformat *self);

static bool xx_ascend_read_at(Abstractformat *self, int64_t offset,
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

static uint16_t xx_ascend_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/*
 * A real calendar date, leap years included. This is doing the work a magic
 * number would otherwise do, so it rejects rather than clamps: 1900-02-29 is
 * not a date, and a file claiming it is not an Ascend file.
 */
static bool xx_ascend_date_is_valid(uint16_t year, uint16_t month,
                                    uint16_t day, uint16_t hour,
                                    uint16_t minute, uint16_t second) {
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30,
                                              31, 31, 30, 31, 30, 31};
    uint8_t limit;

    if (year < XX_ASCEND_MIN_YEAR || year > XX_ASCEND_MAX_YEAR) return false;
    if (month < 1U || month > 12U) return false;
    if (hour > 23U || minute > 59U || second > 59U) return false;

    limit = days_in_month[month - 1U];
    if (month == 2U) {
        bool leap = (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
        if (leap) limit = 29U;
    }
    return day >= 1U && day <= limit;
}

/*
 * Validate, and measure the payload. The decode is the expensive half, so the
 * cheap structural checks run first -- a file that is not Ascend is almost
 * always rejected before anything is read into memory.
 */
static bool xx_ascend_probe(Abstractformat *self, xx_ascend_context *context,
                            xx_pd_struct *pd) {
    uint8_t prefix[XX_ASCEND_HEADER_SIZE + 2];
    uint8_t *packed = NULL;
    uint16_t year, month, day, hour, minute, second;
    int64_t total;
    int64_t span;
    int64_t compressed;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool result = false;

    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_ASCEND_HEADER_SIZE + XX_ASCEND_MIN_STREAM_SIZE ||
        span > XX_ASCEND_MAX_ARCHIVE_SIZE) {
        return false;
    }
    /* The date and the two DCL header bytes come in one read, so the cheap
     * gate runs before the payload is pulled into memory. */
    if (!xx_ascend_read_at(self, self->base_address, prefix, sizeof(prefix))) {
        return false;
    }

    year = xx_ascend_u16(prefix);
    month = xx_ascend_u16(prefix + 2);
    day = xx_ascend_u16(prefix + 4);
    hour = xx_ascend_u16(prefix + 6);
    minute = xx_ascend_u16(prefix + 8);
    second = xx_ascend_u16(prefix + 10);
    if (!xx_ascend_date_is_valid(year, month, day, hour, minute, second)) {
        return false;
    }
    if (prefix[XX_ASCEND_HEADER_SIZE] > XX_ASCEND_MAX_LITERAL_MODE ||
        prefix[XX_ASCEND_HEADER_SIZE + 1] < XX_ASCEND_MIN_DICTIONARY_BITS ||
        prefix[XX_ASCEND_HEADER_SIZE + 1] > XX_ASCEND_MAX_DICTIONARY_BITS) {
        return false;
    }

    compressed = span - XX_ASCEND_HEADER_SIZE;
    if (compressed <= 0 || (uint64_t)compressed > (uint64_t)SIZE_MAX) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)compressed);
    if (!packed) return false;
    if (!xx_ascend_read_at(self, self->base_address + XX_ASCEND_HEADER_SIZE,
                           packed, (size_t)compressed) ||
        (pd && xx_pd_is_stopped(pd))) {
        goto cleanup;
    }
    /* The decisive test: the stream must decode AND finish exactly at the end
     * of the file. A stream that stops short leaves trailing bytes no Ascend
     * writer would produce. */
    if (!xx_dcl_scan_memory(packed, (size_t)compressed,
                            (size_t)XX_ASCEND_MAX_UNPACKED_SIZE, &consumed,
                            &produced) ||
        consumed != (size_t)compressed || produced < 1U) {
        goto cleanup;
    }

    context->uncompressed_size = produced;
    context->compressed_size = compressed;
    context->year = year;
    context->month = (uint8_t)month;
    context->day = (uint8_t)day;
    context->hour = (uint8_t)hour;
    context->minute = (uint8_t)minute;
    context->second = (uint8_t)second;
    context->literal_mode = prefix[XX_ASCEND_HEADER_SIZE];
    context->dictionary_bits = prefix[XX_ASCEND_HEADER_SIZE + 1];
    result = true;

cleanup:
    xx_mem_free(packed);
    return result;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ascend_init(xx_ascend *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ASCEND;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ascend");
    xx_format_set_extension(&archive->format, "in!");
    archive->format.check_is_valid = xx_ascend_check_is_valid;
    archive->format.handle_base_info = xx_ascend_handle_base_info;
    archive->format.get_format_size = xx_ascend_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ascend_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ascend_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ascend_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ascend_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ascend_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ascend_free_archive_records_reading;
    archive->format.destroy = xx_ascend_vtable_destroy;
}

xx_ascend *xx_ascend_create(xx_io_device *device, int64_t base_address) {
    xx_ascend *archive = (xx_ascend *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ascend_init(archive, device, base_address);
    return archive;
}

void xx_ascend_destroy(xx_ascend *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
}

void xx_ascend_free(xx_ascend *archive) {
    if (!archive) return;
    xx_ascend_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ascend_vtable_destroy(Abstractformat *self) {
    xx_ascend_destroy((xx_ascend *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ascend_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ascend_context context;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_ascend_probe(self, &context, pd);
}

bool xx_ascend_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ascend *archive = (xx_ascend *)self;
    xx_ascend_context context;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    if (!xx_ascend_probe(self, &context, pd)) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = XX_ASCEND_HEADER_SIZE + context.compressed_size;
    self->number_of_archive_records = 1U;
    archive->uncompressed_size = context.uncompressed_size;
    archive->year = context.year;
    archive->month = context.month;
    archive->day = context.day;
    archive->hour = context.hour;
    archive->minute = context.minute;
    archive->second = context.second;
    archive->literal_mode = context.literal_mode;
    archive->dictionary_bits = context.dictionary_bits;
    return true;
}

int64_t xx_ascend_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ascend_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? 1U : 0U;
}

/* ------------------------------------------------------------- records -- */

/*
 * Decode the single member. The caller already knows the size from
 * handle_base_info, so this allocates exactly that and refuses anything else:
 * a decode that produces a different length than the scan did means the file
 * changed underneath, and continuing would write whatever happened to fit.
 */
static bool xx_ascend_decode(Abstractformat *self, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    xx_ascend_context context;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;

    *out = NULL;
    *out_size = 0U;
    if (!xx_ascend_probe(self, &context, pd)) return false;
    if (context.uncompressed_size > (uint64_t)SIZE_MAX) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)context.compressed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)context.uncompressed_size);
    if (!packed || !plain) goto cleanup;
    if (!xx_ascend_read_at(self, self->base_address + XX_ASCEND_HEADER_SIZE,
                           packed, (size_t)context.compressed_size)) {
        goto cleanup;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)context.compressed_size, plain,
                              (size_t)context.uncompressed_size, &written) ||
        written != (size_t)context.uncompressed_size) {
        goto cleanup;
    }
    *out = plain;
    *out_size = written;
    plain = NULL;
    result = true;

cleanup:
    xx_mem_free(plain);
    xx_mem_free(packed);
    return result;
}

static bool xx_ascend_set_record(Abstractformat *self,
                                 xx_archive_record *record) {
    xx_ascend *archive = (xx_ascend *)self;

    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = XX_ASCEND_HEADER_SIZE;
    record->data_offset = self->base_address + XX_ASCEND_HEADER_SIZE;
    record->compressed_size = self->format_size - XX_ASCEND_HEADER_SIZE;
    return xx_archive_record_set_original_name(record,
                                               XX_ASCEND_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ascend_copy_options(xx_list_s *target,
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

static const xx_var *xx_ascend_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ascend_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_ascend_copy_options(&state->options, options) ||
        !xx_ascend_set_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_ascend_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ascend_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_ascend_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
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
    path_option = xx_ascend_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_ascend_decode(self, &plain, &plain_size, pd);
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
        target_path = xx_str_concat3(base_path, "/", XX_ASCEND_MEMBER_NAME);
    } else {
        target_path = xx_str_concat(base_path, XX_ASCEND_MEMBER_NAME);
    }
    xx_str_free(converted_path);
    if (!target_path || !xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    if (!xx_ascend_decode(self, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_ascend_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_ascend_get_uncompressed_size(const xx_ascend *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

uint8_t xx_ascend_get_dictionary_bits(const xx_ascend *archive) {
    return archive ? archive->dictionary_bits : 0U;
}
