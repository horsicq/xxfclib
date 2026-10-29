/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the DiskDupe DDI image.  A per-track table gives each
 * written track's position inside the data area; the member is the flat
 * image those tracks reassemble into.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/filesystem/diskdupe/xx_diskdupe.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef DISKDUPE
#define XX_DISKDUPE_FILE_TYPE XX_FILE_TYPE_DISKDUPE
#else
#define XX_DISKDUPE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DISKDUPE_OVERHEAD UINT64_C(65536)
#define DISKDUPE_OPTION_BYTES 8192U

#define DISKDUPE_MAX_MEMBERS 65536U
#define DISKDUPE_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct diskdupe_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method; /* 0 = stored, non-zero = format codec */
    bool decode;
} diskdupe_member;

typedef struct diskdupe_stream_s {
    diskdupe_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} diskdupe_stream;

static const xx_var *diskdupe_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}

static uint64_t diskdupe_budget(Abstractformat *f, const xx_list_s *options, uint32_t id)
{
    uint64_t limit = UINT64_MAX;
    const xx_list_s *lists[2] = {f ? &f->list_extra_parameters : NULL, options};
    for (size_t k = 0U; k < 2U; ++k) if (lists[k]) for (size_t i = 0U; i < lists[k]->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)lists[k], i);
        if (m && m->meta_id == id && xx_var_get_u64(&m->var) < limit) limit = xx_var_get_u64(&m->var);
    }
    return limit;
}

static bool diskdupe_options_valid(const xx_list_s *options)
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
            if ((!m->var.val.str.ptr && n) || bytes >= DISKDUPE_OPTION_BYTES || n >= DISKDUPE_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            n = m->var.val.wstr.len;
            if ((!m->var.val.wstr.ptr && n) || (DISKDUPE_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DISKDUPE_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
        } else if (m->var.type >= XX_VAR_TYPE_STRING) return false;
    }
    return true;
}

/* Bounded owned options keep the 64KiB overhead charge conservative even
 * during wide-path conversion. Views are cloned, never borrowed. */
static bool diskdupe_copy_options(xx_list_s *to, const xx_list_s *from)
{
    size_t i, bytes = 0U;
    if (!from) return true;
    if (!diskdupe_options_valid(from)) return false;
    for (i = 0U; i < from->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)from, i);
        xx_meta copy;
        bool ok = false;
        if (!m) return false;
        xx_meta_init(&copy, m->meta_id);
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t n = m->var.val.str.len;
            char *s;
            if (bytes >= DISKDUPE_OPTION_BYTES || n >= DISKDUPE_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
            s = (char *)xx_mem_alloc(n + 1U);
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.str.ptr, n); s[n] = 0; ok = xx_var_set_str_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t n = m->var.val.wstr.len;
            wchar_t *s;
            if ((DISKDUPE_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DISKDUPE_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
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
static char *diskdupe_owned_path(const xx_var *v)
{
    size_t n;
    if (!v) return NULL;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        char *s;
        n = v->val.str.len;
        if (n >= DISKDUPE_OPTION_BYTES || (!v->val.str.ptr && n)) return NULL;
        s = (char *)xx_mem_alloc(n + 1U);
        if (s) { if (n) xx_rt_memcpy(s, v->val.str.ptr, n); s[n] = 0; }
        return s;
    }
    if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wchar_t *wide;
        char *s;
        n = v->val.wstr.len;
        if (n >= DISKDUPE_OPTION_BYTES / sizeof(wchar_t) || (!v->val.wstr.ptr && n)) return NULL;
        wide = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
        if (!wide) return NULL;
        if (n) xx_rt_memcpy(wide, v->val.wstr.ptr, n * sizeof(wchar_t));
        wide[n] = 0; s = xx_str_unicode_to_utf8(wide); xx_mem_free(wide); return s;
    }
    return NULL;
}


