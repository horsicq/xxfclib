/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://gota7.github.io/Citric-Composer/specs/common.html
 * NintendoWare endian-aware bounded section extraction only. Exports complete encoded INFO/SEEK/DATA sections; no audio decoding or Camelot malformed-size variants.
 */
#include "xxfclib/formats/nintendo_bcstm/xx_nintendo_bcstm.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint16_t r16(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false);
}
static uint32_t r32(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false);
}
static XXFC_MAYBE_UNUSED uint64_t r64(const uint8_t *p, bool be)
{
    return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true) << 32) | xx_data_get_u32(p + 4, 4, 0, true)
              : ((uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32) | xx_data_get_u32(p, 4, 0, false);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[20], e[12], b[8];
    bool be;
    uint32_t total, header, count, i, seen = 0;
    int64_t previous = 0;
    if (!pm_read(f, 0, h, 20) || xx_rt_memcmp(h, "CSTM", 4)) return false;
    be = h[4] == 0xfe && h[5] == 0xff;
    if (!be && !(h[4] == 0xff && h[5] == 0xfe)) return false;
    header = r16(h + 6, be);
    total = r32(h + 12, be);
    count = r16(h + 16, be);
    if (count < 2 || count > 3 || header < 20 + count * 12 || (header & 31) || header > total || total > pm_available(f) || r16(h + 18, be)) return false;
    for (i = 0; i < count; ++i) {
        uint32_t off, size, kind;
        const char *label, *sig;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 20 + (int64_t)i * 12, e, 12)) return false;
        kind = r16(e, be);
        off = r32(e + 4, be);
        size = r32(e + 8, be);
        if (kind == 0x4000) {
            label = "info.bin";
            sig = "INFO";
        } else if (kind == 0x4001) {
            label = "seek.bin";
            sig = "SEEK";
        } else if (kind == 0x4002) {
            label = "data.bin";
            sig = "DATA";
        } else return false;
        if ((seen & (1U << (kind - 0x4000))) || r16(e + 2, be) || off < header || off < (uint64_t)previous || off > total || size < 8 || size > total - off ||
            !pm_read(f, off, b, 8) || xx_rt_memcmp(b, sig, 4) || r32(b + 4, be) != size)
            return false;
        seen |= 1U << (kind - 0x4000);
        previous = (int64_t)off + size;
        if (!pm_add(f, s, label, off, size)) return false;
    }
    if ((seen & 5) != 5) return false;
    s->size = total;
    return true;
}

void xx_nintendo_bcstm_init(xx_nintendo_bcstm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_BCSTM, "bcstm");
    }
}
xx_nintendo_bcstm *xx_nintendo_bcstm_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_bcstm *r = (xx_nintendo_bcstm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_bcstm_init(r, d, b);
    return r;
}
void xx_nintendo_bcstm_destroy(xx_nintendo_bcstm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_bcstm_free(xx_nintendo_bcstm *r)
{
    if (r) {
        xx_nintendo_bcstm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_bcstm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_bcstm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
