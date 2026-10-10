/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation from the published Apple Pascal layout.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apple_pascal/xx_apple_pascal.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include "xxfclib/data/xx_data.h"
#ifdef APPLE_PASCAL
#define PASCAL_TYPE XX_FILE_TYPE_APPLE_PASCAL
#else
#define PASCAL_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define PASCAL_BLOCK 512U
#define PASCAL_DIRECTORY 2048U
#define PASCAL_ENTRY 26U
#define PASCAL_FILES 77U
#define PASCAL_FLOPPY 143360U
#define PASCAL_COPY 65536U
/* Pascal/ProDOS logical half-block -> DOS logical sector, within one track. */
static const uint8_t pascal_dos_sector[16] = {0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15};
typedef struct pascal_member_s {
    char name[48];
    uint32_t offset;
    uint32_t size;
    uint16_t type;
    uint16_t date;
    uint16_t slot;
} pascal_member;
typedef struct pascal_view_s {
    xx_io_device *device;
    int64_t base;
    uint32_t bytes;
    uint16_t blocks;
    uint16_t declared;
    char volume_name[8];
    xx_apple_pascal_order order;
    pascal_member members[PASCAL_FILES];
    size_t count;
    size_t index;
} pascal_view;
static bool pascal_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size, xx_pd_struct *pd)
{
    int64_t saved;
    size_t done = 0U;
    bool ok = false;
    if (!device || (!buffer && size) || offset < 0) return false;
    saved = xx_io_tell(device);
    if (saved < 0) return false;
    if (xx_io_seek64(device, offset, SEEK_SET) == 0) {
        while (done < size && !(pd && xx_pd_is_stopped(pd))) {
            ssize_t got = xx_io_read(device, (uint8_t *)buffer + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    return ok && !(pd && xx_pd_is_stopped(pd));
}
static uint32_t pascal_physical(xx_apple_pascal_order order, uint32_t offset)
{
    if (order == XX_APPLE_PASCAL_ORDER_DOS) {
        uint32_t half = offset / 256U;
        return (half / 16U) * 4096U + pascal_dos_sector[half % 16U] * 256U + (offset % 256U);
    }
    return offset;
}
static bool pascal_read(const pascal_view *view, uint32_t offset, void *buffer, size_t size, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (offset > view->bytes || size > view->bytes - offset) return false;
    while (done < size) {
        uint32_t at = offset + (uint32_t)done;
        size_t part = 256U - at % 256U;
        if (part > size - done) part = size - done;
        if (!pascal_read_at(view->device, view->base + pascal_physical(view->order, at), (uint8_t *)buffer + done, part, pd)) return false;
        done += part;
    }
    return true;
}
static char pascal_fold(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}
static bool pascal_device_name(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t i, k, stem = 0U;
    while (name[stem] && name[stem] != '.') ++stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        for (k = 0U; k < stem; ++k)
            if (!devices[i][k] || pascal_fold(name[k]) != devices[i][k]) break;
        if (k == stem && !devices[i][k]) return true;
    }
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((pascal_fold(name[0]) == 'C' && pascal_fold(name[1]) == 'O' && pascal_fold(name[2]) == 'M') ||
            (pascal_fold(name[0]) == 'L' && pascal_fold(name[1]) == 'P' && pascal_fold(name[2]) == 'T'));
}
static bool pascal_equal(const char *left, const char *right)
{
    while (*left && *right && pascal_fold(*left) == pascal_fold(*right)) {
        ++left;
        ++right;
    }
    return !*left && !*right;
}
static bool pascal_safe_name(const uint8_t *name, size_t length, char *output)
{
    size_t i, prefix = 0U;
    if (!length || length > 15U) return false;
    for (i = 0U; i < length; ++i) {
        unsigned char c = name[i];
        if (c < 0x20U || c > 0x7EU) return false;
        output[i] = c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' ? '_' : (char)c;
    }
    for (i = length; i && (output[i - 1U] == '.' || output[i - 1U] == ' '); --i) output[i - 1U] = '_';
    output[length] = 0;
    if (pascal_device_name(output)) prefix = 1U;
    if (prefix) {
        for (i = length + 1U; i; --i) output[i] = output[i - 1U];
        output[0] = '_';
    }
    return true;
}
static bool pascal_parse_order(Abstractformat *self, xx_apple_pascal_order order, pascal_view *view, xx_pd_struct *pd)
{
    uint8_t directory[PASCAL_DIRECTORY];
    uint32_t previous_end = 6U;
    size_t i, n;
    int64_t total;
    if (!self || !self->device || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || total - self->base_address < 6 * PASCAL_BLOCK) return false;
    xx_mem_zero(view, sizeof(*view));
    view->device = self->device;
    view->base = self->base_address;
    view->bytes = 6U * PASCAL_BLOCK;
    view->order = order;
    if (order == XX_APPLE_PASCAL_ORDER_DOS) {
        if (total - self->base_address < PASCAL_FLOPPY) return false;
        view->bytes = PASCAL_FLOPPY;
    }
    if (!pascal_read(view, 2U * PASCAL_BLOCK, directory, sizeof(directory), pd)) return false;
    view->blocks = xx_data_get_u16(directory + 14, 2, 0, false);
    view->declared = xx_data_get_u16(directory + 16, 2, 0, false);
    n = directory[6];
    if (xx_data_get_u16(directory, 2, 0, false) != 0U || xx_data_get_u16(directory + 2, 2, 0, false) != 6U || xx_data_get_u16(directory + 4, 2, 0, false) != 0U || !n ||
        n > 7U || view->blocks < 6U || view->declared > PASCAL_FILES || (order == XX_APPLE_PASCAL_ORDER_DOS && view->blocks != 280U) ||
        (uint64_t)view->blocks * PASCAL_BLOCK > (uint64_t)(total - view->base))
        return false;
    for (i = 0U; i < n; ++i) {
        unsigned char c = directory[7U + i];
        if (c < 0x21U || c > 0x7EU || c == '=' || c == '$' || c == '?' || c == ',') return false;
        view->volume_name[i] = (char)c;
    }
    view->volume_name[n] = 0;
    view->bytes = (uint32_t)view->blocks * PASCAL_BLOCK;
    for (i = 0U; i < view->declared; ++i) {
        const uint8_t *entry = directory + (i + 1U) * PASCAL_ENTRY;
        uint32_t first = xx_data_get_u16(entry, 2, 0, false), end = xx_data_get_u16(entry + 2, 2, 0, false);
        uint16_t type = xx_data_get_u16(entry + 4, 2, 0, false), last = xx_data_get_u16(entry + 22, 2, 0, false);
        uint16_t date = xx_data_get_u16(entry + 24, 2, 0, false);
        pascal_member member;
        unsigned attempt;
        char original[32];
        size_t j;
        if ((pd && xx_pd_is_stopped(pd)) || first < previous_end || end <= first || end > view->blocks || last > PASCAL_BLOCK || (type & 0xFU) > 7U ||
            !pascal_safe_name(entry + 7, entry[6], original))
            return false;
        previous_end = end;
        /* These entries do not name accessible regular files at boot. */
        if ((type & 0xFU) == 1U || last == 0U || (date >> 9) >= 100U) continue;
        xx_mem_zero(&member, sizeof(member));
        for (attempt = 0U; attempt <= PASCAL_FILES; ++attempt) {
            bool taken = false;
            if (!attempt) xx_rt_snprintf(member.name, sizeof(member.name), "%s", original);
            else xx_rt_snprintf(member.name, sizeof(member.name), "%s~%u", original, attempt + 1U);
            for (j = 0U; j < view->count; ++j)
                if (pascal_equal(member.name, view->members[j].name)) {
                    taken = true;
                    break;
                }
            if (!taken) break;
        }
        if (attempt > PASCAL_FILES) return false;
        member.offset = first * PASCAL_BLOCK;
        member.size = (end - first - 1U) * PASCAL_BLOCK + last;
        member.type = type;
        member.date = date;
        member.slot = (uint16_t)(i + 1U);
        view->members[view->count++] = member;
    }
    /* Undeleted slots beyond the declared count are outside this directory;
     * a live first unused slot exposes an inconsistent count, not a new file. */
    if (view->declared < PASCAL_FILES && directory[(view->declared + 1U) * PASCAL_ENTRY + 6U]) return false;
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pascal_parse(Abstractformat *self, pascal_view *view, xx_pd_struct *pd)
{
    xx_apple_pascal *volume = (xx_apple_pascal *)self;
    pascal_view *other;
    bool block_ok, dos_ok;
    if (!self) return false;
    if (volume->requested_order == XX_APPLE_PASCAL_ORDER_BLOCK || volume->requested_order == XX_APPLE_PASCAL_ORDER_DOS)
        return pascal_parse_order(self, volume->requested_order, view, pd);
    if (volume->requested_order != XX_APPLE_PASCAL_ORDER_AUTO) return false;
    other = (pascal_view *)xx_mem_alloc(sizeof(*other));
    if (!other) return false;
    block_ok = pascal_parse_order(self, XX_APPLE_PASCAL_ORDER_BLOCK, view, pd);
    dos_ok = pascal_parse_order(self, XX_APPLE_PASCAL_ORDER_DOS, other, pd);
    if (!block_ok && dos_ok) *view = *other;
    xx_mem_free(other);
    return block_ok != dos_ok && !(pd && xx_pd_is_stopped(pd));
}
static void pascal_vtable_destroy(Abstractformat *self)
{
    xx_apple_pascal_destroy((xx_apple_pascal *)self);
}
void xx_apple_pascal_init_order(xx_apple_pascal *volume, xx_io_device *device, int64_t base_address, xx_apple_pascal_order order)
{
    if (!volume) return;
    xx_mem_zero(volume, sizeof(*volume));
    xx_format_init(&volume->format, device, base_address);
    volume->requested_order = order;
    volume->format.endian = XX_ENDIAN_LITTLE;
    volume->format.file_type = PASCAL_TYPE;
    volume->format.format_type = XX_TYPE_ARCHIVE;
    volume->format.is_archive = true;
    xx_format_set_mime_type(&volume->format, "application/x-apple-pascal-fs");
    xx_format_set_extension(&volume->format, "po");
    volume->format.check_is_valid = xx_apple_pascal_check_is_valid;
    volume->format.handle_base_info = xx_apple_pascal_handle_base_info;
    volume->format.get_format_size = xx_apple_pascal_get_format_size;
    volume->format.get_number_of_archive_records = xx_apple_pascal_get_number_of_archive_records;
    volume->format.create_archive_records_reading = xx_apple_pascal_create_archive_records_reading;
    volume->format.get_current_archive_record = xx_apple_pascal_get_current_archive_record;
    volume->format.archive_record_move_to_next = xx_apple_pascal_archive_record_move_to_next;
    volume->format.unpack_current_archive_record = xx_apple_pascal_unpack_current_archive_record;
    volume->format.free_archive_records_reading = xx_apple_pascal_free_archive_records_reading;
    volume->format.destroy = pascal_vtable_destroy;
}
void xx_apple_pascal_init(xx_apple_pascal *volume, xx_io_device *device, int64_t base_address)
{
    xx_apple_pascal_init_order(volume, device, base_address, XX_APPLE_PASCAL_ORDER_AUTO);
}
xx_apple_pascal *xx_apple_pascal_create(xx_io_device *device, int64_t base_address)
{
    xx_apple_pascal *volume = (xx_apple_pascal *)xx_mem_alloc(sizeof(*volume));
    if (volume) xx_apple_pascal_init(volume, device, base_address);
    return volume;
}
void xx_apple_pascal_destroy(xx_apple_pascal *volume)
{
    if (volume) xx_format_cleanup_extra_parameters(&volume->format);
}
void xx_apple_pascal_free(xx_apple_pascal *volume)
{
    if (!volume) return;
    xx_apple_pascal_destroy(volume);
    xx_mem_free(volume);
}
bool xx_apple_pascal_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    pascal_view *view = (pascal_view *)xx_mem_alloc(sizeof(*view));
    bool ok = view && pascal_parse(self, view, pd);
    xx_mem_free(view);
    return ok;
}
bool xx_apple_pascal_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_apple_pascal *volume = (xx_apple_pascal *)self;
    pascal_view *view;
    int64_t total, end;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    view = (pascal_view *)xx_mem_alloc(sizeof(*view));
    if (!view || !pascal_parse(self, view, pd)) {
        xx_mem_free(view);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    volume->number_of_records = view->count;
    volume->block_count = view->blocks;
    volume->directory_file_count = view->declared;
    volume->detected_order = view->order;
    xx_mem_copy(volume->volume_name, view->volume_name, sizeof(volume->volume_name));
    self->format_size = view->bytes;
    self->number_of_archive_records = view->count;
    total = xx_io_total_size(self->device);
    end = self->base_address + view->bytes;
    self->overlay_offset = total > end ? end : -1;
    self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_mem_free(view);
    return true;
}
int64_t xx_apple_pascal_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && xx_apple_pascal_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_apple_pascal_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    return self && xx_apple_pascal_handle_base_info(self, pd) ? ((xx_apple_pascal *)self)->number_of_records : 0U;
}
static bool pascal_record(xx_archive_record *record, const pascal_view *view, const pascal_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = view->base + pascal_physical(view->order, 2U * PASCAL_BLOCK + (uint32_t)member->slot * PASCAL_ENTRY);
    record->header_size = PASCAL_ENTRY;
    record->data_offset = view->base + pascal_physical(view->order, member->offset);
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}
static bool pascal_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static void pascal_view_free(void *view)
{
    xx_mem_free(view);
}
xx_archive_record_state *xx_apple_pascal_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    pascal_view *view = (pascal_view *)xx_mem_alloc(sizeof(*view));
    xx_archive_record_state *state;
    if (!view || !pascal_parse(self, view, pd)) {
        xx_mem_free(view);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(view);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = view;
    state->free_internal = pascal_view_free;
    state->total_records = (int64_t)view->count;
    if (!pascal_options(&state->options, options) || (view->count && !pascal_record(&state->current_record, view, &view->members[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = view->count != 0U;
    state->current_index = view->count ? 0 : -1;
    return state;
}
const xx_archive_record *xx_apple_pascal_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_apple_pascal_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    pascal_view *view;
    if (!self || !state || state->format != self || !state->has_record || !(view = (pascal_view *)state->internal_state) || (pd && xx_pd_is_stopped(pd))) return false;
    if (view->index + 1U >= view->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!pascal_record(&state->current_record, view, &view->members[view->index + 1U])) {
        state->has_record = false;
        return false;
    }
    ++view->index;
    ++state->current_index;
    return true;
}
static uint64_t pascal_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback)
{
    const xx_var *var = xx_format_resolve_extra_parameter(self, options, id);
    if (!var) return fallback;
    switch (var->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(var);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(var);
            return value < 0 ? fallback : (uint64_t)value;
        }
        default: return fallback;
    }
}
static bool pascal_extract_limits(Abstractformat *self, xx_archive_record_state *state, const pascal_member *member, size_t *buffer_size)
{
    *buffer_size = member->size < PASCAL_COPY ? member->size : PASCAL_COPY;
    return member->size <= pascal_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
           (uint64_t)sizeof(pascal_view) + sizeof(*state) + *buffer_size <= pascal_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_apple_pascal_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    pascal_view *view;
    const pascal_member *member;
    uint8_t *buffer;
    uint32_t done = 0U;
    size_t buffer_size;
    bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record ||
        !(view = (pascal_view *)state->internal_state) || view->index >= view->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &view->members[view->index];
    if (!pascal_extract_limits(self, state, member, &buffer_size)) return false;
    if (!buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size);
    if (!buffer) return false;
    while (done < member->size) {
        size_t part = member->size - done < buffer_size ? member->size - done : buffer_size;
        size_t written = 0U;
        if (!pascal_read(view, member->offset + done, buffer, part, pd)) {
            ok = false;
            break;
        }
        while (destination && written < part && !(pd && xx_pd_is_stopped(pd))) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written) {
                ok = false;
                break;
            }
            written += (size_t)got;
        }
        if (!ok || (pd && xx_pd_is_stopped(pd))) {
            ok = false;
            break;
        }
        done += (uint32_t)part;
    }
    xx_mem_free(buffer);
    return ok && !(pd && xx_pd_is_stopped(pd));
}
static xx_io_device *pascal_stage(const char *destination, char **stage_path)
{
    unsigned attempt;
    size_t i, parent = 0U;
    char *directory = xx_str_dup(destination);
    *stage_path = NULL;
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48];
        char *candidate;
        xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_pascal.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (pascal_equal(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        output = xx_io_file_open(candidate, "wbx");
        if (output) {
            *stage_path = candidate;
            xx_str_free(directory);
            return output;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
bool xx_apple_pascal_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    pascal_view *view;
    const xx_var *option, *overwrite_option;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL;
    size_t buffer_size;
    bool ok = false, overwrite;
    if (!self || !state || state->format != self || !state->has_record || !(view = (pascal_view *)state->internal_state) || view->index >= view->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!pascal_extract_limits(self, state, view->members + view->index, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_apple_pascal_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", view->members[view->index].name)
                                                                                                : xx_str_concat(base, view->members[view->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = pascal_stage(path, &stage_path);
        if (!output) goto done;
        ok = xx_apple_pascal_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output) != 0) ok = false;
    }
    if (ok && !(pd && xx_pd_is_stopped(pd))) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
done:
    if (stage_path) {
        if (!ok) (void)xx_io_file_remove_a(stage_path);
        xx_str_free(stage_path);
    }
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}
void xx_apple_pascal_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
