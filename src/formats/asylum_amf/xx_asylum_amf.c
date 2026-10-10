/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/asylum_amf/xx_asylum_amf.h"
#include "../common/xx_disk_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    disk_music_blob b = {0};
    uint64_t at;
    uint32_t ni, np, no, i, j, len[64];
    bool ok = false;
    DISK_MUSIC_NEED(disk_music_load(f, &b, pd) && disk_music_tag(&b, 0, "ASYLUM Music Format V1.0\0\0\0\0\0\0\0\0", 32) && disk_music_span(&b, 0, 2662));
    ni = b.p[34];
    np = b.p[35];
    no = b.p[36];
    DISK_MUSIC_NEED(b.p[32] && b.p[32] <= 31 && b.p[33] >= 32 && ni && ni <= 64 && np && no && b.p[37] < no);
    for (i = 0; i < no; ++i) DISK_MUSIC_NEED(b.p[38 + i] < np);
    DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "descriptor.amf", 0, 38) && disk_music_emit(f, s, &b, "orders.amf", 38, 256));
    for (i = 0; i < ni; ++i) {
        const uint8_t *q = b.p + 294 + i * 37;
        uint32_t a = xx_data_get_u32(q + 29, 4, 0, false), z = xx_data_get_u32(q + 33, 4, 0, false);
        len[i] = xx_data_get_u32(q + 25, 4, 0, false);
        DISK_MUSIC_NEED(len[i] < 131072 && q[22] <= 15 && q[23] <= 64 && a <= len[i] && z <= len[i] - a);
    }
    DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "instruments.amf", 294, 2368));
    at = 2662;
    for (i = 0; i < np; ++i) {
        DISK_MUSIC_NEED(disk_music_span(&b, at, 2048));
        for (j = 0; j < 512; ++j) {
            const uint8_t *q = b.p + (size_t)at + j * 4;
            DISK_MUSIC_NEED(disk_music_work(&b, 1) && q[0] <= 114 && q[1] <= ni);
        }
        DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "pattern.amf", at, 2048));
        at += 2048;
    }
    for (i = 0; i < ni; ++i)
        if (len[i] > 1) {
            DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "sample.pcm8", at, len[i]));
            at += len[i];
        }
    DISK_MUSIC_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_asylum_amf_init(xx_asylum_amf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ASYLUM_AMF, "asylum_amf");
    }
}
xx_asylum_amf *xx_asylum_amf_create(xx_io_device *d, int64_t b)
{
    xx_asylum_amf *r = (xx_asylum_amf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_asylum_amf_init(r, d, b);
    return r;
}
void xx_asylum_amf_destroy(xx_asylum_amf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_asylum_amf_free(xx_asylum_amf *r)
{
    if (r) {
        xx_asylum_amf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_asylum_amf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_asylum_amf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
