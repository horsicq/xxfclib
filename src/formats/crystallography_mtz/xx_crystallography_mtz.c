/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/project-gemmi/gemmi/master/include/gemmi/mtz.hpp
 * CCP4 MTZ little-endian reflection tables: complete counted finite/missing-value columns and typed80-byte footer records, local datasets/columns/cell/symmetry metadata
 * and exact EOF. Original descriptor, reflection table and header records exported; batch blocks, big-endian and unknown extensions declined. Bounded32MiB input,4096
 * components and bounded work.
 */
#include "xxfclib/formats/crystallography_mtz/xx_crystallography_mtz.h"
#include "../common/xx_component_text.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[12];
    return n >= 160 && pm_read(f, 0, b, 12) && component_tag(b, "MTZ ", 4) && b[8] == 0x44 && b[9] == 0x41 && b[10] == 0 && b[11] == 0;
}
typedef struct scene_bitmap_column {
    char name[64];
    uint8_t type;
    int32_t dataset;
    bool source;
} scene_bitmap_column;
static bool scene_bitmap_mtoken(component_text_cursor *q, char *out, size_t z)
{
    size_t k = 0;
    component_text_space(q);
    while (q->t < q->stop && q->b[q->t] != 32 && q->b[q->t] != 9) {
        uint8_t c = q->b[q->t++];
        if (k + 1 >= z || c < 33 || c > 126) return false;
        out[k++] = (char)c;
    }
    out[k] = 0;
    return k != 0;
}
static bool scene_bitmap_mcell(component_text_cursor *q)
{
    double v;
    unsigned i;
    for (i = 0; i < 6; ++i)
        if (!component_text_number_36_digits(q, &v) || v <= 0 || (i >= 3 && v >= 180)) return false;
    return component_text_done(q);
}
static bool scene_bitmap_sym(component_text_cursor *q)
{
    unsigned axis;
    for (axis = 0; axis < 3; ++axis) {
        unsigned terms = 0, variables = 0;
        while (q->t < q->stop) {
            uint8_t c;
            component_text_space(q);
            if (q->t == q->stop) break;
            c = q->b[q->t];
            if (c == '+' || c == '-') {
                ++q->t;
                component_text_space(q);
            } else if (terms) return false;
            if (q->t == q->stop || ++terms > 8) return false;
            c = q->b[q->t++];
            if (c == 'X' || c == 'Y' || c == 'Z') {
                unsigned bit = 1U << (c - 'X');
                if (variables & bit) return false;
                variables |= bit;
            } else if (c >= '0' && c <= '9') {
                unsigned num = c - '0', den = 1;
                while (q->t < q->stop && q->b[q->t] >= '0' && q->b[q->t] <= '9') {
                    num = num * 10 + q->b[q->t++] - '0';
                    if (num > 96) return false;
                }
                if (q->t < q->stop && q->b[q->t] == '/') {
                    ++q->t;
                    den = 0;
                    while (q->t < q->stop && q->b[q->t] >= '0' && q->b[q->t] <= '9') {
                        den = den * 10 + q->b[q->t++] - '0';
                        if (den > 48) return false;
                    }
                    if (!den) return false;
                }
            } else return false;
            component_text_space(q);
            if (q->t == q->stop || q->b[q->t] == ',') break;
        }
        if (!terms || !variables) return false;
        if (axis < 2) {
            if (q->t == q->stop || q->b[q->t++] != ',') return false;
        } else if (!component_text_done(q)) return false;
    }
    return true;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t start, p, data;
    scene_bitmap_column *cols = NULL;
    uint8_t datasets[128];
    uint32_t nc = 0, nref = 0, ncols = 0, sym = 0, expected_sym = 0, ndif = 0, history = 0, flags = 0;
    bool missing = false, ended = false, final = false, ok = false;
    char name[128], label[48];
    unsigned records = 0, i;
    xx_mem_zero(datasets, sizeof(datasets));
    if (n < 160 || !component_tag(b, "MTZ ", 4) || b[8] != 0x44 || b[9] != 0x41 || b[10] || b[11] || xx_data_get_u32(b + 4, 4, 0, false) < 21 ||
        !component_zero(b + 12, 68))
        return false;
    start = ((uint64_t)xx_data_get_u32(b + 4, 4, 0, false) - 1) * 4;
    if (start < 80 || start >= n || (n - start) % 80 || (n - start) / 80 > 4000) return false;
    data = start - 80;
    cols = (scene_bitmap_column *)xx_mem_alloc(sizeof(*cols) * 512);
    if (!cols) return false;
    xx_mem_zero(cols, sizeof(*cols) * 512);
    if (!component_emit(f, s, "descriptor.mtz", 0, 80, n) || !component_emit(f, s, "reflections.f32le", 80, data, n)) goto done;
    for (p = start; p < n; p += 80) {
        component_text_cursor q = {b, p, p + 80, p, p + 80, p};
        double v, w;
        int32_t a, c;
        unsigned field;
        if (xx_component_parser_stopped(pd) || !component_utf8(b + p, 80, true, pd)) goto done;
        if (final) goto done;
        if (history) {
            --history;
        } else if (ended) {
            if (component_text_word(&q, "MTZHIST")) {
                if (!component_text_integer_delimited(&q, &a) || a < 0 || a > 1000 || !component_text_done(&q)) goto done;
                history = (uint32_t)a;
            } else if (component_text_word(&q, "MTZENDOFHEADERS") && component_text_done(&q)) {
                final = true;
            } else goto done;
        } else if (component_text_word(&q, "VERS")) {
            if ((flags & 1) || !component_text_word(&q, "MTZ:V1.1") || !component_text_done(&q)) goto done;
            flags |= 1;
        } else if (component_text_word(&q, "TITLE")) {
            if (flags & 2) goto done;
            flags |= 2;
        } else if (component_text_word(&q, "NCOL")) {
            if ((flags & 4) || !component_text_integer_delimited(&q, &a) || a < 1 || a > 512) goto done;
            nc = (uint32_t)a;
            if (!component_text_integer_delimited(&q, &a) || a < 1 || a > 1000000) goto done;
            nref = (uint32_t)a;
            if (!component_text_integer_delimited(&q, &a) || a || !component_text_done(&q) || data != (uint64_t)nc * nref * 4) goto done;
            flags |= 4;
        } else if (component_text_word(&q, "CELL")) {
            if ((flags & 8) || !scene_bitmap_mcell(&q)) goto done;
            flags |= 8;
        } else if (component_text_word(&q, "SORT")) {
            if (flags & 16) goto done;
            for (i = 0; i < 5; ++i)
                if (!component_text_integer_delimited(&q, &a) || a < 0 || a > (int32_t)nc) goto done;
            if (!component_text_done(&q)) goto done;
            flags |= 16;
        } else if (component_text_word(&q, "SYMINF")) {
            uint64_t at, z;
            if ((flags & 32) || !component_text_integer_delimited(&q, &a) || a < 1 || a > 192) goto done;
            expected_sym = (uint32_t)a;
            if (!component_text_integer_delimited(&q, &c) || c < 1 || c > a || !scene_bitmap_mtoken(&q, name, sizeof(name)) || xx_rt_strlen(name) != 1 ||
                !xx_rt_strchr("PABCIFR", name[0]) || !component_text_integer_delimited(&q, &a) || a < 1 || a > 230)
                goto done;
            component_text_space(&q);
            at = q.t;
            if (at == q.stop || b[q.t++] != '\'') goto done;
            z = q.t;
            while (q.t < q.stop && b[q.t] != '\'') ++q.t;
            if (q.t == q.stop || q.t - z < 1 || q.t - z > 40) goto done;
            ++q.t;
            if (!scene_bitmap_mtoken(&q, name, sizeof(name)) || !component_text_done(&q)) goto done;
            flags |= 32;
        } else if (component_text_word(&q, "SYMM")) {
            if (!(flags & 32) || ++sym > expected_sym || !scene_bitmap_sym(&q)) goto done;
        } else if (component_text_word(&q, "RESO")) {
            if ((flags & 64) || !component_text_number_36_digits(&q, &v) || !component_text_number_36_digits(&q, &w) || v <= 0 || w < v || !component_text_done(&q))
                goto done;
            flags |= 64;
        } else if (component_text_word(&q, "VALM")) {
            if (flags & 128) goto done;
            if (component_text_word(&q, "NAN")) missing = true;
            else if (!component_text_number_36_digits(&q, &v)) goto done;
            if (!component_text_done(&q)) goto done;
            flags |= 128;
        } else if (component_text_word(&q, "COLUMN")) {
            char type[8];
            if (!(flags & 4) || ncols >= nc || !scene_bitmap_mtoken(&q, cols[ncols].name, 64) || !scene_bitmap_mtoken(&q, type, sizeof(type)) ||
                xx_rt_strlen(type) != 1 || !xx_rt_strchr("HFJDQGLKMEPWABYIR", type[0]) || !component_text_number_36_digits(&q, &v) ||
                !component_text_number_36_digits(&q, &w) || w < v || !component_text_integer_delimited(&q, &a) || a < 0 || a >= 128 || !component_text_done(&q))
                goto done;
            for (i = 0; i < ncols; ++i)
                if (cols[i].dataset == a && !xx_rt_strcmp(cols[i].name, cols[ncols].name)) goto done;
            cols[ncols].dataset = a;
            cols[ncols++].type = (uint8_t)type[0];
        } else if (component_text_word(&q, "COLSRC")) {
            if (!scene_bitmap_mtoken(&q, name, sizeof(name))) goto done;
            {
                char date[64];
                if (!scene_bitmap_mtoken(&q, date, sizeof(date)) || !component_text_integer_delimited(&q, &a) || !component_text_done(&q)) goto done;
            }
            for (i = 0; i < ncols; ++i)
                if (cols[i].dataset == a && !xx_rt_strcmp(cols[i].name, name)) break;
            if (i == ncols || cols[i].source) goto done;
            cols[i].source = true;
        } else if (component_text_word(&q, "NDIF")) {
            if ((flags & 256) || !component_text_integer_delimited(&q, &a) || a < 1 || a > 128 || !component_text_done(&q)) goto done;
            ndif = (uint32_t)a;
            flags |= 256;
        } else {
            component_text_cursor copy = q;
            field = 0;
            if (component_text_word(&q, "PROJECT")) field = 1;
            else if (component_text_word(&q, "CRYSTAL")) field = 2;
            else if (component_text_word(&q, "DATASET")) field = 4;
            else if (component_text_word(&q, "DCELL")) field = 8;
            else if (component_text_word(&q, "DWAVEL")) field = 16;
            if (field) {
                if (!(flags & 256) || !component_text_integer_delimited(&q, &a) || a < 0 || a >= 128 || (datasets[a] & field)) goto done;
                if (field == 8) {
                    if (!scene_bitmap_mcell(&q)) goto done;
                } else if (field == 16) {
                    if (!component_text_number_36_digits(&q, &v) || v < 0 || !component_text_done(&q)) goto done;
                } else if (!scene_bitmap_mtoken(&q, name, sizeof(name)) || !component_text_done(&q)) goto done;
                datasets[a] |= (uint8_t)field;
            } else {
                q = copy;
                if (!component_text_word(&q, "END") || !component_text_done(&q)) goto done;
                ended = true;
            }
        }
        xx_rt_snprintf(label, sizeof(label), "header-record-%u.mtz", records++);
        if (!component_emit(f, s, label, p, 80, n)) goto done;
    }
    if (!final || history || flags != 511 || ncols != nc || sym != expected_sym) {
        goto done;
    }
    {
        unsigned found = 0;
        for (i = 0; i < 128; ++i)
            if (datasets[i]) {
                if (datasets[i] != 31) goto done;
                ++found;
            }
        if (found != ndif) goto done;
    }
    for (i = 0; i < ncols; ++i)
        if (datasets[cols[i].dataset] != 31) goto done;
    for (p = 80; p < start; p += 4) {
        uint32_t u = xx_data_get_u32(b + p, 4, 0, false), column = (uint32_t)((p - 80) / 4 % nc);
        if (((p & 4095) == 0 && xx_component_parser_stopped(pd))) goto done;
        if ((u & 0x7f800000U) == 0x7f800000U) {
            if (!missing || !(u & 0x007fffffU)) goto done;
        } else if (cols[column].type == 'H') {
            union {
                uint32_t u;
                float f;
            } val;
            val.u = u;
            if (val.f < -32768 || val.f > 32767 || val.f != (float)(int32_t)val.f) goto done;
        }
    }
    ok = true;
done:
    if (cols) xx_mem_free(cols);
    return ok;
}

void xx_crystallography_mtz_init(xx_crystallography_mtz *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_CRYSTALLOGRAPHY_MTZ, "mtz");
    }
}
xx_crystallography_mtz *xx_crystallography_mtz_create(xx_io_device *d, int64_t at)
{
    xx_crystallography_mtz *r = (xx_crystallography_mtz *)xx_mem_alloc(sizeof(*r));
    if (r) xx_crystallography_mtz_init(r, d, at);
    return r;
}
void xx_crystallography_mtz_destroy(xx_crystallography_mtz *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_crystallography_mtz_free(xx_crystallography_mtz *r)
{
    if (r) {
        xx_crystallography_mtz_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_crystallography_mtz_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_crystallography_mtz_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
