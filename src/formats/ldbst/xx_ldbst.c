/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent native subset of John Elliott's LDBS text specification.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ldbst/xx_ldbst.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LDBST_TEXT_MAX (8U * 1024U * 1024U)
#define LDBST_IMAGE_MAX (16U * 1024U * 1024U)
#define LDBST_TRACK_MAX 256U
#define LDBST_SECTOR_MAX 64U
#define LDBST_SECTOR_BYTES_MAX 8192U
#define LDBST_TRAIL_MAX 4096U
#define LDBST_WORK_EXTRA 16384U
#define LDBST_REQ 0x1ffU

typedef struct ldbst_sector_s {
    uint8_t idc, idh, id, psh, st1, st2, copies, fill;
    uint16_t bytes, trail;
    uint16_t flags;
    uint8_t *data;
} ldbst_sector;
typedef struct ldbst_track_s {
    uint16_t cyl;
    uint8_t head, count, flags;
    ldbst_sector sector[LDBST_SECTOR_MAX];
} ldbst_track;
typedef struct ldbst_view_s {
    char *text;
    ldbst_track *track;
    uint32_t tracks, source_size, raw_size;
    uint64_t work;
    uint16_t cylinders, sector_bytes;
    uint8_t heads, sectors, sector_base;
} ldbst_view;

