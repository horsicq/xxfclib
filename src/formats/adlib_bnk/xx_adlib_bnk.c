/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/adlib_bnk/xx_adlib_bnk.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    music_range ranges[1024];
    unsigned nr = 0;
    uint32_t used, total, list, data, count, i, j, seen = 0;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 20) && b.p[0] == 1 && b.p[1] == 0 && music_tag(&b, 2, "ADLIB-", 6));
    used = xx_data_get_u16(b.p + 8, 2, 0, false);
    total = xx_data_get_u16(b.p + 10, 2, 0, false);
    list = xx_data_get_u32(b.p + 12, 4, 0, false);
    data = xx_data_get_u32(b.p + 16, 4, 0, false);
    MUSIC_NEED(used && used <= total && total <= 1000 && list >= 20 && data >= list + (uint64_t)total * 12 && data <= b.n && (b.n - data) % 30 == 0);
    count = (uint32_t)((b.n - data) / 30);
    MUSIC_NEED(count && count <= 1000);
    MUSIC_NEED(music_claim(&b, ranges, &nr, 0, 20, false) && music_claim(&b, ranges, &nr, list, (uint64_t)total * 12, false) &&
               music_claim(&b, ranges, &nr, data, (uint64_t)count * 30, false) && music_emit(f, s, &b, "descriptor.bnk", 0, 20) &&
               music_emit(f, s, &b, "names.bnk", list, (uint64_t)total * 12));
    for (i = 0; i < total; ++i) {
        const uint8_t *q = b.p + list + i * 12;
        MUSIC_NEED(music_work(&b, 1) && q[2] <= 1 && xx_data_get_u16(q, 2, 0, false) < count);
        if (q[2]) {
            bool z = false;
            MUSIC_NEED(q[3]);
            for (j = 0; j < 9; ++j)
                if (!q[3 + j]) z = true;
            MUSIC_NEED(z);
            ++seen;
        }
    }
    MUSIC_NEED(seen == used);
    for (i = 0; i < count; ++i) {
        const uint8_t *q = b.p + data + i * 30;
        MUSIC_NEED(q[0] <= 1 && q[1] <= 10);
        MUSIC_NEED(music_emit(f, s, &b, "instrument.bnk", data + (uint64_t)i * 30, 30));
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_adlib_bnk_init(xx_adlib_bnk *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ADLIB_BNK, "adlib_bnk");
    }
}
xx_adlib_bnk *xx_adlib_bnk_create(xx_io_device *d, int64_t b)
{
    xx_adlib_bnk *r = (xx_adlib_bnk *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adlib_bnk_init(r, d, b);
    return r;
}
void xx_adlib_bnk_destroy(xx_adlib_bnk *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adlib_bnk_free(xx_adlib_bnk *r)
{
    if (r) {
        xx_adlib_bnk_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adlib_bnk_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adlib_bnk_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
