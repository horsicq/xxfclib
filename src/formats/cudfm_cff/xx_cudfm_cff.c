/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/cudfm_cff/xx_cudfm_cff.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t base = 32, at, stride = 1728;
    uint32_t np, i;
    bool end = false, ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_tag(&b, 0, "<CUD-FM-File>\032\336\340", 16) && music_span(&b, 0, 32) && b.p[16] == 1 && !b.p[19] &&
               music_zero_block(&b, 20, 12) && xx_data_get_u16(b.p + 17, 2, 0, false) == b.n - 32 && music_span(&b, base, 0x669));
    np = b.p[base + 0x5e0];
    MUSIC_NEED(np && np <= 36 && b.n - base == 0x669 + (uint64_t)np * stride && music_tag(&b, base + 0x5e1, "CUD-FM-File - SEND A POSTCARD -", 31) &&
               music_emit(f, s, &b, "descriptor.cff", 0, 32));
    for (i = 0; i < 47; ++i) {
        MUSIC_NEED(music_emit(f, s, &b, "instrument.cff", base + i * 32, 32));
    }
    MUSIC_NEED(music_emit(f, s, &b, "song-info.cff", base + 0x5e0, 72));
    for (i = 0; i < 64; ++i) {
        uint8_t c = b.p[base + 0x628 + i];
        if (c & 128) end = true;
        else MUSIC_NEED(!end && c < np);
    }
    MUSIC_NEED(end && b.p[base + 0x628] < np && music_emit(f, s, &b, "orders.cff", base + 0x628, 65));
    for (i = 0; i < np; ++i) {
        uint64_t j;
        at = base + 0x669 + (uint64_t)i * stride;
        for (j = 0; j < stride; j += 3) {
            uint8_t effect = b.p[(size_t)(at + j) + 1], arg = b.p[(size_t)(at + j) + 2];
            MUSIC_NEED(music_work(&b, 1));
            if (effect == 'I') MUSIC_NEED(arg < 47);
            MUSIC_NEED(!effect || effect == 'I' || effect == 'H' || effect == 'A' || effect == 'L' || effect == 'K' || effect == 'M' || effect == 'C' || effect == 'G' ||
                       effect == 'B' || effect == 'E' || effect == 'F' || effect == 'D' || effect == 'J');
        }
        MUSIC_NEED(music_emit(f, s, &b, "pattern.cff", at, stride));
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_cudfm_cff_init(xx_cudfm_cff *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_CUDFM_CFF, "cudfm_cff");
    }
}
xx_cudfm_cff *xx_cudfm_cff_create(xx_io_device *d, int64_t b)
{
    xx_cudfm_cff *r = (xx_cudfm_cff *)xx_mem_alloc(sizeof(*r));
    if (r) xx_cudfm_cff_init(r, d, b);
    return r;
}
void xx_cudfm_cff_destroy(xx_cudfm_cff *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_cudfm_cff_free(xx_cudfm_cff *r)
{
    if (r) {
        xx_cudfm_cff_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_cudfm_cff_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_cudfm_cff_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
