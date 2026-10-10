/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/softstar_rix/xx_softstar_rix.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at, music;
    uint32_t ni, i;
    bool ended = false, ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 20) && xx_data_get_u16(b.p, 2, 0, false) == 0x55aa && b.p[2] <= 1 &&
               xx_data_get_u16(b.p + 8, 2, 0, false) == 20);
    music = xx_data_get_u16(b.p + 12, 2, 0, false);
    MUSIC_NEED(music > 20 && (music - 20) % 64 == 0 && music < b.n && !((b.n - music) & 1));
    ni = (uint32_t)((music - 20) / 64);
    MUSIC_NEED(ni <= 256 && music_emit(f, s, &b, "descriptor.rix", 0, 20));
    for (i = 0; i < ni; ++i) {
        at = 20 + (uint64_t)i * 64;
        MUSIC_NEED(music_emit(f, s, &b, "instrument.rix", at, 64));
    }
    for (at = music; at < b.n; at += 2) {
        uint8_t lo = b.p[(size_t)at], hi = b.p[(size_t)at + 1];
        MUSIC_NEED(music_work(&b, 1));
        if (hi == 128) {
            MUSIC_NEED(!lo && at + 2 == b.n);
            ended = true;
            break;
        }
        if (hi < 128) continue;
        MUSIC_NEED((hi & 15) < 11 && (hi & 240) >= 144 && (hi & 240) <= 192);
        if ((hi & 240) == 144) MUSIC_NEED(lo < ni);
    }
    MUSIC_NEED(ended && music_emit(f, s, &b, "event-stream.rix", music, b.n - music));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_softstar_rix_init(xx_softstar_rix *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SOFTSTAR_RIX, "softstar_rix");
    }
}
xx_softstar_rix *xx_softstar_rix_create(xx_io_device *d, int64_t b)
{
    xx_softstar_rix *r = (xx_softstar_rix *)xx_mem_alloc(sizeof(*r));
    if (r) xx_softstar_rix_init(r, d, b);
    return r;
}
void xx_softstar_rix_destroy(xx_softstar_rix *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_softstar_rix_free(xx_softstar_rix *r)
{
    if (r) {
        xx_softstar_rix_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_softstar_rix_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_softstar_rix_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
