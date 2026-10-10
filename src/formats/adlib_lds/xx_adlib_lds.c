/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_lds/xx_adlib_lds.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at = 17, positions, patterns, start;
    uint32_t patches, np, i, len;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 17) && b.p[0] <= 2 && xx_data_get_u16(b.p + 1, 2, 0, false) && b.p[3] && b.p[4]);
    patches = xx_data_get_u16(b.p + 15, 2, 0, false);
    len = b.p[4];
    MUSIC_NEED(patches && patches <= 64 && music_emit(f, s, &b, "descriptor.lds", 0, 17));
    for (i = 0; i < patches; ++i) {
        MUSIC_NEED(music_emit(f, s, &b, "patch.lds", at, 46));
        at += 46;
    }
    MUSIC_NEED(music_span(&b, at, 2));
    start = at;
    np = xx_data_get_u16(b.p + (size_t)at, 2, 0, false);
    at += 2;
    MUSIC_NEED(np && np <= 256 && music_span(&b, at, (uint64_t)np * 27 + 2));
    positions = at;
    at += (uint64_t)np * 27 + 2;
    patterns = at;
    MUSIC_NEED(at < b.n && !((b.n - at) & 1) && music_emit(f, s, &b, "position-table.lds", start, at - start));
    for (i = 0; i < np * 9; ++i) {
        uint64_t off = xx_data_get_u16(b.p + (size_t)positions + i * 3, 2, 0, false), p;
        unsigned rows = 0;
        MUSIC_NEED(!(off & 1) && off < b.n - patterns);
        p = patterns + off;
        while (rows < len) {
            uint16_t cmd;
            uint8_t hi, lo;
            MUSIC_NEED(music_span(&b, p, 2) && music_work(&b, 1));
            cmd = xx_data_get_u16(b.p + (size_t)p, 2, 0, false);
            p += 2;
            hi = (uint8_t)(cmd >> 8);
            lo = (uint8_t)cmd;
            if (hi == 128) {
                MUSIC_NEED((unsigned)lo + 1 <= len - rows);
                rows += (unsigned)lo + 1;
                continue;
            }
            ++rows;
            if (hi == 249) {
                MUSIC_NEED(lo < np);
                break;
            }
            if (hi == 250 || hi == 252) break;
            MUSIC_NEED(hi < 160 || hi >= 240);
            if (hi < 128 && cmd) {
                uint8_t tr = b.p[(size_t)positions + i * 3 + 2];
                int transpose = (int)(tr & 127);
                uint32_t idx;
                if (tr & 64) transpose -= 128;
                idx = (uint32_t)((int)lo + ((tr & 128) ? transpose : 0)) & 63U;
                MUSIC_NEED(idx < patches);
            }
        }
    }
    MUSIC_NEED(music_emit(f, s, &b, "pattern-words.lds", patterns, b.n - patterns));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_adlib_lds_init(xx_adlib_lds *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ADLIB_LDS, "adlib_lds");
    }
}
xx_adlib_lds *xx_adlib_lds_create(xx_io_device *d, int64_t b)
{
    xx_adlib_lds *r = (xx_adlib_lds *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adlib_lds_init(r, d, b);
    return r;
}
void xx_adlib_lds_destroy(xx_adlib_lds *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adlib_lds_free(xx_adlib_lds *r)
{
    if (r) {
        xx_adlib_lds_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adlib_lds_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adlib_lds_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
