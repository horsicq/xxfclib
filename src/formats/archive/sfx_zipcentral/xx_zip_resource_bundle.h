/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Private PE TYPE_ZIP_FILE bundle. Each resource owns an independent, bounded
 * native ZIP reader; the executable is never loaded or executed. */
#ifndef XX_ZIP_RESOURCE_BUNDLE_H
#define XX_ZIP_RESOURCE_BUNDLE_H

typedef struct sfx_zip_bundle_entry {
    sfx_carrier_view view;
    xx_zip zip;
    wchar_t prefix[128];
} sfx_zip_bundle_entry;
typedef struct sfx_zip_bundle {
    sfx_zip_bundle_entry *entries[128];
    unsigned count;
    uint64_t records;
    int64_t stored;
    bool encrypted;
} sfx_zip_bundle;
typedef struct sfx_zip_bundle_cursor {
    sfx_zip_bundle *bundle;
    unsigned entry;
    xx_archive_record_state *inner;
} sfx_zip_bundle_cursor;

static sfx_zip_bundle *sfx_zip_bundle_get(Abstractformat *f)
{
    return f && f->priv && f->priv != &sfx_zip_resource_invalid ? (sfx_zip_bundle *)f->priv : NULL;
}
static void sfx_zip_bundle_destroy(sfx_zip_bundle *bundle)
{
    unsigned i;
    if (!bundle) return;
    for (i = 0; i < bundle->count; ++i) {
        xx_zip_destroy(&bundle->entries[i]->zip);
        xx_mem_free(bundle->entries[i]);
    }
    xx_mem_free(bundle);
}
/* Validate the original path before adding a provenance prefix. In particular,
 * prefixing must not turn an absolute path or parent traversal into a safe one. */
