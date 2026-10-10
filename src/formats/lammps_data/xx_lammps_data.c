/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.lammps.org/read_data.html */
#include "xxfclib/formats/lammps_data/xx_lammps_data.h"
#include "../common/xx_scientific_structure.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[16];
    uint8_t *ids = NULL;
    unsigned nt, header = 0, sections = 0, which, j;
    uint64_t atoms = 0, types = 0, id, type, i, begin = 0, head = 0;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    BLOB_NEED(scientific_text_line(&c, &line) && line.n);
    for (;;) {
        begin = c.at;
        BLOB_NEED(structure_words(&c, &line, t, 16, &nt, "#"));
        if (scientific_text_eq(&b, t[0], "Masses") || scientific_text_eq(&b, t[0], "Atoms") || scientific_text_eq(&b, t[0], "Velocities")) {
            head = begin;
            break;
        }
        if (nt == 2 && scientific_text_eq(&b, t[1], "atoms")) {
            BLOB_NEED(!(header & 1) && scientific_text_uint(&b, t[0], &atoms) && atoms && atoms <= 1000000);
            header |= 1;
        } else if (nt == 3 && scientific_text_eq(&b, t[1], "atom") && scientific_text_eq(&b, t[2], "types")) {
            BLOB_NEED(!(header & 2) && scientific_text_uint(&b, t[0], &types) && types && types <= 100000);
            header |= 2;
        } else if (nt == 4) {
            unsigned bit = scientific_text_eq(&b, t[2], "xlo") && scientific_text_eq(&b, t[3], "xhi")   ? 4
                           : scientific_text_eq(&b, t[2], "ylo") && scientific_text_eq(&b, t[3], "yhi") ? 8
                           : scientific_text_eq(&b, t[2], "zlo") && scientific_text_eq(&b, t[3], "zhi") ? 16
                                                                                                        : 0;
            BLOB_NEED(bit && !(header & bit) && structure_floats(&b, t, 2) && molecular_value(&b, t[0]) < molecular_value(&b, t[1]));
            header |= bit;
        } else if (nt == 6 && scientific_text_eq(&b, t[3], "xy") && scientific_text_eq(&b, t[4], "xz") && scientific_text_eq(&b, t[5], "yz")) {
            BLOB_NEED(!(header & 32) && structure_floats(&b, t, 3));
            header |= 32;
        } else BLOB_NEED(false);
    }
    BLOB_NEED((header & 31) == 31 && blob_add(f, s, &b, "data-header", 0, head));
    ids = (uint8_t *)xx_mem_alloc((size_t)(atoms > types ? atoms : types));
    BLOB_NEED(ids);
    for (;;) {
        uint64_t count, end;
        BLOB_NEED(nt == 1);
        which = scientific_text_eq(&b, t[0], "Masses") ? 1 : scientific_text_eq(&b, t[0], "Atoms") ? 2 : scientific_text_eq(&b, t[0], "Velocities") ? 4 : 0;
        BLOB_NEED(which && !(sections & which) && (which != 4 || (sections & 2)));
        sections |= which;
        count = which == 1 ? types : atoms;
        xx_rt_memset(ids, 0, (size_t)count);
        for (i = 0; i < count; ++i) {
            BLOB_NEED(structure_words(&c, &line, t, 16, &nt, "#") &&
                      nt == (which == 1   ? 2U
                             : which == 2 ? 5U
                                          : 4U) &&
                      scientific_text_uint(&b, t[0], &id) && id && id <= count && !ids[(size_t)id - 1]);
            ids[(size_t)id - 1] = 1;
            if (which == 1) BLOB_NEED(structure_positive(&b, t[1]));
            else if (which == 2) BLOB_NEED(scientific_text_uint(&b, t[1], &type) && type && type <= types && structure_floats(&b, t + 2, 3));
            else
                for (j = 1; j < 4; ++j) BLOB_NEED(scientific_text_float(&b, t[j]));
        }
        end = c.at;
        BLOB_NEED(blob_add(f, s, &b, which == 1 ? "masses" : which == 2 ? "atoms" : "velocities", begin, end - begin));
        begin = c.at;
        if (!structure_words(&c, &line, t, 16, &nt, "#")) {
            BLOB_NEED(c.at == b.n);
            break;
        }
    }
    BLOB_NEED(sections & 2);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(ids);
    xx_mem_free(b.p);
    return ok;
}

void xx_lammps_data_init(xx_lammps_data *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LAMMPS_DATA, "lammps_data");
    }
}
xx_lammps_data *xx_lammps_data_create(xx_io_device *d, int64_t b)
{
    xx_lammps_data *r = (xx_lammps_data *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lammps_data_init(r, d, b);
    return r;
}
void xx_lammps_data_destroy(xx_lammps_data *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lammps_data_free(xx_lammps_data *r)
{
    if (r) {
        xx_lammps_data_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lammps_data_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lammps_data_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
