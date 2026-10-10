/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/NetBSD/xsrc/trunk/external/mit/xorgproto/dist/include/X11/XWDFile.h
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/xwd/xx_xwd.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[100], name[4096], entry[12];
    uint32_t hs, depth, w, height, bpp, pad, stride, ncolors, visual, rm, gm, bm, i;
    uint64_t raster, expected, end;
    bool nul = false;
    if (!pm_read(f, 0, h, 100) || xx_data_get_u32(h + 4, 4, 0, true) != 7 || xx_data_get_u32(h + 8, 4, 0, true) != 2) return false;
    hs = xx_data_get_u32(h, 4, 0, true);
    depth = xx_data_get_u32(h + 12, 4, 0, true);
    w = xx_data_get_u32(h + 16, 4, 0, true);
    height = xx_data_get_u32(h + 20, 4, 0, true);
    pad = xx_data_get_u32(h + 40, 4, 0, true);
    bpp = xx_data_get_u32(h + 44, 4, 0, true);
    stride = xx_data_get_u32(h + 48, 4, 0, true);
    visual = xx_data_get_u32(h + 52, 4, 0, true);
    ncolors = xx_data_get_u32(h + 76, 4, 0, true);
    if (hs < 101 || hs > 4196 || !depth || depth > bpp || !w || !height || w > 32768 || height > 32768 || (uint64_t)w * height > 67108864 ||
        xx_data_get_u32(h + 24, 4, 0, true) || xx_data_get_u32(h + 28, 4, 0, true) > 1 || xx_data_get_u32(h + 36, 4, 0, true) > 1 || visual > 5 || ncolors > 4096)
        return false;
    if ((bpp != 1 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) || (pad != 8 && pad != 16 && pad != 32) ||
        (xx_data_get_u32(h + 32, 4, 0, true) != 8 && xx_data_get_u32(h + 32, 4, 0, true) != 16 && xx_data_get_u32(h + 32, 4, 0, true) != 32))
        return false;
    expected = (((uint64_t)w * bpp + pad - 1U) / pad) * (pad / 8U);
    raster = (uint64_t)stride * height;
    end = (uint64_t)hs + 12U * ncolors + raster;
    if (stride != expected || raster > 268435456U || end > (uint64_t)pm_available(f) || !pm_read(f, 100, name, hs - 100) || name[hs - 101]) return false;
    for (i = 0; i < hs - 100; ++i) {
        if (!name[i]) nul = true;
        else if (!nul && (name[i] < 32 || name[i] > 126)) return false;
    }
    rm = xx_data_get_u32(h + 56, 4, 0, true);
    gm = xx_data_get_u32(h + 60, 4, 0, true);
    bm = xx_data_get_u32(h + 64, 4, 0, true);
    if (visual >= 4 && (!rm || !gm || !bm || (rm & gm) || (rm & bm) || (gm & bm) || (bpp < 32 && ((rm | gm | bm) >> bpp)))) return false;
    if (!xx_data_get_u32(h + 68, 4, 0, true) || xx_data_get_u32(h + 68, 4, 0, true) > 16) return false;
    for (i = 0; i < ncolors; ++i) {
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)hs + 12 * (int64_t)i, entry, 12) || entry[10] > 7 || entry[11] ||
            (depth < 32 && (xx_data_get_u32(entry, 4, 0, true) >> depth)))
            return false;
    }
    if (!pm_add(f, s, "descriptor.bin", 0, 100) || !pm_add(f, s, "window-name.txt", 100, hs - 100) ||
        (ncolors && !pm_add(f, s, "colormap.xwd", hs, 12 * (int64_t)ncolors)) || !pm_add(f, s, "raster.bin", hs + 12 * (int64_t)ncolors, (int64_t)raster))
        return false;
    s->size = (int64_t)end;
    return true;
}

void xx_xwd_init(xx_xwd *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_XWD, "xwd");
    }
}
xx_xwd *xx_xwd_create(xx_io_device *d, int64_t b)
{
    xx_xwd *r = (xx_xwd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_xwd_init(r, d, b);
    return r;
}
void xx_xwd_destroy(xx_xwd *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_xwd_free(xx_xwd *r)
{
    if (r) {
        xx_xwd_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_xwd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_xwd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
