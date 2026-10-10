/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/legionus/kbd/master/src/libkfont/psffontop.c
 * PSF1/PSF2 console bitmap fonts with exact glyph geometry, complete bitmap array and optional fully terminated scalar Unicode mapping/sequence grammar. Encoded bitmap
 * and mappings are exported; font rendering and Sony PSF audio semantics are unrelated. Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/font_psf/xx_font_psf.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[4];
    return texture_font_probe(f, n, b, 4) && (pm_tag(b, "\x36\x04", 2) || xx_data_get_u32(b, 4, 0, false) == 0x864ab572U);
}
static bool texture_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t glyphs, height, width, bytes, flags, i;
    uint64_t p, bitmap, after;
    bool one = b[0] == 0x36;
    char label[64];
    if (one) {
        flags = b[2];
        glyphs = (flags & 1) ? 512U : 256U;
        height = b[3];
        width = 8;
        bytes = height;
        p = 4;
        if (flags > 7 || !height) return false;
    } else {
        if (n < 32 || xx_data_get_u32(b + 4, 4, 0, false) != 0 || xx_data_get_u32(b + 8, 4, 0, false) != 32) return false;
        flags = xx_data_get_u32(b + 12, 4, 0, false);
        glyphs = xx_data_get_u32(b + 16, 4, 0, false);
        bytes = xx_data_get_u32(b + 20, 4, 0, false);
        height = xx_data_get_u32(b + 24, 4, 0, false);
        width = xx_data_get_u32(b + 28, 4, 0, false);
        p = 32;
        if (flags > 1 || !glyphs || glyphs > 4093 || !width || width > 4096 || !height || height > 4096 || bytes != (uint64_t)((width + 7) / 8) * height) return false;
    }
    bitmap = p;
    if (!texture_font_span(p, (uint64_t)glyphs * bytes, n) || !texture_font_emit(f, s, "psf-header.bin", 0, p, n)) return false;
    for (i = 0; i < glyphs; ++i) {
        if (texture_font_stop(pd)) return false;
        xx_rt_snprintf(label, sizeof(label), "glyph-%u.bitmap", i);
        if (!texture_font_emit(f, s, label, p, bytes, n)) return false;
        p += bytes;
    }
    after = p;
    if (flags & (one ? 6U : 1U)) {
        for (i = 0; i < glyphs; ++i) {
            bool sequence = false, has = false;
            for (;;) {
                uint32_t c;
                if (texture_font_stop(pd) || p >= n) return false;
                if (one) {
                    if (!texture_font_span(p, 2, n)) return false;
                    c = xx_data_get_u16(b + p, 2, 0, false);
                    p += 2;
                } else c = b[p++];
                if (c == (one ? 65535U : 255U)) {
                    if (sequence && !has) return false;
                    break;
                }
                if (c == (one ? 65534U : 254U)) {
                    if (one && !(flags & 4)) return false;
                    if (sequence && !has) return false;
                    sequence = true;
                    has = false;
                    continue;
                }
                if (one) {
                    if (!texture_font_scalar(c)) return false;
                } else {
                    --p;
                    if (!texture_font_utf(b, &p, n, false)) return false;
                }
                has = true;
            }
        }
        if (!texture_font_emit(f, s, "unicode-map.bin", after, p - after, n)) return false;
    }
    if (p != n || bitmap == 0) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_font_psf_init(xx_font_psf *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_FONT_PSF, "psf");
    }
}
xx_font_psf *xx_font_psf_create(xx_io_device *d, int64_t at)
{
    xx_font_psf *r = (xx_font_psf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_font_psf_init(r, d, at);
    return r;
}
void xx_font_psf_destroy(xx_font_psf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_font_psf_free(xx_font_psf *r)
{
    if (r) {
        xx_font_psf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_font_psf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_font_psf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
