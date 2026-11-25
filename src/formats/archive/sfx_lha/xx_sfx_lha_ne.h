/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_SFX_LHA_NE_PRIVATE_H
#define XX_SFX_LHA_NE_PRIVATE_H
#include "xxfclib/strings/xx_string.h"
#include "../../executable/pe/xx_pe_resources_internal.h"
#define SL_NE_MAX_RESOURCES 4096U
#define SL_NE_MAX_RECORDS 100000U
#define SL_NE_MAX_BYTES (256U * 1024U * 1024U)
typedef struct sl_ne_volume {
    xx_io_device *view;
    xx_lha archive;
    int64_t offset, size;
    uint64_t records;
    uint16_t type, id;
    char folder[96];
} sl_ne_volume;
typedef struct sl_ne_context {
    sl_ne_volume *volumes;
    size_t count;
    uint64_t records;
    int64_t size;
} sl_ne_context;
typedef struct sl_ne_records {
    xx_archive_record_state base;
    xx_archive_record_state *active;
    size_t volume;
} sl_ne_records;
static void sl_ne_free(sl_ne_context *c)
{
    size_t i;
    if (!c) return;
    for (i = 0; i < c->count; i++) {
        xx_lha_destroy(&c->volumes[i].archive);
        if (c->volumes[i].view) xx_io_close(c->volumes[i].view);
    }
    xx_mem_free(c->volumes);
    xx_mem_free(c);
}
static bool sl_ne_padding(Abstractformat *f, int64_t at, int64_t end, xx_pd_struct *pd)
{
    uint8_t bytes[256];
    while (at < end) {
        size_t i, n = end - at > (int64_t)sizeof(bytes) ? sizeof(bytes) : (size_t)(end - at);
        if (sfx_lha_stop(pd) || !sfx_lha_read(f, at, bytes, n)) return false;
        for (i = 0; i < n; i++)
            if (bytes[i]) return false;
        at += (int64_t)n;
    }
    return true;
}
static bool sl_resource_add(sl_ne_context *c, Abstractformat *f, int64_t at, int64_t size, const char *folder, xx_pd_struct *pd)
{
    int64_t limit = sfx_lha_available(f), logical;
    uint64_t bytes = 0;
    size_t i, length = xx_rt_strlen(folder);
    sl_ne_volume *v;
    if (at < 0 || size < 24 || at > limit || size > limit - at || !length || length >= sizeof(c->volumes[0].folder) || c->count >= SL_NE_MAX_RESOURCES) return false;
    for (i = 0; i < c->count; i++) {
        sl_ne_volume *old = &c->volumes[i];
        const char *a = folder, *b = old->folder;
        bytes += (uint64_t)old->size;
        while (*a && *b) {
            unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
            if (x >= 'A' && x <= 'Z') x += 32;
            if (y >= 'A' && y <= 'Z') y += 32;
            if (x != y) break;
            ++a;
            ++b;
        }
        if ((!*a && !*b) || (at < old->offset + old->size && old->offset < at + size)) return false;
    }
    if (bytes > SL_NE_MAX_BYTES || (uint64_t)size > SL_NE_MAX_BYTES - bytes) return false;
    v = &c->volumes[c->count++];
    v->offset = at;
    v->size = size;
    xx_rt_memcpy(v->folder, folder, length + 1U);
    v->view = xx_io_sub_open_ro(f->device, f->base_address + at, size);
    if (!v->view) return false;
    xx_lha_init(&v->archive, v->view, 0);
    v->archive.sanitize_sfx_drive = true;
    if (!xx_lha_handle_base_info(&v->archive.format, pd) || !(v->records = v->archive.format.number_of_archive_records) || v->records > SL_NE_MAX_RECORDS - c->records)
        return false;
    logical = v->archive.format.format_size;
    if (logical <= 0 || logical > size || !sl_ne_padding(f, at + logical, at + size, pd)) return false;
    c->records += v->records;
    return true;
}
static int sl_pe_open(xx_sfx_lha *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    int64_t limit = sfx_lha_available(f);
    uint8_t h[4];
    uint32_t pe_at;
    xx_pe pe;
    xx_resource_state *state = NULL;
    sl_ne_context *c = NULL;
    bool recognized = false, ok = false;
    unsigned rows = 0;
    if (limit < 128 || !sfx_lha_read(f, 60, h, 4)) return 0;
    pe_at = xx_data_get_u32(h, 4, 0, false);
    if (pe_at < 64U || pe_at > (uint64_t)limit - 4U || !sfx_lha_read(f, pe_at, h, 4) || xx_rt_memcmp(h, "PE\0\0", 4)) return 0;
    xx_pe_init(&pe, f->device, f->base_address);
    state = pe_resource_stream_create(&pe.format, pd);
    if (!state) goto done;
    c = (sl_ne_context *)xx_mem_calloc(1, sizeof(*c));
    if (!c) goto done;
    c->volumes = (sl_ne_volume *)xx_mem_calloc(SL_NE_MAX_RESOURCES, sizeof(*c->volumes));
    if (!c->volumes) goto done;
    c->size = limit;
    for (;;) {
        const xx_resource_record *record = pe_resource_stream_current(&pe.format, state);
        char folder[96];
        size_t i, n;
        if (!record) break;
        if (++rows > SL_NE_MAX_RESOURCES || sfx_lha_stop(pd)) goto done;
        if (record->type_name && !xx_rt_strcmp(record->type_name, "ARCHIV")) {
            recognized = true;
            if (!record->name) goto done;
            n = xx_rt_strlen(record->name);
            if (!n || n > 64U || record->name[0] == '.') goto done;
            for (i = 0; i < n; i++) {
                unsigned char ch = (unsigned char)record->name[i];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) goto done;
            }
            if (record->name[n - 1U] == '.') goto done;
            xx_rt_snprintf(folder, sizeof(folder), "ARCHIV/%s", record->name);
            if (record->offset < f->base_address || !sl_resource_add(c, f, record->offset - f->base_address, record->size, folder, pd)) goto done;
        }
        if (!pe_resource_stream_next(&pe.format, state, pd)) break;
    }
    if (state->failed || !c->count || sfx_lha_stop(pd)) goto done;
    r->format.priv = c;
    c = NULL;
    xx_lha_init(&r->inner, f->device, 0);
    r->inner_ready = true;
    ok = true;
