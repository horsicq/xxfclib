/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/calculators/demon/demon.html */
#include "xxfclib/formats/demon_input/xx_demon_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool demon_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    scientific_text_lines c = {0};
    scientific_text_token row, t[16];
    unsigned n, atoms = 0, seen = 0;
    uint64_t begin;
    c.b = b;
    while (chemistry_words(&c, &row, t, 16, &n, "#")) {
        unsigned bit;
        if (structure_eq(b, t[0], "GEOMETRY"))
            break;
        if (structure_eq(b, t[0], "TITLE")) {
            if (n < 2)
                return false;
            bit = 1;
        } else if (structure_eq(b, t[0], "SCFTYPE")) {
            if (n != 2 || (!structure_eq(b, t[1], "RKS") && !structure_eq(b, t[1], "UKS") &&
                           !structure_eq(b, t[1], "RHF") && !structure_eq(b, t[1], "UHF")))
                return false;
            bit = 2;
        } else if (structure_eq(b, t[0], "VXCTYPE")) {
            if (n != 2 || !chemistry_identifier(b, t[1]))
                return false;
            bit = 4;
        } else if (structure_eq(b, t[0], "GUESS")) {
            if (n != 2 || !chemistry_identifier(b, t[1]))
                return false;
            bit = 8;
        } else if (structure_eq(b, t[0], "PRINT")) {
            if (n < 2)
                return false;
            bit = 16;
        } else if (structure_eq(b, t[0], "BASIS")) {
            if (n != 2 || t[1].n < 3 || b->p[(size_t)t[1].at] != '(' || b->p[(size_t)(t[1].at + t[1].n - 1)] != ')' ||
                !chemistry_identifier(b, scientific_text_slice(t[1], 1, t[1].n - 2)))
                return false;
            bit = 32;
        } else
            return false;
        if (seen & bit) {
            return false;
        }
        seen |= bit;
    }
    if ((seen & 47) != 47 || n != 3 || !structure_eq(b, t[0], "GEOMETRY") || !structure_eq(b, t[1], "CARTESIAN") ||
        !structure_eq(b, t[2], "ANGSTROM"))
        return false;
    begin = c.at;
    if (!blob_add(f, s, b, "calculation-header", 0, begin))
        return false;
    while (chemistry_words(&c, &row, t, 16, &n, "#")) {
        uint64_t k = 0, z;
        scientific_text_token symbol, labelnum;
        if (n != 6 || ++atoms > 4090 || !structure_floats(b, t + 1, 3) || !scientific_text_uint(b, t[4], &z) ||
            !structure_positive(b, t[5]))
            return false;
        while (k < t[0].n && ((b->p[(size_t)(t[0].at + k)] >= 'A' && b->p[(size_t)(t[0].at + k)] <= 'Z') ||
                              (b->p[(size_t)(t[0].at + k)] >= 'a' && b->p[(size_t)(t[0].at + k)] <= 'z')))
            ++k;
        symbol = scientific_text_slice(t[0], 0, k);
        labelnum = scientific_text_slice(t[0], k, t[0].n - k);
        if (z != chemistry_atomic_number(b, symbol) || !z || !scientific_text_uint(b, labelnum, &k) || k != atoms ||
            !chemistry_add_row(f, s, b, "atomic-position", row, c.at))
            return false;
    }
    return c.at == b->n && atoms && !binary_stop(b->pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_DEMON_INPUT && demon_input_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_demon_input_init(xx_demon_input *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DEMON_INPUT, "demon_input");
    }
}
xx_demon_input *xx_demon_input_create(xx_io_device *d, int64_t b) {
    xx_demon_input *r = (xx_demon_input *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_demon_input_init(r, d, b);
    return r;
}
void xx_demon_input_destroy(xx_demon_input *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_demon_input_free(xx_demon_input *r) {
    if (r) {
        xx_demon_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_demon_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_demon_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
