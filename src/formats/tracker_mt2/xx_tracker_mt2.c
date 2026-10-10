/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_mt2/xx_tracker_mt2.h"
#include "../common/xx_audio_components.h"
#ifndef TRACKER_MT2
#define XX_FILE_TYPE_TRACKER_MT2 ((xx_file_type_t)812)
#endif
static bool audio_component_parse(audio_component_blob *c)
{
    size_t p = 388;
    unsigned patterns, orders, chn, i;
    uint16_t version;
    if (!audio_component_range(c, 0, 388) || !audio_component_eq(c, 0, "MT20", 4) || (version = xx_data_get_u16(c->b + 8, 2, 0, false)) < 0x200 || version > 0x201 ||
        !(orders = xx_data_get_u16(c->b + 106, 2, 0, false)) || orders > 256 || xx_data_get_u16(c->b + 108, 2, 0, false) >= orders ||
        !(patterns = xx_data_get_u16(c->b + 110, 2, 0, false)) || patterns > 256 || !(chn = xx_data_get_u16(c->b + 112, 2, 0, false)) || chn > 64 ||
        xx_data_get_u32(c->b + 118, 4, 0, false) || xx_data_get_u16(c->b + 122, 2, 0, false) || xx_data_get_u16(c->b + 124, 2, 0, false) ||
        xx_data_get_u16(c->b + 382, 2, 0, false) || xx_data_get_u32(c->b + 384, 4, 0, false))
        return false;
    for (i = 0; i < orders; ++i) {
        if (c->b[126 + i] >= patterns) return false;
    }
    if (!audio_component_add(c, "headers-orders.bin", 0, p)) return false;
    for (i = 0; i < patterns; ++i) {
        uint32_t z;
        unsigned rows;
        size_t n;
        if (!audio_component_range(c, p, 6) || !(rows = xx_data_get_u16(c->b + p, 2, 0, false)) || rows > 1024 ||
            (z = xx_data_get_u32(c->b + p + 2, 4, 0, false)) != (uint32_t)(rows * chn * 7U))
            return false;
        n = (z + 1U) & ~1U;
        if (!audio_component_add(c, "pattern.bin", p, 6U + n)) return false;
        if (n > z && c->b[p + 6 + z]) return false;
        p += 6U + n;
    }
    {
        size_t start = p;
        for (i = 0; i < 511; ++i) {
            if (!audio_component_range(c, p, 36) || xx_data_get_u32(c->b + p + 32, 4, 0, false)) return false;
            p += 36;
        }
        if (!audio_component_add(c, "empty-instrument-sample-slots.bin", start, p - start)) return false;
    }
    return p == c->n;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return audio_component_loaded(f, s, pd, audio_component_parse);
}
void xx_tracker_mt2_init(xx_tracker_mt2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRACKER_MT2, "bin");
    }
}
xx_tracker_mt2 *xx_tracker_mt2_create(xx_io_device *d, int64_t b)
{
    xx_tracker_mt2 *r = (xx_tracker_mt2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tracker_mt2_init(r, d, b);
    return r;
}
void xx_tracker_mt2_destroy(xx_tracker_mt2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tracker_mt2_free(xx_tracker_mt2 *r)
{
    if (r) {
        xx_tracker_mt2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tracker_mt2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tracker_mt2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
