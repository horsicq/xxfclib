/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Alcohol MDS v1.4 / raw MDF reader. Format evidence: Simulant mkdcdisc
 * src/disc_image/formats/mds.c, commit 2b98b0d9480e364e3517355648fbc3b49fb19882.
 * This parser and transfer code are original; no producer code is imported.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mds/xx_mds.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#ifdef MDS
#define MDS_TYPE XX_FILE_TYPE_MDS
#else
#define MDS_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define MDS_HEADER 88U
#define MDS_SESSION 24U
#define MDS_BLOCK 80U
#define MDS_EXTRA 8U
#define MDS_FOOTER 16U
#define MDS_SECTOR 2352U
#define MDS_MAX_DESCRIPTOR (1024U * 1024U)
#define MDS_MAX_SESSIONS 32U
#define MDS_MAX_TRACKS 99U
#define MDS_COPY 65536U

typedef struct mds_track_s {
    uint64_t offset, length;
    uint32_t lba, pregap, sectors;
    uint16_t number;
    uint8_t mode;
    int64_t header_offset;
} mds_track;
typedef struct mds_view_s {
    uint32_t refs, count, sessions;
    int64_t base, descriptor_size;
    uint64_t data_size;
    mds_track tracks[MDS_MAX_TRACKS];
} mds_view;
typedef struct mds_cursor_s {
    mds_view *view;
    uint32_t index;
} mds_cursor;
static bool mds_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool mds_mode(uint8_t mode)
{
    return mode == 0xA9U || mode == 0xAAU || mode == 0xABU || mode == 0xECU || mode == 0xADU;
}
static void mds_release(mds_view *v)
{
    if (v && !--v->refs) xx_mem_free(v);
}
static bool mds_read_at(xx_io_device *d, int64_t at, uint8_t *data, size_t length, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (mds_stop(pd) || xx_io_seek64(d, at, SEEK_SET) != 0) return false;
    while (done < length) {
        ssize_t got;
        if (mds_stop(pd)) return false;
        got = xx_io_read(d, data + done, length - done);
        if (got <= 0 || (size_t)got > length - done || mds_stop(pd)) return false;
        done += (size_t)got;
    }
    return !mds_stop(pd);
}
static mds_view *mds_parse(Abstractformat *f, xx_pd_struct *pd)
{
    xx_io_device *device;
    int64_t saved, total;
    uint8_t *data = NULL;
    mds_view *v = NULL;
    uint32_t sessions, block_total = 0U, s, block_index = 0U;
    uint64_t block_start, extra_start, footer, expected_mdf = 0U;
    bool ok = false;
    if (!f || !(device = f->device) || f->base_address < 0 || mds_stop(pd)) return NULL;
    saved = xx_io_tell(device);
    total = xx_io_total_size(device);
    if (saved < 0 || total < f->base_address || total - f->base_address < (int64_t)(MDS_HEADER + MDS_SESSION + MDS_FOOTER + 6U) ||
        total - f->base_address > MDS_MAX_DESCRIPTOR)
        return NULL;
    data = (uint8_t *)xx_mem_alloc((size_t)(total - f->base_address));
    v = (mds_view *)xx_mem_calloc(1U, sizeof(*v));
    if (v) v->refs = 1U;
    if (!data || !v || !mds_read_at(device, f->base_address, data, (size_t)(total - f->base_address), pd)) goto done;
    v->base = f->base_address;
    v->descriptor_size = total - f->base_address;
    if (memcmp(data, "MEDIA DESCRIPTOR", 16U) || data[0x10U] != 1U || data[0x11U] != 4U || xx_data_get_u16(data + 0x12U, 2, 0, false) != 0U ||
        xx_data_get_u32(data + 0x50U, 4, 0, false) != MDS_HEADER)
        goto done;
    sessions = xx_data_get_u16(data + 0x14U, 2, 0, false);
    if (!sessions || sessions > MDS_MAX_SESSIONS || xx_data_get_u16(data + 0x16U, 2, 0, false) != sessions) goto done;
    v->sessions = sessions;
    block_start = MDS_HEADER + (uint64_t)sessions * MDS_SESSION;
    if (block_start > (uint64_t)v->descriptor_size) goto done;
    for (s = 0U; s < sessions; ++s) {
        const uint8_t *session = data + MDS_HEADER + s * MDS_SESSION;
        uint32_t count = session[0x0AU], first = xx_data_get_u16(session + 0x0CU, 2, 0, false);
        uint32_t last = xx_data_get_u16(session + 0x0EU, 2, 0, false), tracks;
        if (xx_data_get_u16(session + 0x08U, 2, 0, false) != s + 1U || !first || last < first || last > MDS_MAX_TRACKS || first != v->count + 1U ||
            session[0x0BU] != 3U || xx_data_get_u32(session + 0x14U, 4, 0, false) != block_start + block_total * MDS_BLOCK)
            goto done;
        tracks = last - first + 1U;
        if (count != tracks + (s == 0U ? 6U : 4U) || block_total > UINT32_MAX - count) goto done;
        block_total += count;
        v->count += tracks;
    }
    if (!v->count || v->count > MDS_MAX_TRACKS) goto done;
    extra_start = block_start + (uint64_t)block_total * MDS_BLOCK;
    footer = extra_start + (uint64_t)block_total * MDS_EXTRA;
    if (footer + MDS_FOOTER + 6U != (uint64_t)v->descriptor_size || xx_data_get_u32(data + footer, 4, 0, false) != footer + MDS_FOOTER ||
        xx_data_get_u32(data + footer + 4U, 4, 0, false) != 0U || memcmp(data + footer + MDS_FOOTER, "*.mdf\0", 6U))
        goto done;
    for (s = 0U; s < sessions; ++s) {
        const uint8_t *session = data + MDS_HEADER + s * MDS_SESSION;
        uint32_t first = xx_data_get_u16(session + 0x0CU, 2, 0, false), last = xx_data_get_u16(session + 0x0EU, 2, 0, false);
        uint32_t count = session[0x0AU], t;
        const uint8_t *blocks = data + block_start + (uint64_t)block_index * MDS_BLOCK;
        if (blocks[4U] != 0xA0U || blocks[MDS_BLOCK + 4U] != 0xA1U || blocks[2U * MDS_BLOCK + 4U] != 0xA2U) goto done;
        for (t = first; t <= last; ++t) {
            uint32_t slot = block_index + 3U + t - first;
            const uint8_t *block = data + block_start + (uint64_t)slot * MDS_BLOCK;
            const uint8_t *extra = data + extra_start + (uint64_t)slot * MDS_EXTRA;
            mds_track *track = &v->tracks[t - 1U];
            uint64_t length;
            if (!mds_mode(block[0]) || block[1U] != 0U || block[2U] != (block[0] == 0xA9U ? 0x10U : 0x14U) || block[4U] != t ||
                xx_data_get_u16(block + 0x10U, 2, 0, false) != MDS_SECTOR || xx_data_get_u32(block + 0x0CU, 4, 0, false) != extra_start + (uint64_t)slot * MDS_EXTRA ||
                xx_data_get_u32(block + 0x30U, 4, 0, false) != 1U || xx_data_get_u32(block + 0x34U, 4, 0, false) != footer || !xx_data_get_u32(extra + 4U, 4, 0, false))
                goto done;
            track->number = (uint16_t)t;
            track->mode = block[0];
            track->lba = xx_data_get_u32(block + 0x24U, 4, 0, false);
            track->offset = xx_data_get_u64(block + 0x28U, 8, 0, false);
            track->pregap = xx_data_get_u32(extra, 4, 0, false);
            track->sectors = xx_data_get_u32(extra + 4U, 4, 0, false);
            track->header_offset = v->base + (int64_t)(block_start + (uint64_t)slot * MDS_BLOCK);
            length = (uint64_t)track->sectors * MDS_SECTOR;
            if (length > INT64_MAX || track->offset != expected_mdf || expected_mdf > INT64_MAX - length ||
                (t > 1U && (uint64_t)track->lba < (uint64_t)v->tracks[t - 2U].lba + v->tracks[t - 2U].sectors))
                goto done;
            track->length = length;
            expected_mdf += length;
        }
        if (blocks[(uint64_t)(count - (s == 0U ? 3U : 1U)) * MDS_BLOCK + 4U] != 0xB0U) goto done;
        block_index += count;
    }
    v->data_size = expected_mdf;
    ok = !mds_stop(pd);
done:
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    xx_mem_free(data);
    if (!ok) {
        mds_release(v);
        return NULL;
    }
    return v;
}
static void mds_close_data(xx_mds *m)
{
    if (m->data_owned && m->data) (void)xx_io_close(m->data);
    m->data = NULL;
    m->data_owned = false;
}
static void mds_destroy_format(Abstractformat *f)
{
    xx_mds_destroy((xx_mds *)f);
}
void xx_mds_init(xx_mds *m, xx_io_device *device, int64_t base)
{
    if (!m) return;
    xx_mem_zero(m, sizeof(*m));
    xx_format_init(&m->format, device, base);
    m->format.file_type = MDS_TYPE;
    m->format.format_type = XX_TYPE_ARCHIVE;
    m->format.is_archive = true;
    xx_format_set_mime_type(&m->format, "application/x-alcohol-mds");
    xx_format_set_extension(&m->format, "mds");
    m->format.check_is_valid = xx_mds_check_is_valid;
    m->format.handle_base_info = xx_mds_handle_base_info;
    m->format.get_format_size = xx_mds_get_format_size;
    m->format.get_number_of_archive_records = xx_mds_get_number_of_archive_records;
    m->format.create_archive_records_reading = xx_mds_create_archive_records_reading;
    m->format.get_current_archive_record = xx_mds_get_current_archive_record;
    m->format.archive_record_move_to_next = xx_mds_archive_record_move_to_next;
    m->format.unpack_current_archive_record = xx_mds_unpack_current_archive_record;
    m->format.free_archive_records_reading = xx_mds_free_archive_records_reading;
    m->format.destroy = mds_destroy_format;
}
xx_mds *xx_mds_create(xx_io_device *device, int64_t base)
{
    xx_mds *m = (xx_mds *)xx_mem_alloc(sizeof(*m));
    if (m) {
        xx_mds_init(m, device, base);
    }
    return m;
}
void xx_mds_destroy(xx_mds *m)
{
    if (!m) return;
    mds_close_data(m);
    mds_release((mds_view *)m->internal);
    m->internal = NULL;
    xx_format_cleanup_extra_parameters(&m->format);
}
void xx_mds_free(xx_mds *m)
{
    if (m) {
        xx_mds_destroy(m);
        xx_mem_free(m);
    }
}
bool xx_mds_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    mds_view *v = mds_parse(f, pd);
    if (!v) {
        return false;
    }
    mds_release(v);
    return true;
}
bool xx_mds_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_mds *m = (xx_mds *)f;
    mds_view *v;
    if (!f || mds_stop(pd)) return false;
    if (f->base_info_handled && m->internal) return f->is_valid;
    v = mds_parse(f, pd);
    if (!v) {
        f->base_info_handled = false;
        f->is_valid = false;
        return false;
    }
    mds_release((mds_view *)m->internal);
    m->internal = v;
    m->number_of_tracks = v->count;
    m->number_of_sessions = v->sessions;
    f->number_of_archive_records = v->count;
    f->format_size = v->descriptor_size;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
