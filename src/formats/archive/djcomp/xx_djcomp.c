/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Bounded DJcomp 32-byte framing and strict GIF-family LSB LZW. Header bytes
 * 21..31 remain opaque. No producer raw-size or checksum interpretation.
 */
#include "xxfclib/formats/archive/djcomp/xx_djcomp.h"
#include "xxfclib/algo/softronics/xx_softronics.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define DJCOMP_MAX_PACKED (UINT64_C(16) * 1024U * 1024U)
#define DJCOMP_MAX_MEMBER (UINT64_C(16) * 1024U * 1024U)
#define DJCOMP_WORK_LIMIT (UINT64_C(64) * 1024U * 1024U)
#define DJCOMP_OVERHEAD UINT64_C(131072)
#define DJCOMP_OPTION_BYTES 8192U

typedef struct djcomp_info {
    size_t input_size, count, max_raw, max_charge;
    uint64_t total_raw;
    uint8_t header[32];
    char name[13];
} djcomp_info;
typedef struct djcomp_entry { size_t header, payload, end, packed, raw; char name[13]; } djcomp_entry;
typedef struct djcomp_cursor { djcomp_info info; djcomp_entry entry; } djcomp_cursor;
static uint64_t djcomp_budget(Abstractformat *f, const xx_list_s *options, uint32_t id);
static bool djcomp_options_valid(const xx_list_s *options);
static bool djcomp_poll(void *context) { return !context || !xx_pd_is_stopped((xx_pd_struct *)context); }
static bool djcomp_read_at(Abstractformat *f, int64_t at, void *buffer, size_t n, xx_pd_struct *pd)
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


static char djcomp_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static bool djcomp_safe_name(char *name, const uint8_t *field)
{
    size_t n = 0U, stem = 0U;
    char lower[13];
    if (field[12] != 0U) return false;
    while (n < 12U && field[n]) ++n;
    /* Explicit output-name normalization; the full raw header is retained. */
    while (n && field[n - 1U] == 0x20U) --n;
    if (!n) return false;
    for (size_t i = 0U; i < n; ++i) {
        unsigned c = field[i];
        if (c <= 32U || c >= 127U || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
        name[i] = (char)c; lower[i] = djcomp_lower((char)c);
    }
    name[n] = lower[n] = 0;
    if (name[n - 1U] == '.') return false;
    while (stem < n && lower[stem] != '.') ++stem;
    if (stem == 3U && (!xx_rt_memcmp(lower, "con", 3U) || !xx_rt_memcmp(lower, "prn", 3U) || !xx_rt_memcmp(lower, "aux", 3U) || !xx_rt_memcmp(lower, "nul", 3U))) return false;
    if (stem == 4U && (!xx_rt_memcmp(lower, "com", 3U) || !xx_rt_memcmp(lower, "lpt", 3U)) && lower[3] >= '1' && lower[3] <= '9') return false;
    return true;
}

/* The shared dictionary parser measures validated phrases into a counter,
 * reserving bounded scratch but no full plaintext allocation in this pass. */
static bool djcomp_parse(Abstractformat *f, djcomp_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    int64_t total;
    uint64_t memory, cap, member;
    size_t packed, raw = 0U;
    uint8_t *input = NULL;
    bool ok = false;
    if (!f || !info || !f->device || f->base_address < 0 || !djcomp_poll(pd) || !djcomp_options_valid(options)) return false;
    memory = djcomp_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
    total = xx_io_total_size(f->device);
    if (total < f->base_address || total - f->base_address <= 32 || (uint64_t)(total - f->base_address - 32) > DJCOMP_MAX_PACKED) return false;
    packed = (size_t)(total - f->base_address - 32);
    if (memory <= DJCOMP_OVERHEAD || packed >= memory - DJCOMP_OVERHEAD) return false;
    xx_rt_memset(info, 0, sizeof(*info));
    if (!djcomp_read_at(f, f->base_address, info->header, 32U, pd) || xx_rt_memcmp(info->header, "DJcomp\0\0", 8U) ||
        !djcomp_safe_name(info->name, info->header + 8U)) return false;
    cap = memory - DJCOMP_OVERHEAD - packed;
    member = djcomp_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (cap > member) cap = member;
    if (cap > DJCOMP_MAX_MEMBER) cap = DJCOMP_MAX_MEMBER;
    if (!cap) return false;
    input = (uint8_t *)xx_mem_alloc(packed);
    if (!input || !djcomp_read_at(f, f->base_address + 32, input, packed, pd)) goto done;
    if (!xx_softronics_lzw_measure_memory_exact_bounded(input, packed, (size_t)cap, DJCOMP_WORK_LIMIT, djcomp_poll, pd, &raw) || !raw ||
        !djcomp_poll(pd) || !djcomp_options_valid(options) || raw > djcomp_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) ||
        (uint64_t)raw + packed + DJCOMP_OVERHEAD > djcomp_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT)) goto done;
    info->input_size = packed + 32U; info->count = 1U; info->max_raw = raw; info->max_charge = packed + raw; info->total_raw = raw;
    ok = true;
