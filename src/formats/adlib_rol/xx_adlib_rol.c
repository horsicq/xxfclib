/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_rol/xx_adlib_rol.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at = 203, start;
    uint32_t voices, i, j, n;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 203) && xx_data_get_u16(b.p, 2, 0, false) == 0 && xx_data_get_u16(b.p + 2, 2, 0, false) == 4 &&
               xx_data_get_u16(b.p + 44, 2, 0, false) && xx_data_get_u16(b.p + 46, 2, 0, false) && b.p[53] <= 1 && music_float32(&b, 197, true) &&
               music_emit(f, s, &b, "descriptor.rol", 0, 201));
    voices = b.p[53] ? 9 : 11;
    n = xx_data_get_u16(b.p + 201, 2, 0, false);
    MUSIC_NEED(n <= 4096 && music_span(&b, at, (uint64_t)n * 6));
    for (i = 0; i < n; ++i) {
        MUSIC_NEED(music_work(&b, 1) && xx_data_get_u16(b.p + (size_t)at + i * 6, 2, 0, false) < 32768 && music_float32(&b, at + i * 6 + 2, true));
        if (i) MUSIC_NEED(xx_data_get_u16(b.p + (size_t)at + i * 6, 2, 0, false) >= xx_data_get_u16(b.p + (size_t)at + (i - 1) * 6, 2, 0, false));
    }
    MUSIC_NEED(music_emit(f, s, &b, "tempo-events.rol", 201, 2 + (uint64_t)n * 6));
    at += (uint64_t)n * 6;
    for (i = 0; i < voices; ++i) {
        uint32_t deadline, total = 0;
        start = at;
        MUSIC_NEED(music_span(&b, at, 17));
        deadline = xx_data_get_u16(b.p + (size_t)at + 15, 2, 0, false);
        MUSIC_NEED(deadline < 32768);
        at += 17;
        while (total < deadline) {
            uint16_t dur, note;
            MUSIC_NEED(music_span(&b, at, 4) && music_work(&b, 1));
            note = xx_data_get_u16(b.p + (size_t)at, 2, 0, false);
            dur = xx_data_get_u16(b.p + (size_t)at + 2, 2, 0, false);
            MUSIC_NEED(note <= 127 && dur && dur <= deadline - total);
            total += dur;
            at += 4;
        }
        MUSIC_NEED(music_emit(f, s, &b, "notes.rol", start, at - start));
        for (j = 0; j < 3; ++j) {
            uint32_t k, previous = 0, width = j ? 6 : 14;
            start = at;
            MUSIC_NEED(music_span(&b, at, 17));
            n = xx_data_get_u16(b.p + (size_t)at + 15, 2, 0, false);
            at += 17;
            MUSIC_NEED(n <= 4096 && music_span(&b, at, (uint64_t)n * width));
            for (k = 0; k < n; ++k) {
                uint32_t time = xx_data_get_u16(b.p + (size_t)at + (uint64_t)k * width, 2, 0, false);
                MUSIC_NEED(music_work(&b, 1) && time < 32768 && time >= previous);
                previous = time;
                if (j) MUSIC_NEED(music_float32(&b, at + (uint64_t)k * width + 2, false));
            }
            at += (uint64_t)n * width;
            MUSIC_NEED(music_emit(f, s, &b, j == 0 ? "instrument-events.rol" : j == 1 ? "volume-events.rol" : "pitch-events.rol", start, at - start));
        }
    }
    MUSIC_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_adlib_rol_init(xx_adlib_rol *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ADLIB_ROL, "adlib_rol");
    }
}
xx_adlib_rol *xx_adlib_rol_create(xx_io_device *d, int64_t b)
{
    xx_adlib_rol *r = (xx_adlib_rol *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adlib_rol_init(r, d, b);
    return r;
}
void xx_adlib_rol_destroy(xx_adlib_rol *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adlib_rol_free(xx_adlib_rol *r)
{
    if (r) {
        xx_adlib_rol_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adlib_rol_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adlib_rol_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
