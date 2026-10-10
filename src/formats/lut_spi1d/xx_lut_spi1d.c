/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/main/src/OpenColorIO/fileformats/FileFormatSpi1D.cpp */
#include "xxfclib/formats/lut_spi1d/xx_lut_spi1d.h"
#include "../common/xx_scientific_tables_traces.h"

static bool lut_spi1d_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    scientific_text_lines c = {0};
    scientific_text_token line, t[4];
    unsigned nt, components;
    uint64_t n, i, start;
    c.b = b;
    if (!structure_ascii(b) || !scientific_table_words(&c, &line, t, 4, &nt) || nt != 2 ||
        !scientific_text_eq(b, t[0], "Version") || !scientific_text_eq(b, t[1], "1"))
        return false;
    if (!scientific_table_words(&c, &line, t, 4, &nt) || nt != 3 || !scientific_text_eq(b, t[0], "From") ||
        !scientific_text_float(b, t[1]) || !scientific_text_float(b, t[2]) ||
        molecular_value(b, t[1]) >= molecular_value(b, t[2]))
        return false;
    if (!scientific_table_words(&c, &line, t, 4, &nt) || nt != 2 || !scientific_text_eq(b, t[0], "Length") ||
        !scientific_text_uint(b, t[1], &n) || !n || n > 262144)
        return false;
    if (!scientific_table_words(&c, &line, t, 4, &nt) || nt != 2 || !scientific_text_eq(b, t[0], "Components") ||
        !scientific_text_uint(b, t[1], &i) || !i || i > 3) {
        return false;
    }
    components = (unsigned)i;
    if (!scientific_table_words(&c, &line, t, 4, &nt) || nt != 1 || !scientific_text_eq(b, t[0], "{")) {
        return false;
    }
    start = c.at;
    if (!blob_add(f, s, b, "lut-header", 0, start))
        return false;
    for (i = 0; i < n; ++i)
        if (!scientific_table_words(&c, &line, t, 4, &nt) || nt != components || !structure_floats(b, t, nt))
            return false;
    if (!blob_add(f, s, b, "curve-values", start, c.at - start) || !scientific_table_words(&c, &line, t, 4, &nt) ||
        nt != 1 || !scientific_text_eq(b, t[0], "}") || !scientific_table_finish(&c))
        return false;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_LUT_SPI1D && lut_spi1d_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_lut_spi1d_init(xx_lut_spi1d *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LUT_SPI1D, "lut_spi1d");
    }
}
xx_lut_spi1d *xx_lut_spi1d_create(xx_io_device *d, int64_t b) {
    xx_lut_spi1d *r = (xx_lut_spi1d *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_lut_spi1d_init(r, d, b);
    return r;
}
void xx_lut_spi1d_destroy(xx_lut_spi1d *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lut_spi1d_free(xx_lut_spi1d *r) {
    if (r) {
        xx_lut_spi1d_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lut_spi1d_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_lut_spi1d_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
