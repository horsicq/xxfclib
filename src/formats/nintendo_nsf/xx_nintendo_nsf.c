/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/libgme/game-music-emu/blob/master/gme/Nsf_Emu.h
 * NSF version 1; bounded 128-byte descriptor and nonempty banked/unbanked program image; original inert program bytes only, no emulation.
 */
#include "xxfclib/formats/nintendo_nsf/xx_nintendo_nsf.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f, pm_stream *s, retro_disk_blob *b)
{
    uint32_t i, load, size;
    bool banked = false;
    if (!retro_disk_range(b, 0, 129) || xx_rt_memcmp(b->p, "NESM\x1a", 5) || b->p[5] != 1 || !b->p[6] || !b->p[7] || b->p[7] > b->p[6] ||
        (load = xx_data_get_u16(b->p + 8, 2, 0, false)) < 0x6000 || xx_data_get_u16(b->p + 10, 2, 0, false) < 0x6000 ||
        (xx_data_get_u16(b->p + 12, 2, 0, false) && xx_data_get_u16(b->p + 12, 2, 0, false) < 0x6000) || (b->p[122] & ~3U) || (b->p[123] & ~63U))
        return false;
    size = b->n - 128;
    for (i = 112; i < 120; ++i)
        if (b->p[i]) banked = true;
    if (!banked) {
        if (size > 65536U - load) return false;
    } else {
        uint32_t pages = (size + (load & 4095U) + 4095U) / 4096U;
        if (size > 1048576U) return false;
        for (i = 112; i < 120; ++i)
            if (b->p[i] >= pages) return false;
    }
    if (!retro_disk_emit(f, s, b, "music-descriptor.bin", 0, 128) || !retro_disk_emit(f, s, b, "program.bin", 128, size)) {
        return false;
    }
    s->size = b->n;
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

void xx_nintendo_nsf_init(xx_nintendo_nsf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_NSF, "nsf");
    }
}
xx_nintendo_nsf *xx_nintendo_nsf_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_nsf *r = (xx_nintendo_nsf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_nsf_init(r, d, b);
    return r;
}
void xx_nintendo_nsf_destroy(xx_nintendo_nsf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_nsf_free(xx_nintendo_nsf *r)
{
    if (r) {
        xx_nintendo_nsf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_nsf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_nsf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
