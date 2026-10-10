/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * ByteKiller/ANC/ACE/GRAC/McDisk/JEK: one listed and extracted payload.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bytekiller/xx_bytekiller.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xx_bytekiller_native.h"
#include <stdio.h>

#ifdef BYTEKILLER
#define XX_BYTEKILLER_TYPE XX_FILE_TYPE_BYTEKILLER
#else
#define XX_BYTEKILLER_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define BK_NAME "payload"

static bool bk_read_at(xx_io_device *device, int64_t offset, uint8_t *data, size_t size, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET)) return false;
    while (done < size) {
        ssize_t got;
        size_t amount = size - done;
        if (amount > 65536U) amount = 65536U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        got = xx_io_read(device, data + done, amount);
        if (got <= 0 || (size_t)got > amount) return false;
        done += (size_t)got;
    }
    return true;
}
static bool bk_write(xx_io_device *device, const uint8_t *data, size_t size, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (!device) return false;
    while (done < size) {
        ssize_t wrote;
        size_t amount = size - done;
        if (amount > 65536U) amount = 65536U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        wrote = xx_io_write(device, data + done, amount);
        if (wrote <= 0 || (size_t)wrote > amount) return false;
        done += (size_t)wrote;
    }
    return true;
}
static bool bk_load(Abstractformat *format, xx_io_device *destination, bk_context *context, xx_pd_struct *pd)
{
    int64_t saved, total;
    size_t size;
    uint8_t *input = NULL, *output = NULL;
    bk_context parsed;
    bool result = false;
    if (!format || !format->device || !context || format->base_address < 0 || destination == format->device || (pd && xx_pd_is_stopped(pd))) return false;
    saved = xx_io_tell(format->device);
    if (saved < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address || total - format->base_address < 12 || total - format->base_address > (int64_t)BK_MAX_PACKED + 16) goto done;
    size = (size_t)(total - format->base_address);
    input = (uint8_t *)xx_mem_alloc(size);
    if (!input || !bk_read_at(format->device, format->base_address, input, size, pd) || !bk_parse_native(input, size, &parsed)) goto done;
    output = (uint8_t *)xx_mem_alloc(parsed.raw_size);
    if (!output || !bk_decode_native(input, size, &parsed, output, parsed.raw_size, pd) || (destination && !bk_write(destination, output, parsed.raw_size, pd)))
        goto done;
    *context = parsed;
    result = true;
done:
    if (output) xx_mem_free(output);
    if (input) xx_mem_free(input);
    if (xx_io_seek64(format->device, saved, SEEK_SET)) result = false;
    return result && !(pd && xx_pd_is_stopped(pd));
}
static bool bk_options(xx_list_s *target, const xx_list_s *source)
{
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(target, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static const xx_var *bk_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}
static bool bk_record(xx_archive_record *record, const xx_bytekiller *archive)
{
    static const char *const names[] = {"ByteKiller", "ByteKiller Pro", "ACE", "ANC", "GRAC", "McDisk 1.0", "McDisk 1.1", "JEK"};
    if (!record || !archive || archive->variant > XX_BYTEKILLER_JEK) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = archive->format.base_address;
    record->header_size = 0;
    record->data_offset = archive->format.base_address;
    record->compressed_size = archive->format.format_size;
    return xx_archive_record_set_original_name(record, BK_NAME) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 1U) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, names[archive->variant]) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}
