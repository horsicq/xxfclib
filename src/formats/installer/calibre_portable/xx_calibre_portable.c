/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * calibre portable installer for Windows (calibre's own setup code), measured
 * on calibre 7.1: the resource of type "EXTRA", name "EXTRA", holds one lzip
 * member (version 0: "LZIP", version byte, coded dictionary size, an LZMA
 * stream with lc 3 lp 0 pb 2, then a 12-byte trailer of CRC-32 and u64 size)
 * whose content is a stored ZIP of the "Calibre Portable" tree (808 entries,
 * 459 MiB). The resource is decoded once by the lzip reader into a temporary
 * device, the ZIP inside is read by the ZIP reader and its records are this
 * reader's records. Nothing is executed.
 */
#include "xxfclib/formats/installer/calibre_portable/xx_calibre_portable.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_locator.h"
#include "../../common/xx_pe_resource_archives.h"
#include "xxfclib/formats/archive/lzip/xx_lzip.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xxfclib/io/xx_io.h"

#define CP_MIN_RESOURCE (64U * 1024U)
#define CP_HEADER 6U

static const uint8_t cp_magic[4] = {'L', 'Z', 'I', 'P'};

typedef struct cp_bundle {
    xx_io_device *view;  /* the resource */
    xx_lzip *lzip;       /* the member over the view */
    xx_io_device *store; /* the decoded ZIP */
    xx_zip *zip;
    Abstractformat *nested; /* the ZIP as a format */
} cp_bundle;
typedef struct cp_cursor {
    cp_bundle *bundle;
    xx_archive_record_state *nested;
} cp_cursor;

static void cp_bundle_free(cp_bundle *b)
{
    if (!b) return;
    if (b->zip) xx_zip_free(b->zip);
    if (b->store) xx_io_close(b->store);
    if (b->lzip) xx_lzip_free(b->lzip);
    if (b->view) xx_io_close(b->view);
    xx_mem_free(b);
}

/* A resource that starts like an lzip member: version 0 or 1, a dictionary
 * of 4 KiB to 512 MiB. One payload per installer. */
static bool cp_candidate(Abstractformat *f, pm_stream *s, int64_t at, uint32_t bytes, xx_pd_struct *pd)
{
    uint8_t h[CP_HEADER];
    if (carrier_stop(pd)) return false;
    if (bytes < CP_MIN_RESOURCE || !pm_read(f, at, h, sizeof(h)) || h[4] > 1U || (h[5] & 0x1FU) < 12U || (h[5] & 0x1FU) > 29U) return true;
    if (s->count) return true;
    return executable_carrier_component(f, s, at, bytes, "payload.lz");
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return xx_sfx_pe_resource_archives(f, s, cp_magic, sizeof(cp_magic), cp_candidate, pd) && s->count > 0 && carrier_members(s, pd);
}

static cp_bundle *cp_nested(Abstractformat *f, xx_pd_struct *pd)
{
    xx_calibre_portable *outer = (xx_calibre_portable *)f;
    if (!f || carrier_stop(pd)) return NULL;
    if (!outer->nested && !xx_calibre_portable_handle_base_info(f, pd)) return NULL;
    return (cp_bundle *)outer->nested;
}
static int64_t cp_size(Abstractformat *f, xx_pd_struct *pd)
{
    return cp_nested(f, pd) ? f->format_size : -1;
}
static uint64_t cp_count(Abstractformat *f, xx_pd_struct *pd)
{
    return cp_nested(f, pd) ? f->number_of_archive_records : 0;
}
static bool cp_options(xx_list_s *dst, const xx_list_s *src)
{
    size_t i;
    xx_list_clear(dst);
    for (i = 0; src && i < src->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(src, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var) || !xx_list_append(dst, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static void cp_cursor_free(void *p)
{
    cp_cursor *c = (cp_cursor *)p;
    if (c) {
        if (c->nested) xx_format_free_archive_records_reading(c->bundle->nested, c->nested);
        xx_mem_free(c);
    }
}
static xx_archive_record_state *cp_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    cp_bundle *b = cp_nested(f, pd);
    xx_archive_record_state *s;
    cp_cursor *c;
    if (!b || !b->nested) return NULL;
    s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_archive_record_state_init(s, f);
    c = (cp_cursor *)xx_mem_calloc(1, sizeof(*c));
    if (!c) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->internal_state = c;
    s->free_internal = cp_cursor_free;
    c->bundle = b;
    if (!cp_options(&s->options, opts)) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    c->nested = xx_format_create_archive_records_reading(b->nested, &s->options, pd);
    if (!c->nested || !c->nested->has_record) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->has_record = true;
    s->current_index = 0;
    s->total_records = (int64_t)f->number_of_archive_records;
    return s;
}
static const xx_archive_record *cp_current(Abstractformat *f, xx_archive_record_state *s)
{
    cp_cursor *c;
    if (!f || !s || s->format != f || !s->has_record) return NULL;
    c = (cp_cursor *)s->internal_state;
    return c && c->nested ? xx_format_get_current_archive_record(c->bundle->nested, c->nested) : NULL;
}
static bool cp_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    cp_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (cp_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    if (xx_format_archive_record_move_to_next(c->bundle->nested, c->nested, pd)) {
        ++s->current_index;
        return true;
    }
    s->has_record = false;
    return false;
}
static bool cp_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    cp_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (cp_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    if (!cp_options(&c->nested->options, &s->options)) return false;
    return xx_format_unpack_current_archive_record(c->bundle->nested, c->nested, pd);
}
static void cp_free_records(Abstractformat *f, xx_archive_record_state *s)
{
    (void)f;
    xx_archive_record_state_free(s);
}

void xx_calibre_portable_init(xx_calibre_portable *r, xx_io_device *d, int64_t b)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, b, XX_FILE_TYPE_CALIBRE_PORTABLE, "exe");
    r->format.check_is_valid = xx_calibre_portable_check_is_valid;
    r->format.handle_base_info = xx_calibre_portable_handle_base_info;
    r->format.get_format_size = cp_size;
    r->format.get_number_of_archive_records = cp_count;
    r->format.create_archive_records_reading = cp_records;
    r->format.get_current_archive_record = cp_current;
    r->format.archive_record_move_to_next = cp_next;
    r->format.unpack_current_archive_record = cp_unpack;
    r->format.free_archive_records_reading = cp_free_records;
}
xx_calibre_portable *xx_calibre_portable_create(xx_io_device *d, int64_t b)
{
    xx_calibre_portable *r = (xx_calibre_portable *)xx_mem_alloc(sizeof(*r));
    if (r) xx_calibre_portable_init(r, d, b);
    return r;
}
void xx_calibre_portable_destroy(xx_calibre_portable *r)
{
    if (!r) return;
    if (r->nested) {
        cp_bundle_free((cp_bundle *)r->nested);
        r->nested = NULL;
    }
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_calibre_portable_free(xx_calibre_portable *r)
{
    if (r) {
        xx_calibre_portable_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_calibre_portable_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
/* Decode the payload once and open the ZIP inside it. */
bool xx_calibre_portable_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_calibre_portable *outer = (xx_calibre_portable *)f;
    pm_stream *s;
    cp_bundle *bundle = NULL;
    int64_t end;
    bool ok = false;
    if (!outer || carrier_stop(pd)) return false;
    if (outer->nested) return true;
    s = pm_open(f, pd);
    if (!s) return false;
    if (s->count != 1U || s->items[0].offset < f->base_address || s->items[0].size <= 0) goto done;
    bundle = (cp_bundle *)xx_mem_calloc(1, sizeof(*bundle));
    if (!bundle) goto done;
    bundle->view = xx_io_sub_open_ro(f->device, s->items[0].offset, s->items[0].size);
    bundle->lzip = bundle->view ? xx_lzip_create(bundle->view, 0) : NULL;
    if (!bundle->lzip || !xx_lzip_check_is_valid(xx_lzip_to_format(bundle->lzip), pd) || !xx_lzip_handle_base_info(xx_lzip_to_format(bundle->lzip), pd)) goto done;
    bundle->store = xx_io_temp_open();
    if (!bundle->store || !xx_lzip_unpack_to_device(bundle->lzip, bundle->store, pd) || carrier_stop(pd)) goto done;
    if ((uint64_t)xx_io_total_size(bundle->store) != xx_lzip_get_uncompressed_size(bundle->lzip)) goto done;
    bundle->zip = xx_zip_create(bundle->store, 0);
    if (!bundle->zip) goto done;
    bundle->nested = xx_zip_to_format(bundle->zip);
    if (!xx_zip_check_is_valid(bundle->nested, pd) || !xx_zip_handle_base_info(bundle->nested, pd) || !bundle->nested->number_of_archive_records) goto done;
    f->format_size = s->size;
    f->number_of_archive_records = bundle->nested->number_of_archive_records;
    end = f->base_address + s->size;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    outer->nested = bundle;
    bundle = NULL;
    ok = true;
done:
    cp_bundle_free(bundle);
    pm_free_stream(s);
    return ok;
}
