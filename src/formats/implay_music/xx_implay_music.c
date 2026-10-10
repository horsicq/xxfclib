/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/implay_music/xx_implay_music.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint64_t at = 70, end, start;
    uint32_t commands = 0, declared, ni = 0;
    uint8_t running = 0;
    bool stop = false, ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 70) && b.p[0] == 1 && !b.p[1] && !xx_data_get_u32(b.p + 2, 4, 0, false) && b.p[36] && b.p[37] &&
               xx_data_get_u32(b.p + 38, 4, 0, false) && xx_data_get_u16(b.p + 60, 2, 0, false) && b.p[58] <= 1 && b.p[59] <= 12 && music_zero_block(&b, 50, 8) &&
               music_zero_block(&b, 62, 8));
    declared = xx_data_get_u32(b.p + 46, 4, 0, false);
    end = 70 + (uint64_t)xx_data_get_u32(b.p + 42, 4, 0, false);
    MUSIC_NEED(declared && declared <= 1000000 && end > 70 && end <= b.n && music_emit(f, s, &b, "descriptor.ims", 0, 70));
    if (end < b.n) {
        uint64_t n;
        MUSIC_NEED(music_span(&b, end, 4) && xx_data_get_u16(b.p + (size_t)end, 2, 0, false) == 0x7777);
        ni = xx_data_get_u16(b.p + (size_t)end + 2, 2, 0, false);
        n = 4 + (uint64_t)ni * 9;
        MUSIC_NEED(ni && ni <= 128 && b.n - end == n);
    }
    while (at < end) {
        uint8_t c;
        unsigned n, j;
        MUSIC_NEED(music_work(&b, 1));
        while (at < end && b.p[(size_t)at] == 248) {
            MUSIC_NEED(music_work(&b, 1));
            ++at;
        }
        MUSIC_NEED(at < end);
        ++at;
        MUSIC_NEED(at < end);
        c = b.p[(size_t)at];
        if (c >= 128) ++at;
        else {
            MUSIC_NEED(running);
            c = running;
        }
        ++commands;
        if (c == 252) {
            MUSIC_NEED(at == end);
            stop = true;
            break;
        }
        if (c == 240) {
            start = at;
            while (at < end && b.p[(size_t)at] != 247) {
                MUSIC_NEED(music_work(&b, 1) && b.p[(size_t)at] < 128 && at - start < 4096);
                ++at;
            }
            MUSIC_NEED(at < end);
            ++at;
            continue;
        }
        MUSIC_NEED(c >= 128 && c < 240 && (c & 15) < 11);
        running = c;
        n = (c & 240) == 160 || (c & 240) == 192 || (c & 240) == 208 ? 1 : 2;
        MUSIC_NEED(at <= end && n <= end - at);
        for (j = 0; j < n; ++j) MUSIC_NEED(b.p[(size_t)at + j] < 128);
        if (ni && (c & 240) == 192) MUSIC_NEED(b.p[(size_t)at] < ni);
        at += n;
    }
    MUSIC_NEED(stop && commands == declared && music_emit(f, s, &b, "timed-events.ims", 70, end - 70));
    if (end < b.n) MUSIC_NEED(music_emit(f, s, &b, "timbre-names.ims", end, b.n - end));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_implay_music_init(xx_implay_music *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_IMPLAY_MUSIC, "implay_music");
    }
}
xx_implay_music *xx_implay_music_create(xx_io_device *d, int64_t b)
{
    xx_implay_music *r = (xx_implay_music *)xx_mem_alloc(sizeof(*r));
    if (r) xx_implay_music_init(r, d, b);
    return r;
}
void xx_implay_music_destroy(xx_implay_music *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_implay_music_free(xx_implay_music *r)
{
    if (r) {
        xx_implay_music_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_implay_music_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_implay_music_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
