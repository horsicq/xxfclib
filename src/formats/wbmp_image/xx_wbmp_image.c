/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/wbmp.c
 * WAP WBMP type0: canonical bounded unsigned varints, positive dimensions, complete row-packed one-bit bitmap and exact EOF. Original descriptor and packed bitmap
 * exported; extension types declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/wbmp_image/xx_wbmp_image.h"
#include "../common/xx_component_binary.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[2];
    return n >= 4 && pm_read(f, 0, b, 2) && !b[0] && !b[1];
}
static bool scene_bitmap_wbmp_uint(component_binary_cursor *q, uint32_t *v)
{
    const uint8_t *p;
    unsigned i;
    uint32_t u = 0;
    for (i = 0; i < 4; ++i) {
        if (!component_binary_take(q, 1, &p) || (i == 0 && *p == 0x80)) return false;
        u = (u << 7) | (*p & 127);
        if (!(*p & 128)) {
            *v = u;
            return true;
        }
    }
    return false;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_binary_cursor q = {b, 2, n, pd};
    uint32_t w, h;
    uint64_t z;
    if (n < 4 || b[0] || b[1] || !scene_bitmap_wbmp_uint(&q, &w) || !scene_bitmap_wbmp_uint(&q, &h) || !w || !h || w > 8192 || h > 8192 || (uint64_t)w * h > 16777216)
        return false;
    z = ((uint64_t)w + 7) / 8 * h;
    return n - q.p == z && component_emit(f, s, "descriptor.wbmp", 0, q.p, n) && component_emit(f, s, "packed-bitmap.wbmp", q.p, z, n);
}

void xx_wbmp_image_init(xx_wbmp_image *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_WBMP_IMAGE, "wbmp");
    }
}
xx_wbmp_image *xx_wbmp_image_create(xx_io_device *d, int64_t at)
{
    xx_wbmp_image *r = (xx_wbmp_image *)xx_mem_alloc(sizeof(*r));
    if (r) xx_wbmp_image_init(r, d, at);
    return r;
}
void xx_wbmp_image_destroy(xx_wbmp_image *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_wbmp_image_free(xx_wbmp_image *r)
{
    if (r) {
        xx_wbmp_image_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_wbmp_image_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_wbmp_image_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
