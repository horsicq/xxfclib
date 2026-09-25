/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SFPack (.sfpack) - a SoundFont 2 compressor.
 *
 * SFPACK IS NOT AN ARCHIVE.  It stores the pieces of ONE .sf2 (RIFF/sfbk) - an
 * INFO list, a pdta list and one compressed stream per sample - and the codec
 * REBUILDS that single file: the sample data is laid out afresh, every shdr
 * record's dwStart / dwEnd / dwStartloop / dwEndloop is rewritten and 46 zero
 * bytes follow each sample.  So this reader publishes exactly ONE member and
 * hands the whole file to xx_sfpack_decode_memory().
 *
 * Container header
 *   +0x00  4    "SFPK"
 *   +0x04  u16  version, 0x0100
 *   +0x06  u16  flags.  Bit 2 means ENCRYPTED and the codec refuses it, so it
 *                is refused here too rather than listed as extractable; bits
 *                0/1 announce an embedded .txt/.lic blob in the skipped
 *                region and are harmless.
 *   +0x08  i32  the size of the .sf2 that will be produced.  DO NOT TRUST IT:
 *                most corpus files declare a few hundred bytes more than what
 *                is actually written.
 *   +0x0c  u32  zero
 *   +0x10       chunk("INFO"), chunk("pdta"), i32 skipLen + skipLen bytes,
 *               i32 tableBytes, tableBytes/4 * i32 absolute sample offsets.
 *
 * THE SAMPLE TABLE HOLDS ABSOLUTE FILE OFFSETS, so the codec owns the whole
 * file: there is no sub-stream to publish and no trailing member could be
 * distinguished from the container.  The whole span is therefore the format
 * size and the record's stream.
 *
 * THE UNCOMPRESSED SIZE IS MEASURED, never taken from +0x08 - that is what
 * xx_sfpack_scan_memory() exists for.  Measuring decompresses the pdta chunk
 * only, so check_is_valid() deliberately does not do it: detection stops at
 * the header.  A measure that succeeds does not promise a decode that does,
 * because the sample streams are not walked.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfpack/xx_sfpack.h"

#include "xxfclib/algo/sfpack/xx_sfpack.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SFPACK_HEADER_SIZE 0x10
/* "SFPK", the version, the flags, the declared size and the zero word, plus
 * the first chunk header the layout promises. */
#define XX_SFPACK_MIN_SIZE 0x20
#define XX_SFPACK_VERSION 0x0100U
#define XX_SFPACK_FLAG_ENCRYPTED 0x0004U
/* The codec takes the whole file in memory and the reference refuses anything
 * past 2 GiB outright; this is the reader's own, tighter allocation bound. */
#define XX_SFPACK_MAX_INPUT ((int64_t)512 * 1024 * 1024)
#define XX_SFPACK_PAYLOAD_NAME "soundfont.sf2"

typedef struct xx_sfpack_context_s {
    int64_t input_size;
    int64_t uncompressed_size; /**< measured; -1 when not measured */
    int64_t declared_size;
    uint16_t version;
    uint16_t flags;
} xx_sfpack_context;

