/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original sector mapping implementation. Layout facts are independently
 * recorded from the public HxC disk layout descriptions; no emulator code.
 */
#include "xxfclib/formats/hxc_raw_floppy/xx_hxc_raw_floppy.h"
#include "xxfclib/formats/hxc_xml_disk_layout/xx_hxc_xml_disk_layout.h"
#include "xxfclib/xml/xx_xml.h"
#include "../xx_payload_members.h"
#include <string.h>
#define HX_MAX_IMAGE UINT64_C(67108864)
#define HX_MAX_RUNS 65536U
#define HX_MAX_XML 2097152U
typedef struct hx_plan {
    xx_hxc_raw_profile profile;
    uint64_t logical_size;
    size_t allocation;
    uint32_t ignored_tracks;
    char name[128];
    /* extents and their inline bytes follow this header in one allocation. */
} hx_plan;
#include "xx_hxc_raw_profiles.inc"

static bool hx_budget(Abstractformat *f, const xx_list_s *opts, uint64_t bytes)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, opts, XX_META_ID_OPT_MEMORY_LIMIT);
    return !v || bytes <= xx_var_get_u64(v);
}
static bool hx_read(Abstractformat *f, uint64_t at, void *data, size_t length, xx_pd_struct *pd)
{
    size_t done = 0;
    int64_t available = pm_available(f);
    if (available < 0 || at > (uint64_t)available || length > (uint64_t)available - at || (pd && xx_pd_is_stopped(pd)) ||
        xx_io_seek64(f->device, f->base_address + (int64_t)at, SEEK_SET) != 0)
        return false;
    while (done < length) {
        size_t request = length - done;
        ssize_t received;
        if (request > 65536U) request = 65536U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        received = xx_io_read(f->device, (uint8_t *)data + done, request);
        if (received <= 0 || (size_t)received > request || (pd && xx_pd_is_stopped(pd))) return false;
        done += (size_t)received;
    }
    return true;
}
static bool hx_profile_valid(const xx_hxc_raw_profile *p, uint64_t *logical, size_t *bytes)
{
    size_t i, memory;
    uint64_t total = 0;
    if (!p || !p->name || !p->tracks || p->tracks > 256 || !p->sides || p->sides > 2 || !p->source_size || p->source_size > HX_MAX_IMAGE || !p->extents ||
        !p->extent_count || p->extent_count > HX_MAX_RUNS)
        return false;
    memory = sizeof(hx_plan) + p->extent_count * sizeof(xx_hxc_raw_extent);
    for (i = 0; i < p->extent_count; ++i) {
        const xx_hxc_raw_extent *e = &p->extents[i];
        uint64_t n = (uint64_t)e->size * e->count, last = e->offset;
        if (!e->size || e->size > 32768U || !e->count || e->count > HX_MAX_RUNS || n > HX_MAX_IMAGE - total || e->mode > XX_HXC_RAW_INLINE) return false;
        total += n;
        if (e->mode == XX_HXC_RAW_SOURCE) {
            uint64_t delta;
            if (e->stride == INT64_MIN) return false;
            delta = (uint64_t)(e->stride < 0 ? -e->stride : e->stride);
            if (e->count > 1U && delta > UINT64_MAX / (e->count - 1U)) return false;
            delta *= e->count - 1U;
            if (e->stride < 0) {
                if (delta > last) return false;
                last -= delta;
            } else {
                if (delta > UINT64_MAX - last) return false;
                last += delta;
            }
            if (e->offset > p->source_size || e->size > p->source_size - e->offset || last > p->source_size || e->size > p->source_size - last) return false;
        } else if (e->mode == XX_HXC_RAW_INLINE) {
            if (!e->data || n > SIZE_MAX - memory) return false;
            memory += (size_t)n;
        }
    }
    *logical = total;
    *bytes = memory;
    return total != 0;
}
static hx_plan *hx_clone(const xx_hxc_raw_profile *p, Abstractformat *f, const xx_list_s *opts)
{
    hx_plan *q;
    xx_hxc_raw_extent *runs;
    uint8_t *tail;
    uint64_t logical;
    size_t bytes, i;
    if (!hx_profile_valid(p, &logical, &bytes) || (f && !hx_budget(f, opts, bytes))) return NULL;
    q = (hx_plan *)xx_mem_alloc(bytes);
    if (!q) return NULL;
    xx_mem_zero(q, sizeof(*q));
    q->profile = *p;
    q->logical_size = logical;
    q->allocation = bytes;
    xx_rt_snprintf(q->name, sizeof(q->name), "%s", p->name);
    q->profile.name = q->name;
    runs = (xx_hxc_raw_extent *)(q + 1);
    q->profile.extents = runs;
    xx_rt_memcpy(runs, p->extents, p->extent_count * sizeof(*runs));
    tail = (uint8_t *)(runs + p->extent_count);
    for (i = 0; i < p->extent_count; ++i)
        if (runs[i].mode == XX_HXC_RAW_INLINE) {
            size_t n = (size_t)runs[i].size * runs[i].count;
            xx_rt_memcpy(tail, runs[i].data, n);
            runs[i].data = tail;
            tail += n;
        }
    return q;
}
static hx_plan *hx_xml_plan(const void *, size_t, bool, Abstractformat *, const xx_list_s *, xx_pd_struct *);
#include "xx_hxc_xml_parser.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_hxc_raw_floppy *r = (xx_hxc_raw_floppy *)f;
    hx_plan *plan = NULL;
    int64_t available = pm_available(f);
    bool xml = f->file_type == XX_FILE_TYPE_HXC_XML_DISK_LAYOUT;
    if (available < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    if (xml) {
        uint8_t *text;
        if (!available || available > HX_MAX_XML || !hx_budget(f, r->parse_options, (uint64_t)available * 2U)) return false;
        text = (uint8_t *)xx_mem_alloc((size_t)available);
        if (!text) return false;
        if (hx_read(f, 0, text, (size_t)available, pd)) plan = hx_xml_plan(text, (size_t)available, false, f, r->parse_options, pd);
        xx_mem_free(text);
    } else if (r->profile && (uint64_t)available == r->profile->source_size) {
        plan = hx_clone(r->profile, f, r->parse_options);
        if (plan && r->owned_profile) plan->ignored_tracks = ((hx_plan *)r->owned_profile)->ignored_tracks;
    }
    if (!plan) return false;
    if (!pm_add(f, s, xml ? "disk-layout.xml" : "original-raw.img", 0, available) ||
        !pm_add(f, s, xml ? "initialized-layout-sectors.img" : "mapped-layout-sectors.img", 0, 0)) {
        xx_mem_free(plan);
        return false;
    }
    s->items[1].memory = (uint8_t *)plan;
    s->items[1].size = (int64_t)plan->logical_size;
    s->items[1].packed_size = 0;
    s->size = available;
    return true;
}
static bool hx_comment(xx_archive_record_state *st)
{
    pm_stream *s = (pm_stream *)st->internal_state;
    hx_plan *p;
    char text[512];
    size_t i;
    bool synthesized = false;
    if (!s || s->count != 2 || !s->items[1].memory) return false;
    p = (hx_plan *)s->items[1].memory;
    for (i = 0; i < p->profile.extent_count; ++i)
        if (p->profile.extents[i].mode != XX_HXC_RAW_SOURCE) synthesized = true;
    xx_rt_snprintf(text, sizeof(text), "HxC layout %s; %u cylinders, %u heads; %llu mapped sector bytes. %s%s", p->profile.name, (unsigned)p->profile.tracks,
                   (unsigned)p->profile.sides, (unsigned long long)p->logical_size,
                   st->format->file_type == XX_FILE_TYPE_HXC_XML_DISK_LAYOUT
                       ? "Descriptor initialization only: inline sector data and declared fills; external image data is not loaded. "
                       : "Explicit raw geometry; source has no sector checksums. ",
                   synthesized ? "Layout fill/template bytes are generated, not recovered from the source." : "Original source retained separately.");
    if (p->ignored_tracks) {
        size_t length = xx_rt_strlen(text);
        xx_rt_snprintf(text + length, sizeof(text) - length, " %u track declaration(s) ignored outside declared geometry.", (unsigned)p->ignored_tracks);
    }
    return xx_archive_record_set_meta_str(&st->current_record, XX_META_ID_COMMENT, text);
}
static xx_archive_record_state *hx_create_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_hxc_raw_floppy *r = (xx_hxc_raw_floppy *)f;
    xx_archive_record_state *st;
    r->parse_options = opts;
    st = pm_create_records(f, opts, pd);
    r->parse_options = NULL;
    if (st && !hx_comment(st)) {
        xx_archive_record_state_free(st);
        return NULL;
    }
    return st;
}
static bool hx_next(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    if (!pm_next(f, st, pd)) return false;
    if (!hx_comment(st)) {
        st->has_record = false;
        return false;
    }
    return true;
}
static bool hx_unpack(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    pm_stream *s;
    pm_member *m;
    hx_plan *p;
    const xx_var *v;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL;
    xx_io_device *out = NULL;
    uint8_t *buffer = NULL;
    bool ok = false, overwrite = false;
    int64_t cursor;
    size_t i, capacity = 65536U, run_count;
    xx_hxc_raw_extent original;
    const xx_hxc_raw_extent *runs;
    if (!f || !st || st->format != f || !st->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    s = (pm_stream *)st->internal_state;
    m = &s->items[s->index];
    p = (hx_plan *)s->items[1].memory;
    if (!p) return false;
    runs = p->profile.extents;
    run_count = p->profile.extent_count;
    if (s->index == 0) {
        xx_mem_zero(&original, sizeof(original));
        original.size = (uint32_t)m->size;
        original.count = 1;
        runs = &original;
        run_count = 1;
    }
    v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && (uint64_t)m->size > xx_var_get_u64(v)) return false;
    v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (v) {
        uint64_t limit = xx_var_get_u64(v);
        if (limit <= p->allocation) return false;
        if (limit - p->allocation < capacity) capacity = (size_t)(limit - p->allocation);
    }
    cursor = xx_io_tell(f->device);
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) goto done;
    v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_UNPACK_PATH);
    if (v) {
        if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(v);
        else if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) base = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(v));
        if (!base) goto done;
        path = base[0] ? xx_str_concat3(base, "/", m->name) : xx_str_dup(m->name);
        if (!path || !xx_store_create_dirs_a(path, false)) goto done;
        v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_OVERWRITE);
        overwrite = v && xx_var_get_bool(v);
        if (!overwrite && xx_io_file_exists_a(path)) goto done;
        out = pm_stage(path, &stage);
        if (!out) goto done;
    }
    ok = true;
    for (i = 0; ok && i < run_count; ++i) {
        const xx_hxc_raw_extent *e = &runs[i];
        uint32_t j;
        for (j = 0; ok && j < e->count; ++j) {
            uint64_t at = 0;
            size_t position = 0;
            if (e->mode == XX_HXC_RAW_SOURCE) at = e->stride < 0 ? e->offset - (uint64_t)(-e->stride) * j : e->offset + (uint64_t)e->stride * j;
            while (ok && position < e->size) {
                size_t n = e->size - position, written = 0;
                if (n > capacity) n = capacity;
                if (pd && xx_pd_is_stopped(pd)) {
                    ok = false;
                    break;
                }
                if (e->mode == XX_HXC_RAW_SOURCE) {
                    if (!hx_read(f, at + position, buffer, n, pd)) {
                        ok = false;
                        break;
                    }
                } else if (e->mode == XX_HXC_RAW_FILL) xx_rt_memset(buffer, e->fill, n);
                else xx_rt_memcpy(buffer, e->data + (size_t)e->size * j + position, n);
                while (out && written < n) {
                    ssize_t got;
                    if (pd && xx_pd_is_stopped(pd)) {
                        ok = false;
                        break;
                    }
                    got = xx_io_write(out, buffer + written, n - written);
                    if (got <= 0 || (size_t)got > n - written) {
                        ok = false;
                        break;
                    }
                    written += (size_t)got;
                }
                position += n;
            }
        }
    }
