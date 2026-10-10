/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/SciresM/hactool/blob/master/hfs0.h
 * Stored partition files with bounded string/range tables and numeric names. SHA-256 hashed prefix verified; suffix-dependent gamecard variants are rejected.
 */
#include "xxfclib/formats/nintendo_hfs0/xx_nintendo_hfs0.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false);
}
static XXFC_MAYBE_UNUSED uint32_t r32(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false);
}
static uint64_t r64(const uint8_t *p, bool be)
{
    return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true) << 32) | xx_data_get_u32(p + 4, 4, 0, true)
              : ((uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32) | xx_data_get_u32(p, 4, 0, false);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[16], e[64], c;
    uint32_t count, strings, i;
    uint64_t table, data, end;
    if (!pm_read(f, 0, h, 16) || xx_rt_memcmp(h, "HFS0", 4) || xx_data_get_u32(h + 12, 4, 0, false)) return false;
    count = xx_data_get_u32(h + 4, 4, 0, false);
    strings = xx_data_get_u32(h + 8, 4, 0, false);
    if (count > 65536 || strings > 16U * 1024U * 1024U) return false;
    table = 16 + (uint64_t)count * 64;
    data = table + strings;
    if (data > (uint64_t)pm_available(f) || (count && !strings)) {
        return false;
    }
    end = data;
    for (i = 0; i < count; ++i) {
        uint64_t off, size, j;
        uint32_t n;
        bool ended = false;
        char label[40];
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 16 + (int64_t)i * 64, e, 64)) return false;
        off = r64(e, false);
        size = r64(e + 8, false);
        n = xx_data_get_u32(e + 16, 4, 0, false);
        if (n >= strings || off > (uint64_t)pm_available(f) - data || size > (uint64_t)pm_available(f) - data - off) return false;
        for (j = n; j < strings; ++j) {
            if (!pm_read(f, (int64_t)(table + j), &c, 1)) return false;
            if (!c) {
                ended = true;
                break;
            }
        }
        if (!ended) return false;

        {
            uint32_t hashed = xx_data_get_u32(e + 20, 4, 0, false);
            uint8_t digest[32];
            if (r64(e + 24, false) || hashed > size || !xx_hash_device(XX_HASH_SHA256, f->device, f->base_address + (int64_t)(data + off), hashed, digest, 32, pd) ||
                !xx_hash_equal(digest, e + 32, 32))
                return false;
        }

        xx_rt_snprintf(label, sizeof(label), "file-%u.bin", (unsigned)i);
        if (!pm_add(f, s, label, (int64_t)(data + off), (int64_t)size)) return false;
        if (data + off + size > end) end = data + off + size;
    }
    s->size = (int64_t)end;
    return true;
}

void xx_nintendo_hfs0_init(xx_nintendo_hfs0 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_HFS0, "hfs0");
    }
}
xx_nintendo_hfs0 *xx_nintendo_hfs0_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_hfs0 *r = (xx_nintendo_hfs0 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_hfs0_init(r, d, b);
    return r;
}
void xx_nintendo_hfs0_destroy(xx_nintendo_hfs0 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_hfs0_free(xx_nintendo_hfs0 *r)
{
    if (r) {
        xx_nintendo_hfs0_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_hfs0_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_hfs0_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
