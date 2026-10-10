/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_dff/xx_audio_dff.h"
#include "../common/xx_audio_components.h"
#ifndef AUDIO_DFF
#define XX_FILE_TYPE_AUDIO_DFF ((xx_file_type_t)814)
#endif
static bool audio_component_parse(audio_component_blob *c)
{
    size_t p = 16;
    unsigned seen = 0, channels = 0;
    uint32_t rate = 0;
    uint64_t data = 0;
    if (!audio_component_eq(c, 0, "FRM8", 4) || !audio_component_eq(c, 12, "DSD ", 4) || xx_data_get_u64(c->b + 4, 8, 0, true) != c->n - 12 ||
        !audio_component_add(c, "header.bin", 0, 16))
        return false;
    while (p < c->n) {
        size_t at = p + 12;
        uint64_t z;
        unsigned bit = 0;
        char name[24];
        if (!audio_component_range(c, p, 12) || (z = xx_data_get_u64(c->b + p + 4, 8, 0, true)) > c->n - at) return false;
        if (audio_component_eq(c, p, "FVER", 4)) {
            bit = 1;
            if (z != 4 || (xx_data_get_u32(c->b + at, 4, 0, true) >> 24) != 1) return false;
        } else if (audio_component_eq(c, p, "PROP", 4)) {
            size_t q = at + 4;
            unsigned ps = 0;
            bit = 2;
            if (!audio_component_eq(c, at, "SND ", 4)) return false;
            while (q < at + z) {
                uint64_t n;
                size_t x = q + 12;
                unsigned pb;
                if (q + 12 > at + z || (n = xx_data_get_u64(c->b + q + 4, 8, 0, true)) > at + z - x) return false;
                if (audio_component_eq(c, q, "FS  ", 4)) {
                    pb = 1;
                    if (n != 4 || !(rate = xx_data_get_u32(c->b + x, 4, 0, true)) || rate > 24576000 || (rate & 7)) return false;
                } else if (audio_component_eq(c, q, "CHNL", 4)) {
                    pb = 2;
                    if (n < 2 || !(channels = xx_data_get_u16(c->b + x, 2, 0, true)) || channels > 32 || n != 2U + 4U * channels) return false;
                } else if (audio_component_eq(c, q, "CMPR", 4)) {
                    pb = 4;
                    if (n < 5 || !audio_component_eq(c, x, "DSD ", 4) || n != 5U + c->b[x + 4]) return false;
                } else return false;
                if (ps & pb) return false;
                ps |= pb;
                q = x + (size_t)n;
                if (n & 1) {
                    if (q >= at + z || c->b[q]) return false;
                    ++q;
                }
            }
            if (q != at + z || ps != 7) return false;
        } else if (audio_component_eq(c, p, "DSD ", 4)) {
            bit = 4;
            data = z;
            if (!z) return false;
        } else {
            return false;
        }
        if (seen & bit) return false;
        seen |= bit;
        xx_rt_snprintf(name, sizeof(name), "%.4s.bin", c->b + p);
        if (!audio_component_add(c, name, p, 12U + (size_t)z)) return false;
        p = at + (size_t)z;
        if (z & 1) {
            if (!audio_component_zero(c, p, 1)) return false;
            ++p;
        }
    }
    return p == c->n && seen == 7 && channels && data % channels == 0;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return audio_component_loaded(f, s, pd, audio_component_parse);
}
void xx_audio_dff_init(xx_audio_dff *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUDIO_DFF, "bin");
    }
}
xx_audio_dff *xx_audio_dff_create(xx_io_device *d, int64_t b)
{
    xx_audio_dff *r = (xx_audio_dff *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_dff_init(r, d, b);
    return r;
}
void xx_audio_dff_destroy(xx_audio_dff *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_dff_free(xx_audio_dff *r)
{
    if (r) {
        xx_audio_dff_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_dff_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_dff_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