static bool sfx_zip_bundle_name(sfx_zip_bundle_entry *entry, xx_archive_record_state *st, xx_pd_struct *pd)
{
    uint8_t h[30], raw[4097];
    wchar_t joined[4225], *decoded = NULL;
    const wchar_t *name;
    size_t n, i, segment = 0, prefix;
    bool folder;
    xx_archive_record *r = &st->current_record;
    if (carrier_stop(pd) || (r->data_offset < 0 && !xx_zip_resolve_record_data_offset(&entry->zip, r)) || r->header_offset < 0 ||
        !xx_io_read_at(&entry->view.device, r->header_offset, h, sizeof(h)) || xx_rt_memcmp(h, "PK\3\4", 4)) return false;
    n = xx_data_get_u16(h + 26, 2, 0, false);
    if (!n || n > 4096 || r->data_offset != r->header_offset + 30 + (int64_t)n + xx_data_get_u16(h + 28, 2, 0, false) ||
        !xx_io_read_at(&entry->view.device, r->header_offset + 30, raw, n)) return false;
    for (i = 0; i < n; ++i) if (!raw[i]) return false;
    raw[n] = 0;
    if (xx_data_get_u16(h + 6, 2, 0, false) & 0x0800U) {
        decoded = xx_str_utf8_to_unicode((const char *)raw);
        if (!decoded) return false;
        name = decoded;
    } else {
        for (i = 0; i < n; ++i)
            joined[i] = (wchar_t)xx_codepage_decode_byte(xx_codepage_by_number(437), raw[i], 0xfffd);
        joined[n] = 0;
        name = joined;
    }
    n = xx_rt_wcslen(name);
    folder = xx_archive_record_get_meta_bool(r, XX_META_ID_IS_FOLDER, false);
    if (!n || n > 4096 || name[0] == L'/' || name[0] == L'\\') goto bad;
    for (i = 0; i <= n; ++i) {
        wchar_t c = name[i];
        if (c && (c < 32 || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')) goto bad;
        if (!c || c == L'/' || c == L'\\') {
            size_t length = i - segment;
            if (!length && !(i == n && folder && segment == n)) goto bad;
            if ((length == 1 && name[segment] == L'.') ||
                (length == 2 && name[segment] == L'.' && name[segment + 1] == L'.')) goto bad;
            segment = i + 1;
        }
    }
    prefix = xx_rt_wcslen(entry->prefix);
    /* The decoded name may already live in joined: move it from the end. */
    for (i = n + 1; i > 0; --i) joined[prefix + i - 1] = name[i - 1];
    for (i = 0; i < prefix; ++i) joined[i] = entry->prefix[i];
    xx_str_wfree(decoded);
    return xx_archive_record_set_original_name_w(r, joined);
bad:
    xx_str_wfree(decoded);
    return false;
}
static bool sfx_zip_bundle_prefix(sfx_zip_bundle_entry *entry, const xx_resource_record *row)
{
    char text[128];
    size_t i, n;
    if (!row->name || xx_rt_strncmp(row->name, "ZIP_FILE_", 9) || row->language_name || row->language_id > 65535U) return false;
    n = xx_rt_strlen(row->name);
    if (n <= 9 || n > 96) return false;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)row->name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    if (xx_rt_snprintf(text, sizeof(text), "%s/%u/", row->name, (unsigned)row->language_id) < 0) return false;
    n = xx_rt_strlen(text);
    if (n >= 128) return false;
    for (i = 0; i <= n; ++i) entry->prefix[i] = (wchar_t)(unsigned char)text[i];
    return true;
}
static bool sfx_zip_bundle_same_prefix(const wchar_t *a, const wchar_t *b)
{
    size_t i;
    for (i = 0;; ++i) {
        wchar_t x = a[i], y = b[i];
        if (x >= L'a' && x <= L'z') x -= L'a' - L'A';
        if (y >= L'a' && y <= L'z') y -= L'a' - L'A';
        if (x != y) return false;
        if (!x) return true;
    }
}
static int sfx_zip_bundle_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    xx_pe pe;
    xx_resource_state *resources = NULL;
    sfx_zip_bundle *bundle = NULL;
    uint8_t h[64], magic[4];
    uint32_t pe_at;
    unsigned rows = 0;
    int result = 0;
    bool recognized = false;
    int64_t total = xx_io_total_size(f->device);
    if (sfx_zip_bundle_get(f)) return 1;
    if (f->priv == &sfx_zip_resource_invalid) return -1;
    if (pm_available(f) < 64 || !pm_read(f, 0, h, 64) || h[0] != 'M' || h[1] != 'Z') return 0;
    pe_at = xx_data_get_u32(h + 60, 4, 0, false);
    if (pe_at < 64 || pe_at > (uint64_t)pm_available(f) - 4 || !pm_read(f, pe_at, magic, 4) || xx_rt_memcmp(magic, "PE\0\0", 4)) return 0;
    xx_pe_init(&pe, f->device, f->base_address);
    resources = pe_resource_stream_create(&pe.format, pd);
    if (!resources) goto done;
    bundle = (sfx_zip_bundle *)xx_mem_alloc(sizeof(*bundle));
    if (!bundle) goto done;
    xx_mem_zero(bundle, sizeof(*bundle));
    for (;;) {
        const xx_resource_record *row = pe_resource_stream_current(&pe.format, resources);
        if (!row) break;
        if (++rows > 4096U || carrier_stop(pd)) goto done;
        if (row->type_name && !xx_rt_strcmp(row->type_name, "TYPE_ZIP_FILE")) {
            sfx_zip_bundle_entry *e;
            xx_archive_record_state *st;
            uint64_t seen = 0;
            unsigned i;
            bool good = true;
            recognized = true;
            if (bundle->count == 128 || row->size < 22 || row->offset < f->base_address || row->offset > total ||
                row->size > total - row->offset || row->size > 256 * 1024 * 1024 - bundle->stored) goto done;
            e = (sfx_zip_bundle_entry *)xx_mem_alloc(sizeof(*e));
            if (!e) goto done;
            xx_mem_zero(e, sizeof(*e));
            sfx_carrier_view_init(&e->view, f->device, row->offset, row->size);
            xx_zip_init(&e->zip, &e->view.device, 0);
            bundle->entries[bundle->count++] = e;
            if (!sfx_zip_bundle_prefix(e, row)) goto done;
            for (i = 0; i + 1 < bundle->count; ++i)
                if (sfx_zip_bundle_same_prefix(bundle->entries[i]->prefix, e->prefix)) goto done;
            if (!xx_io_read_at(&e->view.device, 0, magic, 4) ||
                (xx_rt_memcmp(magic, "PK\3\4", 4) && xx_rt_memcmp(magic, "PK\5\6", 4)) ||
                !xx_zip_handle_base_info(&e->zip.format, pd) || e->zip.format.format_size != row->size ||
                e->zip.format.number_of_archive_records > 4096U ||
                e->zip.format.number_of_archive_records > 65536U - bundle->records) goto done;
            st = xx_zip_create_archive_records_reading(&e->zip.format, NULL, pd);
            if (!st) goto done;
            while (st->has_record) {
                const xx_archive_record *m = &st->current_record;
                if (carrier_stop(pd) || ++seen > 4096U || m->compressed_size < 0 ||
                    (m->data_offset < 0 && !xx_zip_resolve_record_data_offset(&e->zip, &st->current_record)) || m->data_offset < 0 ||
                    m->data_offset > row->size || m->compressed_size > row->size - m->data_offset || !sfx_zip_bundle_name(e, st, pd)) {
                    good = false;
                    break;
                }
                bundle->encrypted |= xx_archive_record_get_meta_bool(m, XX_META_ID_IS_ENCRYPTED, false);
                if (!xx_zip_archive_record_move_to_next(&e->zip.format, st, pd)) break;
            }
            if (seen != e->zip.format.number_of_archive_records || e->zip.directory_damaged) good = false;
            xx_zip_free_archive_records_reading(&e->zip.format, st);
            if (!good) goto done;
            bundle->records += seen;
            bundle->stored += row->size;
        }
        if (!pe_resource_stream_next(&pe.format, resources, pd)) break;
    }
    if (resources->failed || !bundle->count || carrier_stop(pd)) goto done;
    f->priv = bundle;
    bundle = NULL;
    result = 1;
done:
    if (resources) pe_resource_stream_free(&pe.format, resources);
    xx_pe_destroy(&pe);
    sfx_zip_bundle_destroy(bundle);
    if (!result && recognized) {
        f->priv = (void *)&sfx_zip_resource_invalid;
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid PE ZIP resource bundle");
    }
    return result ? result : (recognized || carrier_stop(pd) ? -1 : 0);
}
static void sfx_zip_bundle_cursor_free(void *data)
{
    sfx_zip_bundle_cursor *c = (sfx_zip_bundle_cursor *)data;
    if (!c) return;
    if (c->inner) xx_zip_free_archive_records_reading(&c->bundle->entries[c->entry]->zip.format, c->inner);
    xx_mem_free(c);
}
static bool sfx_zip_bundle_publish(xx_archive_record_state *outer, xx_pd_struct *pd)
{
    sfx_zip_bundle_cursor *c = (sfx_zip_bundle_cursor *)outer->internal_state;
    sfx_zip_bundle_entry *e = c->bundle->entries[c->entry];
    xx_archive_record *dst = &outer->current_record, *src = &c->inner->current_record;
    size_t i;
    if (!sfx_zip_bundle_name(e, c->inner, pd)) return false;
    xx_archive_record_cleanup(dst);
    xx_archive_record_init(dst);
    dst->header_offset = e->view.begin + src->header_offset;
    dst->header_size = src->header_size;
    dst->data_offset = e->view.begin + src->data_offset;
    dst->compressed_size = src->compressed_size;
    for (i = 0; i < src->list_meta.count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(&src->list_meta, i);
        if (!m || !xx_archive_record_add_meta(dst, m->meta_id, &m->var)) return false;
    }
    outer->has_record = true;
    ++outer->current_index;
    return true;
}
static bool sfx_zip_bundle_open_next(xx_archive_record_state *outer, xx_pd_struct *pd)
{
    sfx_zip_bundle_cursor *c = (sfx_zip_bundle_cursor *)outer->internal_state;
    while (c->entry < c->bundle->count) {
        sfx_zip_bundle_entry *e = c->bundle->entries[c->entry];
        if (carrier_stop(pd)) return false;
        c->inner = xx_zip_create_archive_records_reading(&e->zip.format, &outer->options, pd);
        if (!c->inner) return false;
        if (c->inner->has_record) return sfx_zip_bundle_publish(outer, pd);
        xx_zip_free_archive_records_reading(&e->zip.format, c->inner);
        c->inner = NULL;
        ++c->entry;
    }
    return true;
}
static xx_archive_record_state *sfx_zip_bundle_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_archive_record_state *st = (xx_archive_record_state *)xx_mem_alloc(sizeof(*st));
    sfx_zip_bundle_cursor *c;
    size_t i;
    if (!st) return NULL;
    xx_archive_record_state_init(st, f);
    c = (sfx_zip_bundle_cursor *)xx_mem_alloc(sizeof(*c));
    if (!c) goto fail;
    xx_mem_zero(c, sizeof(*c));
    c->bundle = sfx_zip_bundle_get(f);
    st->internal_state = c;
    st->free_internal = sfx_zip_bundle_cursor_free;
    st->total_records = (int64_t)c->bundle->records;
    if (opts) for (i = 0; i < opts->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at(opts, i);
        xx_meta copy;
        if (!m) goto fail;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var) || !xx_list_append(&st->options, &copy)) {
            xx_meta_cleanup(&copy);
            goto fail;
        }
    }
    if (!sfx_zip_bundle_open_next(st, pd)) goto fail;
    return st;
