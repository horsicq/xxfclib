/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * install4j Windows installers (ej-technologies install4j; measured on the
 * BiglyBT 2.4 and 2.8 installers, install4j 8). After the launcher's
 * sections:
 *
 *   config      u32 magic D5 13 E4 E8 (as bytes), u32 version 1, 8 bytes,
 *               u32 0, then keyed blocks: u32 count, count x (u32 key,
 *               u32 length, bytes); a second block holds UTF-16 messages
 *   runtime     obfuscated blobs (the installer runtime), not read here
 *   table       big-endian, Java DataOutput style: u32 count, count x
 *               (u16 length, name such as "165.dat", u64 packed size)
 *   entries     back to back in table order, each an .lzma stream (5 bytes
 *               of properties, u64 unpacked size, LZMA) around a ZIP of the
 *               installed files (".install4j/i4jdel.exe", the application)
 *   Authenticode signature
 *
 * The table is located by its first ".dat" name (the runtime blobs have no
 * readable framing) and every entry must decode to a ZIP. Members are the
 * ZIPs' members, prefixed by the entry name without its extension; the
 * entries are decoded once into temporary devices. Nothing is executed.
 */
#include "xxfclib/formats/installer/install4j/xx_install4j.h"
#include "../../common/xx_executable_carrier.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"
#include <wchar.h>

#define I4_MAGIC 0xE8E413D5U
#define I4_HEADER 24U
#define I4_MAX_ENTRIES 64U
#define I4_MAX_NAME 64U
#define I4_SEARCH (INT64_C(64) * 1024 * 1024)
#define I4_CHUNK 65536U
#define I4_LZMA_HEADER 13U
#define I4_MAX_UNPACKED (INT64_C(2) * 1024 * 1024 * 1024)
#define I4_MAX_CANDIDATES 16U

typedef struct i4_bundle {
    size_t count;
    xx_zip *readers[I4_MAX_ENTRIES];
    xx_io_device *stores[I4_MAX_ENTRIES];
    wchar_t prefixes[I4_MAX_ENTRIES][I4_MAX_NAME + 2];
} i4_bundle;
typedef struct i4_cursor {
    i4_bundle *bundle;
    size_t index;
    xx_archive_record_state *nested;
} i4_cursor;

static void i4_bundle_free(i4_bundle *b)
{
    size_t i;
    if (!b) return;
    for (i = 0; i < b->count; ++i) {
        if (b->readers[i]) xx_zip_free(b->readers[i]);
        if (b->stores[i]) xx_io_close(b->stores[i]);
    }
    xx_mem_free(b);
}

/* The first ".dat" name followed by the high zero bytes of its u64 size,
 * searched from `from` in overlapping chunks. */
static bool i4_find_dat(Abstractformat *f, int64_t from, int64_t limit, int64_t *where, xx_pd_struct *pd)
{
    static const uint8_t tag[8] = {'.', 'd', 'a', 't', 0, 0, 0, 0};
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(I4_CHUNK + sizeof(tag));
    int64_t at = from;
    bool found = false;
    if (!buffer) return false;
    while (!found && limit - at >= (int64_t)sizeof(tag)) {
        size_t n = limit - at < (int64_t)(I4_CHUNK + sizeof(tag)) ? (size_t)(limit - at) : I4_CHUNK + sizeof(tag), k;
        if (carrier_stop(pd) || !pm_read(f, at, buffer, n)) break;
        for (k = 0; k + sizeof(tag) <= n; ++k) {
            if (buffer[k] == '.' && xx_rt_memcmp(buffer + k, tag, sizeof(tag)) == 0) {
                *where = at + (int64_t)k;
                found = true;
                break;
            }
        }
        if (n < I4_CHUNK + sizeof(tag)) break;
        at += (int64_t)I4_CHUNK;
    }
    xx_mem_free(buffer);
    return found;
}

