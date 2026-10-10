/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_sa2/xx_adlib_sa2.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at = 5;
    uint32_t i, np, len, restart, tracks;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_tag(&b, 0, "SAdT\011", 5) && music_span(&b, 0, 2190) && music_emit(f, s, &b, "descriptor.sa2", 0, 5));
    for (i = 0; i < 31; ++i) {
        MUSIC_NEED(music_emit(f, s, &b, "instrument.sa2", at, 15));
        at += 15;
    }
    MUSIC_NEED(music_emit(f, s, &b, "instrument-names.sa2", at, 496));
    at += 496;
    MUSIC_NEED(music_emit(f, s, &b, "orders.sa2", at, 128));
    at += 128;
    np = xx_data_get_u16(b.p + (size_t)at, 2, 0, false);
    len = b.p[(size_t)at + 2];
    restart = b.p[(size_t)at + 3];
    MUSIC_NEED(np && np <= 64 && len && len <= 128 && restart < len && xx_data_get_u16(b.p + (size_t)at + 4, 2, 0, false));
    for (i = 0; i < len; ++i) MUSIC_NEED(b.p[966 + i] < np);
    MUSIC_NEED(music_emit(f, s, &b, "song-info.sa2", at, 6));
    at += 6;
    MUSIC_NEED(music_emit(f, s, &b, "arpeggios.sa2", at, 512));
    at += 512;
    MUSIC_NEED(music_emit(f, s, &b, "track-map.sa2", at, 576));
    MUSIC_NEED(b.n > 2190 && (b.n - 2190) % 192 == 0);
    tracks = (uint32_t)((b.n - 2190) / 192);
    MUSIC_NEED(tracks <= 576);
    for (i = 0; i < len; ++i) {
        unsigned j;
        uint32_t p = b.p[966 + i];
        for (j = 0; j < 9; ++j) MUSIC_NEED(b.p[(size_t)at + p * 9 + j] < tracks);
    }
    at += 576;
    MUSIC_NEED(music_emit(f, s, &b, "active-channels.sa2", at, 2));
    at += 2;
    for (i = 0; i < tracks; ++i) {
        MUSIC_NEED(music_work(&b, 64) && music_emit(f, s, &b, "track.sa2", at, 192));
        at += 192;
    }
    MUSIC_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_adlib_sa2_init(xx_adlib_sa2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ADLIB_SA2, "adlib_sa2");
    }
}
xx_adlib_sa2 *xx_adlib_sa2_create(xx_io_device *d, int64_t b)
{
    xx_adlib_sa2 *r = (xx_adlib_sa2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adlib_sa2_init(r, d, b);
    return r;
}
void xx_adlib_sa2_destroy(xx_adlib_sa2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adlib_sa2_free(xx_adlib_sa2 *r)
{
    if (r) {
        xx_adlib_sa2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adlib_sa2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adlib_sa2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
