/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/sega_sgc/xx_sega_sgc.h"
#include "../common/xx_retro_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f, pm_stream *s, retro_music_blob *b)
{
    const uint8_t *p = b->p;
    uint32_t load;
    if (b->n <= 160 || b->n > 4194464 || xx_rt_memcmp(p, "SGC\x1a", 4) || p[4] != 1 || p[5] > 1 || p[40] > 2 || !p[37] || (unsigned)p[36] + p[37] > 256 ||
        (p[39] && p[38] > p[39]))
        return false;
    load = xx_data_get_u16(p + 8, 2, 0, false);
    if (p[40] == 2 && (load < 0x8000 || b->n - 160 > 65536 - load)) return false;
    if (p[40] < 2 && load >= 0xc000) return false;
    if (!retro_music_emit(f, s, b, "music-descriptor.bin", 0, 160) || !retro_music_emit(f, s, b, "program.bin", 160, b->n - 160)) {
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
void xx_sega_sgc_init(xx_sega_sgc *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SEGA_SGC, "sega_sgc");
    }
}
xx_sega_sgc *xx_sega_sgc_create(xx_io_device *d, int64_t b)
{
    xx_sega_sgc *r = (xx_sega_sgc *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sega_sgc_init(r, d, b);
    return r;
}
void xx_sega_sgc_destroy(xx_sega_sgc *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sega_sgc_free(xx_sega_sgc *r)
{
    if (r) {
        xx_sega_sgc_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sega_sgc_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sega_sgc_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
