/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * DFSee framing and LZW dialect are documented by Jan van Wijk's MIT-licensed
 * DFSee 17.1 sources (dfsimage.c, dfsimzci.c, dfsulzw.c). This bounded reader
 * requires physical END, defined dictionary codes, the encoder's three-zero-
 * code flush, exact sector count, and original CRC32s for LZW and RLE units.
 * SMART records omit original unused contents; disk.img represents them as
 * zeros, matching DFSee's virtual-image reader. Their fixed marker is not CRC
 * authentication of the omitted bytes. Unsupported variants fail closed.
 */
#include "xxfclib/formats/filesystem/dfsee_imz/xx_dfsee_imz.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define DFSEE_MAX_SIZE (UINT64_C(256) * 1024U * 1024U)
#define DFSEE_OVERHEAD UINT64_C(65536)
#define DFSEE_OPTION_BYTES 8192U
#define DFSEE_NAME "disk.img"

typedef struct dfsee_info { size_t input_size, image_size; uint32_t capacity, source_offset; bool has_smart; } dfsee_info;

static uint32_t df_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U); }
static unsigned df_u16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8U); }

static bool df_read_at(Abstractformat *f, int64_t at, void *buffer, size_t n, xx_pd_struct *pd)
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

/* Catalog validation checks all physical unit extents; decoding authenticates
 * their contents later, before an output file is opened. */
static bool df_parse(Abstractformat *f, dfsee_info *info, xx_pd_struct *pd)
{
    uint8_t h[520], unit[8];
    int64_t total;
    size_t at, count = 0U;
    uint64_t image, minimum = 0U;
    if (!f || !info || !f->device || f->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address || total - f->base_address < 528 || (uint64_t)(total - f->base_address) > DFSEE_MAX_SIZE) return false;
    if (!df_read_at(f, f->base_address, h, sizeof(h), pd) || xx_rt_memcmp(h, "DFSeeImageFile #", 16U) != 0 ||
        df_u32(h + 448) != 1U || (df_u32(h + 456) != 0x100U && df_u32(h + 456) != 0x10U && df_u32(h + 456) != 0x30U) ||
        (df_u32(h + 476) != 0U && df_u32(h + 476) != 512U) || xx_rt_memcmp(h + 512, "Tpack#", 6U) != 0) return false;
    for (at = 480U; at < 512U; ++at) if (h[at]) return false;
    image = (uint64_t)df_u32(h + 460) * 512U;
    if (!image || image > DFSEE_MAX_SIZE) return false;
    info->input_size = (size_t)(total - f->base_address);
    info->image_size = (size_t)image;
    info->source_offset = df_u32(h + 452);
    info->capacity = df_u16(h + 518);
    info->has_smart = false;
    if (!info->capacity || info->capacity % 512U != 0U || df_u32(h + 464) != info->capacity) return false;
    at = 520U;
    while (at < info->input_size) {
        unsigned method, field;
        if (++count > (image + 511U) / 512U || info->input_size - at < sizeof(unit) ||
            !df_read_at(f, f->base_address + (int64_t)at, unit, sizeof(unit), pd)) return false;
        method = df_u16(unit); field = df_u16(unit + 2);
        if (!field || field > info->capacity) return false;
        at += sizeof(unit);
        if (method == 0x1cU) {
            if (field > info->input_size - at) return false;
            at += field; minimum += 512U;
        } else if ((method & 0xff00U) == 0xfc00U || method == 0xeefcU) {
            if (field % 512U != 0U || (field != info->capacity && at != info->input_size)) return false;
            minimum += field;
            if (method == 0xeefcU) info->has_smart = true;
        } else return false;
        if (minimum > image) return false;
    }
    return count != 0U && at == info->input_size;
}

static const xx_var *df_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}

static uint64_t df_budget(Abstractformat *f, const xx_list_s *options, uint32_t id)
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

static bool df_options_valid(const xx_list_s *options)
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
            if ((!m->var.val.str.ptr && n) || bytes >= DFSEE_OPTION_BYTES || n >= DFSEE_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            n = m->var.val.wstr.len;
            if ((!m->var.val.wstr.ptr && n) || (DFSEE_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DFSEE_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
        } else if (m->var.type >= XX_VAR_TYPE_STRING) return false;
    }
    return true;
}

