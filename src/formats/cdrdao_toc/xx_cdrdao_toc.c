/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded CDRDAO TOC track reader. Format evidence: cdrdao project TOC
 * documentation and cue2toc 0.4 output validated by cdrdao toc-info.
 * No upstream parser code is incorporated.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cdrdao_toc/xx_cdrdao_toc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef CDRDAO_TOC
#define TOC_TYPE XX_FILE_TYPE_CDRDAO_TOC
#else
#define TOC_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define TOC_MAX_TEXT 65536U
#define TOC_MAX_NAME 255U
#define TOC_COPY 65536U

enum toc_mode_e {
    TOC_AUDIO,
    TOC_MODE1_RAW,
    TOC_MODE1,
    TOC_MODE2_RAW
};
typedef struct toc_track_s {
    char filename[TOC_MAX_NAME + 1U];
    uint64_t offset, length;
    uint32_t sector_size, mode;
    int64_t header_offset;
    bool has_data;
} toc_track;
typedef struct toc_view_s {
    uint32_t refs, count;
    int64_t base, sheet_size;
    toc_track tracks[XX_CDRDAO_TOC_MAX_TRACKS];
} toc_view;
typedef struct toc_cursor_s {
    toc_view *view;
    uint32_t index;
} toc_cursor;
static bool toc_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static void toc_release(toc_view *v)
{
    if (v && !--v->refs) xx_mem_free(v);
}
static char toc_fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}
static bool toc_equal(const char *a, const char *b)
{
    while (*a && toc_fold(*a) == toc_fold(*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static bool toc_blank(char c)
{
    return c == ' ' || c == '\t';
}
static void toc_space(char **p)
{
    while (toc_blank(**p)) ++*p;
}
static bool toc_digit(char c)
{
    return c >= '0' && c <= '9';
}
static bool toc_wave_name(const char *name)
{
    size_t n = strlen(name);
    return n >= 4U && toc_fold(name[n - 4U]) == '.' && toc_fold(name[n - 3U]) == 'w' && toc_fold(name[n - 2U]) == 'a' && toc_fold(name[n - 1U]) == 'v';
}
static bool toc_frames(char **text, uint64_t *frames)
{
    uint64_t parts[3] = {0U, 0U, 0U};
    size_t i;
    char *p = *text;
    toc_space(&p);
    for (i = 0U; i < 3U; ++i) {
        if (!toc_digit(*p)) return false;
        while (toc_digit(*p)) {
            uint32_t digit = (uint32_t)(*p++ - '0');
            if (parts[i] > (UINT64_MAX - digit) / 10U) return false;
            parts[i] = parts[i] * 10U + digit;
        }
        if (i < 2U) {
            if (*p++ != ':') return false;
        }
    }
    if ((*p && !toc_blank(*p)) || parts[1] >= 60U || parts[2] >= 75U || parts[0] > (UINT64_MAX - 4499U) / 4500U) return false;
    *frames = parts[0] * 4500U + parts[1] * 75U + parts[2];
    *text = p;
    return true;
}
static bool toc_name(char **text, char name[TOC_MAX_NAME + 1U])
{
    char *p = *text;
    size_t n = 0U;
    toc_space(&p);
    if (*p++ != '"') return false;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20U || c == 0x7FU || n == TOC_MAX_NAME) return false;
        name[n++] = *p++;
    }
    if (!n || *p++ != '"' || (*p && !toc_blank(*p))) return false;
    name[n] = 0;
    *text = p;
    return true;
}
static bool toc_track_line(char *line, toc_track *track)
{
    toc_space(&line);
    if (!strcmp(line, "AUDIO")) {
        track->mode = TOC_AUDIO;
        track->sector_size = 2352U;
    } else if (!strcmp(line, "MODE1_RAW")) {
        track->mode = TOC_MODE1_RAW;
        track->sector_size = 2352U;
    } else if (!strcmp(line, "MODE1")) {
        track->mode = TOC_MODE1;
        track->sector_size = 2048U;
    } else if (!strcmp(line, "MODE2_RAW")) {
        track->mode = TOC_MODE2_RAW;
        track->sector_size = 2352U;
    } else return false;
    return true;
}
static bool toc_data_line(char *line, toc_track *track, bool audiofile)
{
    uint64_t start = 0U, length = UINT64_MAX;
    if (track->has_data || (track->mode == TOC_AUDIO) != audiofile || !toc_name(&line, track->filename)) return false;
    if (audiofile && toc_wave_name(track->filename)) return false;
    toc_space(&line);
    if (audiofile) {
        if (!toc_frames(&line, &start)) return false;
        toc_space(&line);
    }
    if (*line) {
        if (!toc_frames(&line, &length)) return false;
        toc_space(&line);
    }
    if (*line || start > (uint64_t)INT64_MAX / track->sector_size || (length != UINT64_MAX && (!length || length > (uint64_t)INT64_MAX / track->sector_size)) ||
        (length != UINT64_MAX && start > ((uint64_t)INT64_MAX / track->sector_size) - length))
        return false;
    track->offset = start * track->sector_size;
    track->length = length == UINT64_MAX ? UINT64_MAX : length * track->sector_size;
    track->has_data = true;
    return true;
}
static bool toc_read_at(xx_io_device *d, int64_t at, uint8_t *p, size_t n, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (toc_stop(pd) || xx_io_seek64(d, at, SEEK_SET) != 0) return false;
    while (done < n) {
        ssize_t got;
        if (toc_stop(pd)) return false;
        got = xx_io_read(d, p + done, n - done);
        if (got <= 0 || (size_t)got > n - done || toc_stop(pd)) return false;
        done += (size_t)got;
    }
    return !toc_stop(pd);
}
static toc_view *toc_parse(Abstractformat *f, xx_pd_struct *pd)
{
    xx_io_device *device;
    int64_t saved, total;
    char *text = NULL;
    toc_view *v = NULL;
    size_t at = 0U;
    bool disc = false, ok = false;
    uint32_t i;
    if (!f || !(device = f->device) || f->base_address < 0 || toc_stop(pd)) return NULL;
    saved = xx_io_tell(device);
    total = xx_io_total_size(device);
    if (saved < 0 || total < f->base_address || total - f->base_address < 25 || total - f->base_address > TOC_MAX_TEXT) return NULL;
    text = (char *)xx_mem_alloc((size_t)(total - f->base_address) + 1U);
    v = (toc_view *)xx_mem_calloc(1U, sizeof(*v));
    if (v) v->refs = 1U;
    if (!text || !v || !toc_read_at(device, f->base_address, (uint8_t *)text, (size_t)(total - f->base_address), pd)) goto done;
    v->base = f->base_address;
    v->sheet_size = total - f->base_address;
    text[v->sheet_size] = 0;
    while (at < (size_t)v->sheet_size) {
        size_t start = at, end = at;
        char *line;
        while (end < (size_t)v->sheet_size && text[end] != '\n') {
            if (!text[end] || ((unsigned char)text[end] < 0x20U && text[end] != '\r' && text[end] != '\t')) goto done;
            ++end;
        }
        at = end < (size_t)v->sheet_size ? end + 1U : end;
        if (end > start && text[end - 1U] == '\r') --end;
        text[end] = 0;
        line = text + start;
        toc_space(&line);
        if (!*line || (line[0] == '/' && line[1] == '/')) continue;
        {
            size_t n = strlen(line);
            while (n && toc_blank(line[n - 1U])) line[--n] = 0;
        }
        if (!disc) {
            if (strcmp(line, "CD_ROM") && strcmp(line, "CD_DA")) goto done;
            disc = true;
            continue;
        }
        if (!strncmp(line, "TRACK ", 6U)) {
            toc_track *t;
            if (v->count == XX_CDRDAO_TOC_MAX_TRACKS || (v->count && !v->tracks[v->count - 1U].has_data)) goto done;
            t = &v->tracks[v->count++];
            t->header_offset = v->base + (int64_t)start;
            if (!toc_track_line(line + 6U, t)) goto done;
        } else if (!strncmp(line, "DATAFILE ", 9U) || !strncmp(line, "AUDIOFILE ", 10U)) {
            bool audiofile = line[0] == 'A';
            if (!v->count || !toc_data_line(line + (audiofile ? 10U : 9U), &v->tracks[v->count - 1U], audiofile)) goto done;
        } else goto done; /* PREGAP, START, CD-Text, WAVE, MP3, etc. */
    }
    if (!disc || !v->count || !v->tracks[v->count - 1U].has_data) goto done;
    for (i = 0U; i < v->count; ++i) {
        const toc_track *t = &v->tracks[i];
        uint32_t j;
        for (j = 0U; j < i; ++j) {
            const toc_track *prior = &v->tracks[j];
            if (toc_equal(t->filename, prior->filename) && (prior->length == UINT64_MAX || t->offset < prior->offset + prior->length)) goto done;
        }
    }
    ok = !toc_stop(pd);
done:
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    xx_mem_free(text);
    if (!ok) {
        toc_release(v);
        return NULL;
    }
    return v;
}
static void toc_close_data(xx_cdrdao_toc *t, uint32_t index)
{
    if (t->data_owned[index] && t->data[index]) (void)xx_io_close(t->data[index]);
    t->data[index] = NULL;
    t->data_owned[index] = false;
}
static void toc_destroy_format(Abstractformat *f)
{
    xx_cdrdao_toc_destroy((xx_cdrdao_toc *)f);
}
void xx_cdrdao_toc_init(xx_cdrdao_toc *t, xx_io_device *device, int64_t base)
{
    if (!t) return;
    xx_mem_zero(t, sizeof(*t));
    xx_format_init(&t->format, device, base);
    t->format.file_type = TOC_TYPE;
    t->format.format_type = XX_TYPE_ARCHIVE;
    t->format.is_archive = true;
    xx_format_set_mime_type(&t->format, "application/x-cdrdao-toc");
    xx_format_set_extension(&t->format, "toc");
    t->format.check_is_valid = xx_cdrdao_toc_check_is_valid;
    t->format.handle_base_info = xx_cdrdao_toc_handle_base_info;
    t->format.get_format_size = xx_cdrdao_toc_get_format_size;
    t->format.get_number_of_archive_records = xx_cdrdao_toc_get_number_of_archive_records;
    t->format.create_archive_records_reading = xx_cdrdao_toc_create_archive_records_reading;
    t->format.get_current_archive_record = xx_cdrdao_toc_get_current_archive_record;
    t->format.archive_record_move_to_next = xx_cdrdao_toc_archive_record_move_to_next;
    t->format.unpack_current_archive_record = xx_cdrdao_toc_unpack_current_archive_record;
    t->format.free_archive_records_reading = xx_cdrdao_toc_free_archive_records_reading;
    t->format.destroy = toc_destroy_format;
}
xx_cdrdao_toc *xx_cdrdao_toc_create(xx_io_device *device, int64_t base)
{
    xx_cdrdao_toc *t = (xx_cdrdao_toc *)xx_mem_alloc(sizeof(*t));
    if (t) {
        xx_cdrdao_toc_init(t, device, base);
    }
    return t;
}
void xx_cdrdao_toc_destroy(xx_cdrdao_toc *t)
{
    uint32_t i;
    if (!t) return;
    for (i = 0U; i < XX_CDRDAO_TOC_MAX_TRACKS; ++i) toc_close_data(t, i);
    toc_release((toc_view *)t->internal);
    t->internal = NULL;
    xx_format_cleanup_extra_parameters(&t->format);
}
void xx_cdrdao_toc_free(xx_cdrdao_toc *t)
{
    if (t) {
        xx_cdrdao_toc_destroy(t);
        xx_mem_free(t);
    }
}
bool xx_cdrdao_toc_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    toc_view *v = toc_parse(f, pd);
    if (!v) {
        return false;
    }
    toc_release(v);
    return true;
}
bool xx_cdrdao_toc_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_cdrdao_toc *t = (xx_cdrdao_toc *)f;
    toc_view *v;
    if (!f || toc_stop(pd)) return false;
    if (f->base_info_handled && t->internal) return f->is_valid;
    v = toc_parse(f, pd);
    if (!v) {
        f->base_info_handled = false;
        f->is_valid = false;
        return false;
    }
    toc_release((toc_view *)t->internal);
    t->internal = v;
    t->number_of_tracks = v->count;
    f->number_of_archive_records = v->count;
    f->format_size = v->sheet_size;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
