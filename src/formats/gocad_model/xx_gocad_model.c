/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/lanl/LaGriT/master/src/read_gocad_tsurf.f
 * GOCAD TSurf1 triangulated surfaces: complete header/property declarations and TFACE records with unique local vertex IDs, finite coordinates/properties, resolved ATOM/TRGL/border references and END framing. Original descriptor and typed surface records exported; external coordinate systems/solid/voxel/complex extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/gocad_model/xx_gocad_model.h"
#include "../common/xx_component_text.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[13];
    return n >= 24 && pm_read(f, 0, b, 13) && component_tag(b, "GOCAD TSurf 1", 13);
}
static bool mesh_font_gocad_field(component_text_cursor *q, const char *key) {
    size_t z = xx_rt_strlen(key);
    component_text_space(q);
    if (!component_span(q->t, z + 1, q->stop) || !component_tag(q->b + q->t, key, z) || q->b[q->t + z] != ':')
        return false;
    q->t += z + 1;
    return true;
}
static bool mesh_font_gocad_words(component_text_cursor *q, unsigned *count) {
    unsigned n = 0;
    component_text_space(q);
    while (q->t < q->stop) {
        uint64_t at = q->t;
        while (q->t < q->stop && q->b[q->t] != 32 && q->b[q->t] != 9) {
            if (q->t - at >= 255)
                return false;
            ++q->t;
        }
        if (q->t == at)
            return false;
        ++n;
        component_text_space(q);
    }
    *count = n;
    return n > 0 && n <= 64;
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    component_id_set ids = {0};
    unsigned props = 0, faces = 0, nodes = 0, triangles = 0;
    bool header = false, ended = false, ok = false, name = false;
    uint64_t face = 0;
    char label[48];
    int32_t value;
    if (!component_utf8(b, n, true, pd) || !component_text_next_poison_overflow(&q) ||
        !component_text_word(&q, "GOCAD") || !component_text_word(&q, "TSurf") || !component_text_integer(&q, &value) ||
        value != 1 || !component_text_done(&q) || !component_text_next_poison_overflow(&q) ||
        !component_text_word(&q, "HEADER") || !component_text_word(&q, "{") || !component_text_done(&q))
        return false;
    while (component_text_next_poison_overflow(&q)) {
        if (component_text_word(&q, "}")) {
            if (!component_text_done(&q))
                return false;
            header = true;
            break;
        }
        if (mesh_font_gocad_field(&q, "name")) {
            if (name || q.t == q.stop || q.stop - q.t > 255)
                return false;
            name = true;
        } else if (mesh_font_gocad_field(&q, "*solid*color")) {
            unsigned i;
            double v;
            for (i = 0; i < 4; ++i)
                if (!component_text_number_36_digits(&q, &v) || v < 0 || v > 1)
                    return false;
            if (!component_text_done(&q))
                return false;
        } else
            return false;
    }
    if (!header || !name || !component_ids_init(&ids, 1000000))
        return false;
    while (component_text_next_poison_overflow(&q)) {
        if (xx_component_parser_stopped(pd))
            goto done;
        if (component_text_word(&q, "GOCAD_ORIGINAL_COORDINATE_SYSTEM")) {
            if (faces || !component_text_done(&q) || !component_text_next_poison_overflow(&q) ||
                !component_text_word(&q, "NAME") || q.t == q.stop || !component_text_next_poison_overflow(&q) ||
                !component_text_word(&q, "AXIS_NAME") || !component_text_string(&q) || !component_text_string(&q) ||
                !component_text_string(&q) || !component_text_done(&q) || !component_text_next_poison_overflow(&q) ||
                !component_text_word(&q, "AXIS_UNIT") || !component_text_string(&q) || !component_text_string(&q) ||
                !component_text_string(&q) || !component_text_done(&q) || !component_text_next_poison_overflow(&q) ||
                !component_text_word(&q, "ZPOSITIVE") ||
                (!component_text_word(&q, "Elevation") && !component_text_word(&q, "Depth")) ||
                !component_text_done(&q) || !component_text_next_poison_overflow(&q) ||
                !component_text_word(&q, "END_ORIGINAL_COORDINATE_SYSTEM") || !component_text_done(&q))
                goto done;
        } else if (component_text_word(&q, "GEOLOGICAL_TYPE")) {
            unsigned words;
            if (faces || !mesh_font_gocad_words(&q, &words) || words != 1)
                goto done;
        } else if (component_text_word(&q, "PROPERTIES")) {
            if (faces || props || !mesh_font_gocad_words(&q, &props) || !component_text_next_poison_overflow(&q) ||
                !component_text_word(&q, "ESIZES"))
                goto done;
            {
                unsigned i;
                for (i = 0; i < props; ++i)
                    if (!component_text_integer(&q, &value) || value != 1)
                        goto done;
            }
            if (!component_text_done(&q))
                goto done;
        } else if (component_text_word(&q, "TFACE")) {
            if (!component_text_done(&q))
                goto done;
            if (face) {
                if (!nodes || !triangles)
                    goto done;
                xx_rt_snprintf(label, sizeof(label), "surface-%u.ts", faces - 1);
                if (!component_emit(f, s, label, face, q.start - face, n))
                    goto done;
            } else if (!component_emit(f, s, "descriptor.ts", 0, q.start, n))
                goto done;
            if (++faces > 4000)
                goto done;
            face = q.start;
            nodes = triangles = 0;
        } else if (component_text_word(&q, "VRTX") || component_text_word(&q, "PVRTX")) {
            unsigned i, dimensions = 3 + props;
            double v;
            if (!face || !component_text_integer(&q, &value) || value < 1 ||
                !component_id(&ids, (uint32_t)value, true, pd))
                goto done;
            for (i = 0; i < dimensions; ++i)
                if (!component_text_number_36_digits(&q, &v))
                    goto done;
            if (!component_text_done(&q) || ++nodes > 1000000)
                goto done;
        } else if (component_text_word(&q, "ATOM")) {
            int32_t target;
            if (!face || !component_text_integer(&q, &value) || value < 1 || !component_text_integer(&q, &target) ||
                target < 1 || !component_text_done(&q) || !component_id(&ids, (uint32_t)target, false, pd) ||
                !component_id(&ids, (uint32_t)value, true, pd) || ++nodes > 1000000)
                goto done;
        } else if (component_text_word(&q, "TRGL")) {
            int32_t a, c, d;
            if (!face || !component_text_integer(&q, &a) || !component_text_integer(&q, &c) ||
                !component_text_integer(&q, &d) || a < 1 || c < 1 || d < 1 || a == c || a == d || c == d ||
                !component_text_done(&q) || !component_id(&ids, (uint32_t)a, false, pd) ||
                !component_id(&ids, (uint32_t)c, false, pd) || !component_id(&ids, (uint32_t)d, false, pd) ||
                ++triangles > 1000000)
                goto done;
        } else if (component_text_word(&q, "BSTONE")) {
            if (!face || !component_text_integer(&q, &value) || value < 1 ||
                !component_id(&ids, (uint32_t)value, false, pd) || !component_text_done(&q))
                goto done;
        } else if (component_text_word(&q, "BORDER")) {
            int32_t a, c;
            if (!face || !component_text_integer(&q, &value) || value < 1 || !component_text_integer(&q, &a) ||
                !component_text_integer(&q, &c) || a < 1 || c < 1 || a == c ||
                !component_id(&ids, (uint32_t)a, false, pd) || !component_id(&ids, (uint32_t)c, false, pd) ||
                !component_text_done(&q))
                goto done;
        } else if (component_text_word(&q, "END")) {
            if (!face || !nodes || !triangles || !component_text_done(&q))
                goto done;
            xx_rt_snprintf(label, sizeof(label), "surface-%u.ts", faces - 1);
            if (!component_emit(f, s, label, face, q.start - face, n) ||
                !component_emit(f, s, "terminator.ts", q.start, q.p - q.start, n))
                goto done;
            ended = true;
            break;
        } else
            goto done;
    }
    ok = ended && !component_text_next_poison_overflow(&q) && q.p == q.end && component_cover(f, s, "framing.ts", n);
done:
    xx_mem_free(ids.values);
    return ok;
}

void xx_gocad_model_init(xx_gocad_model *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_GOCAD_MODEL, "ts");
    }
}
xx_gocad_model *xx_gocad_model_create(xx_io_device *d, int64_t at) {
    xx_gocad_model *r = (xx_gocad_model *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_gocad_model_init(r, d, at);
    return r;
}
void xx_gocad_model_destroy(xx_gocad_model *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gocad_model_free(xx_gocad_model *r) {
    if (r) {
        xx_gocad_model_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gocad_model_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_gocad_model_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
