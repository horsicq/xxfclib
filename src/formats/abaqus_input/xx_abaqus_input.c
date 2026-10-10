/* SPDX-License-Identifier: MIT
 * Primary reference: https://docs.software.vt.edu/abaqusv2025/English/SIMACAEMODRefMap/simamod-c-inputsyntax.htm
 * Abaqus mesh input subset: complete typed NODE/ELEMENT/ELSET/NSET/SURFACE blocks, finite coordinates, unique local identifiers and resolved connectivity/set references.
 * Original typed mesh blocks and comments exported; includes, solver steps, material evaluation and arbitrary keywords declined. Bounded32MiB input,4096 components and
 * bounded work.
 */
#include "xxfclib/formats/abaqus_input/xx_abaqus_input.h"
#include "../common/xx_component_lexer.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t c;
    return n >= 24 && pm_read(f, 0, &c, 1) && (c == '*' || c == 32);
}
static bool mesh_font_ab_key(component_text_cursor *q, const char *s)
{
    size_t z = xx_rt_strlen(s), i;
    component_text_space(q);
    if (!component_span(q->t, z, q->stop)) return false;
    for (i = 0; i < z; ++i) {
        uint8_t c = q->b[q->t + i], v = (uint8_t)s[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (v >= 'a' && v <= 'z') v -= 32;
        if (c != v) return false;
    }
    if (q->t + z < q->stop && q->b[q->t + z] != ',' && q->b[q->t + z] != 32 && q->b[q->t + z] != 9 && q->b[q->t + z] != '=') return false;
    q->t += z;
    return true;
}
static bool mesh_font_ab_punct(component_text_cursor *q, uint8_t c)
{
    component_text_space(q);
    if (q->t == q->stop || q->b[q->t++] != c) return false;
    return true;
}
static bool mesh_font_ab_name(component_text_cursor *q)
{
    uint64_t start;
    component_text_space(q);
    start = q->t;
    while (q->t < q->stop && q->b[q->t] != ',' && q->b[q->t] != 32 && q->b[q->t] != 9) {
        uint8_t c = q->b[q->t++];
        if (q->t - start > 255 || (!component_lexer_identifier_char(c) && c != '.')) return false;
    }
    return q->t > start;
}
static bool mesh_font_ab_csv(component_text_cursor *q, int32_t *v, bool more)
{
    component_text_space(q);
    if (!component_text_integer(q, v)) return false;
    component_text_space(q);
    if (more) return mesh_font_ab_punct(q, ',');
    if (q->t < q->stop && q->b[q->t] == ',') ++q->t;
    return component_text_done(q);
}
static bool mesh_font_ab_real(component_text_cursor *q, double *v, bool more)
{
    uint64_t stop;
    component_text_cursor t;
    component_text_space(q);
    stop = q->t;
    while (stop < q->stop && q->b[stop] != ',' && q->b[stop] != 32 && q->b[stop] != 9) ++stop;
    t = *q;
    t.stop = stop;
    if (!component_text_number_36_digits(&t, v) || t.t != stop) return false;
    q->t = stop;
    component_text_space(q);
    return more ? mesh_font_ab_punct(q, ',') : component_text_done(q);
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    component_id_set nodes = {0}, elements = {0};
    uint32_t nodecount = 0, elementcount = 0;
    unsigned mode = 0, arity = 0, block = 0, rows = 0;
    uint64_t start = 0;
    bool ok = false, active = false;
    char label[48];
    if (!component_utf8(b, n, true, pd) || !component_ids_init(&nodes, 1000000) || !component_ids_init(&elements, 1000000)) goto done;
    while (component_text_line_poison_overflow(&q)) {
        int32_t id, index;
        unsigned i;
        double v;
        component_text_space(&q);
        if (xx_component_parser_stopped(pd)) goto done;
        if (q.t == q.stop || (component_span(q.t, 2, q.stop) && b[q.t] == '*' && b[q.t + 1] == '*')) continue;
        if (b[q.t] == '*') {
            if (active) {
                if (mode != 1 && mode != 2 && mode != 7 && !rows) goto done;
                xx_rt_snprintf(label, sizeof(label), "block-%u.inp", block++);
                if (!component_emit(f, s, label, start, q.start - start, n)) goto done;
            }
            start = q.start;
            active = true;
            rows = 0;
            ++q.t;
            if (mesh_font_ab_key(&q, "Heading")) {
                mode = 1;
                if (!component_text_done(&q)) goto done;
            } else if (mesh_font_ab_key(&q, "Preprint")) {
                mode = 2;
                while (q.t < q.stop) {
                    if (!mesh_font_ab_punct(&q, ',') ||
                        (!mesh_font_ab_key(&q, "echo") && !mesh_font_ab_key(&q, "model") && !mesh_font_ab_key(&q, "history") && !mesh_font_ab_key(&q, "contact")) ||
                        !mesh_font_ab_punct(&q, '=') || (!mesh_font_ab_key(&q, "YES") && !mesh_font_ab_key(&q, "NO")))
                        goto done;
                }
                if (!component_text_done(&q)) goto done;
            } else if (mesh_font_ab_key(&q, "Node")) {
                mode = 3;
                if (!component_text_done(&q)) goto done;
            } else if (mesh_font_ab_key(&q, "Element")) {
                mode = 4;
                if (!mesh_font_ab_punct(&q, ',') || !mesh_font_ab_key(&q, "type") || !mesh_font_ab_punct(&q, '=')) goto done;
                if (mesh_font_ab_key(&q, "C3D4")) arity = 4;
                else if (mesh_font_ab_key(&q, "C3D8")) arity = 8;
                else if (mesh_font_ab_key(&q, "C3D6")) arity = 6;
                else if (mesh_font_ab_key(&q, "S3")) arity = 3;
                else if (mesh_font_ab_key(&q, "S4")) arity = 4;
                else goto done;
                if (!component_text_done(&q)) goto done;
            } else if (mesh_font_ab_key(&q, "Elset") || mesh_font_ab_key(&q, "Nset")) {
                bool el = component_tag(b + q.start + 1, "Elset", 5) || component_tag(b + q.start + 1, "ELSET", 5);
                mode = el ? 5 : 6;
                if (!mesh_font_ab_punct(&q, ',') || !mesh_font_ab_key(&q, el ? "elset" : "nset") || !mesh_font_ab_punct(&q, '=') || !mesh_font_ab_name(&q) ||
                    !component_text_done(&q))
                    goto done;
            } else if (mesh_font_ab_key(&q, "Surface")) {
                mode = 8;
                if (!mesh_font_ab_punct(&q, ',') || !mesh_font_ab_key(&q, "type") || !mesh_font_ab_punct(&q, '=') || !mesh_font_ab_key(&q, "ELEMENT") ||
                    !mesh_font_ab_punct(&q, ',') || !mesh_font_ab_key(&q, "name") || !mesh_font_ab_punct(&q, '=') || !mesh_font_ab_name(&q) || !component_text_done(&q))
                    goto done;
            } else if (mesh_font_ab_key(&q, "System")) {
                mode = 7;
                if (!component_text_done(&q)) goto done;
            } else goto done;
        } else {
            if (++rows > 1000000 || !mode || mode == 2 || mode == 7) goto done;
            if (mode == 1) {
                if (q.stop - q.t > 1024) goto done;
            } else if (mode == 3) {
                if (!mesh_font_ab_csv(&q, &id, true) || id < 1 || !component_id(&nodes, (uint32_t)id, true, pd) || !mesh_font_ab_real(&q, &v, true) ||
                    !mesh_font_ab_real(&q, &v, true) || !mesh_font_ab_real(&q, &v, false) || ++nodecount > 1000000)
                    goto done;
            } else if (mode == 4) {
                if (!mesh_font_ab_csv(&q, &id, true) || id < 1 || !component_id(&elements, (uint32_t)id, true, pd) || ++elementcount > 1000000) goto done;
                for (i = 0; i < arity; ++i)
                    if (!mesh_font_ab_csv(&q, &index, i + 1 < arity) || index < 1 || !component_id(&nodes, (uint32_t)index, false, pd)) goto done;
            } else if (mode == 5 || mode == 6) {
                do {
                    if (!component_text_integer(&q, &index) || index < 1 || !component_id(mode == 5 ? &elements : &nodes, (uint32_t)index, false, pd)) goto done;
                    component_text_space(&q);
                    if (q.t == q.stop) break;
                    if (!mesh_font_ab_punct(&q, ',')) goto done;
                    component_text_space(&q);
                } while (q.t < q.stop);
            } else if (mode == 8) {
                if (!mesh_font_ab_csv(&q, &index, true) || index < 1 || !component_id(&elements, (uint32_t)index, false, pd) || !mesh_font_ab_punct(&q, 'S') ||
                    !component_text_integer(&q, &id) || id < 1 || id > 6 || !component_text_done(&q))
                    goto done;
            }
        }
    }
    if (q.p != q.end || !active || nodecount < 3 || !elementcount || (mode != 1 && mode != 2 && mode != 7 && !rows)) {
        goto done;
    }
    xx_rt_snprintf(label, sizeof(label), "block-%u.inp", block);
    if (!component_emit(f, s, label, start, n - start, n) || !component_cover(f, s, "framing.inp", n)) goto done;
    ok = true;
done:
    if (nodes.values) xx_mem_free(nodes.values);
    if (elements.values) xx_mem_free(elements.values);
    return ok;
}

void xx_abaqus_input_init(xx_abaqus_input *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_ABAQUS_INPUT, "inp");
    }
}
xx_abaqus_input *xx_abaqus_input_create(xx_io_device *d, int64_t at)
{
    xx_abaqus_input *r = (xx_abaqus_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_abaqus_input_init(r, d, at);
    return r;
}
void xx_abaqus_input_destroy(xx_abaqus_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_abaqus_input_free(xx_abaqus_input *r)
{
    if (r) {
        xx_abaqus_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_abaqus_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_abaqus_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
