/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.web3d.org/documents/specifications/14772/V2.0/part1/nodesRef.html
 * VRML97 bounded complete static node subset: Shape/Appearance/Material/IndexedFaceSet/Coordinate/Group/Transform and basic primitive/view/light metadata, typed fields, finite vectors, bounded indexes, legal node contexts, normalized rotation axes and balanced structure. Original encoded root nodes exported. DEF/USE, scripting, PROTO/ROUTE, textures and other nodes/fields declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/vrml_scene/xx_vrml_scene.h"
#include "../common/xx_component_lexer.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[15];
    return n >= 24 && pm_read(f, 0, b, 15) && component_tag(b, "#VRML V2.0 utf8", 15);
}
static bool vrml_vec(component_lexer *q, unsigned size, bool color, bool positive) {
    unsigned i;
    double v;
    for (i = 0; i < size; ++i)
        if (!component_lexer_number_hash_block_comments(q, &v) || (color && (v < 0 || v > 1)) || (positive && v <= 0))
            return false;
    return true;
}
static bool vrml_bool(component_lexer *q) {
    return component_lexer_keyword_hash_block_comments(q, "TRUE") ||
           component_lexer_keyword_hash_block_comments(q, "FALSE");
}
static bool vrml_strings(component_lexer *q, bool navigation) {
    bool array = component_lexer_char_hash_block_comments(q, '[');
    unsigned count = 0;
    do {
        uint64_t p, z;
        if (array && component_lexer_char_hash_block_comments(q, ']'))
            return true;
        if (++count > 256 || !component_lexer_quoted_hash_block_comments(q, '"', &p, &z))
            return false;
        if (navigation && !((z == 7 && component_tag(q->b + p, "EXAMINE", 7)) ||
                            (z == 4 && (component_tag(q->b + p, "WALK", 4) || component_tag(q->b + p, "NONE", 4))) ||
                            (z == 3 && (component_tag(q->b + p, "FLY", 3) || component_tag(q->b + p, "ANY", 3))) ||
                            (z == 6 && component_tag(q->b + p, "LOOKAT", 6))))
            return false;
        if (!array)
            return true;
    } while (count <= 256);
    return false;
}
static bool vrml_child(unsigned kind) { return kind == 1 || kind == 2 || kind == 3 || (kind >= 10 && kind <= 13); }
static bool vrml_rotation(component_lexer *q) {
    double x, y, z, v, length;
    if (!component_lexer_number_hash_block_comments(q, &x) || !component_lexer_number_hash_block_comments(q, &y) ||
        !component_lexer_number_hash_block_comments(q, &z) || !component_lexer_number_hash_block_comments(q, &v))
        return false;
    length = x * x + y * y + z * z;
    return length > 0.999 && length < 1.001;
}
static bool vrml_node(component_lexer *q, unsigned depth, unsigned expected, uint32_t *points, unsigned *kindout) {
    unsigned kind, fields = 0;
    uint32_t coords = 0, maxindex = 0, indices = 0, faces = 0, facepoints = 0;
    bool haveindex = false;
    uint64_t id, z;
    if (depth > 32)
        return false;
    if (component_lexer_keyword_hash_block_comments(q, "Group"))
        kind = 1;
    else if (component_lexer_keyword_hash_block_comments(q, "Transform"))
        kind = 2;
    else if (component_lexer_keyword_hash_block_comments(q, "Shape"))
        kind = 3;
    else if (component_lexer_keyword_hash_block_comments(q, "Appearance"))
        kind = 4;
    else if (component_lexer_keyword_hash_block_comments(q, "Material"))
        kind = 5;
    else if (component_lexer_keyword_hash_block_comments(q, "Sphere"))
        kind = 6;
    else if (component_lexer_keyword_hash_block_comments(q, "Box"))
        kind = 7;
    else if (component_lexer_keyword_hash_block_comments(q, "Coordinate"))
        kind = 8;
    else if (component_lexer_keyword_hash_block_comments(q, "IndexedFaceSet"))
        kind = 9;
    else if (component_lexer_keyword_hash_block_comments(q, "WorldInfo"))
        kind = 10;
    else if (component_lexer_keyword_hash_block_comments(q, "NavigationInfo"))
        kind = 11;
    else if (component_lexer_keyword_hash_block_comments(q, "DirectionalLight"))
        kind = 12;
    else if (component_lexer_keyword_hash_block_comments(q, "Viewpoint"))
        kind = 13;
    else if (component_lexer_keyword_hash_block_comments(q, "Cone"))
        kind = 14;
    else if (component_lexer_keyword_hash_block_comments(q, "Cylinder"))
        kind = 15;
    else
        return false;
    if ((expected && kind != expected) || !component_lexer_char_hash_block_comments(q, '{'))
        return false;
    while (!component_lexer_char_hash_block_comments(q, '}')) {
        unsigned field = 0, mode = 0;
        double v;
        uint32_t unused = 0;
        unsigned child = 0;
        if (xx_component_parser_stopped(q->pd) || !component_lexer_identifier_hash_block_comments(q, &id, &z))
            return false;
#define VF(name, number, type)                                                                                         \
    if (z == sizeof(name) - 1 && component_tag(q->b + id, name, sizeof(name) - 1)) {                                   \
        field = number;                                                                                                \
        mode = type;                                                                                                   \
    }
        if (kind == 1 || kind == 2) {
            VF("children", 1, 1) else VF("bboxCenter", 2, 3) else VF("bboxSize", 3, 3) else if (kind == 2) {
                VF("translation", 4, 3)
                else VF("rotation", 5, 4) else VF("scale", 6, 13) else VF("center", 7, 3) else VF("scaleOrientation", 8,
                                                                                                  4)
            }
        } else if (kind == 3) {
            VF("geometry", 1, 2) else VF("appearance", 2, 14)
        } else if (kind == 4) {
            VF("material", 1, 15)
        } else if (kind == 5) {
            VF("diffuseColor", 1, 5)
            else VF("emissiveColor", 2, 5) else VF("specularColor", 3, 5) else VF("ambientIntensity", 4, 6) else VF(
                "shininess", 5, 6) else VF("transparency", 6, 6)
        } else if (kind == 6) {
            VF("radius", 1, 7)
        } else if (kind == 7) {
            VF("size", 1, 13)
        } else if (kind == 8) {
            VF("point", 1, 8)
        } else if (kind == 9) {
            VF("coord", 1, 16)
            else VF("coordIndex", 2, 9) else VF("ccw", 3, 10) else VF("solid", 4, 10) else VF("convex", 5, 10) else VF(
                "creaseAngle", 6, 17)
        } else if (kind == 10) {
            VF("title", 1, 11) else VF("info", 2, 12)
        } else if (kind == 11) {
            VF("headlight", 1, 10)
            else VF("type", 2, 18) else VF("avatarSize", 3, 3) else VF("speed", 4, 7) else VF("visibilityLimit", 5, 19)
        } else if (kind == 12) {
            VF("direction", 1, 3)
            else VF("color", 2, 5) else VF("intensity", 3, 6) else VF("ambientIntensity", 4, 6) else VF("on", 5, 10)
        } else if (kind == 13) {
            VF("position", 1, 3)
            else VF("orientation", 2, 4) else VF("fieldOfView", 3, 20) else VF("description", 4, 11) else VF("jump", 5,
                                                                                                             10)
        } else if (kind == 14) {
            VF("bottomRadius", 1, 7) else VF("height", 2, 7) else VF("bottom", 3, 10) else VF("side", 4, 10)
        } else if (kind == 15) {
            VF("radius", 1, 7)
            else VF("height", 2, 7) else VF("bottom", 3, 10) else VF("top", 4, 10) else VF("side", 5, 10)
        }
#undef VF
        if (!field || (fields & (1U << field))) {
            return false;
        }
        fields |= 1U << field;
        if (mode == 1) {
            unsigned count = 0;
            bool array = component_lexer_char_hash_block_comments(q, '[');
            if (array && component_lexer_char_hash_block_comments(q, ']'))
                continue;
            do {
                if (++count > 4096 || !vrml_node(q, depth + 1, 0, &unused, &child) || !vrml_child(child))
                    return false;
                if (array && component_lexer_char_hash_block_comments(q, ']'))
                    break;
                if (!array)
                    break;
            } while (count <= 4096);
        } else if (mode == 2) {
            if (!vrml_node(q, depth + 1, 0, &unused, &child) ||
                (child != 6 && child != 7 && child != 9 && child != 14 && child != 15))
                return false;
        } else if (mode == 14 || mode == 15 || mode == 16) {
            if (!vrml_node(q, depth + 1, mode == 14 ? 4 : mode == 15 ? 5 : 8, &unused, &child))
                return false;
            if (mode == 16)
                coords = unused;
        } else if (mode == 4) {
            if (!vrml_rotation(q))
                return false;
        } else if (mode == 3 || mode == 5 || mode == 13) {
            if (!vrml_vec(q, 3, mode == 5, mode == 13))
                return false;
        } else if (mode == 6 || mode == 7 || mode == 17 || mode == 19 || mode == 20) {
            if (!component_lexer_number_hash_block_comments(q, &v) || (mode == 6 && (v < 0 || v > 1)) ||
                (mode == 7 && v <= 0) || (mode == 17 && (v < 0 || v > 3.141593)) || (mode == 19 && v < 0) ||
                (mode == 20 && (v <= 0 || v >= 3.141593)))
                return false;
        } else if (mode == 10) {
            if (!vrml_bool(q))
                return false;
        } else if (mode == 11) {
            if (!component_lexer_quoted_hash_block_comments(q, '"', NULL, NULL))
                return false;
        } else if (mode == 12 || mode == 18) {
            if (!vrml_strings(q, mode == 18))
                return false;
        } else if (mode == 8) {
            if (!component_lexer_char_hash_block_comments(q, '['))
                return false;
            while (!component_lexer_char_hash_block_comments(q, ']')) {
                if (++coords > 100000 || !vrml_vec(q, 3, false, false))
                    return false;
            }
            if (!coords)
                return false;
        } else if (mode == 9) {
            int32_t index;
            if (!component_lexer_char_hash_block_comments(q, '['))
                return false;
            while (!component_lexer_char_hash_block_comments(q, ']')) {
                if (++indices > 300000 || !component_lexer_integer_hash_block_comments(q, &index) || index < -1)
                    return false;
                if (index == -1) {
                    if (facepoints < 3)
                        return false;
                    facepoints = 0;
                    if (++faces > 100000)
                        return false;
                } else {
                    if ((uint32_t)index > maxindex)
                        maxindex = (uint32_t)index;
                    haveindex = true;
                    ++facepoints;
                }
            }
            if (facepoints) {
                if (facepoints < 3)
                    return false;
                ++faces;
            }
        }
    }
    if (kind == 8 && (!coords || (fields & (1 << 1)) == 0)) {
        return false;
    }
    if (kind == 9 && (!coords || !faces || !haveindex || maxindex >= coords))
        return false;
    if (kind == 3 && !(fields & (1 << 1))) {
        return false;
    }
    if (points)
        *points = coords;
    if (kindout)
        *kindout = kind;
    return true;
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor line = {b, 0, n, 0, 0, 0};
    component_lexer q;
    unsigned roots = 0;
    uint32_t points;
    unsigned kind;
    if (!component_utf8(b, n, false, pd) || !component_text_line(&line) || line.stop < 15 ||
        !component_tag(b, "#VRML V2.0 utf8", 15) || (line.stop > 15 && b[15] != 32 && b[15] != 9) ||
        !component_emit(f, s, "descriptor.wrl", 0, line.p, n))
        return false;
    q.b = b;
    q.p = line.p;
    q.n = n;
    q.pd = pd;
    q.work = 0;
    q.hash = true;
    q.commas = true;
    q.comments = false;
    while (!component_lexer_end_hash_block_comments(&q)) {
        uint64_t start;
        if (!component_lexer_skip_hash_block_comments(&q))
            return false;
        start = q.p;
        if (++roots > 4093 || !vrml_node(&q, 0, 0, &points, &kind) || !vrml_child(kind) ||
            !component_emit(f, s, "node.wrl", start, q.p - start, n))
            return false;
    }
    if (!roots || !component_cover(f, s, "comments.wrl", n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_vrml_scene_init(xx_vrml_scene *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_VRML_SCENE, "wrl");
    }
}
xx_vrml_scene *xx_vrml_scene_create(xx_io_device *d, int64_t at) {
    xx_vrml_scene *r = (xx_vrml_scene *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_vrml_scene_init(r, d, at);
    return r;
}
void xx_vrml_scene_destroy(xx_vrml_scene *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_vrml_scene_free(xx_vrml_scene *r) {
    if (r) {
        xx_vrml_scene_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_vrml_scene_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_vrml_scene_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
