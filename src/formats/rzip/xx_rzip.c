/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * RZIP2 grammar follows XArchive/Algos/xrzipdecoder.cpp (MIT).
 * Bounded input256MiB, output1GiB, substream blocks16MiB. Decode stages the
 * complete result before publishing; each chunk CRC and physical byte is checked.
 */
#include "xxfclib/formats/rzip/xx_rzip.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define RZ_INPUT (UINT64_C(256) * 1024 * 1024)
#define RZ_OUTPUT (UINT64_C(1024) * 1024 * 1024)
#define RZ_BLOCK (UINT64_C(16) * 1024 * 1024)
#define RZ_BLOCKS 100000U

typedef struct rz_block {
    uint32_t next;
    uint8_t *bytes;
    size_t size, at;
} rz_block;
typedef struct rz_extent {
    int64_t start, end;
} rz_extent;
typedef struct rz_streams {
    Abstractformat *format;
    int64_t start;
    rz_block streams[2];
    rz_extent *extents;
    size_t count, capacity;
    xx_pd_struct *pd;
    uint64_t memory_limit;
} rz_streams;
static bool rz_header(Abstractformat *f, uint64_t *raw)
{
    uint8_t h[24];
    size_t i;
    int64_t n = pm_available(f);
    if (n < 24 || (uint64_t)n > RZ_INPUT || !pm_read(f, 0, h, sizeof(h)) || xx_rt_memcmp(h, "RZIP", 4) || h[4] != 2 || h[5] > 1) return false;
    for (i = 14; i < 24; ++i)
        if (h[i]) return false;
    *raw = xx_data_get_u32(h + 6, 4, 0, true) | ((uint64_t)xx_data_get_u32(h + 10, 4, 0, true) << 32);
    return *raw <= RZ_OUTPUT && (*raw ? n >= 70 : n == 24);
}
static bool rz_range(Abstractformat *f, int64_t at, uint64_t size)
{
    int64_t n = pm_available(f);
    return at >= 0 && n >= at && size <= (uint64_t)(n - at);
}
static void rz_close(rz_streams *s)
{
    xx_mem_free(s->streams[0].bytes);
    xx_mem_free(s->streams[1].bytes);
    xx_mem_free(s->extents);
}
static bool rz_open(rz_streams *s, Abstractformat *f, int64_t start, uint64_t memory_limit, xx_pd_struct *pd)
{
    uint8_t h[26];
    unsigned empty = 0, i;
    xx_mem_zero(s, sizeof(*s));
    s->format = f;
    s->start = start;
    s->pd = pd;
    s->memory_limit = memory_limit;
    while (rz_range(f, s->start, 13) && pm_read(f, s->start, h, 13)) {
        if (h[0] != 3 || xx_data_get_u32(h + 1, 4, 0, false) || xx_data_get_u32(h + 5, 4, 0, false) || xx_data_get_u32(h + 9, 4, 0, false)) break;
        if (++empty > 1024 || xx_pd_is_stopped(pd)) return false;
        s->start += 13;
    }
    if (!rz_range(f, s->start, 26) || !pm_read(f, s->start, h, 26)) return false;
    for (i = 0; i < 2; ++i) {
        const uint8_t *p = h + i * 13;
        if (p[0] != 3 || xx_data_get_u32(p + 1, 4, 0, false) || xx_data_get_u32(p + 5, 4, 0, false)) return false;
        s->streams[i].next = xx_data_get_u32(p + 9, 4, 0, false);
        if (s->streams[i].next && s->streams[i].next < 26) return false;
    }
    return s->streams[0].next != 0;
}
static bool rz_load(rz_streams *s, unsigned stream)
{
    rz_block *b = &s->streams[stream];
    uint8_t h[13], *packed = NULL, *decoded = NULL;
    uint32_t pc, raw, next;
    int64_t at, end;
    size_t consumed = 0;
    bool ok = false;
    xx_io_device *out = NULL;
    rz_extent *grown;
    if (!b->next || s->count >= RZ_BLOCKS || xx_pd_is_stopped(s->pd)) return false;
    at = s->start + b->next;
    if (!rz_range(s->format, at, 13) || !pm_read(s->format, at, h, 13)) return false;
    pc = xx_data_get_u32(h + 1, 4, 0, false);
    raw = xx_data_get_u32(h + 5, 4, 0, false);
    next = xx_data_get_u32(h + 9, 4, 0, false);
    if ((h[0] != 3 && h[0] != 4) || !pc || !raw || pc > RZ_BLOCK || raw > RZ_BLOCK || raw > s->memory_limit || pc > s->memory_limit ||
        !rz_range(s->format, at + 13, pc) || (h[0] == 3 && pc != raw))
        return false;
    end = at + 13 + pc;
    if (next && (uint64_t)next < (uint64_t)(end - s->start)) return false;
    packed = (uint8_t *)xx_mem_alloc(pc);
    if (!packed || !pm_read(s->format, at + 13, packed, pc)) goto done;
    if (h[0] == 3) {
        decoded = packed;
        packed = NULL;
    } else {
        decoded = (uint8_t *)xx_mem_alloc(raw);
        if (!decoded) goto done;
        out = xx_io_mem_open(decoded, raw);
        if (!out) goto done;
        if (!xx_bzip2_unpack_memory_to_device_ex(packed, pc, out, &consumed, s->pd) || consumed != pc || xx_io_tell(out) != (int64_t)raw) goto done;
    }
    /* Every next link advances past its own block. Cross-stream collisions and
     * holes are checked once by sorted physical extents, avoiding quadratic scans. */
    if (s->count == s->capacity) {
        size_t cap = s->capacity ? s->capacity * 2 : 8;
        grown = (rz_extent *)xx_mem_realloc(s->extents, cap * sizeof(*grown));
        if (!grown) goto done;
        s->extents = grown;
        s->capacity = cap;
    }
    s->extents[s->count].start = at;
    s->extents[s->count++].end = end;
    xx_mem_free(b->bytes);
    b->bytes = decoded;
    decoded = NULL;
    b->at = 0;
    b->size = raw;
    b->next = next;
    ok = true;
done:
    if (out) xx_io_close(out);
    xx_mem_free(packed);
    xx_mem_free(decoded);
    return ok;
}
static bool rz_read(rz_streams *s, unsigned stream, uint8_t *dst, size_t n)
{
    rz_block *b = &s->streams[stream];
    while (n) {
        size_t take;
        if (xx_pd_is_stopped(s->pd) || (b->at == b->size && !rz_load(s, stream))) return false;
        take = b->size - b->at;
        if (take > n) take = n;
        if (!take) return false;
        xx_rt_memcpy(dst, b->bytes + b->at, take);
        b->at += take;
        dst += take;
        n -= take;
    }
    return true;
}
static int rz_cmp(const void *a, const void *b)
{
    const rz_extent *x = (const rz_extent *)a, *y = (const rz_extent *)b;
    return x->start < y->start ? -1 : x->start > y->start;
}
static bool rz_finish(rz_streams *s, int64_t *end)
{
    size_t i;
    int64_t next = s->start + 26;
    for (i = 0; i < 2; ++i)
        if (s->streams[i].next || s->streams[i].at != s->streams[i].size) return false;
    xx_rt_qsort(s->extents, s->count, sizeof(*s->extents), rz_cmp);
    for (i = 0; i < s->count; ++i) {
        if (s->extents[i].start != next) return false;
        next = s->extents[i].end;
    }
    *end = next;
    return !xx_pd_is_stopped(s->pd);
}
static bool rz_unpack(Abstractformat *f, pm_member *m, xx_io_device *destination, xx_pd_struct *pd)
{
    uint64_t raw, used = 0, memory_limit = RZ_BLOCK;
    int64_t physical = 24;
    uint8_t *output = NULL;
    unsigned chunks = 0, blocks = 0;
    bool ok = false;
    const xx_var *limit;
    if (!rz_header(f, &raw) || raw != (uint64_t)m->size || xx_pd_is_stopped(pd)) return false;
    limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit) {
        uint64_t cap = xx_var_get_u64(limit);
        if (raw > cap) return false;
        if (memory_limit > (cap - raw) / 3) memory_limit = (cap - raw) / 3;
    }
    output = (uint8_t *)xx_mem_alloc(raw ? (size_t)raw : 1);
    if (!output) return false;
    while (used < raw) {
        rz_streams streams;
        uint64_t before = used;
        uint32_t crc = 0;
        int64_t end = 0;
        bool chunk_ok = false;
        if (chunks++ >= RZ_BLOCKS || !rz_open(&streams, f, physical, memory_limit, pd)) goto done;
        for (;;) {
            uint8_t token[3], v[4];
            uint32_t len, distance;
            uint64_t j;
            if (xx_pd_is_stopped(pd) || !rz_read(&streams, 0, token, 3)) break;
            len = xx_data_get_u16(token + 1, 2, 0, false);
            if (!len) {
                if (token[0] || !rz_read(&streams, 0, v, 4) || xx_data_get_u32(v, 4, 0, false) != crc) break;
                chunk_ok = true;
                break;
            }
            if (len > raw - used) break;
            if (token[0] == 0) {
                if (!rz_read(&streams, 1, output + (size_t)used, len)) break;
            } else {
                if (!rz_read(&streams, 0, v, 4)) break;
                distance = xx_data_get_u32(v, 4, 0, false);
                if (!distance || distance > used) break;
                for (j = 0; j < len; ++j) output[(size_t)(used + j)] = output[(size_t)(used + j - distance)];
            }
            crc = ~xx_crc32_calc(~crc, output + (size_t)used, len);
            used += len;
        }
        if (!chunk_ok || used == before || !rz_finish(&streams, &end) || end <= physical || streams.count > RZ_BLOCKS - blocks) {
            rz_close(&streams);
            goto done;
        }
        blocks += (unsigned)streams.count;
        rz_close(&streams);
        physical = end;
    }
    if (physical != pm_available(f) || used != raw || xx_pd_is_stopped(pd)) goto done;
    if (destination) {
        size_t at = 0;
        while (at < (size_t)raw) {
            size_t n = (size_t)raw - at;
            ssize_t wrote;
            if (n > 65536) n = 65536;
            if (xx_pd_is_stopped(pd)) goto done;
            wrote = xx_io_write(destination, output + at, n);
            if (wrote <= 0 || (size_t)wrote > n) goto done;
            at += (size_t)wrote;
        }
    }
    ok = !xx_pd_is_stopped(pd);
