/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/mad_tracker/xx_mad_tracker.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at = 4;
    uint32_t np, no, i, j;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_tag(&b, 0, "MAD+", 4) && music_span(&b, 0, 188) && !b.p[184] && b.p[187]);
    no = b.p[185];
    np = b.p[186];
    MUSIC_NEED(no && np && np <= 64 && b.n == 188 + (uint64_t)np * 288 + no && music_emit(f, s, &b, "descriptor.mad", 0, 4));
    for (i = 0; i < 9; ++i) {
        MUSIC_NEED(music_emit(f, s, &b, "instrument.mad", at, 20));
        at += 20;
    }
    MUSIC_NEED(music_emit(f, s, &b, "song-info.mad", at, 4));
    at += 4;
    for (i = 0; i < np; ++i) {
        for (j = 0; j < 288; ++j) {
            uint8_t v = b.p[(size_t)at + j];
            MUSIC_NEED(music_work(&b, 1) && (v <= 96 || v == 254 || v == 255));
        }
        MUSIC_NEED(music_emit(f, s, &b, "pattern.mad", at, 288));
        at += 288;
    }
    for (i = 0; i < no; ++i) MUSIC_NEED(b.p[(size_t)at + i] && b.p[(size_t)at + i] <= np);
    MUSIC_NEED(music_emit(f, s, &b, "orders.mad", at, no));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_mad_tracker_init(xx_mad_tracker *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MAD_TRACKER, "mad_tracker");
    }
}
xx_mad_tracker *xx_mad_tracker_create(xx_io_device *d, int64_t b)
{
    xx_mad_tracker *r = (xx_mad_tracker *)xx_mem_alloc(sizeof(*r));
    if (r) xx_mad_tracker_init(r, d, b);
    return r;
}
void xx_mad_tracker_destroy(xx_mad_tracker *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_mad_tracker_free(xx_mad_tracker *r)
{
    if (r) {
        xx_mad_tracker_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_mad_tracker_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_mad_tracker_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
