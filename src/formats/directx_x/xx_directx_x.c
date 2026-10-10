/* SPDX-License-Identifier: MIT
 * Primary reference: https://learn.microsoft.com/en-us/windows/win32/direct3d9/dx9-graphics-reference-x-file-format
 * Microsoft X text0303/32-bit meshes: complete standard typed frames/materials/mesh data with finite values and bounded resolved indexes, including bounded
 * XSkinMeshHeader and SkinWeights records. Recognized standard templates are validated; original top-level templates and objects exported without skinning evaluation.
 * Compressed/binary/animation/custom templates declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/directx_x/xx_directx_x.h"
#include "../common/xx_component_lexer.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
typedef struct mesh_font_x_name {
    uint64_t at, z;
    unsigned kind;
} mesh_font_x_name;
typedef struct mesh_font_x_state {
    component_lexer q;
    mesh_font_x_name *names, *refs;
    unsigned nn, nr, meshes, objects;
    uint32_t work;
} mesh_font_x_state;
static bool mesh_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[16];
    return n >= 32 && pm_read(f, 0, b, 16) && component_tag(b, "xof 0303txt 0032", 16);
}
static bool mesh_font_x_equal(mesh_font_x_state *st, mesh_font_x_name a, mesh_font_x_name b)
{
    uint64_t i;
    if (a.z != b.z) return false;
    for (i = 0; i < a.z; ++i) {
        if (++st->work > 16000000 || xx_component_parser_stopped(st->q.pd) || st->q.b[a.at + i] != st->q.b[b.at + i]) return false;
    }
    return true;
}
static bool mesh_font_x_add(mesh_font_x_state *st, uint64_t at, uint64_t z, unsigned kind, bool ref)
{
    mesh_font_x_name v = {at, z, kind};
    unsigned i;
    if (ref) {
        if (st->nr >= 4096) return false;
        st->refs[st->nr++] = v;
        return true;
    }
    if (st->nn >= 4096) return false;
    for (i = 0; i < st->nn; ++i)
        if (mesh_font_x_equal(st, v, st->names[i]) || st->work > 16000000) return false;
    st->names[st->nn++] = v;
    return true;
}
static bool mesh_font_x_int(component_lexer *q, int32_t *v)
{
    return component_lexer_integer_hash_bang_cpp_comments(q, v) && *v >= 0 && component_lexer_char_hash_bang_cpp_comments(q, ';');
}
static bool mesh_font_x_float(component_lexer *q, bool color)
{
    double v;
    return component_lexer_number_hash_bang_cpp_comments(q, &v) && (!color || (v >= 0 && v <= 1)) && component_lexer_char_hash_bang_cpp_comments(q, ';');
}
static bool mesh_font_x_vectors(component_lexer *q, uint32_t count, unsigned width, bool color)
{
    uint32_t i;
    unsigned j;
    for (i = 0; i < count; ++i) {
        for (j = 0; j < width; ++j)
            if (!mesh_font_x_float(q, color)) return false;
        if (!component_lexer_char_hash_bang_cpp_comments(q, i + 1 < count ? ',' : ';')) return false;
    }
    return true;
}
static bool mesh_font_x_indexes(component_lexer *q, uint32_t count, uint32_t vertices)
{
    uint32_t i, j;
    int32_t width, index;
    for (i = 0; i < count; ++i) {
        if (!mesh_font_x_int(q, &width) || width < 3 || width > 64) return false;
        for (j = 0; j < (uint32_t)width; ++j) {
            if (!component_lexer_integer_hash_bang_cpp_comments(q, &index) || index < 0 || (uint32_t)index >= vertices ||
                !component_lexer_char_hash_bang_cpp_comments(q, j + 1 < (uint32_t)width ? ',' : ';'))
                return false;
        }
        if (!component_lexer_char_hash_bang_cpp_comments(q, i + 1 < count ? ',' : ';')) return false;
    }
    return true;
}
static bool mesh_font_x_template(mesh_font_x_state *st)
{
    static const char *const names[] = {"XSkinMeshHeader", "VertexDuplicationIndices", "SkinWeights", "AnimTicksPerSecond"};
    static const char *const body[] = {"<3cf169ce-ff7c-44ab-93c0-f78f62d172e2>WORDnMaxSkinWeightsPerVertex;WORDnMaxSkinWeightsPerFace;WORDnBones;",
                                       "<b8d65549-d7c9-4995-89cf-53a9a8b031e3>DWORDnIndices;DWORDnOriginalVertices;arrayDWORDindices[nIndices];",
                                       "<6f0d123b-bad2-4167-a0d0-80224f25fabb>STRINGtransformNodeName;DWORDnWeights;arrayDWORDvertexIndices[nWeights];arrayFLOATweights["
                                       "nWeights];Matrix4x4matrixOffset;",
                                       "<9e415a43-7ba6-4a73-8743-b73d47e88476>DWORDAnimTicksPerSecond;"};
    component_lexer *q = &st->q;
    uint64_t at, z;
    unsigned kind = 4;
    size_t i = 0;
    if (!component_lexer_keyword_hash_bang_cpp_comments(q, "template") || !component_lexer_identifier_hash_bang_cpp_comments(q, &at, &z)) return false;
    for (kind = 0; kind < 4; ++kind)
        if (z == xx_rt_strlen(names[kind]) && component_tag(q->b + at, names[kind], (size_t)z)) break;
    if (kind == 4 || !component_lexer_char_hash_bang_cpp_comments(q, '{')) return false;
    while (body[kind][i]) {
        uint8_t c;
        if (!component_lexer_skip_hash_bang_cpp_comments(q) || q->p == q->n) return false;
        c = q->b[q->p++];
        if (c != (uint8_t)body[kind][i++]) return false;
    }
    return component_lexer_char_hash_bang_cpp_comments(q, '}');
}
static bool mesh_font_x_object(mesh_font_x_state *st, unsigned parent, unsigned depth, uint32_t vertices, uint32_t faces)
{
    component_lexer *q = &st->q;
    unsigned kind = 0, seen = 0;
    uint64_t at, z;
    int32_t count, value;
    uint32_t nv = 0, nf = 0;
    unsigned i;
    bool named = false;
    if (depth > 32 || ++st->objects > 4096) return false;
    if (component_lexer_keyword_hash_bang_cpp_comments(q, "Frame")) kind = 1;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Mesh")) kind = 2;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Material")) kind = 3;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "FrameTransformMatrix")) kind = 4;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "MeshNormals")) kind = 5;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "MeshTextureCoords")) kind = 6;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "MeshMaterialList")) kind = 7;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "VertexDuplicationIndices")) kind = 8;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "XSkinMeshHeader")) kind = 9;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "SkinWeights")) kind = 10;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "AnimTicksPerSecond")) kind = 11;
    else if (component_lexer_keyword_hash_bang_cpp_comments(q, "TextureFilename")) kind = 12;
    else return false;
    if (parent == 0) {
        if (kind != 1 && kind != 2 && kind != 3 && kind != 11) return false;
    } else if (parent == 1) {
        if (kind != 1 && kind != 2 && kind != 4) return false;
    } else if (parent == 2) {
        if (kind < 5 || kind > 10) return false;
    } else if (parent == 3) {
        if (kind != 12) return false;
    } else if (parent == 7) {
        if (kind != 3) return false;
    } else return false;
    if (!component_lexer_skip_hash_bang_cpp_comments(q)) {
        return false;
    }
    if (q->p < q->n && q->b[q->p] != '{') {
        if (!component_lexer_identifier_hash_bang_cpp_comments(q, &at, &z)) return false;
        named = true;
        if ((kind == 1 || kind == 3) && !mesh_font_x_add(st, at, z, kind, false)) return false;
    }
    if (!component_lexer_char_hash_bang_cpp_comments(q, '{')) return false;
    if (kind == 4) {
        for (i = 0; i < 16; ++i) {
            double v;
            if (!component_lexer_number_hash_bang_cpp_comments(q, &v) || !component_lexer_char_hash_bang_cpp_comments(q, i < 15 ? ',' : ';')) return false;
        }
        if (!component_lexer_char_hash_bang_cpp_comments(q, ';')) return false;
    } else if (kind == 3) {
        if (!mesh_font_x_vectors(q, 1, 4, true) || !mesh_font_x_float(q, false) || !mesh_font_x_vectors(q, 1, 3, true) || !mesh_font_x_vectors(q, 1, 3, true))
            return false;
        while (component_lexer_skip_hash_bang_cpp_comments(q) && q->p < q->n && q->b[q->p] != '}')
            if (!mesh_font_x_object(st, kind, depth + 1, 0, 0)) return false;
    } else if (kind == 12) {
        if (!component_lexer_quoted_hash_bang_cpp_comments(q, '"', &at, &z) || !z || !component_lexer_char_hash_bang_cpp_comments(q, ';')) return false;
    } else if (kind == 11) {
        if (!mesh_font_x_int(q, &count) || count < 1 || count > 1000000) return false;
    } else if (kind == 9) {
        if (!mesh_font_x_int(q, &count) || count < 1 || count > 1024 || !mesh_font_x_int(q, &count) || count < 1 || count > 65536 || !mesh_font_x_int(q, &count) ||
            count < 1 || count > 4096)
            return false;
    } else if (kind == 8) {
        if (!mesh_font_x_int(q, &count) || count != (int32_t)vertices || !mesh_font_x_int(q, &value) || value < 1 || (uint32_t)value > vertices) return false;
        for (i = 0; i < (unsigned)count; ++i) {
            int32_t index;
            if (!component_lexer_integer_hash_bang_cpp_comments(q, &index) || index < 0 || index >= value ||
                !component_lexer_char_hash_bang_cpp_comments(q, i + 1 < (unsigned)count ? ',' : ';'))
                return false;
        }
    } else if (kind == 10) {
        double weight;
        if (!component_lexer_quoted_hash_bang_cpp_comments(q, '"', &at, &z) || !z || !mesh_font_x_add(st, at, z, 1, true) ||
            !component_lexer_char_hash_bang_cpp_comments(q, ';') || !mesh_font_x_int(q, &count) || count < 1 || (uint32_t)count > vertices)
            return false;
        for (i = 0; i < (unsigned)count; ++i) {
            if (!component_lexer_integer_hash_bang_cpp_comments(q, &value) || value < 0 || (uint32_t)value >= vertices ||
                !component_lexer_char_hash_bang_cpp_comments(q, i + 1 < (unsigned)count ? ',' : ';'))
                return false;
        }
        for (i = 0; i < (unsigned)count; ++i)
            if (!component_lexer_number_hash_bang_cpp_comments(q, &weight) || weight <= 0 || weight > 1 ||
                !component_lexer_char_hash_bang_cpp_comments(q, i + 1 < (unsigned)count ? ',' : ';'))
                return false;
        for (i = 0; i < 16; ++i)
            if (!component_lexer_number_hash_bang_cpp_comments(q, &weight) || !component_lexer_char_hash_bang_cpp_comments(q, i < 15 ? ',' : ';')) return false;
        if (!component_lexer_char_hash_bang_cpp_comments(q, ';')) return false;
    } else if (kind == 6) {
        if (!mesh_font_x_int(q, &count) || (uint32_t)count != vertices || !mesh_font_x_vectors(q, (uint32_t)count, 2, false)) return false;
    } else if (kind == 5) {
        if (!mesh_font_x_int(q, &count) || count < 1 || count > 1000000 || !mesh_font_x_vectors(q, (uint32_t)count, 3, false)) return false;
        nv = (uint32_t)count;
        if (!mesh_font_x_int(q, &count) || (uint32_t)count != faces || !mesh_font_x_indexes(q, faces, nv)) return false;
    } else if (kind == 7) {
        int32_t materials;
        if (!mesh_font_x_int(q, &materials) || materials < 1 || materials > 4096 || !mesh_font_x_int(q, &count) || (uint32_t)count != faces) return false;
        for (i = 0; i < (unsigned)count; ++i) {
            if (!component_lexer_integer_hash_bang_cpp_comments(q, &value) || value < 0 || value >= materials ||
                !component_lexer_char_hash_bang_cpp_comments(q, i + 1 < (unsigned)count ? ',' : ';'))
                return false;
        }
        for (i = 0; i < (unsigned)materials; ++i) {
            component_lexer save = *q;
            if (component_lexer_char_hash_bang_cpp_comments(q, '{')) {
                if (!component_lexer_identifier_hash_bang_cpp_comments(q, &at, &z) || !mesh_font_x_add(st, at, z, 3, true) ||
                    !component_lexer_char_hash_bang_cpp_comments(q, '}'))
                    return false;
            } else {
                *q = save;
                if (!mesh_font_x_object(st, 7, depth + 1, 0, 0)) return false;
            }
        }
    } else {
        if (kind == 2) {
            if (!mesh_font_x_int(q, &count) || count < 3 || count > 1000000 || !mesh_font_x_vectors(q, (uint32_t)count, 3, false)) return false;
            nv = (uint32_t)count;
            if (!mesh_font_x_int(q, &count) || count < 1 || count > 1000000 || !mesh_font_x_indexes(q, (uint32_t)count, nv)) return false;
            nf = (uint32_t)count;
            ++st->meshes;
        }
        while (component_lexer_skip_hash_bang_cpp_comments(q) && q->p < q->n && q->b[q->p] != '}') {
            unsigned bit = 0;
            component_lexer save = *q;
            if (component_lexer_keyword_hash_bang_cpp_comments(q, "FrameTransformMatrix")) bit = 1;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "MeshNormals")) bit = 2;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "MeshTextureCoords")) bit = 4;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "MeshMaterialList")) bit = 8;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "VertexDuplicationIndices")) bit = 16;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "XSkinMeshHeader")) bit = 32;
            *q = save;
            if (bit && (seen & bit)) return false;
            seen |= bit;
            if (!mesh_font_x_object(st, kind, depth + 1, nv, nf)) return false;
        }
    }
    (void)named;
    return component_lexer_char_hash_bang_cpp_comments(q, '}');
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    mesh_font_x_state st;
    unsigned i, j;
    bool ok = false;
    xx_mem_zero(&st, sizeof(st));
    st.q.b = b;
    st.q.p = 16;
    st.q.n = n;
    st.q.pd = pd;
    st.q.comments = true;
    if (n < 16 || !component_tag(b, "xof 0303txt 0032", 16) || !component_utf8(b, n, true, pd) || !component_emit(f, s, "descriptor.x", 0, 16, n)) {
        return false;
    }
    st.names = (mesh_font_x_name *)xx_mem_alloc(sizeof(mesh_font_x_name) * 4096);
    st.refs = (mesh_font_x_name *)xx_mem_alloc(sizeof(mesh_font_x_name) * 4096);
    if (!st.names || !st.refs) goto done;
    while (component_lexer_skip_hash_bang_cpp_comments(&st.q) && st.q.p < n) {
        uint64_t p = st.q.p;
        component_lexer save = st.q;
        bool temp = component_lexer_keyword_hash_bang_cpp_comments(&st.q, "template");
        char label[48];
        st.q = save;
        if (!(temp ? mesh_font_x_template(&st) : mesh_font_x_object(&st, 0, 0, 0, 0))) goto done;
        xx_rt_snprintf(label, sizeof(label), "object-%u.x", (unsigned)s->count);
        if (!component_emit(f, s, label, p, st.q.p - p, n)) goto done;
    }
    if (!st.meshes || !component_lexer_end_hash_bang_cpp_comments(&st.q)) {
        goto done;
    }
    for (i = 0; i < st.nr; ++i) {
        bool found = false;
        for (j = 0; j < st.nn; ++j)
            if (mesh_font_x_equal(&st, st.refs[i], st.names[j])) {
                if (st.refs[i].kind != st.names[j].kind) goto done;
                found = true;
                break;
            }
        if (!found || st.work > 16000000) goto done;
    }
    ok = component_cover(f, s, "framing.x", n);
done:
    if (st.names) xx_mem_free(st.names);
    if (st.refs) xx_mem_free(st.refs);
    return ok;
}

void xx_directx_x_init(xx_directx_x *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_DIRECTX_X, "x");
    }
}
xx_directx_x *xx_directx_x_create(xx_io_device *d, int64_t at)
{
    xx_directx_x *r = (xx_directx_x *)xx_mem_alloc(sizeof(*r));
    if (r) xx_directx_x_init(r, d, at);
    return r;
}
void xx_directx_x_destroy(xx_directx_x *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_directx_x_free(xx_directx_x *r)
{
    if (r) {
        xx_directx_x_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_directx_x_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_directx_x_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