done:
    xx_mem_free(input); return ok;
}
static const xx_var *djcomp_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}

static uint64_t djcomp_budget(Abstractformat *f, const xx_list_s *options, uint32_t id)
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

static bool djcomp_options_valid(const xx_list_s *options)
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
            if ((!m->var.val.str.ptr && n) || bytes >= DJCOMP_OPTION_BYTES || n >= DJCOMP_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            n = m->var.val.wstr.len;
            if ((!m->var.val.wstr.ptr && n) || (DJCOMP_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DJCOMP_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
        } else if (m->var.type >= XX_VAR_TYPE_STRING) return false;
    }
    return true;
}

static bool djcomp_ready(Abstractformat *f, const djcomp_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    return !(pd && xx_pd_is_stopped(pd)) && djcomp_options_valid(options) &&
        (uint64_t)info->max_raw <= djcomp_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
        (uint64_t)info->max_charge + DJCOMP_OVERHEAD <= djcomp_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

/* Bounded owned options keep the 128KiB overhead charge conservative even
 * during wide-path conversion. Views are cloned, never borrowed. */
static bool djcomp_copy_options(xx_list_s *to, const xx_list_s *from)
{
    size_t i, bytes = 0U;
    if (!from) return true;
    if (!djcomp_options_valid(from)) return false;
    for (i = 0U; i < from->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)from, i);
        xx_meta copy;
        bool ok = false;
        if (!m) return false;
        xx_meta_init(&copy, m->meta_id);
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t n = m->var.val.str.len;
            char *s;
            if (bytes >= DJCOMP_OPTION_BYTES || n >= DJCOMP_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
            s = (char *)xx_mem_alloc(n + 1U);
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.str.ptr, n); s[n] = 0; ok = xx_var_set_str_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t n = m->var.val.wstr.len;
            wchar_t *s;
            if ((DJCOMP_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DJCOMP_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
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
static char *djcomp_owned_path(const xx_var *v)
{
    size_t n;
    if (!v) return NULL;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        char *s;
        n = v->val.str.len;
        if (n >= DJCOMP_OPTION_BYTES || (!v->val.str.ptr && n)) return NULL;
        s = (char *)xx_mem_alloc(n + 1U);
        if (s) { if (n) xx_rt_memcpy(s, v->val.str.ptr, n); s[n] = 0; }
        return s;
    }
    if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wchar_t *wide;
        char *s;
        n = v->val.wstr.len;
        if (n >= DJCOMP_OPTION_BYTES / sizeof(wchar_t) || (!v->val.wstr.ptr && n)) return NULL;
        wide = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
        if (!wide) return NULL;
        if (n) xx_rt_memcpy(wide, v->val.wstr.ptr, n * sizeof(wchar_t));
        wide[n] = 0; s = xx_str_unicode_to_utf8(wide); xx_mem_free(wide); return s;
    }
    return NULL;
}


static bool djcomp_decode(Abstractformat *f, const djcomp_cursor *cursor, const xx_list_s *options, uint8_t **result, xx_pd_struct *pd)
{
    const djcomp_entry *entry = &cursor->entry;
    uint8_t h[32], *input = NULL, *output = NULL;
    int64_t total;
    bool ok = false;
    *result = NULL;
    if (!djcomp_ready(f, &cursor->info, options, pd)) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address || (uint64_t)(total - f->base_address) != cursor->info.input_size ||
        !djcomp_read_at(f, f->base_address, h, sizeof(h), pd) || xx_rt_memcmp(h, cursor->info.header, sizeof(h))) return false;
    input = (uint8_t *)xx_mem_alloc(entry->packed); output = (uint8_t *)xx_mem_alloc(entry->raw);
    if (!input || !output || !djcomp_ready(f, &cursor->info, options, pd) ||
        !djcomp_read_at(f, f->base_address + 32, input, entry->packed, pd) ||
        !xx_softronics_lzw_decompress_memory_exact_bounded(input, entry->packed, output, entry->raw, DJCOMP_WORK_LIMIT, djcomp_poll, pd) ||
        !djcomp_ready(f, &cursor->info, options, pd)) goto done;
    *result = output; output = NULL; ok = true;
done:
    xx_mem_free(input); xx_mem_free(output); return ok;
}

