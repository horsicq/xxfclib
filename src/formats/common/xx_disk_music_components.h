/* SPDX-License-Identifier: MIT. Private checked disk/music primitives. */
#ifndef XX_DISK_MUSIC_COMPONENTS_H
#define XX_DISK_MUSIC_COMPONENTS_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
typedef struct disk_music_blob {
    uint8_t *p;
    uint64_t n;
    xx_pd_struct *pd;
    uint64_t work, crc_bytes;
} disk_music_blob;
typedef struct disk_music_range {
    uint64_t at, n;
} disk_music_range;
static bool disk_music_stop(disk_music_blob *b) { return b->pd && xx_pd_is_stopped(b->pd); }
static bool disk_music_work(disk_music_blob *b, uint64_t n) {
    if (disk_music_stop(b) || n > 2000000 - b->work)
        return false;
    b->work += n;
    return true;
}
static bool disk_music_span(disk_music_blob *b, uint64_t at, uint64_t n) {
    return !disk_music_stop(b) && at <= b->n && n <= b->n - at;
}
static XXFC_MAYBE_UNUSED bool disk_music_load(Abstractformat *f, disk_music_blob *b, xx_pd_struct *pd) {
    int64_t n = pm_available(f);
    uint64_t at = 0;
    b->pd = pd;
    if (n < 1 || n > 33554432 || disk_music_stop(b))
        return false;
    b->n = (uint64_t)n;
    b->p = (uint8_t *)xx_mem_alloc((size_t)n);
    if (!b->p)
        return false;
    while (at < b->n) {
        size_t z = b->n - at > 65536 ? 65536 : (size_t)(b->n - at);
        if (!disk_music_span(b, at, z) || !pm_read(f, (int64_t)at, b->p + (size_t)at, z))
            return false;
        at += z;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool disk_music_tag(disk_music_blob *b, uint64_t at, const char *tag, size_t n) {
    return disk_music_span(b, at, n) && !xx_rt_memcmp(b->p + (size_t)at, tag, n);
}
static XXFC_MAYBE_UNUSED bool disk_music_zero(disk_music_blob *b, uint64_t at, uint64_t n) {
    uint64_t i;
    if (!disk_music_span(b, at, n))
        return false;
    for (i = 0; i < n; ++i) {
        if (!(i & 4095U) && disk_music_stop(b))
            return false;
        if (b->p[(size_t)(at + i)])
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool disk_music_emit(Abstractformat *f, pm_stream *s, disk_music_blob *b, const char *name,
                                              uint64_t at, uint64_t n) {
    return n && s->count < 4096 && disk_music_span(b, at, n) && pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static XXFC_MAYBE_UNUSED bool disk_music_bytes(Abstractformat *f, pm_stream *s, disk_music_blob *b, const char *name,
                                               const void *p, size_t n) {
    uint8_t *copy;
    if (!n || s->count >= 4096 || disk_music_stop(b))
        return false;
    copy = (uint8_t *)xx_mem_alloc(n);
    if (!copy)
        return false;
    xx_rt_memcpy(copy, p, n);
    if (!pm_add(f, s, name, 0, 0)) {
        xx_mem_free(copy);
        return false;
    }
    s->items[s->count - 1].memory = copy;
    s->items[s->count - 1].size = (int64_t)n;
    return true;
}
static XXFC_MAYBE_UNUSED bool disk_music_claim(disk_music_blob *b, disk_music_range *r, unsigned *count, uint64_t at,
                                               uint64_t n) {
    unsigned i;
    if (!n || *count >= 1024 || !disk_music_span(b, at, n))
        return false;
    for (i = 0; i < *count; ++i) {
        if (!disk_music_work(b, 1) || (at < r[i].at + r[i].n && r[i].at < at + n))
            return false;
    }
    r[*count].at = at;
    r[(*count)++].n = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool disk_music_crc(disk_music_blob *b, uint64_t at, uint64_t n, uint32_t *out) {
    static const xx_crc_model model = {32U, 0x1edc6f41U, 0U, false, false, 0U, "CRC-32/Castagnoli (unreflected)"};
    xx_crc_context ctx;
    uint64_t k = 0;
    if (!disk_music_span(b, at, n) || n > 33554432 - b->crc_bytes)
        return false;
    b->crc_bytes += n;
    if (!xx_crc_context_init(&ctx, &model))
        return false;
    while (k < n) {
        size_t part = n - k > 4096U ? 4096U : (size_t)(n - k);
        if (disk_music_stop(b))
            return false;
        xx_crc_context_update(&ctx, b->p + (size_t)(at + k), part);
        k += part;
    }
    *out = (uint32_t)xx_crc_context_final(&ctx);
    return true;
}
#define DISK_MUSIC_NEED(x)                                                                                             \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            goto done;                                                                                                 \
    } while (0)
#endif
