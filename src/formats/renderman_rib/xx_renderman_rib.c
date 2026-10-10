/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/aqsis/aqsis/master/libs/riutil/ribparser.cpp
 * RenderMan ASCII static RIB subset: complete typed camera/display/transform/world/frame/attribute commands and Polygon/PointsPolygons/Sphere geometry, finite arrays,
 * local indexes and balanced scopes; geometry P precedes optional N/Cs/Os/st arrays. Original encoded commands exported; shaders retained only as typed metadata, never
 * loaded. Binary RIB, motion, archives/procedurals, custom parameters and rendering declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/renderman_rib/xx_renderman_rib.h"
#include "../common/xx_component_lexer.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b;
    return n >= 24 && pm_read(f, 0, &b, 1) && b >= 9 && b <= 126;
}
static bool rib_numbers(component_lexer *q, unsigned count, bool positive, bool color)
{
    unsigned i;
    double v;
    for (i = 0; i < count; ++i)
        if (!component_lexer_number_hash_block_comments(q, &v) || (positive && v <= 0) || (color && (v < 0 || v > 1))) return false;
    return true;
}
static bool rib_scalar(component_lexer *q, double *v)
{
    bool array = component_lexer_char_hash_block_comments(q, '[');
    return component_lexer_number_hash_block_comments(q, v) && (!array || component_lexer_char_hash_block_comments(q, ']'));
}
static bool rib_array(component_lexer *q, uint32_t *count, bool integer, uint32_t *maximum, uint64_t *sum, bool color)
{
    uint32_t used = 0;
    double v;
    int32_t i;
    if (!component_lexer_char_hash_block_comments(q, '[')) return false;
    while (!component_lexer_char_hash_block_comments(q, ']')) {
        if (++used > 300000 || xx_component_parser_stopped(q->pd)) return false;
        if (integer) {
            if (!component_lexer_integer_hash_block_comments(q, &i) || i < 0) return false;
            if (maximum && (uint32_t)i > *maximum) *maximum = (uint32_t)i;
            if (sum) *sum += (uint32_t)i;
        } else if (!component_lexer_number_hash_block_comments(q, &v) || (color && (v < 0 || v > 1))) return false;
    }
    *count = used;
    return used > 0;
}
static bool rib_parameters(component_lexer *q, bool geometry, uint32_t *vertices)
{
    unsigned fields = 0;
    while (component_lexer_skip_hash_block_comments(q) && q->p < q->n && q->b[q->p] == '"') {
        uint64_t p, z;
        unsigned kind;
        uint32_t count = 0;
        double value;
        if (!component_lexer_quoted_hash_block_comments(q, '"', &p, &z)) return false;
        if (geometry) {
            if (z == 1 && q->b[p] == 'P') kind = 0;
            else if (z == 1 && q->b[p] == 'N') kind = 1;
            else if (z == 2 && component_tag(q->b + p, "Cs", 2)) kind = 2;
            else if (z == 2 && component_tag(q->b + p, "Os", 2)) kind = 3;
            else if (z == 2 && component_tag(q->b + p, "st", 2)) kind = 4;
            else return false;
            if (fields & (1U << kind) || !rib_array(q, &count, false, NULL, NULL, kind == 2 || kind == 3)) {
                return false;
            }
            fields |= 1U << kind;
            if (!kind) {
                if (count % 3 || count < 9) return false;
                *vertices = count / 3;
            } else if (!*vertices || count != *vertices * (kind == 4 ? 2 : 3)) return false;
        } else {
            if ((z == 2 && (component_tag(q->b + p, "Ka", 2) || component_tag(q->b + p, "Kd", 2) || component_tag(q->b + p, "Ks", 2))) ||
                (z == 9 && component_tag(q->b + p, "roughness", 9)) || (z == 6 && component_tag(q->b + p, "sphere", 6))) {
                if (!rib_scalar(q, &value) || value < 0 || value > 1000000) return false;
            } else if (z == 11 && component_tag(q->b + p, "compression", 11)) {
                if (!component_lexer_quoted_hash_block_comments(q, '"', NULL, NULL)) return false;
            } else return false;
        }
    }
    return !geometry || ((fields & 1) != 0);
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_lexer q = {b, 0, n, pd, 0, true, false, false};
    uint8_t stack[64];
    unsigned depth = 0;
    bool world = false, frame = false;
    unsigned worlds = 0, geometry = 0;
    if (!component_utf8(b, n, false, pd)) return false;
    while (!component_lexer_end_hash_block_comments(&q)) {
        uint64_t start, p, z;
        double v;
        uint32_t vertices = 0;
        int32_t integer;
        if (!component_lexer_skip_hash_block_comments(&q)) return false;
        start = q.p;
        if (component_lexer_keyword_hash_block_comments(&q, "WorldBegin")) {
            if (world || depth >= 64) return false;
            stack[depth++] = 3;
            world = true;
        } else if (component_lexer_keyword_hash_block_comments(&q, "WorldEnd")) {
            if (!world || !depth || stack[--depth] != 3) return false;
            world = false;
            ++worlds;
        } else if (component_lexer_keyword_hash_block_comments(&q, "FrameBegin")) {
            if (frame || world || depth || !component_lexer_integer_hash_block_comments(&q, &integer) || integer < 0) return false;
            stack[depth++] = 4;
            frame = true;
        } else if (component_lexer_keyword_hash_block_comments(&q, "FrameEnd")) {
            if (world || !frame || !depth || stack[--depth] != 4) return false;
            frame = false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "AttributeBegin")) {
            if (!world || depth >= 64) return false;
            stack[depth++] = 1;
        } else if (component_lexer_keyword_hash_block_comments(&q, "AttributeEnd")) {
            if (!depth || stack[--depth] != 1) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "TransformBegin")) {
            if (depth >= 64) return false;
            stack[depth++] = 2;
        } else if (component_lexer_keyword_hash_block_comments(&q, "TransformEnd")) {
            if (!depth || stack[--depth] != 2) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Format")) {
            if (world || !component_lexer_integer_hash_block_comments(&q, &integer) || integer < 1 || integer > 65536 ||
                !component_lexer_integer_hash_block_comments(&q, &integer) || integer < 1 || integer > 65536 || !rib_numbers(&q, 1, true, false))
                return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Display")) {
            if (world || !component_lexer_quoted_hash_block_comments(&q, '"', NULL, NULL) || !component_lexer_quoted_hash_block_comments(&q, '"', NULL, NULL) ||
                !component_lexer_quoted_hash_block_comments(&q, '"', NULL, NULL) || !rib_parameters(&q, false, &vertices))
                return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Projection")) {
            if (world || !component_lexer_quoted_hash_block_comments(&q, '"', &p, &z) ||
                !((z == 11 && component_tag(b + p, "perspective", 11)) || (z == 12 && component_tag(b + p, "orthographic", 12))))
                return false;
            if (component_lexer_skip_hash_block_comments(&q) && q.p < n && b[q.p] == '"') {
                if (!component_lexer_quoted_hash_block_comments(&q, '"', &p, &z) || z != 3 || !component_tag(b + p, "fov", 3) || !rib_scalar(&q, &v) || v <= 0 ||
                    v >= 180)
                    return false;
            }
        } else if (component_lexer_keyword_hash_block_comments(&q, "PixelSamples")) {
            if (world || !rib_numbers(&q, 2, true, false)) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "PixelFilter")) {
            if (world || !component_lexer_quoted_hash_block_comments(&q, '"', NULL, NULL) || !rib_numbers(&q, 2, true, false)) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "ShadingRate")) {
            if (!rib_numbers(&q, 1, true, false)) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Translate")) {
            if (!rib_numbers(&q, 3, false, false)) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Scale")) {
            unsigned i;
            for (i = 0; i < 3; ++i)
                if (!component_lexer_number_hash_block_comments(&q, &v) || v == 0) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Rotate")) {
            double x, y, zv;
            if (!component_lexer_number_hash_block_comments(&q, &v) || !component_lexer_number_hash_block_comments(&q, &x) ||
                !component_lexer_number_hash_block_comments(&q, &y) || !component_lexer_number_hash_block_comments(&q, &zv) || (x == 0 && y == 0 && zv == 0))
                return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Identity")) {
        } else if (component_lexer_keyword_hash_block_comments(&q, "Transform") || component_lexer_keyword_hash_block_comments(&q, "ConcatTransform")) {
            uint32_t count;
            if (!rib_array(&q, &count, false, NULL, NULL, false) || count != 16) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Color") || component_lexer_keyword_hash_block_comments(&q, "Opacity")) {
            if (!world || !rib_numbers(&q, 3, false, true)) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Sides")) {
            if (!component_lexer_integer_hash_block_comments(&q, &integer) || (integer != 1 && integer != 2)) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Orientation")) {
            if (!component_lexer_quoted_hash_block_comments(&q, '"', &p, &z) ||
                !((z == 2 && (component_tag(b + p, "lh", 2) || component_tag(b + p, "rh", 2))) || (z == 6 && component_tag(b + p, "inside", 6)) ||
                  (z == 7 && component_tag(b + p, "outside", 7))))
                return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Surface") || component_lexer_keyword_hash_block_comments(&q, "Displacement")) {
            if (!world || !component_lexer_quoted_hash_block_comments(&q, '"', NULL, NULL) || !rib_parameters(&q, false, &vertices)) return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Attribute")) {
            if (!world || !component_lexer_quoted_hash_block_comments(&q, '"', &p, &z) || z != 17 || !component_tag(b + p, "displacementbound", 17) ||
                !rib_parameters(&q, false, &vertices))
                return false;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Polygon")) {
            if (!world || !rib_parameters(&q, true, &vertices)) return false;
            ++geometry;
        } else if (component_lexer_keyword_hash_block_comments(&q, "PointsPolygons")) {
            uint32_t count, maxindex = 0, indexcount;
            uint64_t sum = 0;
            uint64_t begin;
            if (!world) {
                return false;
            }
            begin = q.p;
            if (!rib_array(&q, &count, true, NULL, &sum, false) || count > 100000 || sum > 300000) return false;
            {
                component_lexer check = q;
                check.p = begin;
                if (!component_lexer_char_hash_block_comments(&check, '[')) return false;
                while (!component_lexer_char_hash_block_comments(&check, ']'))
                    if (!component_lexer_integer_hash_block_comments(&check, &integer) || integer < 3 || integer > 100000) return false;
            }
            if (!rib_array(&q, &indexcount, true, &maxindex, NULL, false) || indexcount != sum || !rib_parameters(&q, true, &vertices) || maxindex >= vertices) {
                return false;
            }
            ++geometry;
        } else if (component_lexer_keyword_hash_block_comments(&q, "Sphere")) {
            double radius, zmin, zmax, theta;
            if (!world || !component_lexer_number_hash_block_comments(&q, &radius) || radius <= 0 || !component_lexer_number_hash_block_comments(&q, &zmin) ||
                !component_lexer_number_hash_block_comments(&q, &zmax) || zmin < -radius || zmax > radius || zmin >= zmax ||
                !component_lexer_number_hash_block_comments(&q, &theta) || theta <= 0 || theta > 360)
                return false;
            ++geometry;
        } else return false;
        if (!component_emit(f, s, "command.rib", start, q.p - start, n)) return false;
    }
    if (depth || world || frame || !worlds || !geometry || !component_cover(f, s, "comments.rib", n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_renderman_rib_init(xx_renderman_rib *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_RENDERMAN_RIB, "rib");
    }
}
xx_renderman_rib *xx_renderman_rib_create(xx_io_device *d, int64_t at)
{
    xx_renderman_rib *r = (xx_renderman_rib *)xx_mem_alloc(sizeof(*r));
    if (r) xx_renderman_rib_init(r, d, at);
    return r;
}
void xx_renderman_rib_destroy(xx_renderman_rib *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_renderman_rib_free(xx_renderman_rib *r)
{
    if (r) {
        xx_renderman_rib_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_renderman_rib_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_renderman_rib_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
