/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CloneCD descriptor/sidecar reader. Format evidence: GNU ccd2cue manual and
 * mistydemeo/cue2ccd v1.1.0 generated CCD/IMG/SUB sets. No source imported.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ccd/xx_ccd.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef CCD
#define CCD_TYPE XX_FILE_TYPE_CCD
#else
#define CCD_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define CCD_MAX_DESCRIPTOR (128U * 1024U)
#define CCD_MAX_TRACKS 99U
#define CCD_MAX_ENTRIES 102U
#define CCD_IMG_SECTOR 2352U
#define CCD_SUB_SECTOR 96U
#define CCD_COPY 65536U

typedef struct ccd_entry_s {
    bool seen, point_set, control_set, plba_set, pmin_set;
    bool session_set, adr_set;
    uint32_t point, control, plba, pmin, session, adr;
} ccd_entry;
typedef struct ccd_track_s {
    bool section, mode_set, index_set, index0_set;
    uint32_t mode, index1;
    int64_t header_offset;
} ccd_track;
typedef struct ccd_view_s {
    uint32_t refs, count, leadout;
    int64_t base, descriptor_size;
    ccd_track tracks[CCD_MAX_TRACKS];
} ccd_view;
typedef struct ccd_cursor_s {
    ccd_view *view;
    uint32_t index;
    bool has_sub;
} ccd_cursor;
enum ccd_section_e {
    CCD_NONE,
    CCD_CLONE,
    CCD_DISC,
    CCD_SESSION,
    CCD_ENTRY,
    CCD_TRACK
};
static bool ccd_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static void ccd_release(ccd_view *v)
{
    if (v && !--v->refs) xx_mem_free(v);
}
static char ccd_fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}
static bool ccd_equal(const char *a, const char *b)
{
    while (*a && ccd_fold(*a) == ccd_fold(*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static bool ccd_blank(char c)
{
    return c == ' ' || c == '\t';
}
static void ccd_trim(char **line)
{
    size_t n;
    while (ccd_blank(**line)) ++*line;
    n = strlen(*line);
    while (n && ccd_blank((*line)[n - 1U])) (*line)[--n] = 0;
}
static bool ccd_number(const char *text, uint32_t *out)
{
    uint64_t n = 0U;
    uint32_t base = 10U;
    bool digit = false;
    if (!text || !*text || *text == '-' || *text == '+') return false;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
        base = 16U;
    }
    while (*text) {
        uint32_t d;
        char c = *text++;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (base == 16U && ccd_fold(c) >= 'a' && ccd_fold(c) <= 'f') d = (uint32_t)(ccd_fold(c) - 'a' + 10);
        else return false;
        if (d >= base || n > (UINT32_MAX - d) / base) return false;
        n = n * base + d;
        digit = true;
    }
    if (!digit) return false;
    *out = (uint32_t)n;
    return true;
}
static bool ccd_read_at(xx_io_device *d, int64_t at, uint8_t *p, size_t n, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (ccd_stop(pd) || xx_io_seek64(d, at, SEEK_SET) != 0) return false;
    while (done < n) {
        ssize_t got;
        if (ccd_stop(pd)) return false;
        got = xx_io_read(d, p + done, n - done);
        if (got <= 0 || (size_t)got > n - done || ccd_stop(pd)) return false;
        done += (size_t)got;
    }
    return !ccd_stop(pd);
}
static bool ccd_set_section(char *line, enum ccd_section_e *kind, uint32_t *id, ccd_entry entries[CCD_MAX_ENTRIES], ccd_track tracks[CCD_MAX_TRACKS], int64_t offset)
{
    char *body;
    size_t n = strlen(line);
    uint32_t index;
    if (n < 3U || line[0] != '[' || line[n - 1U] != ']') return false;
    line[n - 1U] = 0;
    body = line + 1U;
    if (ccd_equal(body, "CloneCD")) {
        *kind = CCD_CLONE;
        return true;
    }
    if (ccd_equal(body, "Disc")) {
        *kind = CCD_DISC;
        return true;
    }
    if (ccd_equal(body, "Session 1")) {
        *kind = CCD_SESSION;
        return true;
    }
    if (strncmp(body, "Entry ", 6U) == 0 && ccd_number(body + 6U, &index) && index < CCD_MAX_ENTRIES && !entries[index].seen) {
        entries[index].seen = true;
        *kind = CCD_ENTRY;
        *id = index;
        return true;
    }
    if (strncmp(body, "TRACK ", 6U) == 0 && ccd_number(body + 6U, &index) && index >= 1U && index <= CCD_MAX_TRACKS && !tracks[index - 1U].section) {
        tracks[index - 1U].section = true;
        tracks[index - 1U].header_offset = offset;
        *kind = CCD_TRACK;
        *id = index - 1U;
        return true;
    }
    return false;
}
static bool ccd_set_u32(bool *seen, uint32_t *field, const char *value)
{
    if (*seen || !ccd_number(value, field)) return false;
    *seen = true;
    return true;
}
static ccd_view *ccd_parse(Abstractformat *f, xx_pd_struct *pd)
{
    xx_io_device *device;
    int64_t saved, total;
    char *text = NULL;
    ccd_view *v = NULL;
    ccd_entry entries[CCD_MAX_ENTRIES];
    enum ccd_section_e section = CCD_NONE;
    uint32_t id = 0U;
    uint32_t version = 0U, tocs = 0U, sessions = 0U, scrambled = 0U;
    uint32_t cdtext = 0U, pregap_mode = 0U, pregap_sub = 0U;
    bool has_version = false, has_tocs = false, has_sessions = false;
    bool has_scrambled = false, has_cdtext = false, has_pregap_mode = false;
    bool has_pregap_sub = false, ok = false;
    size_t at = 0U;
    uint32_t i;
    if (!f || !(device = f->device) || f->base_address < 0 || ccd_stop(pd)) return NULL;
    saved = xx_io_tell(device);
    total = xx_io_total_size(device);
    if (saved < 0 || total < f->base_address || total - f->base_address < 80 || total - f->base_address > CCD_MAX_DESCRIPTOR) return NULL;
    text = (char *)xx_mem_alloc((size_t)(total - f->base_address) + 1U);
    v = (ccd_view *)xx_mem_calloc(1U, sizeof(*v));
    if (v) v->refs = 1U;
    xx_mem_zero(entries, sizeof(entries));
    if (!text || !v || !ccd_read_at(device, f->base_address, (uint8_t *)text, (size_t)(total - f->base_address), pd)) goto done;
    v->base = f->base_address;
    v->descriptor_size = total - f->base_address;
    text[v->descriptor_size] = 0;
    if (memcmp(text, "[CloneCD]", 9U)) goto done;
    while (at < (size_t)v->descriptor_size) {
        size_t start = at, end = at;
        char *line, *eq;
        while (end < (size_t)v->descriptor_size && text[end] != '\n') {
            if (!text[end] || ((unsigned char)text[end] < 0x20U && text[end] != '\r' && text[end] != '\t')) goto done;
            ++end;
        }
        at = end < (size_t)v->descriptor_size ? end + 1U : end;
        if (end > start && text[end - 1U] == '\r') --end;
        text[end] = 0;
        line = text + start;
        ccd_trim(&line);
        if (!*line) continue;
        if (*line == '[') {
            if (!ccd_set_section(line, &section, &id, entries, v->tracks, v->base + (int64_t)start)) goto done;
            continue;
        }
        eq = strchr(line, '=');
        if (!eq || section == CCD_NONE) goto done;
        *eq++ = 0;
        ccd_trim(&line);
        ccd_trim(&eq);
        if (section == CCD_CLONE && ccd_equal(line, "Version")) {
            if (!ccd_set_u32(&has_version, &version, eq)) goto done;
        } else if (section == CCD_DISC && ccd_equal(line, "TocEntries")) {
            if (!ccd_set_u32(&has_tocs, &tocs, eq)) goto done;
        } else if (section == CCD_DISC && ccd_equal(line, "Sessions")) {
            if (!ccd_set_u32(&has_sessions, &sessions, eq)) goto done;
        } else if (section == CCD_DISC && ccd_equal(line, "DataTracksScrambled")) {
            if (!ccd_set_u32(&has_scrambled, &scrambled, eq)) goto done;
        } else if (section == CCD_DISC && ccd_equal(line, "CDTextLength")) {
            if (!ccd_set_u32(&has_cdtext, &cdtext, eq)) goto done;
        } else if (section == CCD_SESSION && ccd_equal(line, "PreGapMode")) {
            if (!ccd_set_u32(&has_pregap_mode, &pregap_mode, eq)) goto done;
        } else if (section == CCD_SESSION && ccd_equal(line, "PreGapSubC")) {
            if (!ccd_set_u32(&has_pregap_sub, &pregap_sub, eq)) goto done;
        } else if (section == CCD_ENTRY && ccd_equal(line, "Point")) {
            if (!ccd_set_u32(&entries[id].point_set, &entries[id].point, eq)) goto done;
        } else if (section == CCD_ENTRY && ccd_equal(line, "Session")) {
            if (!ccd_set_u32(&entries[id].session_set, &entries[id].session, eq)) goto done;
        } else if (section == CCD_ENTRY && ccd_equal(line, "ADR")) {
            if (!ccd_set_u32(&entries[id].adr_set, &entries[id].adr, eq)) goto done;
        } else if (section == CCD_ENTRY && ccd_equal(line, "Control")) {
            if (!ccd_set_u32(&entries[id].control_set, &entries[id].control, eq)) goto done;
        } else if (section == CCD_ENTRY && ccd_equal(line, "PLBA")) {
            if (!ccd_set_u32(&entries[id].plba_set, &entries[id].plba, eq)) goto done;
        } else if (section == CCD_ENTRY && ccd_equal(line, "PMin")) {
            if (!ccd_set_u32(&entries[id].pmin_set, &entries[id].pmin, eq)) goto done;
        } else if (section == CCD_TRACK && ccd_equal(line, "MODE")) {
            if (!ccd_set_u32(&v->tracks[id].mode_set, &v->tracks[id].mode, eq)) goto done;
        } else if (section == CCD_TRACK && ccd_equal(line, "INDEX 1")) {
            if (!ccd_set_u32(&v->tracks[id].index_set, &v->tracks[id].index1, eq)) goto done;
        } else if (section == CCD_TRACK && ccd_equal(line, "INDEX 0")) {
            v->tracks[id].index0_set = true;
        }
    }
    if (!has_version || version != 3U || !has_tocs || tocs < 4U || tocs > CCD_MAX_ENTRIES || !has_sessions || sessions != 1U || !has_scrambled || scrambled ||
        !has_cdtext || cdtext || !has_pregap_mode || pregap_mode != 1U || !has_pregap_sub || pregap_sub)
        goto done;
    v->count = tocs - 3U;
    if (v->count > CCD_MAX_TRACKS || !entries[0].seen || !entries[1].seen || !entries[2].seen || !entries[0].point_set || entries[0].point != 0xA0U ||
        !entries[1].point_set || entries[1].point != 0xA1U || !entries[2].point_set || entries[2].point != 0xA2U || !entries[0].pmin_set || entries[0].pmin != 1U ||
        !entries[1].pmin_set || entries[1].pmin != v->count || !entries[2].plba_set || !entries[2].plba)
        goto done;
    v->leadout = entries[2].plba;
    for (i = 0U; i < tocs; ++i)
        if (!entries[i].seen || !entries[i].session_set || entries[i].session != 1U || !entries[i].adr_set || entries[i].adr != 1U) goto done;
    for (i = 0U; i < v->count; ++i) {
        ccd_entry *e = &entries[i + 3U];
        ccd_track *t = &v->tracks[i];
        if (!e->seen || !e->point_set || e->point != i + 1U || !e->control_set || !e->plba_set || !t->section || !t->mode_set || !t->index_set || t->index0_set ||
            (t->mode != 0U && t->mode != 1U && t->mode != 2U) || e->control != (t->mode ? 4U : 0U) || e->plba != t->index1 ||
            (i == 0U ? t->index1 != 0U : t->index1 <= v->tracks[i - 1U].index1) || t->index1 >= v->leadout)
            goto done;
    }
    for (i = tocs; i < CCD_MAX_ENTRIES; ++i)
        if (entries[i].seen) goto done;
    for (i = v->count; i < CCD_MAX_TRACKS; ++i)
        if (v->tracks[i].section) goto done;
    ok = !ccd_stop(pd);
done:
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    xx_mem_free(text);
    if (!ok) {
        ccd_release(v);
        return NULL;
    }
    return v;
}
static void ccd_close_img(xx_ccd *c)
{
    if (c->img_owned && c->img) (void)xx_io_close(c->img);
    c->img = NULL;
    c->img_owned = false;
}
static void ccd_close_sub(xx_ccd *c)
{
    if (c->sub_owned && c->sub) (void)xx_io_close(c->sub);
    c->sub = NULL;
    c->sub_owned = false;
}
static void ccd_destroy_format(Abstractformat *f)
{
    xx_ccd_destroy((xx_ccd *)f);
}
void xx_ccd_init(xx_ccd *c, xx_io_device *device, int64_t base)
{
    if (!c) return;
    xx_mem_zero(c, sizeof(*c));
    xx_format_init(&c->format, device, base);
    c->format.file_type = CCD_TYPE;
    c->format.format_type = XX_TYPE_ARCHIVE;
    c->format.is_archive = true;
    xx_format_set_mime_type(&c->format, "application/x-clonecd");
    xx_format_set_extension(&c->format, "ccd");
    c->format.check_is_valid = xx_ccd_check_is_valid;
    c->format.handle_base_info = xx_ccd_handle_base_info;
    c->format.get_format_size = xx_ccd_get_format_size;
    c->format.get_number_of_archive_records = xx_ccd_get_number_of_archive_records;
    c->format.create_archive_records_reading = xx_ccd_create_archive_records_reading;
    c->format.get_current_archive_record = xx_ccd_get_current_archive_record;
    c->format.archive_record_move_to_next = xx_ccd_archive_record_move_to_next;
    c->format.unpack_current_archive_record = xx_ccd_unpack_current_archive_record;
    c->format.free_archive_records_reading = xx_ccd_free_archive_records_reading;
    c->format.destroy = ccd_destroy_format;
}
xx_ccd *xx_ccd_create(xx_io_device *device, int64_t base)
{
    xx_ccd *c = (xx_ccd *)xx_mem_alloc(sizeof(*c));
    if (c) {
        xx_ccd_init(c, device, base);
    }
    return c;
}
void xx_ccd_destroy(xx_ccd *c)
{
    if (!c) return;
    ccd_close_img(c);
    ccd_close_sub(c);
    ccd_release((ccd_view *)c->internal);
    c->internal = NULL;
    xx_format_cleanup_extra_parameters(&c->format);
}
void xx_ccd_free(xx_ccd *c)
{
    if (c) {
        xx_ccd_destroy(c);
        xx_mem_free(c);
    }
}
bool xx_ccd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    ccd_view *v = ccd_parse(f, pd);
    if (!v) {
        return false;
    }
    ccd_release(v);
    return true;
}
bool xx_ccd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_ccd *c = (xx_ccd *)f;
    ccd_view *v;
    if (!f || ccd_stop(pd)) return false;
    if (f->base_info_handled && c->internal) return f->is_valid;
    v = ccd_parse(f, pd);
    if (!v) {
        f->base_info_handled = false;
        f->is_valid = false;
        return false;
    }
    ccd_release((ccd_view *)c->internal);
    c->internal = v;
    c->number_of_tracks = v->count;
    f->number_of_archive_records = v->count * (c->sub ? 2U : 1U);
    f->format_size = v->descriptor_size;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
int64_t xx_ccd_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_ccd_handle_base_info(f, pd) ? f->format_size : -1;
}
uint64_t xx_ccd_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    if (!xx_ccd_handle_base_info(f, pd)) return 0U;
    f->number_of_archive_records = ((xx_ccd *)f)->number_of_tracks * (((xx_ccd *)f)->sub ? 2U : 1U);
    return f->number_of_archive_records;
}
bool xx_ccd_set_img_device(xx_ccd *c, xx_io_device *device)
{
    if (!c || !xx_ccd_handle_base_info(&c->format, NULL) || (c->img_owned && c->img == device)) return false;
    ccd_close_img(c);
    c->img = device;
    return true;
}
bool xx_ccd_set_sub_device(xx_ccd *c, xx_io_device *device)
{
    if (!c || !xx_ccd_handle_base_info(&c->format, NULL) || (c->sub_owned && c->sub == device)) return false;
    ccd_close_sub(c);
    c->sub = device;
    c->format.number_of_archive_records = c->number_of_tracks * (device ? 2U : 1U);
    return true;
}
uint32_t xx_ccd_open_data_files(xx_ccd *c, const char *ccd_path)
{
    char *path;
    size_t n, i;
    uint32_t opened = 0U;
    if (!c || !ccd_path || !xx_ccd_handle_base_info(&c->format, NULL)) return 0U;
    n = strlen(ccd_path);
    if (n < 4U || ccd_path[n - 4U] != '.') return 0U;
    for (i = n - 4U; i < n; ++i)
        if (ccd_path[i] == '/' || ccd_path[i] == '\\') return 0U;
    if (!((ccd_path[n - 3U] == 'c' || ccd_path[n - 3U] == 'C') && (ccd_path[n - 2U] == 'c' || ccd_path[n - 2U] == 'C') &&
          (ccd_path[n - 1U] == 'd' || ccd_path[n - 1U] == 'D')))
        return 0U;
    path = xx_str_dup(ccd_path);
    if (!path) return 0U;
    if (!c->img) {
        path[n - 3U] = 'i';
        path[n - 2U] = 'm';
        path[n - 1U] = 'g';
        c->img = xx_io_file_open(path, "rb");
        if (c->img) c->img_owned = true;
    }
    if (!c->sub) {
        path[n - 3U] = 's';
        path[n - 2U] = 'u';
        path[n - 1U] = 'b';
        c->sub = xx_io_file_open(path, "rb");
        if (c->sub) c->sub_owned = true;
    }
    opened = (c->img ? 1U : 0U) + (c->sub ? 1U : 0U);
    c->format.number_of_archive_records = c->number_of_tracks * (c->sub ? 2U : 1U);
    xx_str_free(path);
    return opened;
}
uint32_t xx_ccd_get_number_of_tracks(xx_ccd *c)
{
    return c && xx_ccd_handle_base_info(&c->format, NULL) ? c->number_of_tracks : 0U;
}
static void ccd_location(const ccd_cursor *cursor, uint32_t *track, bool *sub, uint64_t *offset, uint64_t *length)
{
    uint32_t start, end, sector;
    *track = cursor->index / (cursor->has_sub ? 2U : 1U);
    *sub = cursor->has_sub && (cursor->index & 1U) != 0U;
    start = cursor->view->tracks[*track].index1;
    end = *track + 1U < cursor->view->count ? cursor->view->tracks[*track + 1U].index1 : cursor->view->leadout;
    sector = *sub ? CCD_SUB_SECTOR : CCD_IMG_SECTOR;
    *offset = (uint64_t)start * sector;
    *length = (uint64_t)(end - start) * sector;
}
static void ccd_name(char name[32], uint32_t track, bool sub, const ccd_view *v)
{
    const char *extension = sub ? "sub" : v->tracks[track].mode == 0U ? "cdda" : "bin";
    (void)xx_rt_snprintf(name, 32U, "ccd-track%02u.%s", track + 1U, extension);
}
static bool ccd_record(xx_archive_record *r, const ccd_cursor *cursor)
{
    uint32_t track;
    bool sub;
    uint64_t offset, length;
    char name[32];
    ccd_location(cursor, &track, &sub, &offset, &length);
    ccd_name(name, track, sub, cursor->view);
    xx_archive_record_cleanup(r);
    xx_archive_record_init(r);
    r->header_offset = cursor->view->tracks[track].header_offset;
    r->header_size = 0;
    r->data_offset = -1;
    r->compressed_size = (int64_t)length;
    return xx_archive_record_set_original_name(r, name) && xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, length) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, length) && xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false);
}
static void ccd_cursor_free(void *ptr)
{
    ccd_cursor *c = (ccd_cursor *)ptr;
    if (c) {
        ccd_release(c->view);
        xx_mem_free(c);
    }
}
xx_archive_record_state *xx_ccd_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_ccd *c = (xx_ccd *)f;
    ccd_view *v;
    ccd_cursor *cursor;
    xx_archive_record_state *state;
    size_t i;
    if (!xx_ccd_handle_base_info(f, pd)) return NULL;
    v = (ccd_view *)c->internal;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    cursor = (ccd_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        xx_mem_free(state);
        xx_mem_free(cursor);
        return NULL;
    }
    ++v->refs;
    cursor->view = v;
    cursor->has_sub = c->sub != NULL;
    xx_archive_record_state_init(state, f);
    state->internal_state = cursor;
    state->free_internal = ccd_cursor_free;
    state->total_records = v->count * (cursor->has_sub ? 2U : 1U);
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
    if (!ccd_record(&state->current_record, cursor)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_ccd_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state)
{
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_ccd_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ccd_cursor *cursor;
    if (!f || !state || state->format != f || !state->has_record || ccd_stop(pd) || !(cursor = (ccd_cursor *)state->internal_state)) return false;
    ++cursor->index;
    if (cursor->index >= cursor->view->count * (cursor->has_sub ? 2U : 1U)) {
        state->has_record = false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    ++state->current_index;
    state->has_record = ccd_record(&state->current_record, cursor);
    return state->has_record;
}
static bool ccd_limits(Abstractformat *f, xx_archive_record_state *state, const ccd_view *v, uint64_t length)
{
    const xx_var *max = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t needed = sizeof(*v) + sizeof(ccd_cursor) + CCD_COPY;
    return (!max || length <= xx_var_get_u64(max)) && (!mem || needed <= xx_var_get_u64(mem));
}
bool xx_ccd_extract_record_to_device(Abstractformat *f, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    xx_ccd *c = (xx_ccd *)f;
    ccd_cursor *cursor;
    xx_io_device *source;
    uint32_t track;
    bool sub;
    uint64_t offset, length, done = 0U;
    int64_t saved;
    uint8_t buffer[CCD_COPY];
    bool ok = true;
    if (!c || !state || state->format != f || !state->has_record || ccd_stop(pd) || !(cursor = (ccd_cursor *)state->internal_state)) return false;
    ccd_location(cursor, &track, &sub, &offset, &length);
    source = sub ? c->sub : c->img;
    if (!source || source == destination || source == f->device || destination == f->device ||
        xx_io_total_size(source) != (int64_t)((uint64_t)cursor->view->leadout * (sub ? CCD_SUB_SECTOR : CCD_IMG_SECTOR)) || !ccd_limits(f, state, cursor->view, length) ||
        (saved = xx_io_tell(source)) < 0)
        return false;
    while (done < length && !ccd_stop(pd)) {
        size_t take = length - done > CCD_COPY ? CCD_COPY : (size_t)(length - done);
        size_t got = 0U, wrote = 0U;
        if (offset > (uint64_t)INT64_MAX - done || xx_io_seek64(source, (int64_t)(offset + done), SEEK_SET) != 0) {
            ok = false;
            break;
        }
        while (got < take && !ccd_stop(pd)) {
            ssize_t step = xx_io_read(source, buffer + got, take - got);
            if (step <= 0 || (size_t)step > take - got || ccd_stop(pd)) {
                ok = false;
                break;
            }
            got += (size_t)step;
        }
        if (!ok || got != take) {
            ok = false;
            break;
        }
        while (destination && wrote < take && !ccd_stop(pd)) {
            ssize_t step = xx_io_write(destination, buffer + wrote, take - wrote);
            if (step <= 0 || (size_t)step > take - wrote || ccd_stop(pd)) {
                ok = false;
                break;
            }
            wrote += (size_t)step;
        }
        if (!ok) break;
        done += take;
    }
    if (xx_io_seek64(source, saved, SEEK_SET) != 0) ok = false;
    return ok && done == length && !ccd_stop(pd);
}
static xx_io_device *ccd_stage(const char *destination, char **stage)
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
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_ccd.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (ccd_equal(candidate, destination)) {
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
bool xx_ccd_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ccd_cursor *cursor;
    const xx_var *option, *ov;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL, name[32];
    uint32_t track;
    bool sub;
    uint64_t offset, length;
    bool ok = false, overwrite;
    if (!f || !state || state->format != f || !state->has_record || ccd_stop(pd) || !(cursor = (ccd_cursor *)state->internal_state)) return false;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    ov = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = ov && xx_var_get_bool(ov);
    if (!option) return xx_ccd_extract_record_to_device(f, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    ccd_location(cursor, &track, &sub, &offset, &length);
    ccd_name(name, track, sub, cursor->view);
    path = *base && base[strlen(base) - 1U] != '/' && base[strlen(base) - 1U] != '\\' ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) || ccd_stop(pd)) goto done;
    {
        xx_io_device *output = ccd_stage(path, &stage);
        if (!output) goto done;
        ok = xx_ccd_extract_record_to_device(f, state, output, pd);
        if (xx_io_close(output) != 0) ok = false;
    }
    if (ok && !ccd_stop(pd)) ok = xx_io_file_replace_a(stage, path, overwrite);
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
void xx_ccd_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}
bool xx_ccd_test_magic(const uint8_t *magic, size_t size)
{
    return magic && size >= 9U && !memcmp(magic, "[CloneCD]", 9U);
}
