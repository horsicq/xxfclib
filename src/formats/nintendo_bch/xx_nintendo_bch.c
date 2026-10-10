/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/gdkchan/SPICA/master/SPICA/Formats/CtrH3D/H3DHeader.cs
 * 3DS BCH compatibility0x21 with six bounded nonoverlapping graphics sections and up to4096 checked relocation entries. Exports encoded
 * contents/string/command/raw/relocation sections; runtime-initialized files, older compatibility levels, pointer fixups, GPU decoding and rendering unsupported.
 * Uninitialized-storage counts are preserved as metadata and never allocated.
 */
#include "xxfclib/formats/nintendo_bch/xx_nintendo_bch.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[68], p[4];
    uint32_t offs[6], lens[6], locations[4096], i, j;
    uint64_t total = 68;
    const char *names[] = {"contents.bin", "strings.bin", "commands.bin", "raw-data.bin", "raw-extra.bin", "relocations.bin"};
    static const uint8_t sections[] = {0, 1, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4};
    if (!pm_read(f, 0, h, 68) || xx_rt_memcmp(h, "BCH\0", 4) || h[4] != 0x21 || h[5] != 0x21 || (h[64] & ~5U) || h[65] ||
        xx_data_get_u32(h + 56, 4, 0, false) != (uint32_t)xx_data_get_u16(h + 66, 2, 0, false) * 4 || (xx_data_get_u32(h + 60, 4, 0, false) & 3))
        return false;
    for (i = 0; i < 6; ++i) {
        offs[i] = xx_data_get_u32(h + 8 + i * 4, 4, 0, false);
        lens[i] = xx_data_get_u32(h + 32 + i * 4, 4, 0, false);
        if (!lens[i]) {
            if (offs[i] && (offs[i] < 68 || offs[i] > (uint64_t)pm_available(f))) return false;
            continue;
        }
        if (offs[i] < 68 || (offs[i] & 3) || !span(offs[i], lens[i], (uint64_t)pm_available(f))) {
            return false;
        }
        for (j = 0; j < i; ++j)
            if (overlap(offs[i], lens[i], offs[j], lens[j])) return false;
        if ((uint64_t)offs[i] + lens[i] > total) total = (uint64_t)offs[i] + lens[i];
    }
    if (!lens[0] || !lens[3] || (lens[5] & 3) || lens[5] > 16384) return false;
    for (i = 0; i < lens[5] / 4; ++i) {
        uint32_t entry, source, target, ptr, relative, location;
        if (stop(pd) || !pm_read(f, offs[5] + (int64_t)i * 4, p, 4)) {
            return false;
        }
        entry = xx_data_get_u32(p, 4, 0, false);
        source = entry >> 29;
        target = (entry >> 25) & 15;
        ptr = entry & 0x1ffffffU;
        if (source > 7 || target >= 14) {
            return false;
        }
        if (target != 1) ptr *= 4;
        source = sections[source];
        target = sections[target];
        if (!span(ptr, 4, lens[source]) || !pm_read(f, offs[source] + (int64_t)ptr, p, 4)) {
            return false;
        }
        relative = xx_data_get_u32(p, 4, 0, false);
        if (relative >= lens[target]) return false;
        location = offs[source] + ptr;
        for (j = 0; j < i; ++j) {
            if (locations[j] == location) return false;
        }
        locations[i] = location;
    }
    for (i = 0; i < 6; ++i) {
        if (lens[i] && (stop(pd) || !emit(f, s, names[i], offs[i], lens[i], total))) return false;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_nintendo_bch_init(xx_nintendo_bch *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_BCH, "bch");
    }
}
xx_nintendo_bch *xx_nintendo_bch_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_bch *r = (xx_nintendo_bch *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_bch_init(r, d, b);
    return r;
}
void xx_nintendo_bch_destroy(xx_nintendo_bch *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_bch_free(xx_nintendo_bch *r)
{
    if (r) {
        xx_nintendo_bch_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_bch_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_bch_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
