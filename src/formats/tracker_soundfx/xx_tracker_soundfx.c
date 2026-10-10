/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/tracker_soundfx/xx_tracker_soundfx.h"
#include "../common/xx_disk_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    disk_music_blob b = {0};
    uint32_t ni, i, j, np = 0, no, lens[31];
    uint64_t at;
    bool ok = false;
    DISK_MUSIC_NEED(disk_music_load(f, &b, pd));
    if (disk_music_tag(&b, 60, "SONG", 4)) ni = 15;
    else {
        DISK_MUSIC_NEED(disk_music_tag(&b, 124, "SONG", 4));
        ni = 31;
    }
    at = ni * 4;
    DISK_MUSIC_NEED(disk_music_span(&b, at, 20 + ni * 30 + 130) && xx_data_get_u16(b.p + (size_t)at + 4, 2, 0, true) >= 178);
    for (i = 0; i < ni; ++i) {
        const uint8_t *q = b.p + (size_t)at + 20 + i * 30;
        uint32_t a = xx_data_get_u16(q + 26, 2, 0, true), z = (uint32_t)xx_data_get_u16(q + 28, 2, 0, true) * 2;
        lens[i] = xx_data_get_u32(b.p + i * 4, 4, 0, true);
        DISK_MUSIC_NEED(lens[i] <= 16777216 && q[24] <= 15 && q[25] <= 64);
        if (z > 2) DISK_MUSIC_NEED(a <= lens[i] && z <= lens[i] - a);
    }
    DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "sample-lengths.sfx", 0, ni * 4) && disk_music_emit(f, s, &b, "descriptor.sfx", at, 20) &&
                    disk_music_emit(f, s, &b, "instruments.sfx", at + 20, ni * 30));
    at += 20 + ni * 30;
    no = b.p[(size_t)at];
    DISK_MUSIC_NEED(no && no <= 127 && (b.p[(size_t)at + 1] < no || b.p[(size_t)at + 1] == 127));
    for (i = 0; i < no; ++i) {
        uint32_t v = b.p[(size_t)at + 2 + i];
        DISK_MUSIC_NEED(v < 128);
        if (v >= np) np = v + 1;
    }
    DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "orders.sfx", at, 130));
    at += 130;
    for (i = 0; i < np; ++i) {
        DISK_MUSIC_NEED(disk_music_span(&b, at, 1024));
        for (j = 0; j < 256; ++j) {
            const uint8_t *q = b.p + (size_t)at + j * 4;
            DISK_MUSIC_NEED(disk_music_work(&b, 1) && (uint32_t)((q[0] & 240) | (q[2] >> 4)) <= ni);
        }
        DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "pattern.sfx", at, 1024));
        at += 1024;
    }
    for (i = 0; i < ni; ++i)
        if (lens[i] > 2) {
            DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "sample.pcm8", at, lens[i]));
            at += lens[i];
        }
    DISK_MUSIC_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_tracker_soundfx_init(xx_tracker_soundfx *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRACKER_SOUNDFX, "tracker_soundfx");
    }
}
xx_tracker_soundfx *xx_tracker_soundfx_create(xx_io_device *d, int64_t b)
{
    xx_tracker_soundfx *r = (xx_tracker_soundfx *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tracker_soundfx_init(r, d, b);
    return r;
}
void xx_tracker_soundfx_destroy(xx_tracker_soundfx *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tracker_soundfx_free(xx_tracker_soundfx *r)
{
    if (r) {
        xx_tracker_soundfx_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tracker_soundfx_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tracker_soundfx_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
