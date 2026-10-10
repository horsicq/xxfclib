/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.khronos.org/files/collada_spec_1_4.pdf
 * COLLADA1.4.1 static geometry/scene subset: complete bounded XML grammar, counted float arrays/accessors/indexed triangles, local identifiers/references and finite scene transforms. Original asset/library/scene structures exported; effects/animation/cameras/lights/controllers/custom extensions, XML entities/comments/CDATA/DOCTYPE and non-UTF8 encodings declined. ASCII NCName identifiers, calendar-valid ISO timestamps and XML1.0 declaration are checked.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/collada_dae/xx_collada_dae.h"
#include "../common/xx_component_lexer.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n) {
    uint8_t c;
    return n >= 32 && pm_read(f, 0, &c, 1) && (c == '<' || c == 32 || c == 9 || c == 10 || c == 13);
}
enum {
    VX_COLLADA = 1,
    VX_ASSET,
    VX_CREATED,
    VX_MODIFIED,
    VX_UP,
    VX_UNIT,
    VX_GEOLIB,
    VX_GEOMETRY,
    VX_MESH,
    VX_SOURCE,
    VX_ARRAY,
    VX_TECH,
    VX_ACCESSOR,
    VX_PARAM,
    VX_VERTICES,
    VX_INPUT,
    VX_TRIANGLES,
    VX_P,
    VX_VISLIB,
    VX_VISUAL,
    VX_NODE,
    VX_GEOMINST,
    VX_TRANSLATE,
    VX_ROTATE,
    VX_SCALE,
    VX_MATRIX,
    VX_SCENE,
    VX_VISINST
};
static const char *const scene_bitmap_xtags[] = {"",
                                                 "COLLADA",
                                                 "asset",
                                                 "created",
                                                 "modified",
                                                 "up_axis",
                                                 "unit",
                                                 "library_geometries",
                                                 "geometry",
                                                 "mesh",
                                                 "source",
                                                 "float_array",
                                                 "technique_common",
                                                 "accessor",
                                                 "param",
                                                 "vertices",
                                                 "input",
                                                 "triangles",
                                                 "p",
                                                 "library_visual_scenes",
                                                 "visual_scene",
                                                 "node",
                                                 "instance_geometry",
                                                 "translate",
                                                 "rotate",
                                                 "scale",
                                                 "matrix",
                                                 "scene",
                                                 "instance_visual_scene"};
static const char *const scene_bitmap_xattrs[] = {"",         "xmlns",  "version", "id",     "name",
                                                  "count",    "source", "stride",  "offset", "type",
                                                  "semantic", "url",    "set",     "meter",  "sid"};
