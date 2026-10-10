/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/main/src/OpenColorIO/fileformats/FileFormatCSP.cpp */
#include "xxfclib/formats/lut_cinespace_csp/xx_lut_cinespace_csp.h"
#include "../common/xx_scientific_tables_traces.h"

#define LUT_CINESPACE_CSP_NEED(x) \
    do {                          \
        if (!(x)) goto done;      \
    } while (0)

static bool lut_cinespace_csp_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token line, *t = NULL;
    unsigned nt, axis;
    uint64_t n, i, total, start;
    bool dim3, ok = false;
    c.b = b;
    LUT_CINESPACE_CSP_NEED(structure_ascii(b));
    t = (scientific_text_token *)xx_mem_alloc(4096 * sizeof(*t));
    LUT_CINESPACE_CSP_NEED(t);
    LUT_CINESPACE_CSP_NEED(scientific_table_words(&c, &line, t, 4096, &nt) && nt == 1 && scientific_text_eq(b, t[0], "CSPLUTV100"));
    LUT_CINESPACE_CSP_NEED(scientific_table_words(&c, &line, t, 4096, &nt) && nt == 1 && (scientific_text_eq(b, t[0], "1D") || scientific_text_eq(b, t[0], "3D")));
    dim3 = scientific_text_eq(b, t[0], "3D");
    start = c.at;
    LUT_CINESPACE_CSP_NEED(scientific_table_words(&c, &line, t, 4096, &nt));
    if (nt == 1 && scientific_text_eq(b, t[0], "BEGIN METADATA")) goto done;
    if (nt == 2 && scientific_text_eq(b, t[0], "BEGIN") && scientific_text_eq(b, t[1], "METADATA")) {
        bool ended = false;
        unsigned lines = 0;
        while (c.at < b->n) {
            LUT_CINESPACE_CSP_NEED(scientific_text_line(&c, &line) && ++lines <= 1024 && c.at - start <= 65536);
            line = scientific_text_trim(b, line);
            if (scientific_text_eq(b, line, "END METADATA")) {
                ended = true;
                break;
            }
        }
        LUT_CINESPACE_CSP_NEED(ended && scientific_table_words(&c, &line, t, 4096, &nt));
    }
    LUT_CINESPACE_CSP_NEED(blob_add(f, s, b, "lut-header", 0, line.at));
    for (axis = 0; axis < 3; ++axis) {
        double previous = 0;
        start = line.at;
        LUT_CINESPACE_CSP_NEED(nt == 1 && scientific_text_uint(b, t[0], &n) && n >= 2 && n <= 4096);
        LUT_CINESPACE_CSP_NEED(scientific_table_words(&c, &line, t, 4096, &nt) && nt == n);
        for (i = 0; i < n; ++i) {
            double x;
            LUT_CINESPACE_CSP_NEED(scientific_text_float(b, t[i]));
            x = molecular_value(b, t[i]);
            LUT_CINESPACE_CSP_NEED(!i || x > previous);
            previous = x;
        }
        LUT_CINESPACE_CSP_NEED(scientific_table_words(&c, &line, t, 4096, &nt) && nt == n && structure_floats(b, t, nt) &&
                               blob_add(f, s, b, "prelut-channel", start, c.at - start));
        LUT_CINESPACE_CSP_NEED(scientific_table_words(&c, &line, t, 4096, &nt));
    }
    if (dim3) {
        uint64_t a, z;
        LUT_CINESPACE_CSP_NEED(nt == 3 && scientific_text_uint(b, t[0], &a) && scientific_text_uint(b, t[1], &n) && scientific_text_uint(b, t[2], &z) && a && n && z &&
                               a <= 256 && n <= 256 && z <= 256);
        total = a * n * z;
    } else LUT_CINESPACE_CSP_NEED(nt == 1 && scientific_text_uint(b, t[0], &total));
    LUT_CINESPACE_CSP_NEED(total && total <= 262144);
    start = line.at;
    for (i = 0; i < total; ++i) LUT_CINESPACE_CSP_NEED(scientific_table_words(&c, &line, t, 4096, &nt) && nt == 3 && structure_floats(b, t, 3));
    LUT_CINESPACE_CSP_NEED(blob_add(f, s, b, "grid-values", start, c.at - start) && scientific_table_finish(&c));
    ok = true;
done:
    if (t) xx_mem_free(t);
    return ok;
}

#undef LUT_CINESPACE_CSP_NEED

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_LUT_CINESPACE_CSP && lut_cinespace_csp_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_lut_cinespace_csp_init(xx_lut_cinespace_csp *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LUT_CINESPACE_CSP, "lut_cinespace_csp");
    }
}
xx_lut_cinespace_csp *xx_lut_cinespace_csp_create(xx_io_device *d, int64_t b)
{
    xx_lut_cinespace_csp *r = (xx_lut_cinespace_csp *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lut_cinespace_csp_init(r, d, b);
    return r;
}
void xx_lut_cinespace_csp_destroy(xx_lut_cinespace_csp *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lut_cinespace_csp_free(xx_lut_cinespace_csp *r)
{
    if (r) {
        xx_lut_cinespace_csp_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lut_cinespace_csp_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lut_cinespace_csp_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
