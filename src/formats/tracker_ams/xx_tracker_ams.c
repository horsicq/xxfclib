/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_ams/xx_tracker_ams.h"
#include "../common/xx_audio_components.h"
#ifndef TRACKER_AMS
#define XX_FILE_TYPE_TRACKER_AMS ((xx_file_type_t)809)
#endif
static bool audio_component_parse(audio_component_blob *c)
{
    unsigned samples, patterns, orders, chn, i;
    size_t p, headers;
    uint32_t lens[255];
    if (!audio_component_range(c, 0, 18) || !audio_component_eq(c, 0, "Extreme", 7) || c->b[8] != 1 || c->b[7] > 2 || c->b[9] & 0xe0 || (samples = c->b[10]) > 255 ||
        !(patterns = xx_data_get_u16(c->b + 11, 2, 0, false)) || patterns > 1024 || !(orders = xx_data_get_u16(c->b + 13, 2, 0, false)) || orders > 256) {
        return false;
    }
    chn = (c->b[9] & 31U) + 1U;
    p = 18U + xx_data_get_u16(c->b + 16, 2, 0, false);
    for (i = 0; i < samples; ++i) {
        uint8_t flags;
        if (!audio_component_range(c, p, 17)) return false;
        flags = c->b[p + 16];
        lens[i] = xx_data_get_u32(c->b + p, 4, 0, false);
        if (flags & ~0x84U || c->b[p + 15] > 127 || xx_data_get_u32(c->b + p + 4, 4, 0, false) > xx_data_get_u32(c->b + p + 8, 4, 0, false) ||
            xx_data_get_u32(c->b + p + 8, 4, 0, false) > lens[i] || ((flags & 0x84) && lens[i] > AUDIO_COMPONENT_LIMIT / 2U))
            return false;
        if (flags & 0x84) lens[i] *= 2U;
        p += 17;
    }
    if (!audio_component_strings(c, &p, 1U + samples + chn + patterns) || !audio_component_range(c, p, 2)) {
        return false;
    }
    {
        unsigned z = xx_data_get_u16(c->b + p, 2, 0, false);
        p += 2;
        if (!audio_component_range(c, p, z)) return false;
        p += z;
    }
    if (!audio_component_range(c, p, 2U * orders)) return false;
    for (i = 0; i < orders; ++i)
        if (xx_data_get_u16(c->b + p + 2U * i, 2, 0, false) >= patterns) return false;
    p += 2U * orders;
    headers = p;
    if (!audio_component_add(c, "headers-text-orders.bin", 0, headers)) return false;
    for (i = 0; i < patterns; ++i) {
        uint32_t z;
        if (!audio_component_range(c, p, 4) || !(z = xx_data_get_u32(c->b + p, 4, 0, false)) || !audio_component_range(c, p + 4, z)) return false;
        if (!audio_component_add(c, "encoded-pattern.bin", p, 4U + z)) return false;
        p += 4U + z;
    }
    for (i = 0; i < samples; ++i) {
        if (!audio_component_range(c, p, lens[i])) return false;
        if (lens[i] && !audio_component_add(c, "sample.bin", p, lens[i])) return false;
        p += lens[i];
    }
    return p == c->n;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return audio_component_loaded(f, s, pd, audio_component_parse);
}
void xx_tracker_ams_init(xx_tracker_ams *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRACKER_AMS, "bin");
    }
}
xx_tracker_ams *xx_tracker_ams_create(xx_io_device *d, int64_t b)
{
    xx_tracker_ams *r = (xx_tracker_ams *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tracker_ams_init(r, d, b);
    return r;
}
void xx_tracker_ams_destroy(xx_tracker_ams *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tracker_ams_free(xx_tracker_ams *r)
{
    if (r) {
        xx_tracker_ams_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tracker_ams_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tracker_ams_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
