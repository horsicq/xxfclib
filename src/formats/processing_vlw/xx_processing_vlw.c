/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/processing/processing4/main/core/src/processing/core/PFont.java
 * Processing VLW11 with complete ordered glyph metrics, exact grayscale bitmap array, modified-UTF name records and smoothing flag. Original glyph bitmaps and names
 * exported; older layouts, external font loading and rendering are unsupported. Signatureless detection is offset-zero only. Limit64MiB,4096 components. No payload or
 * external resource is executed.
 */
#include "xxfclib/formats/processing_vlw/xx_processing_vlw.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[24];
    return texture_font_probe(f, n, b, 24) && xx_data_get_u32(b + 4, 4, 0, true) == 11;
}
static bool texture_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t count = xx_data_get_u32(b, 4, 0, true), size = xx_data_get_u32(b + 8, 4, 0, true), last = 0, i;
    uint64_t p = 24 + (uint64_t)count * 28, tableEnd = p, tail;
    char label[64];
    if (!count || count > 4092 || !size || size > 4096 || xx_data_get_u32(b + 12, 4, 0, true) != 0 || xx_data_get_u32(b + 16, 4, 0, true) > 65536 ||
        xx_data_get_u32(b + 20, 4, 0, true) > 65536 || p > n || !texture_font_emit(f, s, "vlw-header-metrics.bin", 0, p, n))
        return false;
    for (i = 0; i < count; ++i) {
        const uint8_t *g = b + 24 + (uint64_t)i * 28;
        uint32_t c = xx_data_get_u32(g, 4, 0, true), h = xx_data_get_u32(g + 4, 4, 0, true), w = xx_data_get_u32(g + 8, 4, 0, true);
        uint64_t bytes = (uint64_t)w * h;
        unsigned k;
        if (texture_font_stop(pd) || !texture_font_scalar(c) || (i && c <= last) || w > 4096 || h > 4096 || xx_data_get_u32(g + 24, 4, 0, true) != 0 ||
            !texture_font_span(p, bytes, n))
            return false;
        last = c;
        for (k = 12; k <= 20; k += 4)
            if ((int32_t)xx_data_get_u32(g + k, 4, 0, true) < -65536 || (int32_t)xx_data_get_u32(g + k, 4, 0, true) > 65536) return false;
        if (bytes) {
            xx_rt_snprintf(label, sizeof(label), "glyph-%u.bitmap", c);
            if (!texture_font_emit(f, s, label, p, bytes, n)) return false;
        }
        p += bytes;
    }
    tail = p;
    if (!texture_font_name16(b, &p, n) || !texture_font_name16(b, &p, n) || !texture_font_span(p, 1, n) || b[p] > 1 || p + 1 != n) return false;
    if (tableEnd <= 24 || !texture_font_emit(f, s, "vlw-names.bin", tail, n - tail, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_processing_vlw_init(xx_processing_vlw *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_PROCESSING_VLW, "vlw");
    }
}
xx_processing_vlw *xx_processing_vlw_create(xx_io_device *d, int64_t at)
{
    xx_processing_vlw *r = (xx_processing_vlw *)xx_mem_alloc(sizeof(*r));
    if (r) xx_processing_vlw_init(r, d, at);
    return r;
}
void xx_processing_vlw_destroy(xx_processing_vlw *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_processing_vlw_free(xx_processing_vlw *r)
{
    if (r) {
        xx_processing_vlw_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_processing_vlw_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_processing_vlw_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
