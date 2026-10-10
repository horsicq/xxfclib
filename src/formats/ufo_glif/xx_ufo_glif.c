/* SPDX-License-Identifier: MIT
 * Primary reference: https://unifiedfontobject.org/versions/ufo3/glyphs/glif/
 * UFO GLIF2 standalone glyphs: complete bounded UTF8 XML and glyph metrics/Unicode/outline contour/point/component/anchor grammar; checked point topology and unique
 * identifiers. Original descriptor and typed outline records exported; component base names retained without external loading; arbitrary lib/images/extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/ufo_glif/xx_ufo_glif.h"
#include "../common/xx_component_lexer.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n)
{
    uint8_t c;
    return n >= 32 && pm_read(f, 0, &c, 1) && (c == '<' || c == 32 || c == 9 || c == 10 || c == 13);
}
enum {
    HX_GLYPH = 1,
    HX_ADVANCE,
    HX_UNICODE,
    HX_OUTLINE,
    HX_CONTOUR,
    HX_POINT,
    HX_COMPONENT,
    HX_ANCHOR,
    HX_GUIDELINE,
    HX_NOTE,
    HX_UNUSED
};
static const char *const image_document_xtags[] = {"",      "glyph",     "advance", "unicode",   "outline", "contour",
                                                   "point", "component", "anchor",  "guideline", "note",    "unsupported"};
static const char *const image_document_xattrs[] = {"",        "name",    "format",     "width", "height",      "hex",      "x",        "y",
                                                    "type",    "smooth",  "identifier", "base",  "xScale",      "xyScale",  "yxScale",  "yScale",
                                                    "xOffset", "yOffset", "angle",      "color", "formatMinor", "reserved", "reserved2"};
typedef struct image_document_xattr {
    uint32_t p, z;
    unsigned kind;
} image_document_xattr;
typedef struct image_document_xnode {
    unsigned kind, parent, child, next;
    uint64_t start, text, stop, end;
    image_document_xattr attrs[8];
    unsigned count;
    uint32_t value, width;
} image_document_xnode;
typedef struct image_document_xml {
    const uint8_t *b;
    uint64_t n, p;
    image_document_xnode *nodes;
    unsigned count;
    xx_pd_struct *pd;
    uint32_t work;
} image_document_xml;
static bool image_document_xspace(uint8_t c)
{
    return c == 32 || c == 9 || c == 10 || c == 13;
}
static bool image_document_xskip(image_document_xml *q)
{
    while (q->p < q->n && image_document_xspace(q->b[q->p])) {
        if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
        ++q->p;
    }
    return true;
}
static unsigned image_document_xname(image_document_xml *q, bool attribute)
{
    uint64_t p = q->p;
    unsigned i, max = attribute ? 23 : 12;
    const char *const *table = attribute ? image_document_xattrs : image_document_xtags;
    while (q->p < q->n && component_lexer_identifier_char(q->b[q->p])) {
        if (q->p - p >= 64) return 0;
        ++q->p;
    }
    for (i = 1; i < max; ++i) {
        size_t z = xx_rt_strlen(table[i]);
        if (z == q->p - p && component_tag(q->b + p, table[i], z)) return i;
    }
    return 0;
}
static bool image_document_xparse(image_document_xml *q, unsigned parent, unsigned depth, unsigned *result)
{
    image_document_xnode *v;
    unsigned id, last = 0, i;
    if (depth > 64 || q->count >= 4096 || xx_component_parser_stopped(q->pd) || q->p == q->n || q->b[q->p++] != '<') return false;
    id = ++q->count;
    v = &q->nodes[id];
    v->start = q->p - 1;
    v->parent = parent;
    if (!(v->kind = image_document_xname(q, false))) return false;
    while (q->p < q->n) {
        image_document_xattr *a;
        uint8_t quote;
        uint64_t p;
        bool space = false;
        while (q->p < q->n && image_document_xspace(q->b[q->p])) {
            space = true;
            ++q->p;
        }
        if (q->p == q->n) return false;
        if (q->b[q->p] == '/' || q->b[q->p] == '>') break;
        if (!space || v->count >= 8) return false;
        a = &v->attrs[v->count];
        if (!(a->kind = image_document_xname(q, true))) return false;
        for (i = 0; i < v->count; ++i)
            if (v->attrs[i].kind == a->kind) return false;
        ++v->count;
        if (!image_document_xskip(q) || q->p == q->n || q->b[q->p++] != '=' || !image_document_xskip(q) || q->p == q->n ||
            ((quote = q->b[q->p++]) != '"' && quote != '\''))
            return false;
        p = q->p;
        while (q->p < q->n && q->b[q->p] != quote) {
            uint8_t c = q->b[q->p++];
            if (q->p - p > 255 || c == '<' || c == '&' || c < 32) return false;
        }
        if (q->p == q->n) return false;
        a->p = (uint32_t)p;
        a->z = (uint32_t)(q->p - p);
        ++q->p;
    }
    if (q->p == q->n) {
        return false;
    }
    if (q->b[q->p] == '/') {
        ++q->p;
        if (q->p == q->n || q->b[q->p++] != '>') return false;
        v->text = v->stop = q->p;
        v->end = q->p;
        *result = id;
        return true;
    }
    if (q->b[q->p++] != '>') return false;
    v->text = q->p;
    while (q->p < q->n) {
        if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
        if (q->b[q->p] == '<') {
            if (component_span(q->p, 2, q->n) && q->b[q->p + 1] == '/') {
                v->stop = q->p;
                q->p += 2;
                if (image_document_xname(q, false) != v->kind || !image_document_xskip(q) || q->p == q->n || q->b[q->p++] != '>') return false;
                v->end = q->p;
                *result = id;
                return true;
            } else {
                unsigned c;
                if (!image_document_xparse(q, id, depth + 1, &c)) return false;
                if (last) q->nodes[last].next = c;
                else v->child = c;
                last = c;
            }
        } else {
            if (q->b[q->p] == '&' || q->b[q->p] == '>') return false;
            ++q->p;
        }
    }
    return false;
}
static const image_document_xattr *image_document_xattr_get(image_document_xnode *v, unsigned type)
{
    unsigned i;
    for (i = 0; i < v->count; ++i)
        if (v->attrs[i].kind == type) return &v->attrs[i];
    return NULL;
}
static bool image_document_xeq(image_document_xml *q, const image_document_xattr *a, const char *s)
{
    return a && a->z == xx_rt_strlen(s) && component_tag(q->b + a->p, s, a->z);
}
static bool image_document_xuint(image_document_xml *q, image_document_xnode *v, unsigned type, uint32_t def, uint32_t *out, bool required)
{
    const image_document_xattr *a = image_document_xattr_get(v, type);
    uint32_t u = 0;
    unsigned i;
    if (!a) {
        *out = def;
        return !required;
    }
    if (!a->z) return false;
    for (i = 0; i < a->z; ++i) {
        uint8_t c = q->b[a->p + i];
        if (c < '0' || c > '9' || u > (1000000U - (c - '0')) / 10) return false;
        u = u * 10 + c - '0';
    }
    *out = u;
    return true;
}
static bool image_document_xattrs_ok(image_document_xnode *v, uint32_t allowed)
{
    unsigned i;
    for (i = 0; i < v->count; ++i)
        if (!(allowed & (1U << v->attrs[i].kind))) return false;
    return true;
}
static bool image_document_xempty(image_document_xml *q, image_document_xnode *v)
{
    uint64_t p = v->text;
    if (v->child) return false;
    while (p < v->stop)
        if (!image_document_xspace(q->b[p++])) return false;
    return true;
}
static bool image_document_xcontainer(image_document_xml *q, image_document_xnode *v)
{
    uint64_t p = v->text;
    unsigned i = v->child;
    while (i) {
        image_document_xnode *c = &q->nodes[i];
        while (p < c->start)
            if (!image_document_xspace(q->b[p++])) return false;
        p = c->end;
        i = c->next;
    }
    while (p < v->stop)
        if (!image_document_xspace(q->b[p++])) return false;
    return true;
}
static bool image_document_xdecl(image_document_xml *q)
{
    component_lexer t = {q->b, q->p + 5, q->n, q->pd, 0, false, false, false};
    uint64_t at, z;
    if (t.p == t.n || !image_document_xspace(t.b[t.p]) || !component_lexer_keyword_hash_cpp_comments(&t, "version") || !component_lexer_char_hash_cpp_comments(&t, '=') ||
        !component_lexer_skip_hash_cpp_comments(&t) || t.p == t.n || (t.b[t.p] != '\"' && t.b[t.p] != '\'') ||
        !component_lexer_quoted_hash_cpp_comments(&t, t.b[t.p], &at, &z) || z != 3 || !component_tag(t.b + at, "1.0", 3))
        return false;
    if (t.p < t.n && !image_document_xspace(t.b[t.p]) && t.b[t.p] != '?') return false;
    if (component_lexer_skip_hash_cpp_comments(&t) && component_span(t.p, 8, t.n) && component_tag(t.b + t.p, "encoding", 8)) {
        if (!component_lexer_keyword_hash_cpp_comments(&t, "encoding") || !component_lexer_char_hash_cpp_comments(&t, '=') ||
            !component_lexer_skip_hash_cpp_comments(&t) || t.p == t.n || (t.b[t.p] != '\"' && t.b[t.p] != '\'') ||
            !component_lexer_quoted_hash_cpp_comments(&t, t.b[t.p], &at, &z) || z != 5 || (!component_tag(t.b + at, "utf-8", 5) && !component_tag(t.b + at, "UTF-8", 5)))
            return false;
        if (t.p < t.n && !image_document_xspace(t.b[t.p]) && t.b[t.p] != '?') return false;
    }
    if (component_lexer_skip_hash_cpp_comments(&t) && component_span(t.p, 10, t.n) && component_tag(t.b + t.p, "standalone", 10)) {
        if (!component_lexer_keyword_hash_cpp_comments(&t, "standalone") || !component_lexer_char_hash_cpp_comments(&t, '=') ||
            !component_lexer_skip_hash_cpp_comments(&t) || t.p == t.n || (t.b[t.p] != '\"' && t.b[t.p] != '\'') ||
            !component_lexer_quoted_hash_cpp_comments(&t, t.b[t.p], &at, &z) ||
            !((z == 3 && component_tag(t.b + at, "yes", 3)) || (z == 2 && component_tag(t.b + at, "no", 2))))
            return false;
    }
    if (!component_lexer_skip_hash_cpp_comments(&t) || !component_span(t.p, 2, t.n) || t.b[t.p] != '?' || t.b[t.p + 1] != '>' || t.p - q->p > 128) return false;
    q->p = t.p + 2;
    return image_document_xskip(q);
}
static bool image_document_xnum(image_document_xml *q, image_document_xnode *v, unsigned kind, double def, double *out, bool mandatory)
{
    const image_document_xattr *a = image_document_xattr_get(v, kind);
    component_lexer t;
    if (!a) {
        *out = def;
        return !mandatory;
    }
    t.b = q->b;
    t.p = a->p;
    t.n = a->p + a->z;
    t.pd = q->pd;
    t.work = 0;
    t.hash = t.commas = t.comments = false;
    return component_lexer_number_hash_cpp_comments(&t, out) && component_lexer_end_hash_cpp_comments(&t);
}
static bool image_document_glif_validate(image_document_xml *q)
{
    unsigned i, j;
    uint32_t rootseen = 0, work = 0;
    for (i = 1; i <= q->count; ++i) {
        image_document_xnode *v = &q->nodes[i], *parent = &q->nodes[v->parent];
        const image_document_xattr *id = image_document_xattr_get(v, 10);
        uint32_t allowed = 0;
        double x, y;
        if (id) {
            if (!id->z || id->z > 100) return false;
            for (j = 0; j < id->z; ++j)
                if (q->b[id->p + j] < 33 || q->b[id->p + j] > 126) return false;
            for (j = 1; j < i; ++j) {
                const image_document_xattr *a = image_document_xattr_get(&q->nodes[j], 10);
                if (++work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
                if (a && a->z == id->z && component_tag(q->b + a->p, (const char *)q->b + id->p, id->z)) return false;
            }
        }
        switch (v->kind) {
            case HX_GLYPH: {
                uint32_t minor;
                const image_document_xattr *name = image_document_xattr_get(v, 1);
                allowed = (1U << 1) | (1U << 2) | (1U << 20);
                if (v->parent || !name || !name->z || !image_document_xeq(q, image_document_xattr_get(v, 2), "2") || !image_document_xuint(q, v, 20, 0, &minor, false) ||
                    minor)
                    return false;
            } break;
            case HX_ADVANCE:
                allowed = (1U << 3) | (1U << 4);
                if (parent->kind != HX_GLYPH || (rootseen & 1) || !image_document_xnum(q, v, 3, 0, &x, false) || !image_document_xnum(q, v, 4, 0, &y, false) || x < 0 ||
                    y < 0)
                    return false;
                rootseen |= 1;
                break;
            case HX_UNICODE: {
                const image_document_xattr *a = image_document_xattr_get(v, 5);
                uint32_t u = 0;
                allowed = 1U << 5;
                if (parent->kind != HX_GLYPH || !a || !a->z || a->z > 6) return false;
                for (j = 0; j < a->z; ++j) {
                    uint8_t c = q->b[a->p + j];
                    unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
                    if (d > 15) return false;
                    u = (u << 4) | d;
                }
                if (u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff)) return false;
                v->value = u;
                for (j = 1; j < i; ++j)
                    if (q->nodes[j].kind == HX_UNICODE && q->nodes[j].value == u) return false;
            } break;
            case HX_OUTLINE:
                if (parent->kind != HX_GLYPH || (rootseen & 2)) return false;
                rootseen |= 2;
                {
                    unsigned c = v->child;
                    while (c) {
                        if (q->nodes[c].kind != HX_CONTOUR && q->nodes[c].kind != HX_COMPONENT) return false;
                        c = q->nodes[c].next;
                    }
                }
                break;
            case HX_CONTOUR:
                allowed = 1U << 10;
                if (parent->kind != HX_OUTLINE || !v->child) return false;
                {
                    unsigned c = v->child;
                    while (c) {
                        if (q->nodes[c].kind != HX_POINT) return false;
                        c = q->nodes[c].next;
                    }
                }
                break;
            case HX_POINT:
                allowed = (1U << 1) | (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9) | (1U << 10);
                if (parent->kind != HX_CONTOUR || !image_document_xnum(q, v, 6, 0, &x, true) || !image_document_xnum(q, v, 7, 0, &y, true)) return false;
                {
                    const image_document_xattr *a = image_document_xattr_get(v, 8), *smooth = image_document_xattr_get(v, 9);
                    if (!a) v->value = 0;
                    else if (image_document_xeq(q, a, "move")) v->value = 1;
                    else if (image_document_xeq(q, a, "line")) v->value = 2;
                    else if (image_document_xeq(q, a, "curve")) v->value = 3;
                    else if (image_document_xeq(q, a, "qcurve")) v->value = 4;
                    else return false;
                    if (smooth && (!image_document_xeq(q, smooth, "yes") && !image_document_xeq(q, smooth, "no"))) return false;
                    if (smooth && image_document_xeq(q, smooth, "yes") && v->value == 0) return false;
                }
                break;
            case HX_COMPONENT:
                allowed = (1U << 10) | (1U << 11) | (1U << 12) | (1U << 13) | (1U << 14) | (1U << 15) | (1U << 16) | (1U << 17);
                if (parent->kind != HX_OUTLINE || !image_document_xattr_get(v, 11) || !image_document_xattr_get(v, 11)->z) return false;
                for (j = 12; j <= 17; ++j)
                    if (!image_document_xnum(q, v, j, j == 12 || j == 15 ? 1 : 0, &x, false)) return false;
                break;
            case HX_ANCHOR:
                allowed = (1U << 1) | (1U << 6) | (1U << 7) | (1U << 10);
                if (parent->kind != HX_GLYPH || !image_document_xnum(q, v, 6, 0, &x, true) || !image_document_xnum(q, v, 7, 0, &y, true)) return false;
                break;
            case HX_GUIDELINE:
                allowed = (1U << 1) | (1U << 6) | (1U << 7) | (1U << 10) | (1U << 18);
                if (parent->kind != HX_GLYPH || (!image_document_xattr_get(v, 6) && !image_document_xattr_get(v, 7)) || !image_document_xnum(q, v, 6, 0, &x, false) ||
                    !image_document_xnum(q, v, 7, 0, &y, false))
                    return false;
                if (image_document_xattr_get(v, 6) && image_document_xattr_get(v, 7)) {
                    if (!image_document_xnum(q, v, 18, 0, &x, true) || x < 0 || x > 360) return false;
                } else if (image_document_xattr_get(v, 18)) return false;
                break;
            case HX_NOTE:
                if (parent->kind != HX_GLYPH || (rootseen & 4) || v->child) return false;
                rootseen |= 4;
                break;
            default: return false;
        }
        if (!image_document_xattrs_ok(v, allowed)) {
            return false;
        }
        if (v->kind == HX_GLYPH || v->kind == HX_OUTLINE || v->kind == HX_CONTOUR) {
            if (!image_document_xcontainer(q, v)) return false;
        } else if (v->kind != HX_NOTE && !image_document_xempty(q, v)) return false;
    }
    for (i = 1; i <= q->count; ++i)
        if (q->nodes[i].kind == HX_CONTOUR) {
            unsigned first = q->nodes[i].child, c = first, on = 0, count = 0, off = 0;
            bool open = false;
            while (c) {
                if (q->nodes[c].value && !on) on = c;
                ++count;
                c = q->nodes[c].next;
            }
            if (!on) return false;
            if (q->nodes[on].value == 1) {
                if (on != first) return false;
                open = true;
            }
            c = q->nodes[on].next;
            if (!c && !open) c = first;
            for (j = 0; j < count - (open ? 1 : 0); ++j) {
                unsigned type;
                if (!c) return false;
                type = q->nodes[c].value;
                if (!type) ++off;
                else {
                    if (type == 1 || (type == 2 && off) || (type == 3 && off != 2) || (type == 4 && !off)) return false;
                    off = 0;
                }
                c = q->nodes[c].next;
                if (!c && !open) c = first;
            }
            if (off) return false;
        }
    return true;
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    image_document_xml q = {b, n, 0, NULL, 0, pd, 0};
    unsigned root = 0, c, part = 0;
    bool ok = false;
    char label[48];
    if (!component_utf8(b, n, false, pd)) return false;
    q.nodes = (image_document_xnode *)xx_mem_alloc(sizeof(*q.nodes) * 4097);
    if (!q.nodes) return false;
    xx_mem_zero(q.nodes, sizeof(*q.nodes) * 4097);
    if (!image_document_xskip(&q)) goto done;
    if (component_span(q.p, 5, n) && component_tag(b + q.p, "<?xml", 5) && !image_document_xdecl(&q)) goto done;
    if (!image_document_xparse(&q, 0, 0, &root) || root != 1 || q.nodes[root].kind != HX_GLYPH || !image_document_xskip(&q) || q.p != n ||
        !image_document_glif_validate(&q))
        goto done;
    c = q.nodes[root].child;
    while (c) {
        image_document_xnode *v = &q.nodes[c];
        xx_rt_snprintf(label, sizeof(label), "glyph-%u-%s.glif", part++, image_document_xtags[v->kind]);
        if (!component_emit(f, s, label, v->start, v->end - v->start, n)) goto done;
        c = v->next;
    }
    ok = component_cover(f, s, "xml-framing.txt", n);
done:
    xx_mem_free(q.nodes);
    return ok;
}

void xx_ufo_glif_init(xx_ufo_glif *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_UFO_GLIF, "glif");
    }
}
xx_ufo_glif *xx_ufo_glif_create(xx_io_device *d, int64_t at)
{
    xx_ufo_glif *r = (xx_ufo_glif *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ufo_glif_init(r, d, at);
    return r;
}
void xx_ufo_glif_destroy(xx_ufo_glif *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_ufo_glif_free(xx_ufo_glif *r)
{
    if (r) {
        xx_ufo_glif_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ufo_glif_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_ufo_glif_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
