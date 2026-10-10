/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.mdanalysis.org/stable/documentation_pages/coordinates/CRD.html */
#include "xxfclib/formats/charmm_crd/xx_charmm_crd.h"
#include "../common/xx_scientific_structure.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[12];
    unsigned nt, comments = 0;
    uint64_t atoms, i, id, res, head;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    do {
        BLOB_NEED(scientific_text_line(&c, &line));
        line = scientific_text_trim(&b, line);
        if (line.n && b.p[(size_t)line.at] == '*') ++comments;
        else break;
    } while (c.at < b.n);
    BLOB_NEED(comments && scientific_text_split(&b, line, t, 12, &nt, false) && (nt == 1 || (nt == 2 && scientific_text_eq(&b, t[1], "EXT"))) &&
              scientific_text_uint(&b, t[0], &atoms) && atoms && atoms <= 100000);
    head = c.at;
    for (i = 1; i <= atoms; ++i) {
        BLOB_NEED(structure_words(&c, &line, t, 12, &nt, "*") && nt == 10 && scientific_text_uint(&b, t[0], &id) && id == i && scientific_text_uint(&b, t[1], &res) &&
                  res && molecular_name(&b, t[2]) && molecular_name(&b, t[3]) && structure_floats(&b, t + 4, 3) && molecular_name(&b, t[7]) && molecular_name(&b, t[8]) &&
                  scientific_text_float(&b, t[9]));
    }
    BLOB_NEED(molecular_trailing(&c) && blob_add(f, s, &b, "coordinate-card-header", 0, head) && blob_add(f, s, &b, "atom-coordinate-cards", head, b.n - head));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_charmm_crd_init(xx_charmm_crd *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_CHARMM_CRD, "charmm_crd");
    }
}
xx_charmm_crd *xx_charmm_crd_create(xx_io_device *d, int64_t b)
{
    xx_charmm_crd *r = (xx_charmm_crd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_charmm_crd_init(r, d, b);
    return r;
}
void xx_charmm_crd_destroy(xx_charmm_crd *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_charmm_crd_free(xx_charmm_crd *r)
{
    if (r) {
        xx_charmm_crd_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_charmm_crd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_charmm_crd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
