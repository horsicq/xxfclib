/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/tracker_dtm/xx_tracker_dtm.h"
#include "../common/xx_disk_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    disk_music_blob b = {0};
    uint64_t at = 0;
    uint32_t ch = 0, np = 0, ni = 0, no = 0, mode = 0, lens[63] = {0};
    uint8_t pats[256] = {0}, samples[63] = {0};
    bool orders = false, inst = false, ok = false;
    unsigned seen = 0, i;
    DISK_MUSIC_NEED(disk_music_load(f, &b, pd) && disk_music_tag(&b, 0, "D.T.", 4));
    while (at < b.n) {
        uint64_t q = at + 8;
        uint32_t n;
        DISK_MUSIC_NEED(disk_music_work(&b, 1) && disk_music_span(&b, at, 8));
        n = xx_data_get_u32(b.p + (size_t)at + 4, 4, 0, true);
        DISK_MUSIC_NEED(disk_music_span(&b, q, n));
        if (disk_music_tag(&b, at, "D.T.", 4)) {
            DISK_MUSIC_NEED(!at && n >= 14 && n <= 142 && xx_data_get_u16(b.p + (size_t)q, 2, 0, true) == 0 && (b.p[(size_t)q + 2] == 0 || b.p[(size_t)q + 2] == 255) &&
                            (b.p[(size_t)q + 3] == 0 || b.p[(size_t)q + 3] == 8 || b.p[(size_t)q + 3] == 16));
            seen |= 1;
        } else if (disk_music_tag(&b, at, "S.Q.", 4)) {
            DISK_MUSIC_NEED(!orders && n >= 9 && n <= 264);
            no = xx_data_get_u16(b.p + (size_t)q, 2, 0, true);
            DISK_MUSIC_NEED(no && no <= 256 && n >= 8 + no && xx_data_get_u16(b.p + (size_t)q + 2, 2, 0, true) < no);
            orders = true;
            seen |= 2;
        } else if (disk_music_tag(&b, at, "PATT", 4)) {
            DISK_MUSIC_NEED(!np && n == 8);
            ch = xx_data_get_u16(b.p + (size_t)q, 2, 0, true);
            np = xx_data_get_u16(b.p + (size_t)q + 2, 2, 0, true);
            mode = xx_data_get_u32(b.p + (size_t)q + 4, 4, 0, true);
            DISK_MUSIC_NEED(ch && ch <= 32 && np && np <= 256 && (mode == 0 || mode == 0x322e3034U));
            seen |= 4;
        } else if (disk_music_tag(&b, at, "INST", 4)) {
            DISK_MUSIC_NEED(!inst && n >= 52);
            ni = xx_data_get_u16(b.p + (size_t)q, 2, 0, true);
            DISK_MUSIC_NEED(ni && ni <= 63 && n == 2 + ni * 50);
            for (i = 0; i < ni; ++i) {
                const uint8_t *v = b.p + (size_t)q + 2 + i * 50;
                uint32_t x = xx_data_get_u32(v + 10, 4, 0, true), z = xx_data_get_u32(v + 14, 4, 0, true);
                lens[i] = xx_data_get_u32(v + 4, 4, 0, true);
                DISK_MUSIC_NEED(disk_music_work(&b, 1) && lens[i] <= 16777216 && v[9] <= 64 && v[40] <= 1 && (v[41] == 0 || v[41] == 8 || v[41] == 16));
                if (z > 2) DISK_MUSIC_NEED(x <= lens[i] && z <= lens[i] - x);
            }
            inst = true;
            seen |= 8;
        } else if (disk_music_tag(&b, at, "DAPT", 4)) {
            uint32_t k, rows, j;
            DISK_MUSIC_NEED(np && inst && n >= 8 && xx_data_get_u32(b.p + (size_t)q, 4, 0, true) == 0xffffffffU);
            k = xx_data_get_u16(b.p + (size_t)q + 4, 2, 0, true);
            rows = xx_data_get_u16(b.p + (size_t)q + 6, 2, 0, true);
            DISK_MUSIC_NEED(k < np && !pats[k] && rows && rows <= 96 && n == 8 + rows * ch * 4);
            pats[k] = 1;
            for (j = 0; j < rows * ch; ++j) {
                const uint8_t *v = b.p + (size_t)q + 8 + j * 4;
                unsigned ins = mode ? ((v[1] & 3) << 4) + (v[2] >> 4) : (v[0] & 240) + (v[2] >> 4);
                DISK_MUSIC_NEED(disk_music_work(&b, 1) && ins <= ni);
                if (mode) DISK_MUSIC_NEED(v[0] <= 0x7c && ((v[0] & 15) <= 12));
            }
        } else if (disk_music_tag(&b, at, "DAIT", 4)) {
            uint32_t k;
            DISK_MUSIC_NEED(inst && n >= 2);
            k = xx_data_get_u16(b.p + (size_t)q, 2, 0, true);
            DISK_MUSIC_NEED(k < ni && !samples[k] && n == 2 + lens[k]);
            samples[k] = 1;
        } else DISK_MUSIC_NEED(false);
        DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "chunk.dtm", at, 8 + n));
        at = q + n;
    }
    DISK_MUSIC_NEED(seen == 15);
    for (i = 0; i < np; ++i) DISK_MUSIC_NEED(pats[i]);
    for (i = 0; i < ni; ++i) DISK_MUSIC_NEED(samples[i]);
    at = 0;
    while (at < b.n) {
        uint32_t n = xx_data_get_u32(b.p + (size_t)at + 4, 4, 0, true);
        if (disk_music_tag(&b, at, "S.Q.", 4))
            for (i = 0; i < no; ++i) DISK_MUSIC_NEED(b.p[(size_t)at + 16 + i] < np);
        at += 8 + n;
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_tracker_dtm_init(xx_tracker_dtm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRACKER_DTM, "tracker_dtm");
    }
}
xx_tracker_dtm *xx_tracker_dtm_create(xx_io_device *d, int64_t b)
{
    xx_tracker_dtm *r = (xx_tracker_dtm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tracker_dtm_init(r, d, b);
    return r;
}
void xx_tracker_dtm_destroy(xx_tracker_dtm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tracker_dtm_free(xx_tracker_dtm *r)
{
    if (r) {
        xx_tracker_dtm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tracker_dtm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tracker_dtm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
