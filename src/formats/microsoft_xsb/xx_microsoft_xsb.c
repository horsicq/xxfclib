/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FNA-XNA/FAudio/master/src/FACT_internal.c
 * Little-endian Windows XACT content versions43-46/tool43, single-sound simple cues and12-byte simple sound entries, up to1024 each and32 wave-bank names. Validates
 * cue-to-sound and sound-to-bank references. Exports encoded cue/sound/name records; complex cues/tracks/RPC/DSP, cue-name/hash tables and audio execution unsupported.
 */
#include "xxfclib/formats/microsoft_xsb/xx_microsoft_xsb.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t g16(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false);
}
static XXFC_MAYBE_UNUSED uint32_t g32(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false);
}
static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool zname(Abstractformat *f, uint64_t at, uint64_t end, bool empty)
{
    uint8_t c;
    uint64_t i;
    if (at >= end || end > (uint64_t)pm_available(f)) return false;
    for (i = 0; i < 4096 && at + i < end; ++i) {
        if (!pm_read(f, (int64_t)(at + i), &c, 1)) return false;
        if (!c) return empty || i != 0;
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool bom(const uint8_t *p, bool *be)
{
    *be = p[0] == 0xfe && p[1] == 0xff;
    return *be || (p[0] == 0xff && p[1] == 0xfe);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[138], p[12], cue[5];
    uint32_t cues, sounds, banks, co, so, bo, i, j;
    uint64_t total = 138;
    char label[40];
    if (!pm_read(f, 0, h, 138) || xx_rt_memcmp(h, "SDBK", 4) || xx_data_get_u16(h + 4, 2, 0, false) < 43 || xx_data_get_u16(h + 4, 2, 0, false) > 46 ||
        xx_data_get_u16(h + 6, 2, 0, false) != 43 || h[18] != 1)
        return false;
    cues = xx_data_get_u16(h + 19, 2, 0, false);
    banks = h[27];
    sounds = xx_data_get_u16(h + 28, 2, 0, false);
    co = xx_data_get_u32(h + 34, 4, 0, false);
    bo = xx_data_get_u32(h + 58, 4, 0, false);
    so = xx_data_get_u32(h + 70, 4, 0, false);
    if (!cues || cues > 1024 || xx_data_get_u16(h + 21, 2, 0, false) || !sounds || sounds > 1024 || !banks || banks > 32 || xx_data_get_u16(h + 30, 2, 0, false) ||
        !zname(f, 74, 138, false))
        return false;
    for (i = 0; i < 7; ++i) {
        static const unsigned fields[] = {38, 42, 46, 50, 54, 62, 66};
        if (xx_data_get_u32(h + fields[i], 4, 0, false) != UINT32_MAX) return false;
    }
    {
        uint64_t at[3] = {co, so, bo}, n[3] = {(uint64_t)cues * 5, (uint64_t)sounds * 12, (uint64_t)banks * 64};
        for (i = 0; i < 3; ++i) {
            if (at[i] < 138 || !span(at[i], n[i], (uint64_t)pm_available(f))) return false;
            for (j = 0; j < i; ++j)
                if (overlap(at[i], n[i], at[j], n[j])) return false;
            if (at[i] + n[i] > total) total = at[i] + n[i];
        }
    }
    for (i = 0; i < banks; ++i) {
        if ((pd && xx_pd_is_stopped(pd)) || !zname(f, (uint64_t)bo + i * 64, (uint64_t)bo + (i + 1) * 64, false)) return false;
        xx_rt_snprintf(label, sizeof(label), "wave-bank-%u.name", i);
        if (!emit(f, s, label, (uint64_t)bo + i * 64, 64, total)) return false;
    }
    for (i = 0; i < sounds; ++i) {
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)so + i * 12, p, 12) || p[0] || xx_data_get_u16(p + 7, 2, 0, false) != 12 || p[11] >= banks) return false;
        xx_rt_snprintf(label, sizeof(label), "sound-%u.bin", i);
        if (!emit(f, s, label, (uint64_t)so + i * 12, 12, total)) return false;
    }
    for (i = 0; i < cues; ++i) {
        uint32_t reference;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)co + i * 5, cue, 5) || cue[0] != 4) return false;
        reference = xx_data_get_u32(cue + 1, 4, 0, false);
        if (reference < so || (reference - so) % 12 || (reference - so) / 12 >= sounds) return false;
        xx_rt_snprintf(label, sizeof(label), "cue-%u.bin", i);
        if (!emit(f, s, label, (uint64_t)co + i * 5, 5, total)) return false;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_microsoft_xsb_init(xx_microsoft_xsb *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MICROSOFT_XSB, "xsb");
    }
}
xx_microsoft_xsb *xx_microsoft_xsb_create(xx_io_device *d, int64_t b)
{
    xx_microsoft_xsb *r = (xx_microsoft_xsb *)xx_mem_alloc(sizeof(*r));
    if (r) xx_microsoft_xsb_init(r, d, b);
    return r;
}
void xx_microsoft_xsb_destroy(xx_microsoft_xsb *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_microsoft_xsb_free(xx_microsoft_xsb *r)
{
    if (r) {
        xx_microsoft_xsb_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_microsoft_xsb_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_microsoft_xsb_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
