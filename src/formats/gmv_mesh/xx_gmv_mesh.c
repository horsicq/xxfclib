/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/lanl/LaGriT/master/src/readgmv_binary.f
 * LANL GMV unstructured mesh subset: ASCII and little-endian IEEE32 binary nodes, fixed-arity cells, counted material/scalar variables and cycle/time records; binary additionally validates velocity, flags and polygon records with 2 to64 finite points, including LaGriT line exports. Complete local cardinalities and terminal endgmv are checked. Original typed mesh/data sections exported; structured/general-polyhedron/unknown extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/gmv_mesh/xx_gmv_mesh.h"
#include "../common/xx_component_lexer.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[8];
    return n >= 24 && pm_read(f, 0, b, 8) && component_tag(b, "gmvinput", 8);
}
static bool mesh_font_gmv_text(const uint8_t *p, const char *s) {
    size_t z = xx_rt_strlen(s), i;
    if (z > 8 || !component_tag(p, s, z))
        return false;
    for (i = z; i < 8; ++i)
        if (p[i] != 32)
            return false;
    return true;
}
static bool mesh_font_gmv_name(const uint8_t *p) {
    unsigned i;
    bool ended = false, have = false;
    for (i = 0; i < 8; ++i) {
        if (p[i] == 32) {
            ended = true;
            continue;
        }
        if (ended || !component_lexer_identifier_char(p[i]))
            return false;
        have = true;
    }
    return have;
}
static unsigned mesh_font_gmv_arity(const uint8_t *p) {
    if (mesh_font_gmv_text(p, "line"))
        return 2;
    if (mesh_font_gmv_text(p, "tri"))
        return 3;
    if (mesh_font_gmv_text(p, "quad") || mesh_font_gmv_text(p, "tet"))
        return 4;
    if (mesh_font_gmv_text(p, "pyramid"))
        return 5;
    if (mesh_font_gmv_text(p, "prism"))
        return 6;
    if (mesh_font_gmv_text(p, "hex"))
        return 8;
    return 0;
}
static bool mesh_font_gmv_binary(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_binary_cursor q = {b, 16, n, pd};
    uint32_t nodes = 0, cells = 0, seen = 0, materials = 0;
    const uint8_t *p;
    unsigned section = 0;
    char label[48];
    bool ended = false;
    if (!component_emit(f, s, "descriptor.gmv", 0, 16, n))
        return false;
    while (q.p < n) {
        uint64_t start = q.p;
        uint32_t kind = 0, count, i, location;
        unsigned arity;
        if (!component_binary_take(&q, 8, &p))
            return false;
        if (mesh_font_gmv_text(p, "nodes")) {
            kind = 1;
            if (nodes || !component_binary_count(&q, 1000000, &nodes) || nodes < 3 ||
                !component_binary_floats(&q, nodes * 3))
                return false;
        } else if (mesh_font_gmv_text(p, "cells")) {
            kind = 2;
            if (!nodes || cells || !component_binary_count(&q, 1000000, &cells) || !cells)
                return false;
            for (i = 0; i < cells; ++i) {
                uint32_t j, index, refs[8];
                if (!component_binary_take(&q, 8, &p) || (arity = mesh_font_gmv_arity(p)) == 0 ||
                    !component_binary_count(&q, 8, &count) || count != arity)
                    return false;
                for (j = 0; j < count; ++j) {
                    unsigned k;
                    if (!component_binary_take(&q, 4, &p) || (index = xx_data_get_u32(p, 4, 0, false)) < 1 ||
                        index > nodes)
                        return false;
                    for (k = 0; k < j; ++k)
                        if (refs[k] == index)
                            return false;
                    refs[j] = index;
                }
            }
        } else if (mesh_font_gmv_text(p, "material")) {
            kind = 4;
            if (!cells || !component_binary_count(&q, 4096, &materials) || !materials ||
                !component_binary_count(&q, 1, &location))
                return false;
            for (i = 0; i < materials; ++i)
                if (!component_binary_take(&q, 8, &p) || !mesh_font_gmv_name(p))
                    return false;
            count = location ? nodes : cells;
            for (i = 0; i < count; ++i)
                if (!component_binary_take(&q, 4, &p) || xx_data_get_u32(p, 4, 0, false) > materials)
                    return false;
        } else if (mesh_font_gmv_text(p, "variable")) {
            kind = 8;
            if (!cells)
                return false;
            count = 0;
            while (true) {
                if (!component_binary_take(&q, 8, &p))
                    return false;
                if (mesh_font_gmv_text(p, "endvars"))
                    break;
                if (!mesh_font_gmv_name(p) || ++count > 4096 || !component_binary_count(&q, 1, &location) ||
                    !component_binary_floats(&q, location ? nodes : cells))
                    return false;
            }
            if (!count)
                return false;
        } else if (mesh_font_gmv_text(p, "velocity")) {
            kind = 16;
            if (!cells || !component_binary_count(&q, 1, &location) ||
                !component_binary_floats(&q, (location ? nodes : cells) * 3))
                return false;
        } else if (mesh_font_gmv_text(p, "flags")) {
            kind = 32;
            if (!cells)
                return false;
            count = 0;
            while (true) {
                uint32_t types;
                if (!component_binary_take(&q, 8, &p))
                    return false;
                if (mesh_font_gmv_text(p, "endflag"))
                    break;
                if (!mesh_font_gmv_name(p) || ++count > 4096 || !component_binary_count(&q, 4096, &types) || !types ||
                    !component_binary_count(&q, 1, &location))
                    return false;
                for (i = 0; i < types; ++i)
                    if (!component_binary_take(&q, 8, &p) || !mesh_font_gmv_name(p))
                        return false;
                for (i = 0; i < (location ? nodes : cells); ++i)
                    if (!component_binary_take(&q, 4, &p) || xx_data_get_u32(p, 4, 0, false) > types)
                        return false;
            }
            if (!count)
                return false;
        } else if (mesh_font_gmv_text(p, "polygons")) {
            kind = 64;
            count = 0;
            while (true) {
                uint32_t color, points;
                if (!component_span(q.p, 8, n))
                    return false;
                if (mesh_font_gmv_text(b + q.p, "endpoly")) {
                    q.p += 8;
                    break;
                }
                if (++count > 1000000 || !component_binary_count(&q, 4096, &color) || !color ||
                    (materials && color > materials) || !component_binary_count(&q, 64, &points) || points < 2 ||
                    !component_binary_floats(&q, points * 3))
                    return false;
            }
            if (!count)
                return false;
        } else if (mesh_font_gmv_text(p, "cycleno")) {
            kind = 128;
            if (!component_binary_count(&q, 2147483647, &count))
                return false;
        } else if (mesh_font_gmv_text(p, "probtime")) {
            kind = 256;
            double v;
            if (!component_binary_float(&q, &v) || v < 0)
                return false;
        } else if (mesh_font_gmv_text(p, "endgmv")) {
            ended = true;
            if (!nodes || !cells || q.p != n)
                return false;
        } else {
            return false;
        }
        if (kind && (seen & kind))
            return false;
        seen |= kind;
        xx_rt_snprintf(label, sizeof(label), "section-%u.gmv", section++);
        if (!component_emit(f, s, label, start, q.p - start, n))
            return false;
        if (ended)
            break;
    }
    return ended && q.p == n;
}
static bool mesh_font_gmv_ascii(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_lexer q = {b, 0, n, pd, 0, false, false, false};
    int32_t nodes = 0, cells = 0, count, id;
    unsigned section = 0;
    uint32_t seen = 0;
    double v;
    bool ended = false;
    char label[48];
    if (!component_utf8(b, n, true, pd) || !component_lexer_keyword_hash_bang_cpp_comments(&q, "gmvinput") ||
        !component_lexer_keyword_hash_bang_cpp_comments(&q, "ascii") ||
        !component_emit(f, s, "descriptor.gmv", 0, q.p, n))
        return false;
    while (component_lexer_skip_hash_bang_cpp_comments(&q) && q.p < n) {
        uint64_t start = q.p;
        unsigned kind = 0;
        int32_t i, j;
        if (component_lexer_keyword_hash_bang_cpp_comments(&q, "nodes")) {
            kind = 1;
            if (nodes || !component_lexer_integer_hash_bang_cpp_comments(&q, &nodes) || nodes < 3 || nodes > 1000000)
                return false;
            for (i = 0; i < nodes * 3; ++i)
                if (!component_lexer_number_hash_bang_cpp_comments(&q, &v))
                    return false;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "cells")) {
            kind = 2;
            if (!nodes || cells || !component_lexer_integer_hash_bang_cpp_comments(&q, &cells) || cells < 1 ||
                cells > 1000000)
                return false;
            for (i = 0; i < cells; ++i) {
                unsigned arity = 0;
                if (component_lexer_keyword_hash_bang_cpp_comments(&q, "line"))
                    arity = 2;
                else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "tri"))
                    arity = 3;
                else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "quad") ||
                         component_lexer_keyword_hash_bang_cpp_comments(&q, "tet"))
                    arity = 4;
                else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "pyramid"))
                    arity = 5;
                else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "prism"))
                    arity = 6;
                else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "hex"))
                    arity = 8;
                else
                    return false;
                if (!component_lexer_integer_hash_bang_cpp_comments(&q, &count) || (unsigned)count != arity)
                    return false;
                for (j = 0; j < count; ++j)
                    if (!component_lexer_integer_hash_bang_cpp_comments(&q, &id) || id < 1 || id > nodes)
                        return false;
            }
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "material")) {
            int32_t location;
            uint64_t at, z;
            kind = 4;
            if (!cells || !component_lexer_integer_hash_bang_cpp_comments(&q, &count) || count < 1 || count > 4096 ||
                !component_lexer_integer_hash_bang_cpp_comments(&q, &location) || location < 0 || location > 1)
                return false;
            for (i = 0; i < count; ++i)
                if (!component_lexer_identifier_hash_bang_cpp_comments(&q, &at, &z) || z > 8)
                    return false;
            for (i = 0; i < (location ? nodes : cells); ++i)
                if (!component_lexer_integer_hash_bang_cpp_comments(&q, &id) || id < 0 || id > count)
                    return false;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "variable")) {
            uint64_t at, z;
            int32_t location;
            unsigned vars = 0;
            kind = 8;
            if (!cells)
                return false;
            while (!component_lexer_keyword_hash_bang_cpp_comments(&q, "endvars")) {
                if (++vars > 4096 || !component_lexer_identifier_hash_bang_cpp_comments(&q, &at, &z) || z > 8 ||
                    !component_lexer_integer_hash_bang_cpp_comments(&q, &location) || location < 0 || location > 1)
                    return false;
                for (i = 0; i < (location ? nodes : cells); ++i)
                    if (!component_lexer_number_hash_bang_cpp_comments(&q, &v))
                        return false;
            }
            if (!vars)
                return false;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "cycleno")) {
            kind = 128;
            if (!component_lexer_integer_hash_bang_cpp_comments(&q, &id) || id < 0)
                return false;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "probtime")) {
            kind = 256;
            if (!component_lexer_number_hash_bang_cpp_comments(&q, &v) || v < 0)
                return false;
        } else if (component_lexer_keyword_hash_bang_cpp_comments(&q, "endgmv")) {
            if (!nodes || !cells || !component_lexer_end_hash_bang_cpp_comments(&q))
                return false;
            ended = true;
        } else {
            return false;
        }
        if (kind && (seen & kind))
            return false;
        seen |= kind;
        xx_rt_snprintf(label, sizeof(label), "section-%u.gmv", section++);
        if (!component_emit(f, s, label, start, q.p - start, n))
            return false;
        if (ended)
            break;
    }
    return ended && component_lexer_end_hash_bang_cpp_comments(&q) && component_cover(f, s, "framing.gmv", n);
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    if (n < 16 || !component_tag(b, "gmvinput", 8))
        return false;
    if (component_tag(b + 8, "ieee    ", 8))
        return mesh_font_gmv_binary(f, s, b, n, pd);
    return mesh_font_gmv_ascii(f, s, b, n, pd);
}

void xx_gmv_mesh_init(xx_gmv_mesh *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_GMV_MESH, "gmv");
    }
}
xx_gmv_mesh *xx_gmv_mesh_create(xx_io_device *d, int64_t at) {
    xx_gmv_mesh *r = (xx_gmv_mesh *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_gmv_mesh_init(r, d, at);
    return r;
}
void xx_gmv_mesh_destroy(xx_gmv_mesh *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gmv_mesh_free(xx_gmv_mesh *r) {
    if (r) {
        xx_gmv_mesh_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gmv_mesh_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_gmv_mesh_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
