/* SPDX-License-Identifier: MIT
 * Primary reference: https://doc.esri.com/en/arcgis-pro/latest/tool-reference/conversion/raster-to-ascii.html
 * Esri ASCII grid: complete case-insensitive NCOLS/NROWS, paired corner or center georeference, positive cell size, optional finite nodata, exact finite sample count;
 * fixed NCOLS/NROWS then XY/cellsize header order. Original descriptor and raster rows exported; NaN/Inf and nonuniform DX/DY extensions declined. Bounded32MiB
 * input,4096 components and bounded work.
 */
#include "xxfclib/formats/esri_ascii_grid/xx_esri_ascii_grid.h"
#include "../common/xx_component_text.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[5];
    return n >= 24 && pm_read(f, 0, b, 5) &&
           ((b[0] == 'n' || b[0] == 'N') && (b[1] == 'c' || b[1] == 'C') && (b[2] == 'o' || b[2] == 'O') && (b[3] == 'l' || b[3] == 'L') && (b[4] == 's' || b[4] == 'S'));
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t w, h;
    double x, y, cell, nodata = -9999, lo = nodata, hi = nodata;
    bool center = false;
    uint64_t at;
    if (!component_utf8(b, n, true, pd) || !component_text_line(&q) || !component_text_word_ci(&q, "ncols") || !component_text_integer(&q, &w) || w < 1 || w > 65536 ||
        !component_text_done(&q) || !component_text_line(&q) || !component_text_word_ci(&q, "nrows") || !component_text_integer(&q, &h) || h < 1 || h > 65536 ||
        !component_text_done(&q) || (uint64_t)w * h > 16000000 || !component_text_line(&q))
        return false;
    if (component_text_word_ci(&q, "xllcenter")) center = true;
    else if (!component_text_word_ci(&q, "xllcorner")) return false;
    if (!component_text_number_36_digits(&q, &x) || !component_text_done(&q) || !component_text_line(&q) ||
        !component_text_word_ci(&q, center ? "yllcenter" : "yllcorner") || !component_text_number_36_digits(&q, &y) || !component_text_done(&q) ||
        !component_text_line(&q) || !component_text_word_ci(&q, "cellsize") || !component_text_number_36_digits(&q, &cell) || cell <= 0 || !component_text_done(&q))
        return false;
    at = q.p;
    if (!component_text_line(&q)) return false;
    if (component_text_word_ci(&q, "nodata_value")) {
        if (!component_text_number_36_digits(&q, &nodata) || !component_text_done(&q)) return false;
        at = q.p;
    }
    if (!component_emit(f, s, "descriptor.asc", 0, at, n) || !component_grid_rows(f, s, b, n, at, (uint64_t)w * h, nodata, &lo, &hi, pd, false)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_esri_ascii_grid_init(xx_esri_ascii_grid *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_ESRI_ASCII_GRID, "asc");
    }
}
xx_esri_ascii_grid *xx_esri_ascii_grid_create(xx_io_device *d, int64_t at)
{
    xx_esri_ascii_grid *r = (xx_esri_ascii_grid *)xx_mem_alloc(sizeof(*r));
    if (r) xx_esri_ascii_grid_init(r, d, at);
    return r;
}
void xx_esri_ascii_grid_destroy(xx_esri_ascii_grid *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_esri_ascii_grid_free(xx_esri_ascii_grid *r)
{
    if (r) {
        xx_esri_ascii_grid_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_esri_ascii_grid_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_esri_ascii_grid_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
