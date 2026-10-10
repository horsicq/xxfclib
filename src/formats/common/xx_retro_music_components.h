/* SPDX-License-Identifier: MIT. Private bounded shared format component helpers. */
#ifndef XX_RETRO_MUSIC_COMPONENTS_H
#define XX_RETRO_MUSIC_COMPONENTS_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define RETRO_MUSIC_LIMIT (64U * 1024U * 1024U)
typedef struct retro_music_blob {
    uint8_t *p;
    uint32_t n, crc_budget;
    xx_pd_struct *pd;
} retro_music_blob;
static bool retro_music_poll(const retro_music_blob *b) { return !b->pd || !xx_pd_is_stopped(b->pd); }
static bool retro_music_range(const retro_music_blob *b, uint32_t a, uint32_t z) { return a <= b->n && z <= b->n - a; }
static bool retro_music_load(Abstractformat *f, retro_music_blob *b, xx_pd_struct *pd) {
    int64_t n = pm_available(f);
    uint32_t a = 0;
    xx_mem_zero(b, sizeof(*b));
    b->pd = pd;
    if (n <= 0 || n > RETRO_MUSIC_LIMIT || !retro_music_poll(b) || !(b->p = (uint8_t *)xx_mem_alloc((size_t)n)))
        return false;
    b->n = (uint32_t)n;
    b->crc_budget = RETRO_MUSIC_LIMIT;
    while (a < b->n) {
        uint32_t z = b->n - a;
        if (z > 65536)
            z = 65536;
        if (!retro_music_poll(b) || !pm_read(f, a, b->p + a, z)) {
            xx_mem_free(b->p);
            b->p = NULL;
            return false;
        }
        a += z;
    }
    return true;
}
static bool retro_music_emit(Abstractformat *f, pm_stream *s, const retro_music_blob *b, const char *name, uint32_t a,
                             uint32_t z) {
    return z && s->count < 4096 && retro_music_poll(b) && retro_music_range(b, a, z) && pm_add(f, s, name, a, z);
}
static XXFC_MAYBE_UNUSED bool retro_music_zero(const uint8_t *p, uint32_t z) {
    uint32_t i;
    for (i = 0; i < z; ++i)
        if (p[i])
            return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool retro_music_match(const uint8_t *p, uint32_t n, const char *text) {
    size_t z = xx_rt_strlen(text);
    return n == z && !xx_rt_memcmp(p, text, z);
}
static XXFC_MAYBE_UNUSED bool retro_music_decimal(const uint8_t *p, uint32_t z, uint32_t cap, uint32_t *value) {
    uint32_t i, v = 0;
    if (!z)
        return false;
    for (i = 0; i < z; ++i) {
        uint32_t d = (uint32_t)p[i] - '0';
        if (d > 9 || v > cap / 10 || (v == cap / 10 && d > cap % 10))
            return false;
        v = v * 10 + d;
    }
    *value = v;
    return true;
}
static XXFC_MAYBE_UNUSED bool retro_music_hex16(const uint8_t *p, uint32_t z, uint32_t *value) {
    uint32_t i, v = 0;
    if (!z || z > 4)
        return false;
    for (i = 0; i < z; ++i) {
        unsigned d = p[i];
        if (d >= '0' && d <= '9')
            d -= '0';
        else if (d >= 'A' && d <= 'F')
            d = d - 'A' + 10;
        else if (d >= 'a' && d <= 'f')
            d = d - 'a' + 10;
        else
            return false;
        v = (v << 4) | d;
    }
    *value = v;
    return true;
}
static XXFC_MAYBE_UNUSED uint32_t retro_music_crc(retro_music_blob *b, uint32_t a, uint32_t z, bool *ok) {
    uint32_t c = 0, i;
    *ok = false;
    if (!retro_music_range(b, a, z) || z > b->crc_budget)
        return 0;
    b->crc_budget -= z;
    for (i = 0; i < z;) {
        uint32_t part = z - i;
        if (part > 4096)
            part = 4096;
        if (!retro_music_poll(b))
            return 0;
        c = xx_crc32_calc(c, b->p + a + i, part);
        i += part;
    }
    *ok = true;
    return c;
}
#endif
