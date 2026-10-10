/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/openmm/openmm/master/wrappers/python/openmm/app/dcdfile.py */
#include "xxfclib/formats/charmm_dcd/xx_charmm_dcd.h"
#include "../common/xx_binary_records.h"

static bool dcd_record(Abstractformat *f, uint64_t *at, uint64_t end, bool be, uint32_t expected, uint64_t *data, xx_pd_struct *pd)
{
    uint32_t n, tail;
    if (!record_word(f, at, end, be, &n, pd) || n != expected || !record_span(*at, n, end)) return false;
    *data = *at;
    *at += n;
    return record_word(f, at, end, be, &tail, pd) && tail == n;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[92], p[8];
    uint64_t total = (uint64_t)pm_available(f), at = 92, data, bytes, header;
    uint32_t frames, atoms, title, i, j, tail;
    bool be, box;
    char label[64];
    if (total > 67108864 || !pm_read(f, 0, h, 92) || xx_rt_memcmp(h + 4, "CORD", 4)) {
        return false;
    }
    be = xx_data_get_u32(h, 4, 0, true) == 84;
    if (xx_data_get_u32(h, 4, 0, be) != 84 || xx_data_get_u32(h + 88, 4, 0, be) != 84 || !(frames = xx_data_get_u32(h + 8, 4, 0, be)) || frames > 1024 ||
        (int32_t)xx_data_get_u32(h + 12, 4, 0, be) < 0 || !xx_data_get_u32(h + 16, 4, 0, be) || xx_data_get_u32(h + 40, 4, 0, be) ||
        (xx_data_get_u32(h + 44, 4, 0, be) & 0x7f800000U) == 0x7f800000U || !xx_data_get_u32(h + 84, 4, 0, be) || xx_data_get_u32(h + 48, 4, 0, be) > 1 ||
        xx_data_get_u32(h + 52, 4, 0, be))
        return false;
    box = xx_data_get_u32(h + 48, 4, 0, be) != 0;
    for (i = 56; i < 84; i += 4)
        if (xx_data_get_u32(h + i, 4, 0, be)) return false;
    if (!record_word(f, &at, total, be, &title, pd) || title < 84 || title > 4 + 80 * 32 || (title - 4) % 80 || !record_span(at, title, total) ||
        !pm_read(f, (int64_t)at, p, 4) || xx_data_get_u32(p, 4, 0, be) != (title - 4) / 80)
        return false;
    at += title;
    if (!record_word(f, &at, total, be, &tail, pd) || tail != title || !dcd_record(f, &at, total, be, 4, &data, pd) || !pm_read(f, (int64_t)data, p, 4) ||
        !(atoms = xx_data_get_u32(p, 4, 0, be)) || atoms > 1048576 || !binary_mul(atoms, 4, &bytes))
        return false;
    header = at;
    if ((uint64_t)frames * (box ? 4 : 3) + 1 > 4096 || !pm_add(f, s, "dcd-header.bin", 0, (int64_t)header)) return false;
    for (i = 0; i < frames; ++i) {
        if (box) {
            if (!dcd_record(f, &at, total, be, 48, &data, pd) || !record_float_array(f, data, 48, 8, be, pd)) return false;
            xx_rt_snprintf(label, sizeof(label), "frame-%u-cell.bin", i);
            if (!pm_add(f, s, label, (int64_t)data, 48)) return false;
        }
        for (j = 0; j < 3; ++j) {
            if (!dcd_record(f, &at, total, be, (uint32_t)bytes, &data, pd) || !record_float_array(f, data, bytes, 4, be, pd)) return false;
            xx_rt_snprintf(label, sizeof(label), "frame-%u-%c.bin", i, "xyz"[j]);
            if (!pm_add(f, s, label, (int64_t)data, (int64_t)bytes)) return false;
        }
    }
    if (at != total) return false;
    s->size = (int64_t)at;
    return true;
}

void xx_charmm_dcd_init(xx_charmm_dcd *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_CHARMM_DCD, "charmm_dcd");
    }
}
xx_charmm_dcd *xx_charmm_dcd_create(xx_io_device *d, int64_t b)
{
    xx_charmm_dcd *r = (xx_charmm_dcd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_charmm_dcd_init(r, d, b);
    return r;
}
void xx_charmm_dcd_destroy(xx_charmm_dcd *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_charmm_dcd_free(xx_charmm_dcd *r)
{
    if (r) {
        xx_charmm_dcd_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_charmm_dcd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_charmm_dcd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
