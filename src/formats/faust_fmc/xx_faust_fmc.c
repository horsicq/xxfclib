/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/faust_fmc/xx_faust_fmc.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at = 284, stride;
    uint32_t ch, np, i;
    bool ended = false, ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_tag(&b, 0, "FMC!", 4) && music_span(&b, 0, 1820));
    ch = b.p[25];
    MUSIC_NEED(ch && ch <= 32);
    stride = (uint64_t)ch * 192;
    MUSIC_NEED(b.n > 1820 && (b.n - 1820) % stride == 0);
    np = (uint32_t)((b.n - 1820) / stride);
    MUSIC_NEED(np <= 64 && music_emit(f, s, &b, "descriptor.fmc", 0, 26) && music_emit(f, s, &b, "orders.fmc", 26, 258));
    for (i = 0; i < 256; ++i) {
        uint8_t v = b.p[26 + i];
        if (v >= 254) {
            ended = true;
            break;
        }
        MUSIC_NEED(v < np);
    }
    MUSIC_NEED(ended && b.p[26] < np);
    for (i = 0; i < 32; ++i) {
        MUSIC_NEED(music_emit(f, s, &b, "instrument.fmc", at, 48));
        at += 48;
    }
    for (i = 0; i < np; ++i) {
        MUSIC_NEED(music_work(&b, (uint64_t)ch * 64) && music_emit(f, s, &b, "pattern.fmc", at, stride));
        at += stride;
    }
    MUSIC_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_faust_fmc_init(xx_faust_fmc *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_FAUST_FMC, "faust_fmc");
    }
}
xx_faust_fmc *xx_faust_fmc_create(xx_io_device *d, int64_t b)
{
    xx_faust_fmc *r = (xx_faust_fmc *)xx_mem_alloc(sizeof(*r));
    if (r) xx_faust_fmc_init(r, d, b);
    return r;
}
void xx_faust_fmc_destroy(xx_faust_fmc *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_faust_fmc_free(xx_faust_fmc *r)
{
    if (r) {
        xx_faust_fmc_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_faust_fmc_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_faust_fmc_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