done:
    xx_mem_free(output);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint64_t raw;
    pm_member *m;
    if (xx_pd_is_stopped(pd) || !rz_header(f, &raw) || !pm_add(f, s, "data", 0, pm_available(f))) return false;
    m = &s->items[0];
    m->size = (int64_t)raw;
    m->read_all = rz_unpack;
    s->size = pm_available(f);
    return true;
}
void xx_rzip_init(xx_rzip *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_RZIP, "rz");
        r->format.check_is_valid = xx_rzip_check_is_valid;
        r->format.handle_base_info = xx_rzip_handle_base_info;
        xx_format_set_mime_type(&r->format, "application/x-rzip");
    }
}
xx_rzip *xx_rzip_create(xx_io_device *d, int64_t b)
{
    xx_rzip *r = (xx_rzip *)xx_mem_alloc(sizeof(*r));
    if (r) xx_rzip_init(r, d, b);
    return r;
}
void xx_rzip_destroy(xx_rzip *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_rzip_free(xx_rzip *r)
{
    if (r) {
        xx_rzip_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_rzip_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    int64_t saved;
    bool valid;
    if (!f || !f->device || (saved = xx_io_tell(f->device)) < 0) return false;
    valid = pm_valid(f, pd);
    return xx_io_seek64(f->device, saved, SEEK_SET) == 0 && valid;
}
bool xx_rzip_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    int64_t saved;
    bool valid;
    if (!f || !f->device || (saved = xx_io_tell(f->device)) < 0) return false;
    valid = pm_handle(f, pd);
    if (xx_io_seek64(f->device, saved, SEEK_SET) != 0) {
        f->is_valid = false;
        return false;
    }
    return valid;
}
xx_file_type_t xx_rzip_detect(xx_io_device *d, int64_t b)
{
    uint8_t h[4];
    xx_rzip r;
    bool valid;
    if (!xx_io_read_at(d, b, h, 4) || xx_rt_memcmp(h, "RZIP", 4)) return XX_FILE_TYPE_UNKNOWN;
    xx_rzip_init(&r, d, b);
    valid = xx_rzip_check_is_valid(&r.format, NULL);
    xx_rzip_destroy(&r);
    return valid ? XX_FILE_TYPE_RZIP : XX_FILE_TYPE_UNKNOWN;
}
