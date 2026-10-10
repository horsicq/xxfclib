/* SPDX-License-Identifier: MIT
 * Independently implemented from https://ase-lib.org/_modules/ase/io/dlp4.html */
#include "xxfclib/formats/dl_poly_config/xx_dl_poly_config.h"
#include "../common/xx_molecular_text.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[4];
    unsigned n;
    uint64_t level, periodic, atoms, i, j, id, at;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    BLOB_NEED(scientific_text_line(&c, &line) && line.n && line.n <= 80 && molecular_words(&c, &line, t, 4, &n) && n == 3 && scientific_text_uint(&b, t[0], &level) &&
              level <= 2 && scientific_text_uint(&b, t[1], &periodic) && periodic <= 3 && scientific_text_uint(&b, t[2], &atoms) && atoms && atoms <= 100000);
    if (periodic)
        for (i = 0; i < 3; ++i)
            BLOB_NEED(molecular_words(&c, &line, t, 4, &n) && n == 3 && molecular_floats(&b, t, 0, 3) &&
                      !(scientific_text_zero(&b, t[0]) && scientific_text_zero(&b, t[1]) && scientific_text_zero(&b, t[2])));
    BLOB_NEED(blob_add(f, s, &b, "configuration-header", 0, c.at));
    for (i = 1; i <= atoms; ++i) {
        at = c.at;
        BLOB_NEED(molecular_words(&c, &line, t, 4, &n) && n == 2 && molecular_element(&b, t[0]) && scientific_text_uint(&b, t[1], &id) && id == i);
        for (j = 0; j <= level; ++j) BLOB_NEED(molecular_words(&c, &line, t, 4, &n) && n == 3 && molecular_floats(&b, t, 0, 3));
        BLOB_NEED(blob_add(f, s, &b, "atom-state", at, c.at - at));
    }
    BLOB_NEED(molecular_trailing(&c));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_dl_poly_config_init(xx_dl_poly_config *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DL_POLY_CONFIG, "dl_poly_config");
    }
}
xx_dl_poly_config *xx_dl_poly_config_create(xx_io_device *d, int64_t b)
{
    xx_dl_poly_config *r = (xx_dl_poly_config *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dl_poly_config_init(r, d, b);
    return r;
}
void xx_dl_poly_config_destroy(xx_dl_poly_config *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dl_poly_config_free(xx_dl_poly_config *r)
{
    if (r) {
        xx_dl_poly_config_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dl_poly_config_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dl_poly_config_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