static bool djcomp_record(Abstractformat *f, xx_archive_record_state *s, djcomp_cursor *cursor)
{
    xx_archive_record *r = &s->current_record;
    const djcomp_entry *e = &cursor->entry;
    r->header_offset = f->base_address + (int64_t)e->header; r->header_size = 32;
    r->data_offset = f->base_address + (int64_t)e->payload; r->compressed_size = (int64_t)e->packed;
    return xx_archive_record_set_original_name(r, e->name) &&
        xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, e->packed) &&
        xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, e->raw) &&
        xx_archive_record_set_meta_str(r, XX_META_ID_COMPRESSION_METHOD, "LZW LSB 9-12 CLEAR/END") &&
        xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false);
}

void xx_djcomp_init(xx_djcomp *a, xx_io_device *device, int64_t base)
{
    if (!a) return;
    xx_rt_memset(a, 0, sizeof(*a)); xx_format_init(&a->format, device, base);
    a->format.endian = XX_ENDIAN_LITTLE; a->format.file_type = XX_FILE_TYPE_DJCOMP;
    a->format.format_type = XX_TYPE_ARCHIVE; a->format.is_archive = true;
    xx_format_set_mime_type(&a->format, "application/x-djcomp");
    a->format.check_is_valid = xx_djcomp_check_is_valid;
    a->format.handle_base_info = xx_djcomp_handle_base_info;
    a->format.get_format_size = xx_djcomp_get_format_size;
    a->format.get_number_of_archive_records = xx_djcomp_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_djcomp_create_archive_records_reading;
    a->format.get_current_archive_record = xx_djcomp_get_current_archive_record;
    a->format.unpack_current_archive_record = xx_djcomp_unpack_current_archive_record;
    a->format.archive_record_move_to_next = xx_djcomp_archive_record_move_to_next;
    a->format.free_archive_records_reading = xx_djcomp_free_archive_records_reading;
    a->archive_end = -1;
}
xx_djcomp *xx_djcomp_create(xx_io_device *device, int64_t base)
{ xx_djcomp *a = (xx_djcomp *)xx_mem_alloc(sizeof(*a)); if (a) xx_djcomp_init(a, device, base); return a; }
void xx_djcomp_destroy(xx_djcomp *a) { if (a) xx_format_cleanup_extra_parameters(&a->format); }
void xx_djcomp_free(xx_djcomp *a) { if (a) { xx_djcomp_destroy(a); xx_mem_free(a); } }
bool xx_djcomp_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { djcomp_info info; return djcomp_parse(f, &info, NULL, pd); }
bool xx_djcomp_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    djcomp_info info; xx_djcomp *a = (xx_djcomp *)f;
    if (!djcomp_parse(f, &info, NULL, pd)) { if (f) { f->is_valid = false; f->base_info_handled = false; f->format_size = -1; f->number_of_archive_records = 0U; } return false; }
    a->number_of_records = info.count; a->archive_end = f->base_address + (int64_t)info.input_size;
    f->format_size = (int64_t)info.input_size; f->number_of_archive_records = info.count;
    f->overlay_offset = -1; f->overlay_size = 0; f->is_valid = true; f->base_info_handled = true; return true;
}
int64_t xx_djcomp_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{ return f && (f->base_info_handled || xx_djcomp_handle_base_info(f, pd)) ? f->format_size : -1; }
uint64_t xx_djcomp_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{ return f && (f->base_info_handled || xx_djcomp_handle_base_info(f, pd)) ? f->number_of_archive_records : 0U; }

