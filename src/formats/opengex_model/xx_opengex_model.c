/* SPDX-License-Identifier: MIT
 * Primary reference: https://opengex.org/opengex-spec.pdf
 * OpenGEX static geometry subset: typed metrics, object/material/node identifiers and resolved references, finite transforms and vertex arrays, bounded triangle indexes and matching attribute cardinalities. Original top-level scene structures exported; animation/skinning/morphing/custom extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/opengex_model/xx_opengex_model.h"
#include "../common/xx_component_lexer.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
typedef struct mesh_font_og_name {
    uint64_t at, z;
    unsigned kind;
} mesh_font_og_name;
typedef struct mesh_font_og_state {
    mesh_font_og_name *names, *refs;
    unsigned nn, nr, nodes, objects, structures;
    uint32_t comparisons;
    component_lexer q;
} mesh_font_og_state;
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t b;
    return n >= 24 && pm_read(f, 0, &b, 1) && (b == 'M' || b == 'G' || b == 'N' || b == '/' || b == ' ');
}
static bool mesh_font_og_lit(component_lexer *q, uint64_t at, uint64_t z, const char *t) {
    return z == xx_rt_strlen(t) && component_tag(q->b + at, t, (size_t)z);
}
static bool mesh_font_og_global(component_lexer *q, uint64_t *at, uint64_t *z) {
    return component_lexer_char_hash_bang_cpp_comments(q, '$') &&
           component_lexer_identifier_hash_bang_cpp_comments(q, at, z);
}
static bool mesh_font_og_same(mesh_font_og_state *st, const mesh_font_og_name *a, const mesh_font_og_name *b) {
    uint64_t i;
    if (a->z != b->z)
        return false;
    for (i = 0; i < a->z; ++i) {
        if (++st->comparisons > 16000000 || xx_component_parser_stopped(st->q.pd) ||
            st->q.b[a->at + i] != st->q.b[b->at + i])
            return false;
    }
    return true;
}
static bool mesh_font_og_add(mesh_font_og_state *st, uint64_t at, uint64_t z, unsigned kind, bool ref) {
    mesh_font_og_name name = {at, z, kind};
    unsigned i;
    if (ref) {
        if (st->nr >= 4096)
            return false;
        st->refs[st->nr++] = name;
        return true;
    }
    if (st->nn >= 4096)
        return false;
    for (i = 0; i < st->nn; ++i) {
        if (mesh_font_og_same(st, &name, &st->names[i]) || st->comparisons > 16000000)
            return false;
    }
    st->names[st->nn++] = name;
    return true;
}
static bool mesh_font_og_float(component_lexer *q, double *v) {
    uint64_t p;
    union {
        uint32_t u;
        float f;
    } u;
    unsigned i;
    if (!component_lexer_skip_hash_bang_cpp_comments(q))
        return false;
    p = q->p;
    if (component_span(p, 2, q->n) && q->b[p] == '0' && q->b[p + 1] == 'x') {
        u.u = 0;
        q->p += 2;
        for (i = 0; i < 8; ++i) {
            uint8_t c;
            unsigned d;
            if (q->p == q->n)
                return false;
            c = q->b[q->p++];
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else
                return false;
            u.u = (u.u << 4) | d;
        }
        if (!component_is_finite32(u.u))
            return false;
        *v = u.f;
        return true;
    }
    return component_lexer_number_hash_bang_cpp_comments(q, v);
}
static bool mesh_font_og_data(mesh_font_og_state *st, unsigned type, unsigned width, uint32_t limit, uint32_t *count) {
    component_lexer *q = &st->q;
    unsigned k;
    uint32_t tuples = 0;
    double v;
    int32_t index;
    uint64_t at, z;
    bool array = width > 1;
    if (type == 1 && !component_lexer_keyword_hash_bang_cpp_comments(q, "float")) {
        return false;
    }
    if (type == 2 && !component_lexer_keyword_hash_bang_cpp_comments(q, "unsigned_int32"))
        return false;
    if (type == 3 && !component_lexer_keyword_hash_bang_cpp_comments(q, "string"))
        return false;
    if (type == 4 && !component_lexer_keyword_hash_bang_cpp_comments(q, "ref"))
        return false;
    if (array) {
        if (!component_lexer_char_hash_bang_cpp_comments(q, '[') ||
            !component_lexer_integer_hash_bang_cpp_comments(q, &index) || index != (int32_t)width ||
            !component_lexer_char_hash_bang_cpp_comments(q, ']'))
            return false;
    }
    if (!component_lexer_char_hash_bang_cpp_comments(q, '{'))
        return false;
    do {
        if (array && !component_lexer_char_hash_bang_cpp_comments(q, '{'))
            return false;
        for (k = 0; k < width; ++k) {
            if (type == 1) {
                if (!mesh_font_og_float(q, &v))
                    return false;
            } else if (type == 2) {
                if (!component_lexer_integer_hash_bang_cpp_comments(q, &index) || index < 0 || (uint32_t)index >= limit)
                    return false;
            } else if (type == 3) {
                if (!component_lexer_quoted_hash_bang_cpp_comments(q, '"', &at, &z) || !z)
                    return false;
            } else if (!mesh_font_og_global(q, &at, &z) || !mesh_font_og_add(st, at, z, limit, true))
                return false;
            if (k + 1 < width && !component_lexer_char_hash_bang_cpp_comments(q, ','))
                return false;
        }
        if (array && !component_lexer_char_hash_bang_cpp_comments(q, '}'))
            return false;
        if (++tuples > 1000000 || !component_lexer_skip_hash_bang_cpp_comments(q))
            return false;
        if (q->p < q->n && q->b[q->p] == ',') {
            ++q->p;
            if (!component_lexer_skip_hash_bang_cpp_comments(q) || q->p == q->n)
                return false;
            if (q->b[q->p] == '}')
                break;
        } else
            break;
    } while (true);
    if (!component_lexer_char_hash_bang_cpp_comments(q, '}')) {
        return false;
    }
    *count = tuples;
    return true;
}
static bool mesh_font_og_structure(mesh_font_og_state *st, unsigned parent, unsigned depth, uint32_t *vertices) {
    component_lexer *q = &st->q;
    unsigned kind = 0;
    uint64_t at, z, prop = 0, psz = 0;
    uint32_t count = 0, local = 0, seen = 0;
    bool named = false;
    int32_t integer;
    double value;
    if (depth > 32 || ++st->structures > 4096 || !component_lexer_skip_hash_bang_cpp_comments(q))
        return false;
    if (component_lexer_keyword_hash_bang_cpp_comments(q, "Metric"))
        kind = 1;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "GeometryNode"))
        kind = 2;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "GeometryObject"))
        kind = 3;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Material"))
        kind = 4;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Name"))
        kind = 5;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "ObjectRef"))
        kind = 6;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "MaterialRef"))
        kind = 7;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Transform"))
        kind = 8;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Mesh"))
        kind = 9;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "VertexArray"))
        kind = 10;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "IndexArray"))
        kind = 11;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Color"))
        kind = 12;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Texture"))
        kind = 13;
    else
        return false;
    if (parent == 0) {
        if (kind > 4)
            return false;
    } else if (parent == 2) {
        if (kind != 5 && kind != 6 && kind != 7 && kind != 8)
            return false;
    } else if (parent == 3) {
        if (kind != 9)
            return false;
    } else if (parent == 4) {
        if (kind != 5 && kind != 12 && kind != 13)
            return false;
    } else if (parent == 9) {
        if (kind != 10 && kind != 11)
            return false;
    } else
        return false;
    if (!component_lexer_skip_hash_bang_cpp_comments(q)) {
        return false;
    }
    if (q->p < q->n && q->b[q->p] == '$') {
        if (kind < 2 || kind > 4 || !mesh_font_og_global(q, &at, &z) || !mesh_font_og_add(st, at, z, kind, false))
            return false;
        named = true;
    }
    if (kind >= 2 && kind <= 4 && !named)
        return false;
    if (kind == 1 || kind == 9 || kind == 10 || kind == 12 || kind == 13) {
        const char *key = kind == 1 ? "key" : kind == 9 ? "primitive" : "attrib";
        if (!component_lexer_char_hash_bang_cpp_comments(q, '(') ||
            !component_lexer_keyword_hash_bang_cpp_comments(q, key) ||
            !component_lexer_char_hash_bang_cpp_comments(q, '=') ||
            !component_lexer_quoted_hash_bang_cpp_comments(q, '"', &prop, &psz) ||
            !component_lexer_char_hash_bang_cpp_comments(q, ')'))
            return false;
    } else if (kind == 7) {
        component_lexer save = *q;
        if (component_lexer_char_hash_bang_cpp_comments(q, '(')) {
            if (!component_lexer_keyword_hash_bang_cpp_comments(q, "index") ||
                !component_lexer_char_hash_bang_cpp_comments(q, '=') ||
                !component_lexer_integer_hash_bang_cpp_comments(q, &integer) || integer != 0 ||
                !component_lexer_char_hash_bang_cpp_comments(q, ')'))
                return false;
        } else
            *q = save;
    }
    if (!component_lexer_char_hash_bang_cpp_comments(q, '{'))
        return false;
    if (kind == 1) {
        component_lexer save = *q;
        if (mesh_font_og_lit(q, prop, psz, "up")) {
            if (!component_lexer_keyword_hash_bang_cpp_comments(q, "string") ||
                !component_lexer_char_hash_bang_cpp_comments(q, '{') ||
                !component_lexer_quoted_hash_bang_cpp_comments(q, '"', &at, &z) ||
                (!mesh_font_og_lit(q, at, z, "y") && !mesh_font_og_lit(q, at, z, "z")) ||
                !component_lexer_char_hash_bang_cpp_comments(q, '}'))
                return false;
        } else {
            if (!mesh_font_og_lit(q, prop, psz, "distance") && !mesh_font_og_lit(q, prop, psz, "angle") &&
                !mesh_font_og_lit(q, prop, psz, "time"))
                return false;
            *q = save;
            if (!component_lexer_keyword_hash_bang_cpp_comments(q, "float") ||
                !component_lexer_char_hash_bang_cpp_comments(q, '{') || !mesh_font_og_float(q, &value) || value <= 0 ||
                !component_lexer_char_hash_bang_cpp_comments(q, '}'))
                return false;
        }
    } else if (kind == 5 || kind == 13) {
        if (kind == 13 && !mesh_font_og_lit(q, prop, psz, "diffuse"))
            return false;
        if (!mesh_font_og_data(st, 3, 1, 0, &count) || count != 1)
            return false;
    } else if (kind == 6 || kind == 7) {
        if (!mesh_font_og_data(st, 4, 1, kind == 6 ? 3 : 4, &count) || count != 1)
            return false;
    } else if (kind == 8) {
        if (!mesh_font_og_data(st, 1, 16, 0, &count) || count != 1)
            return false;
    } else if (kind == 10) {
        unsigned width;
        if (mesh_font_og_lit(q, prop, psz, "position"))
            width = 3;
        else if (mesh_font_og_lit(q, prop, psz, "normal"))
            width = 3;
        else if (mesh_font_og_lit(q, prop, psz, "texcoord"))
            width = 2;
        else
            return false;
        if (!mesh_font_og_data(st, 1, width, 0, &count))
            return false;
        if (mesh_font_og_lit(q, prop, psz, "position")) {
            if (*vertices)
                return false;
            *vertices = count;
        } else if (!*vertices || *vertices != count)
            return false;
    } else if (kind == 11) {
        if (!*vertices || !mesh_font_og_data(st, 2, 3, *vertices, &count))
            return false;
    } else if (kind == 12) {
        if (!mesh_font_og_lit(q, prop, psz, "diffuse") && !mesh_font_og_lit(q, prop, psz, "specular") &&
            !mesh_font_og_lit(q, prop, psz, "emission"))
            return false;
        if (!mesh_font_og_data(st, 1, 3, 0, &count) || count != 1)
            return false;
    } else {
        if (kind == 9 && !mesh_font_og_lit(q, prop, psz, "triangles"))
            return false;
        while (component_lexer_skip_hash_bang_cpp_comments(q) && q->p < q->n && q->b[q->p] != '}') {
            unsigned child;
            component_lexer save = *q;
            if (component_lexer_keyword_hash_bang_cpp_comments(q, "ObjectRef"))
                child = 1;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Mesh"))
                child = 2;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "IndexArray"))
                child = 4;
            else
                child = 0;
            *q = save;
            if (child && (seen & child))
                return false;
            seen |= child;
            if (!mesh_font_og_structure(st, kind, depth + 1, &local))
                return false;
        }
        if (kind == 2) {
            if (!(seen & 1))
                return false;
            ++st->nodes;
        }
        if (kind == 3) {
            if (!(seen & 2))
                return false;
            ++st->objects;
        }
        if (kind == 9 && (!(seen & 4) || !local))
            return false;
    }
    return component_lexer_char_hash_bang_cpp_comments(q, '}');
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    mesh_font_og_state st;
    unsigned i, j;
    bool ok = false;
    xx_mem_zero(&st, sizeof(st));
    st.q.b = b;
    st.q.n = n;
    st.q.pd = pd;
    st.q.comments = true;
    if (!component_utf8(b, n, true, pd)) {
        return false;
    }
    st.names = (mesh_font_og_name *)xx_mem_alloc(sizeof(mesh_font_og_name) * 4096);
    st.refs = (mesh_font_og_name *)xx_mem_alloc(sizeof(mesh_font_og_name) * 4096);
    if (!st.names || !st.refs)
        goto done;
    while (component_lexer_skip_hash_bang_cpp_comments(&st.q) && st.q.p < n) {
        uint64_t at = st.q.p;
        uint32_t v = 0;
        char label[48];
        if (!mesh_font_og_structure(&st, 0, 0, &v))
            goto done;
        xx_rt_snprintf(label, sizeof(label), "structure-%u.ogex", (unsigned)s->count);
        if (!component_emit(f, s, label, at, st.q.p - at, n))
            goto done;
    }
    if (!st.nodes || !st.objects || !component_lexer_end_hash_bang_cpp_comments(&st.q))
        goto done;
    for (i = 0; i < st.nr; ++i) {
        bool found = false;
        for (j = 0; j < st.nn; ++j)
            if (mesh_font_og_same(&st, &st.refs[i], &st.names[j])) {
                if (st.refs[i].kind != st.names[j].kind)
                    goto done;
                found = true;
                break;
            }
        if (!found || st.comparisons > 16000000)
            goto done;
    }
    ok = component_cover(f, s, "framing.ogex", n);
done:
    if (st.names)
        xx_mem_free(st.names);
    if (st.refs)
        xx_mem_free(st.refs);
    return ok;
}

void xx_opengex_model_init(xx_opengex_model *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_OPENGEX_MODEL, "ogex");
    }
}
xx_opengex_model *xx_opengex_model_create(xx_io_device *d, int64_t at) {
    xx_opengex_model *r = (xx_opengex_model *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_opengex_model_init(r, d, at);
    return r;
}
void xx_opengex_model_destroy(xx_opengex_model *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_opengex_model_free(xx_opengex_model *r) {
    if (r) {
        xx_opengex_model_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_opengex_model_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_opengex_model_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
