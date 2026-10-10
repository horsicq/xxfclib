/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.wwpdb.org/documentation/file-format-content/format33/sect9.html */
#include "xxfclib/formats/protein_pdb/xx_protein_pdb.h"
#include "../common/xx_scientific_text.h"
static bool pdb_metadata(memory_blob *b, scientific_text_token kind)
{
    static const char *known[] = {"HEADER", "OBSLTE", "TITLE",  "SPLT",   "CAVEAT", "COMPND", "SOURCE", "KEYWDS", "EXPDTA", "NUMMDL", "MDLTYP",
                                  "AUTHOR", "REVDAT", "SPRSDE", "JRNL",   "REMARK", "DBREF",  "DBREF1", "DBREF2", "SEQADV", "SEQRES", "MODRES",
                                  "HET",    "HETNAM", "HETSYN", "FORMUL", "HELIX",  "SHEET",  "SSBOND", "LINK",   "CISPEP", "SITE",   "CRYST1",
                                  "ORIGX1", "ORIGX2", "ORIGX3", "SCALE1", "SCALE2", "SCALE3", "MTRIX1", "MTRIX2", "MTRIX3", "MASTER"};
    unsigned i;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (scientific_text_eq(b, kind, known[i])) return true;
    return false;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, kind;
    uint8_t *serials = NULL;
    uint64_t atoms = 0, body_start = 0, end_start = 0, last = 0;
    bool ok = false, body = false, ended = false;
    BLOB_NEED(blob_load(f, &b, pd));
    serials = (uint8_t *)xx_mem_alloc(100000);
    BLOB_NEED(serials);
    xx_mem_zero(serials, 100000);
    c.b = &b;
    while (c.at < b.n) {
        uint64_t start = c.at, n;
        unsigned i;
        BLOB_NEED(scientific_text_line(&c, &line) && line.n >= 3 && line.n <= 80);
        kind = scientific_text_trim(&b, scientific_text_slice(line, 0, line.n >= 6 ? 6 : line.n));
        if (scientific_text_eq(&b, kind, "ATOM") || scientific_text_eq(&b, kind, "HETATM")) {
            BLOB_NEED(!ended && line.n >= 78 && scientific_text_uint(&b, scientific_text_slice(line, 6, 5), &n) && n && n <= 99999 && !serials[n]);
            serials[n] = 1;
            last = n;
            ++atoms;
            BLOB_NEED(atoms <= 100000);
            BLOB_NEED(scientific_text_ident(&b, scientific_text_trim(&b, scientific_text_slice(line, 12, 4))) &&
                      scientific_text_chars(&b, scientific_text_trim(&b, scientific_text_slice(line, 17, 3)),
                                            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", true) &&
                      scientific_text_range(&b, scientific_text_slice(line, 22, 4), 999, 9999));
            for (i = 0; i < 3; ++i) BLOB_NEED(scientific_text_float(&b, scientific_text_slice(line, 30 + 8 * i, 8)));
            BLOB_NEED(
                scientific_text_float(&b, scientific_text_slice(line, 54, 6)) && scientific_text_float(&b, scientific_text_slice(line, 60, 6)) &&
                scientific_text_chars(&b, scientific_text_trim(&b, scientific_text_slice(line, 76, 2)), "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", false));
            {
                char text[8];
                const char *e;
                double occupancy;
                scientific_text_token v = scientific_text_trim(&b, scientific_text_slice(line, 54, 6));
                xx_rt_memcpy(text, b.p + (size_t)v.at, (size_t)v.n);
                text[v.n] = 0;
                occupancy = xx_rt_strtod(text, &e);
                BLOB_NEED(occupancy >= 0 && occupancy <= 1);
            }
            if (!body) {
                body_start = start;
                body = true;
            }
        } else if (scientific_text_eq(&b, kind, "ANISOU")) {
            BLOB_NEED(body && line.n >= 70 && scientific_text_uint(&b, scientific_text_slice(line, 6, 5), &n) && n == last);
            for (i = 0; i < 6; ++i) BLOB_NEED(scientific_text_range(&b, scientific_text_slice(line, 28 + 7 * i, 7), 999999, 9999999));
        } else if (scientific_text_eq(&b, kind, "TER")) {
            BLOB_NEED(body && line.n >= 27 && scientific_text_uint(&b, scientific_text_slice(line, 6, 5), &n) && n && n <= 99999 &&
                      scientific_text_ident(&b, scientific_text_trim(&b, scientific_text_slice(line, 17, 3))) &&
                      scientific_text_range(&b, scientific_text_slice(line, 22, 4), 999, 9999));
        } else if (scientific_text_eq(&b, kind, "CONECT")) {
            BLOB_NEED(body && line.n >= 11);
            for (i = 6; i + 5 <= line.n; i += 5) {
                scientific_text_token v = scientific_text_trim(&b, scientific_text_slice(line, i, 5));
                if (v.n) BLOB_NEED(scientific_text_uint(&b, v, &n) && n && n <= 99999 && serials[n]);
            }
        } else if (scientific_text_eq(&b, kind, "END")) {
            BLOB_NEED(body && scientific_text_eq(&b, scientific_text_trim(&b, line), "END") && c.at == b.n);
            end_start = start;
            ended = true;
        } else {
            BLOB_NEED(!ended && pdb_metadata(&b, kind));
            if (scientific_text_eq(&b, kind, "NUMMDL")) BLOB_NEED(line.n >= 14 && scientific_text_uint(&b, scientific_text_slice(line, 10, 4), &n) && n == 1);
        }
    }
    BLOB_NEED(atoms && ended);
    if (body_start) BLOB_NEED(blob_add(f, s, &b, "metadata", 0, body_start));
    BLOB_NEED(blob_add(f, s, &b, "coordinates", body_start, end_start - body_start) && blob_add(f, s, &b, "end", end_start, b.n - end_start));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(serials);
    xx_mem_free(b.p);
    return ok;
}

void xx_protein_pdb_init(xx_protein_pdb *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PROTEIN_PDB, "protein_pdb");
    }
}
xx_protein_pdb *xx_protein_pdb_create(xx_io_device *d, int64_t b)
{
    xx_protein_pdb *r = (xx_protein_pdb *)xx_mem_alloc(sizeof(*r));
    if (r) xx_protein_pdb_init(r, d, b);
    return r;
}
void xx_protein_pdb_destroy(xx_protein_pdb *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_protein_pdb_free(xx_protein_pdb *r)
{
    if (r) {
        xx_protein_pdb_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_protein_pdb_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_protein_pdb_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
