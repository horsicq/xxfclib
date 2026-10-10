/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/ASE/ASEParser.cpp
 * Autodesk ASE200 static ASCII triangle meshes: complete typed scene, Standard materials, node transforms, ordered unique vertex/face IDs, finite coordinates and local index/material-reference checks. Original encoded sections exported. Nonzero animation times, UV/normal/color lists, external maps, hierarchy nodes and other material/node extensions declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/autodesk_ase/xx_autodesk_ase.h"
#include "../common/xx_component_text.h"

static bool graphics_text_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool graphics_text_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(graphics_text, 33554432, )
static bool graphics_text_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[19];
    return n >= 20 && pm_read(f, 0, b, 19) && component_tag(b, "*3DSMAX_ASCIIEXPORT", 18);
}
static bool ae_open(component_text_cursor *q) { return component_text_word(q, "{") && component_text_done(q); }
static bool ae_bool(component_text_cursor *q, unsigned count) {
    unsigned i;
    int32_t v;
    for (i = 0; i < count; ++i)
        if (!component_text_integer(q, &v) || v < 0 || v > 1)
            return false;
    return component_text_done(q);
}
static bool graphics_text_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    unsigned stack[4], depth = 0, state = 0, objects = 0, matdecl = 0, mats = 0;
    uint32_t nv = 0, nf = 0, vertices = 0, faces = 0;
    unsigned meshflags = 0, objflags = 0, tmflags = 0;
    uint64_t start = 0, covered;
    bool scene = false, material = false;
    int32_t v;
    const char *str;
    if (!component_utf8(b, n, false, pd) || !component_text_line(&q) ||
        !component_text_word(&q, "*3DSMAX_ASCIIEXPORT") || !component_text_integer(&q, &v) || v != 200 ||
        !component_text_done(&q) || !component_emit(f, s, "descriptor.ase", 0, q.p, n)) {
        return false;
    }
    covered = q.p;
    while (q.p < n) {
        if (xx_component_parser_stopped(pd))
            return false;
        if (!component_text_next(&q)) {
            if (q.p < n)
                return false;
            break;
        }
        if (component_text_word(&q, "}")) {
            if (!depth || !component_text_done(&q))
                return false;
            if (state == 2 && mats != matdecl) {
                return false;
            }
            if (state == 4 && (objflags != 7))
                return false;
            if (state == 5 && (tmflags & 15) != 15)
                return false;
            if (state == 6 && (meshflags != 15)) {
                return false;
            }
            if (state == 7 && vertices != nv)
                return false;
            if (state == 8 && faces != nf)
                return false;
            state = stack[--depth];
            if (!depth) {
                if (!component_emit(f, s, "section.ase", start, q.p - start, n))
                    return false;
                covered = q.p;
            }
            continue;
        }
        if (!depth) {
            start = covered;
            if (component_text_word(&q, "*COMMENT")) {
                if (!component_text_string(&q) || !component_text_done(&q))
                    return false;
                continue;
            }
            if (component_text_word(&q, "*SCENE")) {
                if (scene || !ae_open(&q))
                    return false;
                scene = true;
                state = 1;
            } else if (component_text_word(&q, "*MATERIAL_LIST")) {
                if (material || objects || !ae_open(&q))
                    return false;
                material = true;
                state = 2;
            } else if (component_text_word(&q, "*GEOMOBJECT")) {
                if (++objects > 1024 || !ae_open(&q))
                    return false;
                state = 4;
                objflags = 0;
            } else {
                return false;
            }
            stack[depth++] = 0;
            continue;
        }
        if (state == 1) {
            if (component_text_word(&q, "*SCENE_FILENAME")) {
                if (!component_text_string(&q) || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*SCENE_FIRSTFRAME") || component_text_word(&q, "*SCENE_LASTFRAME")) {
                if (!component_text_integer(&q, &v) || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*SCENE_FRAMESPEED") ||
                       component_text_word(&q, "*SCENE_TICKSPERFRAME")) {
                if (!component_text_integer(&q, &v) || v < 1 || v > 1000000 || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*SCENE_BACKGROUND_STATIC") ||
                       component_text_word(&q, "*SCENE_AMBIENT_STATIC")) {
                if (!component_text_numbers_36_digits(&q, 3))
                    return false;
            } else
                return false;
        } else if (state == 2) {
            if (component_text_word(&q, "*MATERIAL_COUNT")) {
                if (matdecl || !component_text_integer(&q, &v) || v < 1 || v > 4096 || !component_text_done(&q))
                    return false;
                matdecl = (unsigned)v;
            } else if (component_text_word(&q, "*MATERIAL")) {
                if (!component_text_integer(&q, &v) || v != (int32_t)mats || mats >= matdecl || !ae_open(&q))
                    return false;
                ++mats;
                stack[depth++] = state;
                state = 3;
            } else
                return false;
        } else if (state == 3) {
            if (component_text_word(&q, "*MATERIAL_NAME")) {
                if (!component_text_string(&q) || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*MATERIAL_CLASS")) {
                if (!component_text_word(&q, "\"Standard\"") || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*MATERIAL_AMBIENT") || component_text_word(&q, "*MATERIAL_DIFFUSE") ||
                       component_text_word(&q, "*MATERIAL_SPECULAR")) {
                if (!component_text_numbers_36_digits(&q, 3))
                    return false;
            } else if (component_text_word(&q, "*MATERIAL_SHINE") ||
                       component_text_word(&q, "*MATERIAL_SHINESTRENGTH") ||
                       component_text_word(&q, "*MATERIAL_TRANSPARENCY") ||
                       component_text_word(&q, "*MATERIAL_WIRESIZE") ||
                       component_text_word(&q, "*MATERIAL_XP_FALLOFF") ||
                       component_text_word(&q, "*MATERIAL_SELFILLUM")) {
                double x;
                if (!component_text_number_36_digits(&q, &x) || x < 0 || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*MATERIAL_SHADING")) {
                if ((!component_text_word(&q, "Blinn") && !component_text_word(&q, "Phong") &&
                     !component_text_word(&q, "Metal") && !component_text_word(&q, "Constant")) ||
                    !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*MATERIAL_FALLOFF")) {
                if ((!component_text_word(&q, "In") && !component_text_word(&q, "Out")) || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*MATERIAL_XP_TYPE")) {
                if ((!component_text_word(&q, "Filter") && !component_text_word(&q, "Subtractive") &&
                     !component_text_word(&q, "Additive")) ||
                    !component_text_done(&q))
                    return false;
            } else
                return false;
        } else if (state == 4) {
            if (component_text_word(&q, "*NODE_NAME")) {
                if (objflags & 1 || !component_text_string(&q) || !component_text_done(&q))
                    return false;
                objflags |= 1;
            } else if (component_text_word(&q, "*NODE_TM")) {
                if (objflags & 2 || !ae_open(&q))
                    return false;
                objflags |= 2;
                tmflags = 0;
                stack[depth++] = state;
                state = 5;
            } else if (component_text_word(&q, "*MESH")) {
                if (objflags & 4 || !ae_open(&q))
                    return false;
                objflags |= 4;
                meshflags = 0;
                nv = nf = vertices = faces = 0;
                stack[depth++] = state;
                state = 6;
            } else if (component_text_word(&q, "*PROP_MOTIONBLUR") || component_text_word(&q, "*PROP_CASTSHADOW") ||
                       component_text_word(&q, "*PROP_RECVSHADOW")) {
                if (!ae_bool(&q, 1))
                    return false;
            } else if (component_text_word(&q, "*MATERIAL_REF")) {
                if (!component_text_integer(&q, &v) || v < 0 || (unsigned)v >= matdecl || !component_text_done(&q))
                    return false;
            } else
                return false;
        } else if (state == 5) {
            unsigned row;
            str = NULL;
            for (row = 0; row < 4; ++row) {
                const char *key[] = {"*TM_ROW0", "*TM_ROW1", "*TM_ROW2", "*TM_ROW3"};
                if (component_text_word(&q, key[row])) {
                    if (tmflags & (1U << row) || !component_text_numbers_36_digits(&q, 3))
                        return false;
                    tmflags |= 1U << row;
                    str = key[row];
                    break;
                }
            }
            if (str)
                continue;
            if (component_text_word(&q, "*NODE_NAME")) {
                if (!component_text_string(&q) || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*INHERIT_POS") || component_text_word(&q, "*INHERIT_ROT") ||
                       component_text_word(&q, "*INHERIT_SCL")) {
                if (!ae_bool(&q, 3))
                    return false;
            } else if (component_text_word(&q, "*TM_POS") || component_text_word(&q, "*TM_ROTAXIS") ||
                       component_text_word(&q, "*TM_SCALE") || component_text_word(&q, "*TM_SCALEAXIS")) {
                if (!component_text_numbers_36_digits(&q, 3))
                    return false;
            } else if (component_text_word(&q, "*TM_ROTANGLE") || component_text_word(&q, "*TM_SCALEAXISANG")) {
                if (!component_text_numbers_36_digits(&q, 1))
                    return false;
            } else
                return false;
        } else if (state == 6) {
            if (component_text_word(&q, "*TIMEVALUE")) {
                if (!component_text_integer(&q, &v) || v != 0 || !component_text_done(&q))
                    return false;
            } else if (component_text_word(&q, "*MESH_NUMVERTEX")) {
                if (meshflags & 1 || !component_text_integer(&q, &v) || v < 3 || v > 1000000 ||
                    !component_text_done(&q))
                    return false;
                nv = (uint32_t)v;
                meshflags |= 1;
            } else if (component_text_word(&q, "*MESH_NUMFACES")) {
                if (meshflags & 2 || !component_text_integer(&q, &v) || v < 1 || v > 1000000 ||
                    !component_text_done(&q))
                    return false;
                nf = (uint32_t)v;
                meshflags |= 2;
            } else if (component_text_word(&q, "*MESH_VERTEX_LIST")) {
                if (!(meshflags & 1) || (meshflags & 4) || !ae_open(&q))
                    return false;
                meshflags |= 4;
                stack[depth++] = state;
                state = 7;
            } else if (component_text_word(&q, "*MESH_FACE_LIST")) {
                if ((meshflags & 7) != 7 || (meshflags & 8) || !ae_open(&q))
                    return false;
                meshflags |= 8;
                stack[depth++] = state;
                state = 8;
            } else
                return false;
        } else if (state == 7) {
            if (vertices >= nv || !component_text_word(&q, "*MESH_VERTEX") || !component_text_integer(&q, &v) ||
                v != (int32_t)vertices++ || !component_text_numbers_36_digits(&q, 3))
                return false;
        } else if (state == 8) {
            const char *keys[] = {"A:", "B:", "C:", "AB:", "BC:", "CA:"};
            int32_t indexes[3];
            unsigned j;
            if (faces >= nf || !component_text_word(&q, "*MESH_FACE") || !component_text_integer(&q, &v) ||
                v != (int32_t)faces++ || q.t == q.stop || b[q.t++] != ':')
                return false;
            for (j = 0; j < 6; ++j) {
                if (!component_text_word(&q, keys[j]) || !component_text_integer(&q, &v) || v < 0 ||
                    (j < 3 ? (uint32_t)v >= nv : v > 1))
                    return false;
                if (j < 3)
                    indexes[j] = v;
            }
            if (indexes[0] == indexes[1] || indexes[0] == indexes[2] || indexes[1] == indexes[2] ||
                !component_text_word(&q, "*MESH_SMOOTHING"))
                return false;
            do {
                if (!component_text_integer(&q, &v) || v < 0 || v > 32)
                    return false;
                component_text_space(&q);
                if (q.t == q.stop || b[q.t] != ',')
                    break;
                ++q.t;
            } while (true);
            if (!component_text_word(&q, "*MESH_MTLID") || !component_text_integer(&q, &v) || v < 0 || v > 65535 ||
                !component_text_done(&q))
                return false;
        } else
            return false;
    }
    if (depth || !scene || !objects)
        return false;
    if (covered < n && !component_emit(f, s, "trailing.ase", covered, n - covered, n))
        return false;
    s->size = (int64_t)n;
    return true;
}

void xx_autodesk_ase_init(xx_autodesk_ase *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_AUTODESK_ASE, "ase");
    }
}
xx_autodesk_ase *xx_autodesk_ase_create(xx_io_device *d, int64_t at) {
    xx_autodesk_ase *r = (xx_autodesk_ase *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_autodesk_ase_init(r, d, at);
    return r;
}
void xx_autodesk_ase_destroy(xx_autodesk_ase *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_autodesk_ase_free(xx_autodesk_ase *r) {
    if (r) {
        xx_autodesk_ase_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_autodesk_ase_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_autodesk_ase_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
