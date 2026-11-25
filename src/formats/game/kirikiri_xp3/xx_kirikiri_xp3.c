/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Bounded XP3 index and segment parser; native zlib, complete member Adler32.
 * Wire grammar: krkrz/krkrz base/XP3Archive.cpp. No title-specific filters. */
#include "xxfclib/formats/game/kirikiri_xp3/xx_kirikiri_xp3.h"
#include "../../xx_payload_members.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/data/xx_data.h"
#define XP_CAP UINT64_C(268435456)
#define XP_MEMBER UINT64_C(67108864)
#define XP_INDEX UINT64_C(16777216)
#define XP_WORK UINT64_C(1048576)
typedef struct xp_segment {
    uint64_t offset, plain, packed;
    uint32_t method;
} xp_segment;
typedef struct xp_member {
    xp_segment *segments;
    size_t count;
    uint32_t checksum, flags;
    uint64_t resident;
    char *stored_name;
} xp_member;
static uint32_t xp_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t xp_u64(const uint8_t *p)
{
    return xp_u32(p) | ((uint64_t)xp_u32(p + 4) << 32);
}
static uint64_t xp_budget(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : XP_CAP;
    return n < XP_CAP ? n : XP_CAP;
}
static bool xp_range(uint64_t total, uint64_t at, uint64_t size)
{
    return at <= total && size <= total - at;
}
static bool xp_overlap(uint64_t at, uint64_t size, uint64_t lo, uint64_t hi)
{
    return size && at < hi && at + size > lo;
}
static void xp_free(void *p)
{
    xp_member *c = (xp_member *)p;
    if (c) {
        xx_mem_free(c->segments);
        xx_mem_free(c->stored_name);
        xx_mem_free(c);
    }
}
static bool xp_zlib(Abstractformat *f, uint64_t at, size_t packed, uint8_t *plain, size_t size, uint64_t resident, xx_pd_struct *pd)
{
    uint8_t *input = NULL;
    xx_io_device *out = NULL;
    size_t consumed = 0;
    bool ok = false;
    if (packed < 6 || resident + packed + XP_WORK > xp_budget(f) || (pd && xx_pd_is_stopped(pd))) return false;
    input = (uint8_t *)xx_mem_alloc(packed);
    if (!input || !pm_read(f, (int64_t)at, input, packed) || !xx_zlib_stream_header_is_valid(input, packed) || (input[1] & 32U)) goto done;
    out = xx_io_mem_open(plain, size);
    if (!out) goto done;
    ok = xx_deflate_unpack_memory_to_device_ex(input + 2, packed - 6, out, &consumed, false, pd) && consumed == packed - 6 && xx_io_tell(out) == (int64_t)size &&
         xx_zlib_stream_trailer_matches(input, packed, plain, size) && !(pd && xx_pd_is_stopped(pd));
done:
    if (out) xx_io_close(out);
    xx_mem_free(input);
    return ok;
}
static bool xp_read_all(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    xp_member *c = (xp_member *)m->context;
    uint8_t *plain;
    uint64_t resident;
    size_t i, at = 0;
    bool ok = false;
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!c || (v && (uint64_t)m->size > xx_var_get_u64(v)) || c->resident + (uint64_t)m->size + XP_WORK > xp_budget(f)) return false;
    plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1U);
    if (!plain) return false;
    resident = c->resident + (uint64_t)m->size;
    for (i = 0; i < c->count; ++i) {
        xp_segment *segment = &c->segments[i];
        if ((pd && xx_pd_is_stopped(pd)) || segment->plain > (uint64_t)m->size - at) goto done;
        if (segment->method) {
            if (!xp_zlib(f, segment->offset, (size_t)segment->packed, plain + at, (size_t)segment->plain, resident, pd)) goto done;
        } else if (!pm_read(f, (int64_t)segment->offset, plain + at, (size_t)segment->plain)) goto done;
        at += (size_t)segment->plain;
    }
    if (at != (uint64_t)m->size || xx_zlib_stream_adler32(plain, at) != c->checksum) goto done;
    at = 0;
    while (out && at < (uint64_t)m->size) {
        size_t n = (uint64_t)m->size - at > 16384U ? 16384U : (size_t)m->size - at;
        ssize_t w;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        w = xx_io_write(out, plain + at, n);
        if (w <= 0 || (size_t)w > n) goto done;
        at += (size_t)w;
    }
    ok = !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(plain);
    return ok;
}
static char *xp_name(const uint8_t *p, size_t count, char **original)
{
    const xx_codepage *codec = xx_codepage_find("UTF-16LE");
    char *utf8, *safe;
    size_t used = 0, written = 0, i;
    *original = NULL;
    if (!codec || !count || count > 4096) return NULL;
    for (i = 0; i < count; ++i)
        if (!p[i * 2] && !p[i * 2 + 1]) return NULL;
    utf8 = (char *)xx_mem_alloc(count * 3U + 1U);
    if (!utf8) return NULL;
    if (xx_codepage_to_utf8(codec, p, count * 2U, (uint8_t *)utf8, count * 3U, &used, &written) != XX_CODEPAGE_OK || used != count * 2U) {
        xx_mem_free(utf8);
        return NULL;
    }
    utf8[written] = 0;
    safe = xx_archive_output_name(utf8, 65001, 0);
    if (safe) *original = utf8;
    else xx_mem_free(utf8);
    return safe;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t magic[11] = {'X', 'P', '3', 13, 10, 32, 10, 26, 139, 103, 1};
    uint8_t h[19], *index = NULL;
    int64_t available = pm_available(f);
    uint64_t total, index_at, index_end, index_size, packed, redirect = 0, extent, budget = xp_budget(f), resident = sizeof(pm_stream), maximum = XP_MEMBER;
    size_t pos = 0, i;
    bool ok = false;
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && xx_var_get_u64(v) < maximum) maximum = xx_var_get_u64(v);
    if (available < 28 || (pd && xx_pd_is_stopped(pd)) || !pm_read(f, 0, h, 19) || xx_rt_memcmp(h, magic, 11)) return false;
    total = (uint64_t)available;
    index_at = xp_u64(h + 11);
    if (index_at < 19 || !xp_range(total, index_at, 9) || !pm_read(f, (int64_t)index_at, h, 9)) return false;
    if (h[0] == 128) {
        if (xp_u64(h + 1) || !xp_range(total, index_at, 17) || !pm_read(f, (int64_t)index_at, h, 17)) return false;
        redirect = index_at;
        index_at = xp_u64(h + 9);
        if (index_at < redirect + 17 || !xp_range(total, index_at, 9) || !pm_read(f, (int64_t)index_at, h, 9)) return false;
    }
    if (h[0] > 1) return false;
    packed = xp_u64(h + 1);
    index_size = packed;
    index_end = index_at + 9;
    if (h[0]) {
        if (!xp_range(total, index_at, 17) || !pm_read(f, (int64_t)index_at, h, 17)) return false;
        index_size = xp_u64(h + 9);
        index_end = index_at + 17;
    }
    if (!index_size || index_size > XP_INDEX || packed > XP_INDEX || !xp_range(total, index_end, packed) || resident + index_size + packed + XP_WORK > budget)
        return false;
    index = (uint8_t *)xx_mem_alloc((size_t)index_size);
    if (!index) return false;
    if (h[0]) {
        if (!xp_zlib(f, index_end, (size_t)packed, index, (size_t)index_size, resident + index_size, pd)) goto done;
    } else if (!pm_read(f, (int64_t)index_end, index, (size_t)index_size)) goto done;
    index_end += packed;
    extent = index_end;
    while (pos < index_size) {
        uint64_t bytes, file_end, part, raw = 0, compressed = 0;
        const uint8_t *info = NULL, *segments = NULL;
        size_t info_size = 0, segment_size = 0, segment_count, capacity, name_bytes, growth;
        uint32_t checksum = 0, flags;
        bool has_checksum = false;
        char *safe = NULL, *name = NULL, *original = NULL, short_leaf[48];
        const char *leaf;
        bool shortened;
        size_t original_bytes;
        xp_member *ctx = NULL;
        pm_member *m;
        if ((pd && xx_pd_is_stopped(pd)) || s->count >= 100000U || index_size - pos < 12U) goto done;
        bytes = xp_u64(index + pos + 4);
        if (bytes > index_size - pos - 12U) goto done;
        file_end = pos + 12U + bytes;
        if (xx_rt_memcmp(index + pos, "File", 4)) {
            pos = (size_t)file_end;
            continue;
        }
        part = pos + 12;
        while (part < file_end) {
            const uint8_t *tag = index + part, *data;
            uint64_t n;
            if (file_end - part < 12 || (n = xp_u64(tag + 4)) > file_end - part - 12) goto done;
            data = tag + 12;
            if (!xx_rt_memcmp(tag, "info", 4)) {
                if (info) goto done;
                info = data;
                info_size = (size_t)n;
            } else if (!xx_rt_memcmp(tag, "segm", 4)) {
                if (segments) goto done;
                segments = data;
                segment_size = (size_t)n;
            } else if (!xx_rt_memcmp(tag, "adlr", 4)) {
                if (has_checksum || n != 4) goto done;
                checksum = xp_u32(data);
                has_checksum = true;
            }
            part += 12 + n;
        }
        if (!info || info_size < 22 || !segments || !segment_size || segment_size % 28 || !has_checksum) goto done;
        flags = xp_u32(info);
        raw = xp_u64(info + 4);
        compressed = xp_u64(info + 12);
        name_bytes = (size_t)(info[20] | ((unsigned)info[21] << 8)) * 2U;
        /* The protected bit is an extraction-permission marker, separate
         * from the engine's optional title-specific content filter. Preserve
         * it as metadata while verifying the unfiltered member bytes. */
        if ((flags & ~UINT32_C(0x80000000)) || raw > maximum || compressed > XP_MEMBER || name_bytes > 8192U ||
            (info_size != 22 + name_bytes && !(info_size == 24 + name_bytes && !info[22 + name_bytes] && !info[23 + name_bytes])))
            goto done;
        safe = xp_name(info + 22, name_bytes / 2U, &original);
        if (!safe) goto done;
        shortened = xx_rt_strlen(safe) > 120U;
        original_bytes = shortened ? xx_rt_strlen(original) + 1U : 0;
        leaf = safe;
        if (shortened) {
            const char *dot = NULL, *q;
            bool extension = true;
            size_t length = 0;
            for (q = safe; *q; ++q) {
                if (*q == '/') dot = NULL;
                else if (*q == '.') dot = q;
            }
            if (dot) {
                for (q = dot + 1; *q; ++q, ++length)
                    if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9'))) extension = false;
            }
            if (!dot || !length || length > 12) extension = false;
            (void)xx_rt_snprintf(short_leaf, sizeof(short_leaf), "long-name%s", extension ? dot : ".bin");
            leaf = short_leaf;
        }
        segment_count = segment_size / 28;
        capacity = s->count == s->capacity ? (s->capacity ? s->capacity * 2U : 8U) : s->capacity;
        name_bytes = xx_rt_strlen(leaf) + 16U;
        growth = (capacity - s->capacity) * sizeof(pm_member);
        if (segment_count > 65536U || resident + (uint64_t)(capacity - s->capacity) * sizeof(pm_member) + sizeof(xp_member) + segment_count * sizeof(xp_segment) +
                                              name_bytes + original_bytes + index_size + raw + XP_WORK >
                                          budget) {
            xx_mem_free(safe);
            xx_mem_free(original);
            goto done;
        }
        name = (char *)xx_mem_alloc(name_bytes);
        ctx = (xp_member *)xx_mem_calloc(1, sizeof(*ctx));
        if (!name || !ctx) {
            xx_mem_free(safe);
            xx_mem_free(original);
            xx_mem_free(name);
            xx_mem_free(ctx);
            goto done;
        }
        (void)xx_rt_snprintf(name, name_bytes, "%04u-%s", (unsigned)s->count, leaf);
        xx_mem_free(safe);
        if (!pm_add(f, s, "file", 0, 0)) {
            xx_mem_free(name);
            xx_mem_free(original);
            xx_mem_free(ctx);
            goto done;
        }
        m = &s->items[s->count - 1];
        m->display_name = name;
        m->context = ctx;
        m->free_context = xp_free;
        m->read_all = xp_read_all;
        m->size = (int64_t)raw;
        ctx->stored_name = shortened ? original : NULL;
        if (!shortened) xx_mem_free(original);
        ctx->count = segment_count;
        ctx->checksum = checksum;
        ctx->flags = flags;
        ctx->segments = (xp_segment *)xx_mem_calloc(segment_count, sizeof(xp_segment));
        if (!ctx->segments) goto done;
        resident += growth + sizeof(xp_member) + segment_count * sizeof(xp_segment) + name_bytes + original_bytes;
        {
            uint64_t sum_raw = 0, sum_packed = 0;
            for (i = 0; i < segment_count; ++i) {
                const uint8_t *d = segments + i * 28U;
                xp_segment *q = &ctx->segments[i];
                q->method = xp_u32(d);
                q->offset = xp_u64(d + 4);
                q->plain = xp_u64(d + 12);
                q->packed = xp_u64(d + 20);
                if (q->method > 1 || q->offset < 19 || !xp_range(total, q->offset, q->packed) || q->plain > raw - sum_raw || q->packed > compressed - sum_packed ||
                    xp_overlap(q->offset, q->packed, index_at, index_end) || (redirect && xp_overlap(q->offset, q->packed, redirect, redirect + 17)) ||
                    (!q->method && q->plain != q->packed) || (q->method && (q->packed < 6 || q->packed > XP_MEMBER)))
                    goto done;
                sum_raw += q->plain;
                sum_packed += q->packed;
                if (q->method) m->compression_method = 8;
                if (q->offset + q->packed > extent) extent = q->offset + q->packed;
            }
            if (sum_raw != raw || sum_packed != compressed) goto done;
        }
        m->offset = f->base_address + (int64_t)ctx->segments[0].offset;
        m->packed_size = (int64_t)compressed;
        ctx->resident = resident + index_size;
        if (!xp_read_all(f, m, NULL, pd)) goto done;
        pos = (size_t)file_end;
    }
    if (!s->count) goto done;
    for (i = 0; i < s->count; ++i) ((xp_member *)s->items[i].context)->resident = resident;
    s->size = (int64_t)extent;
    ok = !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(index);
    return ok;
}
static const xx_archive_record *xp_current(Abstractformat *f, xx_archive_record_state *state)
{
    const xx_archive_record *r = pm_current(f, state);
    if (r) {
        pm_stream *s = (pm_stream *)state->internal_state;
        xp_member *c = (xp_member *)s->items[s->index].context;
        if (c->stored_name) {
            char *comment = xx_str_concat3("XP3 stored name: ", c->stored_name, c->flags ? " [protected flag 0x80000000; no title-specific filter applied]" : "");
            bool ok;
            if (!comment) return NULL;
            ok = xx_archive_record_set_meta_str(&state->current_record, XX_META_ID_COMMENT, comment);
            xx_mem_free(comment);
            if (!ok) return NULL;
        } else if (c->flags &&
                   !xx_archive_record_set_meta_str(&state->current_record, XX_META_ID_COMMENT, "XP3 protected flag 0x80000000; no title-specific filter applied"))
            return NULL;
    }
    return r;
}
void xx_kirikiri_xp3_init(xx_kirikiri_xp3 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_KIRIKIRI_XP3, "xp3");
        r->format.get_current_archive_record = xp_current;
    }
}
xx_kirikiri_xp3 *xx_kirikiri_xp3_create(xx_io_device *d, int64_t b)
{
    xx_kirikiri_xp3 *r = (xx_kirikiri_xp3 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_kirikiri_xp3_init(r, d, b);
    return r;
}
void xx_kirikiri_xp3_destroy(xx_kirikiri_xp3 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_kirikiri_xp3_free(xx_kirikiri_xp3 *r)
{
    if (r) {
        xx_kirikiri_xp3_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_kirikiri_xp3_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_kirikiri_xp3_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
