/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Zoom - an Amiga floppy imager (magic "ZOM5"). The original label-free
 * path publishes disk.adf. USEHEADER records contain separate 512-byte sector
 * data and 16-byte sector-label planes and publish both disk.adf and
 * disk.adf.labels. The scoped labelled path accepts dense masks, cylinders
 * 0..79 and no attached text/note, and authenticates all three stored CRCs.
 * Multi-byte fields are big-endian. The codec owns the whole container;
 * published records therefore refer to the same complete compressed extent.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/filesystem/zoom/xx_zoom.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zoom/xx_zoom.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_ZOOM_HEADER_SIZE 0x4C
#define XX_ZOOM_CHUNK_HEADER_SIZE 42
#define XX_ZOOM_CYLINDER_SIZE 0x2C00
/* The cylinder numbers are single bytes, so the range can never be wider. */
#define XX_ZOOM_MAX_CYLINDERS 256
/* A Zoom container images a floppy.  Anything past this is not one, and the
 * codec takes the whole file in memory, so the ceiling is also the allocation
 * bound. */
#define XX_ZOOM_MAX_INPUT ((int64_t)64 * 1024 * 1024)
#define XX_ZOOM_PAYLOAD_NAME "disk.adf"
#define XX_ZOOM_LABEL_NAME "disk.adf.labels"
#define XX_ZOOM_LABEL_CYLINDER_SIZE (22U * 16U)
#define XX_ZOOM_READER_OVERHEAD 16384U
#define XX_ZOOM_CODEC_WORKSPACE 262144U

typedef struct xx_zoom_context_s {
    int64_t input_size;
    int64_t chunks_offset;
    int64_t uncompressed_size;
    uint8_t first_cylinder;
    uint8_t last_cylinder;
    bool is_protected;
    bool use_header;
    bool labels_record;
    int64_t label_size;
} xx_zoom_context;

static void xx_zoom_vtable_destroy(Abstractformat *self);
static const xx_var *xx_zoom_get_option(const xx_list_s *options, uint32_t meta_id);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zoom_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;
    int64_t saved;
    bool ok = true;
    if (!self || !self->device || offset < 0 || (!buffer && size != 0U)) return false;
    saved = xx_io_tell(self->device);
    if (xx_io_seek64(self->device, offset, SEEK_SET) != 0) ok = false;
    while (ok && completed < size) {
        ssize_t received = xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) ok = false;
        else completed += (size_t)received;
    }
    /* Retain the original seek/read contract for legacy devices without tell.
     * The labelled path requires tell separately before whole-file decoding. */
    if (saved >= 0 && xx_io_seek64(self->device, saved, SEEK_SET) != 0) ok = false;
    return ok;
}

static size_t xx_zoom_selected_size(const xx_zoom_context *context)
{
    return (size_t)(context->labels_record ? context->label_size : context->uncompressed_size);
}

static const char *xx_zoom_selected_name(const xx_zoom_context *context)
{
    return context->labels_record ? XX_ZOOM_LABEL_NAME : XX_ZOOM_PAYLOAD_NAME;
}

static uint64_t xx_zoom_budget(Abstractformat *self, const xx_list_s *options, uint32_t id)
{
    const xx_var *v = xx_format_resolve_extra_parameter(self, NULL, id);
    uint64_t limit = v ? xx_var_get_u64(v) : UINT64_MAX;
    v = xx_zoom_get_option(options, id);
    if (v && xx_var_get_u64(v) < limit) limit = xx_var_get_u64(v);
    return limit;
}

