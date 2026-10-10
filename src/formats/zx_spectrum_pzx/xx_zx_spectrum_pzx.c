/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/zx_spectrum_pzx/xx_zx_spectrum_pzx.h"
#include "../common/xx_retro_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f, pm_stream *s, retro_music_blob *b)
{
    const uint8_t *p = b->p;
    uint32_t a = 0, count = 0;
    uint64_t pulses = 0;
    bool signal = false;
    if (b->n < 10 || xx_rt_memcmp(p, "PZXT", 4)) return false;
    while (a < b->n) {
        uint32_t z, t, end, j;
        char name[64];
        if (!retro_music_poll(b) || ++count > 2048 || !retro_music_range(b, a, 8)) return false;
        z = xx_data_get_u32(p + a + 4, 4, 0, false);
        if (!retro_music_range(b, a + 8, z)) return false;
        t = a + 8;
        end = t + z;
        for (j = 0; j < 4; ++j)
            if (!((p[a + j] >= 'A' && p[a + j] <= 'Z') || (p[a + j] >= 'a' && p[a + j] <= 'z'))) return false;
        if (!xx_rt_memcmp(p + a, "PZXT", 4)) {
            if (z < 2 || p[t] != 1 || p[t + 1] != 0) return false;
        } else if (!xx_rt_memcmp(p + a, "PULS", 4)) {
            signal = true;
            while (t < end) {
                unsigned d, n = 1;
                if (end - t < 2 || !retro_music_poll(b)) return false;
                d = xx_data_get_u16(p + t, 2, 0, false);
                t += 2;
                if (d > 0x8000) {
                    n = d & 0x7fff;
                    if (end - t < 2) return false;
                    d = xx_data_get_u16(p + t, 2, 0, false);
                    t += 2;
                }
                if (d >= 0x8000) {
                    if (end - t < 2) return false;
                    t += 2;
                }
                pulses += n;
                if (pulses > 8000000) return false;
            }
        } else if (!xx_rt_memcmp(p + a, "DATA", 4)) {
            uint32_t bits, header;
            if (z < 8) return false;
            bits = xx_data_get_u32(p + t, 4, 0, false) & 0x7fffffff;
            header = 8 + 2U * (p[t + 6] + p[t + 7]);
            if (header > z || (uint64_t)header + ((uint64_t)bits + 7) / 8 != z) return false;
            signal = true;
        } else if (!xx_rt_memcmp(p + a, "PAUS", 4)) {
            if (z != 4) return false;
        } else if (!xx_rt_memcmp(p + a, "STOP", 4)) {
            if (z != 2) return false;
        }
        xx_rt_snprintf(name, sizeof(name), "chunk-%u-%c%c%c%c.pzx", count - 1, p[a], p[a + 1], p[a + 2], p[a + 3]);
        if (!retro_music_emit(f, s, b, name, a, 8 + z)) return false;
        a = end;
    }
    if (!signal) {
        return false;
    }
    s->size = b->n;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_music_blob b;
    bool ok;
    if (!retro_music_load(f, &b, pd)) return false;
    ok = read_components(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}
void xx_zx_spectrum_pzx_init(xx_zx_spectrum_pzx *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ZX_SPECTRUM_PZX, "zx_spectrum_pzx");
    }
}
xx_zx_spectrum_pzx *xx_zx_spectrum_pzx_create(xx_io_device *d, int64_t b)
{
    xx_zx_spectrum_pzx *r = (xx_zx_spectrum_pzx *)xx_mem_alloc(sizeof(*r));
    if (r) xx_zx_spectrum_pzx_init(r, d, b);
    return r;
}
void xx_zx_spectrum_pzx_destroy(xx_zx_spectrum_pzx *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_zx_spectrum_pzx_free(xx_zx_spectrum_pzx *r)
{
    if (r) {
        xx_zx_spectrum_pzx_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_zx_spectrum_pzx_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_zx_spectrum_pzx_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
