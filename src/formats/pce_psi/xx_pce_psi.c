/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/pce_psi/xx_pce_psi.h"
#include "../common/xx_disk_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    disk_music_blob b = {0};
    uint64_t at = 0;
    uint32_t sectors = 0, keys[1024], size = 0, curkey = 0;
    bool filled = true, ibm = false, ended = false, ok = false;
    DISK_MUSIC_NEED(disk_music_load(f, &b, pd) && disk_music_tag(&b, 0, "PSI ", 4));
    while (at < b.n) {
        uint64_t q = at + 8;
        uint32_t n, crc;
        DISK_MUSIC_NEED(disk_music_work(&b, 1) && disk_music_span(&b, at, 12));
        n = xx_data_get_u32(b.p + (size_t)at + 4, 4, 0, true);
        DISK_MUSIC_NEED(disk_music_span(&b, q, (uint64_t)n + 4) && disk_music_crc(&b, at, 8 + (uint64_t)n, &crc) &&
                        crc == xx_data_get_u32(b.p + (size_t)q + n, 4, 0, true));
        if (!at) {
            uint16_t enc;
            DISK_MUSIC_NEED(n == 4 && !xx_data_get_u16(b.p + (size_t)q, 2, 0, true));
            enc = xx_data_get_u16(b.p + (size_t)q + 2, 2, 0, true);
            DISK_MUSIC_NEED(enc == 0 || enc == 256 || enc == 512 || enc == 513 || enc == 514);
        } else if (disk_music_tag(&b, at, "SECT", 4)) {
            uint32_t j;
            uint8_t flags;
            DISK_MUSIC_NEED(filled && n == 8 && sectors < 1024 && b.p[(size_t)q + 2] <= 1);
            curkey = ((uint32_t)xx_data_get_u16(b.p + (size_t)q, 2, 0, true) << 16) | ((uint32_t)b.p[(size_t)q + 2] << 8) | b.p[(size_t)q + 3];
            size = xx_data_get_u16(b.p + (size_t)q + 4, 2, 0, true);
            flags = b.p[(size_t)q + 6];
            DISK_MUSIC_NEED(size >= 128 && size <= 16384 && !(size & (size - 1)) && flags <= 1);
            for (j = 0; j < sectors; ++j) DISK_MUSIC_NEED(disk_music_work(&b, 1) && keys[j] != curkey);
            keys[sectors++] = curkey;
            filled = (flags & 1) != 0;
            ibm = false;
            if (filled) {
                uint8_t *v = (uint8_t *)xx_mem_alloc(size);
                DISK_MUSIC_NEED(v);
                xx_rt_memset(v, b.p[(size_t)q + 7], size);
                if (!disk_music_bytes(f, s, &b, "sector.raw", v, size)) {
                    xx_mem_free(v);
                    goto done;
                }
                xx_mem_free(v);
            }
        } else if (disk_music_tag(&b, at, "DATA", 4)) {
            DISK_MUSIC_NEED(sectors && !filled && n == size && disk_music_emit(f, s, &b, "sector.raw", q, n));
            filled = true;
        } else if (disk_music_tag(&b, at, "IBMF", 4) || disk_music_tag(&b, at, "IBMM", 4)) {
            const uint8_t *v = b.p + (size_t)q;
            DISK_MUSIC_NEED(sectors && !ibm && n == 6 && v[0] == (curkey >> 16) && v[1] == ((curkey >> 8) & 255) && v[2] == (curkey & 255) && v[3] <= 7 &&
                            (128U << v[3]) == size && !(v[4] & ~4U) && v[5] <= (disk_music_tag(&b, at, "IBMF", 4) ? 1 : 2));
            ibm = true;
        } else if (disk_music_tag(&b, at, "OFFS", 4) || disk_music_tag(&b, at, "TIME", 4)) {
            DISK_MUSIC_NEED(sectors && n == 4);
        } else if (disk_music_tag(&b, at, "TEXT", 4)) {
            uint32_t j;
            DISK_MUSIC_NEED(n && n <= 1048576); /* Narrow ASCII/LF comment subset, no unsafe path names. */
            for (j = 0; j < n; ++j)
                DISK_MUSIC_NEED(disk_music_work(&b, 1) &&
                                ((b.p[(size_t)q + j] >= 32 && b.p[(size_t)q + j] < 127) || b.p[(size_t)q + j] == 10 || b.p[(size_t)q + j] == 9));
        } else if (disk_music_tag(&b, at, "END ", 4)) {
            DISK_MUSIC_NEED(!n && filled && sectors && q + 4 == b.n);
            ended = true;
        } else DISK_MUSIC_NEED(false);
        DISK_MUSIC_NEED(disk_music_emit(f, s, &b, "chunk.psi", at, 12 + (uint64_t)n));
        at = q + n + 4;
    }
    DISK_MUSIC_NEED(ended);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_pce_psi_init(xx_pce_psi *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PCE_PSI, "pce_psi");
    }
}
xx_pce_psi *xx_pce_psi_create(xx_io_device *d, int64_t b)
{
    xx_pce_psi *r = (xx_pce_psi *)xx_mem_alloc(sizeof(*r));
    if (r) xx_pce_psi_init(r, d, b);
    return r;
}
void xx_pce_psi_destroy(xx_pce_psi *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_pce_psi_free(xx_pce_psi *r)
{
    if (r) {
        xx_pce_psi_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_pce_psi_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_pce_psi_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