xx_archive_record_state *xx_djcomp_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    djcomp_info info; djcomp_cursor *cursor; xx_archive_record_state *s;
    if (!djcomp_parse(f, &info, options, pd) || !djcomp_ready(f, &info, options, pd)) return NULL;
    cursor = (djcomp_cursor *)xx_mem_alloc(sizeof(*cursor)); s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!cursor || !s) { xx_mem_free(cursor); xx_mem_free(s); return NULL; }
    cursor->info = info; xx_archive_record_state_init(s, f); s->internal_state = cursor; s->free_internal = xx_mem_free; s->total_records = (int64_t)info.count;
    cursor->info = info;
    cursor->entry.header = 0U; cursor->entry.payload = 32U; cursor->entry.end = info.input_size;
    cursor->entry.packed = info.input_size - 32U; cursor->entry.raw = info.max_raw;
    xx_rt_memcpy(cursor->entry.name, info.name, sizeof(info.name));
    if (!djcomp_copy_options(&s->options, options) || !djcomp_record(f, s, cursor)) {
        xx_archive_record_state_free(s); return NULL;
    }
    s->has_record = true; return s;
}
const xx_archive_record *xx_djcomp_get_current_archive_record(Abstractformat *f, xx_archive_record_state *s)
{ return f && s && s->format == f && s->has_record ? &s->current_record : NULL; }
bool xx_djcomp_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    djcomp_cursor *cursor;
    if (!f || !s || s->format != f || !s->has_record || !(cursor = (djcomp_cursor *)s->internal_state) || !djcomp_ready(f, &cursor->info, &s->options, pd)) return false;
    ++s->current_index; s->has_record = false;
    xx_archive_record_cleanup(&s->current_record); xx_archive_record_init(&s->current_record);
    return false;
}
bool xx_djcomp_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const djcomp_cursor *cursor; const xx_var *option, *overwrite;
    uint8_t *plain = NULL; char *base = NULL, *path = NULL; xx_io_device *out = NULL;
    size_t done = 0U; bool ok = false, created = false;
    if (!f || !s || s->format != f || !s->has_record || !(cursor = (const djcomp_cursor *)s->internal_state) || !djcomp_decode(f, cursor, &s->options, &plain, pd)) return false;
    option = djcomp_option(&s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) { xx_mem_free(plain); return true; }
    base = djcomp_owned_path(option);
    if (!base || !djcomp_ready(f, &cursor->info, &s->options, pd)) goto done;
    path = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat3(base, "/", cursor->entry.name) : xx_str_concat(base, cursor->entry.name);
    overwrite = djcomp_option(&s->options, XX_META_ID_OPT_OVERWRITE);
    if (!overwrite) overwrite = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_OVERWRITE);
    if (!path || (xx_io_file_exists_a(path) && (!overwrite || !xx_var_get_bool(overwrite))) || !djcomp_ready(f, &cursor->info, &s->options, pd) ||
        !xx_store_create_dirs_a(path, false) || !djcomp_ready(f, &cursor->info, &s->options, pd)) goto done;
    out = xx_io_file_open(path, "wb"); created = out != NULL; ok = created;
    while (ok && done < cursor->entry.raw) {
        size_t step = cursor->entry.raw - done; ssize_t sent;
        if (!djcomp_ready(f, &cursor->info, &s->options, pd)) { ok = false; break; }
        if (step > 65536U) step = 65536U;
        sent = xx_io_write(out, plain + done, step);
        if (sent <= 0 || (size_t)sent > step) ok = false; else done += (size_t)sent;
    }
    if (!djcomp_ready(f, &cursor->info, &s->options, pd)) ok = false;
    if (out && xx_io_close(out) != 0) ok = false;
    out = NULL;
done:
    if (out) xx_io_close(out);
    if (!ok && created) xx_rt_remove(path);
    xx_mem_free(plain); xx_str_free(base); xx_str_free(path); return ok;
}
void xx_djcomp_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s)
{ (void)f; xx_archive_record_state_free(s); }