static bool diskdupe_ready(Abstractformat *f, const diskdupe_member *m, const xx_list_s *options, xx_pd_struct *pd)
{
    return !(pd && xx_pd_is_stopped(pd)) && diskdupe_options_valid(options) &&
        diskdupe_options_valid(&f->list_extra_parameters) &&
        m->unpacked_size <= diskdupe_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
        m->unpacked_size + DISKDUPE_OVERHEAD <= diskdupe_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

static bool diskdupe_read_at(Abstractformat *f, int64_t at, void *buffer, size_t n, xx_pd_struct *pd)
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


/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction.  The helper only has to be CRT free. */
static char *diskdupe_make_name(const char *prefix, int a, int b, const char *suffix)
{
    char buffer[64];
    size_t used = 0U;
    size_t index;
    char *result;
    for (index = 0U; prefix && prefix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[index];
    }
    if (a >= 0) {
        char digits[8];
        size_t count = 0U;
        int value = a;
        do {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 2U) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    if (b >= 0) {
        if (used >= sizeof(buffer) - 2U) return NULL;
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + (b % 10));
    }
    for (index = 0U; suffix && suffix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[index];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void diskdupe_stream_free(void *opaque)
{
    diskdupe_stream *stream = (diskdupe_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool diskdupe_add_member(diskdupe_stream *stream, const diskdupe_member *member)
{
    diskdupe_member *grown;
    if (!stream || !member || stream->count >= DISKDUPE_MAX_MEMBERS || stream->count > SIZE_MAX / sizeof(*grown) - 1U) return false;
    grown = (diskdupe_member *)xx_mem_realloc(stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define DISKDUPE_TABLE_OFFSET 0x48
#define DISKDUPE_ENTRY_SIZE 5

/* DiskDupe's DDI: the ASCII banner "MSD Image Version 1 " and a 0x1A, a small
 * fixed header whose bytes 0x43 and 0x44 carry the track count and the
 * allocation block size, then one five byte entry per track - a 24-bit start,
 * in blocks from the start of the data area, plus two status bytes.
 *
 * Nothing stores where the data area begins or how many tracks were actually
 * written.  Both follow from the table: the starts of the written tracks rise
 * in exact multiples of the per-track block count, so the highest start fixes
 * the number of stored tracks, and the data area must then end at the end of
 * the file.  A file whose arithmetic does not close is rejected. */
static bool diskdupe_layout(const uint8_t *table, uint32_t track_count, uint32_t *track_blocks, uint32_t *stored)
{
    uint32_t index;
    uint32_t count = 0U;
    int64_t previous = -1;
    uint32_t step = 0U;
    for (index = 0U; index < track_count; ++index) {
        const uint8_t *entry = table + (size_t)index * DISKDUPE_ENTRY_SIZE;
        uint32_t value = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8U) | ((uint32_t)entry[2] << 16U);
        if (entry[3] != 1U && entry[3] != 2U) return false;
        if ((int64_t)value <= previous) continue;
        if (count == 1U) {
            step = value;
            if (step == 0U) return false;
        } else if (count > 1U) {
            if (step == 0U || value != count * step) return false;
        }
        previous = (int64_t)value;
        ++count;
    }
    if (count < 2U || step == 0U) return false;
    *track_blocks = step;
    *stored = count;
    return true;
}

typedef struct diskdupe_im_info_s {
    uint32_t tracks, track_bytes, stored;
    uint8_t locations[160];
} diskdupe_im_info;

/* IM has one header track and bounded physical-track mappings. Only the
 * observed raw-track / absent-track forms are admitted. Missing tracks are
 * an F6 projection, not authenticated original content. */
static bool diskdupe_im_layout(Abstractformat *f, diskdupe_im_info *info, xx_pd_struct *pd)
{
    static const uint8_t magic[10] = {'I','M',0,0,0,0,0,0,0,0};
    static const uint32_t tracks[4] = {80U,160U,160U,160U};
    static const uint32_t sectors[4] = {9U,15U,9U,18U};
    uint8_t header[9216], used[161] = {0};
    uint32_t type, i, n;
    bool sequential = false, explicit_locations = false;
    int64_t total;
    if (!f || !f->device || f->base_address < 0 || !info || (pd && xx_pd_is_stopped(pd))) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address || total - f->base_address < 14 ||
        !diskdupe_read_at(f, f->base_address, header, 14U, pd) || xx_rt_memcmp(header, magic, 10U)) return false;
    type = xx_data_get_u32(header + 10U, 4, 0, false);
    if (type < 1U || type > 4U) return false;
    xx_rt_memset(info, 0, sizeof(*info));
    info->tracks = tracks[type - 1U]; info->track_bytes = 512U * sectors[type - 1U];
    if ((uint64_t)(total - f->base_address) < info->track_bytes ||
        !diskdupe_read_at(f, f->base_address, header, info->track_bytes, pd) ||
        xx_rt_memcmp(header, magic, 10U) || xx_data_get_u32(header + 10U, 4, 0, false) != type) return false;
    for (i = 14U; i < 100U; ++i) if (header[i]) return false;
    for (i = 100U + info->tracks * 6U; i < info->track_bytes; ++i) if (header[i]) return false;
    for (i = 0U; i < info->tracks; ++i) {
        const uint8_t *e = header + 100U + i * 6U;
        if (!e[0]) { for (n = 1U; n < 6U; ++n) if (e[n]) return false; continue; }
        if (e[0] != 1U || e[2] || e[3] || e[4] || e[5] != 1U) return false;
        if (!e[1]) {
            /* Type1/4 uniformly zero locations follow fdimg's explicitly guessed
             * sequential policy. This does not authenticate producer ordering. */
            if ((type != 1U && type != 4U) || explicit_locations) return false;
            sequential = true; info->locations[i] = (uint8_t)(++info->stored);
        } else {
            if (sequential || e[1] > info->tracks || used[e[1]]) return false;
            explicit_locations = true; used[e[1]] = 1U;
            info->locations[i] = e[1]; ++info->stored;
        }
    }
    if (explicit_locations) for (i = 1U; i <= info->stored; ++i) if (!used[i]) return false;
    return (uint64_t)(total - f->base_address) == (uint64_t)(info->stored + 1U) * info->track_bytes && !(pd && xx_pd_is_stopped(pd));
}

static bool diskdupe_im_member(Abstractformat *f, diskdupe_member *m, xx_pd_struct *pd)
{
    diskdupe_im_info info;
    if (!diskdupe_im_layout(f, &info, pd)) return false;
    xx_rt_memset(m, 0, sizeof(*m));
    m->name = diskdupe_make_name("image", -1, -1, ".img");
    m->header_offset = f->base_address; m->header_size = info.track_bytes;
    m->data_offset = f->base_address + info.track_bytes;
    m->packed_size = (int64_t)info.stored * info.track_bytes;
    m->unpacked_size = (uint64_t)info.tracks * info.track_bytes; m->method = 2U; m->decode = true;
    return m->name != NULL;
}

static bool diskdupe_im_decode(Abstractformat *f, const diskdupe_member *m, uint8_t **plain, size_t *plain_size, xx_pd_struct *pd)
{
    diskdupe_im_info info;
    uint8_t *out;
    uint32_t i;
    if (!diskdupe_im_layout(f, &info, pd) || m->header_offset != f->base_address || m->header_size != info.track_bytes ||
        m->data_offset != f->base_address + info.track_bytes || m->packed_size != (int64_t)info.stored * info.track_bytes ||
        m->unpacked_size != (uint64_t)info.tracks * info.track_bytes) return false;
    out = (uint8_t *)xx_mem_alloc((size_t)m->unpacked_size);
    if (!out) return false;
    xx_rt_memset(out, 0xf6, (size_t)m->unpacked_size);
    for (i = 0U; i < info.tracks; ++i) {
        if ((pd && xx_pd_is_stopped(pd)) || (info.locations[i] &&
            !diskdupe_read_at(f, f->base_address + (int64_t)info.locations[i] * info.track_bytes,
                              out + (size_t)i * info.track_bytes, info.track_bytes, pd))) { xx_mem_free(out); return false; }
    }
    *plain = out; *plain_size = (size_t)m->unpacked_size; return true;
}

static bool diskdupe_parse(Abstractformat *format, diskdupe_stream **result, const xx_list_s *options, xx_pd_struct *pd)
{
    static const char magic[21] = {'M', 'S', 'D', ' ', 'I', 'm', 'a', 'g', 'e', ' ', 'V', 'e', 'r', 's', 'i', 'o', 'n', ' ', '1', ' ', 0x1a};
    uint8_t header[DISKDUPE_TABLE_OFFSET];
    uint8_t *table = NULL;
    diskdupe_stream *stream = NULL;
    diskdupe_member member;
    int64_t total, size, table_size, data_start;
    uint32_t track_count, block_size, track_blocks = 0U, stored = 0U;
    uint64_t track_bytes, image_size;
    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)) || !diskdupe_options_valid(options) || !diskdupe_options_valid(&format->list_extra_parameters) ||
        diskdupe_budget(format, options, XX_META_ID_OPT_MEMORY_LIMIT) < DISKDUPE_OVERHEAD) return false;
    {
        uint8_t signature[2];
        if (!diskdupe_read_at(format, format->base_address, signature, sizeof(signature), pd)) return false;
        if (signature[0] == 'I' && signature[1] == 'M') {
            if (!diskdupe_im_member(format, &member, pd)) return false;
            if (!diskdupe_ready(format, &member, options, pd)) { xx_mem_free(member.name); return false; }
            stream = (diskdupe_stream *)xx_mem_calloc(1U, sizeof(*stream));
            if (!stream || !diskdupe_add_member(stream, &member)) { xx_mem_free(member.name); diskdupe_stream_free(stream); return false; }
            stream->archive_size = member.header_size + member.packed_size; *result = stream; return true;
        }
    }
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)DISKDUPE_TABLE_OFFSET || !diskdupe_read_at(format, format->base_address, header, sizeof(header), pd) ||
        xx_rt_memcmp(header, magic, sizeof(magic)) != 0)
        return false;
    track_count = header[0x43];
    block_size = xx_data_get_u16(header + 0x44U, 2, 0, false);
    if (track_count < 2U || block_size < 128U || block_size > 4096U || (block_size & (block_size - 1U)) != 0U) return false;
    table_size = (int64_t)track_count * DISKDUPE_ENTRY_SIZE;
    if (table_size > size - DISKDUPE_TABLE_OFFSET) return false;
    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) return false;
    if (!diskdupe_read_at(format, format->base_address + DISKDUPE_TABLE_OFFSET, table, (size_t)table_size, pd) ||
        !diskdupe_layout(table, track_count, &track_blocks, &stored))
        goto fail;
    track_bytes = (uint64_t)track_blocks * block_size;
    image_size = track_bytes * track_count;
    if (track_bytes == 0U || track_bytes > DISKDUPE_MAX_OUTPUT || image_size > DISKDUPE_MAX_OUTPUT || stored > track_count) goto fail;
    if ((uint64_t)(size - DISKDUPE_TABLE_OFFSET - table_size) < track_bytes * stored) goto fail;
    data_start = size - (int64_t)(track_bytes * stored);
    if (data_start < DISKDUPE_TABLE_OFFSET + table_size) goto fail;
    stream = (diskdupe_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(&member, sizeof(member));
    member.name = diskdupe_make_name("image", -1, -1, ".img");
    member.header_offset = format->base_address;
    member.header_size = DISKDUPE_TABLE_OFFSET + table_size;
    member.data_offset = format->base_address + data_start;
    member.packed_size = size - data_start;
    member.unpacked_size = image_size;
    member.method = 1U;
    member.decode = true;
    if (!member.name || !diskdupe_ready(format, &member, options, pd) || !diskdupe_add_member(stream, &member)) {
        if (member.name) xx_mem_free(member.name);
        goto fail;
    }
    xx_mem_free(table);
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (table) xx_mem_free(table);
    diskdupe_stream_free(stream);
    return false;
}

