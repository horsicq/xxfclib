/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/adlib_rad/xx_adlib_rad.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    music_range ranges[1024];
    unsigned nr = 0;
    uint64_t at = 18, start, end = 0;
    uint32_t last = 0, i, no;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_tag(&b, 0, "RAD by REALiTY!!", 16) && music_span(&b, 0, 18) && b.p[16] == 0x10 && !(b.p[17] & 0x20));
    if (b.p[17] & 128) MUSIC_NEED(music_zstring_any(&b, &at, b.n, 65536));
    MUSIC_NEED(music_emit(f, s, &b, "descriptor.rad", 0, at));
    for (;;) {
        uint8_t id;
        MUSIC_NEED(music_span(&b, at, 1));
        start = at;
        id = b.p[(size_t)at++];
        if (!id) break;
        MUSIC_NEED(id <= 31 && id > last && music_span(&b, at, 11) && music_emit(f, s, &b, "instrument.rad", start, 12));
        last = id;
        at += 11;
    }
    start = at;
    MUSIC_NEED(music_span(&b, at, 1));
    no = b.p[(size_t)at++];
    MUSIC_NEED(no && no <= 128 && music_span(&b, at, (uint64_t)no + 64));
    for (i = 0; i < no; ++i) {
        uint8_t v = b.p[(size_t)(at + i)];
        MUSIC_NEED((v & 128) ? (v & 127U) < no : v < 32);
    }
    at += no;
    MUSIC_NEED(music_emit(f, s, &b, "orders.rad", start, (uint64_t)no + 1));
    start = at;
    at += 64;
    MUSIC_NEED(music_claim(&b, ranges, &nr, 0, at, false) && music_emit(f, s, &b, "pattern-offsets.rad", start, 64));
    for (i = 0; i < 32; ++i) {
        uint64_t p = xx_data_get_u16(b.p + (size_t)start + i * 2, 2, 0, false), a = p;
        uint32_t line = 0, lastline = 0;
        bool first = true;
        if (!p) continue;
        MUSIC_NEED(p >= at);
        for (;;) {
            uint8_t l, c;
            MUSIC_NEED(music_span(&b, p, 1) && music_work(&b, 1));
            l = b.p[(size_t)p++];
            line = l & 127;
            MUSIC_NEED(line < 64 && (first || line > lastline));
            lastline = line;
            first = false;
            for (;;) {
                uint8_t inst;
                MUSIC_NEED(music_span(&b, p, 3) && music_work(&b, 1));
                c = b.p[(size_t)p++];
                MUSIC_NEED((c & 15) < 9 && !(c & 0x70));
                ++p;
                inst = b.p[(size_t)p++];
                MUSIC_NEED((inst >> 4) <= 31);
                if (inst & 15) {
                    MUSIC_NEED(music_span(&b, p, 1));
                    ++p;
                }
                if (c & 128) break;
            }
            if (l & 128) break;
        }
        MUSIC_NEED(music_claim(&b, ranges, &nr, a, p - a, false) && music_emit(f, s, &b, "pattern.rad", a, p - a));
        if (p > end) end = p;
    }
    MUSIC_NEED(end == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_adlib_rad_init(xx_adlib_rad *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ADLIB_RAD, "adlib_rad");
    }
}
xx_adlib_rad *xx_adlib_rad_create(xx_io_device *d, int64_t b)
{
    xx_adlib_rad *r = (xx_adlib_rad *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adlib_rad_init(r, d, b);
    return r;
}
void xx_adlib_rad_destroy(xx_adlib_rad *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adlib_rad_free(xx_adlib_rad *r)
{
    if (r) {
        xx_adlib_rad_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adlib_rad_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adlib_rad_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
