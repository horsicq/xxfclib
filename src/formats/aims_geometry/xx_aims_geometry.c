/* SPDX-License-Identifier: MIT
 * Independently implemented from https://fhi-aims.org/uploads/documents/FHI-aims.250320.pdf */
#include "xxfclib/formats/aims_geometry/xx_aims_geometry.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool aims_geometry_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token row, t[8], cell[3][3];
    unsigned n, atoms = 0, vectors = 0, mode = 0;
    bool constrained = false, spin = false, charge = false;
    c.b = b;
    while (chemistry_words(&c, &row, t, 8, &n, "#")) {
        const char *label;
        if (structure_eq(b, t[0], "lattice_vector")) {
            unsigned j;
            if (atoms || vectors == 3 || n != 4 || !structure_floats(b, t + 1, 3)) return false;
            for (j = 0; j < 3; ++j) {
                cell[vectors][j] = t[j + 1];
            }
            ++vectors;
            label = "lattice-vector";
        } else if (structure_eq(b, t[0], "atom") || structure_eq(b, t[0], "atom_frac")) {
            unsigned m = structure_eq(b, t[0], "atom_frac") ? 2U : 1U;
            if (n != 5 || ++atoms > 4090 || (mode && mode != m) || (m == 2 && vectors != 3) || !chemistry_atom(b, t, n, 4, 1)) return false;
            mode = m;
            constrained = spin = charge = false;
            label = "atomic-position";
        } else if (structure_eq(b, t[0], "constrain_relaxation")) {
            if (!atoms || constrained || n != 2 ||
                (!structure_eq(b, t[1], ".true.") && !structure_eq(b, t[1], "x") && !structure_eq(b, t[1], "y") && !structure_eq(b, t[1], "z")))
                return false;
            constrained = true;
            label = "atom-constraint";
        } else if (structure_eq(b, t[0], "initial_moment") || structure_eq(b, t[0], "initial_charge")) {
            bool isspin = structure_eq(b, t[0], "initial_moment");
            if (!atoms || n != 2 || !scientific_text_float(b, t[1]) || (isspin ? spin : charge)) return false;
            if (isspin) spin = true;
            else charge = true;
            label = isspin ? "atom-spin" : "atom-charge";
        } else return false;
        if (!chemistry_add_row(f, s, b, label, row, c.at)) return false;
    }
    return c.at == b->n && atoms && (!vectors || (vectors == 3 && chemistry_cell_valid(b, cell))) && !binary_stop(b->pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_AIMS_GEOMETRY && aims_geometry_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_aims_geometry_init(xx_aims_geometry *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AIMS_GEOMETRY, "aims_geometry");
    }
}
xx_aims_geometry *xx_aims_geometry_create(xx_io_device *d, int64_t b)
{
    xx_aims_geometry *r = (xx_aims_geometry *)xx_mem_alloc(sizeof(*r));
    if (r) xx_aims_geometry_init(r, d, b);
    return r;
}
void xx_aims_geometry_destroy(xx_aims_geometry *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_aims_geometry_free(xx_aims_geometry *r)
{
    if (r) {
        xx_aims_geometry_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_aims_geometry_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_aims_geometry_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
