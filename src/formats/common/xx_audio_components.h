/* SPDX-License-Identifier: MIT. Independent bounded original-component helpers. */
#ifndef XX_AUDIO_COMPONENTS_H
#define XX_AUDIO_COMPONENTS_H
#include "xx_numeric_values.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define AUDIO_COMPONENT_LIMIT 67108864U
#define AUDIO_COMPONENT_COUNT 4096U
typedef struct audio_component_blob {
    Abstractformat *f;
    pm_stream *s;
    xx_pd_struct *pd;
    uint8_t *b;
    size_t n;
} audio_component_blob;
static bool audio_component_range(audio_component_blob *c, uint64_t p, uint64_t z) {
    return !binary_stop(c->pd) && binary_range(p, z, c->n);
}
static bool audio_component_eq(audio_component_blob *c, size_t p, const char *t, size_t z) {
    return audio_component_range(c, p, z) && !xx_rt_memcmp(c->b + p, t, z);
}
static bool audio_component_add(audio_component_blob *c, const char *name, size_t p, size_t z) {
    return c->s->count < AUDIO_COMPONENT_COUNT && audio_component_range(c, p, z) &&
           pm_add(c->f, c->s, name, (int64_t)p, (int64_t)z);
}
static XXFC_MAYBE_UNUSED bool audio_component_zero(audio_component_blob *c, size_t p, size_t z) {
    size_t i;
    if (!audio_component_range(c, p, z))
        return false;
    for (i = 0; i < z; ++i) {
        if ((i & 4095U) == 0 && binary_stop(c->pd))
            return false;
        if (c->b[p + i])
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool audio_component_strings(audio_component_blob *c, size_t *p, unsigned n) {
    unsigned i;
    for (i = 0; i < n; ++i) {
        unsigned z;
        if (!audio_component_range(c, *p, 1))
            return false;
        z = c->b[(*p)++];
        if (!audio_component_range(c, *p, z))
            return false;
        *p += z;
    }
    return true;
}
static XXFC_MAYBE_UNUSED uint16_t audio_component_crc(const uint8_t *b, size_t n, xx_pd_struct *pd) {
    xx_crc_context crc;
    size_t at = 0U;
    if (!xx_crc_context_init_type(&crc, XX_CRC_TYPE_CRC16_BUYPASS))
        return 1U;
    while (at < n) {
        size_t part = n - at > 4096U ? 4096U : n - at;
        if (binary_stop(pd))
            return 1U;
        xx_crc_context_update(&crc, b + at, part);
        at += part;
    }
    return (uint16_t)xx_crc_context_final(&crc);
}
/* Classic tracker token grammar: row terminators and bounded field flags.
 * The event values remain original encoded components, never interpreted as audio. */
static XXFC_MAYBE_UNUSED bool audio_component_events(audio_component_blob *c, size_t p, size_t z, unsigned rows,
                                                     unsigned channels, bool dsm) {
    size_t end = p + z;
    unsigned row = 0;
    uint32_t seen = 0;
    if (!audio_component_range(c, p, z))
        return false;
    while (p < end && row < rows) {
        unsigned v = c->b[p++], k, need;
        if (!v) {
            ++row;
            seen = 0;
            continue;
        }
        k = v & (dsm ? 15U : 31U);
        if (k >= channels || (!dsm && (seen & (1U << k))))
            return false;
        seen |= 1U << k;
        need = dsm ? ((v & 128) ? 1U : 0U) + ((v & 64) ? 1U : 0U) + ((v & 32) ? 1U : 0U) + ((v & 16) ? 2U : 0U)
                   : ((v & 32) ? 2U : 0U) + ((v & 64) ? 2U : 0U) + ((v & 128) ? 1U : 0U);
        if (!need || need > end - p)
            return false;
        p += need;
    }
    return row == rows && p == end && !binary_stop(c->pd);
}
static bool audio_component_finish(audio_component_blob *c) {
    if (!c->s->count || binary_stop(c->pd))
        return false;
    c->s->size = (int64_t)c->n;
    return true;
}
static bool audio_component_loaded(Abstractformat *f, pm_stream *s, xx_pd_struct *pd,
                                   bool (*parse)(audio_component_blob *)) {
    audio_component_blob c;
    int64_t n = pm_available(f);
    size_t p;
    bool ok;
    if (n <= 0 || n > AUDIO_COMPONENT_LIMIT || binary_stop(pd))
        return false;
    c.f = f;
    c.s = s;
    c.pd = pd;
    c.n = (size_t)n;
    c.b = (uint8_t *)xx_mem_alloc(c.n);
    if (!c.b)
        return false;
    for (p = 0; p < c.n; p += 65536U) {
        size_t z = c.n - p > 65536U ? 65536U : c.n - p;
        if (binary_stop(pd) || !pm_read(f, (int64_t)p, c.b + p, z)) {
            xx_mem_free(c.b);
            return false;
        }
    }
    ok = parse(&c) && audio_component_finish(&c);
    xx_mem_free(c.b);
    return ok;
}
#endif