static bool xx_zoom_ready(Abstractformat *self, const xx_zoom_context *context, const xx_list_s *options, xx_pd_struct *pd)
{
    uint64_t selected;
    uint64_t charge;
    if (!self || !context || (pd && xx_pd_is_stopped(pd))) return false;
    if (!context->use_header) return true;
    if (xx_io_tell(self->device) < 0) return false;
    selected = (uint64_t)xx_zoom_selected_size(context);
    charge = XX_ZOOM_READER_OVERHEAD + (uint64_t)context->input_size + selected + XX_ZOOM_CODEC_WORKSPACE;
    return selected <= xx_zoom_budget(self, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
           charge <= xx_zoom_budget(self, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

/* --------------------------------------------------------------- parse -- */

static bool xx_zoom_parse(Abstractformat *self, xx_zoom_context *context, xx_pd_struct *pd)
{
    uint8_t header[XX_ZOOM_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t note_size;
    int64_t chunks_offset;
    int64_t cylinders;

    if (context) xx_mem_zero(context, sizeof(*context));
    if (!self || !self->device || !context || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < (int64_t)(XX_ZOOM_HEADER_SIZE + XX_ZOOM_CHUNK_HEADER_SIZE)) {
        return false;
    }
    if (!xx_zoom_read_at(self, self->base_address, header, sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, "ZOM5", 4U) != 0 || header[6] != 5U) return false;
    if (header[5] < header[4]) return false;

    /* The note block is skipped whole: N bytes of note plus a four-byte
     * checksum.  It has to fit inside the real file - the codec cannot check
     * that for a caller who hands it a short slice. */
    context->use_header = (xx_data_get_u32(header + 8, 4, 0, true) & 1U) != 0U;
    if (context->use_header && (header[4] != 0U || header[5] >= 80U ||
        xx_io_tell(self->device) < 0 || xx_data_get_u32(header + 24, 4, 0, true) != 0U ||
        xx_data_get_u32(header + 28, 4, 0, true) != 0U || xx_data_get_u32(header + 32, 4, 0, true) != 0U)) return false;
    note_size = (int64_t)xx_data_get_u32(header + 0x1c, 4, 0, true);
    chunks_offset = XX_ZOOM_HEADER_SIZE;
    if (note_size != 0) {
        if (note_size > span - (int64_t)XX_ZOOM_HEADER_SIZE - 4) return false;
        chunks_offset += note_size + 4;
    }
    if (chunks_offset >= span) return false;

    cylinders = (int64_t)header[5] - (int64_t)header[4] + 1;
    if (cylinders <= 0 || cylinders > XX_ZOOM_MAX_CYLINDERS) return false;

    context->input_size = span;
    context->chunks_offset = chunks_offset;
    context->first_cylinder = header[4];
    context->last_cylinder = header[5];
    context->is_protected = header[0x24] != 0U;
    context->uncompressed_size = cylinders * (int64_t)XX_ZOOM_CYLINDER_SIZE;
    context->label_size = context->use_header ? cylinders * (int64_t)XX_ZOOM_LABEL_CYLINDER_SIZE : 0;
    return true;
}

/* -------------------------------------------------------------- decode -- */

/*
 * The codec owns the whole container, so the reader's job is only to get the
 * bytes into memory and to size the output.  xx_zoom_scan_memory() runs the
 * full decode with the product discarded, which is why the measurement and the
 * decode cannot disagree; the image size the header implies is used as the
 * allocation bound rather than as the answer.
 */
static bool xx_zoom_read_container(Abstractformat *self, const xx_zoom_context *context, uint8_t **out, size_t *out_size)
{
    uint8_t *input;
    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !context || !out || !out_size || context->input_size <= 0 || context->input_size > XX_ZOOM_MAX_INPUT ||
        (uint64_t)context->input_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    input = (uint8_t *)xx_mem_alloc((size_t)context->input_size);
    if (!input) return false;
    if (!xx_zoom_read_at(self, self->base_address, input, (size_t)context->input_size)) {
        xx_mem_free(input);
        return false;
    }
    *out = input;
    *out_size = (size_t)context->input_size;
    return true;
}

static bool xx_zoom_decode(Abstractformat *self, const xx_zoom_context *context, const xx_list_s *options, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size = 0U;
    size_t produced = 0U;
    size_t written = 0U;

    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !context || !out || !out_size || context->uncompressed_size <= 0 || (uint64_t)context->uncompressed_size > (uint64_t)SIZE_MAX ||
        !xx_zoom_ready(self, context, options, pd) || !xx_zoom_read_container(self, context, &input, &input_size)) {
        return false;
    }
    produced = xx_zoom_selected_size(context);
    if (!xx_zoom_ready(self, context, options, pd) || (!context->use_header &&
        (!xx_zoom_scan_memory(input, input_size, produced, NULL, &written) || written != produced))) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc(produced);
    if (!output || !xx_zoom_ready(self, context, options, pd) ||
        !(context->labels_record ? xx_zoom_decode_labels_memory(input, input_size, output, produced, &written) :
                                  xx_zoom_decode_memory(input, input_size, output, produced, &written)) ||
        written != produced || !xx_zoom_ready(self, context, options, pd)) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = produced;
    return true;
}

static bool xx_zoom_recover_prefix(Abstractformat *self, const xx_zoom_context *context, const xx_list_s *options, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size = 0U;
    size_t written = 0U;
    bool complete = false;

    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !context || !out || !out_size || context->uncompressed_size <= 0 || (uint64_t)context->uncompressed_size > (uint64_t)SIZE_MAX ||
        !xx_zoom_ready(self, context, options, pd) || !xx_zoom_read_container(self, context, &input, &input_size)) {
        return false;
    }
    if (!xx_zoom_ready(self, context, options, pd)) { xx_mem_free(input); return false; }
    output = (uint8_t *)xx_mem_alloc(xx_zoom_selected_size(context));
    if (!output ||
        !(context->labels_record ? xx_zoom_decode_labels_prefix_memory(input, input_size, output, xx_zoom_selected_size(context), &written, &complete) :
                                  xx_zoom_decode_prefix_memory(input, input_size, output, xx_zoom_selected_size(context), &written, &complete)) ||
        complete || written == 0U || !xx_zoom_ready(self, context, options, pd)) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;
}

/* ------------------------------------------------------------ lifetime -- */

void xx_zoom_init(xx_zoom *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    /* Registration pending: xxfc_defs.h is shared and out of scope here, so
     * the file type stays generic until XX_FILE_TYPE_ZOOM lands. */
    archive->format.file_type = XX_FILE_TYPE_ZOOM;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zoom-adf");
    xx_format_set_extension(&archive->format, "zom");
    xx_format_set_version(&archive->format, "5");
    archive->format.check_is_valid = xx_zoom_check_is_valid;
    archive->format.handle_base_info = xx_zoom_handle_base_info;
    archive->format.get_format_size = xx_zoom_get_format_size;
    archive->format.get_number_of_archive_records = xx_zoom_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_zoom_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_zoom_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_zoom_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_zoom_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_zoom_free_archive_records_reading;
    archive->format.destroy = xx_zoom_vtable_destroy;
    archive->chunks_offset = -1;
    archive->uncompressed_size = -1;
}

xx_zoom *xx_zoom_create(xx_io_device *device, int64_t base_address)
{
    xx_zoom *archive = (xx_zoom *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_zoom_init(archive, device, base_address);
    return archive;
}

void xx_zoom_destroy(xx_zoom *archive)
{
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->chunks_offset = -1;
    archive->uncompressed_size = -1;
}

static void xx_zoom_vtable_destroy(Abstractformat *self)
{
    xx_zoom_destroy((xx_zoom *)self);
}

void xx_zoom_free(xx_zoom *archive)
{
    if (!archive) return;
    xx_zoom_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_zoom_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_zoom_context context;
    /* Header only, as in the reference: running the codec here would decode a
     * whole floppy to answer a detector question. */
    return xx_zoom_parse(self, &context, pd);
}

bool xx_zoom_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_zoom *archive = (xx_zoom *)self;
    xx_zoom_context context;

    if (!self) return false;
    self->base_info_handled = true;
    if (!xx_zoom_parse(self, &context, pd)) {
        self->is_valid = false;
        self->format_size = 0;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_crypted = false;
        archive->number_of_records = 0U;
        archive->chunks_offset = -1;
        archive->uncompressed_size = -1;
        archive->is_protected = false;
        return false;
    }
    /* The chunk records run to the end of the file and are not walked here,
     * so the whole span is the format - exactly what the reference reports. */
    self->is_valid = true;
    self->format_size = context.input_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = context.use_header ? 2U : 1U;
    self->is_crypted = context.is_protected;
    archive->number_of_records = self->number_of_archive_records;
    archive->chunks_offset = context.chunks_offset;
    archive->uncompressed_size = context.uncompressed_size;
    archive->first_cylinder = context.first_cylinder;
    archive->last_cylinder = context.last_cylinder;
    archive->is_protected = context.is_protected;
    return true;
}

int64_t xx_zoom_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zoom_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zoom *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zoom_set_record(xx_archive_record *record, Abstractformat *self, const xx_zoom_context *context)
{
    if (!record || !self || !context) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_ZOOM_HEADER_SIZE;
    /* The whole container is the record's stream: the chunk index lives in
     * the records themselves and no member extent exists. */
    record->data_offset = self->base_address;
    record->compressed_size = context->input_size;
    return xx_archive_record_set_original_name(record, xx_zoom_selected_name(context)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)context->input_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)xx_zoom_selected_size(context)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, context->is_protected);
}

