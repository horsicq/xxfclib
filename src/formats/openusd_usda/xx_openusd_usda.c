/* SPDX-License-Identifier: MIT
 * Primary reference: https://openusd.org/release/spec_usda.html
 * OpenUSD USDA1.0 static Mesh/Xform subset: complete bounded typed layer/prim/property grammar, unique prim paths/settings, finite point/transform arrays and counted resolved face topology/local references; original metadata/prim/property records exported; variants/composition/payloads/animation/unknown schemas declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/openusd_usda/xx_openusd_usda.h"
#include "../common/xx_component_lexer.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[9];
    return n >= 16 && pm_read(f, 0, b, 9) && component_tag(b, "#usda 1.0", 9);
}
typedef struct image_document_usdprim {
    uint32_t parent, kind, name, size, seen, points, sum, indices, maxindex, ops, order;
    double lo[3], hi[3];
} image_document_usdprim;
typedef struct image_document_usd {
    component_lexer q;
    Abstractformat *f;
    pm_stream *s;
    image_document_usdprim *prims;
    unsigned count, parts;
    uint32_t work;
} image_document_usd;
static bool image_document_usd_name(component_lexer *q, uint64_t *at, uint64_t *z) {
    uint64_t i;
    if (!component_lexer_quoted_hash_cpp_comments(q, '"', at, z) || !*z || *z > 255)
        return false;
    for (i = 0; i < *z; ++i) {
        uint8_t c = q->b[*at + i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (i && c >= '0' && c <= '9')))
            return false;
    }
    return true;
}
static bool image_document_usd_type(component_lexer *q, char *out, size_t cap) {
    uint64_t at, z;
    if (!component_lexer_identifier_hash_cpp_comments(q, &at, &z) || z + 1 > cap)
        return false;
    xx_rt_memcpy(out, q->b + at, (size_t)z);
    out[z] = 0;
    return true;
}
static bool image_document_usd_tuple(component_lexer *q, unsigned size, double *v) {
    unsigned j;
    if (!component_lexer_char_hash_cpp_comments(q, '('))
        return false;
    for (j = 0; j < size; ++j) {
        if (!component_lexer_number_hash_cpp_comments(q, &v[j]))
            return false;
        if (j + 1 < size && !component_lexer_char_hash_cpp_comments(q, ','))
            return false;
    }
    return component_lexer_char_hash_cpp_comments(q, ')');
}
static bool image_document_usd_property(image_document_usd *u, unsigned id) {
    component_lexer *q = &u->q;
    image_document_usdprim *p = &u->prims[id];
    uint64_t start, at, z;
    char type[32], name[64], label[48];
    uint32_t bit = 0;
    bool array = false, uniform = false;
    double values[4];
    if (!component_lexer_skip_hash_cpp_comments(q))
        return false;
    start = q->p;
    {
        component_lexer copy = *q;
        if (component_lexer_keyword_hash_cpp_comments(q, "uniform"))
            uniform = true;
        else
            *q = copy;
    }
    if (!image_document_usd_type(q, type, sizeof(type)))
        return false;
    if (!component_lexer_skip_hash_cpp_comments(q))
        return false;
    if (q->p < q->n && q->b[q->p] == '[') {
        if (!component_lexer_char_hash_cpp_comments(q, '[') || !component_lexer_char_hash_cpp_comments(q, ']'))
            return false;
        array = true;
    }
    if (!component_lexer_identifier_hash_cpp_comments(q, &at, &z) || z >= sizeof(name))
        return false;
    xx_rt_memcpy(name, q->b + at, (size_t)z);
    name[z] = 0;
    if (!component_lexer_skip_hash_cpp_comments(q))
        return false;
    if (q->p < q->n && q->b[q->p] == ':') {
        uint64_t a, l;
        if (!component_lexer_char_hash_cpp_comments(q, ':') ||
            !component_lexer_identifier_hash_cpp_comments(q, &a, &l) || z + 1 + l >= sizeof(name))
            return false;
        name[z++] = ':';
        xx_rt_memcpy(name + z, q->b + a, (size_t)l);
        name[z + l] = 0;
    }
    if (!component_lexer_char_hash_cpp_comments(q, '='))
        return false;
    if (!xx_rt_strcmp(name, "faceVertexCounts") || !xx_rt_strcmp(name, "faceVertexIndices")) {
        bool counts = !xx_rt_strcmp(name, "faceVertexCounts");
        uint32_t num = 0, total = 0, max = 0;
        int32_t value;
        bit = counts ? 1 : 2;
        if (p->kind != 1 || xx_rt_strcmp(type, "int") || !array || uniform ||
            !component_lexer_char_hash_cpp_comments(q, '['))
            return false;
        while (true) {
            if (!component_lexer_skip_hash_cpp_comments(q) || q->p == q->n)
                return false;
            if (q->b[q->p] == ']') {
                ++q->p;
                break;
            }
            if (!component_lexer_integer_hash_cpp_comments_delimited(q, &value) || value < (counts ? 3 : 0) ||
                ++num > 1000000)
                return false;
            if (counts) {
                if ((uint32_t)value > 1000000 - total)
                    return false;
                total += (uint32_t)value;
            } else if ((uint32_t)value > max)
                max = (uint32_t)value;
            if (!component_lexer_skip_hash_cpp_comments(q))
                return false;
            if (q->p < q->n && q->b[q->p] == ',') {
                ++q->p;
                continue;
            }
            if (!component_lexer_char_hash_cpp_comments(q, ']'))
                return false;
            break;
        }
        if (!num)
            return false;
        if (counts)
            p->sum = total;
        else {
            p->indices = num;
            p->maxindex = max;
        }
    } else if (!xx_rt_strcmp(name, "points")) {
        uint32_t count = 0;
        unsigned j;
        bit = 4;
        if (p->kind != 1 || xx_rt_strcmp(type, "point3f") || !array || uniform ||
            !component_lexer_char_hash_cpp_comments(q, '['))
            return false;
        while (true) {
            if (!component_lexer_skip_hash_cpp_comments(q) || q->p == q->n)
                return false;
            if (q->b[q->p] == ']') {
                ++q->p;
                break;
            }
            if (!image_document_usd_tuple(q, 3, values) || ++count > 1000000)
                return false;
            for (j = 0; j < 3; ++j) {
                if (count == 1)
                    p->lo[j] = p->hi[j] = values[j];
                else {
                    if (values[j] < p->lo[j])
                        p->lo[j] = values[j];
                    if (values[j] > p->hi[j])
                        p->hi[j] = values[j];
                }
            }
            if (!component_lexer_skip_hash_cpp_comments(q))
                return false;
            if (q->p < q->n && q->b[q->p] == ',') {
                ++q->p;
                continue;
            }
            if (!component_lexer_char_hash_cpp_comments(q, ']'))
                return false;
            break;
        }
        if (!count)
            return false;
        p->points = count;
    } else if (!xx_rt_strcmp(name, "subdivisionScheme") || !xx_rt_strcmp(name, "orientation")) {
        bit = !xx_rt_strcmp(name, "subdivisionScheme") ? 8 : 16;
        if (p->kind != 1 || xx_rt_strcmp(type, "token") || array || !uniform ||
            !component_lexer_quoted_hash_cpp_comments(q, '"', &at, &z))
            return false;
        if (bit == 8) {
            if (!((z == 4 && component_tag(q->b + at, "none", 4)) ||
                  (z == 7 && component_tag(q->b + at, "bilinear", 7)) ||
                  (z == 11 && component_tag(q->b + at, "catmullClark", 11)) ||
                  (z == 4 && component_tag(q->b + at, "loop", 4))))
                return false;
        } else if (!((z == 11 && component_tag(q->b + at, "rightHanded", 11)) ||
                     (z == 10 && component_tag(q->b + at, "leftHanded", 10))))
            return false;
    } else if (!xx_rt_strcmp(name, "doubleSided")) {
        component_lexer copy;
        bit = 32;
        if (p->kind != 1 || xx_rt_strcmp(type, "bool") || array || !uniform)
            return false;
        copy = *q;
        if (!component_lexer_keyword_hash_cpp_comments(q, "true")) {
            *q = copy;
            if (!component_lexer_keyword_hash_cpp_comments(q, "false"))
                return false;
        }
    } else if (!xx_rt_strcmp(name, "xformOp:translate") || !xx_rt_strcmp(name, "xformOp:rotateXYZ") ||
               !xx_rt_strcmp(name, "xformOp:scale") || !xx_rt_strcmp(name, "xformOp:transform")) {
        unsigned op = !xx_rt_strcmp(name, "xformOp:translate")   ? 1
                      : !xx_rt_strcmp(name, "xformOp:rotateXYZ") ? 2
                      : !xx_rt_strcmp(name, "xformOp:scale")     ? 4
                                                                 : 8;
        bit = op << 8;
        if (p->kind == 3 || array || uniform || (p->ops & op))
            return false;
        if (op == 8) {
            unsigned j;
            if (xx_rt_strcmp(type, "matrix4d") || !component_lexer_char_hash_cpp_comments(q, '('))
                return false;
            for (j = 0; j < 4; ++j) {
                if (!image_document_usd_tuple(q, 4, values) ||
                    (j < 3 && !component_lexer_char_hash_cpp_comments(q, ',')))
                    return false;
            }
            if (!component_lexer_char_hash_cpp_comments(q, ')'))
                return false;
        } else if ((xx_rt_strcmp(type, "double3") && xx_rt_strcmp(type, "float3")) ||
                   !image_document_usd_tuple(q, 3, values))
            return false;
        p->ops |= op;
    } else if (!xx_rt_strcmp(name, "xformOpOrder")) {
        uint32_t mask = 0;
        bit = 64;
        if (p->kind == 3 || xx_rt_strcmp(type, "token") || !array || !uniform ||
            !component_lexer_char_hash_cpp_comments(q, '['))
            return false;
        while (true) {
            unsigned op;
            if (!component_lexer_skip_hash_cpp_comments(q) || q->p == q->n)
                return false;
            if (q->b[q->p] == ']') {
                ++q->p;
                break;
            }
            if (!component_lexer_quoted_hash_cpp_comments(q, '"', &at, &z))
                return false;
            op = z == 17 && component_tag(q->b + at, "xformOp:translate", 17)   ? 1
                 : z == 17 && component_tag(q->b + at, "xformOp:rotateXYZ", 17) ? 2
                 : z == 13 && component_tag(q->b + at, "xformOp:scale", 13)     ? 4
                 : z == 17 && component_tag(q->b + at, "xformOp:transform", 17) ? 8
                                                                                : 0;
            if (!op || (mask & op))
                return false;
            mask |= op;
            if (!component_lexer_skip_hash_cpp_comments(q))
                return false;
            if (q->p < q->n && q->b[q->p] == ',') {
                ++q->p;
                continue;
            }
            if (!component_lexer_char_hash_cpp_comments(q, ']'))
                return false;
            break;
        }
        p->order = mask;
    } else {
        return false;
    }
    if (p->seen & bit)
        return false;
    p->seen |= bit;
    if (++u->parts > 3000)
        return false;
    xx_rt_snprintf(label, sizeof(label), "prim-%u-property-%u.usda", id, u->parts - 1);
    return component_emit(u->f, u->s, label, start, q->p - start, q->n);
}
static bool image_document_usd_prim(image_document_usd *u, unsigned parent, unsigned depth) {
    component_lexer *q = &u->q;
    uint64_t at, z;
    unsigned id, i;
    uint32_t kind;
    if (depth > 64 || u->count >= 1024 || !component_lexer_keyword_hash_cpp_comments(q, "def"))
        return false;
    {
        component_lexer copy = *q;
        if (component_lexer_keyword_hash_cpp_comments(q, "Mesh"))
            kind = 1;
        else {
            *q = copy;
            if (component_lexer_keyword_hash_cpp_comments(q, "Xform"))
                kind = 2;
            else {
                *q = copy;
                if (!component_lexer_keyword_hash_cpp_comments(q, "Scope"))
                    return false;
                kind = 3;
            }
        }
    }
    if (!image_document_usd_name(q, &at, &z))
        return false;
    for (i = 1; i <= u->count; ++i) {
        image_document_usdprim *p = &u->prims[i];
        if (++u->work > 16000000 || xx_component_parser_stopped(q->pd))
            return false;
        if (p->parent == parent && p->size == z && component_tag(q->b + p->name, (const char *)q->b + at, (size_t)z))
            return false;
    }
    id = ++u->count;
    u->prims[id].parent = parent;
    u->prims[id].kind = kind;
    u->prims[id].name = (uint32_t)at;
    u->prims[id].size = (uint32_t)z;
    if (!component_lexer_char_hash_cpp_comments(q, '{'))
        return false;
    while (true) {
        component_lexer copy;
        if (!component_lexer_skip_hash_cpp_comments(q) || q->p == q->n)
            return false;
        if (q->b[q->p] == '}') {
            ++q->p;
            break;
        }
        copy = *q;
        if (component_lexer_keyword_hash_cpp_comments(q, "def")) {
            *q = copy;
            if (!image_document_usd_prim(u, id, depth + 1))
                return false;
        } else {
            *q = copy;
            if (!image_document_usd_property(u, id))
                return false;
        }
    }
    {
        image_document_usdprim *p = &u->prims[id];
        if (p->kind == 1 && ((p->seen & 7) != 7 || p->sum != p->indices || p->maxindex >= p->points))
            return false;
        if (p->ops != p->order)
            return false;
    }
    return true;
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    image_document_usd u;
    uint64_t defaultAt = 0, defaultSize = 0, at, z, header;
    uint32_t seen = 0;
    unsigned i;
    bool ok = false;
    if (n < 16 || !component_tag(b, "#usda 1.0", 9) || (b[9] != 10 && b[9] != 13) || !component_utf8(b, n, false, pd))
        return false;
    xx_mem_zero(&u, sizeof(u));
    u.q.b = b;
    u.q.p = 9;
    u.q.n = n;
    u.q.pd = pd;
    u.q.hash = true;
    u.f = f;
    u.s = s;
    u.prims = (image_document_usdprim *)xx_mem_alloc(sizeof(*u.prims) * 1025);
    if (!u.prims)
        return false;
    xx_mem_zero(u.prims, sizeof(*u.prims) * 1025);
    if (!component_lexer_skip_hash_cpp_comments(&u.q))
        goto done;
    if (u.q.p < n && b[u.q.p] == '(') {
        ++u.q.p;
        while (true) {
            uint32_t bit;
            double v;
            if (!component_lexer_skip_hash_cpp_comments(&u.q) || u.q.p == n)
                goto done;
            if (b[u.q.p] == ')') {
                ++u.q.p;
                break;
            }
            if (!component_lexer_identifier_hash_cpp_comments(&u.q, &at, &z) ||
                !component_lexer_char_hash_cpp_comments(&u.q, '='))
                goto done;
            if (z == 11 && component_tag(b + at, "defaultPrim", 11)) {
                bit = 1;
                if (!image_document_usd_name(&u.q, &defaultAt, &defaultSize))
                    goto done;
            } else if (z == 6 && component_tag(b + at, "upAxis", 6)) {
                bit = 2;
                if (!component_lexer_quoted_hash_cpp_comments(&u.q, '"', &at, &z) || z != 1 ||
                    (b[at] != 'Y' && b[at] != 'Z'))
                    goto done;
            } else if (z == 13 && component_tag(b + at, "metersPerUnit", 13)) {
                bit = 4;
                if (!component_lexer_number_hash_cpp_comments(&u.q, &v) || v <= 0)
                    goto done;
            } else if (z == 18 && component_tag(b + at, "timeCodesPerSecond", 18)) {
                bit = 8;
                if (!component_lexer_number_hash_cpp_comments(&u.q, &v) || v <= 0)
                    goto done;
            } else
                goto done;
            if (seen & bit)
                goto done;
            seen |= bit;
        }
    }
    header = u.q.p;
    if (!component_emit(f, s, "layer-descriptor.usda", 0, header, n))
        goto done;
    while (component_lexer_skip_hash_cpp_comments(&u.q) && u.q.p < n)
        if (!image_document_usd_prim(&u, 0, 0))
            goto done;
    if (!u.count || !component_lexer_end_hash_cpp_comments(&u.q))
        goto done;
    if (defaultSize) {
        bool found = false;
        for (i = 1; i <= u.count; ++i)
            if (!u.prims[i].parent && u.prims[i].size == defaultSize &&
                component_tag(b + u.prims[i].name, (const char *)b + defaultAt, (size_t)defaultSize))
                found = true;
        if (!found)
            goto done;
    }
    ok = component_cover(f, s, "layer-prim-framing.txt", n);
done:
    xx_mem_free(u.prims);
    return ok;
}

void xx_openusd_usda_init(xx_openusd_usda *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_OPENUSD_USDA, "usda");
    }
}
xx_openusd_usda *xx_openusd_usda_create(xx_io_device *d, int64_t at) {
    xx_openusd_usda *r = (xx_openusd_usda *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_openusd_usda_init(r, d, at);
    return r;
}
void xx_openusd_usda_destroy(xx_openusd_usda *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_openusd_usda_free(xx_openusd_usda *r) {
    if (r) {
        xx_openusd_usda_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_openusd_usda_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_openusd_usda_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
