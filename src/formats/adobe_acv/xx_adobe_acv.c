/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
 * Adobe curve file versions1/4: complete counted curves and sorted distinct input/output 8-bit point pairs with exact EOF. Original header and individual curve records
 * exported; extended curve-map/footer variants declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/adobe_acv/xx_adobe_acv.h"
#include "../common/xx_component_binary.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[4];
    return n >= 6 && pm_read(f, 0, b, 4) && (xx_data_get_u16(b, 2, 0, true) == 1 || xx_data_get_u16(b, 2, 0, true) == 4) && xx_data_get_u16(b + 2, 2, 0, true) > 0 &&
           xx_data_get_u16(b + 2, 2, 0, true) <= 16;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t p = 4;
    unsigned i, count;
    char name[48];
    if (n < 6 || (xx_data_get_u16(b, 2, 0, true) != 1 && xx_data_get_u16(b, 2, 0, true) != 4) || (count = xx_data_get_u16(b + 2, 2, 0, true)) == 0 || count > 16 ||
        !component_emit(f, s, "descriptor.acv", 0, 4, n))
        return false;
    for (i = 0; i < count; ++i) {
        uint64_t start = p;
        unsigned j, points, last = 0;
        if (xx_component_parser_stopped(pd) || !component_span(p, 2, n) || (points = xx_data_get_u16(b + p, 2, 0, true)) < 2 || points > 19) return false;
        p += 2;
        for (j = 0; j < points; ++j) {
            unsigned input;
            if (!component_span(p, 4, n) || xx_data_get_u16(b + p, 2, 0, true) > 255 || (input = xx_data_get_u16(b + p + 2, 2, 0, true)) > 255 || (j && input <= last))
                return false;
            last = input;
            p += 4;
        }
        xx_rt_snprintf(name, sizeof(name), "curve-%u.acv", i);
        if (!component_emit(f, s, name, start, p - start, n)) return false;
    }
    return p == n;
}

void xx_adobe_acv_init(xx_adobe_acv *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_ADOBE_ACV, "acv");
    }
}
xx_adobe_acv *xx_adobe_acv_create(xx_io_device *d, int64_t at)
{
    xx_adobe_acv *r = (xx_adobe_acv *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adobe_acv_init(r, d, at);
    return r;
}
void xx_adobe_acv_destroy(xx_adobe_acv *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adobe_acv_free(xx_adobe_acv *r)
{
    if (r) {
        xx_adobe_acv_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adobe_acv_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adobe_acv_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