/* A plausible .lzma header: known properties, a dictionary, a size. */
static bool i4_lzma_header(const uint8_t *h, int64_t *unpacked)
{
    uint32_t dictionary = xx_data_get_u32(h + 1, 4, 0, false);
    uint64_t size = xx_data_get_u64(h + 5, 8, 0, false);
    if (h[0] >= 225U || dictionary < 4096U || dictionary > 256U * 1024U * 1024U || !size || size > (uint64_t)I4_MAX_UNPACKED) return false;
    *unpacked = (int64_t)size;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[I4_HEADER], raw[2 + I4_MAX_NAME + 8], lzma[I4_LZMA_HEADER];
    int64_t overlay, cabinet, cabinet_end, available = pm_available(f), dat, table = -1, at, data, from, limit;
    uint32_t count = 0, i, back, candidate;
    if (!carrier_pe(f, &overlay, &cabinet, &cabinet_end, pd) || overlay <= 0 || available - overlay < (int64_t)(I4_HEADER + 32U) || !pm_read(f, overlay, header, sizeof(header)))
        return false;
    if (xx_data_get_u32(header, 4, 0, false) != I4_MAGIC || xx_data_get_u32(header + 4, 4, 0, false) != 1U) return false;
    /* The table: the first ".dat" name whose u16 length and u32 count hold up;
     * a stray hit in the runtime blobs is passed over. */
    from = overlay + (int64_t)I4_HEADER;
    limit = available - overlay > I4_SEARCH ? overlay + I4_SEARCH : available;
    for (candidate = 0; table < 0 && candidate < I4_MAX_CANDIDATES; ++candidate) {
        if (!i4_find_dat(f, from, limit, &dat, pd)) return false;
        from = dat + 1;
        for (back = 2; back <= 2 + I4_MAX_NAME; ++back) {
            int64_t c = dat + 4 - (int64_t)back;
            uint8_t probe[6];
            if (c < overlay + (int64_t)I4_HEADER + 4 || !pm_read(f, c - 4, probe, 6)) break;
            if (xx_data_get_u16(probe + 4, 2, 0, true) != back - 2) continue;
            count = xx_data_get_u32(probe, 4, 0, true);
            if (count && count <= I4_MAX_ENTRIES) table = c - 4;
            break;
        }
    }
    if (table < 0) return false;
    at = table + 4;
    for (i = 0; i < count; ++i) { /* the names and packed sizes */
        uint16_t length;
        size_t k;
        if (carrier_stop(pd) || available - at < 10 || !pm_read(f, at, raw, 2)) return false;
        length = xx_data_get_u16(raw, 2, 0, true);
        if (!length || length > I4_MAX_NAME || available - at < 2 + (int64_t)length + 8 || !pm_read(f, at + 2, raw + 2, (size_t)length + 8U)) return false;
        for (k = 0; k < length; ++k) {
            if (raw[2 + k] < 0x20U || raw[2 + k] > 0x7EU) return false;
        }
        {
            char name[I4_MAX_NAME + 1];
            uint64_t packed = xx_data_get_u64(raw + 2 + length, 8, 0, true);
            if (packed <= I4_LZMA_HEADER || packed > (uint64_t)available) return false;
            xx_rt_memcpy(name, raw + 2, length);
            name[length] = 0;
            if (!pm_add(f, s, name, 0, (int64_t)packed)) return false; /* offsets follow the table */
        }
        at += 2 + length + 8;
    }
    data = at;
    for (i = 0; i < count; ++i) { /* the entries: an .lzma stream each */
        pm_member *m = &s->items[i];
        int64_t unpacked;
        if (m->packed_size > available - data || !pm_read(f, data, lzma, sizeof(lzma)) || !i4_lzma_header(lzma, &unpacked)) return false;
        m->offset = f->base_address + data;
        m->size = unpacked;
        m->compression_method = 14U; /* LZMA, as ZIP numbers it */
        data += m->packed_size;
    }
    s->size = data;
    return !carrier_stop(pd);
}

static i4_bundle *i4_nested(Abstractformat *f, xx_pd_struct *pd)
{
    xx_install4j *outer = (xx_install4j *)f;
    if (!f || carrier_stop(pd)) return NULL;
    if (!outer->nested && !xx_install4j_handle_base_info(f, pd)) return NULL;
    return (i4_bundle *)outer->nested;
}
static int64_t i4_size(Abstractformat *f, xx_pd_struct *pd)
{
    return i4_nested(f, pd) ? f->format_size : -1;
}
static uint64_t i4_count(Abstractformat *f, xx_pd_struct *pd)
{
    return i4_nested(f, pd) ? f->number_of_archive_records : 0;
}
/* The caller's options for a nested reader; with a prefix the unpack path
 * gets it appended (unused: the record names carry the entry prefix). */
