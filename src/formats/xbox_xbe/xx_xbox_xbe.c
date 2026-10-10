/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xemu-project/xemu/master/xemu-xbe.h
 * Original Xbox executable stored raw sections only; does not execute, relocate or verify executable signatures or section SHA-1 digests.
 */
#include "xxfclib/formats/xbox_xbe/xx_xbox_xbe.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false);
}
static XXFC_MAYBE_UNUSED uint32_t r32(const uint8_t *p, bool be)
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
    uint8_t h[0x178], e[56];
    uint32_t base, headers, count, table, i;
    int64_t end;
    if (!pm_read(f, 0, h, sizeof(h)) || xx_rt_memcmp(h, "XBEH", 4)) return false;
    base = xx_data_get_u32(h + 0x104, 4, 0, false);
    headers = xx_data_get_u32(h + 0x108, 4, 0, false);
    count = xx_data_get_u32(h + 0x11c, 4, 0, false);
    table = xx_data_get_u32(h + 0x120, 4, 0, false);
    if (!base || headers < sizeof(h) || headers > pm_available(f) || xx_data_get_u32(h + 0x110, 4, 0, false) < sizeof(h) ||
        xx_data_get_u32(h + 0x110, 4, 0, false) > headers || !count || count > 4096 || table < base || table - base > headers ||
        (uint64_t)count * 56 > headers - (table - base))
        return false;
    end = headers;
    for (i = 0; i < count; ++i) {
        uint32_t off, size, virtual_size;
        char label[40];
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, table - base + (int64_t)i * 56, e, 56)) return false;
        virtual_size = xx_data_get_u32(e + 8, 4, 0, false);
        off = xx_data_get_u32(e + 12, 4, 0, false);
        size = xx_data_get_u32(e + 16, 4, 0, false);
        if (size > virtual_size || off < headers || size > (uint64_t)pm_available(f) - off || off > pm_available(f)) return false;
        xx_rt_snprintf(label, sizeof(label), "section-%u.bin", (unsigned)i);
        if (!pm_add(f, s, label, off, size)) {
            return false;
        }
        if ((int64_t)off + size > end) end = (int64_t)off + size;
    }
    s->size = end;
    return true;
}

void xx_xbox_xbe_init(xx_xbox_xbe *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_XBOX_XBE, "xbe");
    }
}
xx_xbox_xbe *xx_xbox_xbe_create(xx_io_device *d, int64_t b)
{
    xx_xbox_xbe *r = (xx_xbox_xbe *)xx_mem_alloc(sizeof(*r));
    if (r) xx_xbox_xbe_init(r, d, b);
    return r;
}
void xx_xbox_xbe_destroy(xx_xbox_xbe *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_xbox_xbe_free(xx_xbox_xbe *r)
{
    if (r) {
        xx_xbox_xbe_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_xbox_xbe_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_xbox_xbe_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