typedef struct scene_bitmap_xattr {
    uint32_t p, z;
    unsigned kind;
} scene_bitmap_xattr;
typedef struct scene_bitmap_xnode {
    unsigned kind, parent, child, next;
    uint64_t start, text, stop, end;
    scene_bitmap_xattr attrs[8];
    unsigned count;
    uint32_t value, width;
} scene_bitmap_xnode;
typedef struct scene_bitmap_xml {
    const uint8_t *b;
    uint64_t n, p;
    scene_bitmap_xnode *nodes;
    unsigned count;
    xx_pd_struct *pd;
    uint32_t work;
} scene_bitmap_xml;
static bool scene_bitmap_xspace(uint8_t c) { return c == 32 || c == 9 || c == 10 || c == 13; }
static bool scene_bitmap_xskip(scene_bitmap_xml *q) {
    while (q->p < q->n && scene_bitmap_xspace(q->b[q->p])) {
        if (++q->work > 16000000 || xx_component_parser_stopped(q->pd))
            return false;
        ++q->p;
    }
    return true;
}
static unsigned scene_bitmap_xname(scene_bitmap_xml *q, bool attribute) {
    uint64_t p = q->p;
    unsigned i, max = attribute ? 15 : 29;
    const char *const *table = attribute ? scene_bitmap_xattrs : scene_bitmap_xtags;
    while (q->p < q->n && component_lexer_identifier_char(q->b[q->p])) {
        if (q->p - p >= 64)
            return 0;
        ++q->p;
    }
    for (i = 1; i < max; ++i) {
        size_t z = xx_rt_strlen(table[i]);
        if (z == q->p - p && component_tag(q->b + p, table[i], z))
            return i;
    }
    return 0;
}
static bool scene_bitmap_xparse(scene_bitmap_xml *q, unsigned parent, unsigned depth, unsigned *result) {
    scene_bitmap_xnode *v;
    unsigned id, last = 0, i;
    if (depth > 64 || q->count >= 4096 || xx_component_parser_stopped(q->pd) || q->p == q->n || q->b[q->p++] != '<')
        return false;
    id = ++q->count;
    v = &q->nodes[id];
    v->start = q->p - 1;
    v->parent = parent;
    if (!(v->kind = scene_bitmap_xname(q, false)))
        return false;
    while (q->p < q->n) {
        scene_bitmap_xattr *a;
        uint8_t quote;
        uint64_t p;
        bool space = false;
        while (q->p < q->n && scene_bitmap_xspace(q->b[q->p])) {
            space = true;
            ++q->p;
        }
        if (q->p == q->n)
            return false;
        if (q->b[q->p] == '/' || q->b[q->p] == '>')
            break;
        if (!space || v->count >= 8)
            return false;
        a = &v->attrs[v->count];
        if (!(a->kind = scene_bitmap_xname(q, true)))
            return false;
        for (i = 0; i < v->count; ++i)
            if (v->attrs[i].kind == a->kind)
                return false;
        ++v->count;
        if (!scene_bitmap_xskip(q) || q->p == q->n || q->b[q->p++] != '=' || !scene_bitmap_xskip(q) || q->p == q->n ||
            ((quote = q->b[q->p++]) != '"' && quote != '\''))
            return false;
        p = q->p;
        while (q->p < q->n && q->b[q->p] != quote) {
            uint8_t c = q->b[q->p++];
            if (q->p - p > 255 || c == '<' || c == '&' || c < 32)
                return false;
        }
        if (q->p == q->n)
            return false;
        a->p = (uint32_t)p;
        a->z = (uint32_t)(q->p - p);
        ++q->p;
    }
    if (q->b[q->p] == '/') {
        ++q->p;
        if (q->p == q->n || q->b[q->p++] != '>')
            return false;
        v->text = v->stop = q->p;
        v->end = q->p;
        *result = id;
        return true;
    }
    if (q->b[q->p++] != '>')
        return false;
    v->text = q->p;
    while (q->p < q->n) {
        if (++q->work > 16000000 || xx_component_parser_stopped(q->pd))
            return false;
        if (q->b[q->p] == '<') {
            if (component_span(q->p, 2, q->n) && q->b[q->p + 1] == '/') {
                v->stop = q->p;
                q->p += 2;
                if (scene_bitmap_xname(q, false) != v->kind || !scene_bitmap_xskip(q) || q->p == q->n ||
                    q->b[q->p++] != '>')
                    return false;
                v->end = q->p;
                *result = id;
                return true;
            } else {
                unsigned c;
                if (!scene_bitmap_xparse(q, id, depth + 1, &c))
                    return false;
                if (last)
                    q->nodes[last].next = c;
                else
                    v->child = c;
                last = c;
            }
        } else {
            if (q->b[q->p] == '&' || q->b[q->p] == '>')
                return false;
            ++q->p;
        }
    }
    return false;
}
static const scene_bitmap_xattr *scene_bitmap_xattr_get(scene_bitmap_xnode *v, unsigned type) {
    unsigned i;
    for (i = 0; i < v->count; ++i)
        if (v->attrs[i].kind == type)
            return &v->attrs[i];
    return NULL;
}
static bool scene_bitmap_xeq(scene_bitmap_xml *q, const scene_bitmap_xattr *a, const char *s) {
    return a && a->z == xx_rt_strlen(s) && component_tag(q->b + a->p, s, a->z);
}
static bool scene_bitmap_xuint(scene_bitmap_xml *q, scene_bitmap_xnode *v, unsigned type, uint32_t def, uint32_t *out,
                               bool required) {
    const scene_bitmap_xattr *a = scene_bitmap_xattr_get(v, type);
    uint32_t u = 0;
    unsigned i;
    if (!a) {
        *out = def;
        return !required;
    }
    if (!a->z)
        return false;
    for (i = 0; i < a->z; ++i) {
        uint8_t c = q->b[a->p + i];
        if (c < '0' || c > '9' || u > (1000000U - (c - '0')) / 10)
            return false;
        u = u * 10 + c - '0';
    }
    *out = u;
    return true;
}
static bool scene_bitmap_xattrs_ok(scene_bitmap_xnode *v, uint32_t allowed) {
    unsigned i;
    for (i = 0; i < v->count; ++i)
        if (!(allowed & (1U << v->attrs[i].kind)))
            return false;
    return true;
}
static bool scene_bitmap_xempty(scene_bitmap_xml *q, scene_bitmap_xnode *v) {
    uint64_t p = v->text;
    if (v->child)
        return false;
    while (p < v->stop)
        if (!scene_bitmap_xspace(q->b[p++]))
            return false;
    return true;
}
static bool scene_bitmap_xcontainer(scene_bitmap_xml *q, scene_bitmap_xnode *v) {
    uint64_t p = v->text;
    unsigned i = v->child;
    while (i) {
        scene_bitmap_xnode *c = &q->nodes[i];
        while (p < c->start)
            if (!scene_bitmap_xspace(q->b[p++]))
                return false;
        p = c->end;
        i = c->next;
    }
    while (p < v->stop)
        if (!scene_bitmap_xspace(q->b[p++]))
            return false;
    return true;
}
static unsigned scene_bitmap_xfind(scene_bitmap_xml *q, const scene_bitmap_xattr *a, unsigned expected) {
    unsigned i;
    if (!a || a->z < 2 || q->b[a->p] != '#')
        return 0;
    for (i = 1; i <= q->count; ++i) {
        const scene_bitmap_xattr *id = scene_bitmap_xattr_get(&q->nodes[i], 3);
        if (id && id->z == a->z - 1 && component_tag(q->b + id->p, (const char *)q->b + a->p + 1, id->z))
            return q->nodes[i].kind == expected ? i : 0;
    }
    return 0;
}
static bool scene_bitmap_xnumbers(scene_bitmap_xml *q, scene_bitmap_xnode *v, uint32_t count, bool integer) {
    component_lexer t = {q->b, v->text, v->stop, q->pd, 0, false, false, false};
    uint32_t i;
    int32_t value;
    double number;
    if (v->child)
        return false;
    for (i = 0; i < count; ++i)
        if (integer ? !component_lexer_integer_hash_bang_cpp_comments_delimited(&t, &value)
                    : !component_lexer_number_hash_bang_cpp_comments(&t, &number))
            return false;
    return component_lexer_end_hash_bang_cpp_comments(&t);
}
static unsigned scene_bitmap_xdigits(const uint8_t *b, unsigned z) {
    unsigned v = 0, i;
    for (i = 0; i < z; ++i)
        v = v * 10 + b[i] - '0';
    return v;
}
static bool scene_bitmap_xtime(scene_bitmap_xml *q, scene_bitmap_xnode *v) {
    uint64_t p = v->text, z = v->stop - p;
    unsigned i, year, month, day, maxday;
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (v->child || z < 19 || z > 33)
        return false;
    for (i = 0; i < 19; ++i) {
        uint8_t c = q->b[p + i];
        if (i == 4 || i == 7) {
            if (c != '-')
                return false;
        } else if (i == 10) {
            if (c != 'T')
                return false;
        } else if (i == 13 || i == 16) {
            if (c != ':')
                return false;
        } else if (c < '0' || c > '9')
            return false;
    }
    year = scene_bitmap_xdigits(q->b + p, 4);
    month = scene_bitmap_xdigits(q->b + p + 5, 2);
    day = scene_bitmap_xdigits(q->b + p + 8, 2);
    if (!year || month < 1 || month > 12 || scene_bitmap_xdigits(q->b + p + 11, 2) > 23 ||
        scene_bitmap_xdigits(q->b + p + 14, 2) > 59 || scene_bitmap_xdigits(q->b + p + 17, 2) > 59)
        return false;
    maxday = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
        ++maxday;
    if (!day || day > maxday)
        return false;
    if (z > 19) {
        i = 19;
        if (q->b[p + i] == '.') {
            ++i;
            while (i < z && q->b[p + i] >= '0' && q->b[p + i] <= '9')
                ++i;
            if (i == 20)
                return false;
        }
        if (i < z && q->b[p + i] == 'Z')
            ++i;
        if (i != z)
            return false;
    }
    return true;
}
static bool scene_bitmap_xdecl(scene_bitmap_xml *q) {
    component_lexer t = {q->b, q->p + 5, q->n, q->pd, 0, false, false, false};
    uint64_t at, z;
    if (t.p == t.n || !scene_bitmap_xspace(t.b[t.p]) ||
        !component_lexer_keyword_hash_bang_cpp_comments(&t, "version") ||
        !component_lexer_char_hash_bang_cpp_comments(&t, '=') || !component_lexer_skip_hash_bang_cpp_comments(&t) ||
        t.p == t.n || (t.b[t.p] != '\"' && t.b[t.p] != '\'') ||
        !component_lexer_quoted_hash_bang_cpp_comments(&t, t.b[t.p], &at, &z) || z != 3 ||
        !component_tag(t.b + at, "1.0", 3))
        return false;
    if (t.p < t.n && !scene_bitmap_xspace(t.b[t.p]) && t.b[t.p] != '?')
        return false;
    if (component_lexer_skip_hash_bang_cpp_comments(&t) && component_span(t.p, 8, t.n) &&
        component_tag(t.b + t.p, "encoding", 8)) {
        if (!component_lexer_keyword_hash_bang_cpp_comments(&t, "encoding") ||
            !component_lexer_char_hash_bang_cpp_comments(&t, '=') || !component_lexer_skip_hash_bang_cpp_comments(&t) ||
            t.p == t.n || (t.b[t.p] != '\"' && t.b[t.p] != '\'') ||
            !component_lexer_quoted_hash_bang_cpp_comments(&t, t.b[t.p], &at, &z) || z != 5 ||
            (!component_tag(t.b + at, "utf-8", 5) && !component_tag(t.b + at, "UTF-8", 5)))
            return false;
        if (t.p < t.n && !scene_bitmap_xspace(t.b[t.p]) && t.b[t.p] != '?')
            return false;
    }
    if (component_lexer_skip_hash_bang_cpp_comments(&t) && component_span(t.p, 10, t.n) &&
        component_tag(t.b + t.p, "standalone", 10)) {
        if (!component_lexer_keyword_hash_bang_cpp_comments(&t, "standalone") ||
            !component_lexer_char_hash_bang_cpp_comments(&t, '=') || !component_lexer_skip_hash_bang_cpp_comments(&t) ||
            t.p == t.n || (t.b[t.p] != '\"' && t.b[t.p] != '\'') ||
            !component_lexer_quoted_hash_bang_cpp_comments(&t, t.b[t.p], &at, &z) ||
            !((z == 3 && component_tag(t.b + at, "yes", 3)) || (z == 2 && component_tag(t.b + at, "no", 2))))
            return false;
    }
    if (!component_lexer_skip_hash_bang_cpp_comments(&t) || !component_span(t.p, 2, t.n) || t.b[t.p] != '?' ||
        t.b[t.p + 1] != '>' || t.p - q->p > 128)
        return false;
    q->p = t.p + 2;
    return scene_bitmap_xskip(q);
}

