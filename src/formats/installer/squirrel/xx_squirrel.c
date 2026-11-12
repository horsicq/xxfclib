/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Squirrel.Windows Setup.exe (Squirrel and Clowd.Squirrel): the setup stub
 * carries a ZIP in one of its resources that holds the application's
 * "<name>.<version>-full.nupkg" and the RELEASES index; the stub then runs
 * Update.exe from the package. The ZIP's end record sits well before the
 * file end (the resource section goes on), which is why the executable
 * carrier scans do not find it. The ZIP's members are listed and unpacked
 * through the ZIP reader; nothing runs. Bounded carrier parser.
 */
#include "xxfclib/formats/installer/squirrel/xx_squirrel.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_locator.h"
#include "../../common/xx_pe_resource_archives.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include <wchar.h>
#include <stdio.h>

#define SQ_MAX_BUNDLES 8U

typedef struct sq_bundle {
    size_t count;
    xx_zip *readers[SQ_MAX_BUNDLES];
    xx_io_device *views[SQ_MAX_BUNDLES];
} sq_bundle;
typedef struct sq_cursor {
    sq_bundle *bundle;
    size_t index;
    xx_archive_record_state *nested;
} sq_cursor;

static void sq_bundle_free(sq_bundle *b)
{
    size_t i;
    if (!b) return;
    for (i = 0; i < b->count; ++i) {
        xx_zip_free(b->readers[i]);
        xx_io_close(b->views[i]);
    }
    xx_mem_free(b);
}

/* Does a member name end in ".nupkg"? (ASCII, case-insensitive) */
static bool sq_is_nupkg(const wchar_t *name)
{
    static const wchar_t suffix[] = L".nupkg";
    size_t n = name ? wcslen(name) : 0U, k = 6U, i;
    if (n < k) return false;
    for (i = 0; i < k; ++i) {
        wchar_t c = name[n - k + i];
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + (L'a' - L'A'));
        if (c != suffix[i]) return false;
    }
    return true;
}

/* The ZIP over [at, at + bytes) of the carrier: validated by the ZIP reader
 * (every record walked), measured, and holding at least one .nupkg. On
 * success the reader and its view are handed back to the caller. */
static bool sq_zip_open(Abstractformat *f, int64_t at, int64_t bytes, xx_zip **out_zip, xx_io_device **out_view, int64_t *out_size, xx_pd_struct *pd)
{
    xx_io_device *view = NULL;
    xx_zip *zip = NULL;
    xx_archive_record_state *records = NULL;
    uint64_t total = 0, count = 0;
    int64_t size = 0;
    bool ok = false, nupkg = false;
    if (at < 0 || bytes <= 0 || carrier_stop(pd) || !carrier_range(pm_available(f), (uint64_t)at, (uint64_t)bytes)) return false;
    view = xx_io_sub_open_ro(f->device, f->base_address + at, bytes);
    zip = view ? xx_zip_create(view, 0) : NULL;
    if (!zip || !xx_zip_handle_base_info(xx_zip_to_format(zip), pd)) goto done;
    size = xx_zip_get_format_size(xx_zip_to_format(zip), pd);
    total = xx_zip_get_number_of_archive_records(xx_zip_to_format(zip), pd);
    if (size <= 0 || size > bytes || !total || total > 65536U) goto done;
    records = xx_zip_create_archive_records_reading(xx_zip_to_format(zip), NULL, pd);
    if (!records) goto done;
    for (;;) {
        const xx_archive_record *r = xx_zip_get_current_archive_record(xx_zip_to_format(zip), records);
        if (!r) break;
        if (carrier_stop(pd) || ++count > total) goto done;
        if (sq_is_nupkg(xx_archive_record_get_original_name_w(r))) nupkg = true;
        if (!xx_zip_archive_record_move_to_next(xx_zip_to_format(zip), records, pd)) break;
    }
    ok = count == total && nupkg && !carrier_stop(pd);
done:
    if (records) xx_zip_free_archive_records_reading(xx_zip_to_format(zip), records);
    if (ok) {
        *out_zip = zip;
        *out_view = view;
        *out_size = size;
    } else {
        if (zip) xx_zip_free(zip);
        if (view) xx_io_close(view);
    }
    return ok;
}

/* A resource whose data starts like a ZIP: a Squirrel bundle only when the
 * ZIP holds a .nupkg; any other ZIP resource is passed over. */
