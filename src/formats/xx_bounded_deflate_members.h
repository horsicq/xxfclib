/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Private memory-budgeted Deflate, bounded output and member helpers. */
#ifndef XX_BOUNDED_DEFLATE_MEMBERS_H
#define XX_BOUNDED_DEFLATE_MEMBERS_H
#include "xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#define BDM_MEMORY_LIMIT (256U * 1024U * 1024U)
#define BDM_BLOCK_LIMIT (64U * 1024U * 1024U)
static XXFC_MAYBE_UNUSED uint64_t bdm_budget(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t budget = v ? xx_var_get_u64(v) : BDM_MEMORY_LIMIT;
    return budget < BDM_MEMORY_LIMIT ? budget : BDM_MEMORY_LIMIT;
}
static XXFC_MAYBE_UNUSED bool bdm_room(Abstractformat *f, pm_stream *s, uint64_t reserve)
{
    uint64_t used = (uint64_t)s->capacity * sizeof(pm_member);
    size_t i;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].memory) used += (uint64_t)s->items[i].size;
    return used <= bdm_budget(f) && reserve <= bdm_budget(f) - used;
}
static XXFC_MAYBE_UNUSED bool bdm_add(Abstractformat *f, pm_stream *s, const char *name, int64_t at, int64_t n)
{
    uint64_t growth = s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8U) * sizeof(pm_member) : 0;
    return bdm_room(f, s, growth) && pm_add(f, s, name, at, n);
}
static XXFC_MAYBE_UNUSED uint32_t bdm_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static XXFC_MAYBE_UNUSED uint16_t bdm_u16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}
typedef struct bdm_buffer {
    uint8_t *data;
    size_t size, capacity, limit;
} bdm_buffer;
static XXFC_MAYBE_UNUSED ssize_t bdm_buffer_write(xx_io_device *d, const void *p, size_t n)
{
    bdm_buffer *b = (bdm_buffer *)d->priv;
    if (n > b->limit - b->size) return -1;
    if (n > b->capacity - b->size) {
        size_t next = b->capacity ? b->capacity : (b->limit < 65536U ? b->limit : 65536U);
        uint8_t *memory;
        while (next < b->size + n) {
            if (next > b->limit / 2U) {
                next = b->limit;
                break;
            }
            next *= 2U;
        }
        memory = (uint8_t *)xx_mem_realloc(b->data, next);
        if (!memory) return -1;
        b->data = memory;
        b->capacity = next;
    }
    xx_mem_copy(b->data + b->size, p, n);
    b->size += n;
    return (ssize_t)n;
}
/* Decode exactly one raw stream into a bounded RAM vector. The consumed byte
 * count is independent of input read-ahead, so trailers remain verifiable. */
static XXFC_MAYBE_UNUSED bool bdm_inflate_reserved(Abstractformat *f, int64_t offset, int64_t size, bool zlib, bdm_buffer *b, int64_t *consumed, xx_pd_struct *pd,
                                                   uint64_t reserve)
{
    xx_io_device sink;
    uint8_t h[2], trailer[4];
    int64_t used = 0;
    xx_mem_zero(&sink, sizeof(sink));
    sink.priv = b;
    sink.write = bdm_buffer_write;
    /* Reserve decoder history and its bounded input/output buffers. */
    if (bdm_budget(f) < 524288U || reserve > bdm_budget(f) - 524288U) return false;
    b->limit = (size_t)(bdm_budget(f) - 524288U - reserve);
    if (offset < 0 || size < 0 || (zlib && (size < 6 || !pm_read(f, offset, h, 2) || !xx_zlib_stream_header_is_valid(h, 2)))) return false;
    if (!xx_deflate_unpack_device_ex(f->device, f->base_address + offset + (zlib ? 2 : 0), size - (zlib ? 2 : 0), &sink, false, 0, NULL, 0, &used, pd)) return false;
    if (zlib) {
        uint32_t stored;
        if (used > size - 6 || !pm_read(f, offset + 2 + used, trailer, 4)) return false;
        stored = ((uint32_t)trailer[0] << 24) | ((uint32_t)trailer[1] << 16) | ((uint32_t)trailer[2] << 8) | trailer[3];
        if (stored != xx_zlib_stream_adler32(b->data, b->size)) return false;
        used += 6;
    }
    if (consumed) *consumed = used;
    return !(pd && xx_pd_is_stopped(pd));
}
static XXFC_MAYBE_UNUSED bool bdm_inflate(Abstractformat *f, int64_t offset, int64_t size, bool zlib, bdm_buffer *b, int64_t *consumed, xx_pd_struct *pd)
{
    return bdm_inflate_reserved(f, offset, size, zlib, b, consumed, pd, 0);
}
static XXFC_MAYBE_UNUSED bool bdm_write(xx_io_device *output, const uint8_t *p, size_t n, xx_pd_struct *pd)
{
    while (n) {
        size_t piece = n < xx_get_file_buffer_size() ? n : xx_get_file_buffer_size();
        ssize_t wrote;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!output) return true;
        wrote = xx_io_write(output, p, piece);
        if (wrote <= 0 || (size_t)wrote > piece) return false;
        p += (size_t)wrote;
        n -= (size_t)wrote;
    }
    return !(pd && xx_pd_is_stopped(pd));
}

static XXFC_MAYBE_UNUSED bool bdm_add_memory(Abstractformat *f, pm_stream *s, const char *name, const uint8_t *p, size_t n)
{
    uint8_t *copy = (uint8_t *)xx_mem_alloc(n ? n : 1U);
    if (!copy) return false;
    if (n) xx_mem_copy(copy, p, n);
    if (!bdm_add(f, s, name, 0, 0)) {
        xx_mem_free(copy);
        return false;
    }
    s->items[s->count - 1U].memory = copy;
    s->items[s->count - 1U].size = (int64_t)n;
    return true;
}

#endif
