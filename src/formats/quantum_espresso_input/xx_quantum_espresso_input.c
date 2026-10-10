/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.quantum-espresso.org/Doc/INPUT_PW.html */
#include "xxfclib/formats/quantum_espresso_input/xx_quantum_espresso_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool quantum_espresso_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    static const char *const groups[] = {"&CONTROL", "&SYSTEM", "&ELECTRONS", "&IONS", "&CELL", "&FCP", "&RISM"};
    scientific_text_lines c = {0};
    scientific_text_token row, t[16], species[118], keys[32];
    unsigned n, g = 0, block = 0, keycount = 0, required = 0, cards = 0, k, atoms = 0, types = 0;
    uint64_t start = 0, count;
    bool in = false, calculation = false;
    c.b = b;
    while (chemistry_words(&c, &row, t, 16, &n, "!")) {
        if (in) {
            if (n == 1 && scientific_text_eq(b, t[0], "/")) {
                if (!blob_add(f, s, b, "namelist", start, c.at - start)) return false;
                in = false;
                continue;
            }
            {
                scientific_text_token key, value;
                unsigned j;
                if (!chemistry_assignment(b, row, &key, &value) || keycount == 32) return false;
                for (j = 0; j < keycount; ++j) {
                    if (chemistry_same_key(b, key, keys[j])) return false;
                }
                keys[keycount++] = key;
                if (block == 1) {
                    if (structure_eq(b, key, "calculation")) {
                        if (!structure_eq(b, value, "scf")) return false;
                        calculation = true;
                    } else if (!structure_eq(b, key, "prefix") && !structure_eq(b, key, "outdir") && !structure_eq(b, key, "pseudo_dir") &&
                               !structure_eq(b, key, "verbosity") && !structure_eq(b, key, "disk_io"))
                        return false;
                } else if (block == 2) {
                    if (structure_eq(b, key, "nat")) {
                        if (!scientific_text_uint(b, value, &count) || !count || count > 4090) return false;
                        atoms = (unsigned)count;
                        required |= 1;
                    } else if (structure_eq(b, key, "ntyp")) {
                        if (!scientific_text_uint(b, value, &count) || !count || count > 118) return false;
                        types = (unsigned)count;
                        required |= 2;
                    } else if (structure_eq(b, key, "ibrav")) {
                        if (!scientific_text_zero(b, value)) return false;
                        required |= 4;
                    } else if (structure_eq(b, key, "ecutwfc") || structure_eq(b, key, "ecutrho")) {
                        if (!structure_positive(b, value)) return false;
                        if (structure_eq(b, key, "ecutwfc")) required |= 8;
                    } else if (structure_eq(b, key, "tot_charge")) {
                        if (!scientific_text_float(b, value)) return false;
                    } else return false;
                } else if (block == 3) {
                    if (structure_eq(b, key, "conv_thr") || structure_eq(b, key, "mixing_beta")) {
                        if (!structure_positive(b, value)) return false;
                    } else if (structure_eq(b, key, "electron_maxstep")) {
                        if (!scientific_text_uint(b, value, &count) || !count || count > 100000) return false;
                    } else return false;
                } else return false;
            }
        } else {
            unsigned match;
            for (match = g; match < 7; ++match)
                if (n == 1 && structure_eq(b, t[0], groups[match])) break;
            if (match == 7) break;
            if ((g == 0 && match != 0) || (g == 1 && match != 1) || (g == 2 && match != 2)) return false;
            block = match + 1;
            g = block;
            start = chemistry_row_start(b, row);
            keycount = 0;
            in = true;
        }
    }
    if (in || g < 3 || required != 15 || !calculation || n != 1 || !structure_eq(b, t[0], "ATOMIC_SPECIES")) return false;
    if (!chemistry_add_row(f, s, b, "species-declaration", row, c.at)) return false;
    for (k = 0; k < types; ++k) {
        unsigned j;
        if (!chemistry_words(&c, &row, t, 16, &n, "!#") || n != 3 || !chemistry_atomic_number(b, t[0]) || !structure_positive(b, t[1]) || !scientific_text_ident(b, t[2]))
            return false;
        for (j = 0; j < k; ++j) {
            if (structure_same(b, t[0], species[j])) return false;
        }
        species[k] = t[0];
        if (!chemistry_add_row(f, s, b, "atomic-species", row, c.at)) return false;
    }
    while (chemistry_words(&c, &row, t, 16, &n, "!#")) {
        if (structure_eq(b, t[0], "K_POINTS")) {
            if ((cards & 1) || n != 2 || (!structure_eq(b, t[1], "gamma") && !structure_eq(b, t[1], "automatic"))) {
                return false;
            }
            cards |= 1;
            start = chemistry_row_start(b, row);
            if (structure_eq(b, t[1], "automatic")) {
                unsigned j;
                if (!chemistry_words(&c, &row, t, 16, &n, "!#") || n != 6) return false;
                for (j = 0; j < 6; ++j)
                    if (!scientific_text_uint(b, t[j], &count) || (j < 3 ? (!count || count > 1024) : count > 1)) return false;
            }
            if (!blob_add(f, s, b, "k-point-grid", start, c.at - start)) return false;
        } else if (structure_eq(b, t[0], "CELL_PARAMETERS")) {
            if ((cards & 2) || n != 2 || (!structure_eq(b, t[1], "angstrom") && !structure_eq(b, t[1], "bohr"))) {
                return false;
            }
            cards |= 2;
            start = chemistry_row_start(b, row);
            if (!chemistry_cell_comments(&c, "!#") || !blob_add(f, s, b, "lattice-vectors", start, c.at - start)) return false;
        } else if (structure_eq(b, t[0], "ATOMIC_POSITIONS")) {
            if ((cards & 4) || n != 2 || (!structure_eq(b, t[1], "angstrom") && !structure_eq(b, t[1], "bohr") && !structure_eq(b, t[1], "crystal"))) {
                return false;
            }
            cards |= 4;
            if (!chemistry_add_row(f, s, b, "coordinate-declaration", row, c.at)) return false;
            for (k = 0; k < atoms; ++k) {
                unsigned j;
                if (!chemistry_words(&c, &row, t, 16, &n, "!#") || (n != 4 && n != 7) || !structure_floats(b, t + 1, 3)) return false;
                for (j = 0; j < types; ++j) {
                    if (structure_same(b, t[0], species[j])) break;
                }
                if (j == types) return false;
                if (n == 7)
                    for (j = 4; j < 7; ++j)
                        if (!scientific_text_uint(b, t[j], &count) || count > 1) return false;
                if (!chemistry_add_row(f, s, b, "atomic-position", row, c.at)) return false;
            }
        } else return false;
    }
    return c.at == b->n && cards == 7 && !binary_stop(b->pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_QUANTUM_ESPRESSO_INPUT && quantum_espresso_input_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_quantum_espresso_input_init(xx_quantum_espresso_input *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_QUANTUM_ESPRESSO_INPUT, "quantum_espresso_input");
    }
}
xx_quantum_espresso_input *xx_quantum_espresso_input_create(xx_io_device *d, int64_t b)
{
    xx_quantum_espresso_input *r = (xx_quantum_espresso_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_quantum_espresso_input_init(r, d, b);
    return r;
}
void xx_quantum_espresso_input_destroy(xx_quantum_espresso_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_quantum_espresso_input_free(xx_quantum_espresso_input *r)
{
    if (r) {
        xx_quantum_espresso_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_quantum_espresso_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_quantum_espresso_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
