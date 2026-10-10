/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/gildor2/UEViewer/master/Unreal/UnrealPackage/UnPackage2.cpp
 * Unreal Engine1 versions61-63, licensee0, little endian, uncompressed export objects. Validates compact indices, name/import/export/heritage tables and object ranges.
 * Later UE versions, game-specific encryption/compression and object decoding/execution unsupported.
 */
#include "xxfclib/formats/unreal_package/xx_unreal_package.h"
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

static bool compact(Abstractformat *f, uint64_t *at, uint64_t end, int32_t *v)
{
    uint8_t b;
    uint64_t n;
    unsigned i = 0, shift = 6;
    bool negative, more;
    if (!span(*at, 1, end) || !pm_read(f, (int64_t)(*at)++, &b, 1)) return false;
    negative = (b & 128) != 0;
    more = (b & 64) != 0;
    n = b & 63;
    while (more) {
        if (++i > 4 || !span(*at, 1, end) || !pm_read(f, (int64_t)(*at)++, &b, 1)) return false;
        more = (b & 128) != 0;
        n |= (uint64_t)(b & 127) << shift;
        shift += 7;
        if (!more && !(b & 127)) return false;
    }
    if (n > INT32_MAX || (!n && negative)) {
        return false;
    }
    *v = negative ? -(int32_t)n : (int32_t)n;
    return true;
}
static bool objectindex(int32_t v, uint32_t exports, uint32_t imports)
{
    return v >= 0 ? (uint32_t)v <= exports : (uint32_t)(-(int64_t)v) <= imports;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[44], b[4], c;
    uint32_t names, imports, exports, no, io, eo, heritage, ho, i;
    uint64_t total = (uint64_t)pm_available(f), ne, ie, ee, end;
    int32_t v;
    if (!pm_read(f, 0, h, 44) || xx_data_get_u32(h, 4, 0, false) != 0x9e2a83c1 || xx_data_get_u16(h + 4, 2, 0, false) < 61 || xx_data_get_u16(h + 4, 2, 0, false) > 63 ||
        xx_data_get_u16(h + 6, 2, 0, false))
        return false;
    names = xx_data_get_u32(h + 12, 4, 0, false);
    no = xx_data_get_u32(h + 16, 4, 0, false);
    exports = xx_data_get_u32(h + 20, 4, 0, false);
    eo = xx_data_get_u32(h + 24, 4, 0, false);
    imports = xx_data_get_u32(h + 28, 4, 0, false);
    io = xx_data_get_u32(h + 32, 4, 0, false);
    heritage = xx_data_get_u32(h + 36, 4, 0, false);
    ho = xx_data_get_u32(h + 40, 4, 0, false);
    if (!names || !exports || names > 4096 || exports > 4096 || imports > 4096 || heritage > 4096 || no < 44 || eo < 44 || (imports && io < 44) ||
        (heritage && ho < 44) || !span(ho, (uint64_t)heritage * 16, total))
        return false;
    ne = no;
    for (i = 0; i < names; ++i) {
        unsigned j;
        if (pd && xx_pd_is_stopped(pd)) return false;
        for (j = 0; j < 4096; ++j) {
            if (!span(ne, 1, total) || !pm_read(f, (int64_t)ne++, &c, 1)) return false;
            if (!c) break;
        }
        if (!j || j == 4096 || !span(ne, 4, total) || !pm_read(f, (int64_t)ne, b, 4)) {
            return false;
        }
        ne += 4;
    }
    ie = io;
    for (i = 0; i < imports; ++i) {
        if ((pd && xx_pd_is_stopped(pd)) || !compact(f, &ie, total, &v) || v < 0 || (uint32_t)v >= names || !compact(f, &ie, total, &v) || v < 0 ||
            (uint32_t)v >= names || !span(ie, 4, total) || !pm_read(f, (int64_t)ie, b, 4))
            return false;
        v = (int32_t)xx_data_get_u32(b, 4, 0, false);
        ie += 4;
        if (!objectindex(v, exports, imports) || !compact(f, &ie, total, &v) || v < 0 || (uint32_t)v >= names) return false;
    }
    ee = eo;
    end = ne;
    if (imports && ie > end) end = ie;
    if (heritage && (uint64_t)ho + heritage * 16 > end) end = (uint64_t)ho + heritage * 16;
    for (i = 0; i < exports; ++i) {
        int32_t size, offset = 0;
        char label[40];
        if ((pd && xx_pd_is_stopped(pd)) || !compact(f, &ee, total, &v) || !objectindex(v, exports, imports) || !compact(f, &ee, total, &v) ||
            !objectindex(v, exports, imports) || !span(ee, 4, total) || !pm_read(f, (int64_t)ee, b, 4))
            return false;
        v = (int32_t)xx_data_get_u32(b, 4, 0, false);
        ee += 4;
        if (!objectindex(v, exports, imports) || !compact(f, &ee, total, &v) || v < 0 || (uint32_t)v >= names || !span(ee, 4, total)) return false;
        ee += 4;
        if (!compact(f, &ee, total, &size) || size < 0 || (size && (!compact(f, &ee, total, &offset) || offset < 44))) return false;
        xx_rt_snprintf(label, sizeof(label), "object-%u.bin", i);
        if (!emit(f, s, label, (uint64_t)offset, (uint64_t)size, total)) return false;
        if ((uint64_t)offset + size > end) end = (uint64_t)offset + size;
    }
    if (ee > end) end = ee;
    {
        uint64_t starts[4] = {no, io, eo, ho}, lengths[4] = {ne - no, imports ? ie - io : 0, ee - eo, (uint64_t)heritage * 16};
        unsigned a, j;
        for (a = 0; a < 4; ++a) {
            if (!lengths[a]) continue;
            for (j = 0; j < a; ++j)
                if (lengths[j] && starts[a] < starts[j] + lengths[j] && starts[j] < starts[a] + lengths[a]) return false;
            for (j = 0; j < s->count; ++j) {
                uint64_t at = (uint64_t)(s->items[j].offset - f->base_address), n = (uint64_t)s->items[j].size;
                if (n && at < starts[a] + lengths[a] && starts[a] < at + n) return false;
            }
        }
    }
    s->size = (int64_t)end;
    return true;
}

void xx_unreal_package_init(xx_unreal_package *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_UNREAL_PACKAGE, "u");
    }
}
xx_unreal_package *xx_unreal_package_create(xx_io_device *d, int64_t b)
{
    xx_unreal_package *r = (xx_unreal_package *)xx_mem_alloc(sizeof(*r));
    if (r) xx_unreal_package_init(r, d, b);
    return r;
}
void xx_unreal_package_destroy(xx_unreal_package *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_unreal_package_free(xx_unreal_package *r)
{
    if (r) {
        xx_unreal_package_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_unreal_package_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_unreal_package_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
