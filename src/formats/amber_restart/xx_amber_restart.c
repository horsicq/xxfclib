/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/ParmEd/ParmEd/blob/master/parmed/amber/asciicrd.py */
#include "xxfclib/formats/amber_restart/xx_amber_restart.h"
#include "../common/xx_molecular_text.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[3];
    unsigned n;
    uint64_t atoms, at, footer = 0;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    BLOB_NEED(scientific_text_line(&c, &line) && line.n && molecular_words(&c, &line, t, 3, &n) && n >= 1 && n <= 2 && scientific_text_uint(&b, t[0], &atoms) &&
              atoms >= 3 && atoms <= 100000);
    if (n == 2) {
        BLOB_NEED(scientific_text_float(&b, t[1]));
    }
    BLOB_NEED(blob_add(f, s, &b, "restart-header", 0, c.at));
    at = c.at;
    BLOB_NEED(molecular_fixed_array(&c, atoms * 3, 12, 6, true) && blob_add(f, s, &b, "coordinates", at, c.at - at));
    at = c.at;
    while (c.at < b.n) {
        BLOB_NEED(scientific_text_line(&c, &line) && molecular_fixed(&b, line, 12, &n, true) && n <= 6 && footer + n <= atoms * 3 + 6);
        footer += n;
    }
    BLOB_NEED(!footer || footer == 6 || footer == atoms * 3 || footer == atoms * 3 + 6);
    if (footer) BLOB_NEED(blob_add(f, s, &b, "optional-velocity-box", at, c.at - at));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_amber_restart_init(xx_amber_restart *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AMBER_RESTART, "amber_restart");
    }
}
xx_amber_restart *xx_amber_restart_create(xx_io_device *d, int64_t b)
{
    xx_amber_restart *r = (xx_amber_restart *)xx_mem_alloc(sizeof(*r));
    if (r) xx_amber_restart_init(r, d, b);
    return r;
}
void xx_amber_restart_destroy(xx_amber_restart *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_amber_restart_free(xx_amber_restart *r)
{
    if (r) {
        xx_amber_restart_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_amber_restart_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_amber_restart_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