done:
    if (pd && xx_pd_is_stopped(pd)) ok = false;
    (void)xx_io_seek64(f->device, cursor, SEEK_SET);
    if (out && xx_io_close(out) != 0) ok = false;
    if (ok && stage) ok = !(pd && xx_pd_is_stopped(pd)) && xx_io_file_replace_a(stage, path, overwrite);
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (buffer) {
        xx_mem_free(buffer);
    }
    if (path) xx_str_free(path);
    if (owned) xx_str_free(owned);
    return ok;
}
size_t xx_hxc_raw_floppy_profile_count(void)
{
    return sizeof(hx_profiles) / sizeof(hx_profiles[0]);
}
const xx_hxc_raw_profile *xx_hxc_raw_floppy_profile_at(size_t n)
{
    return n < xx_hxc_raw_floppy_profile_count() ? &hx_profiles[n] : NULL;
}
const xx_hxc_raw_profile *xx_hxc_raw_floppy_profile_by_name(const char *name)
{
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < xx_hxc_raw_floppy_profile_count(); ++i)
        if (!strcmp(name, hx_profiles[i].name)) return &hx_profiles[i];
    return NULL;
}
void xx_hxc_raw_floppy_init(xx_hxc_raw_floppy *r, xx_io_device *d, int64_t b)
{
    if (!r) {
        return;
    }
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, b, XX_FILE_TYPE_HXC_RAW_FLOPPY, "raw");
    r->format.create_archive_records_reading = hx_create_records;
    r->format.archive_record_move_to_next = hx_next;
    r->format.unpack_current_archive_record = hx_unpack;
}
xx_hxc_raw_floppy *xx_hxc_raw_floppy_create(xx_io_device *d, int64_t b)
{
    xx_hxc_raw_floppy *r = (xx_hxc_raw_floppy *)xx_mem_alloc(sizeof(*r));
    if (r) xx_hxc_raw_floppy_init(r, d, b);
    return r;
}
xx_hxc_raw_floppy *xx_hxc_raw_floppy_create_profile(xx_io_device *d, int64_t b, const char *name)
{
    const xx_hxc_raw_profile *p = xx_hxc_raw_floppy_profile_by_name(name);
    xx_hxc_raw_floppy *r;
    if (!p) {
        return NULL;
    }
    r = xx_hxc_raw_floppy_create(d, b);
    if (r) r->profile = p;
    return r;
}
xx_hxc_raw_floppy *xx_hxc_raw_floppy_create_layout(xx_io_device *d, int64_t b, const xx_hxc_raw_profile *p)
{
    hx_plan *q = hx_clone(p, NULL, NULL);
    xx_hxc_raw_floppy *r;
    if (!q) {
        return NULL;
    }
    r = xx_hxc_raw_floppy_create(d, b);
    if (!r) {
        xx_mem_free(q);
        return NULL;
    }
    r->owned_profile = q;
    r->profile = &q->profile;
    return r;
}
xx_hxc_raw_floppy *xx_hxc_raw_floppy_create_layout_xml(xx_io_device *d, int64_t b, const void *xml, size_t size)
{
    hx_plan *q = hx_xml_plan(xml, size, true, NULL, NULL, NULL);
    xx_hxc_raw_floppy *r;
    if (!q) {
        return NULL;
    }
    r = xx_hxc_raw_floppy_create(d, b);
    if (!r) {
        xx_mem_free(q);
        return NULL;
    }
    r->owned_profile = q;
    r->profile = &q->profile;
    return r;
}
void xx_hxc_raw_floppy_destroy(xx_hxc_raw_floppy *r)
{
    if (r) {
        xx_format_cleanup_extra_parameters(&r->format);
        xx_mem_free(r->owned_profile);
        r->owned_profile = NULL;
        r->profile = NULL;
    }
}
void xx_hxc_raw_floppy_free(xx_hxc_raw_floppy *r)
{
    if (r) {
        xx_hxc_raw_floppy_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_hxc_raw_floppy_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_hxc_raw_floppy_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
