/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.cs.cmu.edu/~maxwell/misc/vascHelpPages/sunRasterFormat.html,
 * https://gitlab.gnome.org/GNOME/gimp/-/blob/master/plug-ins/common/file-sunras.c Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/sun_raster/xx_sun_raster.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[32];
    uint32_t w, height, depth, size, type, maptype, map;
    uint64_t row, raw, done = 0;
    int64_t at, end, body;
    if (!pm_read(f, 0, h, 32) || xx_data_get_u32(h, 4, 0, true) != 0x59A66A95U || !(w = xx_data_get_u32(h + 4, 4, 0, true)) ||
        !(height = xx_data_get_u32(h + 8, 4, 0, true)))
        return false;
    depth = xx_data_get_u32(h + 12, 4, 0, true);
    size = xx_data_get_u32(h + 16, 4, 0, true);
    type = xx_data_get_u32(h + 20, 4, 0, true);
    maptype = xx_data_get_u32(h + 24, 4, 0, true);
    map = xx_data_get_u32(h + 28, 4, 0, true);
    if ((depth != 1 && depth != 8 && depth != 24 && depth != 32) || type > 3 || maptype > 2 || (maptype == 0 && map) || (maptype == 1 && (map % 3 || map / 3 > 256)))
        return false;
    row = ((uint64_t)w * depth + 15) / 16 * 2;
    if (row > (uint64_t)INT64_MAX / height) return false;
    raw = row * height;
    body = 32 + (int64_t)map;
    if (body > pm_available(f)) return false;
    if (type != 2) {
        if (size && size != raw) return false;
        if (!size && type != 0) return false;
        if (raw > (uint64_t)(pm_available(f) - body)) return false;
        end = body + (int64_t)raw;
    } else {
        if (!size || size > (uint64_t)(pm_available(f) - body)) return false;
        end = body + size;
        at = body;
        while (at < end) {
            uint8_t b, n;
            uint64_t run = 1;
            if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at++, &b, 1)) return false;
            if (b == 128) {
                if (at >= end || !pm_read(f, at++, &n, 1)) return false;
                if (n) {
                    if (at >= end || !pm_read(f, at++, &b, 1)) return false;
                    run = (uint64_t)n + 1;
                }
            }
            if (run > raw - done) {
                return false;
            }
            done += run;
        }
        if (done != raw) return false;
    }
    if (!pm_add(f, s, "descriptor.bin", 4, 28) || (map && !pm_add(f, s, "colormap.bin", 32, map)) ||
        !pm_add(f, s, type == 2 ? "pixels-rle.bin" : "pixels-raw.bin", body, end - body))
        return false;
    s->size = end;
    return true;
}

void xx_sun_raster_init(xx_sun_raster *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SUN_RASTER, "sun_raster");
    }
}
xx_sun_raster *xx_sun_raster_create(xx_io_device *d, int64_t b)
{
    xx_sun_raster *r = (xx_sun_raster *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sun_raster_init(r, d, b);
    return r;
}
void xx_sun_raster_destroy(xx_sun_raster *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sun_raster_free(xx_sun_raster *r)
{
    if (r) {
        xx_sun_raster_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sun_raster_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sun_raster_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
