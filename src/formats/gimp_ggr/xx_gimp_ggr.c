/* SPDX-License-Identifier: MIT
 * Primary reference: https://developer.gimp.org/core/standards/ggr/
 * GIMP GGR complete ordered color segments spanning0..1 with finite RGBA values and typed blend/color/endpoint enums. Original segments exported; interpolation/rendering
 * unsupported. Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/gimp_ggr/xx_gimp_ggr.h"
#include "../common/xx_component_text.h"

static bool graphics_text_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool graphics_text_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(graphics_text, 33554432, )
static bool graphics_text_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[13];
    return n >= 13 && pm_read(f, 0, b, 13) && component_tag(b, "GIMP Gradient", 13);
}
static bool graphics_text_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t count, i;
    double last = 0;
    uint64_t start;
    char label[48];
    if (!component_utf8(b, n, false, pd) || !component_text_line(&q) || !component_text_word(&q, "GIMP") || !component_text_word(&q, "Gradient") ||
        !component_text_done(&q) || !component_text_line(&q))
        return false;
    if (component_text_word(&q, "Name:")) {
        component_text_space(&q);
        if (q.t == q.stop || q.stop - q.t > 4096 || !component_text_line(&q)) return false;
    }
    if (!component_text_integer(&q, &count) || count < 1 || count > 4093 || !component_text_done(&q) || !component_emit(f, s, "descriptor.ggr", 0, q.p, n)) return false;
    for (i = 0; i < count; ++i) {
        double x[11];
        int32_t modes[4] = {0, 0, 0, 0};
        unsigned j;
        if (xx_component_parser_stopped(pd) || !component_text_line(&q)) return false;
        start = q.start;
        for (j = 0; j < 11; ++j)
            if (!component_text_number_36_digits(&q, &x[j]) || x[j] < 0 || x[j] > 1) return false;
        if (x[0] != last || x[0] >= x[2] || x[1] < x[0] || x[1] > x[2]) {
            return false;
        }
        last = x[2];
        if (!component_text_integer(&q, &modes[0]) || !component_text_integer(&q, &modes[1]) || modes[0] < 0 || modes[0] > 5 || modes[1] < 0 || modes[1] > 2)
            return false;
        if (!component_text_done(&q)) {
            if (!component_text_integer(&q, &modes[2]) || !component_text_integer(&q, &modes[3]) || modes[2] < 0 || modes[2] > 4 || modes[3] < 0 || modes[3] > 4)
                return false;
        }
        if (!component_text_done(&q)) {
            return false;
        }
        xx_rt_snprintf(label, sizeof(label), "segment-%u.ggr", (unsigned)i);
        if (!component_emit(f, s, label, start, q.p - start, n)) return false;
    }
    start = q.p;
    while (q.p < n) {
        if (!component_text_line(&q) || !component_text_done(&q)) return false;
    }
    if (last != 1 || (q.p > start && !component_emit(f, s, "trailing.ggr", start, q.p - start, n))) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_gimp_ggr_init(xx_gimp_ggr *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_GIMP_GGR, "ggr");
    }
}
xx_gimp_ggr *xx_gimp_ggr_create(xx_io_device *d, int64_t at)
{
    xx_gimp_ggr *r = (xx_gimp_ggr *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gimp_ggr_init(r, d, at);
    return r;
}
void xx_gimp_ggr_destroy(xx_gimp_ggr *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gimp_ggr_free(xx_gimp_ggr *r)
{
    if (r) {
        xx_gimp_ggr_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gimp_ggr_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gimp_ggr_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
