/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/theochem/iodata/blob/master/iodata/formats/fchk.py */
#include "xxfclib/formats/gaussian_fchk/xx_gaussian_fchk.h"
#include "../common/xx_molecular_text.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[4], labels[4096];
    unsigned n, fields = 0, ff_count = 0;
    uint64_t atoms = 0, zcount = 0, ccount = 0, budget = 8388608, ff_value = 0;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    BLOB_NEED(
        scientific_text_line(&c, &line) && line.n && line.n <= 80 && molecular_words(&c, &line, t, 4, &n) && n == 3 &&
        (scientific_text_eq(&b, t[0], "SP") || scientific_text_eq(&b, t[0], "FOpt") || scientific_text_eq(&b, t[0], "Freq") || scientific_text_eq(&b, t[0], "Scan")) &&
        blob_add(f, s, &b, "checkpoint-header", 0, c.at));
    while (c.at < b.n) {
        uint64_t begin = c.at, count, i;
        scientific_text_token name;
        bool real, array, az, coords;
        uint8_t type;
        BLOB_NEED(scientific_text_line(&c, &line) && line.n >= 47 && fields < 4096 && b.p[(size_t)line.at + 40] == ' ' && b.p[(size_t)line.at + 41] == ' ' &&
                  b.p[(size_t)line.at + 42] == ' ' && b.p[(size_t)line.at + 44] == ' ');
        name = scientific_text_trim(&b, scientific_text_slice(line, 0, 40));
        BLOB_NEED(name.n && (scientific_text_eq(&b, name, "Force Field") || scientific_text_unique(&b, name, labels, fields, &budget)));
        labels[fields++] = name;
        type = b.p[(size_t)line.at + 43];
        BLOB_NEED(type == 'I' || type == 'R');
        real = type == 'R';
        BLOB_NEED(scientific_text_split(&b, scientific_text_slice(line, 45, line.n - 45), t, 4, &n, false) && (n == 1 || n == 2));
        array = n == 2;
        az = scientific_text_eq(&b, name, "Atomic numbers");
        coords = scientific_text_eq(&b, name, "Current cartesian coordinates");
        if (scientific_text_eq(&b, name, "Force Field")) {
            uint64_t v;
            BLOB_NEED(!real && !array && scientific_text_uint(&b, t[0], &v) && ++ff_count <= 2);
            if (ff_count == 2) BLOB_NEED(v == ff_value);
            ff_value = v;
        }
        if (array) {
            BLOB_NEED(scientific_text_eq(&b, t[0], "N=") && scientific_text_uint(&b, t[1], &count) && count <= 1000000);
            if (az) {
                BLOB_NEED(!real && count && !zcount);
                zcount = count;
            }
            if (coords) {
                BLOB_NEED(real && count && !ccount);
                ccount = count;
            }
            for (i = 0; i < count;) {
                scientific_text_token values;
                unsigned got, j, width = real ? 16 : 12, perline = real ? 5 : 6;
                BLOB_NEED(scientific_text_line(&c, &values) && molecular_fixed(&b, values, width, &got, real) &&
                          got == ((count - i) < perline ? (unsigned)(count - i) : perline));
                if (az)
                    for (j = 0; j < got; ++j) BLOB_NEED(molecular_z(&b, scientific_text_trim(&b, scientific_text_slice(values, j * width, width))));
                i += got;
            }
        } else {
            BLOB_NEED(real ? scientific_text_float(&b, t[0]) : scientific_text_integer(&b, t[0]));
            BLOB_NEED(!az && !coords);
            if (scientific_text_eq(&b, name, "Number of atoms")) BLOB_NEED(!real && scientific_text_uint(&b, t[0], &atoms) && atoms && atoms <= 100000);
        }
        BLOB_NEED(blob_add(f, s, &b, "checkpoint-field", begin, c.at - begin));
    }
    BLOB_NEED(atoms && zcount == atoms && ccount == atoms * 3);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_gaussian_fchk_init(xx_gaussian_fchk *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GAUSSIAN_FCHK, "gaussian_fchk");
    }
}
xx_gaussian_fchk *xx_gaussian_fchk_create(xx_io_device *d, int64_t b)
{
    xx_gaussian_fchk *r = (xx_gaussian_fchk *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gaussian_fchk_init(r, d, b);
    return r;
}
void xx_gaussian_fchk_destroy(xx_gaussian_fchk *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gaussian_fchk_free(xx_gaussian_fchk *r)
{
    if (r) {
        xx_gaussian_fchk_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gaussian_fchk_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gaussian_fchk_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