static bool df_ready(Abstractformat *f, const dfsee_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    return !(pd && xx_pd_is_stopped(pd)) && df_options_valid(options) && (uint64_t)info->image_size <= df_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
        (uint64_t)info->input_size + info->image_size + DFSEE_OVERHEAD <= df_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

/* Bounded owned options keep the 64KiB overhead charge conservative even
 * during wide-path conversion. Views are cloned, never borrowed. */
static bool df_copy_options(xx_list_s *to, const xx_list_s *from)
{
    size_t i, bytes = 0U;
    if (!from) return true;
    if (!df_options_valid(from)) return false;
    for (i = 0U; i < from->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)from, i);
        xx_meta copy;
        bool ok = false;
        if (!m) return false;
        xx_meta_init(&copy, m->meta_id);
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t n = m->var.val.str.len;
            char *s;
            if (bytes >= DFSEE_OPTION_BYTES || n >= DFSEE_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
            s = (char *)xx_mem_alloc(n + 1U);
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.str.ptr, n); s[n] = 0; ok = xx_var_set_str_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t n = m->var.val.wstr.len;
            wchar_t *s;
            if ((DFSEE_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DFSEE_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
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
static char *df_owned_path(const xx_var *v)
{
    size_t n;
    if (!v) return NULL;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        char *s;
        n = v->val.str.len;
        if (n >= DFSEE_OPTION_BYTES || (!v->val.str.ptr && n)) return NULL;
        s = (char *)xx_mem_alloc(n + 1U);
        if (s) { if (n) xx_rt_memcpy(s, v->val.str.ptr, n); s[n] = 0; }
        return s;
    }
    if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wchar_t *wide;
        char *s;
        n = v->val.wstr.len;
        if (n >= DFSEE_OPTION_BYTES / sizeof(wchar_t) || (!v->val.wstr.ptr && n)) return NULL;
        wide = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
        if (!wide) return NULL;
        if (n) xx_rt_memcpy(wide, v->val.wstr.ptr, n * sizeof(wchar_t));
        wide[n] = 0; s = xx_str_unicode_to_utf8(wide); xx_mem_free(wide); return s;
    }
    return NULL;
}

static bool df_bits(const uint8_t *p, size_t n, size_t *bit, unsigned width, unsigned *code)
{
    unsigned v = 0U, j;
    if (*bit > n * 8U || width > n * 8U - *bit) return false;
    for (j = 0U; j < width; ++j, ++*bit) v = (v << 1U) | ((p[*bit / 8U] >> (7U - *bit % 8U)) & 1U);
    *code = v;
    return true;
}

static bool df_lzw(Abstractformat *f, const dfsee_info *info, const xx_list_s *options, const uint8_t *p, size_t n,
                   uint8_t *out, size_t cap, size_t *written, xx_pd_struct *pd)
{
    uint16_t prefix[4096];
    uint8_t suffix[4096], stack[4096];
    unsigned width = 9U, maximum = 511U, next = 258U, previous = 0U;
    size_t bit = 0U, produced = 0U, work = 0U;
    bool have_previous = false;
    *written = 0U;
    for (;;) {
        unsigned code, walk, first;
        size_t used = 0U;
        if ((work++ & 4095U) == 0U && !df_ready(f, info, options, pd)) return false;
        if (!df_bits(p, n, &bit, width, &code)) return false;
        if (code == 257U) {
            size_t tail = n * 8U - bit;
            size_t expected = 8U * ((bit + 3U * width) / 8U) - bit;
            if (!have_previous || !produced || tail != expected) return false;
            while (bit < n * 8U) { if ((p[bit / 8U] >> (7U - bit % 8U)) & 1U) return false; ++bit; }
            *written = produced;
            return df_ready(f, info, options, pd);
        }
        if (code == 256U) {
            if (!have_previous) return false;
            width = 9U; maximum = 511U; next = 258U; have_previous = false; continue;
        }
        if (!have_previous) {
            if (code > 255U || produced == cap) return false;
            out[produced++] = (uint8_t)code;
            previous = code; have_previous = true; continue;
        }
        if (code > next || (code == next && next > maximum)) return false;
        walk = code == next ? previous : code;
        while (walk >= 258U) {
            if (walk >= next || used == sizeof(stack) || prefix[walk] >= walk) return false;
            stack[used++] = suffix[walk]; walk = prefix[walk];
            if ((work++ & 4095U) == 0U && !df_ready(f, info, options, pd)) return false;
        }
        if (walk > 255U || used == sizeof(stack)) return false;
        first = walk;
        stack[used++] = (uint8_t)first;
        if (used + (code == next ? 1U : 0U) > cap - produced) return false;
        while (used) out[produced++] = stack[--used];
        if (code == next) out[produced++] = (uint8_t)first;
        if (next <= maximum) {
            prefix[next] = (uint16_t)previous; suffix[next] = (uint8_t)first; ++next;
            if (next == maximum && width < 12U) { ++width; maximum = (1U << width) - 1U; }
        }
        previous = code;
    }
}

static uint32_t df_crc32(const uint8_t *p, size_t n)
{
    uint32_t table[256], crc = UINT32_MAX;
    unsigned i, j;
    size_t at;
    for (i = 0U; i < 256U; ++i) { uint32_t c = i; for (j = 0U; j < 8U; ++j) c = (c >> 1U) ^ ((c & 1U) ? UINT32_C(0xedb88320) : 0U); table[i] = c; }
    for (at = 0U; at < n; ++at) crc = (crc >> 8U) ^ table[(crc ^ p[at]) & 255U];
    return crc ^ UINT32_MAX;
}

static bool df_decode(Abstractformat *f, const dfsee_info *info, const xx_list_s *options, uint8_t **result, xx_pd_struct *pd)
{
    uint8_t *input = NULL, *output = NULL;
    size_t at = 520U, raw = 0U;
    bool ok = false;
    *result = NULL;
    if (!df_ready(f, info, options, pd)) return false;
    input = (uint8_t *)xx_mem_alloc(info->input_size);
    output = (uint8_t *)xx_mem_alloc(info->image_size);
    if (!input || !output || !df_ready(f, info, options, pd) || !df_read_at(f, f->base_address, input, info->input_size, pd)) goto done;
    while (at < info->input_size) {
        unsigned method, field;
        size_t decoded = 0U, cap = info->image_size - raw;
        if (!df_ready(f, info, options, pd) || info->input_size - at < 8U) goto done;
        method = df_u16(input + at); field = df_u16(input + at + 2U);
        if (!field || field > info->capacity || !cap) goto done;
        if (cap > info->capacity) cap = info->capacity;
        if (method == 0x1cU) {
            if (field > info->input_size - at - 8U ||
                !df_lzw(f, info, options, input + at + 8U, field, output + raw, cap, &decoded, pd) ||
                df_crc32(output + raw, decoded) != df_u32(input + at + 4U)) goto done;
            at += 8U + field;
        } else if ((method & 0xff00U) == 0xfc00U || method == 0xeefcU) {
            decoded = field;
            if (decoded > cap || decoded % 512U != 0U) goto done;
            xx_rt_memset(output + raw, method == 0xeefcU ? 0 : (int)(method & 255U), decoded);
            if (method == 0xeefcU) {
                if (df_u32(input + at + 4U) != UINT32_C(0xdeafface)) goto done;
            } else if (df_crc32(output + raw, decoded) != df_u32(input + at + 4U)) goto done;
            at += 8U;
        } else goto done;
        if (decoded % 512U != 0U || (decoded != info->capacity && at != info->input_size)) goto done;
        raw += decoded;
    }
    if (raw != info->image_size || !df_ready(f, info, options, pd)) goto done;
    *result = output; output = NULL; ok = true;
done:
    xx_mem_free(input); xx_mem_free(output); return ok;
}

void xx_dfsee_imz_init(xx_dfsee_imz *a, xx_io_device *device, int64_t base)
{
    if (!a) return;
    xx_rt_memset(a, 0, sizeof(*a)); xx_format_init(&a->format, device, base);
    a->format.endian = XX_ENDIAN_LITTLE; a->format.file_type = XX_FILE_TYPE_DFSEE_IMZ;
    a->format.format_type = XX_TYPE_ARCHIVE; a->format.is_archive = true;
    xx_format_set_mime_type(&a->format, "application/x-dfsee-imz"); xx_format_set_extension(&a->format, "imz");
    a->format.check_is_valid = xx_dfsee_imz_check_is_valid;
    a->format.handle_base_info = xx_dfsee_imz_handle_base_info;
    a->format.get_format_size = xx_dfsee_imz_get_format_size;
    a->format.get_number_of_archive_records = xx_dfsee_imz_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_dfsee_imz_create_archive_records_reading;
    a->format.get_current_archive_record = xx_dfsee_imz_get_current_archive_record;
    a->format.unpack_current_archive_record = xx_dfsee_imz_unpack_current_archive_record;
    a->format.archive_record_move_to_next = xx_dfsee_imz_archive_record_move_to_next;
    a->format.free_archive_records_reading = xx_dfsee_imz_free_archive_records_reading;
    a->archive_end = -1;
}
xx_dfsee_imz *xx_dfsee_imz_create(xx_io_device *device, int64_t base)
{ xx_dfsee_imz *a = (xx_dfsee_imz *)xx_mem_alloc(sizeof(*a)); if (a) xx_dfsee_imz_init(a, device, base); return a; }
void xx_dfsee_imz_destroy(xx_dfsee_imz *a) { if (a) xx_format_cleanup_extra_parameters(&a->format); }
void xx_dfsee_imz_free(xx_dfsee_imz *a) { if (a) { xx_dfsee_imz_destroy(a); xx_mem_free(a); } }
bool xx_dfsee_imz_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { dfsee_info info; return df_parse(f, &info, pd); }
bool xx_dfsee_imz_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    dfsee_info info;
    xx_dfsee_imz *a = (xx_dfsee_imz *)f;
    if (!df_parse(f, &info, pd)) { if (f) { f->is_valid = false; f->base_info_handled = false; f->format_size = -1; f->number_of_archive_records = 0U; } return false; }
    a->number_of_records = 1U; a->archive_end = f->base_address + (int64_t)info.input_size;
    a->image_size = info.image_size; a->source_offset = info.source_offset; a->unit_capacity = info.capacity;
    f->format_size = (int64_t)info.input_size; f->number_of_archive_records = 1U;
    f->overlay_offset = -1; f->overlay_size = 0; f->is_valid = true; f->base_info_handled = true;
    return true;
}
int64_t xx_dfsee_imz_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{ return f && (f->base_info_handled || xx_dfsee_imz_handle_base_info(f, pd)) ? f->format_size : -1; }
uint64_t xx_dfsee_imz_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{ return f && (f->base_info_handled || xx_dfsee_imz_handle_base_info(f, pd)) ? 1U : 0U; }

