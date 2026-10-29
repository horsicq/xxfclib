/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Bounded DH0902 32-byte records with standalone PKWARE DCL payloads.
 * Framing was corroborated on an actual archive through U3 and unchanged
 * Mark Adler blast. Unknown header words are not checksum authority.
 */
#include "xxfclib/formats/archive/dh/xx_dh.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define DH_MAX_SOURCE (UINT64_C(64) * 1024U * 1024U)
#define DH_MAX_MEMBER (UINT64_C(16) * 1024U * 1024U)
#define DH_MAX_TOTAL (UINT64_C(64) * 1024U * 1024U)
#define DH_MAX_RECORDS 4096U
#define DH_OVERHEAD UINT64_C(131072)
#define DH_OPTION_BYTES 8192U

typedef struct dh_info { size_t input_size, count, max_raw, max_charge; uint64_t total_raw; } dh_info;
typedef struct dh_entry { size_t header, payload, end, packed, raw; char name[15]; } dh_entry;
typedef struct dh_cursor { dh_info info; dh_entry entry; } dh_cursor;
static uint64_t dh_budget(Abstractformat *f, const xx_list_s *options, uint32_t id);
static bool dh_options_valid(const xx_list_s *options);
static uint32_t dh_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U); }
static bool dh_read_at(Abstractformat *f, int64_t at, void *buffer, size_t n, xx_pd_struct *pd)
{
    int64_t saved;
    size_t done = 0U;
    bool ok;
    if (!f || !f->device || at < 0 || (!buffer && n)) return false;
    saved = xx_io_tell(f->device);
    if (saved < 0) return false;
    ok = !(pd && xx_pd_is_stopped(pd)) && xx_io_seek64(f->device, at, SEEK_SET) == 0;
    while (ok && done < n) {
        size_t step = n - done;
        ssize_t got;
        if (pd && xx_pd_is_stopped(pd)) { ok = false; break; }
        if (step > 65536U) step = 65536U;
        got = xx_io_read(f->device, (uint8_t *)buffer + done, step);
        if (got <= 0 || (size_t)got > step) ok = false;
        else done += (size_t)got;
    }
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0) ok = false;
    return ok && !(pd && xx_pd_is_stopped(pd));
}


static char dh_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static bool dh_safe_name(char *name, const uint8_t *field)
{
    size_t n = 0U, stem = 0U;
    char lower[15];
    while (n < 15U && field[n]) ++n;
    if (n == 0U) return false;
    if (field[n - 1U] == '\n') { if (n < 2U) return false; --n; }
    else if (n == 15U) return false; /* A plain name requires NUL inside this bounded field. */
    for (size_t i = 0U; i < n; ++i) {
        unsigned c = field[i];
        if (c <= 32U || c >= 127U || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
        name[i] = (char)c; lower[i] = dh_lower((char)c);
    }
    name[n] = lower[n] = 0;
    if (name[n - 1U] == '.' || (n == 1U && name[0] == '.') || (n == 2U && name[0] == '.' && name[1] == '.')) return false;
    while (stem < n && lower[stem] != '.') ++stem;
    if (stem == 3U && (!xx_rt_memcmp(lower, "con", 3U) || !xx_rt_memcmp(lower, "prn", 3U) || !xx_rt_memcmp(lower, "aux", 3U) || !xx_rt_memcmp(lower, "nul", 3U))) return false;
    if (stem == 4U && (!xx_rt_memcmp(lower, "com", 3U) || !xx_rt_memcmp(lower, "lpt", 3U)) && lower[3] >= '1' && lower[3] <= '9') return false;
    return true;
}

static bool dh_load_entry(Abstractformat *f, size_t input_size, size_t at, dh_entry *entry, xx_pd_struct *pd)
{
    uint8_t h[34];
    if (at > input_size || input_size - at < sizeof(h) || !dh_read_at(f, f->base_address + (int64_t)at, h, sizeof(h), pd) ||
        xx_rt_memcmp(h, "DH\x09\x02", 4U) || !dh_safe_name(entry->name, h + 4U)) return false;
    entry->packed = dh_u32(h + 20U); entry->raw = dh_u32(h + 24U);
    if (entry->packed < 2U || entry->packed > DH_MAX_MEMBER || !entry->raw || entry->raw > DH_MAX_MEMBER ||
        entry->packed > input_size - at - 32U || h[32] > 1U || h[33] < 4U || h[33] > 6U) return false;
    entry->header = at; entry->payload = at + 32U; entry->end = entry->payload + entry->packed;
    return true;
}

/* Fixed bounded catalog workspace also prevents case-folded output collisions.
 * No dictionary/output allocation or payload decompression occurs here. */
static bool dh_parse(Abstractformat *f, dh_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    char (*names)[15] = NULL;
    int64_t total;
    size_t at = 0U;
    bool ok = false;
    if (!f || !info || !f->device || f->base_address < 0 || (pd && xx_pd_is_stopped(pd)) ||
        !dh_options_valid(options) || dh_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT) < DH_OVERHEAD) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address || total - f->base_address < 34 || (uint64_t)(total - f->base_address) > DH_MAX_SOURCE) return false;
    names = (char (*)[15])xx_mem_alloc(DH_MAX_RECORDS * sizeof(*names));
    if (!names) return false;
    xx_rt_memset(info, 0, sizeof(*info)); info->input_size = (size_t)(total - f->base_address);
    while (at < info->input_size) {
        dh_entry entry;
        char lower[15] = {0};
        if (info->count == DH_MAX_RECORDS || !dh_load_entry(f, info->input_size, at, &entry, pd)) goto done;
        for (size_t i = 0U;; ++i) { lower[i] = dh_lower(entry.name[i]); if (!entry.name[i]) break; }
        for (size_t i = 0U; i < info->count; ++i) if (xx_str_cmp(names[i], lower) == 0) goto done;
        xx_rt_memcpy(names[info->count], lower, sizeof(lower));
        ++info->count; info->total_raw += entry.raw;
        if (info->total_raw > DH_MAX_TOTAL) goto done;
        if (entry.raw > info->max_raw) info->max_raw = entry.raw;
        if (entry.raw + entry.packed > info->max_charge) info->max_charge = entry.raw + entry.packed;
        at = entry.end;
    }
    ok = info->count != 0U && at == info->input_size;
