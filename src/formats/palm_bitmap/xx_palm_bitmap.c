/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/palm.c
 * PalmOS indexed bitmap v0/v1/v2: checked dimensions/stride/flags, bounded palette and complete uncompressed or byte-RLE raster extents. Original descriptor, palette and
 * encoded raster exported; direct-color, indirect storage, v3 and image chains declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/palm_bitmap/xx_palm_bitmap.h"
#include "../common/xx_component_binary.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[16];
    return n >= 16 && pm_read(f, 0, b, 16) && xx_data_get_u16(b, 2, 0, true) && xx_data_get_u16(b + 2, 2, 0, true) &&
           (b[8] == 1 || b[8] == 2 || b[8] == 4 || b[8] == 8) && b[9] <= 2;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t p = 16, bytes;
    unsigned w, h, stride, flags, bits, colors = 0, i;
    bool compressed;
    const uint8_t *palette = NULL;
    uint8_t used[256];
    xx_mem_zero(used, sizeof(used));
    if (n < 16) return false;
    w = xx_data_get_u16(b, 2, 0, true);
    h = xx_data_get_u16(b + 2, 2, 0, true);
    stride = xx_data_get_u16(b + 4, 2, 0, true);
    flags = xx_data_get_u16(b + 6, 2, 0, true);
    bits = b[8];
    if (!w || !h || w > 8192 || h > 8192 || (uint64_t)w * h > 16777216 || (bits != 1 && bits != 2 && bits != 4 && bits != 8) || b[9] > 2 ||
        xx_data_get_u16(b + 10, 2, 0, true) || xx_data_get_u16(b + 14, 2, 0, true) || (flags & ~0xe000U) || stride != ((w * bits + 15) / 16) * 2)
        return false;
    compressed = (flags & 0x8000) != 0;
    bytes = (uint64_t)stride * h;
    if (compressed && (b[9] != 2 || b[13] != 1)) return false;
    if (!compressed && b[13] != 0xff && b[13] != 0) return false;
    if ((flags & 0x2000) && b[12] >= (1U << bits)) return false;
    if (!component_emit(f, s, "descriptor.palm", 0, 16, n)) return false;
    if (bits == 8 && (flags & 0x4000)) {
        uint64_t start = p;
        if (!component_span(p, 2, n) || (colors = xx_data_get_u16(b + p, 2, 0, true)) == 0 || colors > 256) return false;
        p += 2;
        xx_mem_zero(used, sizeof(used));
        if (!component_span(p, (uint64_t)colors * 4, n)) return false;
        palette = b + p;
        for (i = 0; i < colors; ++i) {
            if (used[b[p + i * 4]]) return false;
            used[b[p + i * 4]] = 1;
        }
        p += (uint64_t)colors * 4;
        if (!component_emit(f, s, "palette.palm", start, p - start, n)) return false;
    }
    if (compressed) {
        uint64_t start = p, decoded = 0;
        unsigned packed;
        if (!component_span(p, 2, n) || (packed = xx_data_get_u16(b + p, 2, 0, true)) < 2 || packed != n - p) return false;
        p += 2;
        while (p < n) {
            unsigned run;
            if (xx_component_parser_stopped(pd) || !component_span(p, 2, n) || (run = b[p]) == 0 || run > bytes - decoded || run > stride - decoded % stride ||
                (palette && !used[b[p + 1]]))
                return false;
            decoded += run;
            p += 2;
        }
        if (decoded != bytes) return false;
        return component_emit(f, s, "rle-bitmap.palm", start, n - start, n);
    }
    if (n - p != bytes) return false;
    if (palette) {
        unsigned y, x;
        for (y = 0; y < h; ++y) {
            if (xx_component_parser_stopped(pd)) return false;
            for (x = 0; x < w; ++x) {
                unsigned index = b[p + (uint64_t)y * stride + x];
                if (!used[index]) return false;
            }
        }
    }
    return component_emit(f, s, "packed-bitmap.palm", p, bytes, n);
}

void xx_palm_bitmap_init(xx_palm_bitmap *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_PALM_BITMAP, "palm");
    }
}
xx_palm_bitmap *xx_palm_bitmap_create(xx_io_device *d, int64_t at)
{
    xx_palm_bitmap *r = (xx_palm_bitmap *)xx_mem_alloc(sizeof(*r));
    if (r) xx_palm_bitmap_init(r, d, at);
    return r;
}
void xx_palm_bitmap_destroy(xx_palm_bitmap *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_palm_bitmap_free(xx_palm_bitmap *r)
{
    if (r) {
        xx_palm_bitmap_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_palm_bitmap_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_palm_bitmap_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
