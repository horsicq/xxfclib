/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.faccts.de/docs/orca/6.1/manual/contents/essentialelements/input.html */
#include "xxfclib/formats/orca_input/xx_orca_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool orca_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token row, t[64];
    unsigned n, atoms = 0, options = 0;
    uint64_t mult, begin;
    c.b = b;
    if (!chemistry_words(&c, &row, t, 64, &n, "#") || n < 2 || !scientific_text_eq(b, t[0], "!") || row.n > 1024) return false;
    {
        unsigned i;
        for (i = 1; i < n; ++i)
            if (!scientific_text_chars(b, t[i], "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_+*-()/=,.", true)) return false;
    }
    while (chemistry_words(&c, &row, t, 64, &n, "#")) {
        if (structure_eq(b, t[0], "%maxcore")) {
            uint64_t x;
            if ((options & 1) || n != 2 || !scientific_text_uint(b, t[1], &x) || !x || x > 1048576) return false;
            options |= 1;
        } else if (structure_eq(b, t[0], "%pal")) {
            if ((options & 2) || n != 1 || !chemistry_words(&c, &row, t, 64, &n, "#") || n != 2 || !structure_eq(b, t[0], "nprocs") ||
                !scientific_text_uint(b, t[1], &mult) || !mult || mult > 65536 || !chemistry_words(&c, &row, t, 64, &n, "#") || n != 1 || !structure_eq(b, t[0], "end")) {
                return false;
            }
            options |= 2;
        } else break;
    }
    if (n != 3 || !structure_eq(b, t[0], "*xyz") || !scientific_text_range(b, t[1], 1000, 1000) || !scientific_text_uint(b, t[2], &mult) || !mult || mult > 1000)
        return false;
    begin = c.at;
    if (!blob_add(f, s, b, "calculation-header", 0, begin)) return false;
    while (chemistry_words(&c, &row, t, 64, &n, "#")) {
        if (n == 1 && scientific_text_eq(b, t[0], "*")) return atoms && chemistry_add_row(f, s, b, "geometry-terminator", row, c.at) && chemistry_finish(&c, "#");
        if (n != 4 || ++atoms > 4090 || !chemistry_atom(b, t, n, 0, 1) || !chemistry_add_row(f, s, b, "atomic-position", row, c.at)) return false;
    }
    return false;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_ORCA_INPUT && orca_input_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_orca_input_init(xx_orca_input *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ORCA_INPUT, "orca_input");
    }
}
xx_orca_input *xx_orca_input_create(xx_io_device *d, int64_t b)
{
    xx_orca_input *r = (xx_orca_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_orca_input_init(r, d, b);
    return r;
}
void xx_orca_input_destroy(xx_orca_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_orca_input_free(xx_orca_input *r)
{
    if (r) {
        xx_orca_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_orca_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_orca_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
