/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/viff.c
 * Khoros VIFFv1 images: complete1024-byte typed descriptor, implicit finite geometry, counted8-bit/16-bit/32-bit/finite-float planes and bounded byte maps with exact EOF. Original descriptor/maps/planes exported; explicit locations, encoding/unknown image extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/khoros_viff/xx_khoros_viff.h"
#include "../common/xx_component_binary.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[5];
    return n >= 1025 && pm_read(f, 0, b, 5) && b[0] == 0xab && b[1] == 1 && b[2] == 1 && b[3] == 3 &&
           (b[4] == 2 || b[4] == 4 || b[4] == 8);
}
static uint32_t image_document_viff_u(const uint8_t *p, bool le) {
    return le ? xx_data_get_u32(p, 4, 0, false) : xx_data_get_u32(p, 4, 0, true);
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    bool le;
    uint32_t w, h, bands, type, bytes, map, rows, cols, i;
    uint64_t p = 1024, plane;
    char label[48];
    if (n < 1025 || b[0] != 0xab || b[1] != 1 || b[2] != 1 || b[3] != 3 || (b[4] != 2 && b[4] != 4 && b[4] != 8) ||
        !component_zero(b + 5, 3) || !component_zero(b + 604, 420))
        return false;
    le = b[4] == 4 || b[4] == 8;
    w = image_document_viff_u(b + 520, le);
    h = image_document_viff_u(b + 524, le);
    bands = image_document_viff_u(b + 560, le);
    type = image_document_viff_u(b + 564, le);
    bytes = type == 1 ? 1 : type == 2 ? 2 : type == 4 || type == 5 ? 4 : type == 9 ? 8 : 0;
    map = image_document_viff_u(b + 572, le);
    rows = image_document_viff_u(b + 580, le);
    cols = image_document_viff_u(b + 584, le);
    if (!w || !h || w > 8192 || h > 8192 || (uint64_t)w * h > 8388608 || (bands != 1 && bands != 3 && bands != 4) ||
        !bytes || image_document_viff_u(b + 528, le) || !component_is_finite32(image_document_viff_u(b + 540, le)) ||
        !component_is_finite32(image_document_viff_u(b + 544, le)) || image_document_viff_u(b + 548, le) != 1 ||
        image_document_viff_u(b + 552, le) || image_document_viff_u(b + 556, le) != 1 ||
        image_document_viff_u(b + 568, le) || image_document_viff_u(b + 600, le) > 15 ||
        !component_emit(f, s, "descriptor.viff", 0, 1024, n))
        return false;
    if (map) {
        uint64_t size;
        if (map != 1 || bands != 1 || image_document_viff_u(b + 576, le) != 1 || !rows || rows > 4 || !cols ||
            cols > 256 || image_document_viff_u(b + 588, le) || image_document_viff_u(b + 592, le) != 1 ||
            image_document_viff_u(b + 596, le))
            return false;
        size = (uint64_t)rows * cols;
        if (!component_emit(f, s, "colormap.viff", p, size, n))
            return false;
        p += size;
    } else if (image_document_viff_u(b + 576, le) || rows || cols || image_document_viff_u(b + 588, le) ||
               image_document_viff_u(b + 592, le) > 1 || image_document_viff_u(b + 596, le))
        return false;
    if (b[4] == 4 && (type == 5 || type == 9)) {
        return false;
    }
    plane = (uint64_t)w * h * bytes;
    if (n - p != plane * bands)
        return false;
    if (type == 5 || type == 9)
        for (i = 0; i < bands; ++i) {
            uint64_t j;
            for (j = 0; j < plane; j += bytes) {
                uint32_t u = image_document_viff_u(b + p + (uint64_t)i * plane + j + (type == 9 && le ? 4 : 0), le);
                if (((j & 4095) == 0 && xx_component_parser_stopped(pd)) ||
                    (type == 5 ? !component_is_finite32(u) : (u & 0x7ff00000U) == 0x7ff00000U))
                    return false;
            }
        }
    if (map && bytes == 1) {
        uint64_t j;
        for (j = p; j < n; ++j)
            if (b[j] >= cols || ((j & 4095) == 0 && xx_component_parser_stopped(pd)))
                return false;
    }
    for (i = 0; i < bands; ++i) {
        xx_rt_snprintf(label, sizeof(label), "sample-band-%u.bin", i);
        if (xx_component_parser_stopped(pd) || !component_emit(f, s, label, p + (uint64_t)i * plane, plane, n))
            return false;
    }
    return true;
}

void xx_khoros_viff_init(xx_khoros_viff *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_KHOROS_VIFF, "viff");
    }
}
xx_khoros_viff *xx_khoros_viff_create(xx_io_device *d, int64_t at) {
    xx_khoros_viff *r = (xx_khoros_viff *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_khoros_viff_init(r, d, at);
    return r;
}
void xx_khoros_viff_destroy(xx_khoros_viff *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_khoros_viff_free(xx_khoros_viff *r) {
    if (r) {
        xx_khoros_viff_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_khoros_viff_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_khoros_viff_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
