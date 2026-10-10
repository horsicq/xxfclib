/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/steinbergmedia/vst3_public_sdk/master/source/vst/vstpresetfile.cpp
 * VST3 preset version1 UID and complete List directory of unique Comp/Cont/Info chunks with disjoint exact extents. Original opaque plugin states/XML bytes exported;
 * plugin loading, state interpretation and unknown chunk IDs are unsupported. Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/steinberg_vst3preset/xx_steinberg_vst3preset.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[48];
    return texture_font_probe(f, n, b, 48) && pm_tag(b, "VST3", 4) && xx_data_get_u32(b + 4, 4, 0, false) == 1;
}
static bool texture_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t index = xx_data_get_u64(b + 40, 8, 0, false), p = 48;
    uint32_t count, i, j;
    char label[64];
    if (texture_font_stop(pd) || index < 48 || !texture_font_span(index, 8, n) || !pm_tag(b + index, "List", 4)) return false;
    for (i = 8; i < 40; ++i)
        if (!((b[i] >= '0' && b[i] <= '9') || (b[i] >= 'A' && b[i] <= 'F') || (b[i] >= 'a' && b[i] <= 'f'))) return false;
    count = xx_data_get_u32(b + index + 4, 4, 0, false);
    if (!count || count > 3 || index + 8 + (uint64_t)count * 20 != n || !texture_font_emit(f, s, "vst3-header.bin", 0, 48, n)) return false;
    for (i = 0; i < count; ++i) {
        uint64_t q = index + 8 + (uint64_t)i * 20, at = xx_data_get_u64(b + q + 4, 8, 0, false), bytes = xx_data_get_u64(b + q + 12, 8, 0, false);
        if (texture_font_stop(pd) || (!pm_tag(b + q, "Comp", 4) && !pm_tag(b + q, "Cont", 4) && !pm_tag(b + q, "Info", 4)) || at != p ||
            !texture_font_span(at, bytes, index))
            return false;
        for (j = 0; j < i; ++j)
            if (pm_tag(b + q, (const char *)(b + index + 8 + (uint64_t)j * 20), 4)) return false;
        xx_rt_snprintf(label, sizeof(label), "state-%c%c%c%c.bin", b[q], b[q + 1], b[q + 2], b[q + 3]);
        if (bytes && !texture_font_emit(f, s, label, at, bytes, n)) return false;
        p += bytes;
    }
    if (p != index || !texture_font_emit(f, s, "vst3-directory.bin", index, n - index, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_steinberg_vst3preset_init(xx_steinberg_vst3preset *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_STEINBERG_VST3PRESET, "vstpreset");
    }
}
xx_steinberg_vst3preset *xx_steinberg_vst3preset_create(xx_io_device *d, int64_t at)
{
    xx_steinberg_vst3preset *r = (xx_steinberg_vst3preset *)xx_mem_alloc(sizeof(*r));
    if (r) xx_steinberg_vst3preset_init(r, d, at);
    return r;
}
void xx_steinberg_vst3preset_destroy(xx_steinberg_vst3preset *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_steinberg_vst3preset_free(xx_steinberg_vst3preset *r)
{
    if (r) {
        xx_steinberg_vst3preset_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_steinberg_vst3preset_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_steinberg_vst3preset_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