static bool sq_candidate(Abstractformat *f, pm_stream *s, int64_t at, uint32_t bytes, xx_pd_struct *pd)
{
    xx_zip *zip = NULL;
    xx_io_device *view = NULL;
    int64_t size = 0;
    if (!sq_zip_open(f, at, (int64_t)bytes, &zip, &view, &size, pd)) return !carrier_stop(pd);
    xx_zip_free(zip);
    xx_io_close(view);
    return executable_carrier_component(f, s, at, size, "bundle.zip");
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {0x50, 0x4b, 0x03, 0x04};
    return xx_sfx_pe_resource_archives(f, s, sig, sizeof(sig), sq_candidate, pd) && carrier_members(s, pd);
}

static sq_bundle *sq_nested(Abstractformat *f, xx_pd_struct *pd)
{
    xx_squirrel *outer = (xx_squirrel *)f;
    if (!f || carrier_stop(pd)) return NULL;
    if (!outer->nested && !xx_squirrel_handle_base_info(f, pd)) return NULL;
    return (sq_bundle *)outer->nested;
}
static int64_t sq_size(Abstractformat *f, xx_pd_struct *pd)
{
    return sq_nested(f, pd) ? f->format_size : -1;
}
static uint64_t sq_count(Abstractformat *f, xx_pd_struct *pd)
{
    return sq_nested(f, pd) ? f->number_of_archive_records : 0;
}
/* The caller's options for a nested reader; with a group the unpack path
 * gets a "/<group>" suffix (unused: the record names carry the prefix). */