done:
    xx_mem_free(names); return ok;
}
static const xx_var *dh_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}

static uint64_t dh_budget(Abstractformat *f, const xx_list_s *options, uint32_t id)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, id);
    uint64_t limit = v ? xx_var_get_u64(v) : UINT64_MAX;
    size_t i;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id && xx_var_get_u64(&m->var) < limit) limit = xx_var_get_u64(&m->var);
    }
    return limit;
}

static bool dh_options_valid(const xx_list_s *options)
{
    size_t i, bytes = 0U;
    if (!options) return true;
    if (options->count > 64U) return false;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        size_t n;
        if (!m) return false;
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            n = m->var.val.str.len;
            if ((!m->var.val.str.ptr && n) || bytes >= DH_OPTION_BYTES || n >= DH_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            n = m->var.val.wstr.len;
            if ((!m->var.val.wstr.ptr && n) || (DH_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DH_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
        } else if (m->var.type >= XX_VAR_TYPE_STRING) return false;
    }
    return true;
}

static bool dh_ready(Abstractformat *f, const dh_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    return !(pd && xx_pd_is_stopped(pd)) && dh_options_valid(options) &&
        (uint64_t)info->max_raw <= dh_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
        (uint64_t)info->max_charge + DH_OVERHEAD <= dh_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

/* Bounded owned options keep the 128KiB overhead charge conservative even
 * during wide-path conversion. Views are cloned, never borrowed. */
static bool dh_copy_options(xx_list_s *to, const xx_list_s *from)
{
    size_t i, bytes = 0U;
    if (!from) return true;
    if (!dh_options_valid(from)) return false;
    for (i = 0U; i < from->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)from, i);
        xx_meta copy;
        bool ok = false;
        if (!m) return false;
        xx_meta_init(&copy, m->meta_id);
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t n = m->var.val.str.len;
            char *s;
            if (bytes >= DH_OPTION_BYTES || n >= DH_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
            s = (char *)xx_mem_alloc(n + 1U);
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.str.ptr, n); s[n] = 0; ok = xx_var_set_str_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t n = m->var.val.wstr.len;
            wchar_t *s;
            if ((DH_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DH_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
            s = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.wstr.ptr, n * sizeof(wchar_t)); s[n] = 0; ok = xx_var_set_wstr_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type < XX_VAR_TYPE_STRING) ok = xx_var_copy(&copy.var, &m->var);
        if (!ok || !xx_list_append(to, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}

/* Iterator options are public and may be appended after catalog creation.
 * Consume views by their declared length, never by searching for a NUL. */
static char *dh_owned_path(const xx_var *v)
{
    size_t n;
    if (!v) return NULL;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        char *s;
        n = v->val.str.len;
        if (n >= DH_OPTION_BYTES || (!v->val.str.ptr && n)) return NULL;
        s = (char *)xx_mem_alloc(n + 1U);
        if (s) { if (n) xx_rt_memcpy(s, v->val.str.ptr, n); s[n] = 0; }
        return s;
    }
    if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wchar_t *wide;
        char *s;
        n = v->val.wstr.len;
        if (n >= DH_OPTION_BYTES / sizeof(wchar_t) || (!v->val.wstr.ptr && n)) return NULL;
        wide = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
        if (!wide) return NULL;
        if (n) xx_rt_memcpy(wide, v->val.wstr.ptr, n * sizeof(wchar_t));
        wide[n] = 0; s = xx_str_unicode_to_utf8(wide); xx_mem_free(wide); return s;
    }
    return NULL;
}


static bool dh_decode(Abstractformat *f, const dh_cursor *cursor, const xx_list_s *options, uint8_t **result, xx_pd_struct *pd)
{
    const dh_entry *entry = &cursor->entry;
    uint8_t *input = NULL, *output = NULL;
    size_t consumed = 0U, produced = 0U, written = 0U;
    bool ok = false;
    *result = NULL;
    if (!dh_ready(f, &cursor->info, options, pd)) return false;
    input = (uint8_t *)xx_mem_alloc(entry->packed); output = (uint8_t *)xx_mem_alloc(entry->raw);
    if (!input || !output || !dh_ready(f, &cursor->info, options, pd) ||
        !dh_read_at(f, f->base_address + (int64_t)entry->payload, input, entry->packed, pd)) goto done;
    if (!xx_dcl_scan_memory(input, entry->packed, entry->raw, &consumed, &produced) || consumed != entry->packed || produced != entry->raw ||
        !dh_ready(f, &cursor->info, options, pd) || !xx_dcl_decode_memory(input, entry->packed, output, entry->raw, &written) || written != entry->raw ||
        !dh_ready(f, &cursor->info, options, pd)) goto done;
    *result = output; output = NULL; ok = true;
done:
    xx_mem_free(input); xx_mem_free(output); return ok;
}

static bool dh_record(Abstractformat *f, xx_archive_record_state *s, dh_cursor *cursor)
{
    xx_archive_record *r = &s->current_record;
    const dh_entry *e = &cursor->entry;
    r->header_offset = f->base_address + (int64_t)e->header; r->header_size = 32;
    r->data_offset = f->base_address + (int64_t)e->payload; r->compressed_size = (int64_t)e->packed;
    return xx_archive_record_set_original_name(r, e->name) &&
        xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, e->packed) &&
        xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, e->raw) &&
        xx_archive_record_set_meta_str(r, XX_META_ID_COMPRESSION_METHOD, "PKWARE DCL") &&
        xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false);
}

