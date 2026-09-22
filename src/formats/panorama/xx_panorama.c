/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Panorama - a RAR 4.x archive under a whole-file, position-keyed XOR.
 *
 * There is no container: the file IS the RAR, with every byte transformed
 * against a 1024-byte pad generated from one 32-bit seed by the Borland LCG
 * multiplier 0x8088405.  Detection is therefore by KNOWN PLAINTEXT - the
 * plaintext has to start with the seven-byte RAR 4.x signature - and that same
 * test is what pins the seed.  xx_panorama_seed_from_header() in
 * algo/panorama does both; this reader never reimplements it.
 *
 * Because the pad phase is indexed by the ABSOLUTE FILE OFFSET, the transform
 * is only defined for a whole file starting at offset 0.  A Panorama stream
 * nested at a non-zero base address would need a phase this codec does not
 * accept, so the reader refuses any base address other than 0 rather than
 * producing garbage.
 *
 * One record is published, covering the whole file, named "archive.rar".  The
 * reference names it after the source file's base name; xxfclib devices carry
 * no path, so a fixed name is used instead - see the port report.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/panorama/xx_panorama.h"

#include "xxfclib/algo/panorama/xx_panorama.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Pending registration in xxfc_defs.h.  Once the enumerator
 * XX_FILE_TYPE_PANORAMA and its short alias PANORAMA are added there this
 * fallback switches itself off. */
#ifndef PANORAMA
#define XX_FILE_TYPE_PANORAMA XX_FILE_TYPE_UNKNOWN
#endif

/** The known-plaintext test needs eight bytes; the reference also refuses
 *  anything shorter than a plausible RAR. */
#define XX_PANORAMA_HEADER_SIZE 8
#define XX_PANORAMA_MIN_SIZE 32

/** The whole file is deciphered in memory (the codec has no streaming entry
 *  point), so this is the allocation ceiling a hostile input can reach. */
#define XX_PANORAMA_MAX_SIZE ((int64_t)512 * 1024 * 1024)

/** The plaintext is always a RAR; the reference names the record after the
 *  source file, which an xx_io_device does not expose. */
#define XX_PANORAMA_MEMBER_NAME "archive.rar"

typedef struct xx_panorama_context_s {
    int64_t input_size;
    uint32_t seed;
} xx_panorama_context;

