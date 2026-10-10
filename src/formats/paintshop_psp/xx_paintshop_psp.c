/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/GNOME/gimp/master/plug-ins/common/file-psp.c
 * Native PSP3.0 RGB24/grayscale8 raster images with one color bitmap per layer: exact38-byte image,375-byte layer and12-byte channel descriptors, bounded
 * rectangles/counts, stored or PSP RLE plane framing. Original encoded headers/channels exported; creator/color-table/alpha/user-mask blocks, other versions/compression
 * and rendering declined. Primary original unavailable; independently specified wire controls only. Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/paintshop_psp/xx_paintshop_psp.h"
#include "../common/xx_component_binary.h"

static bool graphics_text_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool graphics_text_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(graphics_text, 33554432, )
static bool graphics_text_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[36];
    return n >= 36 && pm_read(f, 0, b, 36) && component_tag(b, "Paint Shop Pro Image File\n\x1a", 27) && component_zero(b + 27, 5) &&
           xx_data_get_u16(b + 32, 2, 0, false) == 3 && xx_data_get_u16(b + 34, 2, 0, false) == 0;
}
static bool ps_block(const uint8_t *b, uint64_t *p, uint64_t n, unsigned id, uint32_t *init, uint64_t *end)
{
    uint32_t z;
    if (!component_span(*p, 14, n) || !component_tag(b + *p, "~BK\0", 4) || xx_data_get_u16(b + *p + 4, 2, 0, false) != id) return false;
    *init = xx_data_get_u32(b + *p + 6, 4, 0, false);
    z = xx_data_get_u32(b + *p + 10, 4, 0, false);
    *p += 14;
    if (*init > z || !component_span(*p, z, n)) return false;
    *end = *p + z;
    return true;
}
static bool ps_rle(const uint8_t *b, uint64_t n, uint64_t size, xx_pd_struct *pd)
{
    uint64_t p = 0, out = 0;
    while (p < n) {
        unsigned c = b[p++], run = c > 128 ? c - 128 : c;
        if (xx_component_parser_stopped(pd) || !run || run > size - out) return false;
        if (c > 128) {
            if (p == n) return false;
            ++p;
        } else {
            if (!component_span(p, run, n)) return false;
            p += run;
        }
        out += run;
    }
    return out == size;
}
static bool ps_rect(const uint8_t *b, uint32_t *w, uint32_t *h)
{
    int32_t x = (int32_t)xx_data_get_u32(b, 4, 0, false), y = (int32_t)xx_data_get_u32(b + 4, 4, 0, false), r = (int32_t)xx_data_get_u32(b + 8, 4, 0, false),
            d = (int32_t)xx_data_get_u32(b + 12, 4, 0, false);
    int64_t width = (int64_t)r - x, height = (int64_t)d - y;
    if (width < 0 || height < 0 || width > 16384 || height > 16384) return false;
    *w = (uint32_t)width;
    *h = (uint32_t)height;
    return true;
}
static bool graphics_text_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t p = 36, end, bank, lend, head, start;
    uint32_t init, w, h, iw, ih, compression, layers, active, depth, seen = 0, i, j;
    if (!ps_block(b, &p, n, 0, &init, &end) || init != 38 || end != p + 38) {
        return false;
    }
    w = xx_data_get_u32(b + p, 4, 0, false);
    h = xx_data_get_u32(b + p + 4, 4, 0, false);
    compression = xx_data_get_u16(b + p + 17, 2, 0, false);
    depth = xx_data_get_u16(b + p + 19, 2, 0, false);
    active = xx_data_get_u32(b + p + 32, 4, 0, false);
    layers = xx_data_get_u16(b + p + 36, 2, 0, false);
    if (!w || !h || w > 16384 || h > 16384 || (uint64_t)w * h > 8388608 || compression > 1 || b[p + 16] > 2 || (depth != 24 && depth != 8) ||
        xx_data_get_u16(b + p + 21, 2, 0, false) != (depth == 24 ? 3U : 1U) || b[p + 27] != (depth == 8) || !layers || layers > 1024 || active >= layers)
        return false;
    {
        uint32_t hi = xx_data_get_u32(b + p + 12, 4, 0, false);
        if ((hi & 0x7ff00000U) == 0x7ff00000U || (hi & 0x80000000U) || !(hi & 0x7fffffffU)) return false;
    }
    p = end;
    if (!component_emit(f, s, "descriptor.psp", 0, p, n) || !ps_block(b, &p, n, 3, &init, &bank) || init) return false;
    head = p - 14;
    if (!component_emit(f, s, "layer-bank.psp", head, 14, n)) return false;
    for (i = 0; i < layers; ++i) {
        uint32_t channels, bitmaps, mask = 0;
        start = p;
        if (xx_component_parser_stopped(pd) || !ps_block(b, &p, bank, 4, &init, &lend) || init != 375) return false;
        head = p;
        if (b[p + 256] > 1 || !ps_rect(b + p + 257, &iw, &ih) || !ps_rect(b + p + 273, &iw, &ih) || !iw || !ih || (uint64_t)iw * ih > 8388608 || b[p + 290] > 17 ||
            b[p + 291] > 1 || b[p + 292] > 1 || b[p + 326] > 1 || b[p + 327] > 1 || !ps_rect(b + p + 294, &w, &h) || !ps_rect(b + p + 310, &w, &h))
            return false;
        bitmaps = xx_data_get_u16(b + p + 371, 2, 0, false);
        channels = xx_data_get_u16(b + p + 373, 2, 0, false);
        if (bitmaps != 1 || channels != (depth == 24 ? 3U : 1U)) return false;
        p += init;
        if (!component_emit(f, s, "layer.psp", start, p - start, n)) return false;
        for (j = 0; j < channels; ++j) {
            uint32_t packed, size, type;
            uint64_t cend, data;
            start = p;
            if (!ps_block(b, &p, lend, 5, &init, &cend) || init != 12) return false;
            packed = xx_data_get_u32(b + p, 4, 0, false);
            size = xx_data_get_u32(b + p + 4, 4, 0, false);
            type = xx_data_get_u16(b + p + 10, 2, 0, false);
            if (xx_data_get_u16(b + p + 8, 2, 0, false) || type > 3 || type != (depth == 24 ? j + 1 : 0) || (mask & (1U << type)) || size != (uint64_t)iw * ih ||
                packed != cend - p - init)
                return false;
            mask |= 1U << type;
            data = p + init;
            if (compression == 0) {
                if (packed != size) return false;
            } else if (!ps_rle(b + data, packed, size, pd)) return false;
            p = cend;
            if (!component_emit(f, s, "channel.psp", start, p - start, n)) return false;
        }
        if (p != lend) return false;
        ++seen;
        (void)head;
    }
    if (p != bank || p != n || seen != layers) return false;
    s->size = (int64_t)n;
    return true;
}

void xx_paintshop_psp_init(xx_paintshop_psp *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_PAINTSHOP_PSP, "psp");
    }
}
xx_paintshop_psp *xx_paintshop_psp_create(xx_io_device *d, int64_t at)
{
    xx_paintshop_psp *r = (xx_paintshop_psp *)xx_mem_alloc(sizeof(*r));
    if (r) xx_paintshop_psp_init(r, d, at);
    return r;
}
void xx_paintshop_psp_destroy(xx_paintshop_psp *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_paintshop_psp_free(xx_paintshop_psp *r)
{
    if (r) {
        xx_paintshop_psp_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_paintshop_psp_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_paintshop_psp_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