void xx_dh_init(xx_dh *a, xx_io_device *device, int64_t base)
{
    if (!a) return;
    xx_rt_memset(a, 0, sizeof(*a)); xx_format_init(&a->format, device, base);
    a->format.endian = XX_ENDIAN_LITTLE; a->format.file_type = XX_FILE_TYPE_DH;
    a->format.format_type = XX_TYPE_ARCHIVE; a->format.is_archive = true;
    xx_format_set_mime_type(&a->format, "application/x-dh"); xx_format_set_extension(&a->format, "dh");
    a->format.check_is_valid = xx_dh_check_is_valid;
    a->format.handle_base_info = xx_dh_handle_base_info;
    a->format.get_format_size = xx_dh_get_format_size;
    a->format.get_number_of_archive_records = xx_dh_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_dh_create_archive_records_reading;
    a->format.get_current_archive_record = xx_dh_get_current_archive_record;
    a->format.unpack_current_archive_record = xx_dh_unpack_current_archive_record;
    a->format.archive_record_move_to_next = xx_dh_archive_record_move_to_next;
    a->format.free_archive_records_reading = xx_dh_free_archive_records_reading;
    a->archive_end = -1;
}
xx_dh *xx_dh_create(xx_io_device *device, int64_t base)
{ xx_dh *a = (xx_dh *)xx_mem_alloc(sizeof(*a)); if (a) xx_dh_init(a, device, base); return a; }
void xx_dh_destroy(xx_dh *a) { if (a) xx_format_cleanup_extra_parameters(&a->format); }
void xx_dh_free(xx_dh *a) { if (a) { xx_dh_destroy(a); xx_mem_free(a); } }
bool xx_dh_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { dh_info info; return dh_parse(f, &info, NULL, pd); }
bool xx_dh_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    dh_info info; xx_dh *a = (xx_dh *)f;
    if (!dh_parse(f, &info, NULL, pd)) { if (f) { f->is_valid = false; f->base_info_handled = false; f->format_size = -1; f->number_of_archive_records = 0U; } return false; }
    a->number_of_records = info.count; a->archive_end = f->base_address + (int64_t)info.input_size;
    f->format_size = (int64_t)info.input_size; f->number_of_archive_records = info.count;
    f->overlay_offset = -1; f->overlay_size = 0; f->is_valid = true; f->base_info_handled = true; return true;
}
int64_t xx_dh_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{ return f && (f->base_info_handled || xx_dh_handle_base_info(f, pd)) ? f->format_size : -1; }
uint64_t xx_dh_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{ return f && (f->base_info_handled || xx_dh_handle_base_info(f, pd)) ? f->number_of_archive_records : 0U; }