fail:
    xx_archive_record_state_free(st);
    return NULL;
}
static bool sfx_zip_bundle_next(xx_archive_record_state *st, xx_pd_struct *pd)
{
    sfx_zip_bundle_cursor *c;
    sfx_zip_bundle_entry *e;
    bool result;
    if (!st || !st->has_record || carrier_stop(pd)) return false;
    c = (sfx_zip_bundle_cursor *)st->internal_state;
    e = c->bundle->entries[c->entry];
    st->has_record = false;
    xx_archive_record_cleanup(&st->current_record);
    xx_archive_record_init(&st->current_record);
    if (xx_zip_archive_record_move_to_next(&e->zip.format, c->inner, pd))
        return sfx_zip_bundle_publish(st, pd);
    if (carrier_stop(pd) || e->zip.directory_damaged ||
        c->inner->current_index + 1 != c->inner->total_records) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Incomplete ZIP resource directory");
        return false;
    }
    xx_zip_free_archive_records_reading(&e->zip.format, c->inner);
    c->inner = NULL;
    ++c->entry;
    result = sfx_zip_bundle_open_next(st, pd);
    if (!result && !carrier_stop(pd)) xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Cannot read next ZIP resource");
    return result && st->has_record;
}
static bool sfx_zip_bundle_unpack(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    sfx_zip_bundle_cursor *c;
    sfx_zip_bundle_entry *e;
    xx_list_s options, parameters;
    bool result;
    if (!st || !st->has_record || carrier_stop(pd)) return false;
    c = (sfx_zip_bundle_cursor *)st->internal_state;
    if (!c || !c->inner || c->entry >= c->bundle->count) return false;
    e = c->bundle->entries[c->entry];
    options = c->inner->options;
    parameters = e->zip.format.list_extra_parameters;
    c->inner->options = st->options;
    e->zip.format.list_extra_parameters = f->list_extra_parameters;
    result = xx_zip_unpack_current_archive_record(&e->zip.format, c->inner, pd);
    c->inner->options = options;
    e->zip.format.list_extra_parameters = parameters;
    return result;
}
#endif