static void xx_sfpack_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_sfpack_read16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_sfpack_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_sfpack_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;
    if (!self || !self->device || offset < 0 || (!buffer && size != 0U) ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_sfpack_read_container(Abstractformat *self,
                                     const xx_sfpack_context *context,
                                     uint8_t **out, size_t *out_size) {
    uint8_t *input;
    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !context || !out || !out_size || context->input_size <= 0 ||
        context->input_size > XX_SFPACK_MAX_INPUT ||
        (uint64_t)context->input_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    input = (uint8_t *)xx_mem_alloc((size_t)context->input_size);
    if (!input) return false;
    if (!xx_sfpack_read_at(self, self->base_address, input,
                           (size_t)context->input_size)) {
        xx_mem_free(input);
        return false;
    }
    *out = input;
    *out_size = (size_t)context->input_size;
    return true;
}

/* --------------------------------------------------------------- parse -- */

/*
 * @p measure decides whether the pdta chunk is decompressed to get the real
 * output size.  The detector path passes false, because an .sfpack is a sample
 * bank and pulling a few hundred megabytes into memory to answer "is this an
 * SFPack?" is not a trade a detector may make.
 */
static bool xx_sfpack_parse(Abstractformat *self, xx_sfpack_context *context,
                            bool measure, xx_pd_struct *pd) {
    uint8_t header[XX_SFPACK_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t declared;

    if (context) {
        xx_mem_zero(context, sizeof(*context));
        context->uncompressed_size = -1;
    }
    if (!self || !self->device || !context || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < (int64_t)XX_SFPACK_MIN_SIZE || span > XX_SFPACK_MAX_INPUT) {
        return false;
    }
    if (!xx_sfpack_read_at(self, self->base_address, header, sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, "SFPK", 4U) != 0) return false;
    if (xx_sfpack_read16(header + 4) != XX_SFPACK_VERSION) return false;
    /* The reserved word is a real discriminator: four bytes of magic plus a
     * version would still match too much. */
    if (xx_sfpack_read32(header + 0x0c) != 0U) return false;

    declared = (int64_t)(int32_t)xx_sfpack_read32(header + 8);
    if (declared <= 0) return false;

    context->input_size = span;
    context->declared_size = declared;
    context->version = xx_sfpack_read16(header + 4);
    context->flags = xx_sfpack_read16(header + 6);
    /* Encrypted containers are refused rather than listed: the codec will not
     * produce them and a record that can never extract is worse than none. */
    if ((context->flags & XX_SFPACK_FLAG_ENCRYPTED) != 0U) return false;

    if (measure) {
        uint8_t *input = NULL;
        size_t input_size = 0U;
        size_t produced = 0U;
        bool measured;
        if (!xx_sfpack_read_container(self, context, &input, &input_size)) {
            return false;
        }
        measured = xx_sfpack_scan_memory(input, input_size, 0U, NULL,
                                         &produced) &&
                   produced != 0U && (uint64_t)produced <= (uint64_t)INT64_MAX;
        xx_mem_free(input);
        if (!measured || (pd && xx_pd_is_stopped(pd))) return false;
        context->uncompressed_size = (int64_t)produced;
    }
    return true;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_sfpack_decode(Abstractformat *self,
                             const xx_sfpack_context *context, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size = 0U;
    size_t produced = 0U;
    size_t written = 0U;

    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !context || !out || !out_size ||
        context->uncompressed_size <= 0 ||
        (uint64_t)context->uncompressed_size > (uint64_t)SIZE_MAX ||
        (pd && xx_pd_is_stopped(pd)) ||
        !xx_sfpack_read_container(self, context, &input, &input_size)) {
        return false;
    }
    produced = (size_t)context->uncompressed_size;
    output = (uint8_t *)xx_mem_alloc(produced);
    if (!output ||
        !xx_sfpack_decode_memory(input, input_size, output, produced,
                                 &written) ||
        written != produced) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = produced;
    return true;
}

/* ------------------------------------------------------------ lifetime -- */

void xx_sfpack_init(xx_sfpack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    /* Registration pending: xxfc_defs.h is shared and out of scope here, so
     * the file type stays generic until XX_FILE_TYPE_SFPACK lands. */
    archive->format.file_type = XX_FILE_TYPE_SFPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sfpack");
    xx_format_set_extension(&archive->format, "sfpack");
    archive->format.check_is_valid = xx_sfpack_check_is_valid;
    archive->format.handle_base_info = xx_sfpack_handle_base_info;
    archive->format.get_format_size = xx_sfpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfpack_free_archive_records_reading;
    archive->format.destroy = xx_sfpack_vtable_destroy;
    archive->uncompressed_size = -1;
    archive->declared_size = -1;
}

xx_sfpack *xx_sfpack_create(xx_io_device *device, int64_t base_address) {
    xx_sfpack *archive = (xx_sfpack *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfpack_init(archive, device, base_address);
    return archive;
}

void xx_sfpack_destroy(xx_sfpack *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->uncompressed_size = -1;
    archive->declared_size = -1;
}

static void xx_sfpack_vtable_destroy(Abstractformat *self) {
    xx_sfpack_destroy((xx_sfpack *)self);
}

void xx_sfpack_free(xx_sfpack *archive) {
    if (!archive) return;
    xx_sfpack_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_sfpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sfpack_context context;
    return xx_sfpack_parse(self, &context, false, pd);
}

bool xx_sfpack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sfpack *archive = (xx_sfpack *)self;
    xx_sfpack_context context;

    if (!self) return false;
    self->base_info_handled = true;
    /* Measured here, not in the detector: the record cannot publish a size the
     * declared field is known to get wrong. */
    if (!xx_sfpack_parse(self, &context, true, pd)) {
        self->is_valid = false;
        self->format_size = 0;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        archive->number_of_records = 0U;
        archive->uncompressed_size = -1;
        archive->declared_size = -1;
        return false;
    }
    self->is_valid = true;
    /* Absolute sample offsets: the codec owns the whole file, so there is no
     * overlay a trailing format could claim. */
    self->format_size = context.input_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    archive->number_of_records = 1U;
    archive->uncompressed_size = context.uncompressed_size;
    archive->declared_size = context.declared_size;
    archive->version = context.version;
    archive->flags = context.flags;
    return true;
}

int64_t xx_sfpack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_sfpack_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_sfpack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_sfpack_set_record(xx_archive_record *record,
                                 Abstractformat *self,
                                 const xx_sfpack_context *context) {
    if (!record || !self || !context || context->uncompressed_size <= 0) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_SFPACK_HEADER_SIZE;
    record->data_offset = self->base_address;
    record->compressed_size = context->input_size;
    return xx_archive_record_set_original_name(record,
                                               XX_SFPACK_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->input_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)context->uncompressed_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_sfpack_copy_options(xx_list_s *target,
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

static const xx_var *xx_sfpack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_sfpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_sfpack_context *context;

    if (!self || !self->device) return NULL;
    context = (xx_sfpack_context *)xx_mem_calloc(1U, sizeof(*context));
    if (!context) return NULL;
    if (!xx_sfpack_parse(self, context, true, pd)) {
        xx_mem_free(context);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(context);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = context;
    state->free_internal = xx_mem_free;
    state->total_records = 1;
    if (!xx_sfpack_copy_options(&state->options, options) ||
        !xx_sfpack_set_record(&state->current_record, self, context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sfpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfpack_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* One rebuilt .sf2 and nothing else; there is no second member. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_sfpack_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    const xx_sfpack_context *context;
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
    context = (const xx_sfpack_context *)state->internal_state;
    if (!context) return false;

    path_option =
        xx_sfpack_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: rebuild and discard, which verifies the container
         * without writing anything.  A measure alone would not - the sample
         * streams are only walked by the decode. */
        result = xx_sfpack_decode(self, context, &plain, &plain_size, pd);
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
        target_path = xx_str_concat3(base_path, "/", XX_SFPACK_PAYLOAD_NAME);
    } else {
        target_path = xx_str_concat(base_path, XX_SFPACK_PAYLOAD_NAME);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_sfpack_decode(self, context, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_sfpack_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

int64_t xx_sfpack_get_uncompressed_size(const xx_sfpack *archive) {
    return archive ? archive->uncompressed_size : -1;
}

int64_t xx_sfpack_get_declared_size(const xx_sfpack *archive) {
    return archive ? archive->declared_size : -1;
}