xx_archive_record_state *xx_dh_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    dh_info info; dh_cursor *cursor; xx_archive_record_state *s;
    if (!dh_parse(f, &info, options, pd) || !dh_ready(f, &info, options, pd)) return NULL;
    cursor = (dh_cursor *)xx_mem_alloc(sizeof(*cursor)); s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!cursor || !s) { xx_mem_free(cursor); xx_mem_free(s); return NULL; }
    cursor->info = info; xx_archive_record_state_init(s, f); s->internal_state = cursor; s->free_internal = xx_mem_free; s->total_records = (int64_t)info.count;
    if (!dh_copy_options(&s->options, options) || !dh_load_entry(f, info.input_size, 0U, &cursor->entry, pd) || !dh_record(f, s, cursor)) {
        xx_archive_record_state_free(s); return NULL;
    }
    s->has_record = true; return s;
}
const xx_archive_record *xx_dh_get_current_archive_record(Abstractformat *f, xx_archive_record_state *s)
{ return f && s && s->format == f && s->has_record ? &s->current_record : NULL; }
bool xx_dh_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    dh_cursor *cursor;
    if (!f || !s || s->format != f || !s->has_record || !(cursor = (dh_cursor *)s->internal_state) || !dh_ready(f, &cursor->info, &s->options, pd)) return false;
    size_t next = cursor->entry.end; ++s->current_index; s->has_record = false;
    xx_archive_record_cleanup(&s->current_record); xx_archive_record_init(&s->current_record);
    if (next == cursor->info.input_size || s->current_index >= (int64_t)cursor->info.count ||
        !dh_load_entry(f, cursor->info.input_size, next, &cursor->entry, pd) || !dh_record(f, s, cursor)) return false;
    s->has_record = true; return true;
}
bool xx_dh_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const dh_cursor *cursor; const xx_var *option, *overwrite;
    uint8_t *plain = NULL; char *base = NULL, *path = NULL; xx_io_device *out = NULL;
    size_t done = 0U; bool ok = false, created = false;
    if (!f || !s || s->format != f || !s->has_record || !(cursor = (const dh_cursor *)s->internal_state) || !dh_decode(f, cursor, &s->options, &plain, pd)) return false;
    option = dh_option(&s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) { xx_mem_free(plain); return true; }
    base = dh_owned_path(option);
    if (!base || !dh_ready(f, &cursor->info, &s->options, pd)) goto done;
    path = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat3(base, "/", cursor->entry.name) : xx_str_concat(base, cursor->entry.name);
    overwrite = dh_option(&s->options, XX_META_ID_OPT_OVERWRITE);
    if (!overwrite) overwrite = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_OVERWRITE);
    if (!path || (xx_io_file_exists_a(path) && (!overwrite || !xx_var_get_bool(overwrite))) || !dh_ready(f, &cursor->info, &s->options, pd) ||
        !xx_store_create_dirs_a(path, false) || !dh_ready(f, &cursor->info, &s->options, pd)) goto done;
    out = xx_io_file_open(path, "wb"); created = out != NULL; ok = created;
    while (ok && done < cursor->entry.raw) {
        size_t step = cursor->entry.raw - done; ssize_t sent;
        if (!dh_ready(f, &cursor->info, &s->options, pd)) { ok = false; break; }
        if (step > 65536U) step = 65536U;
        sent = xx_io_write(out, plain + done, step);
        if (sent <= 0 || (size_t)sent > step) ok = false; else done += (size_t)sent;
    }
    if (!dh_ready(f, &cursor->info, &s->options, pd)) ok = false;
    if (out && xx_io_close(out) != 0) ok = false;
    out = NULL;
done:
    if (out) xx_io_close(out);
    if (!ok && created) xx_rt_remove(path);
    xx_mem_free(plain); xx_str_free(base); xx_str_free(path); return ok;
}
void xx_dh_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s)
{ (void)f; xx_archive_record_state_free(s); }
