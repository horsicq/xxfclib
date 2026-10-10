/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/rehlds/ReHLDS/master/rehlds/engine/hashpak.h
 * GoldSrc HPAK version1 32-bit on-disk resource records, stored lumps. Checks member MD5 and resource sizes. 64-bit native-struct variants rejected; original paths
 * become numeric safe names.
 */
#include "xxfclib/formats/valve_hpak/xx_valve_hpak.h"
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

#include "xxfclib/algo/hash/xx_hash.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[12], b[4], e[144], digest[16];
    uint32_t dir, count, i;
    uint64_t total = (uint64_t)pm_available(f), end;
    if (!pm_read(f, 0, h, 12) || xx_rt_memcmp(h, "HPAK", 4) || xx_data_get_u32(h + 4, 4, 0, false) != 1) return false;
    dir = xx_data_get_u32(h + 8, 4, 0, false);
    if (dir < 12 || !pm_read(f, dir, b, 4)) return false;
    count = xx_data_get_u32(b, 4, 0, false);
    if (!count || count > 4096 || !span((uint64_t)dir + 4, (uint64_t)count * 144, total)) {
        return false;
    }
    end = (uint64_t)dir + 4 + (uint64_t)count * 144;
    for (i = 0; i < count; ++i) {
        uint64_t at, n;
        unsigned j;
        char label[40];
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)dir + 4 + (int64_t)i * 144, e, 144)) return false;
        for (j = 0; j < 64 && e[j]; ++j) {
        }
        if (!j || j == 64 || xx_data_get_u32(e + 64, 4, 0, false) > 6) return false;
        at = xx_data_get_u32(e + 136, 4, 0, false);
        n = xx_data_get_u32(e + 140, 4, 0, false);
        if (at < 12 || !n || n >= 0x20000 || xx_data_get_u32(e + 72, 4, 0, false) != n || !span(at, n, dir)) return false;
        if (!xx_hash_device(XX_HASH_MD5, f->device, f->base_address + (int64_t)at, (int64_t)n, digest, 16, pd) || !xx_hash_equal(digest, e + 77, 16)) return false;
        xx_rt_snprintf(label, sizeof(label), "resource-%u.bin", i);
        if (!emit(f, s, label, at, n, end)) return false;
    }
    s->size = (int64_t)end;
    return true;
}

void xx_valve_hpak_init(xx_valve_hpak *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_VALVE_HPAK, "hpk");
    }
}
xx_valve_hpak *xx_valve_hpak_create(xx_io_device *d, int64_t b)
{
    xx_valve_hpak *r = (xx_valve_hpak *)xx_mem_alloc(sizeof(*r));
    if (r) xx_valve_hpak_init(r, d, b);
    return r;
}
void xx_valve_hpak_destroy(xx_valve_hpak *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_valve_hpak_free(xx_valve_hpak *r)
{
    if (r) {
        xx_valve_hpak_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_valve_hpak_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_valve_hpak_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