static bool i4_options(xx_list_s *dst, const xx_list_s *src, const wchar_t *prefix)
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
        if (prefix && m->meta_id == XX_META_ID_OPT_UNPACK_PATH) {
            wchar_t *base = NULL, *joined, *slashed;
            bool ok;
            if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) base = xx_str_utf8_to_unicode(xx_var_get_str(&m->var));
            else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) base = xx_str_wdup(xx_var_get_wstr(&m->var));
            if (!base) {
                xx_meta_cleanup(&copy);
                return false;
            }
            slashed = xx_str_wconcat(base, L"/");
            xx_str_wfree(base);
            joined = slashed ? xx_str_wconcat(slashed, prefix) : NULL;
            xx_str_wfree(slashed);
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
static bool i4_prefix(i4_cursor *c)
{
    wchar_t *joined, *slashed;
    const wchar_t *name;
    bool ok;
    if (!c->nested || !c->nested->has_record) return false;
    name = xx_archive_record_get_original_name_w(&c->nested->current_record);
    slashed = xx_str_wconcat(c->bundle->prefixes[c->index], L"/");
    joined = slashed && name ? xx_str_wconcat(slashed, name) : NULL;
    xx_str_wfree(slashed);
    ok = joined && xx_archive_record_set_original_name_w(&c->nested->current_record, joined);
    xx_str_wfree(joined);
    return ok;
}
static void i4_cursor_free(void *p)
{
    i4_cursor *c = (i4_cursor *)p;
    if (c) {
        if (c->nested) xx_zip_free_archive_records_reading(xx_zip_to_format(c->bundle->readers[c->index]), c->nested);
        xx_mem_free(c);
    }
}
static xx_archive_record_state *i4_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    i4_bundle *b = i4_nested(f, pd);
    xx_archive_record_state *s;
    i4_cursor *c;
    if (!b || !b->count) return NULL;
    s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_archive_record_state_init(s, f);
    c = (i4_cursor *)xx_mem_calloc(1, sizeof(*c));
    if (!c) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->internal_state = c;
    s->free_internal = i4_cursor_free;
    c->bundle = b;
    if (!i4_options(&s->options, opts, NULL)) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    c->nested = xx_zip_create_archive_records_reading(xx_zip_to_format(b->readers[0]), &s->options, pd);
    if (!i4_prefix(c)) {
        xx_archive_record_state_free(s);
        return NULL;
    }
    s->has_record = true;
    s->current_index = 0;
    s->total_records = (int64_t)f->number_of_archive_records;
    return s;
}
static const xx_archive_record *i4_current(Abstractformat *f, xx_archive_record_state *s)
{
    i4_cursor *c;
    if (!f || !s || s->format != f || !s->has_record) return NULL;
    c = (i4_cursor *)s->internal_state;
    return c && c->nested ? xx_zip_get_current_archive_record(xx_zip_to_format(c->bundle->readers[c->index]), c->nested) : NULL;
}
static bool i4_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    i4_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (i4_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    if (xx_zip_archive_record_move_to_next(xx_zip_to_format(c->bundle->readers[c->index]), c->nested, pd)) {
        if (!i4_prefix(c)) {
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
    if (!i4_prefix(c)) {
        s->has_record = false;
        return false;
    }
    ++s->current_index;
    return true;
}
static bool i4_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    i4_cursor *c;
    if (!f || !s || s->format != f || !s->has_record || carrier_stop(pd)) return false;
    c = (i4_cursor *)s->internal_state;
    if (!c || !c->nested) return false;
    /* The record name already carries the entry prefix: the path stays. */
    if (!i4_options(&c->nested->options, &s->options, NULL)) return false;
    return xx_zip_unpack_current_archive_record(xx_zip_to_format(c->bundle->readers[c->index]), c->nested, pd);
}
static void i4_free_records(Abstractformat *f, xx_archive_record_state *s)
{
    (void)f;
    xx_archive_record_state_free(s);
}

void xx_install4j_init(xx_install4j *r, xx_io_device *d, int64_t b)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, b, XX_FILE_TYPE_INSTALL4J, "exe");
    r->format.check_is_valid = xx_install4j_check_is_valid;
    r->format.handle_base_info = xx_install4j_handle_base_info;
    r->format.get_format_size = i4_size;
    r->format.get_number_of_archive_records = i4_count;
    r->format.create_archive_records_reading = i4_records;
    r->format.get_current_archive_record = i4_current;
    r->format.archive_record_move_to_next = i4_next;
    r->format.unpack_current_archive_record = i4_unpack;
    r->format.free_archive_records_reading = i4_free_records;
}
xx_install4j *xx_install4j_create(xx_io_device *d, int64_t b)
{
    xx_install4j *r = (xx_install4j *)xx_mem_alloc(sizeof(*r));
    if (r) xx_install4j_init(r, d, b);
    return r;
}
void xx_install4j_destroy(xx_install4j *r)
{
    if (!r) return;
    if (r->nested) {
        i4_bundle_free((i4_bundle *)r->nested);
        r->nested = NULL;
    }
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_install4j_free(xx_install4j *r)
{
    if (r) {
        xx_install4j_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_install4j_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
/* Decode every entry once and open the ZIP inside it. */
bool xx_install4j_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_install4j *outer = (xx_install4j *)f;
    pm_stream *s;
    i4_bundle *bundle = NULL;
    uint64_t records = 0;
    size_t i;
    int64_t end;
    bool ok = false;
    if (!outer || carrier_stop(pd)) return false;
    if (outer->nested) return true;
    s = pm_open(f, pd);
    if (!s) return false;
    if (!s->count || s->count > I4_MAX_ENTRIES) goto done;
    bundle = (i4_bundle *)xx_mem_calloc(1, sizeof(*bundle));
    if (!bundle) goto done;
    for (i = 0; i < s->count; ++i) {
        pm_member *m = &s->items[i];
        uint8_t props[5];
        xx_io_device *store;
        xx_zip *zip;
        uint64_t n;
        const char *name = m->name;
        size_t k, length;
        /* The label carries the framework's "NNNN-" position: skip it. */
        for (k = 0; name[k] >= '0' && name[k] <= '9'; ++k) {
        }
        if (k && name[k] == '-') name += k + 1;
        length = xx_rt_strlen(name);
        if (carrier_stop(pd) || !pm_read(f, m->offset - f->base_address, props, sizeof(props))) goto done;
        store = xx_io_temp_open();
        if (!store) goto done;
        bundle->stores[bundle->count] = store;
        ++bundle->count;
        if (!xx_lzma_unpack_device(f->device, m->offset + I4_LZMA_HEADER, m->packed_size - I4_LZMA_HEADER, props, 5U, m->size, store, pd) ||
            xx_io_total_size(store) != m->size)
            goto done;
        zip = xx_zip_create(store, 0);
        if (!zip) goto done;
        bundle->readers[bundle->count - 1] = zip;
        if (!xx_zip_handle_base_info(xx_zip_to_format(zip), pd)) goto done;
        n = xx_zip_get_number_of_archive_records(xx_zip_to_format(zip), pd);
        if (!n || n > 1048576U - records) goto done;
        records += n;
        /* The entry's name without its extension is the member prefix. */
        if (length > 4 && name[length - 4] == '.') length -= 4;
        if (length > I4_MAX_NAME) length = I4_MAX_NAME;
        for (k = 0; k < length; ++k) bundle->prefixes[bundle->count - 1][k] = (wchar_t)(unsigned char)name[k];
        bundle->prefixes[bundle->count - 1][length] = 0;
    }
    f->format_size = s->size;
    f->number_of_archive_records = records;
    end = f->base_address + s->size;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    outer->nested = bundle;
    bundle = NULL;
    ok = true;
done:
    i4_bundle_free(bundle);
    pm_free_stream(s);
    return ok;
}
