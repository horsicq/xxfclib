/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/vgmstream/vgmstream/master/src/meta/vag.c
 * Standard big-endian mono VAGp version0x20 with 48-byte header. Exports encoded PS-ADPCM frames after validating frame predictor/shift/flag fields.
 * Stereo/interleaved/game-specific headers and PCM decoding unsupported.
 */
#include "xxfclib/formats/sony_vag/xx_sony_vag.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p, bool be)
{
    return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true) << 32) | xx_data_get_u32(p + 4, 4, 0, true)
              : ((uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32) | xx_data_get_u32(p, 4, 0, false);
}
static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i) {
        uint64_t a = (uint64_t)(s->items[i].offset - f->base_address), b = (uint64_t)s->items[i].size;
        if (n && b && at < a + b && a < at + n) return false;
    }
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f, uint64_t at, uint64_t end)
{
    uint8_t c;
    uint64_t i;
    if (at >= end || end > (uint64_t)pm_available(f)) return false;
    for (i = 0; i < 4096 && at + i < end; ++i) {
        if (!pm_read(f, (int64_t)(at + i), &c, 1)) return false;
        if (!c) return i != 0;
    }
    return false;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[48], b[16];
    uint32_t n, rate, i;
    if (!pm_read(f, 0, h, 48) || xx_rt_memcmp(h, "VAGp", 4) || xx_data_get_u32(h + 4, 4, 0, true) != 0x20 || xx_data_get_u32(h + 8, 4, 0, true)) return false;
    n = xx_data_get_u32(h + 12, 4, 0, true);
    rate = xx_data_get_u32(h + 16, 4, 0, true);
    if (!n || n % 16 || n > 256U * 1024U * 1024U || rate < 1000 || rate > 192000 || !span(48, n, (uint64_t)pm_available(f))) return false;
    for (i = 20; i < 32; ++i)
        if (h[i]) return false;
    for (i = 0; i < n; i += 16) {
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 48 + (int64_t)i, b, 16) || b[0] >> 4 > 4 || (b[0] & 15) > 12 || b[1] > 7) return false;
    }
    s->size = 48 + (int64_t)n;
    return emit(f, s, "adpcm.bin", 48, n, (uint64_t)s->size);
}

void xx_sony_vag_init(xx_sony_vag *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SONY_VAG, "vag");
    }
}
xx_sony_vag *xx_sony_vag_create(xx_io_device *d, int64_t b)
{
    xx_sony_vag *r = (xx_sony_vag *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sony_vag_init(r, d, b);
    return r;
}
void xx_sony_vag_destroy(xx_sony_vag *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sony_vag_free(xx_sony_vag *r)
{
    if (r) {
        xx_sony_vag_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sony_vag_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sony_vag_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
