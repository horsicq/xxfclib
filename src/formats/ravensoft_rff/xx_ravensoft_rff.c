/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/nukeykt/NBlood/blob/master/source/blood/src/resource.h
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/ravensoft_rff/xx_ravensoft_rff.h"
#include "../common/xx_carrier_helpers.h"

static bool carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[48];
    uint16_t version;
    uint32_t table, count, i;
    uint64_t memory = 0;
    int64_t limit = pm_available(f), end;
    if (!pm_read(f, 0, h, 32) || xx_rt_memcmp(h, "RFF\x1a", 4) || ((version = xx_data_get_u16(h + 4, 2, 0, false)) != 0x200 && version != 0x300 && version != 0x301))
        return false;
    table = xx_data_get_u32(h + 8, 4, 0, false);
    count = xx_data_get_u32(h + 12, 4, 0, false);
    if (!count || count > 65536 || table < 32 || !carrier_range(limit, table, (uint64_t)count * 48)) return false;
    end = (int64_t)table + (int64_t)count * 48;
    for (i = 0; i < count; ++i) {
        uint32_t at, bytes;
        unsigned j;
        char name[48];
        if (carrier_stop(pd) || !pm_read(f, table + (int64_t)i * 48, h, 48)) return false;
        if (version >= 0x300) {
            uint16_t key = (uint16_t)(table * (1U + (version & 255U)) + (uint64_t)i * 48);
            for (j = 0; j < 48; ++j, ++key) h[j] ^= (uint8_t)(key >> 1);
        }
        at = xx_data_get_u32(h + 16, 4, 0, false);
        bytes = xx_data_get_u32(h + 20, 4, 0, false);
        if (at < 32 || !carrier_range(limit, at, bytes) || ((uint64_t)at < (uint64_t)table + count * 48ULL && (uint64_t)at + bytes > table) || (h[32] & ~29U) || !h[36])
            return false;
        xx_rt_snprintf(name, sizeof(name), "resource-%u.bin", i);
        if (!pm_add(f, s, name, at, bytes)) return false;
        if (h[32] & 16) {
            uint8_t *data;
            if (bytes > 16777216 || memory + bytes > 67108864) return false;
            memory += bytes;
            data = (uint8_t *)xx_mem_alloc(bytes ? bytes : 1);
            if (!data || !pm_read(f, at, data, bytes)) {
                if (data) xx_mem_free(data);
                return false;
            }
            for (j = 0; j < bytes && j < 256; ++j) {
                data[j] ^= (uint8_t)(j >> 1);
            }
            s->items[s->count - 1].memory = data;
        }
        if ((int64_t)at + bytes > end) end = (int64_t)at + bytes;
    }
    s->size = end;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_ravensoft_rff_init(xx_ravensoft_rff *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_RAVENSOFT_RFF, "rff");
    }
}
xx_ravensoft_rff *xx_ravensoft_rff_create(xx_io_device *d, int64_t b)
{
    xx_ravensoft_rff *r = (xx_ravensoft_rff *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ravensoft_rff_init(r, d, b);
    return r;
}
void xx_ravensoft_rff_destroy(xx_ravensoft_rff *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_ravensoft_rff_free(xx_ravensoft_rff *r)
{
    if (r) {
        xx_ravensoft_rff_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ravensoft_rff_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_ravensoft_rff_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