static void xx_panorama_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_panorama_read_at(xx_io_device *device, int64_t offset,
                                uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received = xx_io_read(device, buffer + completed,
                                      size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_panorama_write_all(xx_io_device *device, const uint8_t *data,
                                  size_t size, xx_pd_struct *pd) {
    size_t completed = 0U;

    if (!device || (!data && size != 0U)) return false;
    while (completed < size) {
        ssize_t sent;
        if (pd && xx_pd_is_stopped(pd)) return false;
        sent = xx_io_write(device, data + completed, size - completed);
        if (sent <= 0 || (size_t)sent > size - completed) return false;
        completed += (size_t)sent;
    }
    return true;
}

/* Validate the container and recover the seed.  Nothing is allocated here, so
 * a caller may run it on a stack context and simply drop it. */
static bool xx_panorama_parse(Abstractformat *self,
                              xx_panorama_context *context,
                              xx_pd_struct *pd) {
    uint8_t header[XX_PANORAMA_HEADER_SIZE];
    int64_t total;
    uint32_t seed = 0U;

    if (context) xx_mem_zero(context, sizeof(*context));
    if (!self || !self->device || !context || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* The pad phase is the absolute file offset, so only a whole file makes
     * sense.  An embedded Panorama stream is not representable. */
    if (self->base_address != 0) return false;

    total = xx_io_total_size(self->device);
    if (total < XX_PANORAMA_MIN_SIZE || total > XX_PANORAMA_MAX_SIZE) {
        return false;
    }
    if (!xx_panorama_read_at(self->device, 0, header, sizeof(header))) {
        return false;
    }
    if (!xx_panorama_seed_from_header(header, sizeof(header), &seed)) {
        return false;
    }
    context->input_size = total;
    context->seed = seed;
    return true;
}

/* Decipher the whole file.  @p destination may be NULL, which verifies the
 * transform without writing anything. */
static bool xx_panorama_decode(Abstractformat *self,
                               xx_io_device *destination,
                               xx_pd_struct *pd) {
    xx_panorama_context context;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t size;
    bool result = false;

    if (!xx_panorama_parse(self, &context, pd)) return false;
    if ((uint64_t)context.input_size > (uint64_t)SIZE_MAX / 2U) return false;
    size = (size_t)context.input_size;

    input = (uint8_t *)xx_mem_alloc(size);
    output = (uint8_t *)xx_mem_alloc(size);
    if (!input || !output) goto cleanup;
    if (!xx_panorama_read_at(self->device, 0, input, size)) goto cleanup;
    if (pd && xx_pd_is_stopped(pd)) goto cleanup;
    /* The seed is already validated, so the _seed form is the right call: it
     * performs no second signature check. */
    if (!xx_panorama_decode_memory_seed(input, size, context.seed, output,
                                        size, &written) ||
        written != size) {
        goto cleanup;
    }
    if (destination && !xx_panorama_write_all(destination, output, size, pd)) {
        goto cleanup;
    }
    result = true;
cleanup:
    xx_mem_free(output);
    xx_mem_free(input);
    return result;
}

static bool xx_panorama_copy_options(xx_list_s *target,
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

static const xx_var *xx_panorama_get_option(const xx_list_s *options,
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

static bool xx_panorama_set_record(xx_archive_record *record,
                                   const xx_panorama *archive) {
    if (!record || !archive || archive->archive_size < 0) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* There is no header: the first byte of the file is already payload. */
    record->header_offset = 0;
    record->header_size = 0;
    record->data_offset = 0;
    record->compressed_size = archive->archive_size;
    return xx_archive_record_set_original_name(record,
                                               XX_PANORAMA_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)archive->archive_size) &&
           /* Length preserving: the two sizes are the same number. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)archive->archive_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           /* The XOR is obfuscation with a recovered key, not encryption the
            * caller has to supply a password for. */
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_panorama_init(xx_panorama *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PANORAMA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-panorama");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_panorama_check_is_valid;
    archive->format.handle_base_info = xx_panorama_handle_base_info;
    archive->format.get_format_size = xx_panorama_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_panorama_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_panorama_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_panorama_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_panorama_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_panorama_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_panorama_free_archive_records_reading;
    archive->format.destroy = xx_panorama_vtable_destroy;
    archive->archive_size = -1;
}

xx_panorama *xx_panorama_create(xx_io_device *device, int64_t base_address) {
    xx_panorama *archive = (xx_panorama *)xx_mem_alloc(sizeof(*archive));

    if (archive) xx_panorama_init(archive, device, base_address);
    return archive;
}

void xx_panorama_destroy(xx_panorama *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->seed = 0U;
    archive->archive_size = -1;
}

static void xx_panorama_vtable_destroy(Abstractformat *self) {
    xx_panorama_destroy((xx_panorama *)self);
}

void xx_panorama_free(xx_panorama *archive) {
    if (!archive) return;
    xx_panorama_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_panorama_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_panorama_context context;

    return xx_panorama_parse(self, &context, pd);
}

bool xx_panorama_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_panorama *archive = (xx_panorama *)self;
    xx_panorama_context context;

    if (!self) return false;
    if (!xx_panorama_parse(self, &context, pd)) {
        archive->number_of_records = 0U;
        archive->seed = 0U;
        archive->archive_size = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive->seed = context.seed;
    archive->archive_size = context.input_size;
    archive->number_of_records = 1U;
    self->format_size = context.input_size;
    /* The cipher covers every byte, so there is never an overlay. */
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->file_type = XX_FILE_TYPE_PANORAMA;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_panorama_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_panorama_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_panorama *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

xx_archive_record_state *xx_panorama_create_archive_records_reading(
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
    if (!xx_panorama_copy_options(&state->options, options) ||
        !xx_panorama_set_record(&state->current_record,
                                (const xx_panorama *)self)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_panorama_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_panorama_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* Exactly one record; the walk is over as soon as it starts. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_panorama_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    bool result;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_option = xx_panorama_get_option(&state->options,
                                         XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decipher and discard, which verifies the member
         * without writing anything. */
        return xx_panorama_decode(self, NULL, pd);
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
        target_path = xx_str_concat3(base_path, "/", XX_PANORAMA_MEMBER_NAME);
    } else {
        target_path = xx_str_concat(base_path, XX_PANORAMA_MEMBER_NAME);
    }
    xx_str_free(converted_path);
    if (!target_path || !xx_store_create_dirs_a(target_path, false)) {
        if (target_path) xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        result = output && xx_panorama_decode(self, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_panorama_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

uint32_t xx_panorama_get_seed(const xx_panorama *archive) {
    return archive ? archive->seed : 0U;
}

int64_t xx_panorama_get_archive_size(const xx_panorama *archive) {
    return archive ? archive->archive_size : -1;
}