static void bk_vtable_destroy(Abstractformat *format)
{
    xx_bytekiller_destroy((xx_bytekiller *)format);
}
void xx_bytekiller_init(xx_bytekiller *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_BYTEKILLER_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-bytekiller");
    xx_format_set_extension(&archive->format, "bk");
    archive->format.check_is_valid = xx_bytekiller_check_is_valid;
    archive->format.handle_base_info = xx_bytekiller_handle_base_info;
    archive->format.get_format_size = xx_bytekiller_get_format_size;
    archive->format.get_number_of_archive_records = xx_bytekiller_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_bytekiller_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_bytekiller_get_current_archive_record;
    archive->format.archive_record_move_to_next = xx_bytekiller_archive_record_move_to_next;
    archive->format.unpack_current_archive_record = xx_bytekiller_unpack_current_archive_record;
    archive->format.free_archive_records_reading = xx_bytekiller_free_archive_records_reading;
    archive->format.destroy = bk_vtable_destroy;
}
xx_bytekiller *xx_bytekiller_create(xx_io_device *device, int64_t base_address)
{
    xx_bytekiller *archive = (xx_bytekiller *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_bytekiller_init(archive, device, base_address);
    return archive;
}
void xx_bytekiller_destroy(xx_bytekiller *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_bytekiller_free(xx_bytekiller *archive)
{
    if (archive) {
        xx_bytekiller_destroy(archive);
        xx_mem_free(archive);
    }
}
bool xx_bytekiller_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    bk_context context;
    return bk_load(self, NULL, &context, pd);
}
bool xx_bytekiller_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    bk_context context;
    xx_bytekiller *archive = (xx_bytekiller *)self;
    int64_t total, end;
    if (!self || !bk_load(self, NULL, &context, pd)) {
        if (self) {
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive->uncompressed_size = context.raw_size;
    archive->variant = (uint32_t)context.variant;
    self->format_size = (int64_t)context.packed_size + (context.variant == BK_PRO ? 4 : 0);
    self->number_of_archive_records = 1U;
    total = xx_io_total_size(self->device);
    end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1;
    self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}
int64_t xx_bytekiller_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_bytekiller_handle_base_info(self, pd)) ? self->format_size : -1;
}
uint64_t xx_bytekiller_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_bytekiller_handle_base_info(self, pd)) ? 1U : 0U;
}
xx_archive_record_state *xx_bytekiller_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    if (!self || (!self->base_info_handled && !xx_bytekiller_handle_base_info(self, pd))) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!bk_options(&state->options, options) || !bk_record(&state->current_record, (const xx_bytekiller *)self)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}
const xx_archive_record *xx_bytekiller_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_bytekiller_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}
bool xx_bytekiller_unpack_to_device(xx_bytekiller *archive, xx_io_device *destination, xx_pd_struct *pd)
{
    bk_context context;
    if (!archive || (!archive->format.base_info_handled && !xx_bytekiller_handle_base_info(&archive->format, pd))) return false;
    return bk_load(&archive->format, destination, &context, pd) && context.raw_size == archive->uncompressed_size && (uint32_t)context.variant == archive->variant;
}
bool xx_bytekiller_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_var *option;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL;
    xx_io_device *output = NULL;
    bool overwrite = false, ok = false;
    unsigned attempt;
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    option = bk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        bk_context context;
        return bk_load(self, NULL, &context, pd);
    }
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", BK_NAME) : xx_str_concat(base, BK_NAME);
    option = bk_option(&state->options, XX_META_ID_OPT_OVERWRITE);
    if (option) overwrite = xx_var_get_bool(option);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    stage = (char *)xx_mem_alloc(xx_str_len(path) + 48U);
    if (!stage) goto done;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (xx_rt_snprintf(stage, xx_str_len(path) + 48U, "%s.xxfc-bk-%u.tmp", path, attempt) <= 0) goto done;
        output = xx_io_file_open(stage, "wbx");
        if (output) break;
    }
    if (!output) goto done;
    ok = xx_bytekiller_unpack_to_device((xx_bytekiller *)self, output, pd);
    if (xx_io_close(output)) ok = false;
    output = NULL;
    if (ok && !(pd && xx_pd_is_stopped(pd))) ok = xx_io_file_replace_a(stage, path, overwrite);
    else ok = false;
done:
    if (output) {
        (void)xx_io_close(output);
        ok = false;
    }
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_mem_free(stage);
    }
    if (path) xx_str_free(path);
    if (owned) xx_str_free(owned);
    return ok;
}
void xx_bytekiller_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
