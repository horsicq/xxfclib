/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Dreamcast GDI: independent parser of the six-field track descriptor.
 * Format evidence: mkdcdisc docs/gd-image.md and src/build_gd.cpp, commit
 * 2b98b0d9480e364e3517355648fbc3b49fb19882. No upstream code copied.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gdi/xx_gdi.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef GDI
#define GDI_TYPE XX_FILE_TYPE_GDI
#else
#define GDI_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define GDI_MAX_SHEET 65536U
#define GDI_MAX_NAME 255U
#define GDI_COPY 65536U

typedef struct gdi_track_s {
    uint32_t number, control, sector_size, lba;
    uint64_t file_offset;
    int64_t line_offset;
    uint32_t line_size;
    char filename[GDI_MAX_NAME + 1U];
} gdi_track;
typedef struct gdi_view_s {
    uint32_t refs, count;
    int64_t sheet_size, base;
    gdi_track tracks[XX_GDI_MAX_TRACKS];
} gdi_view;
typedef struct gdi_cursor_s {
    gdi_view *view;
    uint32_t index;
} gdi_cursor;

static bool gdi_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static void gdi_release(gdi_view *v)
{
    if (v && !--v->refs) xx_mem_free(v);
}
static bool gdi_blank(char c)
{
    return c == ' ' || c == '\t';
}
static bool gdi_digit(char c)
{
    return c >= '0' && c <= '9';
}
static char gdi_fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}
static bool gdi_equal(const char *a, const char *b)
{
    while (*a && gdi_fold(*a) == gdi_fold(*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static void gdi_spaces(char **text)
{
    while (gdi_blank(**text)) ++*text;
}
static bool gdi_number(char **text, uint64_t *value)
{
    uint64_t n = 0U;
    char *p = *text;
    gdi_spaces(&p);
    if (!gdi_digit(*p)) return false;
    while (gdi_digit(*p)) {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (n > (UINT64_MAX - digit) / 10U) return false;
        n = n * 10U + digit;
    }
    if (*p && !gdi_blank(*p)) return false;
    *value = n;
    *text = p;
    return true;
}
static bool gdi_filename(char **text, char name[GDI_MAX_NAME + 1U])
{
    char *p = *text;
    size_t n = 0U;
    bool quoted = false;
    gdi_spaces(&p);
    if (*p == '"') {
        quoted = true;
        ++p;
    }
    while (*p && ((quoted && *p != '"') || (!quoted && !gdi_blank(*p)))) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20U || c == 0x7FU || n == GDI_MAX_NAME) return false;
        name[n++] = *p++;
    }
    if (!n || (quoted && *p++ != '"') || (*p && !gdi_blank(*p))) return false;
    name[n] = 0;
    *text = p;
    return true;
}
static bool gdi_track_line(char *line, gdi_track *t, uint32_t expected)
{
    uint64_t number, lba, control, sector, offset;
    if (!gdi_number(&line, &number) || !gdi_number(&line, &lba) || !gdi_number(&line, &control) || !gdi_number(&line, &sector) || !gdi_filename(&line, t->filename) ||
        !gdi_number(&line, &offset))
        return false;
    gdi_spaces(&line);
    if (*line || number != expected || lba > UINT32_MAX || offset > INT64_MAX ||
        !((control == 0U && sector == 2352U) || (control == 4U && (sector == 2048U || sector == 2352U))))
        return false;
    t->number = (uint32_t)number;
    t->lba = (uint32_t)lba;
    t->control = (uint32_t)control;
    t->sector_size = (uint32_t)sector;
    t->file_offset = offset;
    return true;
}
static bool gdi_read_at(xx_io_device *device, int64_t at, uint8_t *data, size_t length, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (gdi_stop(pd) || xx_io_seek64(device, at, SEEK_SET) != 0) return false;
    while (done < length) {
        ssize_t got;
        if (gdi_stop(pd)) return false;
        got = xx_io_read(device, data + done, length - done);
        if (got <= 0 || (size_t)got > length - done || gdi_stop(pd)) return false;
        done += (size_t)got;
    }
    return !gdi_stop(pd);
}
static gdi_view *gdi_parse(Abstractformat *f, xx_pd_struct *pd)
{
    xx_io_device *device;
    int64_t size, saved;
    gdi_view *v = NULL;
    char *sheet = NULL;
    size_t at = 0U;
    uint32_t found = 0U;
    bool first = true, ok = false;
    if (!f || !(device = f->device) || f->base_address < 0 || gdi_stop(pd)) return NULL;
    saved = xx_io_tell(device);
    size = xx_io_total_size(device);
    if (saved < 0 || size < f->base_address || size - f->base_address < 8 || size - f->base_address > GDI_MAX_SHEET) return NULL;
    sheet = (char *)xx_mem_alloc((size_t)(size - f->base_address) + 1U);
    v = (gdi_view *)xx_mem_calloc(1U, sizeof(*v));
    if (v) v->refs = 1U;
    if (!sheet || !v || !gdi_read_at(device, f->base_address, (uint8_t *)sheet, (size_t)(size - f->base_address), pd)) goto done;
    sheet[size - f->base_address] = 0;
    v->sheet_size = size - f->base_address;
    v->base = f->base_address;
    if ((uint8_t)sheet[0] == 0xEFU && (uint8_t)sheet[1] == 0xBBU && (uint8_t)sheet[2] == 0xBFU) at = 3U;
    while (at < (size_t)v->sheet_size) {
        size_t start = at, end = at;
        char *line;
        uint64_t count;
        while (end < (size_t)v->sheet_size && sheet[end] != '\n') {
            if (!sheet[end] || ((unsigned char)sheet[end] < 0x20U && sheet[end] != '\r' && sheet[end] != '\t')) goto done;
            ++end;
        }
        at = end < (size_t)v->sheet_size ? end + 1U : end;
        if (end > start && sheet[end - 1U] == '\r') --end;
        sheet[end] = 0;
        line = sheet + start;
        gdi_spaces(&line);
        if (!*line) {
            if (first || found < v->count) goto done;
            continue;
        }
        if (first) {
            if (!gdi_number(&line, &count)) goto done;
            gdi_spaces(&line);
            if (*line || !count || count > XX_GDI_MAX_TRACKS) goto done;
            v->count = (uint32_t)count;
            first = false;
        } else {
            gdi_track *t;
            uint32_t i;
            if (found == v->count) goto done;
            t = &v->tracks[found];
            if (!gdi_track_line(line, t, found + 1U)) goto done;
            if (found && t->lba <= v->tracks[found - 1U].lba) goto done;
            for (i = 0U; i < found; ++i)
                if (gdi_equal(t->filename, v->tracks[i].filename) && t->file_offset <= v->tracks[i].file_offset) goto done;
            t->line_offset = v->base + (int64_t)start;
            t->line_size = (uint32_t)(at - start);
            ++found;
        }
    }
    ok = !first && found == v->count && !gdi_stop(pd);
done:
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    xx_mem_free(sheet);
    if (!ok) {
        gdi_release(v);
        return NULL;
    }
    return v;
}
static void gdi_release_data(xx_gdi *g, uint32_t index)
{
    if (g->data_owned[index] && g->data[index]) (void)xx_io_close(g->data[index]);
    g->data[index] = NULL;
    g->data_owned[index] = false;
}
static void gdi_destroy_format(Abstractformat *f)
{
    xx_gdi_destroy((xx_gdi *)f);
}
void xx_gdi_init(xx_gdi *g, xx_io_device *device, int64_t base)
{
    if (!g) return;
    xx_mem_zero(g, sizeof(*g));
    xx_format_init(&g->format, device, base);
    g->format.file_type = GDI_TYPE;
    g->format.format_type = XX_TYPE_ARCHIVE;
    g->format.is_archive = true;
    xx_format_set_mime_type(&g->format, "application/x-dreamcast-gdi");
    xx_format_set_extension(&g->format, "gdi");
    g->format.check_is_valid = xx_gdi_check_is_valid;
    g->format.handle_base_info = xx_gdi_handle_base_info;
    g->format.get_format_size = xx_gdi_get_format_size;
    g->format.get_number_of_archive_records = xx_gdi_get_number_of_archive_records;
    g->format.create_archive_records_reading = xx_gdi_create_archive_records_reading;
    g->format.get_current_archive_record = xx_gdi_get_current_archive_record;
    g->format.archive_record_move_to_next = xx_gdi_archive_record_move_to_next;
    g->format.unpack_current_archive_record = xx_gdi_unpack_current_archive_record;
    g->format.free_archive_records_reading = xx_gdi_free_archive_records_reading;
    g->format.destroy = gdi_destroy_format;
}
xx_gdi *xx_gdi_create(xx_io_device *device, int64_t base)
{
    xx_gdi *g = (xx_gdi *)xx_mem_alloc(sizeof(*g));
    if (g) xx_gdi_init(g, device, base);
    return g;
}
void xx_gdi_destroy(xx_gdi *g)
{
    uint32_t i;
    if (!g) return;
    for (i = 0U; i < XX_GDI_MAX_TRACKS; ++i) gdi_release_data(g, i);
    gdi_release((gdi_view *)g->internal);
    g->internal = NULL;
    xx_format_cleanup_extra_parameters(&g->format);
}
void xx_gdi_free(xx_gdi *g)
{
    if (g) {
        xx_gdi_destroy(g);
        xx_mem_free(g);
    }
}
bool xx_gdi_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    gdi_view *v = gdi_parse(f, pd);
    if (!v) return false;
    gdi_release(v);
    return true;
}
bool xx_gdi_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_gdi *g = (xx_gdi *)f;
    gdi_view *v;
    if (!f || gdi_stop(pd)) return false;
    if (f->base_info_handled && g->internal) return f->is_valid;
    v = gdi_parse(f, pd);
    if (!v) {
        f->base_info_handled = false;
        f->is_valid = false;
        return false;
    }
    gdi_release((gdi_view *)g->internal);
    g->internal = v;
    g->number_of_tracks = v->count;
    f->number_of_archive_records = v->count;
    f->format_size = v->sheet_size;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
