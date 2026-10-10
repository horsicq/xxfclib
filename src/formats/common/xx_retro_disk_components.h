/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded retro-container helpers; grammars remain in each reader.
 */
#ifndef XX_RETRO_DISK_COMPONENTS_H
#define XX_RETRO_DISK_COMPONENTS_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define RETRO_DISK_LIMIT (64U * 1024U * 1024U)
typedef struct retro_disk_blob {
    uint8_t *p;
    uint32_t n, crc_budget;
    xx_pd_struct *pd;
} retro_disk_blob;
typedef struct retro_disk_span {
    uint32_t a, z;
} retro_disk_span;
static bool retro_disk_poll(const retro_disk_blob *b) { return !b->pd || !xx_pd_is_stopped(b->pd); }
static bool retro_disk_range(const retro_disk_blob *b, uint32_t at, uint32_t n) { return at <= b->n && n <= b->n - at; }
static XXFC_MAYBE_UNUSED bool retro_disk_load(Abstractformat *f, retro_disk_blob *b, xx_pd_struct *pd) {
    int64_t n = pm_available(f);
    uint32_t at = 0;
    xx_mem_zero(b, sizeof(*b));
    b->pd = pd;
    if (n <= 0 || n > RETRO_DISK_LIMIT || !retro_disk_poll(b) || !(b->p = (uint8_t *)xx_mem_alloc((size_t)n)))
        return false;
    b->n = (uint32_t)n;
    b->crc_budget = RETRO_DISK_LIMIT;
    while (at < b->n) {
        uint32_t z = b->n - at;
        if (z > 65536U)
            z = 65536U;
        if (!retro_disk_poll(b) || !pm_read(f, at, b->p + at, z)) {
            xx_mem_free(b->p);
            b->p = NULL;
            return false;
        }
        at += z;
    }
    return true;
}
static bool retro_disk_emit(Abstractformat *f, pm_stream *s, const retro_disk_blob *b, const char *name, uint32_t a,
                            uint32_t z) {
    return s->count < 4096U && retro_disk_poll(b) && retro_disk_range(b, a, z) && pm_add(f, s, name, a, z);
}
static XXFC_MAYBE_UNUSED bool retro_disk_memory(Abstractformat *f, pm_stream *s, const retro_disk_blob *b,
                                                const char *name, uint32_t a, uint8_t *p, uint32_t z) {
    if (z > RETRO_DISK_LIMIT || !retro_disk_emit(f, s, b, name, a, 0)) {
        xx_mem_free(p);
        return false;
    }
    s->items[s->count - 1].memory = p;
    s->items[s->count - 1].size = z;
    s->items[s->count - 1].packed_size = 0;
    return true;
}
static XXFC_MAYBE_UNUSED bool retro_disk_disjoint(retro_disk_span *sp, uint32_t *count, uint32_t cap, uint32_t a,
                                                  uint32_t z) {
    uint32_t i;
    if (!z)
        return true;
    if (*count >= cap || a > UINT32_MAX - z)
        return false;
    for (i = 0; i < *count; ++i)
        if (a < sp[i].a + sp[i].z && sp[i].a < a + z)
            return false;
    sp[*count].a = a;
    sp[*count].z = z;
    ++*count;
    return true;
}
static XXFC_MAYBE_UNUSED bool retro_disk_zero(const uint8_t *p, uint32_t z) {
    uint32_t i;
    for (i = 0; i < z; ++i)
        if (p[i])
            return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool retro_disk_ascii(const uint8_t *p, uint32_t z, bool allowzero) {
    uint32_t i;
    for (i = 0; i < z; ++i)
        if ((p[i] < 32 || p[i] > 126) && !(allowzero && !p[i]))
            return false;
    return true;
}
static XXFC_MAYBE_UNUSED uint32_t retro_disk_crc(retro_disk_blob *b, uint32_t a, uint32_t z, bool *ok) {
    uint32_t c = 0, i;
    *ok = false;
    if (!retro_disk_range(b, a, z) || z > b->crc_budget)
        return 0;
    b->crc_budget -= z;
    for (i = 0; i < z;) {
        uint32_t part = z - i;
        if (part > 4096)
            part = 4096;
        if (!retro_disk_poll(b))
            return 0;
        c = xx_crc32_calc(c, b->p + a + i, part);
        i += part;
    }
    *ok = true;
    return c;
}
#endif
