/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/GNOME/gimp/master/app/core/gimppalette-load.c
 * GIMP GPL UTF8 named RGB8 palettes: complete header, optional columns and exact bounded color rows. Original descriptor and color records exported; rendering
 * unsupported. Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/gimp_gpl/xx_gimp_gpl.h"
#include "../common/xx_component_text.h"

static bool graphics_text_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool graphics_text_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(graphics_text, 33554432, )
static bool graphics_text_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[12];
    return n >= 12 && pm_read(f, 0, b, 12) && component_tag(b, "GIMP Palette", 12);
}
static bool graphics_text_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    bool name = false, columns = false, data = false;
    uint32_t colors = 0;
    uint64_t section = 0;
    char label[48];
    if (!component_utf8(b, n, false, pd) || !component_text_line(&q) || !component_text_word(&q, "GIMP") || !component_text_word(&q, "Palette") ||
        !component_text_done(&q))
        return false;
    while (q.p < n) {
        int32_t rgb[3];
        unsigned i;
        if (xx_component_parser_stopped(pd) || !component_text_line(&q)) return false;
        if (component_text_done(&q)) continue;
        if (!data && component_text_word(&q, "Name:")) {
            component_text_space(&q);
            if (name || q.t == q.stop || q.stop - q.t > 4096) return false;
            name = true;
            continue;
        }
        if (!data && component_text_word(&q, "Columns:")) {
            int32_t v;
            if (columns || !component_text_integer(&q, &v) || v < 0 || v > 256 || !component_text_done(&q)) return false;
            columns = true;
            continue;
        }
        if (!data) {
            section = q.start;
            if (!component_emit(f, s, "descriptor.gpl", 0, section, n)) return false;
            data = true;
        }
        for (i = 0; i < 3; ++i)
            if (!component_text_integer(&q, &rgb[i]) || rgb[i] < 0 || rgb[i] > 255) return false;
        component_text_space(&q);
        if (q.stop - q.t > 4096 || ++colors > 4094) return false;
        xx_rt_snprintf(label, sizeof(label), "color-%u.gpl", colors - 1);
        if (!component_emit(f, s, label, q.start, q.p - q.start, n)) {
            return false;
        }
        section = q.p;
    }
    if (!data || !name || !colors) return false;
    /* Retain intervening comments as metadata as well as every color row. */
    {
        size_t i, count = s->count;
        uint64_t covered = 0;
        for (i = 0; i < count; ++i) {
            uint64_t p = (uint64_t)(s->items[i].offset - f->base_address), z = (uint64_t)s->items[i].size;
            if (p > covered && !component_emit(f, s, "comments.gpl", covered, p - covered, n)) return false;
            covered = p + z;
        }
        if (covered < n && !component_emit(f, s, "comments.gpl", covered, n - covered, n)) return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_gimp_gpl_init(xx_gimp_gpl *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_GIMP_GPL, "gpl");
    }
}
xx_gimp_gpl *xx_gimp_gpl_create(xx_io_device *d, int64_t at)
{
    xx_gimp_gpl *r = (xx_gimp_gpl *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gimp_gpl_init(r, d, at);
    return r;
}
void xx_gimp_gpl_destroy(xx_gimp_gpl *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gimp_gpl_free(xx_gimp_gpl *r)
{
    if (r) {
        xx_gimp_gpl_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gimp_gpl_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gimp_gpl_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
