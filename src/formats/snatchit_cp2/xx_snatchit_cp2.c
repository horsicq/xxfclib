/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native Snatch-it CP2 implementation. LibDsk is an independent
 * fixture producer/oracle only; its source is not imported here.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/snatchit_cp2/xx_snatchit_cp2.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef SNATCHIT_CP2
#define CP2_FILE_TYPE XX_FILE_TYPE_SNATCHIT_CP2
#else
#define CP2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CP2_HEADER_BYTES 30U
#define CP2_TRACK_BYTES 387U
#define CP2_MAX_TRACKS_IN_SEGMENT 32U
#define CP2_MAX_SEGMENT_HEADER (CP2_TRACK_BYTES * CP2_MAX_TRACKS_IN_SEGMENT + 1U)
#define CP2_MAX_CYLINDERS 80U
#define CP2_MAX_HEADS 2U
#define CP2_MAX_SECTORS_PER_TRACK 18U
#define CP2_TRACK_COUNT (CP2_MAX_CYLINDERS * CP2_MAX_HEADS)
#define CP2_SECTOR_COUNT (CP2_TRACK_COUNT * CP2_MAX_SECTORS_PER_TRACK)
#define CP2_MAX_FILE_BYTES (4U * 1024U * 1024U)
#define CP2_SECTOR_BYTES 512U
#define CP2_BIAS 0x16adU
#define CP2_REQUIRED_WORK (sizeof(cp2_view) + CP2_MAX_SEGMENT_HEADER + 8192U)

typedef struct cp2_view_s {
    int64_t sector_at[CP2_SECTOR_COUNT];
    uint8_t sectors_seen[CP2_SECTOR_COUNT];
    uint8_t tracks_seen[CP2_TRACK_COUNT];
    uint64_t source_size, disk_size;
    unsigned cylinders, heads, sectors, track_count, sector_count;
} cp2_view;

