/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/snes_spc/xx_snes_spc.h"
#include "../common/xx_retro_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f, pm_stream *s, retro_music_blob *b)
{
    uint32_t a = 66048, count = 0;
    const uint8_t *p = b->p;
    if (b->n < 66048 || xx_rt_memcmp(p, "SNES-SPC700 Sound File Data v0.30", 33) || p[33] != 26 || p[34] != 26 || (p[35] != 26 && p[35] != 27) || p[36] != 30)
        return false;
    if (!retro_music_emit(f, s, b, "snapshot-descriptor.bin", 0, 256) || !retro_music_emit(f, s, b, "spc700-ram.bin", 256, 65536) ||
        !retro_music_emit(f, s, b, "dsp-registers.bin", 65792, 128) || !retro_music_emit(f, s, b, "reserved.bin", 65920, 64) ||
        !retro_music_emit(f, s, b, "ipl-rom.bin", 65984, 64))
        return false;
    if (a < b->n) {
        uint32_t end, z;
        if (!retro_music_range(b, a, 8) || xx_rt_memcmp(p + a, "xid6", 4)) return false;
        z = xx_data_get_u32(p + a + 4, 4, 0, false);
        if (!retro_music_range(b, a + 8, z) || a + 8 + z != b->n || z > 1048576) return false;
        end = a + 8 + z;
        a += 8;
        while (a < end) {
            uint32_t n, after;
            unsigned type;
            if (!retro_music_poll(b) || ++count > 4096 || end - a < 4) return false;
            type = p[a + 1];
            n = type ? xx_data_get_u16(p + a + 2, 2, 0, false) : 0;
            if (type > 4 || (type != 0 && type != 1 && type != 4) || (type == 4 && n != 4) || n > end - a - 4) return false;
            after = a + 4 + n;
            if (type == 1 && (!n || p[after - 1])) return false;
            a = (after + 3) & ~3U;
            if (a > end || !retro_music_zero(p + after, a - after)) return false;
        }
        if (!retro_music_emit(f, s, b, "extended-tags.xid6", 66048, b->n - 66048)) return false;
    }
    s->size = b->n;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_music_blob b;
    bool ok;
    if (!retro_music_load(f, &b, pd)) return false;
    ok = read_components(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}
void xx_snes_spc_init(xx_snes_spc *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SNES_SPC, "snes_spc");
    }
}
xx_snes_spc *xx_snes_spc_create(xx_io_device *d, int64_t b)
{
    xx_snes_spc *r = (xx_snes_spc *)xx_mem_alloc(sizeof(*r));
    if (r) xx_snes_spc_init(r, d, b);
    return r;
}
void xx_snes_spc_destroy(xx_snes_spc *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_snes_spc_free(xx_snes_spc *r)
{
    if (r) {
        xx_snes_spc_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_snes_spc_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_snes_spc_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
