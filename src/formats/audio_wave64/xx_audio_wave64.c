/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_wave64/xx_audio_wave64.h"
#include "../common/xx_audio_components.h"
#ifndef AUDIO_WAVE64
#define XX_FILE_TYPE_AUDIO_WAVE64 ((xx_file_type_t)815)
#endif
static bool audio_component_parse(audio_component_blob *c)
{
    static const uint8_t riff[] = {0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 4, 0xc1, 0, 0};
    static const uint8_t tail[] = {0xf3, 0xac, 0xd3, 0x11, 0x8c, 0xd1, 0, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a};
    size_t p = 40;
    unsigned seen = 0, align = 0;
    uint64_t databytes = 0;
    if (!audio_component_range(c, 0, 40) || xx_rt_memcmp(c->b, riff, 16) || xx_data_get_u64(c->b + 16, 8, 0, false) != c->n || !audio_component_eq(c, 24, "wave", 4) ||
        xx_rt_memcmp(c->b + 28, tail, 12) || !audio_component_add(c, "header.bin", 0, 40))
        return false;
    while (p < c->n) {
        uint64_t z;
        size_t at = p + 24, end, padded;
        char name[24];
        if (!audio_component_range(c, p, 24) || xx_rt_memcmp(c->b + p + 4, tail, 12) || (z = xx_data_get_u64(c->b + p + 16, 8, 0, false)) < 24 || z > c->n - p)
            return false;
        end = p + (size_t)z;
        if (audio_component_eq(c, p, "fmt ", 4)) {
            unsigned channels, bits;
            uint32_t rate;
            if (seen & 1 || (z != 40 && z != 42) || xx_data_get_u16(c->b + at, 2, 0, false) != 1 || !(channels = xx_data_get_u16(c->b + at + 2, 2, 0, false)) ||
                channels > 32 || !(rate = xx_data_get_u32(c->b + at + 4, 4, 0, false)) || rate > 768000 || (bits = xx_data_get_u16(c->b + at + 14, 2, 0, false)) == 0 ||
                (bits != 8 && bits != 16 && bits != 24 && bits != 32) || (align = xx_data_get_u16(c->b + at + 12, 2, 0, false)) != channels * (bits / 8U) ||
                xx_data_get_u32(c->b + at + 8, 4, 0, false) != rate * align || (z == 42 && xx_data_get_u16(c->b + at + 16, 2, 0, false)))
                return false;
            seen |= 1;
        } else if (audio_component_eq(c, p, "data", 4)) {
            if (seen & 2 || z == 24) return false;
            seen |= 2;
            databytes = z - 24;
        } else return false;
        xx_rt_snprintf(name, sizeof(name), "%.4s.bin", c->b + p);
        if (!audio_component_add(c, name, p, (size_t)z)) return false;
        padded = (end + 7U) & ~7U;
        if (padded > c->n || !audio_component_zero(c, end, padded - end)) return false;
        p = padded;
    }
    return p == c->n && seen == 3 && align && databytes % align == 0;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return audio_component_loaded(f, s, pd, audio_component_parse);
}
void xx_audio_wave64_init(xx_audio_wave64 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUDIO_WAVE64, "bin");
    }
}
xx_audio_wave64 *xx_audio_wave64_create(xx_io_device *d, int64_t b)
{
    xx_audio_wave64 *r = (xx_audio_wave64 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_wave64_init(r, d, b);
    return r;
}
void xx_audio_wave64_destroy(xx_audio_wave64 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_wave64_free(xx_audio_wave64 *r)
{
    if (r) {
        xx_audio_wave64_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_wave64_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_wave64_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
