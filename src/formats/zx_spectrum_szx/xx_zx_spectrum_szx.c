/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/fuse-emulator/libspectrum/blob/master/szx.c
 * SZX v1.0-v1.5 for 16K/48K/128K; requires Z80R, SPCR and complete stored 16KiB RAM pages, bounded JOY/KEYB/ZXPR/AY/CRTR state records. Compressed RAM and unsupported
 * device extensions rejected.
 */
#include "xxfclib/formats/zx_spectrum_szx/xx_zx_spectrum_szx.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f, pm_stream *s, retro_disk_blob *b)
{
    uint32_t at = 8, flags = 0, pages = 0, machine, needed;
    char name[48];
    if (!retro_disk_range(b, 0, 8) || xx_rt_memcmp(b->p, "ZXST", 4) || b->p[4] != 1 || b->p[5] > 5 || (machine = b->p[6]) > 2 || b->p[7] > 1 ||
        !retro_disk_emit(f, s, b, "snapshot-descriptor.bin", 0, 8))
        return false;
    needed = machine == 0 ? 32U : machine == 1 ? 37U : 255U;
    while (at < b->n) {
        uint32_t z, bit = 0;
        const uint8_t *p;
        if (!retro_disk_range(b, at, 8) || (z = xx_data_get_u32(b->p + at + 4, 4, 0, false)) > b->n - at - 8) {
            return false;
        }
        p = b->p + at + 8;
        if (!xx_rt_memcmp(b->p + at, "Z80R", 4)) {
            bit = 1;
            if (z != 37 || p[26] > 1 || p[27] > 1 || p[28] > 2 || (p[34] & ~7U)) return false;
        } else if (!xx_rt_memcmp(b->p + at, "SPCR", 4)) {
            bit = 2;
            if (z != 8 || p[0] > 7 || !retro_disk_zero(p + 4, 4) || (machine < 2 && (p[1] || p[2]))) return false;
        } else if (!xx_rt_memcmp(b->p + at, "RAMP", 4)) {
            uint32_t pg;
            if (z != 16387 || xx_data_get_u16(p, 2, 0, false) || (pg = p[2]) > 7 || !(needed & (1U << pg)) || (pages & (1U << pg))) return false;
            pages |= 1U << pg;
        } else if (!xx_rt_memcmp(b->p + at, "CRTR", 4)) {
            bit = 4;
            if (z < 36 || !retro_disk_ascii(p, 32, true)) return false;
        } else if (!xx_rt_memcmp(b->p + at, "JOY\0", 4)) {
            bit = 16;
            if (z != 6 || xx_data_get_u32(p, 4, 0, false) > 1 || p[4] > 8 || p[5] > 8) return false;
        } else if (!xx_rt_memcmp(b->p + at, "KEYB", 4)) {
            bit = 32;
            if (z != (b->p[5] ? 5U : 4U) || xx_data_get_u32(p, 4, 0, false) > 1 || (z == 5 && p[4] > 8)) return false;
        } else if (!xx_rt_memcmp(b->p + at, "ZXPR", 4)) {
            bit = 64;
            if (z != 2 || xx_data_get_u16(p, 2, 0, false) > 1) return false;
        } else if (!xx_rt_memcmp(b->p + at, "AY\0\0", 4)) {
            bit = 8;
            if (machine != 2 || z != 18 || p[0] > 1 || p[1] > 15) return false;
        } else return false;
        if (bit && (flags & bit)) {
            return false;
        }
        flags |= bit;
        xx_rt_snprintf(name, sizeof(name), "chunk-%u.bin", (unsigned)s->count - 1U);
        if (!retro_disk_emit(f, s, b, name, at, z + 8)) return false;
        at += z + 8;
    }
    if ((flags & 3U) != 3U || pages != needed) {
        return false;
    }
    s->size = at;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_disk_blob b;
    bool ok;
    if (!retro_disk_load(f, &b, pd)) return false;
    ok = parse_blob(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}

void xx_zx_spectrum_szx_init(xx_zx_spectrum_szx *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ZX_SPECTRUM_SZX, "szx");
    }
}
xx_zx_spectrum_szx *xx_zx_spectrum_szx_create(xx_io_device *d, int64_t b)
{
    xx_zx_spectrum_szx *r = (xx_zx_spectrum_szx *)xx_mem_alloc(sizeof(*r));
    if (r) xx_zx_spectrum_szx_init(r, d, b);
    return r;
}
void xx_zx_spectrum_szx_destroy(xx_zx_spectrum_szx *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_zx_spectrum_szx_free(xx_zx_spectrum_szx *r)
{
    if (r) {
        xx_zx_spectrum_szx_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_zx_spectrum_szx_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_zx_spectrum_szx_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