static bool scene_bitmap_xvalidate(scene_bitmap_xml *q) {
    unsigned i, j;
    uint64_t work = 0;
    for (i = 1; i <= q->count; ++i) {
        scene_bitmap_xnode *v = &q->nodes[i], *parent = &q->nodes[v->parent];
        const scene_bitmap_xattr *id = scene_bitmap_xattr_get(v, 3);
        uint32_t allowed = 0;
        unsigned c = v->child;
        if (id) {
            if (!id->z || id->z > 63 ||
                !((q->b[id->p] >= 'A' && q->b[id->p] <= 'Z') || (q->b[id->p] >= 'a' && q->b[id->p] <= 'z') ||
                  q->b[id->p] == '_'))
                return false;
            for (j = 0; j < id->z; ++j)
                if (!component_lexer_identifier_char(q->b[id->p + j]))
                    return false;
            for (j = 1; j < i; ++j) {
                const scene_bitmap_xattr *other = scene_bitmap_xattr_get(&q->nodes[j], 3);
                if (++work > 16000000 || xx_component_parser_stopped(q->pd))
                    return false;
                if (other && other->z == id->z && component_tag(q->b + other->p, (const char *)q->b + id->p, id->z))
                    return false;
            }
        }
        switch (v->kind) {
        case VX_COLLADA:
            allowed = (1U << 1) | (1U << 2);
            if (v->parent ||
                !scene_bitmap_xeq(q, scene_bitmap_xattr_get(v, 1), "http://www.collada.org/2005/11/COLLADASchema") ||
                !scene_bitmap_xeq(q, scene_bitmap_xattr_get(v, 2), "1.4.1"))
                return false;
            {
                unsigned required = 0;
                while (c) {
                    unsigned kind = q->nodes[c].kind, bit = kind == VX_ASSET    ? 1
                                                            : kind == VX_GEOLIB ? 2
                                                            : kind == VX_VISLIB ? 4
                                                            : kind == VX_SCENE  ? 8
                                                                                : 0;
                    if (!bit || (required & bit))
                        return false;
                    required |= bit;
                    c = q->nodes[c].next;
                }
                if (required != 15)
                    return false;
            }
            break;
        case VX_ASSET:
            if (parent->kind != VX_COLLADA)
                return false;
            {
                unsigned seen = 0;
                while (c) {
                    unsigned kind = q->nodes[c].kind, bit = kind == VX_CREATED    ? 1
                                                            : kind == VX_MODIFIED ? 2
                                                            : kind == VX_UP       ? 4
                                                            : kind == VX_UNIT     ? 8
                                                                                  : 0;
                    if (!bit || (seen & bit))
                        return false;
                    seen |= bit;
                    c = q->nodes[c].next;
                }
                if ((seen & 3) != 3)
                    return false;
            }
            break;
        case VX_CREATED:
        case VX_MODIFIED:
            if (parent->kind != VX_ASSET || !scene_bitmap_xtime(q, v))
                return false;
            break;
        case VX_UP:
            if (parent->kind != VX_ASSET || v->child || v->stop - v->text != 4 ||
                (!component_tag(q->b + v->text, "X_UP", 4) && !component_tag(q->b + v->text, "Y_UP", 4) &&
                 !component_tag(q->b + v->text, "Z_UP", 4)))
                return false;
            break;
        case VX_UNIT:
            allowed = (1U << 4) | (1U << 13);
            {
                const scene_bitmap_xattr *a = scene_bitmap_xattr_get(v, 13);
                component_lexer t;
                double num;
                if (parent->kind != VX_ASSET || !a || !scene_bitmap_xempty(q, v))
                    return false;
                t.b = q->b;
                t.p = a->p;
                t.n = a->p + a->z;
                t.pd = q->pd;
                t.work = 0;
                t.hash = t.commas = t.comments = false;
                if (!component_lexer_number_hash_bang_cpp_comments(&t, &num) || num <= 0 ||
                    !component_lexer_end_hash_bang_cpp_comments(&t))
                    return false;
            }
            break;
        case VX_GEOLIB:
        case VX_VISLIB:
            if (parent->kind != VX_COLLADA || !c)
                return false;
            while (c) {
                if (q->nodes[c].kind != (unsigned)(v->kind == VX_GEOLIB ? VX_GEOMETRY : VX_VISUAL))
                    return false;
                c = q->nodes[c].next;
            }
            break;
        case VX_GEOMETRY:
            allowed = (1U << 3) | (1U << 4);
            if (parent->kind != VX_GEOLIB || !id || !c || q->nodes[c].kind != VX_MESH || q->nodes[c].next)
                return false;
            break;
        case VX_MESH:
            if (parent->kind != VX_GEOMETRY)
                return false;
            {
                unsigned sources = 0, vertices = 0, triangles = 0;
                while (c) {
                    unsigned kind = q->nodes[c].kind;
                    if (kind == VX_SOURCE)
                        ++sources;
                    else if (kind == VX_VERTICES)
                        ++vertices;
                    else if (kind == VX_TRIANGLES)
                        ++triangles;
                    else
                        return false;
                    c = q->nodes[c].next;
                }
                if (!sources || vertices != 1 || !triangles)
                    return false;
            }
            break;
        case VX_SOURCE:
            allowed = (1U << 3) | (1U << 4);
            if (parent->kind != VX_MESH || !id || !c || q->nodes[c].kind != VX_ARRAY || !q->nodes[c].next ||
                q->nodes[q->nodes[c].next].kind != VX_TECH || q->nodes[q->nodes[c].next].next)
                return false;
            break;
        case VX_ARRAY:
            allowed = (1U << 3) | (1U << 5);
            if (parent->kind != VX_SOURCE || !id || !scene_bitmap_xuint(q, v, 5, 0, &v->value, true) || !v->value ||
                !scene_bitmap_xnumbers(q, v, v->value, false))
                return false;
            break;
        case VX_TECH:
            if (parent->kind != VX_SOURCE || !c || q->nodes[c].kind != VX_ACCESSOR || q->nodes[c].next)
                return false;
            break;
        case VX_ACCESSOR:
            allowed = (1U << 5) | (1U << 6) | (1U << 7) | (1U << 8);
            {
                uint32_t offset;
                unsigned array = scene_bitmap_xfind(q, scene_bitmap_xattr_get(v, 6), VX_ARRAY), params = 0;
                if (parent->kind != VX_TECH || !array || q->nodes[array].parent != parent->parent ||
                    !scene_bitmap_xuint(q, v, 5, 0, &v->value, true) || !v->value ||
                    !scene_bitmap_xuint(q, v, 7, 1, &v->width, false) || !v->width || v->width > 16 ||
                    !scene_bitmap_xuint(q, v, 8, 0, &offset, false))
                    return false;
                while (c) {
                    if (q->nodes[c].kind != VX_PARAM)
                        return false;
                    ++params;
                    c = q->nodes[c].next;
                }
                if (params != v->width || (uint64_t)offset + (uint64_t)v->value * v->width > q->nodes[array].value)
                    return false;
                q->nodes[parent->parent].value = v->value;
                q->nodes[parent->parent].width = v->width;
            }
            break;
        case VX_PARAM:
            allowed = (1U << 4) | (1U << 9);
            if (parent->kind != VX_ACCESSOR || !scene_bitmap_xeq(q, scene_bitmap_xattr_get(v, 9), "float") ||
                !scene_bitmap_xattr_get(v, 4) || !scene_bitmap_xempty(q, v))
                return false;
            break;
        case VX_VERTICES:
            allowed = (1U << 3);
            if (parent->kind != VX_MESH || !id || !c || q->nodes[c].kind != VX_INPUT || q->nodes[c].next)
                return false;
            break;
        case VX_INPUT:
            allowed = (1U << 6) | (1U << 8) | (1U << 10) | (1U << 12);
            if ((parent->kind != VX_VERTICES && parent->kind != VX_TRIANGLES) || !scene_bitmap_xempty(q, v))
                return false;
            {
                const scene_bitmap_xattr *semantic = scene_bitmap_xattr_get(v, 10);
                unsigned target = 0;
                uint32_t set;
                if (parent->kind == VX_VERTICES) {
                    if (!scene_bitmap_xeq(q, semantic, "POSITION") || scene_bitmap_xattr_get(v, 8) ||
                        scene_bitmap_xattr_get(v, 12))
                        return false;
                    target = scene_bitmap_xfind(q, scene_bitmap_xattr_get(v, 6), VX_SOURCE);
                } else {
                    if (scene_bitmap_xeq(q, semantic, "VERTEX"))
                        target = scene_bitmap_xfind(q, scene_bitmap_xattr_get(v, 6), VX_VERTICES);
                    else if (scene_bitmap_xeq(q, semantic, "NORMAL") || scene_bitmap_xeq(q, semantic, "TEXCOORD") ||
                             scene_bitmap_xeq(q, semantic, "COLOR"))
                        target = scene_bitmap_xfind(q, scene_bitmap_xattr_get(v, 6), VX_SOURCE);
                    else
                        return false;
                    if (!scene_bitmap_xuint(q, v, 8, 0, &v->width, true) || v->width > 7 ||
                        !scene_bitmap_xuint(q, v, 12, 0, &set, false) || set > 7)
                        return false;
                }
                if (!target || q->nodes[target].parent != parent->parent)
                    return false;
                v->value = target;
            }
            break;
        case VX_TRIANGLES:
            allowed = (1U << 5);
            if (parent->kind != VX_MESH || !scene_bitmap_xuint(q, v, 5, 0, &v->value, true) || !v->value ||
                v->value > 1000000)
                return false;
            {
                unsigned inputs = 0, mask = 0, vertex = 0, indices = 0;
                while (c) {
                    if (q->nodes[c].kind == VX_INPUT) {
                        uint32_t offset;
                        if (!scene_bitmap_xuint(q, &q->nodes[c], 8, 0, &offset, true) || offset > 7 ||
                            (mask & (1U << offset)))
                            return false;
                        mask |= 1U << offset;
                        ++inputs;
                        if (scene_bitmap_xeq(q, scene_bitmap_xattr_get(&q->nodes[c], 10), "VERTEX"))
                            ++vertex;
                    } else if (q->nodes[c].kind == VX_P)
                        ++indices;
                    else
                        return false;
                    c = q->nodes[c].next;
                }
                if (!inputs || vertex != 1 || indices != 1 || mask != (1U << inputs) - 1)
                    return false;
                v->width = inputs;
            }
            break;
        case VX_P:
            if (parent->kind != VX_TRIANGLES || v->child)
                return false;
            break;
        case VX_VISUAL:
            allowed = (1U << 3) | (1U << 4);
            if (parent->kind != VX_VISLIB || !id || !c)
                return false;
            while (c) {
                if (q->nodes[c].kind != VX_NODE)
                    return false;
                c = q->nodes[c].next;
            }
            break;
        case VX_NODE:
            allowed = (1U << 3) | (1U << 4) | (1U << 9) | (1U << 14);
            if ((parent->kind != VX_VISUAL && parent->kind != VX_NODE) || !id || !c)
                return false;
            if (scene_bitmap_xattr_get(v, 9) && !scene_bitmap_xeq(q, scene_bitmap_xattr_get(v, 9), "NODE"))
                return false;
            while (c) {
                unsigned type = q->nodes[c].kind;
                if (type != VX_NODE && type != VX_GEOMINST && type != VX_TRANSLATE && type != VX_ROTATE &&
                    type != VX_SCALE && type != VX_MATRIX)
                    return false;
                c = q->nodes[c].next;
            }
            break;
        case VX_GEOMINST:
            allowed = (1U << 11);
            if (parent->kind != VX_NODE || !scene_bitmap_xfind(q, scene_bitmap_xattr_get(v, 11), VX_GEOMETRY) ||
                !scene_bitmap_xempty(q, v))
                return false;
            break;
        case VX_TRANSLATE:
        case VX_ROTATE:
        case VX_SCALE:
        case VX_MATRIX:
            allowed = (1U << 14);
            if (parent->kind != VX_NODE || !scene_bitmap_xnumbers(q, v,
                                                                  v->kind == VX_MATRIX   ? 16
                                                                  : v->kind == VX_ROTATE ? 4
                                                                                         : 3,
                                                                  false))
                return false;
            break;
        case VX_SCENE:
            if (parent->kind != VX_COLLADA || !c || q->nodes[c].kind != VX_VISINST || q->nodes[c].next)
                return false;
            break;
        case VX_VISINST:
            allowed = (1U << 11);
            if (parent->kind != VX_SCENE || !scene_bitmap_xfind(q, scene_bitmap_xattr_get(v, 11), VX_VISUAL) ||
                !scene_bitmap_xempty(q, v))
                return false;
            break;
        default:
            return false;
        }
        if (!scene_bitmap_xattrs_ok(v, allowed)) {
            return false;
        }
        if (v->child && !scene_bitmap_xcontainer(q, v))
            return false;
    }
    /* Resolve input streams only after all source accessors have been validated. */
    for (i = 1; i <= q->count; ++i) {
        scene_bitmap_xnode *v = &q->nodes[i];
        if (v->kind == VX_VERTICES) {
            scene_bitmap_xnode *input = &q->nodes[v->child];
            v->value = q->nodes[input->value].value;
            v->width = q->nodes[input->value].width;
            if (v->width < 3)
                return false;
        }
        if (v->kind == VX_P) {
            scene_bitmap_xnode *tri = &q->nodes[v->parent];
            uint32_t limits[8], k;
            component_lexer t = {q->b, v->text, v->stop, q->pd, 0, false, false, false};
            unsigned c = tri->child;
            int32_t index;
            while (c) {
                scene_bitmap_xnode *in = &q->nodes[c];
                if (in->kind == VX_INPUT)
                    limits[in->width] = q->nodes[in->value].value;
                c = in->next;
            }
            for (k = 0; k < tri->value * 3 * tri->width; ++k)
                if (!component_lexer_integer_hash_bang_cpp_comments_delimited(&t, &index) || index < 0 ||
                    (uint32_t)index >= limits[k % tri->width])
                    return false;
            if (!component_lexer_end_hash_bang_cpp_comments(&t))
                return false;
        }
    }
    return true;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    scene_bitmap_xml q = {b, n, 0, NULL, 0, pd, 0};
    unsigned root = 0, c;
    bool ok = false;
    char label[48];
    if (!component_utf8(b, n, false, pd))
        return false;
    q.nodes = (scene_bitmap_xnode *)xx_mem_alloc(sizeof(*q.nodes) * 4097);
    if (!q.nodes)
        return false;
    xx_mem_zero(q.nodes, sizeof(*q.nodes) * 4097);
    if (!scene_bitmap_xskip(&q))
        goto done;
    if (component_span(q.p, 5, n) && component_tag(b + q.p, "<?xml", 5) && !scene_bitmap_xdecl(&q))
        goto done;
    if (!scene_bitmap_xparse(&q, 0, 0, &root) || root != 1 || !scene_bitmap_xskip(&q) || q.p != n ||
        !scene_bitmap_xvalidate(&q))
        goto done;
    c = q.nodes[root].child;
    while (c) {
        scene_bitmap_xnode *v = &q.nodes[c];
        xx_rt_snprintf(label, sizeof(label), "%s.xml", scene_bitmap_xtags[v->kind]);
        if (!component_emit(f, s, label, v->start, v->end - v->start, n))
            goto done;
        c = v->next;
    }
    ok = component_cover(f, s, "xml-framing.txt", n);
done:
    xx_mem_free(q.nodes);
    return ok;
}

void xx_collada_dae_init(xx_collada_dae *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_COLLADA_DAE, "dae");
    }
}
xx_collada_dae *xx_collada_dae_create(xx_io_device *d, int64_t at) {
    xx_collada_dae *r = (xx_collada_dae *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_collada_dae_init(r, d, at);
    return r;
}
void xx_collada_dae_destroy(xx_collada_dae *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_collada_dae_free(xx_collada_dae *r) {
    if (r) {
        xx_collada_dae_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_collada_dae_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_collada_dae_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
