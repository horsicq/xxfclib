/* SPDX-License-Identifier: MIT
 * Primary reference: https://gdal.org/en/stable/drivers/raster/gxf.html
 * GXF3 uncompressed numeric grids: complete unique counted header fields, bounded POINTS/ROWS, finite georeference/nodata and exactly declared samples; complete metadata
 * sections and optional EOF. Original header and sample rows exported. Compressed GTYPE, transforms/projection and unknown header extensions declined. Bounded32MiB
 * input,4096 components and bounded work.
 */
#include "xxfclib/formats/gxf_grid/xx_gxf_grid.h"
#include "../common/xx_component_text.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b;
    return n >= 16 && pm_read(f, 0, &b, 1) && b == '#';
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t w = 0, h = 0;
    double nodata = -1e12, lo = nodata, hi = nodata, zlo = 0, zhi = 0;
    uint32_t fields = 0;
    bool grid = false;
    if (!component_utf8(b, n, true, pd)) return false;
    while (q.p < n) {
        unsigned kind;
        double v;
        int32_t value;
        uint64_t z;
        if (xx_component_parser_stopped(pd) || !component_text_line(&q)) return false;
        component_text_space(&q);
        if (q.t == q.stop) continue;
        z = q.stop - q.t;
        if (component_text_word_ci(&q, "#grid")) {
            if (!component_text_done(&q)) {
                return false;
            }
            grid = true;
            break;
        }
        if (component_text_word_ci(&q, "#points") || component_text_word_ci(&q, "#poin")) kind = 0;
        else if (component_text_word_ci(&q, "#rows")) kind = 1;
        else if (component_text_word_ci(&q, "#ptseparation") || component_text_word_ci(&q, "#ptse")) kind = 2;
        else if (component_text_word_ci(&q, "#rwseparation") || component_text_word_ci(&q, "#rwse")) kind = 3;
        else if (component_text_word_ci(&q, "#xorigin") || component_text_word_ci(&q, "#xori")) kind = 4;
        else if (component_text_word_ci(&q, "#yorigin") || component_text_word_ci(&q, "#yori")) kind = 5;
        else if (component_text_word_ci(&q, "#dummy") || component_text_word_ci(&q, "#dumm")) kind = 6;
        else if (component_text_word_ci(&q, "#sense") || component_text_word_ci(&q, "#sens")) kind = 7;
        else if (component_text_word_ci(&q, "#gtype")) kind = 8;
        else if (component_text_word_ci(&q, "#zmin")) kind = 9;
        else if (component_text_word_ci(&q, "#zmax")) kind = 10;
        else if (component_text_word_ci(&q, "#title") || component_text_word_ci(&q, "#titl")) kind = 11;
        else if (component_text_word_ci(&q, "#rotation") || component_text_word_ci(&q, "#rota")) kind = 12;
        else return false;
        if (!z || fields & (1U << kind) || !component_text_done(&q) || !component_text_line(&q)) {
            return false;
        }
        fields |= 1U << kind;
        if (kind == 11) {
            if (q.stop - q.start > 4096) return false;
            continue;
        }
        if (kind == 0 || kind == 1 || kind == 7 || kind == 8) {
            if (!component_text_integer(&q, &value) || !component_text_done(&q)) return false;
            if (kind < 2) {
                if (value < 1 || value > 65536) return false;
                if (!kind) w = value;
                else h = value;
            } else if (kind == 7) {
                if (!value || value < -4 || value > 4) return false;
            } else if (value) return false;
        } else {
            if (!component_text_number_36_digits(&q, &v) || !component_text_done(&q) || ((kind == 2 || kind == 3) && v <= 0)) return false;
            if (kind == 6) nodata = v;
            if (kind == 9) zlo = v;
            if (kind == 10) zhi = v;
        }
    }
    if (!grid || (fields & 3) != 3 || (uint64_t)w * h > 16000000 || !component_emit(f, s, "descriptor.gxf", 0, q.p, n) ||
        !component_grid_rows(f, s, b, n, q.p, (uint64_t)w * h, nodata, &lo, &hi, pd, true))
        return false;
    if ((fields & (1 << 9)) && lo < zlo) {
        return false;
    }
    if ((fields & (1 << 10)) && hi > zhi) return false;
    s->size = (int64_t)n;
    return true;
}

void xx_gxf_grid_init(xx_gxf_grid *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_GXF_GRID, "gxf");
    }
}
xx_gxf_grid *xx_gxf_grid_create(xx_io_device *d, int64_t at)
{
    xx_gxf_grid *r = (xx_gxf_grid *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gxf_grid_init(r, d, at);
    return r;
}
void xx_gxf_grid_destroy(xx_gxf_grid *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gxf_grid_free(xx_gxf_grid *r)
{
    if (r) {
        xx_gxf_grid_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gxf_grid_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gxf_grid_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
