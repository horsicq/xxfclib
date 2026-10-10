/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/scummvm/scummvm/master/engines/grim/lab.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/lucas_lab/xx_lucas_lab.h"
#include "../bethesda_bsa/xx_game_table.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[16], r[16];
    uint32_t count, names, i;
    uint64_t nt, floor, used;
    int64_t total = pm_available(f);
    if (!gm_read(f, total, 0, h, 16) || xx_rt_memcmp(h, "LABN", 4) || xx_data_get_u32(h + 4, 4, 0, false) != 0x10000) return false;
    count = xx_data_get_u32(h + 8, 4, 0, false);
    names = xx_data_get_u32(h + 12, 4, 0, false);
    nt = 16 + (uint64_t)count * 16;
    floor = nt + names;
    if (count > 65536 || names > 16777216 || !gm_range(total, nt, names)) {
        return false;
    }
    s->size = (int64_t)floor;
    for (i = 0; i < count; ++i) {
        uint32_t name;
        if (gm_stopped(pd) || !gm_read(f, total, 16 + (uint64_t)i * 16, r, 16)) return false;
        name = xx_data_get_u32(r, 4, 0, false);
        if (name >= names || !gm_string(f, total, nt + name, names - name, &used) || used == 1 ||
            !gm_add(f, s, "member.bin", xx_data_get_u32(r + 4, 4, 0, false), xx_data_get_u32(r + 8, 4, 0, false), floor, total))
            return false;
    }
    return true;
}
void xx_lucas_lab_init(xx_lucas_lab *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LUCAS_LAB, "bin");
    }
}
xx_lucas_lab *xx_lucas_lab_create(xx_io_device *d, int64_t b)
{
    xx_lucas_lab *r = (xx_lucas_lab *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lucas_lab_init(r, d, b);
    return r;
}
void xx_lucas_lab_destroy(xx_lucas_lab *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lucas_lab_free(xx_lucas_lab *r)
{
    if (r) {
        xx_lucas_lab_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lucas_lab_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lucas_lab_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
