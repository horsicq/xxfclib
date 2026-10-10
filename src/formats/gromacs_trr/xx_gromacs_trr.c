/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/gromacs/gromacs/main/src/gromacs/fileio/trrio.cpp */
#include "xxfclib/formats/gromacs_trr/xx_gromacs_trr.h"
#include "../common/xx_binary_records.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint64_t total = (uint64_t)pm_available(f), at = 0;
    uint32_t frame = 0, first_atoms = 0;
    char label[64];
    if (total > 67108864) return false;
    while (at < total) {
        uint8_t p[64];
        uint64_t start = at, arraybytes;
        uint32_t len, atoms, width = 0, sizes[10], i;
        if (frame >= 512 || !record_take(f, &at, total, p, 12, pd) || xx_data_get_u32(p, 4, 0, true) != 1993 || xx_data_get_u32(p + 4, 4, 0, true) != 13 ||
            (len = xx_data_get_u32(p + 8, 4, 0, true)) != 12 || !record_take(f, &at, total, p, len, pd) || xx_rt_memcmp(p, "GMX_trn_file\0", 12) ||
            !record_take(f, &at, total, p, 52, pd))
            return false;
        for (i = 0; i < 10; ++i) {
            sizes[i] = xx_data_get_u32(p + i * 4, 4, 0, true);
        }
        atoms = xx_data_get_u32(p + 40, 4, 0, true);
        if (!atoms || atoms > 1048576 || (frame && atoms != first_atoms) || sizes[0] || sizes[1] || sizes[5] || sizes[6] ||
            (int32_t)xx_data_get_u32(p + 44, 4, 0, true) < 0 || xx_data_get_u32(p + 48, 4, 0, true)) {
            return false;
        }
        first_atoms = atoms;
        if (sizes[2]) width = sizes[2] / 9;
        else
            for (i = 7; i < 10 && !width; ++i)
                if (sizes[i]) width = sizes[i] / (atoms * 3);
        if ((width != 4 && width != 8) || !binary_mul(atoms, (uint64_t)width * 3, &arraybytes)) return false;
        for (i = 2; i <= 4; ++i)
            if (sizes[i] && sizes[i] != width * 9) return false;
        for (i = 7; i < 10; ++i)
            if (sizes[i] && sizes[i] != arraybytes) return false;
        if (!record_span(at, width * 2, total) || !record_float_array(f, at, width * 2, width, true, pd)) {
            return false;
        }
        at += width * 2;
        xx_rt_snprintf(label, sizeof(label), "frame-%u-header.bin", frame);
        if (!pm_add(f, s, label, (int64_t)start, (int64_t)(at - start))) return false;
        for (i = 0; i < 10; ++i)
            if (sizes[i]) {
                if (!record_span(at, sizes[i], total) || !record_float_array(f, at, sizes[i], width, true, pd)) return false;
                xx_rt_snprintf(label, sizeof(label), "frame-%u-block-%u.bin", frame, i);
                if (!pm_add(f, s, label, (int64_t)at, sizes[i])) return false;
                at += sizes[i];
            }
        ++frame;
    }
    if (!frame) return false;
    s->size = (int64_t)at;
    return true;
}

void xx_gromacs_trr_init(xx_gromacs_trr *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GROMACS_TRR, "gromacs_trr");
    }
}
xx_gromacs_trr *xx_gromacs_trr_create(xx_io_device *d, int64_t b)
{
    xx_gromacs_trr *r = (xx_gromacs_trr *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gromacs_trr_init(r, d, b);
    return r;
}
void xx_gromacs_trr_destroy(xx_gromacs_trr *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gromacs_trr_free(xx_gromacs_trr *r)
{
    if (r) {
        xx_gromacs_trr_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gromacs_trr_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gromacs_trr_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