static bool ldbst_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool ldbst_read(xx_io_device *d, int64_t at, void *p, size_t n, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (ldbst_stop(pd) || xx_io_seek64(d, at, SEEK_SET) != 0) return false;
    while (done < n) {
        ssize_t got;
        if (ldbst_stop(pd)) return false;
        got = xx_io_read(d, (uint8_t *)p + done, n - done);
        if (got <= 0 || (size_t)got > n - done || ldbst_stop(pd)) return false;
        done += (size_t)got;
    }
    return !ldbst_stop(pd);
}
static bool ldbst_write(xx_io_device *d, const void *p, size_t n, xx_pd_struct *pd)
{
    size_t done = 0U;
    while (done < n) {
        ssize_t put;
        if (ldbst_stop(pd)) return false;
        put = xx_io_write(d, (const uint8_t *)p + done, n - done);
        if (put <= 0 || (size_t)put > n - done || ldbst_stop(pd)) return false;
        done += (size_t)put;
    }
    return !ldbst_stop(pd);
}
static void ldbst_release(ldbst_view *v)
{
    uint32_t i, j;
    if (!v) return;
    if (v->track) {
        for (i = 0U; i < v->tracks; ++i)
            for (j = 0U; j < v->track[i].count; ++j)
                if (v->track[i].sector[j].data) xx_mem_free(v->track[i].sector[j].data);
        xx_mem_free(v->track);
    }
    if (v->text) xx_mem_free(v->text);
    xx_mem_free(v);
}
static int ldbst_fold(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}
static bool ldbst_eq(const char *a, size_t n, const char *b)
{
    size_t i;
    if (strlen(b) != n) return false;
    for (i = 0U; i < n; ++i)
        if (ldbst_fold((unsigned char)a[i]) != ldbst_fold((unsigned char)b[i])) return false;
    return true;
}
static void ldbst_trim(const char **p, size_t *n)
{
    while (*n && (**p == ' ' || **p == '\t' || **p == '\r')) {
        ++*p;
        --*n;
    }
    while (*n && ((*p)[*n - 1U] == ' ' || (*p)[*n - 1U] == '\t' || (*p)[*n - 1U] == '\r')) --*n;
}
static bool ldbst_number(const char *p, size_t n, uint32_t *out)
{
    uint64_t value = 0U;
    unsigned base = 10U;
    size_t i = 0U;
    ldbst_trim(&p, &n);
    if (n >= 2U && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16U;
        i = 2U;
    }
    if (i >= n) return false;
    for (; i < n; ++i) {
        unsigned digit;
        int c = (unsigned char)p[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == ';' || c == '#') break;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else return false;
        if (digit >= base) return false;
        value = value * base + digit;
        if (value > UINT32_MAX) return false;
    }
    *out = (uint32_t)value;
    return true;
}
static int ldbst_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool ldbst_hex_data(ldbst_view *v, size_t open, size_t *cursor, ldbst_sector *s, uint64_t cap)
{
    size_t i = open + 1U, n = 0U, need = (size_t)s->bytes + s->trail;
    int high = -1;
    bool comment = false, closed = false;
    if (v->text[open] != '{' || !s->copies || !s->bytes || need > LDBST_SECTOR_BYTES_MAX + LDBST_TRAIL_MAX || v->work + need > cap || v->work + need > UINT32_MAX ||
        s->data)
        return false;
    s->data = (uint8_t *)xx_mem_alloc(need);
    if (!s->data) return false;
    v->work += need;
    for (; i < v->source_size; ++i) {
        int c = (unsigned char)v->text[i], h;
        if (c == '\n' || c == '\r') {
            comment = false;
            continue;
        }
        if (comment) continue;
        if (c == ';' || c == '#') {
            comment = true;
            continue;
        }
        if (c == '}') {
            closed = true;
            ++i;
            break;
        }
        h = ldbst_hex(c);
        if (h >= 0) {
            if (high < 0) high = h;
            else {
                if (n >= need) return false;
                s->data[n++] = (uint8_t)((high << 4) | h);
                high = -1;
            }
        } else if (c != ' ' && c != '\t') return false;
    }
    if (!closed || high >= 0 || n != need) return false;
    while (i < v->source_size && v->text[i] != '\n') {
        if (v->text[i] != ' ' && v->text[i] != '\t' && v->text[i] != '\r' && v->text[i] != ';' && v->text[i] != '#') return false;
        if (v->text[i] == ';' || v->text[i] == '#') {
            while (i < v->source_size && v->text[i] != '\n') ++i;
            break;
        }
        ++i;
    }
    *cursor = i < v->source_size ? i + 1U : i;
    s->flags |= 0x200U;
    return true;
}
static int ldbst_track_cmp(const void *a, const void *b)
{
    const ldbst_track *x = (const ldbst_track *)a, *y = (const ldbst_track *)b;
    uint32_t kx = (uint32_t)x->cyl * 2U + x->head;
    uint32_t ky = (uint32_t)y->cyl * 2U + y->head;
    return kx < ky ? -1 : kx > ky ? 1 : 0;
}
static int ldbst_sector_cmp(const void *a, const void *b)
{
    const ldbst_sector *x = (const ldbst_sector *)a;
    const ldbst_sector *y = (const ldbst_sector *)b;
    return x->id < y->id ? -1 : x->id > y->id ? 1 : 0;
}
static uint64_t ldbst_cap(Abstractformat *f, const xx_list_s *opts, uint32_t key)
{
    const xx_var *var = xx_format_resolve_extra_parameter(f, opts, key);
    return var ? xx_var_get_u64(var) : UINT64_MAX;
}
static ldbst_view *ldbst_parse_body(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    enum {
        SEC_NONE,
        SEC_HEADER,
        SEC_TRACK,
        SEC_SECTOR,
        SEC_GEOMETRY,
        SEC_IGNORE
    };
    int section = SEC_NONE;
    ldbst_view *v = NULL;
    ldbst_track *track = NULL;
    ldbst_sector *sector = NULL;
    int64_t total;
    size_t pos = 0U;
    uint64_t cap = ldbst_cap(f, opts, XX_META_ID_OPT_MEMORY_LIMIT), work;
    uint32_t geometry[6] = {0U}, geom_flags = 0U, geom_bad = 0U;
    uint32_t i, j, maxc = 0U, maxh = 0U, spt = 0U, secbytes = 0U, sbase = 0U;
    bool first = true, ok = false;
    if (!f || !f->device || f->base_address < 0 || ldbst_stop(pd)) return NULL;
    total = xx_io_total_size(f->device);
    if (total < f->base_address || total - f->base_address < 7 || total - f->base_address > LDBST_TEXT_MAX) return NULL;
    work = (uint64_t)sizeof(*v) + (uint64_t)LDBST_TRACK_MAX * sizeof(ldbst_track) + (uint64_t)(total - f->base_address) + 1U + LDBST_WORK_EXTRA;
    if (work > cap || work > UINT32_MAX) return NULL;
    v = (ldbst_view *)xx_mem_calloc(1U, sizeof(*v));
    if (!v) return NULL;
    v->source_size = (uint32_t)(total - f->base_address);
    v->work = work;
    v->text = (char *)xx_mem_alloc((size_t)v->source_size + 1U);
    v->track = (ldbst_track *)xx_mem_calloc(LDBST_TRACK_MAX, sizeof(ldbst_track));
    if (!v->text || !v->track || !ldbst_read(f->device, f->base_address, v->text, v->source_size, pd)) goto done;
    v->text[v->source_size] = 0;
    if (v->source_size >= 3U && (uint8_t)v->text[0U] == 0xefU && (uint8_t)v->text[1U] == 0xbbU && (uint8_t)v->text[2U] == 0xbfU) pos = 3U;
    while (pos < v->source_size) {
        size_t start = pos, end, key_end, val_start;
        const char *line, *key, *val;
        size_t len, key_n, val_n;
        uint32_t number;
        while (pos < v->source_size && v->text[pos] != '\n') ++pos;
        end = pos;
        if (pos < v->source_size) ++pos;
        line = v->text + start;
        len = end - start;
        ldbst_trim(&line, &len);
        if (!len || *line == ';' || *line == '#') continue;
        if (len >= 3U && line[0U] == '[' && line[len - 1U] == ']') {
            if (first) {
                if (!ldbst_eq(line, len, "[LDBS]")) goto done;
                first = false;
                section = SEC_HEADER;
                continue;
            }
            if (ldbst_eq(line, len, "[Track]")) {
                if (v->tracks >= LDBST_TRACK_MAX) goto done;
                track = &v->track[v->tracks++];
                track->cyl = UINT16_MAX;
                track->head = UINT8_MAX;
                sector = NULL;
                section = SEC_TRACK;
            } else if (ldbst_eq(line, len, "[Sector]")) {
                if (!track || (section != SEC_TRACK && section != SEC_SECTOR) || track->count >= LDBST_SECTOR_MAX) goto done;
                sector = &track->sector[track->count++];
                section = SEC_SECTOR;
            } else if (ldbst_eq(line, len, "[Geometry]")) {
                track = NULL;
                sector = NULL;
                section = SEC_GEOMETRY;
            } else if (ldbst_eq(line, len, "[Block]") || ldbst_eq(line, len, "[Creator]") || ldbst_eq(line, len, "[Comment]") || ldbst_eq(line, len, "[DPB]")) {
                track = NULL;
                sector = NULL;
                section = SEC_IGNORE;
            } else goto done;
            continue;
        }
        if (first) goto done;
        for (key_end = 0U; key_end < len && line[key_end] != '='; ++key_end) {
        }
        if (key_end == len) goto done;
        key = line;
        key_n = key_end;
        ldbst_trim(&key, &key_n);
        val_start = key_end + 1U;
        val = line + val_start;
        val_n = len - val_start;
        ldbst_trim(&val, &val_n);
        if (!key_n || !val_n) goto done;
        if (section == SEC_IGNORE || section == SEC_HEADER) continue;
        if (section == SEC_TRACK) {
            if (!track) goto done;
            if (ldbst_eq(key, key_n, "Cylinder")) {
                if (!ldbst_number(val, val_n, &number) || number > 255U || (track->flags & 1U)) goto done;
                track->cyl = (uint16_t)number;
                track->flags |= 1U;
            } else if (ldbst_eq(key, key_n, "Head")) {
                if (!ldbst_number(val, val_n, &number) || number > 1U || (track->flags & 2U)) goto done;
                track->head = (uint8_t)number;
                track->flags |= 2U;
            } else if (ldbst_eq(key, key_n, "RecMode")) {
                if (!ldbst_eq(val, val_n, "FM") && !ldbst_eq(val, val_n, "MFM") && !ldbst_eq(val, val_n, "Unknown")) goto done;
            } else if (!ldbst_eq(key, key_n, "DataRate") && !ldbst_eq(key, key_n, "GAP3") && !ldbst_eq(key, key_n, "Filler") && !ldbst_eq(key, key_n, "TotalLength"))
                goto done;
            continue;
        }
        if (section == SEC_GEOMETRY) {
            if (ldbst_eq(key, key_n, "Sides") || ldbst_eq(key, key_n, "Sidedness")) {
                if (!ldbst_eq(val, val_n, "Alt")) geom_bad = 1U;
            } else if (ldbst_eq(key, key_n, "Complement")) {
                if (!ldbst_eq(val, val_n, "N")) geom_bad = 1U;
            } else {
                int which = -1;
                if (ldbst_eq(key, key_n, "Cylinders")) which = 0;
                else if (ldbst_eq(key, key_n, "Heads")) which = 1;
                else if (ldbst_eq(key, key_n, "Sectors")) which = 2;
                else if (ldbst_eq(key, key_n, "SecBase")) which = 3;
                else if (ldbst_eq(key, key_n, "SecSize")) which = 4;
                if (which >= 0) {
                    if ((geom_flags & (1U << (unsigned)which)) || !ldbst_number(val, val_n, &number)) goto done;
                    geometry[which] = number;
                    geom_flags |= 1U << (unsigned)which;
                }
            }
            continue;
        }
        if (section != SEC_SECTOR || !sector) goto done;
        {
            unsigned field = 0U;
            if (ldbst_eq(key, key_n, "ID.Cylinder")) field = 0U;
            else if (ldbst_eq(key, key_n, "ID.Head")) field = 1U;
            else if (ldbst_eq(key, key_n, "ID.Sector")) field = 2U;
            else if (ldbst_eq(key, key_n, "ID.PSH")) field = 3U;
            else if (ldbst_eq(key, key_n, "Status1")) field = 4U;
            else if (ldbst_eq(key, key_n, "Status2")) field = 5U;
            else if (ldbst_eq(key, key_n, "Copies")) field = 6U;
            else if (ldbst_eq(key, key_n, "Filler")) field = 7U;
            else if (ldbst_eq(key, key_n, "DataLen")) field = 8U;
            else if (ldbst_eq(key, key_n, "TrailBytes")) field = 10U;
            else if (ldbst_eq(key, key_n, "Offset")) field = 11U;
            else if (ldbst_eq(key, key_n, "Data")) field = 9U;
            else goto done;
            if (field == 9U) {
                if (val[0] != '{' || !(sector->flags & (1U << 8U)) || !(sector->flags & (1U << 6U)) || !ldbst_hex_data(v, (size_t)(val - v->text), &pos, sector, cap))
                    goto done;
                continue;
            }
            if (!ldbst_number(val, val_n, &number)) goto done;
            if (field <= 8U) {
                if (sector->flags & (1U << field)) goto done;
                sector->flags |= (uint16_t)(1U << field);
            }
            if ((field <= 7U && number > 255U) || (field == 3U && number > 7U) || (field == 8U && number > LDBST_SECTOR_BYTES_MAX) ||
                (field == 10U && number > LDBST_TRAIL_MAX) || (field == 11U && number > UINT16_MAX))
                goto done;
            switch (field) {
                case 0U: sector->idc = (uint8_t)number; break;
                case 1U: sector->idh = (uint8_t)number; break;
                case 2U: sector->id = (uint8_t)number; break;
                case 3U: sector->psh = (uint8_t)number; break;
                case 4U: sector->st1 = (uint8_t)number; break;
                case 5U: sector->st2 = (uint8_t)number; break;
                case 6U: sector->copies = (uint8_t)number; break;
                case 7U: sector->fill = (uint8_t)number; break;
                case 8U: sector->bytes = (uint16_t)number; break;
                case 10U: sector->trail = (uint16_t)number; break;
                default: break;
            }
        }
    }
    if (first || !v->tracks || geom_bad) goto done;
    for (i = 0U; i < v->tracks; ++i) {
        ldbst_track *tr = &v->track[i];
        if (tr->flags != 3U || tr->count == 0U) goto done;
        if (tr->cyl > maxc) maxc = tr->cyl;
        if (tr->head > maxh) maxh = tr->head;
        if (!spt) spt = tr->count;
        if (spt != tr->count) goto done;
        qsort(tr->sector, tr->count, sizeof(ldbst_sector), ldbst_sector_cmp);
        if (i == 0U) sbase = tr->sector[0U].id;
        for (j = 0U; j < tr->count; ++j) {
            ldbst_sector *s = &tr->sector[j];
            if ((s->flags & LDBST_REQ) != LDBST_REQ || s->idc != tr->cyl || s->idh != tr->head || s->id != (uint8_t)(sbase + j) || s->st1 || s->st2 || s->copies > 1U ||
                !s->bytes || s->bytes > LDBST_SECTOR_BYTES_MAX || (s->copies && (!s->data || !(s->flags & 0x200U))) || (!s->copies && (s->data || s->trail)))
                goto done;
            if (!secbytes) secbytes = s->bytes;
            if (s->bytes != secbytes) goto done;
        }
    }
    if (sbase + spt - 1U > 255U || (maxc + 1U) * (maxh + 1U) != v->tracks || (uint64_t)v->tracks * spt * secbytes > LDBST_IMAGE_MAX) goto done;
    qsort(v->track, v->tracks, sizeof(ldbst_track), ldbst_track_cmp);
    for (i = 0U; i < v->tracks; ++i)
        if (v->track[i].cyl != i / (maxh + 1U) || v->track[i].head != i % (maxh + 1U)) goto done;
    if (geom_flags && (geom_flags != 31U || geometry[0U] != maxc + 1U || geometry[1U] != maxh + 1U || geometry[2U] != spt ||
                       (geometry[3U] != sbase && geometry[3U] != sbase + spt - 1U) || geometry[4U] != secbytes))
        goto done;
    v->cylinders = (uint16_t)(maxc + 1U);
    v->heads = (uint8_t)(maxh + 1U);
    v->sectors = (uint8_t)spt;
    v->sector_base = (uint8_t)sbase;
    v->sector_bytes = (uint16_t)secbytes;
    v->raw_size = v->tracks * spt * secbytes;
    if (v->raw_size > ldbst_cap(f, opts, XX_META_ID_OPT_MAX_MEMBER_SIZE)) goto done;
    ok = true;
done:
    if (!ok) {
        ldbst_release(v);
        v = NULL;
    }
    return v;
}
static ldbst_view *ldbst_parse(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    int64_t saved;
    ldbst_view *v;
    if (!f || !f->device) return NULL;
    saved = xx_io_tell(f->device);
    if (saved < 0) return NULL;
    v = ldbst_parse_body(f, opts, pd);
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0) {
        ldbst_release(v);
        v = NULL;
    }
    return v;
}
static bool ldbst_emit(const ldbst_view *v, xx_io_device *d, xx_pd_struct *pd)
{
    uint8_t filler[LDBST_SECTOR_BYTES_MAX];
    uint32_t i, j;
    for (i = 0U; i < v->tracks; ++i)
        for (j = 0U; j < v->sectors; ++j) {
            const ldbst_sector *s = &v->track[i].sector[j];
            if (ldbst_stop(pd)) return false;
            if (!s->copies) memset(filler, s->fill, v->sector_bytes);
            if (!ldbst_write(d, s->copies ? s->data : filler, v->sector_bytes, pd)) return false;
        }
    return !ldbst_stop(pd);
}
static bool ldbst_unpack_device(Abstractformat *f, const xx_list_s *opts, xx_io_device *d, xx_pd_struct *pd)
{
    ldbst_view *v;
    bool ok;
    if (!f || !d || d == f->device || ldbst_stop(pd)) return false;
    v = ldbst_parse(f, opts, pd);
    if (!v) return false;
    ok = ldbst_emit(v, d, pd);
    ldbst_release(v);
    return ok && !ldbst_stop(pd);
}
static bool ldbst_check(Abstractformat *f, xx_pd_struct *pd)
{
    ldbst_view *v = ldbst_parse(f, NULL, pd);
    if (!v) return false;
    ldbst_release(v);
    return true;
}
static bool ldbst_handle(Abstractformat *f, xx_pd_struct *pd)
{
    ldbst_view *v = ldbst_parse(f, NULL, pd);
    if (!v) {
        if (f) {
            f->is_valid = false;
            f->base_info_handled = false;
        }
        return false;
    }
    f->format_size = v->source_size;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->number_of_archive_records = 1U;
    f->is_valid = true;
    f->base_info_handled = true;
    ldbst_release(v);
    return true;
}
static int64_t ldbst_size(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || ldbst_handle(f, pd)) ? f->format_size : -1;
}
static uint64_t ldbst_count(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (f->base_info_handled || ldbst_handle(f, pd)) ? f->number_of_archive_records : 0U;
}
static bool ldbst_record(xx_archive_record_state *s, uint32_t raw)
{
    xx_archive_record *r = &s->current_record;
    xx_archive_record_cleanup(r);
    xx_archive_record_init(r);
    r->header_offset = s->format->base_address;
    r->data_offset = s->format->base_address;
    r->compressed_size = s->format->format_size;
    return xx_archive_record_set_original_name(r, "disk.img") && xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, (uint64_t)s->format->format_size) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, raw) && xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, 1U) &&
           xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false);
}
static xx_archive_record_state *ldbst_create(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_archive_record_state *s;
    ldbst_view *v;
    size_t i;
    if (!f || ldbst_stop(pd) || (!f->base_info_handled && !ldbst_handle(f, pd))) return NULL;
    v = ldbst_parse(f, opts, pd);
    if (!v) return NULL;
    s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!s) {
        ldbst_release(v);
        return NULL;
    }
    xx_archive_record_state_init(s, f);
    s->total_records = 1U;
    for (i = 0U; opts && i < opts->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(opts, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var) || !xx_list_append(&s->options, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(s);
            ldbst_release(v);
            return NULL;
        }
    }
    s->has_record = ldbst_record(s, v->raw_size);
    ldbst_release(v);
    if (!s->has_record) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    return s;
}
static const xx_archive_record *ldbst_current(Abstractformat *f, xx_archive_record_state *s)
{
    return f && s && s->format == f && s->has_record ? &s->current_record : NULL;
}
static bool ldbst_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    if (!f || !s || s->format != f || !s->has_record || ldbst_stop(pd)) return false;
    s->has_record = false;
    return false;
}
static ssize_t ldbst_discard(xx_io_device *d, const void *p, size_t n)
{
    (void)d;
    (void)p;
    return (ssize_t)n;
}
static bool ldbst_same_path(const char *a, const char *b)
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
static xx_io_device *ldbst_stage(const char *dest, char **stage)
{
    char *parent = xx_str_dup(dest);
    size_t i, cut = 0U;
    unsigned attempt;
    *stage = NULL;
    if (!parent) return NULL;
    for (i = 0U; parent[i]; ++i)
        if (parent[i] == '/' || parent[i] == '\\') cut = i + 1U;
    parent[cut] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40], *candidate;
        xx_io_device *d;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_ldbst.tmp.%u", attempt);
        candidate = xx_str_concat(parent, suffix);
        if (!candidate) break;
        if (ldbst_same_path(candidate, dest)) {
            xx_str_free(candidate);
            continue;
        }
        d = xx_io_file_open(candidate, "wbx");
        if (d) {
            *stage = candidate;
            xx_str_free(parent);
            return d;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}
