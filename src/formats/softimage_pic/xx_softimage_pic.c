/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/AcademySoftwareFoundation/OpenImageIO/main/src/softimage.imageio/softimageinput.cpp
 * Softimage PIC1.0/1.6 8/16-bit RGB/RGBA scanline packet descriptors with complete stored/pure-RLE/mixed-RLE channel packet runs and exact image extent. Original header
 * and encoded scanlines exported; unsupported fields/channel combinations and rendering refused. Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/softimage_pic/xx_softimage_pic.h"
#include "../common/xx_component_binary.h"

static bool model_image_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool model_image_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(model_image, 67108864, )
#include "xxfclib/data/xx_data.h"
static bool model_image_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[4];
    return n >= 108 && pm_read(f, 0, b, 4) && xx_data_get_u32(b, 4, 0, true) == 0x5380f634U;
}
static bool model_image_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t w = xx_data_get_u16(b + 92, 2, 0, true), h = xx_data_get_u16(b + 94, 2, 0, true), version = xx_data_get_u32(b + 4, 4, 0, true), seen = 0, packets = 0, y, i;
    uint8_t sizes[4], types[4];
    uint64_t p = 104;
    char label[64];
    bool next = true;
    if (xx_data_get_u32(b, 4, 0, true) != 0x5380f634U || (version != 0x3f800000U && version != 0x3fcccccdU) || !component_tag(b + 88, "PICT", 4) || !w || !h ||
        w > 16384 || h > 2048 || (uint64_t)w * h > 8388608 || !component_is_finite32(xx_data_get_u32(b + 96, 4, 0, true)) ||
        (xx_data_get_u32(b + 96, 4, 0, true) & 0x80000000U) || !(xx_data_get_u32(b + 96, 4, 0, true) & 0x7fffffffU) || xx_data_get_u16(b + 100, 2, 0, true) > 3 ||
        xx_data_get_u16(b + 102, 2, 0, true))
        return false;
    while (next) {
        uint8_t bits, mask;
        unsigned channels = 0, k;
        if (packets == 4 || !component_span(p, 4, n) || b[p] > 1) return false;
        next = b[p] != 0;
        bits = b[p + 1];
        types[packets] = b[p + 2];
        mask = b[p + 3];
        p += 4;
        if ((bits != 8 && bits != 16) || types[packets] > 2 || !mask || (mask & 15) || (mask & seen)) return false;
        for (k = 0; k < 4; ++k) {
            if (mask & (128U >> k)) ++channels;
        }
        seen |= mask;
        sizes[packets] = (uint8_t)(channels * (bits / 8));
        ++packets;
    }
    if ((seen != 0xe0 && seen != 0xf0) || !component_emit(f, s, "descriptor.pic", 0, p, n)) return false;
    for (y = 0; y < h; ++y) {
        uint64_t start = p;
        for (i = 0; i < packets; ++i) {
            uint32_t x = 0;
            if (types[i] == 0) {
                uint64_t z = (uint64_t)w * sizes[i];
                if (!component_span(p, z, n)) return false;
                p += z;
                continue;
            }
            while (x < w) {
                uint32_t count;
                uint64_t bytes;
                uint8_t code;
                if (xx_component_parser_stopped(pd) || !component_span(p, 1, n)) {
                    return false;
                }
                code = b[p++];
                if (types[i] == 1) {
                    count = code;
                    bytes = sizes[i];
                } else if (code < 128) {
                    count = code + 1U;
                    bytes = (uint64_t)count * sizes[i];
                } else {
                    if (code == 128) {
                        if (!component_span(p, 2, n)) return false;
                        count = xx_data_get_u16(b + p, 2, 0, true);
                        p += 2;
                    } else count = code - 127U;
                    bytes = sizes[i];
                }
                if (!count || count > w - x || !component_span(p, bytes, n)) {
                    return false;
                }
                p += bytes;
                x += count;
            }
        }
        xx_rt_snprintf(label, sizeof(label), "scanline-%u.pic", y);
        if (!component_emit(f, s, label, start, p - start, n)) return false;
    }
    if (p != n) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_softimage_pic_init(xx_softimage_pic *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_SOFTIMAGE_PIC, "pic");
    }
}
xx_softimage_pic *xx_softimage_pic_create(xx_io_device *d, int64_t at)
{
    xx_softimage_pic *r = (xx_softimage_pic *)xx_mem_alloc(sizeof(*r));
    if (r) xx_softimage_pic_init(r, d, at);
    return r;
}
void xx_softimage_pic_destroy(xx_softimage_pic *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_softimage_pic_free(xx_softimage_pic *r)
{
    if (r) {
        xx_softimage_pic_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_softimage_pic_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_softimage_pic_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
