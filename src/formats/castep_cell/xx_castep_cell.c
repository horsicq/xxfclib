/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/io/castep.html */
#include "xxfclib/formats/castep_cell/xx_castep_cell.h"
#include "../common/xx_scientific_structure.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[12], kind, cell[3][3];
    unsigned nt, blocks = 0, rows = 0;
    uint64_t start, atoms = 0;
    bool lattice = false, positions = false, ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    while (c.at < b.n) {
        start = c.at;
        if (!structure_words(&c, &line, t, 12, &nt, "#!")) {
            BLOB_NEED(c.at == b.n);
            break;
        }
        BLOB_NEED(nt == 2 && structure_eq(&b, t[0], "%BLOCK") && ++blocks <= 2);
        kind = t[1];
        rows = 0;
        if (structure_eq(&b, kind, "LATTICE_CART")) {
            BLOB_NEED(!lattice);
            lattice = true;
        } else {
            BLOB_NEED(!positions && (structure_eq(&b, kind, "POSITIONS_ABS") || structure_eq(&b, kind, "POSITIONS_FRAC")));
            positions = true;
        }
        for (;;) {
            BLOB_NEED(structure_words(&c, &line, t, 12, &nt, "#!"));
            if (structure_eq(&b, t[0], "%ENDBLOCK")) {
                BLOB_NEED(nt == 2 && (structure_eq(&b, kind, "LATTICE_CART")    ? structure_eq(&b, t[1], "LATTICE_CART")
                                      : structure_eq(&b, kind, "POSITIONS_ABS") ? structure_eq(&b, t[1], "POSITIONS_ABS")
                                                                                : structure_eq(&b, t[1], "POSITIONS_FRAC")));
                break;
            }
            if (!rows && nt == 1 && (structure_eq(&b, t[0], "ang") || structure_eq(&b, t[0], "bohr"))) {
                BLOB_NEED(!structure_eq(&b, kind, "POSITIONS_FRAC"));
                BLOB_NEED(structure_words(&c, &line, t, 12, &nt, "#!"));
            }
            if (structure_eq(&b, kind, "LATTICE_CART")) {
                BLOB_NEED(nt == 3 && rows < 3 && structure_floats(&b, t, 3));
                xx_rt_memcpy(cell[rows], t, 3 * sizeof(scientific_text_token));
            } else {
                BLOB_NEED(nt == 4 && molecular_element(&b, t[0]) && structure_floats(&b, t + 1, 3) && ++atoms <= 100000);
            }
            ++rows;
        }
        BLOB_NEED(rows && (!structure_eq(&b, kind, "LATTICE_CART") || (rows == 3 && structure_cell(&b, cell))));
        BLOB_NEED(blob_add(f, s, &b, structure_eq(&b, kind, "LATTICE_CART") ? "lattice-block" : "positions-block", start, c.at - start));
    }
    BLOB_NEED(lattice && positions && atoms && blocks == 2);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_castep_cell_init(xx_castep_cell *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_CASTEP_CELL, "castep_cell");
    }
}
xx_castep_cell *xx_castep_cell_create(xx_io_device *d, int64_t b)
{
    xx_castep_cell *r = (xx_castep_cell *)xx_mem_alloc(sizeof(*r));
    if (r) xx_castep_cell_init(r, d, b);
    return r;
}
void xx_castep_cell_destroy(xx_castep_cell *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_castep_cell_free(xx_castep_cell *r)
{
    if (r) {
        xx_castep_cell_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_castep_cell_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_castep_cell_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
