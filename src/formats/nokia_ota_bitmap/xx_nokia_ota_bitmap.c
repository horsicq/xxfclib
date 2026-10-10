/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/otb.c
 * Nokia OTA bitmap: checked information flags, short/extended positive dimensions, depth1 and exact MSB-packed rows; original descriptor and row bitmaps exported; extension chains declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/nokia_ota_bitmap/xx_nokia_ota_bitmap.h"
#include "../common/xx_component_binary.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b;
    return n >= 5 && pm_read(f, 0, &b, 1) && (b == 0 || b == 16);
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    uint32_t w, h, y;
    uint64_t at, stride;
    char label[48];
    if (n < 5 || (b[0] != 0 && b[0] != 16))
        return false;
    if (b[0] == 0) {
        w = b[1];
        h = b[2];
        at = 4;
    } else {
        if (n < 7)
            return false;
        w = xx_data_get_u16(b + 1, 2, 0, true);
        h = xx_data_get_u16(b + 3, 2, 0, true);
        at = 6;
    }
    if (!w || !h || w > 8192 || h > 4095 || (uint64_t)w * h > 16777216 || b[at - 1] != 1)
        return false;
    stride = ((uint64_t)w + 7) / 8;
    if (n - at != stride * h || !component_emit(f, s, "descriptor.otb", 0, at, n))
        return false;
    for (y = 0; y < h; ++y) {
        if (xx_component_parser_stopped(pd))
            return false;
        xx_rt_snprintf(label, sizeof(label), "bitmap-row-%u.msb1", y);
        if (!component_emit(f, s, label, at + y * stride, stride, n))
            return false;
    }
    return true;
}

void xx_nokia_ota_bitmap_init(xx_nokia_ota_bitmap *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_NOKIA_OTA_BITMAP, "otb");
    }
}
xx_nokia_ota_bitmap *xx_nokia_ota_bitmap_create(xx_io_device *d, int64_t at) {
    xx_nokia_ota_bitmap *r = (xx_nokia_ota_bitmap *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_nokia_ota_bitmap_init(r, d, at);
    return r;
}
void xx_nokia_ota_bitmap_destroy(xx_nokia_ota_bitmap *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nokia_ota_bitmap_free(xx_nokia_ota_bitmap *r) {
    if (r) {
        xx_nokia_ota_bitmap_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nokia_ota_bitmap_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_nokia_ota_bitmap_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