static bool ldbst_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const xx_var *v, *ov;
    const char *base = NULL;
    char *owned = NULL, *dest = NULL, *stage = NULL;
    xx_io_device *out = NULL, discard;
    bool ok = false, overwrite = false;
    if (!f || !s || s->format != f || !s->has_record || ldbst_stop(pd)) return false;
    v = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!v) {
        xx_rt_memset(&discard, 0, sizeof(discard));
        discard.write = ldbst_discard;
        return ldbst_unpack_device(f, &s->options, &discard, pd);
    }
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(v);
    else if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) base = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(v));
    if (!base) goto done;
    dest = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat3(base, "/", "disk.img") : xx_str_concat(base, "disk.img");
    if (!dest) goto done;
    ov = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = ov && xx_var_get_bool(ov);
    if ((!overwrite && xx_io_file_exists_a(dest)) || !xx_store_create_dirs_a(dest, false) || ldbst_stop(pd)) goto done;
    out = ldbst_stage(dest, &stage);
    if (!out) goto done;
    ok = ldbst_unpack_device(f, &s->options, out, pd);
    if (xx_io_close(out) != 0) ok = false;
    out = NULL;
    if (ok && !ldbst_stop(pd)) ok = xx_io_file_replace_a(stage, dest, overwrite);
    else ok = false;
