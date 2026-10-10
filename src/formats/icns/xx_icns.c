/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from:
 * https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Headers/IconsCore.h
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/icns/xx_icns.h"
#include "xxfclib/formats/png/xx_png.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool in_rle(Abstractformat *f, int64_t at, int64_t end, uint32_t pixels)
{
    unsigned channel;
    for (channel = 0; channel < 3; ++channel) {
        uint32_t done = 0;
        while (done < pixels) {
            uint8_t b;
            uint32_t n;
            if (at >= end || !pm_read(f, at++, &b, 1)) {
                return false;
            }
            n = b >= 128 ? (uint32_t)b - 125U : (uint32_t)b + 1U;
            if (n > pixels - done || (b >= 128 ? 1U : n) > (uint64_t)(end - at)) {
                return false;
            }
            at += b >= 128 ? 1 : n;
            done += n;
        }
    }
    return at == end;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[8];
    int64_t at = 8, end;
    unsigned count = 0, icons = 0;
    if (!pm_read(f, 0, h, 8) || xx_rt_memcmp(h, "icns", 4) || (end = xx_data_get_u32(h + 4, 4, 0, true)) < 16 || end > pm_available(f)) return false;
    while (at < end) {
        uint32_t n, pixels = 0, raw = 0;
        int64_t data;
        char name[40];
        if ((pd && xx_pd_is_stopped(pd)) || ++count > 4096 || end - at < 8 || !pm_read(f, at, h, 8) || (n = xx_data_get_u32(h + 4, 4, 0, true)) < 8 ||
            n > (uint64_t)(end - at)) {
            return false;
        }
        data = at + 8;
        if (!xx_rt_memcmp(h, "is32", 4)) pixels = 256;
        else if (!xx_rt_memcmp(h, "il32", 4)) pixels = 1024;
        else if (!xx_rt_memcmp(h, "ih32", 4)) pixels = 2304;
        else if (!xx_rt_memcmp(h, "it32", 4)) pixels = 16384;
        if (!xx_rt_memcmp(h, "s8mk", 4)) raw = 256;
        else if (!xx_rt_memcmp(h, "l8mk", 4)) raw = 1024;
        else if (!xx_rt_memcmp(h, "h8mk", 4)) raw = 2304;
        else if (!xx_rt_memcmp(h, "t8mk", 4)) raw = 16384;
        else if (!xx_rt_memcmp(h, "ICN#", 4)) raw = 256;
        else if (!xx_rt_memcmp(h, "icn4", 4)) raw = 512;
        else if (!xx_rt_memcmp(h, "icn8", 4)) raw = 1024;
        else if (!xx_rt_memcmp(h, "ics#", 4)) raw = 64;
        else if (!xx_rt_memcmp(h, "ics4", 4)) raw = 128;
        else if (!xx_rt_memcmp(h, "ics8", 4)) raw = 256;
        else if (!xx_rt_memcmp(h, "ich#", 4)) raw = 576;
        else if (!xx_rt_memcmp(h, "ich4", 4)) raw = 1152;
        else if (!xx_rt_memcmp(h, "ich8", 4)) raw = 2304;
        if (raw && n - 8 != raw) return false;
        if (pixels && n - 8 != pixels * 4U) {
            int64_t begin = data;
            if (pixels == 16384) {
                uint8_t pad[4];
                if (n < 12 || !pm_read(f, begin, pad, 4) || xx_data_get_u32(pad, 4, 0, true)) return false;
                begin += 4;
            }
            if (!in_rle(f, begin, at + n, pixels)) return false;
        }
        if (h[0] == 'i' && h[1] == 'c' && h[2] >= '0' && h[2] <= '1') {
            uint8_t sig[8];
            if (n < 16 || !pm_read(f, data, sig, 8)) return false;
            if (!xx_rt_memcmp(sig, "\x89PNG\r\n\x1A\n", 8)) {
                xx_png png;
                bool valid;
                xx_png_init(&png, f->device, f->base_address + data);
                valid = xx_png_handle_base_info(&png.format, pd) && png.format.format_size == n - 8;
                xx_png_destroy(&png);
                if (!valid) return false;
            }
        }
        if (raw || pixels || (h[0] == 'i' && h[1] == 'c')) ++icons;
        xx_rt_snprintf(name, sizeof(name), "element-%08X.bin", xx_data_get_u32(h, 4, 0, true));
        if (!pm_add(f, s, name, data, n - 8)) return false;
        at += n;
    }
    s->size = end;
    return at == end && icons > 0;
}

void xx_icns_init(xx_icns *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ICNS, "icns");
    }
}
xx_icns *xx_icns_create(xx_io_device *d, int64_t b)
{
    xx_icns *r = (xx_icns *)xx_mem_alloc(sizeof(*r));
    if (r) xx_icns_init(r, d, b);
    return r;
}
void xx_icns_destroy(xx_icns *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_icns_free(xx_icns *r)
{
    if (r) {
        xx_icns_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_icns_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_icns_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
