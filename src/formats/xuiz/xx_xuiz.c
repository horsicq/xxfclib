/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Xbox XUIZ resource archive.
 * https://github.com/rene0/xbox360/blob/master/extract360.py
 */
#include "xxfclib/formats/xuiz/xx_xuiz.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

static bool xuiz_utf16be(const uint8_t *b, size_t units, char *out)
{
    size_t i, p = 0;
    for (i = 0; i < units; ++i) {
        uint32_t c = xgr_be16(b + 2 * i);
        if (!c) return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            uint32_t d;
            if (++i >= units || (d = xgr_be16(b + 2 * i)) < 0xdc00 || d > 0xdfff) return false;
            c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
        } else if (c >= 0xdc00 && c <= 0xdfff) return false;
        if (c < 0x80) out[p++] = (char)c;
        else if (c < 0x800) {
            out[p++] = (char)(0xc0 | (c >> 6));
            out[p++] = (char)(0x80 | (c & 63));
        } else if (c < 0x10000) {
            out[p++] = (char)(0xe0 | (c >> 12));
            out[p++] = (char)(0x80 | ((c >> 6) & 63));
            out[p++] = (char)(0x80 | (c & 63));
        } else {
            out[p++] = (char)(0xf0 | (c >> 18));
            out[p++] = (char)(0x80 | ((c >> 12) & 63));
            out[p++] = (char)(0x80 | ((c >> 6) & 63));
            out[p++] = (char)(0x80 | (c & 63));
        }
    }
    out[p] = 0;
    return true;
}
static bool xuiz_xuiz_table(Abstractformat *f, pm_stream *s, xx_pd_struct *pd, uint64_t table_end, uint32_t count, bool extended, bool emit)
{
    uint64_t p = 22, n = (uint64_t)pm_available(f);
    uint32_t i;
    for (i = 0; i < count; ++i) {
        uint8_t h[19], raw[510];
        char name[1021];
        uint64_t size, at;
        size_t units, extra = extended ? 10U : 0U;
        if (xgr_stop(pd) || !xgr_range(p, extra + 9, table_end) || !pm_read(f, (int64_t)p, h, extra + 9)) return false;
        size = xgr_be32(h + extra);
        at = xgr_be32(h + extra + 4);
        units = h[extra + 8];
        p += extra + 9;
        if (!units || !xgr_range(p, units * 2, table_end) || !pm_read(f, (int64_t)p, raw, units * 2) || !xuiz_utf16be(raw, units, name)) return false;
        p += units * 2;
        if (!xgr_name(name) || !xgr_range(table_end + at, size, n)) return false;
        if (emit && !xgr_add(f, s, name, table_end + at, size)) return false;
    }
    return p == table_end;
}
static bool xuiz_xuiz(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[22];
    uint64_t n = (uint64_t)pm_available(f), end;
    uint32_t count;
    bool short_ok, long_ok;
    if (n < 22 || !pm_read(f, 0, h, 22) || memcmp(h, "XUIZ", 4) || xgr_be32(h + 4) != 1 || xgr_be32(h + 8) != n) return false;
    end = 22ULL + xgr_be32(h + 16);
    count = xgr_be16(h + 20);
    if (!count || count > XGR_MAX_RECORDS || end > n) return false;
    short_ok = xuiz_xuiz_table(f, s, pd, end, count, false, false);
    long_ok = xuiz_xuiz_table(f, s, pd, end, count, true, false);
    if (short_ok == long_ok || !xuiz_xuiz_table(f, s, pd, end, count, long_ok, true)) return false;
    s->size = (int64_t)n;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return pm_available(f) >= 0 && !xgr_stop(pd) && xuiz_xuiz(f, s, pd);
}
Abstractformat *xx_xuiz_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (!f) return NULL;
    xx_mem_zero(f, sizeof(*f));
    pm_init(f, d, base, XX_FILE_TYPE_XUIZ, "xzp");
    return f;
}
void xx_xuiz_free(Abstractformat *f)
{
    if (f) {
        xx_format_cleanup_extra_parameters(f);
        xx_mem_free(f);
    }
}
xx_file_type_t xx_xuiz_detect(xx_io_device *d, int64_t base)
{
    Abstractformat f;
    uint8_t h[64];
    int64_t size, old = xx_io_tell(d);
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f, sizeof(f));
    f.device = d;
    f.base_address = base;
    size = pm_available(&f);
    if (size < 8 || !pm_read(&f, 0, h, (size_t)(size < 64 ? size : 64))) goto done;
    {
        uint32_t magic = xgr_le32(h);
        if (magic == 0x5a495558U) type = XX_FILE_TYPE_XUIZ;
    }
done:
    if (old >= 0) xx_io_seek64(d, old, SEEK_SET);
    return type;
}
static Abstractformat *xuiz_open(xx_io_device *d)
{
    return xx_xuiz_create(d, 0);
}
static const xx_file_type_t xuiz_types[] = {XX_FILE_TYPE_XUIZ};
static const xx_format_search_desc xuiz_desc = {xuiz_types, 1, NULL, 0, xuiz_open, xx_xuiz_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(xuiz, xuiz_desc)
