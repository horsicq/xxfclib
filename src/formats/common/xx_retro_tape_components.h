/* SPDX-License-Identifier: MIT. Private bounded retro components. */
#ifndef XX_RETRO_TAPE_COMPONENTS_H
#define XX_RETRO_TAPE_COMPONENTS_H
#include "../xx_payload_members.h"
#define RETRO_TAPE_LIMIT (16U * 1024U * 1024U)
typedef struct retro_tape_blob {
    uint8_t *p;
    uint32_t n;
    xx_pd_struct *pd;
} retro_tape_blob;
static bool retro_tape_poll(const retro_tape_blob *b)
{
    return !b->pd || !xx_pd_is_stopped(b->pd);
}
static bool retro_tape_range(const retro_tape_blob *b, uint32_t a, uint32_t z)
{
    return a <= b->n && z <= b->n - a;
}
static bool retro_tape_load(Abstractformat *f, retro_tape_blob *b, xx_pd_struct *pd)
{
    int64_t n = pm_available(f);
    uint32_t a = 0;
    xx_mem_zero(b, sizeof(*b));
    b->pd = pd;
    if (n <= 0 || n > RETRO_TAPE_LIMIT || !retro_tape_poll(b) || !(b->p = (uint8_t *)xx_mem_alloc((size_t)n))) return false;
    b->n = (uint32_t)n;
    while (a < b->n) {
        uint32_t z = b->n - a;
        if (z > 65536) z = 65536;
        if (!retro_tape_poll(b) || !pm_read(f, a, b->p + a, z)) {
            xx_mem_free(b->p);
            b->p = NULL;
            return false;
        }
        a += z;
    }
    return true;
}
static bool retro_tape_emit(Abstractformat *f, pm_stream *s, const retro_tape_blob *b, const char *name, uint32_t a, uint32_t z)
{
    return z && s->count < 4096 && retro_tape_poll(b) && retro_tape_range(b, a, z) && pm_add(f, s, name, a, z);
}
static XXFC_MAYBE_UNUSED bool retro_tape_zero(const uint8_t *p, uint32_t z)
{
    uint32_t i;
    for (i = 0; i < z; ++i)
        if (p[i]) return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool retro_tape_name(const uint8_t *p, uint32_t z, char *out, bool petscii)
{
    uint32_t i, n = 0;
    bool ended = false;
    if (!z || !p[0] || p[0] == ' ') return false;
    for (i = 0; i < z; ++i) {
        unsigned c = p[i];
        if (!c) ended = true;
        else if (ended || c < 32 || c == 127 || (!petscii && c > 126)) return false;
        else out[n++] = (char)c;
    }
    while (n && out[n - 1] == ' ') {
        --n;
    }
    out[n] = 0;
    return n != 0;
}
#endif
