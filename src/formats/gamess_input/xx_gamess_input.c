/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.msg.chem.iastate.edu/gamess/GAMESS_Manual/input.pdf */
#include "xxfclib/formats/gamess_input/xx_gamess_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool gamess_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    static const char *const contrl[] = {"RUNTYP", "MULT", "SCFTYP", "ICHARG", "COORD", "UNITS"};
    static const char *const basis[] = {"GBASIS", "NGAUSS", "NDFUNC", "NPFUNC", "POLAR"};
    scientific_text_lines c = {0};
    scientific_text_token row, t[16], keys[16];
    unsigned n, group, atoms = 0;
    c.b = b;
    for (group = 0; group < 2; ++group) {
        unsigned count = 0, seen = 0;
        uint64_t begin;
        if (!chemistry_words(&c, &row, t, 16, &n, "!") || n != 1 ||
            !structure_eq(b, t[0], group ? "$BASIS" : "$CONTRL")) {
            return false;
        }
        begin = chemistry_row_start(b, row);
        while (chemistry_words(&c, &row, t, 16, &n, "!")) {
            unsigned j;
            if (n == 1 && structure_eq(b, t[0], "$END"))
                break;
            for (j = 0; j < n; ++j) {
                scientific_text_token key, value;
                unsigned k;
                if (!chemistry_assignment(b, t[j], &key, &value) || count == 16 ||
                    !chemistry_in(b, key, group ? basis : contrl, group ? 5U : 6U))
                    return false;
                for (k = 0; k < count; ++k) {
                    if (chemistry_same_key(b, key, keys[k]))
                        return false;
                }
                keys[count++] = key;
                if (structure_eq(b, key, "RUNTYP")) {
                    if (!structure_eq(b, value, "ENERGY") && !structure_eq(b, value, "GRADIENT") &&
                        !structure_eq(b, value, "OPTIMIZE"))
                        return false;
                    seen |= 1;
                } else if (structure_eq(b, key, "SCFTYP")) {
                    if (!structure_eq(b, value, "RHF") && !structure_eq(b, value, "UHF") &&
                        !structure_eq(b, value, "ROHF"))
                        return false;
                    seen |= 2;
                } else if (structure_eq(b, key, "MULT")) {
                    uint64_t x;
                    if (!scientific_text_uint(b, value, &x) || !x || x > 1000)
                        return false;
                    seen |= 4;
                } else if (structure_eq(b, key, "ICHARG")) {
                    if (!scientific_text_range(b, value, 1000, 1000))
                        return false;
                } else if (structure_eq(b, key, "COORD")) {
                    if (!structure_eq(b, value, "UNIQUE"))
                        return false;
                } else if (structure_eq(b, key, "UNITS")) {
                    if (!structure_eq(b, value, "ANGS"))
                        return false;
                } else if (structure_eq(b, key, "GBASIS")) {
                    if (!chemistry_identifier(b, value))
                        return false;
                    seen |= 8;
                } else if (structure_eq(b, key, "POLAR")) {
                    if (!chemistry_identifier(b, value))
                        return false;
                } else {
                    uint64_t x;
                    if (!scientific_text_uint(b, value, &x) || x > 20)
                        return false;
                }
            }
        }
        if (n != 1 || !structure_eq(b, t[0], "$END") || (group ? !(seen & 8) : (seen & 7) != 7) ||
            !blob_add(f, s, b, group ? "basis-group" : "control-group", begin, c.at - begin))
            return false;
    }
    if (!chemistry_words(&c, &row, t, 16, &n, "!") || n != 1 || !structure_eq(b, t[0], "$DATA") ||
        !chemistry_line(&c, &row) || !scientific_text_trim(b, row).n || !chemistry_words(&c, &row, t, 16, &n, "!") ||
        n != 1 || !structure_eq(b, t[0], "C1") || !chemistry_add_row(f, s, b, "symmetry-declaration", row, c.at))
        return false;
    while (chemistry_words(&c, &row, t, 16, &n, "!")) {
        if (n == 1 && structure_eq(b, t[0], "$END"))
            return atoms && chemistry_add_row(f, s, b, "data-terminator", row, c.at) && chemistry_finish(&c, "!");
        if (n != 5 || ++atoms > 4090 || !chemistry_atom(b, t, n, 0, 2) || !scientific_text_float(b, t[1]) ||
            molecular_value(b, t[1]) != (double)chemistry_atomic_number(b, t[0]) ||
            !chemistry_add_row(f, s, b, "atomic-position", row, c.at))
            return false;
    }
    return false;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_GAMESS_INPUT && gamess_input_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_gamess_input_init(xx_gamess_input *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GAMESS_INPUT, "gamess_input");
    }
}
xx_gamess_input *xx_gamess_input_create(xx_io_device *d, int64_t b) {
    xx_gamess_input *r = (xx_gamess_input *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_gamess_input_init(r, d, b);
    return r;
}
void xx_gamess_input_destroy(xx_gamess_input *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gamess_input_free(xx_gamess_input *r) {
    if (r) {
        xx_gamess_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gamess_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_gamess_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
