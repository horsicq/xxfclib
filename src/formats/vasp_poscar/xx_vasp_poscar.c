/* SPDX-License-Identifier: MIT
 * Independently implemented from https://vasp.at/wiki/POSCAR */
#include "xxfclib/formats/vasp_poscar/xx_vasp_poscar.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool vasp_poscar_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    scientific_text_lines c = {0};
    scientific_text_token row, t[128], species[128];
    unsigned n, k, ns, count = 0;
    uint64_t v, coords;
    bool selective = false;
    c.b = b;
    if (!chemistry_line(&c, &row) || !scientific_text_trim(b, row).n || !chemistry_words(&c, &row, t, 128, &n, "#") ||
        n != 1 || !structure_positive(b, t[0]) || !chemistry_cell(&c))
        return false;
    if (!chemistry_words(&c, &row, t, 128, &ns, "#") || !ns || ns > 118)
        return false;
    for (k = 0; k < ns; ++k) {
        unsigned j;
        if (!chemistry_atomic_number(b, t[k]))
            return false;
        for (j = 0; j < k; ++j)
            if (structure_same(b, t[k], species[j]))
                return false;
        species[k] = t[k];
    }
    if (!chemistry_words(&c, &row, t, 128, &n, "#") || n != ns)
        return false;
    for (k = 0; k < n; ++k) {
        if (!scientific_text_uint(b, t[k], &v) || !v || v > 4090 - count)
            return false;
        count += (unsigned)v;
    }
    if (!chemistry_words(&c, &row, t, 128, &n, "#") || !n)
        return false;
    if (structure_eq(b, t[0], "Selective")) {
        if (n > 2 || (n == 2 && !structure_eq(b, t[1], "dynamics")))
            return false;
        selective = true;
        if (!chemistry_words(&c, &row, t, 128, &n, "#") || n != 1)
            return false;
    } else if (n != 1)
        return false;
    if (!structure_eq(b, t[0], "Direct") && !structure_eq(b, t[0], "Cartesian")) {
        return false;
    }
    coords = c.at;
    if (!blob_add(f, s, b, "structure-header", 0, coords))
        return false;
    for (k = 0; k < count; ++k) {
        if (!chemistry_words(&c, &row, t, 128, &n, "#") || n != (selective ? 6U : 3U) || !structure_floats(b, t, 3))
            return false;
        if (selective) {
            unsigned j;
            for (j = 3; j < 6; ++j)
                if (!structure_eq(b, t[j], "T") && !structure_eq(b, t[j], "F"))
                    return false;
        }
        if (!chemistry_add_row(f, s, b, "atomic-position", row, c.at))
            return false;
    }
    return chemistry_finish(&c, "#");
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_VASP_POSCAR && vasp_poscar_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_vasp_poscar_init(xx_vasp_poscar *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_VASP_POSCAR, "vasp_poscar");
    }
}
xx_vasp_poscar *xx_vasp_poscar_create(xx_io_device *d, int64_t b) {
    xx_vasp_poscar *r = (xx_vasp_poscar *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_vasp_poscar_init(r, d, b);
    return r;
}
void xx_vasp_poscar_destroy(xx_vasp_poscar *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_vasp_poscar_free(xx_vasp_poscar *r) {
    if (r) {
        xx_vasp_poscar_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_vasp_poscar_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_vasp_poscar_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
