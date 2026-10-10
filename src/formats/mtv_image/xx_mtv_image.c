/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/mtv.c
 * MTV raytracing image: complete ASCII dimensions and exact RGB24 rows; original descriptor and raster rows exported; image sequences declined. Signatureless fallback after structured readers.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/mtv_image/xx_mtv_image.h"
#include "../common/xx_component_text.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b;
    return n >= 5 && pm_read(f, 0, &b, 1) && b >= '1' && b <= '9';
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t w, h;
    unsigned y;
    char label[48];
    if (!component_text_line_poison_overflow(&q) || q.p > 64 || q.p == q.stop || !component_utf8(b, q.p, true, pd) ||
        !component_text_integer_delimited(&q, &w) || w < 1 || w > 8192 || !component_text_integer_delimited(&q, &h) ||
        h < 1 || h > 4095 || !(component_text_space(&q), q.t == q.stop) || (uint64_t)w * h > 8388608 ||
        n - q.p != (uint64_t)w * h * 3 || !component_emit(f, s, "descriptor.mtv", 0, q.p, n))
        return false;
    for (y = 0; y < (unsigned)h; ++y) {
        if (xx_component_parser_stopped(pd))
            return false;
        xx_rt_snprintf(label, sizeof(label), "rgb-row-%u.bin", y);
        if (!component_emit(f, s, label, q.p + (uint64_t)y * (unsigned)w * 3, (uint64_t)(unsigned)w * 3, n))
            return false;
    }
    return true;
}

void xx_mtv_image_init(xx_mtv_image *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_MTV_IMAGE, "mtv");
    }
}
xx_mtv_image *xx_mtv_image_create(xx_io_device *d, int64_t at) {
    xx_mtv_image *r = (xx_mtv_image *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_mtv_image_init(r, d, at);
    return r;
}
void xx_mtv_image_destroy(xx_mtv_image *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_mtv_image_free(xx_mtv_image *r) {
    if (r) {
        xx_mtv_image_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_mtv_image_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_mtv_image_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
