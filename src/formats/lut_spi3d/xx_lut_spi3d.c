/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/main/src/OpenColorIO/fileformats/FileFormatSpi3D.cpp */
#include "xxfclib/formats/lut_spi3d/xx_lut_spi3d.h"
#include "../common/xx_scientific_tables_traces.h"

static bool lut_spi3d_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    scientific_text_lines c = {0};
    scientific_text_token line, t[6];
    unsigned nt;
    uint64_t n[3], i, total, start, slab;
    c.b = b;
    if (!structure_ascii(b) || !scientific_table_words(&c, &line, t, 6, &nt) || nt != 2 ||
        !scientific_text_eq(b, t[0], "SPILUT") || !scientific_text_eq(b, t[1], "1.0"))
        return false;
    if (!scientific_table_words(&c, &line, t, 6, &nt) || nt != 2 || !scientific_text_eq(b, t[0], "3") ||
        !scientific_text_eq(b, t[1], "3"))
        return false;
    if (!scientific_table_words(&c, &line, t, 6, &nt) || nt != 3)
        return false;
    for (i = 0; i < 3; ++i)
        if (!scientific_text_uint(b, t[i], &n[i]) || !n[i] || n[i] > 256)
            return false;
    total = n[0] * n[1] * n[2];
    if (total > 262144 || !blob_add(f, s, b, "lut-header", 0, c.at))
        return false;
    slab = n[1] * n[2];
    start = c.at;
    for (i = 0; i < total; ++i) {
        uint64_t x, y, z;
        if (!scientific_table_words(&c, &line, t, 6, &nt) || nt != 6 || !scientific_text_uint(b, t[0], &x) ||
            !scientific_text_uint(b, t[1], &y) || !scientific_text_uint(b, t[2], &z) || x != i / slab ||
            y != (i / n[2]) % n[1] || z != i % n[2] || !structure_floats(b, t + 3, 3))
            return false;
        if ((i + 1) % slab == 0) {
            if (!blob_add(f, s, b, "red-axis-slab", start, c.at - start))
                return false;
            start = c.at;
        }
    }
    return scientific_table_finish(&c);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_LUT_SPI3D && lut_spi3d_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_lut_spi3d_init(xx_lut_spi3d *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LUT_SPI3D, "lut_spi3d");
    }
}
xx_lut_spi3d *xx_lut_spi3d_create(xx_io_device *d, int64_t b) {
    xx_lut_spi3d *r = (xx_lut_spi3d *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_lut_spi3d_init(r, d, b);
    return r;
}
void xx_lut_spi3d_destroy(xx_lut_spi3d *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lut_spi3d_free(xx_lut_spi3d *r) {
    if (r) {
        xx_lut_spi3d_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lut_spi3d_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_lut_spi3d_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
