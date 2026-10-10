/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/1j01/anypalette.js/master/anypalette.js
 * Adobe Color Table: exact768-byte RGB256 table or772-byte counted/transparency variant, bounded active color count and transparent index. Original color triplets and
 * optional descriptor exported; no color-space conversion. Signatureless fixed-size palette recognition is an ambiguous fallback after structured readers. Bounded32MiB
 * input,4096 components and bounded work.
 */
#include "xxfclib/formats/adobe_act/xx_adobe_act.h"
#include "../common/xx_component_binary.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    (void)f;
    return n == 768 || n == 772;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    unsigned i, count = 256;
    char name[48];
    if (n != 768 && n != 772) return false;
    if (n == 772) {
        unsigned transparent = xx_data_get_u16(b + 770, 2, 0, true);
        count = xx_data_get_u16(b + 768, 2, 0, true);
        if (!count) count = 256;
        if (count > 256 || (transparent != 65535 && transparent >= count)) return false;
    }
    for (i = 0; i < 256; ++i) {
        if (xx_component_parser_stopped(pd)) return false;
        xx_rt_snprintf(name, sizeof(name), "color-%u.rgb", i);
        if (!component_emit(f, s, name, (uint64_t)i * 3, 3, n)) return false;
    }
    return n == 768 || component_emit(f, s, "descriptor.act", 768, 4, n);
}

void xx_adobe_act_init(xx_adobe_act *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_ADOBE_ACT, "act");
    }
}
xx_adobe_act *xx_adobe_act_create(xx_io_device *d, int64_t at)
{
    xx_adobe_act *r = (xx_adobe_act *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adobe_act_init(r, d, at);
    return r;
}
void xx_adobe_act_destroy(xx_adobe_act *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adobe_act_free(xx_adobe_act *r)
{
    if (r) {
        xx_adobe_act_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adobe_act_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adobe_act_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
