/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.abinit.org/guide/abinit/ */
#include "xxfclib/formats/abinit_input/xx_abinit_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool abinit_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token row, t[128];
    unsigned n, atoms = 0, types = 0, seen = 0;
    uint64_t x, begin;
    c.b = b;
    while (chemistry_words(&c, &row, t, 128, &n, "#")) {
        unsigned bit;
        if (structure_eq(b, t[0], "natom") || structure_eq(b, t[0], "ntypat")) {
            bool isatoms = structure_eq(b, t[0], "natom");
            bit = isatoms ? 1U : 2U;
            if (n != 2 || (seen & bit) || !scientific_text_uint(b, t[1], &x) || !x || x > (isatoms ? 4090U : 118U)) return false;
            if (isatoms) atoms = (unsigned)x;
            else types = (unsigned)x;
        } else if (structure_eq(b, t[0], "acell")) {
            unsigned j;
            bit = 4;
            if (n != 1 || (seen & bit)) return false;
            begin = chemistry_row_start(b, row);
            if (!chemistry_words(&c, &row, t, 128, &n, "#") || (n != 3 && n != 4) || (n == 4 && !structure_eq(b, t[3], "Angstrom") && !structure_eq(b, t[3], "Bohr")))
                return false;
            for (j = 0; j < 3; ++j)
                if (!structure_positive(b, t[j])) return false;
            if (!blob_add(f, s, b, "cell-scale", begin, c.at - begin)) {
                return false;
            }
            seen |= bit;
            continue;
        } else if (structure_eq(b, t[0], "rprim")) {
            bit = 8;
            if (n != 1 || (seen & bit) || !(seen & 4)) return false;
            begin = chemistry_row_start(b, row);
            if (!chemistry_cell(&c) || !blob_add(f, s, b, "lattice-vectors", begin, c.at - begin)) {
                return false;
            }
            seen |= bit;
            continue;
        } else if (structure_eq(b, t[0], "znucl")) {
            unsigned j, k;
            uint64_t elements[118];
            bit = 16;
            if ((seen & bit) || !types || n != types + 1) return false;
            for (j = 0; j < types; ++j) {
                if (!scientific_text_uint(b, t[j + 1], &x) || !x || x > 118) return false;
                for (k = 0; k < j; ++k)
                    if (elements[k] == x) return false;
                elements[j] = x;
            }
        } else if (structure_eq(b, t[0], "typat")) {
            unsigned count = 0;
            bit = 32;
            if (n != 1 || !atoms || !types || !(seen & 16) || (seen & bit)) return false;
            begin = chemistry_row_start(b, row);
            while (count < atoms) {
                unsigned j;
                if (!chemistry_words(&c, &row, t, 128, &n, "#") || !n || n > atoms - count) return false;
                for (j = 0; j < n; ++j)
                    if (!scientific_text_uint(b, t[j], &x) || !x || x > types) return false;
                count += n;
            }
            if (!blob_add(f, s, b, "atom-type-indices", begin, c.at - begin)) {
                return false;
            }
            seen |= bit;
            continue;
        } else if (structure_eq(b, t[0], "xcart")) {
            unsigned j;
            bit = 64;
            if (n != 1 || !atoms || (seen & 63) != 63 || (seen & bit) || !chemistry_add_row(f, s, b, "coordinate-declaration", row, c.at)) return false;
            for (j = 0; j < atoms; ++j)
                if (!chemistry_words(&c, &row, t, 128, &n, "#") || n != 3 || !structure_floats(b, t, 3) || !chemistry_add_row(f, s, b, "atomic-position", row, c.at))
                    return false;
            seen |= bit;
            continue;
        } else if (structure_eq(b, t[0], "ecut")) {
            bit = 128;
            if ((seen & bit) || (n != 2 && (n != 3 || (!structure_eq(b, t[2], "eV") && !structure_eq(b, t[2], "Ha")))) || !structure_positive(b, t[1])) return false;
        } else if (structure_eq(b, t[0], "ixc")) {
            bit = 256;
            if ((seen & bit) || n != 2 || !scientific_text_uint(b, t[1], &x) || x > 100000) return false;
        } else if (structure_eq(b, t[0], "nsppol")) {
            bit = 512;
            if ((seen & bit) || n != 2 || !scientific_text_uint(b, t[1], &x) || (x != 1 && x != 2)) return false;
        } else if (structure_eq(b, t[0], "chkprim") || structure_eq(b, t[0], "chkexit")) {
            bit = structure_eq(b, t[0], "chkprim") ? 1024U : 2048U;
            if ((seen & bit) || n != 2 || !scientific_text_uint(b, t[1], &x) || x > 1) return false;
        } else return false;
        seen |= bit;
        if (!chemistry_add_row(f, s, b, "input-declaration", row, c.at)) return false;
    }
    return c.at == b->n && (seen & 127) == 127 && !binary_stop(b->pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_ABINIT_INPUT && abinit_input_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_abinit_input_init(xx_abinit_input *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ABINIT_INPUT, "abinit_input");
    }
}
xx_abinit_input *xx_abinit_input_create(xx_io_device *d, int64_t b)
{
    xx_abinit_input *r = (xx_abinit_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_abinit_input_init(r, d, b);
    return r;
}
void xx_abinit_input_destroy(xx_abinit_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_abinit_input_free(xx_abinit_input *r)
{
    if (r) {
        xx_abinit_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_abinit_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_abinit_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