int64_t xx_gdi_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_gdi_handle_base_info(f, pd) ? f->format_size : -1;
}
uint64_t xx_gdi_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_gdi_handle_base_info(f, pd) ? ((xx_gdi *)f)->number_of_tracks : 0U;
}
bool xx_gdi_set_data_device(xx_gdi *g, uint32_t index, xx_io_device *device)
{
    if (!g || !xx_gdi_handle_base_info(&g->format, NULL) || index >= g->number_of_tracks || (g->data_owned[index] && g->data[index] == device)) return false;
    gdi_release_data(g, index);
    g->data[index] = device;
    return true;
}
static bool gdi_safe_sidecar(const char *name)
{
    static const char *const reserved[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t i, n, stem;
    char word[16];
    if (!name) return false;
    n = strlen(name);
    if (!n || n > GDI_MAX_NAME || name[n - 1U] == '.' || name[n - 1U] == ' ') return false;
    for (i = 0U; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
    }
    stem = 0U;
    while (stem < n && name[stem] != '.') ++stem;
    while (stem && name[stem - 1U] == ' ') --stem;
    if (!stem || stem >= sizeof(word)) return stem != 0U;
    for (i = 0U; i < stem; ++i) {
        char c = name[i];
        word[i] = c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
    }
    word[stem] = 0;
    for (i = 0U; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (!strcmp(word, reserved[i])) return false;
    if (stem == 4U && ((!strncmp(word, "COM", 3U) || !strncmp(word, "LPT", 3U)) && word[3] >= '1' && word[3] <= '9')) return false;
    return true;
}
uint32_t xx_gdi_open_data_files(xx_gdi *g, const char *gdi_path)
{
    gdi_view *v;
    size_t directory = 0U, i;
    uint32_t opened = 0U, t;
    if (!g || !gdi_path || !xx_gdi_handle_base_info(&g->format, NULL)) return 0U;
    v = (gdi_view *)g->internal;
    for (i = 0U; gdi_path[i]; ++i)
        if (gdi_path[i] == '/' || gdi_path[i] == '\\') directory = i + 1U;
    for (t = 0U; t < v->count; ++t) {
        const char *name = v->tracks[t].filename;
        char *path;
        xx_io_device *device;
        if (g->data[t]) {
            ++opened;
            continue;
        }
        if (!gdi_safe_sidecar(name) || directory > SIZE_MAX - strlen(name) - 1U) continue;
        path = (char *)xx_mem_alloc(directory + strlen(name) + 1U);
        if (!path) continue;
        memcpy(path, gdi_path, directory);
        memcpy(path + directory, name, strlen(name) + 1U);
        device = xx_io_file_open(path, "rb");
        xx_mem_free(path);
        if (!device) continue;
        g->data[t] = device;
        g->data_owned[t] = true;
        ++opened;
    }
    return opened;
}
uint32_t xx_gdi_get_number_of_tracks(xx_gdi *g)
{
    return g && xx_gdi_handle_base_info(&g->format, NULL) ? g->number_of_tracks : 0U;
}
char *xx_gdi_get_track_file_name(xx_gdi *g, uint32_t index)
{
    gdi_view *v;
    if (!g || !xx_gdi_handle_base_info(&g->format, NULL)) return NULL;
    v = (gdi_view *)g->internal;
    return index < v->count ? xx_str_dup(v->tracks[index].filename) : NULL;
}
static bool gdi_length(const xx_gdi *g, const gdi_view *v, uint32_t index, uint64_t *length)
{
    const gdi_track *t = &v->tracks[index];
    int64_t total;
    uint64_t end;
    uint32_t i;
    if (!g->data[index]) return false;
    total = xx_io_total_size(g->data[index]);
    if (total < 0 || t->file_offset > (uint64_t)total) return false;
    end = (uint64_t)total;
    for (i = index + 1U; i < v->count; ++i)
        if (gdi_equal(t->filename, v->tracks[i].filename) && v->tracks[i].file_offset < end) end = v->tracks[i].file_offset;
    if (end <= t->file_offset || (end - t->file_offset) % t->sector_size) return false;
    *length = end - t->file_offset;
    return true;
}
static void gdi_member_name(char name[32], const gdi_track *t)
{
    const char *extension = t->control == 0U ? "cdda" : t->sector_size == 2048U ? "iso" : "bin";
    (void)xx_rt_snprintf(name, 32U, "gdi-track%02u.%s", t->number, extension);
}
static bool gdi_record(xx_archive_record *record, const gdi_view *v, const xx_gdi *g, uint32_t index)
{
    const gdi_track *t = &v->tracks[index];
    uint64_t length;
    char name[32];
    gdi_member_name(name, t);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = t->line_offset;
    record->header_size = t->line_size;
    record->data_offset = -1;
    if (!xx_archive_record_set_original_name(record, name) || !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) || !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (gdi_length(g, v, index, &length)) {
        record->compressed_size = (int64_t)length;
        if (!xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, length) || !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, length))
            return false;
    }
    return true;
}
static void gdi_cursor_free(void *ptr)
{
    gdi_cursor *cursor = (gdi_cursor *)ptr;
    if (cursor) {
        gdi_release(cursor->view);
        xx_mem_free(cursor);
    }
}
xx_archive_record_state *xx_gdi_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_gdi *g = (xx_gdi *)f;
    gdi_view *v;
    gdi_cursor *cursor;
    xx_archive_record_state *state;
    size_t i;
    if (!xx_gdi_handle_base_info(f, pd)) return NULL;
    v = (gdi_view *)g->internal;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    cursor = (gdi_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        xx_mem_free(state);
        xx_mem_free(cursor);
        return NULL;
    }
    ++v->refs;
    cursor->view = v;
    xx_archive_record_state_init(state, f);
    state->internal_state = cursor;
    state->free_internal = gdi_cursor_free;
    state->total_records = (int64_t)v->count;
    if (options)
        for (i = 0U; i < options->count; ++i) {
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
    if (!gdi_record(&state->current_record, v, g, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_gdi_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state)
{
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_gdi_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    gdi_cursor *cursor;
    if (!f || !state || state->format != f || !state->has_record || gdi_stop(pd) || !(cursor = (gdi_cursor *)state->internal_state)) return false;
    ++cursor->index;
    if (cursor->index >= cursor->view->count) {
        state->has_record = false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    ++state->current_index;
    state->has_record = gdi_record(&state->current_record, cursor->view, (xx_gdi *)f, cursor->index);
    return state->has_record;
}
static bool gdi_limits(Abstractformat *f, xx_archive_record_state *state, const gdi_view *v, uint64_t length)
{
    const xx_var *max = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t required = sizeof(*v) + sizeof(gdi_cursor) + GDI_COPY;
    return (!max || length <= xx_var_get_u64(max)) && (!mem || required <= xx_var_get_u64(mem));
}
bool xx_gdi_extract_record_to_device(Abstractformat *f, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    xx_gdi *g = (xx_gdi *)f;
    gdi_cursor *cursor;
    const gdi_track *t;
    xx_io_device *source;
    uint64_t length, done = 0U;
    int64_t saved;
    uint8_t buffer[GDI_COPY];
    bool ok = true;
    if (!g || !state || state->format != f || !state->has_record || gdi_stop(pd) || !(cursor = (gdi_cursor *)state->internal_state) ||
        cursor->index >= cursor->view->count)
        return false;
    t = &cursor->view->tracks[cursor->index];
    source = g->data[cursor->index];
    if (!source || source == destination || source == f->device || destination == f->device || !gdi_length(g, cursor->view, cursor->index, &length) ||
        !gdi_limits(f, state, cursor->view, length) || (saved = xx_io_tell(source)) < 0)
        return false;
    while (done < length && !gdi_stop(pd)) {
        size_t take = length - done > GDI_COPY ? GDI_COPY : (size_t)(length - done);
        size_t got = 0U, wrote = 0U;
        if (t->file_offset > (uint64_t)INT64_MAX - done || xx_io_seek64(source, (int64_t)(t->file_offset + done), SEEK_SET) != 0) {
            ok = false;
            break;
        }
        while (got < take && !gdi_stop(pd)) {
            ssize_t step = xx_io_read(source, buffer + got, take - got);
            if (step <= 0 || (size_t)step > take - got || gdi_stop(pd)) {
                ok = false;
                break;
            }
            got += (size_t)step;
        }
        if (!ok || got != take) {
            ok = false;
            break;
        }
        while (destination && wrote < take && !gdi_stop(pd)) {
            ssize_t step = xx_io_write(destination, buffer + wrote, take - wrote);
            if (step <= 0 || (size_t)step > take - wrote || gdi_stop(pd)) {
                ok = false;
                break;
            }
            wrote += (size_t)step;
        }
        if (!ok) break;
        done += take;
    }
    if (xx_io_seek64(source, saved, SEEK_SET) != 0) ok = false;
    return ok && done == length && !gdi_stop(pd);
}
static xx_io_device *gdi_stage(const char *destination, char **stage)
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
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_gdi.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (gdi_equal(candidate, destination)) {
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
bool xx_gdi_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    gdi_cursor *cursor;
    const xx_var *option, *ov;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL, name[32];
    bool ok = false, overwrite;
    if (!f || !state || state->format != f || !state->has_record || gdi_stop(pd) || !(cursor = (gdi_cursor *)state->internal_state)) return false;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    ov = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = ov && xx_var_get_bool(ov);
    if (!option) return xx_gdi_extract_record_to_device(f, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    gdi_member_name(name, &cursor->view->tracks[cursor->index]);
    path = *base && base[strlen(base) - 1U] != '/' && base[strlen(base) - 1U] != '\\' ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) || gdi_stop(pd)) goto done;
    {
        xx_io_device *output = gdi_stage(path, &stage);
        if (!output) goto done;
        ok = xx_gdi_extract_record_to_device(f, state, output, pd);
        if (xx_io_close(output) != 0) ok = false;
    }
    if (ok && !gdi_stop(pd)) ok = xx_io_file_replace_a(stage, path, overwrite);
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
void xx_gdi_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}
bool xx_gdi_test_magic(const uint8_t *magic, size_t length)
{
    size_t i = 0U, digits = 0U;
    if (!magic) return false;
    if (length >= 3U && magic[0] == 0xEFU && magic[1] == 0xBBU && magic[2] == 0xBFU) i = 3U;
    while (i < length && (magic[i] == ' ' || magic[i] == '\t')) ++i;
    while (i < length && magic[i] >= '0' && magic[i] <= '9' && digits < 2U) {
        ++i;
        ++digits;
    }
    if (!digits || i == length || (magic[i] != '\n' && magic[i] != '\r' && magic[i] != ' ' && magic[i] != '\t')) return false;
    while (i < length && magic[i] != '\n') ++i;
    if (i == length) return false;
    ++i;
    while (i < length && (magic[i] == ' ' || magic[i] == '\t')) ++i;
    return i + 1U < length && magic[i] == '1' && (magic[i + 1U] == ' ' || magic[i + 1U] == '\t');
}
