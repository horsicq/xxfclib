/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/1j01/anypalette.js/master/src/formats/PaintShopPro.coffee
 * JASC-PAL0100: exact declared RGB8 color rows and complete text framing. Original descriptor and palette rows exported; no rendering.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/jasc_palette/xx_jasc_palette.h"
#include "../common/xx_component_text.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[8];
    return n >= 16 && pm_read(f, 0, b, 8) && component_tag(b, "JASC-PAL", 8);
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t count, rgb;
    unsigned i, j;
    char label[48];
    if (!component_utf8(b, n, true, pd) || !component_text_line(&q) || !component_text_word(&q, "JASC-PAL") ||
        !component_text_done(&q) || !component_text_line(&q) || !component_text_word(&q, "0100") ||
        !component_text_done(&q) || !component_text_line(&q) || !component_text_integer(&q, &count) || count < 1 ||
        count > 4094 || !component_text_done(&q) || !component_emit(f, s, "descriptor.pal", 0, q.p, n))
        return false;
    for (i = 0; i < (unsigned)count; ++i) {
        if (xx_component_parser_stopped(pd) || !component_text_line(&q))
            return false;
        for (j = 0; j < 3; ++j)
            if (!component_text_integer(&q, &rgb) || rgb < 0 || rgb > 255)
                return false;
        if (!component_text_done(&q))
            return false;
        xx_rt_snprintf(label, sizeof(label), "color-%u.pal", i);
        if (!component_emit(f, s, label, q.start, q.p - q.start, n))
            return false;
    }
    {
        uint64_t at = q.p;
        while (q.p < n)
            if (!component_text_line(&q) || !component_text_done(&q))
                return false;
        if (at < n && !component_emit(f, s, "trailing.pal", at, n - at, n))
            return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_jasc_palette_init(xx_jasc_palette *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_JASC_PALETTE, "pal");
    }
}
xx_jasc_palette *xx_jasc_palette_create(xx_io_device *d, int64_t at) {
    xx_jasc_palette *r = (xx_jasc_palette *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_jasc_palette_init(r, d, at);
    return r;
}
void xx_jasc_palette_destroy(xx_jasc_palette *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_jasc_palette_free(xx_jasc_palette *r) {
    if (r) {
        xx_jasc_palette_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_jasc_palette_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_jasc_palette_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
