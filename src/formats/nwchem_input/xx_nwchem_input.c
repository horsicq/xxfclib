/* SPDX-License-Identifier: MIT
 * Independently implemented from https://nwchemgit.github.io/Input-and-Output.html */
#include "xxfclib/formats/nwchem_input/xx_nwchem_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool nwchem_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token row, t[16];
    unsigned n, geometry = 0, basis = 0, scf = 0, dft = 0, atoms = 0;
    uint64_t begin, x;
    c.b = b;
    while (chemistry_words(&c, &row, t, 16, &n, "#")) {
        if (structure_eq(b, t[0], "geometry")) {
            bool crystal = false;
            if (geometry++ || atoms || n > 8) return false;
            {
                unsigned j, seen = 0;
                for (j = 1; j < n; ++j) {
                    unsigned bit;
                    if (structure_eq(b, t[j], "units")) {
                        bit = 1;
                        if (++j == n || (!structure_eq(b, t[j], "angstrom") && !structure_eq(b, t[j], "au"))) return false;
                    } else if (structure_eq(b, t[j], "nocenter")) bit = 2;
                    else if (structure_eq(b, t[j], "noautosym")) bit = 4;
                    else if (structure_eq(b, t[j], "noautoz")) bit = 8;
                    else {
                        return false;
                    }
                    if (seen & bit) return false;
                    seen |= bit;
                }
            }
            begin = chemistry_row_start(b, row);
            while (chemistry_words(&c, &row, t, 16, &n, "#")) {
                if (n == 1 && structure_eq(b, t[0], "end")) break;
                if (structure_eq(b, t[0], "system")) {
                    if (crystal || atoms || n != 4 || !structure_eq(b, t[1], "crystal") || !structure_eq(b, t[2], "units") || !structure_eq(b, t[3], "angstrom"))
                        return false;
                    crystal = true;
                    if (!chemistry_words(&c, &row, t, 16, &n, "#") || n != 1 || !structure_eq(b, t[0], "lattice_vectors") || !chemistry_cell(&c) ||
                        !chemistry_words(&c, &row, t, 16, &n, "#") || n != 1 || !structure_eq(b, t[0], "end"))
                        return false;
                } else if (n != 4 || ++atoms > 4090 || !chemistry_atom(b, t, n, 0, 1) || !chemistry_add_row(f, s, b, "atomic-position", row, c.at)) return false;
            }
            if (!atoms || n != 1 || !structure_eq(b, t[0], "end") || !blob_add(f, s, b, "geometry-section", begin, c.at - begin)) return false;
        } else if (structure_eq(b, t[0], "basis")) {
            if (basis++ || (n != 1 && (n != 2 || !structure_eq(b, t[1], "noprint")))) {
                return false;
            }
            begin = chemistry_row_start(b, row);
            if (!chemistry_words(&c, &row, t, 16, &n, "#") || n != 3 || !scientific_text_eq(b, t[0], "*") || !structure_eq(b, t[1], "library") ||
                !scientific_text_ident(b, t[2]) || !chemistry_words(&c, &row, t, 16, &n, "#") || n != 1 || !structure_eq(b, t[0], "end") ||
                !blob_add(f, s, b, "basis-section", begin, c.at - begin))
                return false;
        } else if (structure_eq(b, t[0], "scf") || structure_eq(b, t[0], "dft")) {
            bool isscf = structure_eq(b, t[0], "scf");
            unsigned seen = 0;
            if (n != 1 || (isscf ? scf++ : dft++)) {
                return false;
            }
            begin = chemistry_row_start(b, row);
            while (chemistry_words(&c, &row, t, 16, &n, "#")) {
                unsigned bit;
                if (n == 1 && structure_eq(b, t[0], "end")) break;
                if (n != 2) return false;
                if (isscf && structure_eq(b, t[0], "nopen")) {
                    if (!scientific_text_uint(b, t[1], &x) || x > 1000) return false;
                    bit = 1;
                } else if (!isscf && structure_eq(b, t[0], "xc")) {
                    if (!chemistry_identifier(b, t[1])) return false;
                    bit = 2;
                } else if (!isscf && (structure_eq(b, t[0], "mult") || structure_eq(b, t[0], "maxiter"))) {
                    if (!scientific_text_uint(b, t[1], &x) || !x || x > 100000) return false;
                    bit = structure_eq(b, t[0], "mult") ? 4U : 8U;
                } else if (isscf && structure_eq(b, t[0], "thresh")) {
                    if (!structure_positive(b, t[1])) return false;
                    bit = 16;
                } else {
                    return false;
                }
                if (seen & bit) return false;
                seen |= bit;
            }
            if (n != 1 || !structure_eq(b, t[0], "end") || !blob_add(f, s, b, "method-section", begin, c.at - begin)) return false;
        } else if (structure_eq(b, t[0], "task")) {
            if (n != 3 || geometry != 1 || basis != 1 || (!structure_eq(b, t[1], "scf") && !structure_eq(b, t[1], "dft")) ||
                (structure_eq(b, t[1], "scf") ? (!scf || dft) : (!dft || scf)) ||
                (!structure_eq(b, t[2], "energy") && !structure_eq(b, t[2], "gradient") && !structure_eq(b, t[2], "optimize")))
                return false;
            return chemistry_add_row(f, s, b, "task-declaration", row, c.at) && chemistry_finish(&c, "#");
        } else {
            if (structure_eq(b, t[0], "title")) {
                if (n < 2 || row.n > 255) return false;
            } else if (structure_eq(b, t[0], "start") || structure_eq(b, t[0], "permanent_dir") || structure_eq(b, t[0], "scratch_dir")) {
                if (n != 2 || t[1].n > 255) return false;
            } else if (structure_eq(b, t[0], "charge")) {
                if (n != 2 || !scientific_text_range(b, t[1], 1000, 1000)) return false;
            } else if (structure_eq(b, t[0], "memory")) {
                if (n != 3 || !scientific_text_uint(b, t[1], &x) || !x || x > 1048576 ||
                    (!structure_eq(b, t[2], "mb") && !structure_eq(b, t[2], "mw") && !structure_eq(b, t[2], "gb")))
                    return false;
            } else if (n != 1 || !structure_eq(b, t[0], "echo")) return false;
            if (!chemistry_add_row(f, s, b, "input-declaration", row, c.at)) return false;
        }
    }
    return false;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_NWCHEM_INPUT && nwchem_input_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_nwchem_input_init(xx_nwchem_input *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NWCHEM_INPUT, "nwchem_input");
    }
}
xx_nwchem_input *xx_nwchem_input_create(xx_io_device *d, int64_t b)
{
    xx_nwchem_input *r = (xx_nwchem_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nwchem_input_init(r, d, b);
    return r;
}
void xx_nwchem_input_destroy(xx_nwchem_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nwchem_input_free(xx_nwchem_input *r)
{
    if (r) {
        xx_nwchem_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nwchem_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nwchem_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