int64_t xx_cdrdao_toc_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_cdrdao_toc_handle_base_info(f, pd) ? f->format_size : -1;
}
uint64_t xx_cdrdao_toc_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_cdrdao_toc_handle_base_info(f, pd) ? ((xx_cdrdao_toc *)f)->number_of_tracks : 0U;
}
bool xx_cdrdao_toc_set_data_device(xx_cdrdao_toc *t, uint32_t index, xx_io_device *device)
{
    if (!t || !xx_cdrdao_toc_handle_base_info(&t->format, NULL) || index >= t->number_of_tracks || (t->data_owned[index] && t->data[index] == device)) return false;
    toc_close_data(t, index);
    t->data[index] = device;
    return true;
}
static bool toc_safe_name(const char *name)
{
    static const char *const reserved[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t n, stem, i;
    char word[16];
    if (!name) {
        return false;
    }
    n = strlen(name);
    if (!n || n > TOC_MAX_NAME || name[n - 1U] == '.' || name[n - 1U] == ' ') return false;
    for (i = 0U; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
    }
    stem = 0U;
    while (stem < n && name[stem] != '.') ++stem;
    while (stem && name[stem - 1U] == ' ') --stem;
    if (!stem || stem >= sizeof(word)) return stem != 0U;
    for (i = 0U; i < stem; ++i) word[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 32) : name[i];
    word[stem] = 0;
    for (i = 0U; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (!strcmp(word, reserved[i])) return false;
    if (stem == 4U && ((!strncmp(word, "COM", 3U) || !strncmp(word, "LPT", 3U)) && word[3] >= '1' && word[3] <= '9')) return false;
    return true;
}
uint32_t xx_cdrdao_toc_open_data_files(xx_cdrdao_toc *t, const char *toc_path)
{
    toc_view *v;
    size_t directory = 0U, i;
    uint32_t track, opened = 0U;
    if (!t || !toc_path || !xx_cdrdao_toc_handle_base_info(&t->format, NULL)) return 0U;
    v = (toc_view *)t->internal;
    for (i = 0U; toc_path[i]; ++i)
        if (toc_path[i] == '/' || toc_path[i] == '\\') directory = i + 1U;
    for (track = 0U; track < v->count; ++track) {
        const char *name = v->tracks[track].filename;
        char *path;
        xx_io_device *device;
        if (t->data[track]) {
            ++opened;
            continue;
        }
        if (!toc_safe_name(name) || directory > SIZE_MAX - strlen(name) - 1U) continue;
        path = (char *)xx_mem_alloc(directory + strlen(name) + 1U);
        if (!path) continue;
        memcpy(path, toc_path, directory);
        memcpy(path + directory, name, strlen(name) + 1U);
        device = xx_io_file_open(path, "rb");
        xx_mem_free(path);
        if (!device) continue;
        t->data[track] = device;
        t->data_owned[track] = true;
        ++opened;
    }
    return opened;
}
uint32_t xx_cdrdao_toc_get_number_of_tracks(xx_cdrdao_toc *t)
{
    return t && xx_cdrdao_toc_handle_base_info(&t->format, NULL) ? t->number_of_tracks : 0U;
}
char *xx_cdrdao_toc_get_file_name(xx_cdrdao_toc *t, uint32_t index)
{
    toc_view *v;
    if (!t || !xx_cdrdao_toc_handle_base_info(&t->format, NULL)) return NULL;
    v = (toc_view *)t->internal;
    return index < v->count ? xx_str_dup(v->tracks[index].filename) : NULL;
}
static bool toc_length(const xx_cdrdao_toc *t, const toc_view *v, uint32_t index, uint64_t *length)
{
    const toc_track *track = &v->tracks[index];
    int64_t size;
    uint64_t end, n;
    if (!t->data[index]) return false;
    size = xx_io_total_size(t->data[index]);
    if (size < 0 || track->offset > (uint64_t)size) return false;
    end = (uint64_t)size;
    if (track->length == UINT64_MAX) n = end - track->offset;
    else {
        n = track->length;
        if (n > end - track->offset) return false;
    }
    if (!n || n % track->sector_size) return false;
    *length = n;
    return true;
}
static void toc_member_name(char name[32], uint32_t index, const toc_track *track)
{
    const char *extension = track->mode == TOC_AUDIO ? "cdda" : track->mode == TOC_MODE1 ? "iso" : "bin";
    (void)xx_rt_snprintf(name, 32U, "toc-track%02u.%s", index + 1U, extension);
}
static bool toc_record(xx_archive_record *r, const toc_view *v, const xx_cdrdao_toc *t, uint32_t index)
{
    const toc_track *track = &v->tracks[index];
    uint64_t length;
    char name[32];
    toc_member_name(name, index, track);
    xx_archive_record_cleanup(r);
    xx_archive_record_init(r);
    r->header_offset = track->header_offset;
    r->header_size = 0;
    r->data_offset = -1;
    if (!xx_archive_record_set_original_name(r, name) || !xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, 0U) ||
        !xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) || !xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false))
        return false;
    length = track->length;
    if (length == UINT64_MAX && !toc_length(t, v, index, &length)) return true;
    if (length != UINT64_MAX) {
        r->compressed_size = (int64_t)length;
        if (!xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, length) || !xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, length))
            return false;
    }
    return true;
}
static void toc_cursor_free(void *ptr)
{
    toc_cursor *c = (toc_cursor *)ptr;
    if (c) {
        toc_release(c->view);
        xx_mem_free(c);
    }
}
xx_archive_record_state *xx_cdrdao_toc_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_cdrdao_toc *t = (xx_cdrdao_toc *)f;
    toc_view *v;
    toc_cursor *c;
    xx_archive_record_state *state;
    size_t i;
    if (!xx_cdrdao_toc_handle_base_info(f, pd)) return NULL;
    v = (toc_view *)t->internal;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    c = (toc_cursor *)xx_mem_calloc(1U, sizeof(*c));
    if (!state || !c) {
        xx_mem_free(state);
        xx_mem_free(c);
        return NULL;
    }
    ++v->refs;
    c->view = v;
    xx_archive_record_state_init(state, f);
    state->internal_state = c;
    state->free_internal = toc_cursor_free;
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
    if (!toc_record(&state->current_record, v, t, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_cdrdao_toc_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state)
{
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_cdrdao_toc_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    toc_cursor *c;
    if (!f || !state || state->format != f || !state->has_record || toc_stop(pd) || !(c = (toc_cursor *)state->internal_state)) return false;
    ++c->index;
    if (c->index >= c->view->count) {
        state->has_record = false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    ++state->current_index;
    state->has_record = toc_record(&state->current_record, c->view, (xx_cdrdao_toc *)f, c->index);
    return state->has_record;
}
static bool toc_limits(Abstractformat *f, xx_archive_record_state *state, const toc_view *v, uint64_t length)
{
    const xx_var *max = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t needed = sizeof(*v) + sizeof(toc_cursor) + TOC_COPY;
    return (!max || length <= xx_var_get_u64(max)) && (!mem || needed <= xx_var_get_u64(mem));
}
bool xx_cdrdao_toc_extract_record_to_device(Abstractformat *f, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    xx_cdrdao_toc *t = (xx_cdrdao_toc *)f;
    toc_cursor *c;
    const toc_track *track;
    xx_io_device *source;
    uint64_t length, done = 0U;
    int64_t saved;
    uint8_t buffer[TOC_COPY];
    bool ok = true;
    if (!t || !state || state->format != f || !state->has_record || toc_stop(pd) || !(c = (toc_cursor *)state->internal_state) || c->index >= c->view->count)
        return false;
    track = &c->view->tracks[c->index];
    source = t->data[c->index];
    if (!source || source == destination || source == f->device || destination == f->device || !toc_length(t, c->view, c->index, &length) ||
        !toc_limits(f, state, c->view, length) || (saved = xx_io_tell(source)) < 0)
        return false;
    while (done < length && !toc_stop(pd)) {
        size_t take = length - done > TOC_COPY ? TOC_COPY : (size_t)(length - done);
        size_t got = 0U, wrote = 0U;
        if (track->offset > (uint64_t)INT64_MAX - done || xx_io_seek64(source, (int64_t)(track->offset + done), SEEK_SET) != 0) {
            ok = false;
            break;
        }
        while (got < take && !toc_stop(pd)) {
            ssize_t step = xx_io_read(source, buffer + got, take - got);
            if (step <= 0 || (size_t)step > take - got || toc_stop(pd)) {
                ok = false;
                break;
            }
            got += (size_t)step;
        }
        if (!ok || got != take) {
            ok = false;
            break;
        }
        while (destination && wrote < take && !toc_stop(pd)) {
            ssize_t step = xx_io_write(destination, buffer + wrote, take - wrote);
            if (step <= 0 || (size_t)step > take - wrote || toc_stop(pd)) {
                ok = false;
                break;
            }
            wrote += (size_t)step;
        }
        if (!ok) break;
        done += take;
    }
    if (xx_io_seek64(source, saved, SEEK_SET) != 0) ok = false;
    return ok && done == length && !toc_stop(pd);
}
static xx_io_device *toc_stage(const char *destination, char **stage)
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
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_toc.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (toc_equal(candidate, destination)) {
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
bool xx_cdrdao_toc_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    toc_cursor *c;
    const xx_var *option, *ov;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL, name[32];
    bool ok = false, overwrite;
    if (!f || !state || state->format != f || !state->has_record || toc_stop(pd) || !(c = (toc_cursor *)state->internal_state)) return false;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    ov = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = ov && xx_var_get_bool(ov);
    if (!option) return xx_cdrdao_toc_extract_record_to_device(f, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    toc_member_name(name, c->index, &c->view->tracks[c->index]);
    path = *base && base[strlen(base) - 1U] != '/' && base[strlen(base) - 1U] != '\\' ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) || toc_stop(pd)) goto done;
    {
        xx_io_device *output = toc_stage(path, &stage);
        if (!output) goto done;
        ok = xx_cdrdao_toc_extract_record_to_device(f, state, output, pd);
        if (xx_io_close(output) != 0) ok = false;
    }
    if (ok && !toc_stop(pd)) ok = xx_io_file_replace_a(stage, path, overwrite);
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
void xx_cdrdao_toc_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}
bool xx_cdrdao_toc_test_magic(const uint8_t *p, size_t n)
{
    size_t i = 0U;
    if (!p) return false;
    while (i < n) {
        while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) ++i;
        if (i + 1U < n && p[i] == '/' && p[i + 1U] == '/') {
            while (i < n && p[i] != '\n') ++i;
            continue;
        }
        return (n - i >= 6U && !memcmp(p + i, "CD_ROM", 6U)) || (n - i >= 5U && !memcmp(p + i, "CD_DA", 5U));
    }
    return false;
}