xx_archive_record_state *xx_dfsee_imz_create_archive_records_reading(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    dfsee_info info, *owned;
    xx_archive_record_state *s;
    xx_archive_record *r;
    if (!df_parse(f, &info, pd) || !df_ready(f, &info, options, pd)) return NULL;
    owned = (dfsee_info *)xx_mem_alloc(sizeof(*owned));
    s = (xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if (!owned || !s) { xx_mem_free(owned); xx_mem_free(s); return NULL; }
    *owned = info; xx_archive_record_state_init(s, f); s->internal_state = owned; s->free_internal = xx_mem_free;
    s->total_records = 1; r = &s->current_record;
    r->header_offset = f->base_address; r->header_size = 520; r->data_offset = f->base_address + 520;
    r->compressed_size = (int64_t)info.input_size - 520;
    if (!df_copy_options(&s->options, options) || !xx_archive_record_set_original_name(r, DFSEE_NAME) ||
        !xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, info.input_size - 520U) ||
        !xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, info.image_size) ||
        !xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, 12U) ||
        !xx_archive_record_set_meta_bool(r, XX_META_ID_IS_ENCRYPTED, false) || !xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, false) ||
        (info.has_smart && !xx_archive_record_set_meta_str(r, XX_META_ID_COMMENT,
            "Logical image; SMART omitted areas are represented as zeros."))) {
        xx_archive_record_state_free(s); return NULL;
    }
    s->has_record = true; return s;
}
const xx_archive_record *xx_dfsee_imz_get_current_archive_record(Abstractformat *f, xx_archive_record_state *s)
{ return f && s && s->format == f && s->has_record ? &s->current_record : NULL; }
bool xx_dfsee_imz_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    (void)pd;
    if (!f || !s || s->format != f) return false;
    s->has_record = false; s->current_index = 1; xx_archive_record_cleanup(&s->current_record); xx_archive_record_init(&s->current_record); return false;
}
bool xx_dfsee_imz_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const dfsee_info *info;
    const xx_var *option;
    uint8_t *plain = NULL;
    char *base = NULL, *path = NULL;
    xx_io_device *out = NULL;
    size_t done = 0U;
    bool ok = false, created = false;
    if (!f || !s || s->format != f || !s->has_record || !(info = (const dfsee_info *)s->internal_state) ||
        !df_decode(f, info, &s->options, &plain, pd)) return false;
    option = df_option(&s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) { xx_mem_free(plain); return true; }
    base = df_owned_path(option);
    if (!base || !df_ready(f, info, &s->options, pd)) goto done;
    path = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat3(base, "/", DFSEE_NAME) : xx_str_concat(base, DFSEE_NAME);
    if (!path || !df_ready(f, info, &s->options, pd) || !xx_store_create_dirs_a(path, false) || !df_ready(f, info, &s->options, pd)) goto done;
    out = xx_io_file_open(path, "wb"); created = out != NULL; ok = created;
    while (ok && done < info->image_size) {
        size_t step = info->image_size - done;
        ssize_t sent;
        if (!df_ready(f, info, &s->options, pd)) { ok = false; break; }
        if (step > 65536U) step = 65536U;
        sent = xx_io_write(out, plain + done, step);
        if (sent <= 0 || (size_t)sent > step) ok = false; else done += (size_t)sent;
    }
    if (!df_ready(f, info, &s->options, pd)) ok = false;
    if (out && xx_io_close(out) != 0) ok = false;
    out = NULL;
done:
    if (out) xx_io_close(out);
    if (!ok && created) xx_rt_remove(path);
    xx_mem_free(plain); xx_str_free(base); xx_str_free(path); return ok;
}
void xx_dfsee_imz_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s)
{ (void)f; xx_archive_record_state_free(s); }
