/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/dosbox_dro/xx_dosbox_dro.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at, size, end, duration = 0;
    uint32_t count, ms, i;
    uint8_t shortdelay, longdelay, map;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 26) && music_tag(&b, 0, "DBRAWOPL", 8) && xx_data_get_u16(b.p + 8, 2, 0, false) == 2 &&
               xx_data_get_u16(b.p + 10, 2, 0, false) == 0);
    count = xx_data_get_u32(b.p + 12, 4, 0, false);
    ms = xx_data_get_u32(b.p + 16, 4, 0, false);
    shortdelay = b.p[23];
    longdelay = b.p[24];
    map = b.p[25];
    MUSIC_NEED(count && count <= 1000000 && b.p[20] <= 2 && !b.p[21] && !b.p[22] && map && map <= 128 && shortdelay != longdelay && shortdelay >= map &&
               longdelay >= map && music_span(&b, 26, map));
    at = 26 + map;
    size = (uint64_t)count * 2;
    MUSIC_NEED(music_span(&b, at, size));
    end = at + size;
    for (i = 0; i < count; ++i) {
        const uint8_t *q = b.p + (size_t)at + i * 2;
        MUSIC_NEED(music_work(&b, 1));
        if (q[0] == shortdelay) duration += (uint64_t)q[1] + 1;
        else if (q[0] == longdelay) duration += ((uint64_t)q[1] + 1) * 256;
        else MUSIC_NEED((q[0] & 127) < map && (b.p[20] != 0 || !(q[0] & 128)));
    }
    MUSIC_NEED(duration == ms);
    MUSIC_NEED(music_emit(f, s, &b, "descriptor.dro", 0, 26) && music_emit(f, s, &b, "register-map.dro", 26, map) && music_emit(f, s, &b, "register-data.dro", at, size));
    if (end < b.n) {
        uint64_t p = end;
        MUSIC_NEED(music_tag(&b, p, "\xff\xff\x1a", 3));
        p += 3;
        MUSIC_NEED(music_zstring_any(&b, &p, b.n, 41));
        if (p < b.n && b.p[(size_t)p] == 0x1b) {
            ++p;
            MUSIC_NEED(music_zstring_any(&b, &p, b.n, 41));
        }
        if (p < b.n) {
            MUSIC_NEED(b.p[(size_t)p++] == 0x1c && music_zstring_any(&b, &p, b.n, 1024));
        }
        MUSIC_NEED(p == b.n && music_emit(f, s, &b, "tags.dro", end, b.n - end));
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_dosbox_dro_init(xx_dosbox_dro *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DOSBOX_DRO, "dosbox_dro");
    }
}
xx_dosbox_dro *xx_dosbox_dro_create(xx_io_device *d, int64_t b)
{
    xx_dosbox_dro *r = (xx_dosbox_dro *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dosbox_dro_init(r, d, b);
    return r;
}
void xx_dosbox_dro_destroy(xx_dosbox_dro *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dosbox_dro_free(xx_dosbox_dro *r)
{
    if (r) {
        xx_dosbox_dro_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dosbox_dro_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dosbox_dro_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
