/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/solitaire_deluxe/xx_solitaire_deluxe.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef SOLITAIRE_DELUXE
#define SD_TYPE XX_FILE_TYPE_SOLITAIRE_DELUXE
#else
#define SD_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define SD_HEADER 119U
#define SD_FILE_ENTRY 25U
#define SD_CATEGORY_ENTRY 22U
#define SD_MAX_FILES 256U
#define SD_MAX_CATEGORIES 64U
#define SD_MAX_VOLUME UINT64_C(1073741824)
#define SD_MAX_MEMBER UINT32_C(67108864)

typedef struct sd_member_s {
    char *name;
    int64_t header_at, data_at;
    uint32_t raw_size, packed_size;
    uint16_t attributes, value;
} sd_member;
typedef struct sd_view_s {
    sd_member *items;
    size_t declared, count, index;
    int64_t format_size;
    bool continuation;
} sd_view;

static bool sd_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool sd_read(xx_io_device *device, int64_t at, void *out, size_t size)
{
    int64_t saved;
    size_t done = 0U;
    bool ok = false;
    if (!device || at < 0 || (!out && size)) return false;
    saved = xx_io_tell(device);
    if (saved < 0) return false;
    if (xx_io_seek64(device, at, SEEK_SET) == 0) {
        while (done < size) {
            ssize_t got = xx_io_read(device, (uint8_t *)out + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    return ok;
}
static void sd_view_free(void *pointer)
{
    sd_view *view = (sd_view *)pointer;
    size_t i;
    if (!view) return;
    for (i = 0U; i < view->declared; ++i) xx_mem_free(view->items[i].name);
    xx_mem_free(view->items);
    xx_mem_free(view);
}
static char sd_fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}
static bool sd_same(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (sd_fold(*a) != sd_fold(*b)) return false;
    return *a == *b;
}
static bool sd_device_name(const char *name, size_t length)
{
    static const char *const words[] = {"con", "prn", "aux", "nul", "conin$", "conout$", "clock$"};
    size_t stem = 0U, i, j;
    while (stem < length && name[stem] != '.') ++stem;
    for (i = 0U; i < sizeof(words) / sizeof(words[0]); ++i) {
        for (j = 0U; j < stem && words[i][j] && sd_fold(name[j]) == words[i][j]; ++j) {
        }
        if (j == stem && words[i][j] == '\0') return true;
    }
    return stem == 4U &&
           ((sd_fold(name[0]) == 'c' && sd_fold(name[1]) == 'o' && sd_fold(name[2]) == 'm') ||
            (sd_fold(name[0]) == 'l' && sd_fold(name[1]) == 'p' && sd_fold(name[2]) == 't')) &&
           name[3] >= '0' && name[3] <= '9';
}
static char *sd_name(const uint8_t *raw)
{
    size_t length = 0U, i;
    char *name;
    while (length < 13U && raw[length]) ++length;
    if (!length || length == 13U || raw[length - 1U] == '.' || raw[length - 1U] == ' ' || (length == 1U && raw[0] == '.') ||
        (length == 2U && raw[0] == '.' && raw[1] == '.'))
        return NULL;
    for (i = 0U; i < length; ++i) {
        uint8_t c = raw[i];
        if (c < 0x21U || c > 0x7eU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return NULL;
    }
    if (sd_device_name((const char *)raw, length)) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_mem_copy(name, raw, length);
    name[length] = '\0';
    return name;
}

/* Directory offsets are derived from two bounded counts, not a guessed
 * signature in the compressed data. A volume can end in the middle of a
 * declared member; only the preceding complete DCL streams are exposed. */
static sd_view *sd_parse(Abstractformat *f, xx_pd_struct *pd)
{
    uint8_t h[SD_HEADER];
    uint16_t file_count, category_count;
    uint64_t table_end, cursor, volume_size;
    int64_t total;
    sd_view *view;
    size_t i;
    bool incomplete = false;
    if (!f || !f->device || f->base_address < 0 || sd_stopped(pd)) return NULL;
    total = xx_io_total_size(f->device);
    if (total < f->base_address + SD_HEADER || (uint64_t)(total - f->base_address) > SD_MAX_VOLUME || !sd_read(f->device, f->base_address, h, sizeof(h)) ||
        xx_rt_memcmp(h, "Solitaire Deluxe.", 17U) || h[100] != 0x1aU || xx_data_get_u32(h + 101U, 4, 0, false) != UINT32_C(0x12345678) || h[105] != 'P' || h[106] != 'E')
        return NULL;
    category_count = xx_data_get_u16(h + 115U, 2, 0, false);
    file_count = xx_data_get_u16(h + 117U, 2, 0, false);
    if (!file_count || file_count > SD_MAX_FILES || category_count > SD_MAX_CATEGORIES) return NULL;
    table_end = SD_HEADER + (uint64_t)file_count * SD_FILE_ENTRY + (uint64_t)category_count * SD_CATEGORY_ENTRY;
    volume_size = (uint64_t)(total - f->base_address);
    if (table_end >= volume_size) return NULL;
    view = (sd_view *)xx_mem_calloc(1U, sizeof(*view));
    if (!view) return NULL;
    view->items = (sd_member *)xx_mem_calloc(file_count, sizeof(*view->items));
    if (!view->items) {
        sd_view_free(view);
        return NULL;
    }
    cursor = table_end;
    for (i = 0U; i < file_count && !sd_stopped(pd); ++i) {
        sd_member *item = &view->items[i];
        uint8_t raw[SD_FILE_ENTRY];
        size_t j;
        uint64_t header_at = SD_HEADER + (uint64_t)i * SD_FILE_ENTRY;
        if (!sd_read(f->device, f->base_address + (int64_t)header_at, raw, sizeof(raw)) || !(item->name = sd_name(raw))) goto bad;
        ++view->declared;
        for (j = 0U; j < i; ++j)
            if (sd_same(item->name, view->items[j].name)) goto bad;
        item->raw_size = xx_data_get_u32(raw + 13U, 4, 0, false);
        item->packed_size = xx_data_get_u32(raw + 17U, 4, 0, false);
        item->attributes = xx_data_get_u16(raw + 21U, 2, 0, false);
        item->value = xx_data_get_u16(raw + 23U, 2, 0, false);
        item->header_at = f->base_address + (int64_t)header_at;
        item->data_at = f->base_address + (int64_t)cursor;
        if (!item->raw_size || !item->packed_size) goto bad;
        if (!incomplete && item->packed_size <= volume_size - cursor) {
            uint8_t *packed;
            size_t consumed = 0U, produced = 0U;
            bool ok;
            if (item->raw_size > SD_MAX_MEMBER || item->packed_size > SD_MAX_MEMBER) goto bad;
            packed = (uint8_t *)xx_mem_alloc(item->packed_size);
            if (!packed) goto bad;
            ok = sd_read(f->device, item->data_at, packed, item->packed_size) && xx_dcl_scan_memory(packed, item->packed_size, item->raw_size, &consumed, &produced) &&
                 consumed == item->packed_size && produced == item->raw_size;
            xx_mem_free(packed);
            if (!ok) goto bad;
            ++view->count;
        } else {
            incomplete = true;
        }
        cursor += item->packed_size;
    }
    if (sd_stopped(pd) || view->declared != file_count || !view->count) goto bad;
    view->continuation = incomplete;
    view->format_size = incomplete ? (int64_t)volume_size : (int64_t)cursor;
    if (!incomplete && cursor > volume_size) goto bad;
    return view;
bad:
    sd_view_free(view);
    return NULL;
}
static bool sd_copy_options(xx_list_s *dest, const xx_list_s *source)
{
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(dest, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static bool sd_set_record(xx_archive_record *record, const sd_member *item)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = item->header_at;
    record->header_size = SD_FILE_ENTRY;
    record->data_offset = item->data_at;
    record->compressed_size = item->packed_size;
    return xx_archive_record_set_original_name(record, item->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, item->raw_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, item->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 1U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}
static bool sd_write(xx_io_device *out, const uint8_t *bytes, size_t size, xx_pd_struct *pd)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t got;
        if (sd_stopped(pd)) return false;
        got = xx_io_write(out, bytes + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return !sd_stopped(pd);
}
static bool sd_decode(Abstractformat *f, const sd_member *item, xx_io_device *out, xx_pd_struct *pd)
{
    uint8_t *packed = NULL, *plain = NULL;
    size_t consumed = 0U, produced = 0U, written = 0U;
    bool ok = false;
    if (sd_stopped(pd) || out == f->device) return false;
    packed = (uint8_t *)xx_mem_alloc(item->packed_size);
    plain = (uint8_t *)xx_mem_alloc(item->raw_size);
    if (!packed || !plain || !sd_read(f->device, item->data_at, packed, item->packed_size) ||
        !xx_dcl_scan_memory(packed, item->packed_size, item->raw_size, &consumed, &produced) || consumed != item->packed_size || produced != item->raw_size ||
        !xx_dcl_decode_memory(packed, item->packed_size, plain, item->raw_size, &written) || written != item->raw_size || sd_stopped(pd))
        goto done;
    ok = !out || sd_write(out, plain, item->raw_size, pd);
done:
    xx_mem_free(plain);
    xx_mem_free(packed);
    return ok;
}

void xx_solitaire_deluxe_init(xx_solitaire_deluxe *archive, xx_io_device *device, int64_t base)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = SD_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-solitaire-deluxe-volume");
    xx_format_set_extension(&archive->format, "1");
    archive->format.check_is_valid = xx_solitaire_deluxe_check_is_valid;
    archive->format.handle_base_info = xx_solitaire_deluxe_handle_base_info;
    archive->format.get_format_size = xx_solitaire_deluxe_get_format_size;
    archive->format.get_number_of_archive_records = xx_solitaire_deluxe_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_solitaire_deluxe_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_solitaire_deluxe_get_current_archive_record;
    archive->format.archive_record_move_to_next = xx_solitaire_deluxe_archive_record_move_to_next;
    archive->format.unpack_current_archive_record = xx_solitaire_deluxe_unpack_current_archive_record;
    archive->format.free_archive_records_reading = xx_solitaire_deluxe_free_archive_records_reading;
}
xx_solitaire_deluxe *xx_solitaire_deluxe_create(xx_io_device *device, int64_t base)
{
    xx_solitaire_deluxe *archive = (xx_solitaire_deluxe *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_solitaire_deluxe_init(archive, device, base);
    return archive;
}
void xx_solitaire_deluxe_destroy(xx_solitaire_deluxe *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_solitaire_deluxe_free(xx_solitaire_deluxe *archive)
{
    if (!archive) return;
    xx_solitaire_deluxe_destroy(archive);
    xx_mem_free(archive);
}
bool xx_solitaire_deluxe_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    sd_view *view = sd_parse(f, pd);
    if (!view) return false;
    sd_view_free(view);
    return true;
}
bool xx_solitaire_deluxe_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    sd_view *view = sd_parse(f, pd);
    if (!view) return false;
    ((xx_solitaire_deluxe *)f)->number_of_records = view->count;
    ((xx_solitaire_deluxe *)f)->declared_records = (uint32_t)view->declared;
    ((xx_solitaire_deluxe *)f)->needs_continuation = view->continuation;
    f->number_of_archive_records = view->count;
    f->format_size = view->format_size;
    f->overlay_offset =
        !view->continuation && (uint64_t)view->format_size < (uint64_t)(xx_io_total_size(f->device) - f->base_address) ? f->base_address + view->format_size : -1;
    f->overlay_size = f->overlay_offset < 0 ? 0 : xx_io_total_size(f->device) - f->overlay_offset;
    f->is_valid = true;
    f->base_info_handled = true;
    sd_view_free(view);
    return true;
}
int64_t xx_solitaire_deluxe_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || xx_solitaire_deluxe_handle_base_info(f, pd)) ? f->format_size : -1;
}
uint64_t xx_solitaire_deluxe_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || xx_solitaire_deluxe_handle_base_info(f, pd)) ? f->number_of_archive_records : 0U;
}
xx_archive_record_state *xx_solitaire_deluxe_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    sd_view *view = sd_parse(f, pd);
    xx_archive_record_state *state;
    if (!view) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sd_view_free(view);
        return NULL;
    }
    xx_archive_record_state_init(state, f);
    state->internal_state = view;
    state->free_internal = sd_view_free;
    state->total_records = (int64_t)view->count;
    if (!sd_copy_options(&state->options, options) || !sd_set_record(&state->current_record, &view->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_solitaire_deluxe_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state)
{
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_solitaire_deluxe_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sd_view *view;
    if (!f || !state || state->format != f || !state->has_record || !(view = (sd_view *)state->internal_state) || sd_stopped(pd)) return false;
    if (++view->index >= view->count || !sd_set_record(&state->current_record, &view->items[view->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}
bool xx_solitaire_deluxe_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sd_view *view;
    const sd_member *item;
    const xx_var *option;
    const char *base;
    char *wide_base = NULL, *path = NULL, *stage = NULL;
    xx_io_device *output = NULL;
    size_t capacity;
    unsigned attempt;
    bool overwrite, ok = false;
    if (!f || !state || state->format != f || !state->has_record || !(view = (sd_view *)state->internal_state) || view->index >= view->count || sd_stopped(pd))
        return false;
    item = &view->items[view->index];
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return sd_decode(f, item, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) base = wide_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
    else return false;
    if (!base) goto done;
    path = base[0] ? xx_str_concat3(base, "/", item->name) : xx_str_dup(item->name);
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) || xx_str_len(path) > SIZE_MAX - 48U) goto done;
    capacity = xx_str_len(path) + 48U;
    stage = (char *)xx_mem_alloc(capacity);
    if (!stage) goto done;
    for (attempt = 0U; attempt < 128U && !sd_stopped(pd); ++attempt) {
        int n = xx_rt_snprintf(stage, capacity, "%s.xx_solitaire.tmp.%u", path, attempt);
        if (n < 0 || (size_t)n >= capacity) goto done;
        output = xx_io_file_open(stage, "wbx");
        if (output) break;
    }
    if (!output) goto done;
    ok = sd_decode(f, item, output, pd);
    if (xx_io_close(output) != 0) ok = false;
    output = NULL;
    if (ok && !sd_stopped(pd)) ok = xx_io_file_replace_a(stage, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage);
done:
    if (output) {
        (void)xx_io_close(output);
        (void)xx_io_file_remove_a(stage);
    }
    xx_mem_free(stage);
    xx_str_free(path);
    xx_str_free(wide_base);
    return ok;
}
void xx_solitaire_deluxe_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}