static bool sq_options(xx_list_s *dst, const xx_list_s *src, size_t group)
{
    size_t i;
    xx_list_clear(dst);
    for (i = 0; src && i < src->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(src, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var)) {
            xx_meta_cleanup(&copy);
            return false;
        }
        if (group && m->meta_id == XX_META_ID_OPT_UNPACK_PATH) {
            wchar_t prefix[32], *base = NULL, *joined;
            bool ok;
            if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) base = xx_str_utf8_to_unicode(xx_var_get_str(&m->var));
            else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) base = xx_str_wdup(xx_var_get_wstr(&m->var));
            if (!base) {
                xx_meta_cleanup(&copy);
                return false;
            }
            swprintf(prefix, 32, L"/%u", (unsigned)group);
            joined = xx_str_wconcat(base, prefix);
            xx_str_wfree(base);
            ok = joined && xx_var_set_wstr(&copy.var, joined);
            xx_str_wfree(joined);
            if (!ok) {
                xx_meta_cleanup(&copy);
                return false;
            }
        }
        if (!xx_list_append(dst, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static bool sq_prefix(sq_cursor *c)
{
    wchar_t prefix[32], *joined;
    const wchar_t *name;
    bool ok;
    if (!c->nested || !c->nested->has_record) return false;
    if (c->bundle->count == 1) return true;
    swprintf(prefix, 32, L"%u/", (unsigned)c->index + 1);
    name = xx_archive_record_get_original_name_w(&c->nested->current_record);
    joined = name ? xx_str_wconcat(prefix, name) : NULL;
    ok = joined && xx_archive_record_set_original_name_w(&c->nested->current_record, joined);
    xx_str_wfree(joined);
    return ok;
}
static void sq_cursor_free(void *p)
{
    sq_cursor *c = (sq_cursor *)p;
    if (c) {
        if (c->nested) xx_zip_free_archive_records_reading(xx_zip_to_format(c->bundle->readers[c->index]), c->nested);
        xx_mem_free(c);
    }
}
static xx_archive_record_state *sq_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    sq_bundle *b = sq_nested(f, pd);
    xx_archive_record_state *s;
    sq_cursor *c;
    if (!b || !b->count) return NULL;
    s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_archive_record_state_init(s, f);
    c = (sq_cursor *)xx_mem_calloc(1, sizeof(*c));
    if (!c) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->internal_state = c;
    s->free_internal = sq_cursor_free;
    c->bundle = b;
    if (!sq_options(&s->options, opts, 0)) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    c->nested = xx_zip_create_archive_records_reading(xx_zip_to_format(b->readers[0]), &s->options, pd);
    if (!sq_prefix(c)) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->has_record = true;
    s->current_index = 0;
    s->total_records = (int64_t)f->number_of_archive_records;
    return s;
}
static const xx_archive_record *sq_current(Abstractformat *f, xx_archive_record_state *s)
{
    sq_cursor *c;
    if (!f || !s || s->format != f || !s->has_record) return NULL;
    c = (sq_cursor *)s->internal_state;
    return c && c->nested ? xx_zip_get_current_archive_record(xx_zip_to_format(c->bundle->readers[c->index]), c->nested) : NULL;
}
static bool sq_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    sq_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (sq_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    if (xx_zip_archive_record_move_to_next(xx_zip_to_format(c->bundle->readers[c->index]), c->nested, pd)) {
        if (!sq_prefix(c)) {
            s->has_record = false;
            return false;
        }
        ++s->current_index;
        return true;
    }
    if (carrier_stop(pd) || c->nested->current_index + 1 < c->nested->total_records) {
        s->has_record = false;
        return false;
    }
    xx_zip_free_archive_records_reading(xx_zip_to_format(c->bundle->readers[c->index]), c->nested);
    c->nested = NULL;
    if (++c->index >= c->bundle->count) {
        s->has_record = false;
        return false;
    }
    c->nested = xx_zip_create_archive_records_reading(xx_zip_to_format(c->bundle->readers[c->index]), &s->options, pd);
    if (!sq_prefix(c)) {
        s->has_record = false;
        return false;
    }
    ++s->current_index;
    return true;
}
static bool sq_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    sq_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (sq_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    /* The record name already carries the bundle prefix (sq_prefix): the path stays. */
    if (!sq_options(&c->nested->options, &s->options, 0)) return false;
    return xx_zip_unpack_current_archive_record(xx_zip_to_format(c->bundle->readers[c->index]), c->nested, pd);
}
static void sq_free_records(Abstractformat *f, xx_archive_record_state *s)
{
    (void)f;
    xx_archive_record_state_free(s);
}

void xx_squirrel_init(xx_squirrel *r, xx_io_device *d, int64_t b)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, b, XX_FILE_TYPE_SQUIRREL, "exe");
    r->format.check_is_valid = xx_squirrel_check_is_valid;
    r->format.handle_base_info = xx_squirrel_handle_base_info;
    r->format.get_format_size = sq_size;
    r->format.get_number_of_archive_records = sq_count;
    r->format.create_archive_records_reading = sq_records;
    r->format.get_current_archive_record = sq_current;
    r->format.archive_record_move_to_next = sq_next;
    r->format.unpack_current_archive_record = sq_unpack;
    r->format.free_archive_records_reading = sq_free_records;
}
xx_squirrel *xx_squirrel_create(xx_io_device *d, int64_t b)
{
    xx_squirrel *r = (xx_squirrel *)xx_mem_alloc(sizeof(*r));
    if (r) xx_squirrel_init(r, d, b);
    return r;
}
void xx_squirrel_destroy(xx_squirrel *r)
{
    if (!r) return;
    if (r->nested) {
        sq_bundle_free((sq_bundle *)r->nested);
        r->nested = NULL;
    }
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_squirrel_free(xx_squirrel *r)
{
    if (r) {
        xx_squirrel_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_squirrel_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_squirrel_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_squirrel *outer = (xx_squirrel *)f;
    pm_stream *s;
    sq_bundle *bundle;
    uint64_t records = 0;
    size_t i;
    int64_t end;
    if (!outer || carrier_stop(pd)) return false;
    if (outer->nested) return true;
    s = pm_open(f, pd);
    if (!s) return false;
    if (!s->count || s->count > SQ_MAX_BUNDLES) {
        pm_free_stream(s);
        return false;
    }
    bundle = (sq_bundle *)xx_mem_calloc(1, sizeof(*bundle));
    if (!bundle) {
        pm_free_stream(s);
        return false;
    }
    for (i = 0; i < s->count; ++i) {
        xx_zip *zip = NULL;
        xx_io_device *view = NULL;
        int64_t size = 0;
        uint64_t n;
        if (s->items[i].offset < f->base_address ||
            !sq_zip_open(f, s->items[i].offset - f->base_address, s->items[i].size, &zip, &view, &size, pd)) {
            sq_bundle_free(bundle);
            pm_free_stream(s);
            return false;
        }
        n = xx_zip_get_number_of_archive_records(xx_zip_to_format(zip), pd);
        if (n > 65536U - records) {
            xx_zip_free(zip);
            xx_io_close(view);
            sq_bundle_free(bundle);
            pm_free_stream(s);
            return false;
        }
        bundle->readers[bundle->count] = zip;
        bundle->views[bundle->count] = view;
        ++bundle->count;
        records += n;
    }
    f->format_size = s->size;
    f->number_of_archive_records = records;
    end = f->base_address + s->size;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    outer->nested = bundle;
    pm_free_stream(s);
    return true;
}