done:
    if (out) {
        (void)xx_io_close(out);
        ok = false;
    }
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (dest) xx_str_free(dest);
    if (owned) xx_str_free(owned);
    return ok;
}
static void ldbst_free_records(Abstractformat *f, xx_archive_record_state *s)
{
    (void)f;
    xx_archive_record_state_free(s);
}
static void ldbst_destroy_vtable(Abstractformat *f)
{
    if (f) xx_format_cleanup_extra_parameters(f);
}
void xx_ldbst_init(xx_ldbst *r, xx_io_device *d, int64_t b)
{
    if (!r) return;
    xx_rt_memset(r, 0, sizeof(*r));
    xx_format_init(&r->format, d, b);
    r->format.file_type = XX_FILE_TYPE_LDBST;
    r->format.format_type = XX_TYPE_ARCHIVE;
    r->format.is_archive = true;
    xx_format_set_extension(&r->format, "ldbst");
    r->format.check_is_valid = ldbst_check;
    r->format.handle_base_info = ldbst_handle;
    r->format.get_format_size = ldbst_size;
    r->format.get_number_of_archive_records = ldbst_count;
    r->format.create_archive_records_reading = ldbst_create;
    r->format.get_current_archive_record = ldbst_current;
    r->format.archive_record_move_to_next = ldbst_next;
    r->format.unpack_current_archive_record = ldbst_unpack;
    r->format.free_archive_records_reading = ldbst_free_records;
    r->format.destroy = ldbst_destroy_vtable;
}
xx_ldbst *xx_ldbst_create(xx_io_device *d, int64_t b)
{
    xx_ldbst *r = (xx_ldbst *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ldbst_init(r, d, b);
    return r;
}
void xx_ldbst_destroy(xx_ldbst *r)
{
    if (r) ldbst_destroy_vtable(&r->format);
}
void xx_ldbst_free(xx_ldbst *r)
{
    if (r) {
        xx_ldbst_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ldbst_unpack_to_device(xx_ldbst *r, xx_io_device *d, xx_pd_struct *pd)
{
    return r && ldbst_unpack_device(&r->format, NULL, d, pd);
}
