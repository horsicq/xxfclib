/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/adlib_amd/xx_adlib_amd.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at = 1072, start;
    uint32_t np, no, i, j, k, nt, rows, id;
    bool tracks[576] = {0}, ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 1072) && (music_tag(&b, 1062, "<o\xefQU\xeeRoR", 9) || music_tag(&b, 1062, "MaDoKaN96", 9)));
    no = b.p[932];
    np = (uint32_t)b.p[933] + 1;
    MUSIC_NEED(no && no <= 128 && np <= 64 && (b.p[1071] == 0x10 || b.p[1071] == 0x11));
    for (i = 0; i < no; ++i) MUSIC_NEED((b.p[934 + i] & 127U) < np);
    MUSIC_NEED(music_emit(f, s, &b, "descriptor.amd", 0, 48) && music_emit(f, s, &b, "instruments.amd", 48, 884) && music_emit(f, s, &b, "orders.amd", 932, 140));
    if (b.p[1071] == 0x10) {
        MUSIC_NEED(music_span(&b, at, (uint64_t)np * 1728));
        for (i = 0; i < np; ++i) {
            for (j = 0; j < 576; ++j) {
                const uint8_t *q = b.p + (size_t)at + j * 3;
                MUSIC_NEED(music_work(&b, 1) && ((q[1] >> 4) | ((q[2] & 1) << 4)) < 26);
            }
            MUSIC_NEED(music_emit(f, s, &b, "pattern.amd", at, 1728));
            at += 1728;
        }
    } else {
        uint64_t table = at;
        MUSIC_NEED(music_span(&b, at, (uint64_t)np * 18 + 2));
        at += (uint64_t)np * 18;
        nt = xx_data_get_u16(b.p + (size_t)at, 2, 0, false);
        at += 2;
        MUSIC_NEED(nt && nt <= 576 && music_emit(f, s, &b, "track-map.amd", table, (uint64_t)np * 18 + 2));
        for (k = 0; k < nt; ++k) {
            start = at;
            MUSIC_NEED(music_span(&b, at, 2));
            id = xx_data_get_u16(b.p + (size_t)at, 2, 0, false);
            at += 2;
            MUSIC_NEED(id < 576 && !tracks[id]);
            tracks[id] = true;
            rows = 0;
            while (rows < 64) {
                uint8_t v;
                MUSIC_NEED(music_span(&b, at, 1) && music_work(&b, 1));
                v = b.p[(size_t)at++];
                if (v & 128) {
                    MUSIC_NEED((v & 127) > 0 && (v & 127U) <= 64U - rows);
                    rows += v & 127;
                } else {
                    MUSIC_NEED(music_span(&b, at, 2) && ((b.p[(size_t)at] >> 4) | ((b.p[(size_t)at + 1] & 1) << 4)) < 26);
                    at += 2;
                    ++rows;
                }
            }
            MUSIC_NEED(music_emit(f, s, &b, "track.amd", start, at - start));
        }
        for (i = 0; i < np * 9; ++i) {
            id = xx_data_get_u16(b.p + (size_t)table + i * 2, 2, 0, false);
            MUSIC_NEED(id < 576 && tracks[id]);
        }
    }
    MUSIC_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_adlib_amd_init(xx_adlib_amd *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ADLIB_AMD, "adlib_amd");
    }
}
xx_adlib_amd *xx_adlib_amd_create(xx_io_device *d, int64_t b)
{
    xx_adlib_amd *r = (xx_adlib_amd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adlib_amd_init(r, d, b);
    return r;
}
void xx_adlib_amd_destroy(xx_adlib_amd *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adlib_amd_free(xx_adlib_amd *r)
{
    if (r) {
        xx_adlib_amd_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adlib_amd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adlib_amd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