static bool diskdupe_decode(Abstractformat *format, const diskdupe_member *member, uint8_t **plain, size_t *plain_size, xx_pd_struct *pd)
{
    uint8_t header[DISKDUPE_TABLE_OFFSET];
    uint8_t *table = NULL;
    uint8_t *output = NULL;
    uint32_t track_count, block_size, track_blocks = 0U, stored = 0U;
    uint32_t index;
    uint64_t track_bytes;
    int64_t table_size;
    int64_t previous = -1;
    if (member->method == 2U) return diskdupe_im_decode(format, member, plain, plain_size, pd);
    if (member->unpacked_size == 0U || member->unpacked_size > DISKDUPE_MAX_OUTPUT || !diskdupe_read_at(format, member->header_offset, header, sizeof(header), pd))
        return false;
    track_count = header[0x43];
    block_size = xx_data_get_u16(header + 0x44U, 2, 0, false);
    table_size = (int64_t)track_count * DISKDUPE_ENTRY_SIZE;
    if (member->header_size != DISKDUPE_TABLE_OFFSET + table_size) return false;
    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!table || !output || !diskdupe_read_at(format, member->header_offset + DISKDUPE_TABLE_OFFSET, table, (size_t)table_size, pd) ||
        !diskdupe_layout(table, track_count, &track_blocks, &stored))
        goto fail;
    track_bytes = (uint64_t)track_blocks * block_size;
    if (track_bytes * track_count != member->unpacked_size) goto fail;
    /* Preserve the legacy F6 projection for unwritten tracks. */
    xx_rt_memset(output, 0xf6, (size_t)member->unpacked_size);
    for (index = 0U; index < track_count; ++index) {
        const uint8_t *entry = table + (size_t)index * DISKDUPE_ENTRY_SIZE;
        uint32_t value = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8U) | ((uint32_t)entry[2] << 16U);
        uint64_t source;
        if ((int64_t)value <= previous) continue;
        previous = (int64_t)value;
        source = (uint64_t)value * block_size;
        if (source > (uint64_t)member->packed_size || track_bytes > (uint64_t)member->packed_size - source) goto fail;
        if (!diskdupe_read_at(format, member->data_offset + (int64_t)source, output + (uint64_t)index * track_bytes, (size_t)track_bytes, pd)) goto fail;
    }
    xx_mem_free(table);
    *plain = output;
    *plain_size = (size_t)member->unpacked_size;
    return true;
