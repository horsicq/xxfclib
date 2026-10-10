/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://geomview.sourceforge.net/docs/html/OFF.html
 * ASCII OFF polygon meshes with complete counts, finite three-coordinate vertices, bounded nondegenerate index lists and optional RGB/RGBA face colors.
 * COFF/NOFF/higher-dimensional and binary dialects unsupported. Original encoded typed sections exported. Limits64MiB input,4096 components; encoded assets are never
 * executed.
 */
#include "xxfclib/formats/off_mesh/xx_off_mesh.h"
#include "../common/xx_component_text.h"

static bool model_image_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool model_image_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(model_image, 67108864, )
static bool model_image_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[3];
    return n >= 30 && pm_read(f, 0, b, 3) && component_tag(b, "OFF", 3);
}
static bool off_next(component_text_cursor *q)
{
    while (q->p < q->end) {
        if (!component_text_line(q)) return false;
        if (!component_text_done(q)) return true;
    }
    return false;
}
static bool model_image_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t nv, nf, ne, i;
    uint64_t vs, fs;
    if (!component_utf8(b, n, true, pd) || !off_next(&q) || !component_text_word(&q, "OFF") || !component_text_done(&q) || !off_next(&q) ||
        !component_text_integer(&q, &nv) || !component_text_integer(&q, &nf) || !component_text_integer(&q, &ne) || !component_text_done(&q) || nv < 3 || nv > 1000000 ||
        nf < 1 || nf > 1000000 || ne < 0)
        return false;
    vs = q.p;
    for (i = 0; i < nv; ++i) {
        if (xx_component_parser_stopped(pd) || !off_next(&q) || !component_text_numbers_18_digits(&q, 3)) return false;
    }
    fs = q.p;
    for (i = 0; i < nf; ++i) {
        int32_t count, j, used[256];
        unsigned colors = 0;
        double v;
        if (xx_component_parser_stopped(pd) || !off_next(&q) || !component_text_integer(&q, &count) || count < 3 || count > 256) return false;
        for (j = 0; j < count; ++j) {
            int32_t k;
            if (!component_text_integer(&q, &used[j]) || used[j] < 0 || used[j] >= nv) return false;
            for (k = 0; k < j; ++k)
                if (used[k] == used[j]) return false;
        }
        while (!component_text_done(&q)) {
            if (++colors > 4 || !component_text_number_18_digits(&q, &v) || v < 0 || v > 255) return false;
        }
        if (colors != 0 && colors != 3 && colors != 4) return false;
    }
    while (q.p < n) {
        if (!component_text_line(&q) || !component_text_done(&q)) return false;
    }
    if (!component_emit(f, s, "descriptor.off", 0, vs, n) || !component_emit(f, s, "vertices.off", vs, fs - vs, n) ||
        !component_emit(f, s, "polygons.off", fs, n - fs, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_off_mesh_init(xx_off_mesh *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_OFF_MESH, "off");
    }
}
xx_off_mesh *xx_off_mesh_create(xx_io_device *d, int64_t at)
{
    xx_off_mesh *r = (xx_off_mesh *)xx_mem_alloc(sizeof(*r));
    if (r) xx_off_mesh_init(r, d, at);
    return r;
}
void xx_off_mesh_destroy(xx_off_mesh *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_off_mesh_free(xx_off_mesh *r)
{
    if (r) {
        xx_off_mesh_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_off_mesh_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_off_mesh_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
