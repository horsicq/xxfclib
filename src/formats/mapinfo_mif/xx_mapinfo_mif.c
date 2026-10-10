/* SPDX-License-Identifier: MIT
 * Primary reference: https://docs.safe.com/fme/2016.0/html/FME_Desktop_Documentation/FME_ReadersWriters/mif/mif.htm
 * MapInfo MIF vector geometry subset: complete version/charset/columns descriptor and bounded typed point/line/polyline/region/rectangle/ellipse geometries with finite coordinates, checked counts/closed rings and typed pen/brush/symbol metadata. Original descriptor and geometry components exported; external MID attributes/complex projection/collection/text extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/mapinfo_mif/xx_mapinfo_mif.h"
#include "../common/xx_component_text.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[8];
    return n >= 24 && pm_read(f, 0, b, 8) && component_tag(b, "Version ", 8);
}
static bool mesh_font_mif_number(component_text_cursor *q, double *v) {
    uint64_t p;
    component_text_cursor t;
    component_text_space(q);
    p = q->t;
    while (p < q->stop && q->b[p] != ',' && q->b[p] != ')' && q->b[p] != ' ' && q->b[p] != 9)
        ++p;
    t = *q;
    t.stop = p;
    return component_text_number_36_digits(&t, v) && t.t == p ? (q->t = p, true) : false;
}
static bool mesh_font_mif_style(component_text_cursor *q, unsigned fields, unsigned colorat) {
    unsigned i;
    double v;
    component_text_space(q);
    if (q->t == q->stop || q->b[q->t++] != '(')
        return false;
    for (i = 0; i < fields; ++i) {
        if (!mesh_font_mif_number(q, &v) || v < 0 || v > 16777215 || (i == colorat && v != (double)(uint32_t)v))
            return false;
        component_text_space(q);
        if (q->t == q->stop || q->b[q->t++] != (i + 1 < fields ? ',' : ')'))
            return false;
    }
    return component_text_done(q);
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t version, columns;
    unsigned geometries = 0;
    uint64_t start = 0;
    bool data = false;
    char label[48];
    if (!component_utf8(b, n, true, pd) || !component_text_next_poison_overflow(&q) ||
        !component_text_word(&q, "Version") || !component_text_integer(&q, &version) ||
        (version != 300 && version != 450 && version != 600) || !component_text_done(&q))
        return false;
    if (!component_text_next_poison_overflow(&q) || !component_text_word(&q, "Charset") || !component_text_string(&q) ||
        !component_text_done(&q))
        return false;
    if (!component_text_next_poison_overflow(&q) || !component_text_word(&q, "Columns") ||
        !component_text_integer(&q, &columns) || columns < 0 || columns > 1024 || !component_text_done(&q))
        return false;
    while (columns--) {
        uint64_t p;
        if (!component_text_next_poison_overflow(&q))
            return false;
        component_text_space(&q);
        p = q.t;
        while (q.t < q.stop && ((b[q.t] >= 'A' && b[q.t] <= 'Z') || (b[q.t] >= 'a' && b[q.t] <= 'z') ||
                                (b[q.t] >= '0' && b[q.t] <= '9') || b[q.t] == '_'))
            ++q.t;
        if (q.t == p)
            return false;
        if (component_text_word(&q, "Integer") || component_text_word(&q, "Smallint") ||
            component_text_word(&q, "Float") || component_text_word(&q, "Logical") || component_text_word(&q, "Date")) {
            if (!component_text_done(&q))
                return false;
        } else
            return false;
    }
    if (!component_text_next_poison_overflow(&q) || !component_text_word(&q, "Data") || !component_text_done(&q) ||
        !component_emit(f, s, "descriptor.mif", 0, q.p, n)) {
        return false;
    }
    data = true;
    while (component_text_next_poison_overflow(&q)) {
        unsigned kind = 0;
        int32_t parts = 1, points, i, j;
        double x, y, firstx = 0, firsty = 0, lastx = 0, lasty = 0, area = 0;
        uint64_t end;
        if (xx_component_parser_stopped(pd) || ++geometries > 4000) {
            return false;
        }
        start = q.start;
        if (component_text_word(&q, "Point")) {
            kind = 1;
            if (!component_text_numbers_36_digits(&q, 2))
                return false;
        } else if (component_text_word(&q, "Line")) {
            kind = 2;
            if (!component_text_numbers_36_digits(&q, 4))
                return false;
        } else if (component_text_word(&q, "Pline")) {
            kind = 3;
            if (!component_text_integer(&q, &points) || points < 2 || points > 1000000 || !component_text_done(&q))
                return false;
            for (i = 0; i < points; ++i)
                if (!component_text_next_poison_overflow(&q) || !component_text_numbers_36_digits(&q, 2))
                    return false;
        } else if (component_text_word(&q, "Region")) {
            kind = 4;
            if (!component_text_integer(&q, &parts) || parts < 1 || parts > 16384 || !component_text_done(&q))
                return false;
            for (j = 0; j < parts; ++j) {
                if (!component_text_next_poison_overflow(&q) || !component_text_integer(&q, &points) || points < 4 ||
                    points > 1000000 || !component_text_done(&q))
                    return false;
                area = 0;
                for (i = 0; i < points; ++i) {
                    if (!component_text_next_poison_overflow(&q) || !component_text_number_36_digits(&q, &x) ||
                        !component_text_number_36_digits(&q, &y) || !component_text_done(&q))
                        return false;
                    if (!i) {
                        firstx = x;
                        firsty = y;
                    } else
                        area += lastx * y - x * lasty;
                    lastx = x;
                    lasty = y;
                }
                if (!component_near(firstx, lastx) || !component_near(firsty, lasty) || area == 0)
                    return false;
            }
        } else if (component_text_word(&q, "Rect") || component_text_word(&q, "Ellipse")) {
            kind = 5;
            if (!component_text_numbers_36_digits(&q, 4))
                return false;
        } else {
            return false;
        }
        end = q.p;
        while (q.p < n) {
            component_text_cursor save = q;
            if (!component_text_next_poison_overflow(&q))
                break;
            if (component_text_word(&q, "Pen")) {
                if (kind == 1 || !mesh_font_mif_style(&q, 3, 2))
                    return false;
            } else if (component_text_word(&q, "Brush")) {
                if (kind < 4 || !mesh_font_mif_style(&q, 3, 1))
                    return false;
            } else if (component_text_word(&q, "Symbol")) {
                if (kind != 1 || !mesh_font_mif_style(&q, 3, 1))
                    return false;
            } else {
                q = save;
                break;
            }
            end = q.p;
        }
        xx_rt_snprintf(label, sizeof(label), "geometry-%u.mif", geometries - 1);
        if (!component_emit(f, s, label, start, end - start, n))
            return false;
    }
    return data && geometries && q.p == q.end && component_cover(f, s, "framing.mif", n);
}

void xx_mapinfo_mif_init(xx_mapinfo_mif *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_MAPINFO_MIF, "mif");
    }
}
xx_mapinfo_mif *xx_mapinfo_mif_create(xx_io_device *d, int64_t at) {
    xx_mapinfo_mif *r = (xx_mapinfo_mif *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_mapinfo_mif_init(r, d, at);
    return r;
}
void xx_mapinfo_mif_destroy(xx_mapinfo_mif *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_mapinfo_mif_free(xx_mapinfo_mif *r) {
    if (r) {
        xx_mapinfo_mif_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_mapinfo_mif_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_mapinfo_mif_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
