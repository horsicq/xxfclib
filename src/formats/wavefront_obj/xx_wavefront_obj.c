/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/blender/blender/main/source/blender/io/wavefront_obj/importer/obj_import_file_reader.cc
 * UTF8 OBJ polygon meshes: complete finite vertex/texture/normal records, positive and relative face references, bounded named groups/material metadata. Freeform curves, continuations, external materials and rendering unsupported. Offset-zero detection only.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/wavefront_obj/xx_wavefront_obj.h"
#include "../common/xx_component_text.h"

static bool model_image_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool model_image_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(model_image, 67108864, )
static bool model_image_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[1];
    return n >= 24 && pm_read(f, 0, b, 1);
}
static bool obj_ref(component_text_cursor *q, uint32_t count, int32_t *index) {
    int32_t v;
    if (!component_text_integer(q, &v) || !v || !count)
        return false;
    if (v < 0) {
        if ((uint32_t)(-v) > count)
            return false;
        v = (int32_t)count + v;
    } else {
        if ((uint32_t)v > count)
            return false;
        --v;
    }
    *index = v;
    return true;
}
static bool model_image_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    uint32_t vertices = 0, uvs = 0, normals = 0, faces = 0, lines = 0;
    unsigned last = 0;
    uint64_t section = 0;
    bool first = true;
    const char *names[] = {"metadata.obj", "vertices.obj", "texture-coordinates.obj", "normals.obj", "polygons.obj"};
    if (!component_utf8(b, n, false, pd))
        return false;
    while (q.p < n) {
        unsigned kind = 0;
        double v;
        if (xx_component_parser_stopped(pd) || ++lines > 1000000 || !component_text_line(&q))
            return false;
        if (component_text_done(&q))
            kind = 0;
        else if (component_text_word(&q, "v")) {
            if (!component_text_numbers_18_digits(&q, 3) || ++vertices > 1000000)
                return false;
            kind = 1;
        } else if (component_text_word(&q, "vt")) {
            if (!component_text_number_18_digits(&q, &v))
                return false;
            if (!component_text_done(&q) && !component_text_number_18_digits(&q, &v))
                return false;
            if (!component_text_done(&q) && !component_text_number_18_digits(&q, &v))
                return false;
            if (!component_text_done(&q) || ++uvs > 1000000)
                return false;
            kind = 2;
        } else if (component_text_word(&q, "vn")) {
            if (!component_text_numbers_18_digits(&q, 3) || ++normals > 1000000)
                return false;
            kind = 3;
        } else if (component_text_word(&q, "f")) {
            int32_t used[256];
            unsigned count = 0, i;
            int style = -1;
            while (!component_text_done(&q)) {
                int32_t vi, ti;
                int st = 0;
                if (count == 256 || !obj_ref(&q, vertices, &vi))
                    return false;
                if (q.t < q.stop && b[q.t] == '/') {
                    ++q.t;
                    st = 1;
                    if (q.t < q.stop && b[q.t] != '/') {
                        if (!obj_ref(&q, uvs, &ti))
                            return false;
                        st = 2;
                    }
                    if (q.t < q.stop && b[q.t] == '/') {
                        ++q.t;
                        if (!obj_ref(&q, normals, &ti))
                            return false;
                        st += 2;
                    } else if (st == 1)
                        return false;
                }
                if (q.t < q.stop && b[q.t] != 32 && b[q.t] != 9 && b[q.t] != '#')
                    return false;
                if (style < 0)
                    style = st;
                else if (style != st)
                    return false;
                for (i = 0; i < count; ++i)
                    if (used[i] == vi)
                        return false;
                used[count++] = vi;
            }
            if (count < 3 || ++faces > 1000000) {
                return false;
            }
            kind = 4;
        } else if (component_text_word(&q, "s")) {
            int32_t smooth;
            if (component_text_word(&q, "off")) {
                if (!component_text_done(&q))
                    return false;
            } else if (!component_text_integer(&q, &smooth) || smooth < 0 || !component_text_done(&q))
                return false;
        } else if (component_text_word(&q, "o") || component_text_word(&q, "g") || component_text_word(&q, "usemtl") ||
                   component_text_word(&q, "mtllib")) {
            component_text_space(&q);
            if (q.stop - q.t > 4096)
                return false;
            q.t = q.stop;
        } else
            return false;
        if (!first && kind != last) {
            if (!component_emit(f, s, names[last], section, q.start - section, n))
                return false;
            section = q.start;
        }
        first = false;
        last = kind;
    }
    if (vertices < 3 || !faces || !component_emit(f, s, names[last], section, n - section, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_wavefront_obj_init(xx_wavefront_obj *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_WAVEFRONT_OBJ, "obj");
    }
}
xx_wavefront_obj *xx_wavefront_obj_create(xx_io_device *d, int64_t at) {
    xx_wavefront_obj *r = (xx_wavefront_obj *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_wavefront_obj_init(r, d, at);
    return r;
}
void xx_wavefront_obj_destroy(xx_wavefront_obj *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_wavefront_obj_free(xx_wavefront_obj *r) {
    if (r) {
        xx_wavefront_obj_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_wavefront_obj_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_wavefront_obj_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
