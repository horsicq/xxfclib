/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Private tiz1/tiz2 solid zlib reader. Included after tz_pe_overlay. */
#include "../../xx_payload_members.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#define TL_CAP UINT64_C(268435456)
#define TL_DATA UINT64_C(67108864)
#define TL_WORK UINT64_C(1048576)
typedef struct tl_member {
    uint64_t resident;
} tl_member;
static uint32_t tl_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t tl_budget(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : TL_CAP;
    return n < TL_CAP ? n : TL_CAP;
}
/* Signature only, used to select callbacks. Full parsing verifies zlib and records. */
static bool tl_locate(Abstractformat *f, int64_t *offset)
{
    uint8_t h[8];
    int64_t n = pm_available(f), at = 0;
    uint32_t alignment;
    bool ok = false;
    int64_t saved;
    if (!f || !f->device || (saved = xx_io_tell(f->device)) < 0) return false;
    if (n < 14 || !pm_read(f, 0, h, 8)) goto done;
    if (h[0] == 'M' && h[1] == 'Z' && !tz_pe_overlay(f, n, &at, &alignment)) goto done;
    if (n - at < 14 || !pm_read(f, at, h, 8) || (xx_rt_memcmp(h, "tiz1", 4) && xx_rt_memcmp(h, "tiz2", 4)) || tl_u32(h + 4) != 1U) goto done;
    *offset = at;
    ok = true;
done:
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0) ok = false;
    return ok;
}
static bool tl_read_all(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    tl_member *c = (tl_member *)m->context;
    size_t at = 0;
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!c || c->resident > tl_budget(f)) return false;
    if (v && (uint64_t)m->size > xx_var_get_u64(v)) return false;
    while (at < (uint64_t)m->size) {
        size_t n = (uint64_t)m->size - at > 16384U ? 16384U : (size_t)m->size - at;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (out) {
            ssize_t w = xx_io_write(out, m->memory + at, n);
            if (w <= 0 || (size_t)w > n) return false;
            at += (size_t)w;
        } else at += n;
    }
    return !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t at, total = pm_available(f), consumed, decoded_size;
    uint8_t *input = NULL, *decoded = NULL;
    xx_io_device *out = NULL;
    uint64_t budget = tl_budget(f), cap, retained = sizeof(pm_stream), maximum = TL_DATA;
    size_t packed, pos = 0, i;
    bool ok = false;
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && xx_var_get_u64(v) < maximum) maximum = xx_var_get_u64(v);
    if (budget <= TL_WORK || (pd && xx_pd_is_stopped(pd)) || !tl_locate(f, &at) || (uint64_t)(total - at - 8) > TL_DATA) return false;
    packed = (size_t)(total - at - 8);
    if (packed < 6U || packed + TL_WORK + 1024U >= budget) return false;
    cap = (budget - packed - TL_WORK) / 2U;
    if (cap > TL_DATA) cap = TL_DATA;
    input = (uint8_t *)xx_mem_alloc(packed);
    decoded = (uint8_t *)xx_mem_alloc((size_t)cap);
    if (!input || !decoded || !pm_read(f, at + 8, input, packed) || !xx_zlib_stream_header_is_valid(input, packed) || (input[1] & 32U)) goto done;
    out = xx_io_mem_open(decoded, (size_t)cap);
    if (!out) goto done;
    {
        size_t used = 0;
        if (!xx_deflate_unpack_memory_to_device_ex(input + 2, packed - 6U, out, &used, false, pd)) goto done;
        consumed = (int64_t)used;
    }
    decoded_size = xx_io_tell(out);
    xx_io_close(out);
    out = NULL;
    if (consumed != (int64_t)packed - 6 || decoded_size < 40 || !xx_zlib_stream_trailer_matches(input, packed, decoded, (size_t)decoded_size)) goto done;
    while (pos < (uint64_t)decoded_size) {
        size_t n, capacity, allocation;
        uint32_t size;
        char raw[1025], *safe, *name;
        uint64_t need;
        pm_member *m;
        tl_member *ctx;
        if ((pd && xx_pd_is_stopped(pd)) || s->count >= 100000U || (uint64_t)decoded_size - pos < 40U || xx_rt_memcmp(decoded + pos, "tzff", 4)) goto done;
        size = tl_u32(decoded + pos + 8);
        n = tz_le16(decoded + pos + 38);
        pos += 40;
        if (!n || n > 1024U || n > (uint64_t)decoded_size - pos || size > maximum || size > (uint64_t)decoded_size - pos - n) goto done;
        for (i = 0; i < n; ++i)
            if (!decoded[pos + i]) goto done;
        xx_rt_memcpy(raw, decoded + pos, n);
        raw[n] = 0;
        pos += n;
        safe = xx_archive_output_name(raw, 1252, 0);
        if (!safe) goto done;
        allocation = xx_rt_strlen(safe) + 16U;
        capacity = s->count == s->capacity ? (s->capacity ? s->capacity * 2U : 8U) : s->capacity;
        need = retained + (uint64_t)(capacity - s->capacity) * sizeof(pm_member) + sizeof(tl_member) + allocation + (size ? size : 1U);
        if (need + cap + packed + TL_WORK > budget) {
            xx_mem_free(safe);
            goto done;
        }
        name = (char *)xx_mem_alloc(allocation);
        ctx = (tl_member *)xx_mem_alloc(sizeof(*ctx));
        if (!name || !ctx) {
            xx_mem_free(safe);
            xx_mem_free(name);
            xx_mem_free(ctx);
            goto done;
        }
        (void)xx_rt_snprintf(name, allocation, "%04u-%s", (unsigned)s->count, safe);
        xx_mem_free(safe);
        if (!pm_add(f, s, raw, at + 8, (int64_t)packed)) {
            xx_mem_free(name);
            xx_mem_free(ctx);
            goto done;
        }
        m = &s->items[s->count - 1];
        m->display_name = name;
        m->context = ctx;
        m->free_context = xx_mem_free;
        m->read_all = tl_read_all;
        m->size = size;
        m->compression_method = 8;
        ctx->resident = 0;
        m->memory = (uint8_t *)xx_mem_alloc(size ? size : 1U);
        if (!m->memory) goto done;
        xx_rt_memcpy(m->memory, decoded + pos, size);
        pos += size;
        retained = need;
    }
    if (!s->count) goto done;
    for (i = 0; i < s->count; ++i) ((tl_member *)s->items[i].context)->resident = retained;
    s->size = total;
    ok = !(pd && xx_pd_is_stopped(pd));
done:
    if (out) xx_io_close(out);
    xx_mem_free(input);
    xx_mem_free(decoded);
    return ok;
}
static bool tl_selected(Abstractformat *f)
{
    return f && f->check_is_valid == pm_valid;
}
static bool tl_to_device(Abstractformat *f, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    pm_stream *s;
    if (!f || !state || state->format != f || !state->has_record || !(s = (pm_stream *)state->internal_state)) return false;
    return tl_read_all(f, &s->items[s->index], destination, pd);
}
