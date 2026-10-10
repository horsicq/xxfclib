/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/hxc_mfm/xx_hxc_mfm.h"
#include "../common/xx_disk_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    disk_music_blob b = {0};
    disk_music_range ranges[1024];
    unsigned nr = 0;
    uint32_t tracks, sides, list, count, i;
    uint64_t maximum;
    bool ok = false;
    DISK_MUSIC_NEED(disk_music_load(f, &b, pd) && disk_music_tag(&b, 0, "HXCMFM\0", 7) && disk_music_span(&b, 0, 19));
    tracks = xx_data_get_u16(b.p + 7, 2, 0, false);
    sides = b.p[9];
    list = xx_data_get_u32(b.p + 15, 4, 0, false);
    count = tracks * sides;
    DISK_MUSIC_NEED(tracks && tracks <= 170 && sides && sides <= 2 &&
                    (xx_data_get_u16(b.p + 10, 2, 0, false) == 0 || (xx_data_get_u16(b.p + 10, 2, 0, false) >= 150 && xx_data_get_u16(b.p + 10, 2, 0, false) <= 600)) &&
                    xx_data_get_u16(b.p + 12, 2, 0, false) >= 125 && xx_data_get_u16(b.p + 12, 2, 0, false) <= 1000 && b.p[14] <= 15 && list == 19);
    DISK_MUSIC_NEED(disk_music_claim(&b, ranges, &nr, 0, 19 + (uint64_t)count * 11) && disk_music_emit(f, s, &b, "descriptor.mfm", 0, 19) &&
                    disk_music_emit(f, s, &b, "track-index.mfm", 19, (uint64_t)count * 11));
    maximum = 19 + (uint64_t)count * 11;
    for (i = 0; i < count; ++i) {
        const uint8_t *q = b.p + 19 + i * 11;
        uint32_t len = xx_data_get_u32(q + 3, 4, 0, false), off = xx_data_get_u32(q + 7, 4, 0, false);
        DISK_MUSIC_NEED(xx_data_get_u16(q, 2, 0, false) == i / sides && q[2] == i % sides && len >= 128 && len <= 1048576 && off >= 19 + (uint64_t)count * 11 &&
                        disk_music_claim(&b, ranges, &nr, off, len) && disk_music_emit(f, s, &b, "track-bitcells.mfm", off, len));
        if ((uint64_t)off + len > maximum) maximum = (uint64_t)off + len;
    }
    DISK_MUSIC_NEED(maximum == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_hxc_mfm_init(xx_hxc_mfm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_HXC_MFM, "hxc_mfm");
    }
}
xx_hxc_mfm *xx_hxc_mfm_create(xx_io_device *d, int64_t b)
{
    xx_hxc_mfm *r = (xx_hxc_mfm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_hxc_mfm_init(r, d, b);
    return r;
}
void xx_hxc_mfm_destroy(xx_hxc_mfm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_hxc_mfm_free(xx_hxc_mfm *r)
{
    if (r) {
        xx_hxc_mfm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_hxc_mfm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_hxc_mfm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