int64_t xx_mds_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_mds_handle_base_info(f, pd) ? f->format_size : -1;
}
uint64_t xx_mds_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_mds_handle_base_info(f, pd) ? ((xx_mds *)f)->number_of_tracks : 0U;
}
bool xx_mds_set_data_device(xx_mds *m, xx_io_device *device)
{
    if (!m || !xx_mds_handle_base_info(&m->format, NULL) || (m->data_owned && m->data == device)) return false;
    mds_close_data(m);
    m->data = device;
    return true;
}
bool xx_mds_open_data_file(xx_mds *m, const char *mds_path)
{
    char *path;
    size_t length, i;
    xx_io_device *device;
    if (!m || !mds_path || !xx_mds_handle_base_info(&m->format, NULL)) return false;
    if (m->data) return true;
    length = strlen(mds_path);
    if (length < 4U) return false;
    for (i = length - 4U; i < length; ++i)
        if (mds_path[i] == '/' || mds_path[i] == '\\') return false;
    if (mds_path[length - 4U] != '.' || (mds_path[length - 3U] != 'm' && mds_path[length - 3U] != 'M') ||
        (mds_path[length - 2U] != 'd' && mds_path[length - 2U] != 'D') || (mds_path[length - 1U] != 's' && mds_path[length - 1U] != 'S'))
        return false;
    path = xx_str_dup(mds_path);
    if (!path) return false;
    path[length - 3U] = 'm';
    path[length - 2U] = 'd';
    path[length - 1U] = 'f';
    device = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!device) return false;
    m->data = device;
    m->data_owned = true;
    return true;
}
uint32_t xx_mds_get_number_of_tracks(xx_mds *m)
{
    return m && xx_mds_handle_base_info(&m->format, NULL) ? m->number_of_tracks : 0U;
}
static void mds_name(char name[32], const mds_track *track)
{
    (void)xx_rt_snprintf(name, 32U, "mds-track%02u.%s", track->number, track->mode == 0xA9U ? "cdda" : "bin");
}
static bool mds_record(xx_archive_record *r, const mds_track *t)
{
    char name[32];
    mds_name(name, t);
    xx_archive_record_cleanup(r);
    xx_archive_record_init(r);
    r->header_offset = t->header_offset;
    r->header_size = MDS_BLOCK;
    r->data_offset = -1;
    r->compressed_size = (int64_t)t->length;
    return xx_archive_record_set_original_name(r, name) && xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, t->length) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, t->length) && xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false);
}
static void mds_cursor_free(void *ptr)
{
    mds_cursor *c = (mds_cursor *)ptr;
    if (c) {
        mds_release(c->view);
        xx_mem_free(c);
    }
}
xx_archive_record_state *xx_mds_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_mds *m = (xx_mds *)f;
    mds_view *v;
    mds_cursor *c;
    xx_archive_record_state *state;
    size_t i;
    if (!xx_mds_handle_base_info(f, pd)) return NULL;
    v = (mds_view *)m->internal;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    c = (mds_cursor *)xx_mem_calloc(1U, sizeof(*c));
    if (!state || !c) {
        xx_mem_free(state);
        xx_mem_free(c);
        return NULL;
    }
    ++v->refs;
    c->view = v;
    xx_archive_record_state_init(state, f);
    state->internal_state = c;
    state->free_internal = mds_cursor_free;
    state->total_records = v->count;
    if (options)
        for (i = 0U; i < options->count; ++i) {
            const xx_meta *original = (const xx_meta *)xx_list_at(options, i);
            xx_meta copy;
            if (!original) continue;
            xx_meta_init(&copy, original->meta_id);
            if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(&state->options, &copy)) {
                xx_meta_cleanup(&copy);
                xx_archive_record_state_free(state);
                return NULL;
            }
        }
    if (!mds_record(&state->current_record, &v->tracks[0U])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_mds_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state)
{
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_mds_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    mds_cursor *c;
    if (!f || !state || state->format != f || !state->has_record || mds_stop(pd) || !(c = (mds_cursor *)state->internal_state)) return false;
    ++c->index;
    if (c->index >= c->view->count) {
        state->has_record = false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    ++state->current_index;
    state->has_record = mds_record(&state->current_record, &c->view->tracks[c->index]);
    return state->has_record;
}
static bool mds_limits(Abstractformat *f, xx_archive_record_state *state, const mds_view *v, const mds_track *t)
{
    const xx_var *max = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t required = sizeof(*v) + sizeof(mds_cursor) + MDS_COPY;
    return (!max || t->length <= xx_var_get_u64(max)) && (!mem || required <= xx_var_get_u64(mem));
}
bool xx_mds_extract_record_to_device(Abstractformat *f, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    xx_mds *m = (xx_mds *)f;
    mds_cursor *c;
    const mds_track *t;
    uint8_t buffer[MDS_COPY];
    uint64_t done = 0U;
    int64_t saved;
    bool ok = true;
    if (!m || !m->data || m->data == destination || m->data == f->device || destination == f->device || !state || state->format != f || !state->has_record ||
        mds_stop(pd) || !(c = (mds_cursor *)state->internal_state) || c->index >= c->view->count)
        return false;
    t = &c->view->tracks[c->index];
    if (xx_io_total_size(m->data) != (int64_t)c->view->data_size || !mds_limits(f, state, c->view, t) || (saved = xx_io_tell(m->data)) < 0) return false;
    while (done < t->length && !mds_stop(pd)) {
        size_t take = t->length - done > MDS_COPY ? MDS_COPY : (size_t)(t->length - done);
        size_t got = 0U, wrote = 0U;
        if (t->offset > (uint64_t)INT64_MAX - done || xx_io_seek64(m->data, (int64_t)(t->offset + done), SEEK_SET) != 0) {
            ok = false;
            break;
        }
        while (got < take && !mds_stop(pd)) {
            ssize_t step = xx_io_read(m->data, buffer + got, take - got);
            if (step <= 0 || (size_t)step > take - got || mds_stop(pd)) {
                ok = false;
                break;
            }
            got += (size_t)step;
        }
        if (!ok || got != take) {
            ok = false;
            break;
        }
        while (destination && wrote < take && !mds_stop(pd)) {
            ssize_t step = xx_io_write(destination, buffer + wrote, take - wrote);
            if (step <= 0 || (size_t)step > take - wrote || mds_stop(pd)) {
                ok = false;
                break;
            }
            wrote += (size_t)step;
        }
        if (!ok) break;
        done += take;
    }
    if (xx_io_seek64(m->data, saved, SEEK_SET) != 0) ok = false;
    return ok && done == t->length && !mds_stop(pd);
}
static char mds_fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}
static bool mds_equal(const char *a, const char *b)
{
    while (*a && mds_fold(*a) == mds_fold(*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static xx_io_device *mds_stage(const char *destination, char **stage)
{
    size_t i, parent = 0U;
    unsigned attempt;
    char *directory;
    *stage = NULL;
    directory = xx_str_dup(destination);
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40], *candidate;
        xx_io_device *d;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_mds.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (mds_equal(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        d = xx_io_file_open(candidate, "wbx");
        if (d) {
            *stage = candidate;
            xx_str_free(directory);
            return d;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
bool xx_mds_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    mds_cursor *c;
    const xx_var *option, *ov;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL, name[32];
    bool ok = false, overwrite;
    if (!f || !state || state->format != f || !state->has_record || mds_stop(pd) || !(c = (mds_cursor *)state->internal_state)) return false;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    ov = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = ov && xx_var_get_bool(ov);
    if (!option) return xx_mds_extract_record_to_device(f, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    mds_name(name, &c->view->tracks[c->index]);
    path = *base && base[strlen(base) - 1U] != '/' && base[strlen(base) - 1U] != '\\' ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) || mds_stop(pd)) goto done;
    {
        xx_io_device *output = mds_stage(path, &stage);
        if (!output) goto done;
        ok = xx_mds_extract_record_to_device(f, state, output, pd);
        if (xx_io_close(output) != 0) ok = false;
    }
    if (ok && !mds_stop(pd)) ok = xx_io_file_replace_a(stage, path, overwrite);
    else ok = false;
done:
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}
void xx_mds_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}
