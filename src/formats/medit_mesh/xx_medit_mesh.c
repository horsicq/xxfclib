/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/LoicMarechal/libMeshb/master/README.md
 * INRIA MEDIT ASCII mesh v1/v2: complete dimension, finite vertex table, typed edge/triangle/quad/tetra/hex connectivity and reference labels, counted Corners and
 * RequiredVertices records. Original typed sections exported; binary/solution/high-order and other auxiliary extensions declined. Bounded32MiB input,4096 components and
 * bounded work.
 */
#include "xxfclib/formats/medit_mesh/xx_medit_mesh.h"
#include "../common/xx_component_lexer.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[20];
    return n >= 24 && pm_read(f, 0, b, 20) && component_tag(b, "MeshVersionFormatted", 20);
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_lexer q = {b, 0, n, pd, 0, true, false, false};
    int32_t version, dim, nv = 0, count, index, label;
    uint32_t seen = 0;
    unsigned section = 0;
    uint64_t start;
    double v;
    bool ended = false, have_cells = false;
    if (!component_utf8(b, n, true, pd) || !component_lexer_keyword_hash_bang_cpp_comments(&q, "MeshVersionFormatted") ||
        !component_lexer_integer_hash_bang_cpp_comments(&q, &version) || (version != 1 && version != 2) ||
        !component_lexer_keyword_hash_bang_cpp_comments(&q, "Dimension") || !component_lexer_integer_hash_bang_cpp_comments(&q, &dim) || (dim != 2 && dim != 3) ||
        !component_emit(f, s, "descriptor.mesh", 0, q.p, n))
        return false;
    while (component_lexer_skip_hash_bang_cpp_comments(&q) && q.p < n) {
        unsigned kind = 0, arity = 0;
        uint32_t bit;
        int32_t i;
        char name[48];
        start = q.p;
        if (component_lexer_keyword_hash_bang_cpp_comments(&q, "End")) {
            ended = true;
            if (!component_emit(f, s, "terminator.mesh", start, q.p - start, n)) return false;
            break;
        }
        if (component_lexer_keyword_hash_bang_cpp_comments(&q, "Vertices")) {
            kind = 1;
            arity = (unsigned)dim;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "Edges")) {
            kind = 2;
            arity = 2;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "Triangles")) {
            kind = 3;
            arity = 3;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "Quadrilaterals")) {
            kind = 4;
            arity = 4;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "Tetrahedra")) {
            kind = 5;
            arity = 4;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "Hexahedra")) {
            kind = 6;
            arity = 8;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "Corners")) {
            kind = 7;
            arity = 1;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "RequiredVertices")) {
            kind = 8;
            arity = 1;
        } else return false;
        bit = 1U << kind;
        if (seen & bit) return false;
        seen |= bit;
        if (!component_lexer_integer_hash_bang_cpp_comments(&q, &count) || count < 1 || count > 1000000 || (kind != 1 && !nv) || (dim == 2 && (kind == 5 || kind == 6)))
            return false;
        for (i = 0; i < count; ++i) {
            unsigned k;
            if (xx_component_parser_stopped(pd)) return false;
            for (k = 0; k < arity; ++k) {
                if (kind == 1) {
                    if (!component_lexer_number_hash_bang_cpp_comments(&q, &v)) return false;
                } else if (!component_lexer_integer_hash_bang_cpp_comments(&q, &index) || index < 1 || index > nv) return false;
            }
            if (kind <= 6 && (!component_lexer_integer_hash_bang_cpp_comments(&q, &label) || label < 0)) return false;
        }
        if (kind == 1) nv = count;
        else if (kind <= 6) have_cells = true;
        xx_rt_snprintf(name, sizeof(name), "section-%u.mesh", section++);
        if (!component_emit(f, s, name, start, q.p - start, n)) return false;
    }
    return nv && have_cells && (!ended || component_lexer_end_hash_bang_cpp_comments(&q)) && component_lexer_end_hash_bang_cpp_comments(&q) &&
           component_cover(f, s, "comments.mesh", n);
}

void xx_medit_mesh_init(xx_medit_mesh *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_MEDIT_MESH, "mesh");
    }
}
xx_medit_mesh *xx_medit_mesh_create(xx_io_device *d, int64_t at)
{
    xx_medit_mesh *r = (xx_medit_mesh *)xx_mem_alloc(sizeof(*r));
    if (r) xx_medit_mesh_init(r, d, at);
    return r;
}
void xx_medit_mesh_destroy(xx_medit_mesh *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_medit_mesh_free(xx_medit_mesh *r)
{
    if (r) {
        xx_medit_mesh_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_medit_mesh_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_medit_mesh_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
