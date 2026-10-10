/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
#ifndef XX_TRACKER_COMPONENTS_H
#define XX_TRACKER_COMPONENTS_H
#include "xx_binary_cursor.h"
#include "xxfclib/data/xx_data.h"
typedef struct tracker_component_rg {
    uint64_t at, n;
} tracker_component_rg;
static bool tracker_component_overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool tracker_component_emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t at, uint64_t n, uint64_t end)
{
    size_t i;
    if (end > 268435456 || end > (uint64_t)pm_available(f) || !binary_range(at, n, end) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (tracker_component_overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, label, (int64_t)at, (int64_t)n);
}
static XXFC_MAYBE_UNUSED bool tracker_component_take_emit(binary_cursor *c, pm_stream *s, const char *name, uint64_t n)
{
    uint64_t at = c->at;
    return binary_skip(c, n) && tracker_component_emit(c->f, s, name, at, n, c->end);
}
static XXFC_MAYBE_UNUSED bool tracker_component_zero(const uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (b[i]) return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool tracker_component_loop(uint32_t a, uint32_t n, uint32_t size)
{
    return a <= size && n <= size - a;
}
static XXFC_MAYBE_UNUSED bool tracker_component_finite(const uint8_t *b)
{
    return (xx_data_get_u32(b, 4, 0, true) & 0x7f800000U) != 0x7f800000U;
}
static XXFC_MAYBE_UNUSED bool tracker_component_reserve(tracker_component_rg *r, unsigned *nr, unsigned cap, uint64_t at, uint64_t n, uint64_t lower, uint64_t end)
{
    unsigned i;
    if (*nr >= cap || at < lower || !binary_range(at, n, end)) return false;
    for (i = 0; i < *nr; ++i)
        if (tracker_component_overlap(at, n, r[i].at, r[i].n)) return false;
    r[*nr].at = at;
    r[*nr].n = n;
    ++*nr;
    return true;
}
#endif
