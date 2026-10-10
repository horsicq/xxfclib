/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/openbabel/openbabel/blob/master/src/formats/mol2format.cpp */
#include "xxfclib/formats/tripos_mol2/xx_tripos_mol2.h"
#include "../common/xx_molecular_text.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[16];
    unsigned n;
    uint64_t atoms, bonds, subs = 0, i, id, a, z, at;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    BLOB_NEED(scientific_text_line(&c, &line) && scientific_text_eq(&b, line, "@<TRIPOS>MOLECULE") && scientific_text_line(&c, &line) && line.n &&
              molecular_words(&c, &line, t, 16, &n) && n >= 2 && n <= 5 && scientific_text_uint(&b, t[0], &atoms) && atoms && atoms <= 100000 &&
              scientific_text_uint(&b, t[1], &bonds) && bonds <= 100000);
    if (n >= 3) {
        BLOB_NEED(scientific_text_uint(&b, t[2], &subs) && subs <= atoms);
    }
    for (i = 3; i < n; ++i) BLOB_NEED(scientific_text_eq(&b, t[i], "0"));
    BLOB_NEED(scientific_text_line(&c, &line) && scientific_text_eq(&b, scientific_text_trim(&b, line), "SMALL") && scientific_text_line(&c, &line) &&
              (scientific_text_eq(&b, scientific_text_trim(&b, line), "NO_CHARGES") || scientific_text_eq(&b, scientific_text_trim(&b, line), "GASTEIGER") ||
               scientific_text_eq(&b, scientific_text_trim(&b, line), "USER_CHARGES") || scientific_text_eq(&b, scientific_text_trim(&b, line), "AMBER") ||
               scientific_text_eq(&b, scientific_text_trim(&b, line), "AM1BCC") || scientific_text_eq(&b, scientific_text_trim(&b, line), "MMFF94_CHARGES")));
    BLOB_NEED(scientific_text_line(&c, &line));
    while (!scientific_text_trim(&b, line).n) BLOB_NEED(scientific_text_line(&c, &line));
    BLOB_NEED(scientific_text_eq(&b, line, "@<TRIPOS>ATOM") && blob_add(f, s, &b, "molecule-header", 0, c.at));
    at = c.at;
    for (i = 1; i <= atoms; ++i) {
        scientific_text_token e;
        uint64_t j = 0;
        BLOB_NEED(molecular_words(&c, &line, t, 16, &n) && (n == 6 || n == 8 || n == 9) && scientific_text_uint(&b, t[0], &id) && id == i && molecular_name(&b, t[1]) &&
                  molecular_floats(&b, t, 2, 5));
        e = t[5];
        while (j < e.n && b.p[(size_t)(e.at + j)] != '.') ++j;
        BLOB_NEED(molecular_element(&b, scientific_text_slice(e, 0, j)) && molecular_name(&b, e));
        if (n >= 8) {
            BLOB_NEED(scientific_text_uint(&b, t[6], &id) && (subs ? id >= 1 : id <= atoms) && id <= (subs ? subs : atoms) && molecular_name(&b, t[7]));
        }
        if (n == 9) BLOB_NEED(scientific_text_float(&b, t[8]));
    }
    BLOB_NEED(blob_add(f, s, &b, "atoms", at, c.at - at) && scientific_text_line(&c, &line) && scientific_text_eq(&b, line, "@<TRIPOS>BOND"));
    at = line.at;
    for (i = 1; i <= bonds; ++i)
        BLOB_NEED(molecular_words(&c, &line, t, 16, &n) && n == 4 && scientific_text_uint(&b, t[0], &id) && id == i && scientific_text_uint(&b, t[1], &a) && a >= 1 &&
                  a <= atoms && scientific_text_uint(&b, t[2], &z) && z >= 1 && z <= atoms && z != a &&
                  (scientific_text_eq(&b, t[3], "1") || scientific_text_eq(&b, t[3], "2") || scientific_text_eq(&b, t[3], "3") || scientific_text_eq(&b, t[3], "ar") ||
                   scientific_text_eq(&b, t[3], "am") || scientific_text_eq(&b, t[3], "du") || scientific_text_eq(&b, t[3], "un") || scientific_text_eq(&b, t[3], "nc")));
    BLOB_NEED(blob_add(f, s, &b, "bonds", at, c.at - at));
    if (subs) {
        BLOB_NEED(scientific_text_line(&c, &line) && scientific_text_eq(&b, line, "@<TRIPOS>SUBSTRUCTURE"));
        at = line.at;
        for (i = 1; i <= subs; ++i) {
            BLOB_NEED(molecular_words(&c, &line, t, 16, &n) && (n == 3 || (n >= 6 && n <= 9)) && scientific_text_uint(&b, t[0], &id) && id == i &&
                      molecular_name(&b, t[1]) && scientific_text_uint(&b, t[2], &a) && a >= 1 && a <= atoms);
            if (n >= 6)
                BLOB_NEED((scientific_text_eq(&b, t[3], "GROUP") || scientific_text_eq(&b, t[3], "RESIDUE") || scientific_text_eq(&b, t[3], "UNKNOWN")) &&
                          scientific_text_uint(&b, t[4], &id) && id <= 65535 && molecular_name(&b, t[5]));
            if (n >= 7) {
                BLOB_NEED(molecular_name(&b, t[6]));
            }
            if (n >= 8) BLOB_NEED(scientific_text_uint(&b, t[7], &id) && id <= bonds);
            if (n == 9) BLOB_NEED(scientific_text_eq(&b, t[8], "ROOT") || scientific_text_eq(&b, t[8], "LEAF"));
        }
        BLOB_NEED(blob_add(f, s, &b, "substructures", at, c.at - at));
    }
    BLOB_NEED(molecular_trailing(&c));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_tripos_mol2_init(xx_tripos_mol2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRIPOS_MOL2, "tripos_mol2");
    }
}
xx_tripos_mol2 *xx_tripos_mol2_create(xx_io_device *d, int64_t b)
{
    xx_tripos_mol2 *r = (xx_tripos_mol2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tripos_mol2_init(r, d, b);
    return r;
}
void xx_tripos_mol2_destroy(xx_tripos_mol2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tripos_mol2_free(xx_tripos_mol2 *r)
{
    if (r) {
        xx_tripos_mol2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tripos_mol2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tripos_mol2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