static bool xx_zoom_copy_options(xx_list_s *target, const xx_list_s *options)
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

static const xx_var *xx_zoom_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_zoom_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_zoom_context *context;

    if (!self || !self->device) return NULL;
    context = (xx_zoom_context *)xx_mem_calloc(1U, sizeof(*context));
    if (!context) return NULL;
    if (!xx_zoom_parse(self, context, pd)) {
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
    state->total_records = context->use_header ? 2 : 1;
    if (!xx_zoom_copy_options(&state->options, options) || !xx_zoom_set_record(&state->current_record, self, context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zoom_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_zoom_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    {
        xx_zoom_context *context = (xx_zoom_context *)state->internal_state;
        if (context && context->use_header && !context->labels_record) {
            context->labels_record = true;
            if (!xx_zoom_set_record(&state->current_record, self, context)) { state->has_record = false; return false; }
            state->current_index = 1;
            return true;
        }
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_zoom_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_zoom_context *context;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;
    bool partial = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    context = (const xx_zoom_context *)state->internal_state;
    if (!context) return false;

    path_option = xx_zoom_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the container
         * without writing anything. */
        result = xx_zoom_decode(self, context, &state->options, &plain, &plain_size, pd);
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
        target_path = xx_str_concat3(base_path, "/", xx_zoom_selected_name(context));
    } else {
        target_path = xx_str_concat(base_path, xx_zoom_selected_name(context));
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    if (!xx_zoom_decode(self, context, &state->options, &plain, &plain_size, pd)) {
        if (!xx_zoom_recover_prefix(self, context, &state->options, &plain, &plain_size, pd)) {
            xx_str_free(target_path);
            return false;
        }
        partial = true;
    }
    {
        xx_io_device *output = xx_zoom_ready(self, context, &state->options, pd) ? xx_io_file_open(target_path, "wb") : NULL;
        created = output != NULL;
        size_t completed = 0U;
        result = output != NULL;
        while (result && completed < plain_size) {
            size_t step = plain_size - completed;
            ssize_t sent;
            if (!xx_zoom_ready(self, context, &state->options, pd)) { result = false; break; }
            if (step > 65536U) step = 65536U;
            sent = xx_io_write(output, plain + completed, step);
            if (sent <= 0 || (size_t)sent > step) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (!xx_zoom_ready(self, context, &state->options, pd)) result = false;
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    /* Keep a committed prefix for salvage, but report the missing remainder. */
    return result && !partial;
}

void xx_zoom_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

int64_t xx_zoom_get_uncompressed_size(const xx_zoom *archive)
{
    return archive ? archive->uncompressed_size : -1;
}

bool xx_zoom_get_is_protected(const xx_zoom *archive)
{
    return archive ? archive->is_protected : false;
}