static bool cp2_read_at(xx_io_device *d, int64_t at, void *bytes, size_t count, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (!d || (!bytes && count) || at < 0 || (pd && xx_pd_is_stopped(pd)) || xx_io_seek64(d, at, SEEK_SET) != 0) return false;
    while (done < count) {
        ssize_t got = xx_io_read(d, (uint8_t *)bytes + done, count - done);
        if (got <= 0 || (size_t)got > count - done || (pd && xx_pd_is_stopped(pd))) return false;
        done += (size_t)got;
    }
    return true;
}
static bool cp2_write(xx_io_device *d, const void *bytes, size_t count, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (!d || (pd && xx_pd_is_stopped(pd))) return false;
    while (done < count) {
        ssize_t put = xx_io_write(d, (const uint8_t *)bytes + done, count - done);
        if (put <= 0 || (size_t)put > count - done || (pd && xx_pd_is_stopped(pd))) return false;
        done += (size_t)put;
    }
    return true;
}
static bool cp2_valid_geometry(const cp2_view *v)
{
    static const struct {
        unsigned c, h, s;
    } geometries[] = {{40U, 1U, 8U}, {40U, 1U, 9U}, {40U, 2U, 8U}, {40U, 2U, 9U}, {80U, 2U, 9U}, {80U, 2U, 15U}, {80U, 2U, 18U}};
    size_t profile, i;
    bool known = false;
    for (profile = 0U; profile < sizeof(geometries) / sizeof(geometries[0]); ++profile)
        if (v->cylinders == geometries[profile].c && v->heads == geometries[profile].h && v->sectors == geometries[profile].s) known = true;
    if (!known || v->track_count != v->cylinders * v->heads || v->sector_count != v->track_count * v->sectors) return false;
    for (i = 0U; i < v->cylinders * v->heads; ++i) {
        unsigned c = (unsigned)i / v->heads, h = (unsigned)i % v->heads, s;
        if (!v->tracks_seen[c * CP2_MAX_HEADS + h]) return false;
        for (s = 0U; s < v->sectors; ++s)
            if (!v->sectors_seen[(c * CP2_MAX_HEADS + h) * CP2_MAX_SECTORS_PER_TRACK + s]) return false;
    }
    return true;
}
static bool cp2_parse(Abstractformat *f, cp2_view **out, xx_pd_struct *pd)
{
    static const uint8_t signature[CP2_HEADER_BYTES] = {'S', 'O', 'F', 'T', 'W', 'A', 'R', 'E', ' ', 'P', 'I', 'R', 'A', 'T', 'E',
                                                        'S', 'R', 'e', 'l', 'e', 'a', 's', 'e', ' ', '3', '.', '0', '2', '$', '0'};
    cp2_view *view = NULL;
    uint8_t header[CP2_MAX_SEGMENT_HEADER], disk_header[CP2_HEADER_BYTES];
    int64_t saved, total;
    uint64_t position, length;
    bool result = false;
    if (out) *out = NULL;
    if (!f || !f->device || !out || f->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    saved = xx_io_tell(f->device);
    if (saved < 0) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address) goto done;
    length = (uint64_t)(total - f->base_address);
    if (length < CP2_HEADER_BYTES + 4U || length > CP2_MAX_FILE_BYTES || !cp2_read_at(f->device, f->base_address, disk_header, sizeof(disk_header), pd) ||
        xx_rt_memcmp(disk_header, signature, sizeof(signature)) != 0)
        goto done;
    view = (cp2_view *)xx_mem_alloc(sizeof(*view));
    if (!view) goto done;
    xx_rt_memset(view, 0, sizeof(*view));
    view->source_size = length;
    position = CP2_HEADER_BYTES;
    while (position < length) {
        uint8_t word[2], data_used[128];
        uint16_t header_bytes, data_bytes;
        unsigned track_count, track, checksum = 0U, segment_sectors = 0U;
        uint64_t data_start, next;
        if (length - position < 4U || !cp2_read_at(f->device, f->base_address + (int64_t)position, word, 2U, pd)) goto done;
        header_bytes = xx_data_get_u16(word, 2, 0, false);
        if (header_bytes < CP2_TRACK_BYTES + 1U || header_bytes > CP2_MAX_SEGMENT_HEADER || (header_bytes - 1U) % CP2_TRACK_BYTES != 0U ||
            (uint64_t)header_bytes > length - position - 4U || !cp2_read_at(f->device, f->base_address + (int64_t)position + 2, header, header_bytes, pd))
            goto done;
        track_count = (header_bytes - 1U) / CP2_TRACK_BYTES;
        for (track = 0U; track < header_bytes - 1U; ++track) checksum = (checksum + header[track]) & 255U;
        if (checksum != header[header_bytes - 1U] || !cp2_read_at(f->device, f->base_address + (int64_t)position + 2 + header_bytes, word, 2U, pd)) goto done;
        data_bytes = xx_data_get_u16(word, 2, 0, false);
        data_start = position + 4U + header_bytes;
        next = data_start + data_bytes;
        if (next > length || data_bytes == 0U || data_bytes % CP2_SECTOR_BYTES != 0U || data_bytes / CP2_SECTOR_BYTES > sizeof(data_used)) goto done;
        xx_rt_memset(data_used, 0, sizeof(data_used));
        for (track = 0U; track < track_count; ++track) {
            const uint8_t *t = header + track * CP2_TRACK_BYTES;
            unsigned c = t[0], h = t[1], count = t[2], sector;
            size_t track_index;
            if (c >= CP2_MAX_CYLINDERS || h >= CP2_MAX_HEADS || (count != 8U && count != 9U && count != 15U && count != 18U) || (view->sectors && count != view->sectors))
                goto done;
            if (!view->sectors) view->sectors = count;
            track_index = c * CP2_MAX_HEADS + h;
            if (view->tracks_seen[track_index]) goto done;
            view->tracks_seen[track_index] = 1U;
            ++view->track_count;
            if (view->cylinders < c + 1U) view->cylinders = c + 1U;
            if (view->heads < h + 1U) view->heads = h + 1U;
            for (sector = 0U; sector < count; ++sector) {
                const uint8_t *s = t + 3U + sector * 16U;
                unsigned id = s[6], size_code = s[7];
                uint16_t biased = xx_data_get_u16(s + 8U, 2, 0, false);
                unsigned data_offset;
                size_t key, slot;
                if (s[0] || s[1] || s[2] || s[3] || s[4] != c || s[5] != h || id == 0U || id > count || size_code != 2U || biased < CP2_BIAS) goto done;
                data_offset = (unsigned)(biased - CP2_BIAS);
                if (data_offset % CP2_SECTOR_BYTES != 0U || data_offset + CP2_SECTOR_BYTES > data_bytes) goto done;
                slot = data_offset / CP2_SECTOR_BYTES;
                if (data_used[slot]) goto done;
                data_used[slot] = 1U;
                key = track_index * CP2_MAX_SECTORS_PER_TRACK + id - 1U;
                if (view->sectors_seen[key]) goto done;
                view->sectors_seen[key] = 1U;
                view->sector_at[key] = f->base_address + (int64_t)(data_start + data_offset);
                ++view->sector_count;
                ++segment_sectors;
            }
            if (pd && xx_pd_is_stopped(pd)) goto done;
        }
        if (segment_sectors != (unsigned)data_bytes / CP2_SECTOR_BYTES) goto done;
        position = next;
    }
    if (!cp2_valid_geometry(view)) goto done;
    view->disk_size = (uint64_t)view->sector_count * CP2_SECTOR_BYTES;
    result = true;
done:
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0) result = false;
    if (result && !(pd && xx_pd_is_stopped(pd))) *out = view;
    else {
        if (view) xx_mem_free(view);
        result = false;
    }
    return result;
}
static bool cp2_check(Abstractformat *f, xx_pd_struct *pd)
{
    cp2_view *v = NULL;
    bool result = cp2_parse(f, &v, pd);
    if (v) xx_mem_free(v);
    return result;
}
static bool cp2_handle(Abstractformat *f, xx_pd_struct *pd)
{
    cp2_view *v = NULL;
    if (!cp2_parse(f, &v, pd)) {
        if (f) {
            f->is_valid = false;
            f->base_info_handled = false;
        }
        return false;
    }
    f->format_size = (int64_t)v->source_size;
    f->number_of_archive_records = 1U;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    xx_mem_free(v);
    return true;
}
static int64_t cp2_size(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || cp2_handle(f, pd)) ? f->format_size : -1;
}
static uint64_t cp2_count(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || cp2_handle(f, pd)) ? f->number_of_archive_records : 0U;
}
static bool cp2_limits(Abstractformat *f, const xx_list_s *options, const cp2_view *v)
{
    const xx_var *limit;
    limit = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && v->disk_size > xx_var_get_u64(limit)) return false;
    limit = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
    return !limit || xx_var_get_u64(limit) >= CP2_REQUIRED_WORK;
}
static bool cp2_emit(Abstractformat *f, const cp2_view *v, xx_io_device *destination, xx_pd_struct *pd)
{
    unsigned c, h, s;
    uint8_t bytes[CP2_SECTOR_BYTES];
    for (c = 0U; c < v->cylinders; ++c)
        for (h = 0U; h < v->heads; ++h)
            for (s = 0U; s < v->sectors; ++s) {
                size_t key = (c * CP2_MAX_HEADS + h) * CP2_MAX_SECTORS_PER_TRACK + s;
                if ((pd && xx_pd_is_stopped(pd)) || !cp2_read_at(f->device, v->sector_at[key], bytes, sizeof(bytes), pd) ||
                    !cp2_write(destination, bytes, sizeof(bytes), pd))
                    return false;
            }
    return true;
}
static bool cp2_unpack_device(Abstractformat *f, const cp2_view *v, const xx_list_s *options, xx_io_device *destination, xx_pd_struct *pd)
{
    int64_t saved;
    bool result;
    if (!f || !v || !destination || destination == f->device || !cp2_limits(f, options, v) || (pd && xx_pd_is_stopped(pd))) return false;
    saved = xx_io_tell(f->device);
    if (saved < 0) return false;
    result = cp2_emit(f, v, destination, pd);
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0) result = false;
    return result && !(pd && xx_pd_is_stopped(pd));
}
static void cp2_free_view(void *p)
{
    if (p) xx_mem_free(p);
}
static bool cp2_record(xx_archive_record_state *state)
{
    cp2_view *v = (cp2_view *)state->internal_state;
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = state->format->base_address;
    record->header_size = CP2_HEADER_BYTES;
    record->data_offset = state->format->base_address + CP2_HEADER_BYTES;
    record->compressed_size = (int64_t)(v->source_size - CP2_HEADER_BYTES);
    return xx_archive_record_set_original_name(record, "disk.img") &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, v->source_size - CP2_HEADER_BYTES) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, v->disk_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 1U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}
