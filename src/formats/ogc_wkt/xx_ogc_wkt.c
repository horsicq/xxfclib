/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.ogc.org/standards/sfa/
 * OGC2D WKT POINT/LINESTRING/POLYGON/MULTIPOINT/MULTILINESTRING/MULTIPOLYGON and bounded nested GEOMETRYCOLLECTION: complete finite coordinate grammar, minimum vertex
 * counts, closed nondegenerate rings. Nonempty ASCII geometries only. Original typed geometry components exported; dimensional/SRID extensions and topology evaluation
 * declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/ogc_wkt/xx_ogc_wkt.h"
#include "../common/xx_component_lexer.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b;
    return n >= 12 && pm_read(f, 0, &b, 1) && b >= 'A' && b <= 'Z';
}
static bool wkt_pair(component_lexer *q, double *x, double *y)
{
    return component_lexer_number_hash_block_comments(q, x) && component_lexer_number_hash_block_comments(q, y);
}
static bool wkt_line(component_lexer *q, bool ring, unsigned minimum)
{
    unsigned count = 0;
    double firstx = 0, firsty = 0, lastx = 0, lasty = 0, x, y, area = 0;
    bool distinct = false;
    if (!component_lexer_char_hash_block_comments(q, '(')) return false;
    do {
        if (++count > 100000 || !wkt_pair(q, &x, &y)) return false;
        if (count == 1) {
            firstx = x;
            firsty = y;
        } else {
            area += lastx * y - x * lasty;
            if (x != firstx || y != firsty) distinct = true;
        }
        lastx = x;
        lasty = y;
    } while (component_lexer_char_hash_block_comments(q, ','));
    return count >= minimum && component_lexer_char_hash_block_comments(q, ')') && (!ring || (lastx == firstx && lasty == firsty && distinct && area != 0));
}
static bool wkt_polygon(component_lexer *q)
{
    unsigned rings = 0;
    if (!component_lexer_char_hash_block_comments(q, '(')) return false;
    do {
        if (++rings > 1024 || !wkt_line(q, true, 4)) return false;
    } while (component_lexer_char_hash_block_comments(q, ','));
    return component_lexer_char_hash_block_comments(q, ')');
}
static bool wkt_geometry(Abstractformat *f, pm_stream *s, component_lexer *q, unsigned depth)
{
    uint64_t start;
    unsigned kind, count = 0;
    double x, y;
    bool wrapped;
    if (depth > 16 || !component_lexer_skip_hash_block_comments(q)) return false;
    start = q->p;
    if (component_lexer_keyword_hash_block_comments(q, "POINT")) kind = 0;
    else if (component_lexer_keyword_hash_block_comments(q, "LINESTRING")) kind = 1;
    else if (component_lexer_keyword_hash_block_comments(q, "POLYGON")) kind = 2;
    else if (component_lexer_keyword_hash_block_comments(q, "MULTIPOINT")) kind = 3;
    else if (component_lexer_keyword_hash_block_comments(q, "MULTILINESTRING")) kind = 4;
    else if (component_lexer_keyword_hash_block_comments(q, "MULTIPOLYGON")) kind = 5;
    else if (component_lexer_keyword_hash_block_comments(q, "GEOMETRYCOLLECTION")) kind = 6;
    else return false;
    if (kind == 0) {
        if (!component_lexer_char_hash_block_comments(q, '(') || !wkt_pair(q, &x, &y) || !component_lexer_char_hash_block_comments(q, ')')) return false;
    } else if (kind == 1) {
        if (!wkt_line(q, false, 2)) return false;
    } else if (kind == 2) {
        if (!wkt_polygon(q)) return false;
    } else {
        if (!component_lexer_char_hash_block_comments(q, '(')) return false;
        wrapped = kind == 3 && component_lexer_char_hash_block_comments(q, '(');
        do {
            if (++count > 1024 || xx_component_parser_stopped(q->pd)) return false;
            if (kind == 3) {
                if (count > 1 && wrapped && !component_lexer_char_hash_block_comments(q, '(')) return false;
                if (!wkt_pair(q, &x, &y) || (wrapped && !component_lexer_char_hash_block_comments(q, ')'))) return false;
            } else if (kind == 4) {
                if (!wkt_line(q, false, 2)) return false;
            } else if (kind == 5) {
                if (!wkt_polygon(q)) return false;
            } else if (!wkt_geometry(f, s, q, depth + 1)) return false;
        } while (component_lexer_char_hash_block_comments(q, ','));
        if (!component_lexer_char_hash_block_comments(q, ')')) return false;
    }
    return kind == 6 || component_emit(f, s, "geometry.wkt", start, q->p - start, q->n);
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_lexer q = {b, 0, n, pd, 0, false, false, false};
    if (!component_utf8(b, n, true, pd) || !wkt_geometry(f, s, &q, 0) || !component_lexer_end_hash_block_comments(&q) || !s->count ||
        !component_cover(f, s, "descriptor.wkt", n))
        return false;
    s->size = (int64_t)n;
    return true;
}

void xx_ogc_wkt_init(xx_ogc_wkt *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_OGC_WKT, "wkt");
    }
}
xx_ogc_wkt *xx_ogc_wkt_create(xx_io_device *d, int64_t at)
{
    xx_ogc_wkt *r = (xx_ogc_wkt *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ogc_wkt_init(r, d, at);
    return r;
}
void xx_ogc_wkt_destroy(xx_ogc_wkt *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_ogc_wkt_free(xx_ogc_wkt *r)
{
    if (r) {
        xx_ogc_wkt_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ogc_wkt_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_ogc_wkt_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