fail:
    if (table) xx_mem_free(table);
    if (output) xx_mem_free(output);
    return false;
}

static bool diskdupe_set_record(xx_archive_record *record, const diskdupe_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Stored members are copied verbatim; everything else goes to the format
 * codec above, which is the only place a size can grow. */
static bool diskdupe_extract(Abstractformat *format, const diskdupe_member *member, uint8_t **plain, size_t *plain_size, xx_pd_struct *pd)
{
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return diskdupe_decode(format, member, plain, plain_size, pd);
    if (member->packed_size < 0 || (uint64_t)member->packed_size > DISKDUPE_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0 ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 && !diskdupe_read_at(format, member->data_offset, output, (size_t)member->packed_size, pd)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_diskdupe_init(xx_diskdupe *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DISKDUPE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-diskdupe");
    xx_format_set_extension(&archive->format, "ddi");
    archive->format.check_is_valid = xx_diskdupe_check_is_valid;
    archive->format.handle_base_info = xx_diskdupe_handle_base_info;
    archive->format.get_format_size = xx_diskdupe_get_format_size;
    archive->format.get_number_of_archive_records = xx_diskdupe_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_diskdupe_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_diskdupe_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_diskdupe_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_diskdupe_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_diskdupe_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_diskdupe *xx_diskdupe_create(xx_io_device *device, int64_t base_address)
{
    xx_diskdupe *archive = (xx_diskdupe *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_diskdupe_init(archive, device, base_address);
    return archive;
}

void xx_diskdupe_destroy(xx_diskdupe *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_diskdupe_free(xx_diskdupe *archive)
{
    if (!archive) return;
    xx_diskdupe_destroy(archive);
    xx_mem_free(archive);
}

bool xx_diskdupe_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    diskdupe_stream *stream;
    if (!diskdupe_parse(format, &stream, NULL, pd)) return false;
    diskdupe_stream_free(stream);
    return true;
}

bool xx_diskdupe_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    diskdupe_stream *stream;
    xx_diskdupe *archive;
    if (!format || !diskdupe_parse(format, &stream, NULL, pd)) return false;
    archive = (xx_diskdupe *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    diskdupe_stream_free(stream);
    return true;
}

int64_t xx_diskdupe_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_diskdupe_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_diskdupe_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_diskdupe_handle_base_info(format, pd)) ? ((xx_diskdupe *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_diskdupe_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    diskdupe_stream *stream;
    xx_archive_record_state *state;
    if (!diskdupe_parse(format, &stream, options, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        diskdupe_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = diskdupe_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!diskdupe_copy_options(&state->options, options) || !diskdupe_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_diskdupe_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_diskdupe_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    diskdupe_stream *stream;
    if (!format || !state || state->format != format || !state->has_record || (pd && xx_pd_is_stopped(pd)) || !(stream = (diskdupe_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = diskdupe_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_diskdupe_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    diskdupe_stream *stream;
    diskdupe_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (diskdupe_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!diskdupe_ready(format, member, &state->options, pd) || !diskdupe_extract(format, member, &plain, &plain_size, pd) ||
        !diskdupe_ready(format, member, &state->options, pd)) goto done;
    path_option = diskdupe_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) path_option = xx_format_resolve_extra_parameter(format, NULL, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    owned_base = diskdupe_owned_path(path_option); base = owned_base;
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", member->name)
                                                                                                  : xx_str_concat(base, member->name);
    {
        const xx_var *overwrite = diskdupe_option(&state->options, XX_META_ID_OPT_OVERWRITE);
        if (!overwrite) overwrite = xx_format_resolve_extra_parameter(format, NULL, XX_META_ID_OPT_OVERWRITE);
        if (!path || (xx_io_file_exists_a(path) && (!overwrite || !xx_var_get_bool(overwrite))) ||
            !diskdupe_ready(format, member, &state->options, pd) || !xx_store_create_dirs_a(path, false) ||
            !diskdupe_ready(format, member, &state->options, pd)) goto done;
    }
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            size_t step = plain_size - written;
            ssize_t amount;
            if (!diskdupe_ready(format, member, &state->options, pd)) { result = false; break; }
            if (step > 65536U) step = 65536U;
            amount = xx_io_write(destination, plain + written, step);
            if (amount <= 0 || (size_t)amount > step) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (!diskdupe_ready(format, member, &state->options, pd)) result = false;
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_diskdupe_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