static xx_archive_record_state *cp2_create(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    cp2_view *view = NULL;
    size_t i;
    if (!f || !f->device || (pd && xx_pd_is_stopped(pd)) || (!f->base_info_handled && !cp2_handle(f, pd)) || !cp2_parse(f, &view, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(view);
        return NULL;
    }
    xx_archive_record_state_init(state, f);
    state->internal_state = view;
    state->free_internal = cp2_free_view;
    state->total_records = 1;
    for (i = 0U; options && i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record = cp2_record(state);
    if (!state->has_record) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}
static const xx_archive_record *cp2_current(Abstractformat *f, xx_archive_record_state *s)
{
    return f && s && s->format == f && s->has_record ? &s->current_record : NULL;
}
static bool cp2_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    if (!f || !s || s->format != f || !s->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    s->has_record = false;
    return false;
}
static ssize_t cp2_discard(xx_io_device *d, const void *p, size_t n)
{
    (void)d;
    (void)p;
    return (ssize_t)n;
}
static bool cp2_same_path(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}
static xx_io_device *cp2_stage(const char *destination, char **stage)
{
    char *directory = xx_str_dup(destination);
    size_t i, parent = 0U;
    unsigned attempt;
    *stage = NULL;
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40], *candidate;
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_cp2.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (cp2_same_path(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        device = xx_io_file_open(candidate, "wbx");
        if (device) {
            *stage = candidate;
            xx_str_free(directory);
            return device;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
static bool cp2_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    cp2_view *view;
    const xx_var *v, *ov;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL;
    xx_io_device *output = NULL, discard;
    bool result = false, overwrite = false;
    if (!f || !s || s->format != f || !s->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    view = (cp2_view *)s->internal_state;
    if (!cp2_limits(f, &s->options, view)) return false;
    v = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!v) {
        xx_rt_memset(&discard, 0, sizeof(discard));
        discard.write = cp2_discard;
        return cp2_unpack_device(f, view, &s->options, &discard, pd);
    }
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(v);
    else if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) base = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(v));
    if (!base) goto done;
    path = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat(base, "/disk.img") : xx_str_concat(base, "disk.img");
    if (!path) goto done;
    ov = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = ov && xx_var_get_bool(ov);
    if ((!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) || (pd && xx_pd_is_stopped(pd))) goto done;
    output = cp2_stage(path, &stage);
    if (!output) goto done;
    result = cp2_unpack_device(f, view, &s->options, output, pd);
    if (xx_io_close(output) != 0) result = false;
    output = NULL;
    if (result && !(pd && xx_pd_is_stopped(pd))) result = xx_io_file_replace_a(stage, path, overwrite);
    else result = false;
done:
    if (output) {
        (void)xx_io_close(output);
        result = false;
    }
    if (stage) {
        if (!result) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (path) xx_str_free(path);
    if (owned) xx_str_free(owned);
    return result;
}
static void cp2_free_records(Abstractformat *f, xx_archive_record_state *s)
{
    (void)f;
    xx_archive_record_state_free(s);
}
static void cp2_destroy_vtable(Abstractformat *f)
{
    if (f) xx_format_cleanup_extra_parameters(f);
}
void xx_snatchit_cp2_init(xx_snatchit_cp2 *r, xx_io_device *d, int64_t b)
{
    if (!r) return;
    xx_rt_memset(r, 0, sizeof(*r));
    xx_format_init(&r->format, d, b);
    r->format.file_type = CP2_FILE_TYPE;
    r->format.format_type = XX_TYPE_ARCHIVE;
    r->format.is_archive = true;
    xx_format_set_extension(&r->format, "cp2");
    r->format.check_is_valid = cp2_check;
    r->format.handle_base_info = cp2_handle;
    r->format.get_format_size = cp2_size;
    r->format.get_number_of_archive_records = cp2_count;
    r->format.create_archive_records_reading = cp2_create;
    r->format.get_current_archive_record = cp2_current;
    r->format.archive_record_move_to_next = cp2_next;
    r->format.unpack_current_archive_record = cp2_unpack;
    r->format.free_archive_records_reading = cp2_free_records;
    r->format.destroy = cp2_destroy_vtable;
}
xx_snatchit_cp2 *xx_snatchit_cp2_create(xx_io_device *d, int64_t b)
{
    xx_snatchit_cp2 *r = (xx_snatchit_cp2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_snatchit_cp2_init(r, d, b);
    return r;
}
void xx_snatchit_cp2_destroy(xx_snatchit_cp2 *r)
{
    if (r) cp2_destroy_vtable(&r->format);
}
void xx_snatchit_cp2_free(xx_snatchit_cp2 *r)
{
    if (r) {
        xx_snatchit_cp2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_snatchit_cp2_unpack_to_device(xx_snatchit_cp2 *r, xx_io_device *destination, xx_pd_struct *pd)
{
    cp2_view *v = NULL;
    bool result;
    if (!r || !cp2_parse(&r->format, &v, pd)) return false;
    result = cp2_unpack_device(&r->format, v, NULL, destination, pd);
    xx_mem_free(v);
    return result;
}
