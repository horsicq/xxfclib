/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://docs.kernel.org/bpf/btf.html */
#include "xxfclib/formats/linux_btf/xx_linux_btf.h"
#include "../common/xx_container_codec_helpers.h"

static bool bt_name(memory_blob *b, uint64_t strings, uint64_t length, uint32_t offset, bool empty)
{
    uint64_t at = strings + offset;
    if (offset >= length) return false;
    return container_codec_z(b, &at, strings + length, empty);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t h, types, tn, strings, sn, at, end, i, j, tail, count = 0;
    uint32_t *offsets = NULL;
    bool ok = false;
    if (!blob_load(f, &b, pd)) {
        return false;
    }
    BLOB_NEED(blob_span(&b, 0, 24) && xx_data_get_u16(b.p, 2, 0, false) == 0xeb9f && b.p[2] == 1 && !b.p[3]);
    h = xx_data_get_u32(b.p + 4, 4, 0, false);
    types = xx_data_get_u32(b.p + 8, 4, 0, false);
    tn = xx_data_get_u32(b.p + 12, 4, 0, false);
    strings = xx_data_get_u32(b.p + 16, 4, 0, false);
    sn = xx_data_get_u32(b.p + 20, 4, 0, false);
    BLOB_NEED(h >= 24 && h <= 65536 && !types && strings == tn && sn && blob_span(&b, h, tn) && blob_span(&b, h + strings, sn) && h + tn + sn == b.n);
    types += h;
    strings += h;
    BLOB_NEED(!b.p[(size_t)strings] && !b.p[(size_t)(strings + sn - 1)]);
    at = strings;
    while (at < strings + sn) BLOB_NEED(container_codec_z(&b, &at, strings + sn, true));
    offsets = (uint32_t *)xx_mem_alloc((size_t)(tn / 12 + 1) * sizeof(*offsets));
    BLOB_NEED(offsets);
    offsets[0] = 0;
    end = types + tn;
    for (at = types; at < end; at += 12 + tail) {
        uint32_t info, kind, vlen, name;
        BLOB_NEED(blob_span(&b, at, 12) && record_span(at, 12, end) && count < 1000000);
        info = xx_data_get_u32(b.p + (size_t)at + 4, 4, 0, false);
        kind = (info >> 24) & 31;
        vlen = info & 65535;
        name = xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
        BLOB_NEED(!(info & 0x60ff0000U) && kind >= 1 && kind <= 19 && bt_name(&b, strings, sn, name, true));
        offsets[++count] = (uint32_t)at;
        tail = 0;
        if (kind == 1 || kind == 14 || kind == 17) tail = 4;
        else if (kind == 3) tail = 12;
        else if (kind == 4 || kind == 5 || kind == 15 || kind == 19) tail = (uint64_t)vlen * 12;
        else if (kind == 6 || kind == 13) tail = (uint64_t)vlen * 8;
        BLOB_NEED(record_span(at + 12, tail, end));
        if (kind != 4 && kind != 5 && kind != 6 && kind != 13 && kind != 15 && kind != 19 && kind != 12) BLOB_NEED(!vlen);
        if (kind == 12) BLOB_NEED(vlen <= 2);
        if (kind != 4 && kind != 5 && kind != 6 && kind != 7 && kind != 17 && kind != 18 && kind != 19) BLOB_NEED(!(info & 0x80000000U));
    }
    BLOB_NEED(at == end);
    for (i = 1; i <= count; ++i) {
        uint32_t info, kind, vlen, x, name;
        at = offsets[i];
        name = xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
        info = xx_data_get_u32(b.p + (size_t)at + 4, 4, 0, false);
        kind = (info >> 24) & 31;
        vlen = info & 65535;
        x = xx_data_get_u32(b.p + (size_t)at + 8, 4, 0, false);
        BLOB_NEED(!binary_stop(pd));
        if (kind == 2 || (kind >= 8 && kind <= 14) || kind == 17 || kind == 18) BLOB_NEED(x <= count);
        if (kind == 1) {
            uint32_t enc = xx_data_get_u32(b.p + (size_t)at + 12, 4, 0, false);
            BLOB_NEED(x && x <= 16 && !(enc & 0xf800ff00U) && (enc & 255) && (uint64_t)((enc >> 16) & 255) + (enc & 255) <= (uint64_t)x * 8);
        }
        if (kind == 3)
            BLOB_NEED(xx_data_get_u32(b.p + (size_t)at + 12, 4, 0, false) > 0 && xx_data_get_u32(b.p + (size_t)at + 12, 4, 0, false) <= count &&
                      xx_data_get_u32(b.p + (size_t)at + 16, 4, 0, false) > 0 && xx_data_get_u32(b.p + (size_t)at + 16, 4, 0, false) <= count);
        if (kind == 6 || kind == 19) BLOB_NEED(x == 1 || x == 2 || x == 4 || x == 8);
        if (kind == 7) BLOB_NEED(!x && name);
        if (kind == 14) BLOB_NEED(x && xx_data_get_u32(b.p + (size_t)at + 12, 4, 0, false) <= 2);
        if (kind == 16) BLOB_NEED(x == 2 || x == 4 || x == 8 || x == 12 || x == 16);
        for (j = 0; j < vlen; ++j) {
            uint64_t p = at + 12 + j * (kind == 6 || kind == 13 ? 8 : 12);
            if (kind == 4 || kind == 5 || kind == 6 || kind == 13 || kind == 19)
                BLOB_NEED(bt_name(&b, strings, sn, xx_data_get_u32(b.p + (size_t)p, 4, 0, false), kind == 4 || kind == 5 || kind == 13));
            if (kind == 4 || kind == 5 || kind == 13) BLOB_NEED(xx_data_get_u32(b.p + (size_t)p + 4, 4, 0, false) <= count);
            if (kind == 4 || kind == 5) {
                uint32_t bits = xx_data_get_u32(b.p + (size_t)p + 8, 4, 0, false);
                uint64_t bitoff = info & 0x80000000U ? bits & 0xffffffU : bits;
                BLOB_NEED(bitoff <= (uint64_t)x * 8 && (!(info & 0x80000000U) || bitoff + (bits >> 24) <= (uint64_t)x * 8));
            }
            if (kind == 15) {
                uint32_t ref = xx_data_get_u32(b.p + (size_t)p, 4, 0, false);
                uint64_t pos = xx_data_get_u32(b.p + (size_t)p + 4, 4, 0, false), len = xx_data_get_u32(b.p + (size_t)p + 8, 4, 0, false);
                BLOB_NEED(ref && ref <= count && ((xx_data_get_u32(b.p + offsets[ref] + 4, 4, 0, false) >> 24) & 31) == 14 && (!x || record_span(pos, len, x)));
            }
        }
        if (kind == 17) {
            uint32_t component = xx_data_get_u32(b.p + (size_t)at + 12, 4, 0, false), k;
            BLOB_NEED(x && x <= count);
            k = (xx_data_get_u32(b.p + offsets[x] + 4, 4, 0, false) >> 24) & 31;
            BLOB_NEED(k == 4 || k == 5 || k == 8 || k == 12 || k == 14);
            if (component != 0xffffffffU) {
                uint32_t target = x;
                if (k == 12) target = xx_data_get_u32(b.p + offsets[x] + 8, 4, 0, false);
                BLOB_NEED(target && target <= count && component < (xx_data_get_u32(b.p + offsets[target] + 4, 4, 0, false) & 65535));
            }
        }
    }
    BLOB_NEED(blob_add(f, s, &b, "btf-header", 0, h) && blob_add(f, s, &b, "types", types, tn) && blob_add(f, s, &b, "strings", strings, sn));
    s->size = (int64_t)b.n;
    ok = true;
done:
    if (offsets) xx_mem_free(offsets);
    xx_mem_free(b.p);
    return ok;
}
void xx_linux_btf_init(xx_linux_btf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LINUX_BTF, "btf");
    }
}
xx_linux_btf *xx_linux_btf_create(xx_io_device *d, int64_t b)
{
    xx_linux_btf *r = (xx_linux_btf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_linux_btf_init(r, d, b);
    return r;
}
void xx_linux_btf_destroy(xx_linux_btf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_linux_btf_free(xx_linux_btf *r)
{
    if (r) {
        xx_linux_btf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_linux_btf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_linux_btf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
