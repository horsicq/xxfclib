/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.lammps.org/dump.html */
#include "xxfclib/formats/lammps_dump/xx_lammps_dump.h"
#include "../common/xx_scientific_structure.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[80], columns[64];
    uint8_t *ids = NULL;
    unsigned nt, ncol, j, frames = 0, bounds, idcol, typecol, elementcol, mode;
    uint64_t count, i, id, type, begin, head, box, values, timestep, budget;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    while (c.at < b.n) {
        begin = c.at;
        if (!structure_words(&c, &line, t, 80, &nt, "#")) {
            BLOB_NEED(c.at == b.n);
            break;
        }
        BLOB_NEED(++frames <= 1000 && nt == 2 && scientific_text_eq(&b, t[0], "ITEM:") && scientific_text_eq(&b, t[1], "TIMESTEP"));
        BLOB_NEED(structure_words(&c, &line, t, 80, &nt, "#") && nt == 1 && scientific_text_uint(&b, t[0], &timestep));
        BLOB_NEED(structure_words(&c, &line, t, 80, &nt, "#") && nt == 4 && scientific_text_eq(&b, t[0], "ITEM:") && scientific_text_eq(&b, t[1], "NUMBER") &&
                  scientific_text_eq(&b, t[2], "OF") && scientific_text_eq(&b, t[3], "ATOMS"));
        BLOB_NEED(structure_words(&c, &line, t, 80, &nt, "#") && nt == 1 && scientific_text_uint(&b, t[0], &count) && count && count <= 1000000);
        head = c.at;
        BLOB_NEED(structure_words(&c, &line, t, 80, &nt, "#") && (nt == 6 || nt == 9) && scientific_text_eq(&b, t[0], "ITEM:") && scientific_text_eq(&b, t[1], "BOX") &&
                  scientific_text_eq(&b, t[2], "BOUNDS"));
        bounds = nt == 9 ? 3 : 2;
        if (bounds == 3) BLOB_NEED(scientific_text_eq(&b, t[3], "xy") && scientific_text_eq(&b, t[4], "xz") && scientific_text_eq(&b, t[5], "yz"));
        for (j = nt - 3; j < nt; ++j)
            BLOB_NEED(t[j].n == 2 && scientific_text_chars(&b, t[j], "pfsm", true) && ((b.p[(size_t)t[j].at] == 'p') == (b.p[(size_t)t[j].at + 1] == 'p')));
        for (j = 0; j < 3; ++j) {
            BLOB_NEED(structure_words(&c, &line, t, 80, &nt, "#") && nt == bounds && structure_floats(&b, t, bounds) &&
                      molecular_value(&b, t[0]) < molecular_value(&b, t[1]));
        }
        box = c.at;
        BLOB_NEED(structure_words(&c, &line, t, 80, &nt, "#") && nt >= 7 && nt <= 66 && scientific_text_eq(&b, t[0], "ITEM:") && scientific_text_eq(&b, t[1], "ATOMS"));
        ncol = nt - 2;
        idcol = typecol = elementcol = 64;
        mode = 0;
        budget = 1000000;
        for (j = 0; j < ncol; ++j) {
            BLOB_NEED(molecular_name(&b, t[j + 2]) && scientific_text_unique(&b, t[j + 2], columns, j, &budget));
            columns[j] = t[j + 2];
            if (scientific_text_eq(&b, columns[j], "id")) idcol = j;
            if (scientific_text_eq(&b, columns[j], "type")) typecol = j;
            if (scientific_text_eq(&b, columns[j], "element")) elementcol = j;
            if (scientific_text_eq(&b, columns[j], "x")) {
                mode |= 1;
            }
            if (scientific_text_eq(&b, columns[j], "y")) mode |= 2;
            if (scientific_text_eq(&b, columns[j], "z")) mode |= 4;
            if (scientific_text_eq(&b, columns[j], "xs")) {
                mode |= 8;
            }
            if (scientific_text_eq(&b, columns[j], "ys")) mode |= 16;
            if (scientific_text_eq(&b, columns[j], "zs")) mode |= 32;
            if (scientific_text_eq(&b, columns[j], "xu")) {
                mode |= 64;
            }
            if (scientific_text_eq(&b, columns[j], "yu")) mode |= 128;
            if (scientific_text_eq(&b, columns[j], "zu")) mode |= 256;
        }
        BLOB_NEED(idcol < ncol && typecol < ncol && (mode == 7 || mode == 56 || mode == 448));
        values = c.at;
        ids = (uint8_t *)xx_mem_alloc((size_t)count);
        BLOB_NEED(ids);
        xx_rt_memset(ids, 0, (size_t)count);
        for (i = 0; i < count; ++i) {
            BLOB_NEED(structure_words(&c, &line, t, 80, &nt, "#") && nt == ncol && scientific_text_uint(&b, t[idcol], &id) && id && id <= count && !ids[(size_t)id - 1] &&
                      scientific_text_uint(&b, t[typecol], &type) && type && type <= 100000);
            ids[(size_t)id - 1] = 1;
            for (j = 0; j < ncol; ++j) BLOB_NEED(j == elementcol ? molecular_element(&b, t[j]) : scientific_text_float(&b, t[j]));
        }
        xx_mem_free(ids);
        ids = NULL;
        BLOB_NEED(blob_add(f, s, &b, "frame-count-time", begin, head - begin) && blob_add(f, s, &b, "frame-bounds", head, box - head) &&
                  blob_add(f, s, &b, "frame-columns", box, values - box) && blob_add(f, s, &b, "frame-atoms", values, c.at - values));
    }
    BLOB_NEED(frames);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(ids);
    xx_mem_free(b.p);
    return ok;
}

void xx_lammps_dump_init(xx_lammps_dump *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LAMMPS_DUMP, "lammps_dump");
    }
}
xx_lammps_dump *xx_lammps_dump_create(xx_io_device *d, int64_t b)
{
    xx_lammps_dump *r = (xx_lammps_dump *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lammps_dump_init(r, d, b);
    return r;
}
void xx_lammps_dump_destroy(xx_lammps_dump *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lammps_dump_free(xx_lammps_dump *r)
{
    if (r) {
        xx_lammps_dump_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lammps_dump_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lammps_dump_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