done:
    if (state) pe_resource_stream_free(&pe.format, state);
    xx_pe_destroy(&pe);
    sl_ne_free(c);
    return ok ? 1 : (recognized || sfx_lha_stop(pd) ? -1 : 0);
}
/* Named NE identifiers are bounded Pascal strings relative to the resource
 * table, never pointers into the archive payload. Keep a separate spelling
 * from numeric IDs so an identifier named "1" cannot alias numeric ID1. */
static bool sl_ne_identifier(Abstractformat *f, uint64_t table, uint64_t end, uint16_t id, char *out, size_t capacity)
{
    uint8_t bytes[65];
    uint64_t at = table + id;
    size_t i, n;
    if ((id & 0x8000U) || at >= end || !sfx_lha_read(f, (int64_t)at, bytes, 1)) return false;
    n = bytes[0];
    if (!n || n > 64U || n >= capacity || n > end - at - 1U || !sfx_lha_read(f, (int64_t)at + 1, bytes, n)) return false;
    if (bytes[0] == '.' || bytes[n - 1U] == '.') return false;
    for (i = 0; i < n; i++) {
        uint8_t c = bytes[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        out[i] = (char)c;
    }
    out[n] = 0;
    return true;
}
/* Numeric custom types 500/501 and DAVINCI type32513 hold LHA archives.
 * Names, decoding and payload checksums stay in the ordinary LHA reader. */
static int sl_ne_open(xx_sfx_lha *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    int64_t limit = sfx_lha_available(f);
    uint8_t h[64], type[8], record[12];
    uint32_t ne;
    uint64_t pos, end;
    uint16_t shift;
    uint64_t table_start;
    unsigned types = 0;
    bool terminated = false, recognized = false;
    sl_ne_context *c = NULL;
    if (limit < 128 || !sfx_lha_read(f, 60, h, 4)) return false;
    ne = xx_data_get_u32(h, 4, 0, false);
    if (ne < 64U || ne > (uint64_t)limit - 64U || !sfx_lha_read(f, ne, h, 64) || h[0] != 'N' || h[1] != 'E') return false;
    pos = (uint64_t)ne + xx_data_get_u16(h + 36, 2, 0, false);
    end = (uint64_t)ne + xx_data_get_u16(h + 38, 2, 0, false);
    if (pos < (uint64_t)ne + 64U || end <= pos + 2U || end > (uint64_t)limit || !sfx_lha_read(f, (int64_t)pos, h, 2)) return false;
    shift = xx_data_get_u16(h, 2, 0, false);
    if (shift > 15U) return false;
    table_start = pos;
    pos += 2U;
    c = (sl_ne_context *)xx_mem_calloc(1, sizeof(*c));
    if (!c) return false;
    c->volumes = (sl_ne_volume *)xx_mem_calloc(SL_NE_MAX_RESOURCES, sizeof(*c->volumes));
    if (!c->volumes) goto fail;
    c->size = limit;
    while (pos + 2U <= end) {
        uint16_t id, count;
        unsigned i;
        bool wave = false;
        if (sfx_lha_stop(pd) || !sfx_lha_read(f, (int64_t)pos, type, 2)) goto fail;
        id = xx_data_get_u16(type, 2, 0, false);
        if (id == 0x81f4U || id == 0x81f5U || id == 0xff01U || id == 0x87a9U) recognized = true;
        if (!id) {
            terminated = true;
            break;
        }
        if (!(id & 0x8000U) && table_start + id < end) {
            uint8_t text[5];
            uint64_t at = table_start + id;
            if (at + 5U <= end && sfx_lha_read(f, (int64_t)at, text, 5) && text[0] == 4U && !xx_rt_memcmp(text + 1, "WAVE", 4)) wave = true;
        }
        if (++types > 256U || pos + 8U > end || !sfx_lha_read(f, (int64_t)pos, type, 8)) goto fail;
        count = xx_data_get_u16(type + 2, 2, 0, false);
        pos += 8U;
        if ((uint64_t)count * 12U > end - pos) goto fail;
        for (i = 0; i < count; i++, pos += 12U) {
            uint64_t at, size;
            uint16_t name;
            char folder[96];
            if (sfx_lha_stop(pd) || !sfx_lha_read(f, (int64_t)pos, record, 12)) goto fail;
            if (id != 0x81f4U && id != 0x81f5U && id != 0xff01U && id != 0x87a9U && !wave) continue;
            at = (uint64_t)xx_data_get_u16(record, 2, 0, false) << shift;
            size = (uint64_t)xx_data_get_u16(record + 2, 2, 0, false) << shift;
            name = xx_data_get_u16(record + 6, 2, 0, false);
            if (wave) {
                uint8_t magic[7];
                if (at > (uint64_t)limit || size > (uint64_t)limit - at || size < sizeof(magic) || !sfx_lha_read(f, (int64_t)at, magic, sizeof(magic))) goto fail;
                if (magic[2] != '-' || magic[3] != 'l' || magic[6] != '-') continue;
                recognized = true;
            }
            if (name & 0x8000U) {
                if (wave) xx_rt_snprintf(folder, sizeof(folder), "WAVE-%u", name & 0x7fffU);
                else xx_rt_snprintf(folder, sizeof(folder), "%u-%u", id & 0x7fffU, name & 0x7fffU);
            } else {
                char identifier[65];
                if (!sl_ne_identifier(f, table_start, end, name, identifier, sizeof(identifier))) goto fail;
                if (wave) xx_rt_snprintf(folder, sizeof(folder), "WAVE-name-%s", identifier);
                else xx_rt_snprintf(folder, sizeof(folder), "%u-name-%s", id & 0x7fffU, identifier);
            }
            if (!sl_resource_add(c, f, (int64_t)at, (int64_t)size, folder, pd)) goto fail;
        }
    }
    if (!terminated || !c->count || sfx_lha_stop(pd)) goto fail;
    r->format.priv = c;
    xx_lha_init(&r->inner, f->device, 0);
    r->inner_ready = true;
    return true;
fail:
    sl_ne_free(c);
    return recognized || sfx_lha_stop(pd) ? -1 : 0;
}
static bool sl_ne_options(xx_archive_record_state *target, const xx_list_s *source, const sl_ne_volume *v)
{
    size_t i;
    char prefix[96];
    xx_rt_snprintf(prefix, sizeof(prefix), "%s", v->folder);
    for (i = 0; source && i < source->count; i++) {
        const xx_meta *m = (const xx_meta *)xx_list_at(source, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var)) goto bad;
        if (m->meta_id == XX_META_ID_OPT_UNPACK_PATH) {
            char *converted = NULL, *path;
            const char *base = NULL;
            if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(&m->var);
            else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
                converted = xx_str_unicode_to_utf8(xx_var_get_wstr(&m->var));
                base = converted;
            }
            path = base ? xx_str_concat3(base, "/", prefix) : NULL;
            xx_str_free(converted);
            if (!path) goto bad;
            if (!xx_var_set_str(&copy.var, path)) {
                xx_str_free(path);
                goto bad;
            }
            xx_str_free(path);
        }
        if (!xx_list_append(&target->options, &copy)) goto bad;
        continue;
    bad:
        xx_meta_cleanup(&copy);
        return false;
    }
    return true;
}
static bool sl_ne_publish(Abstractformat *f, sl_ne_records *s)
{
    sl_ne_context *c = (sl_ne_context *)f->priv;
    sl_ne_volume *v = &c->volumes[s->volume];
    const xx_archive_record *r = xx_lha_get_current_archive_record(&v->archive.format, s->active);
    xx_archive_record *t = &s->base.current_record;
    size_t i;
    char prefix[100], *name;
    const char *original;
    if (!r || r->header_offset < 0 || r->header_offset > v->size || r->data_offset < 0 || r->data_offset > v->size || r->compressed_size < 0 ||
        r->compressed_size > v->size - r->data_offset)
        return false;
    xx_archive_record_cleanup(t);
    xx_archive_record_init(t);
    t->header_offset = f->base_address + v->offset + r->header_offset;
    t->header_size = r->header_size;
    t->data_offset = f->base_address + v->offset + r->data_offset;
    t->compressed_size = r->compressed_size;
    for (i = 0; i < r->list_meta.count; i++) {
        const xx_meta *m = (const xx_meta *)xx_list_at(&r->list_meta, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var) || !xx_list_append(&t->list_meta, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    original = xx_archive_record_get_original_name(r);
    if (!original) return false;
    xx_rt_snprintf(prefix, sizeof(prefix), "%s/", v->folder);
    name = xx_str_concat(prefix, original);
    if (!name) return false;
    i = xx_archive_record_set_original_name(t, name);
    xx_str_free(name);
    return i != 0;
}
static bool sl_ne_activate(Abstractformat *f, sl_ne_records *s, xx_pd_struct *pd)
{
    sl_ne_context *c = (sl_ne_context *)f->priv;
    sl_ne_volume *v = &c->volumes[s->volume];
    xx_archive_record_state options;
    bool ok;
    xx_archive_record_state_init(&options, f);
    ok = sl_ne_options(&options, &s->base.options, v);
    s->active = ok ? xx_lha_create_archive_records_reading(&v->archive.format, &options.options, pd) : NULL;
    xx_archive_record_state_cleanup(&options);
    return s->active && sl_ne_publish(f, s);
}
static xx_archive_record_state *sl_ne_create(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    sl_ne_context *c = (sl_ne_context *)f->priv;
    sl_ne_records *s;
    size_t i;
    if (!c || sfx_lha_stop(pd)) return NULL;
    s = (sl_ne_records *)xx_mem_calloc(1, sizeof(*s));
    if (!s) return NULL;
    xx_archive_record_state_init(&s->base, f);
    s->base.total_records = (int64_t)c->records;
    for (i = 0; options && i < options->count; i++) {
        const xx_meta *m = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        if (!m) continue;
        xx_meta_init(&copy, m->meta_id);
        if (!xx_var_copy(&copy.var, &m->var) || !xx_list_append(&s->base.options, &copy)) {
            xx_meta_cleanup(&copy);
            goto fail;
        }
    }
    s->base.has_record = sl_ne_activate(f, s, pd);
    if (s->base.has_record) return &s->base;
fail:
    if (s->active) xx_lha_free_archive_records_reading(&c->volumes[s->volume].archive.format, s->active);
    xx_archive_record_state_free(&s->base);
    return NULL;
}
static bool sl_ne_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sl_ne_records *s = (sl_ne_records *)state;
    sl_ne_context *c = (sl_ne_context *)f->priv;
    sl_ne_volume *v;
    if (!c || !s || state->format != f || !state->has_record || !s->active || sfx_lha_stop(pd)) return false;
    v = &c->volumes[s->volume];
    if (xx_lha_archive_record_move_to_next(&v->archive.format, s->active, pd)) {
        ++state->current_index;
        state->has_record = sl_ne_publish(f, s);
        return state->has_record;
    }
    if ((uint64_t)(s->active->current_index + 1) < v->records) {
        state->has_record = false;
        return false;
    }
    xx_lha_free_archive_records_reading(&v->archive.format, s->active);
    s->active = NULL;
    if (++s->volume >= c->count) {
        state->has_record = false;
        return false;
    }
    state->has_record = sl_ne_activate(f, s, pd);
    if (state->has_record) ++state->current_index;
    return state->has_record;
}
static bool sl_ne_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sl_ne_context *c = (sl_ne_context *)f->priv;
    sl_ne_records *s = (sl_ne_records *)state;
    if (!c || !state || state->format != f || !state->has_record || !s->active || s->volume >= c->count) return false;
    return xx_lha_unpack_current_archive_record(&c->volumes[s->volume].archive.format, s->active, pd);
}
static void sl_ne_free_records(Abstractformat *f, xx_archive_record_state *state)
{
    sl_ne_context *c = (sl_ne_context *)f->priv;
    sl_ne_records *s = (sl_ne_records *)state;
    if (!s) return;
    if (s->active && c && s->volume < c->count) xx_lha_free_archive_records_reading(&c->volumes[s->volume].archive.format, s->active);
    xx_archive_record_state_free(state);
}
#endif
