/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Wireless Application Protocol Bitmap (WBMP) codec.
 * Load: Type 0 (monochrome), multi-byte variable length integers (uintvar),
 * 1 = white, 0 = black.
 * Save: Type 0 WBMP, thresholding luminance at 128 (1 = white, 0 = black).
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool wbmp_read_uintvar(const uint8_t *d, size_t n, size_t *p, uint32_t *out)
{
    uint64_t val = 0;
    size_t count = 0;
    while (*p < n && count < 5) {
        uint8_t b = d[(*p)++];
        val = (val << 7) | (uint64_t)(b & 0x7F);
        if (val > 0xFFFFFFFFULL) return false;
        if (!(b & 0x80)) {
            *out = (uint32_t)val;
            return true;
        }
        count++;
    }
    return false;
}

static bool wbmp_write_uintvar(xx_image_buffer *b, uint32_t val)
{
    uint8_t buf[5];
    int len = 0;
    buf[len++] = (uint8_t)(val & 0x7F);
    val >>= 7;
    while (val > 0) {
        buf[len++] = (uint8_t)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    for (int i = len - 1; i >= 0; --i) {
        if (!xx_image_buf_u8(b, buf[i])) return false;
    }
    return true;
}

static int wbmp_probe(const uint8_t *d, size_t n)
{
    size_t p = 0;
    uint32_t type, w, h;
    if (n < 4) return 0;
    if (d[0] != 0 || d[1] != 0) return 0;
    if (!wbmp_read_uintvar(d, n, &p, &type) || type != 0) return 0;
    if (p >= n || d[p++] != 0) return 0; /* FixHeader must be 0 */
    if (!wbmp_read_uintvar(d, n, &p, &w) || w == 0 || w > 65535) return 0;
    if (!wbmp_read_uintvar(d, n, &p, &h) || h == 0 || h > 65535) return 0;
    {
        size_t stride = ((size_t)w + 7) / 8;
        if (xx_image_in_range(n, p, stride * h)) return 20; /* Probe score at most 20 */
    }
    return 10;
}

static xx_image_status wbmp_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    size_t p = 0;
    uint32_t type = 0, w = 0, h = 0;
    uint8_t fix_header;
    size_t stride;
    xx_image_frame *f;
    xx_image_status st;
    const char *g = "WBMP";

    if (!wbmp_read_uintvar(d, n, &p, &type) || type != 0) return XX_IMAGE_ERR_FORMAT;
    if (p >= n) return XX_IMAGE_ERR_CORRUPT;
    fix_header = d[p++];
    if (fix_header != 0) return XX_IMAGE_ERR_UNSUPPORTED;

    if (!wbmp_read_uintvar(d, n, &p, &w) || !wbmp_read_uintvar(d, n, &p, &h)) {
        return XX_IMAGE_ERR_CORRUPT;
    }

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    stride = ((size_t)w + 7) / 8;
    if (!xx_image_in_range(n, p, stride * h)) return XX_IMAGE_ERR_CORRUPT;

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *row = d + p + (size_t)y * stride;
        uint8_t *dst = f->rgba + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t bit = (uint8_t)((row[x / 8] >> (7 - (x % 8))) & 1);
            /* WBMP: 1 = white, 0 = black */
            uint8_t c = bit ? 255 : 0;
            dst[x * 4]     = c;
            dst[x * 4 + 1] = c;
            dst[x * 4 + 2] = c;
            dst[x * 4 + 3] = 255;
        }
    }

    img->info.color_model = XX_IMAGE_COLOR_BILEVEL;
    img->info.bits_per_pixel = 1;
    img->info.bits_per_sample = 1;
    img->info.channels = 1;
    img->info.has_alpha = false;
    img->info.frame_count = 1;
    XX_IMAGE_SET_STR(img->info.compression, "none");

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, g, "type", type);
        xx_image_add_prop_uint(img, g, "fix header", fix_header);
    }

    return XX_IMAGE_OK;
}

static xx_image_status wbmp_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    size_t stride;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || (uint64_t)w * h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;

    stride = ((size_t)w + 7) / 8;

    /* Header: type 0, fix header 0, width, height */
    if (!wbmp_write_uintvar(out, 0)) return XX_IMAGE_ERR_MEMORY;
    if (!xx_image_buf_u8(out, 0)) return XX_IMAGE_ERR_MEMORY;
    if (!wbmp_write_uintvar(out, w)) return XX_IMAGE_ERR_MEMORY;
    if (!wbmp_write_uintvar(out, h)) return XX_IMAGE_ERR_MEMORY;

    if (!xx_image_buf_reserve(out, out->size + stride * h)) return XX_IMAGE_ERR_MEMORY;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *src = f->rgba + (size_t)y * w * 4;
        for (size_t b = 0; b < stride; ++b) {
            uint8_t byte_val = 0;
            for (uint32_t bit = 0; bit < 8; ++bit) {
                uint32_t x = (uint32_t)(b * 8 + bit);
                if (x < w) {
                    const uint8_t *px = src + (size_t)x * 4;
                    uint32_t lum = ((uint32_t)px[0] * 77 + (uint32_t)px[1] * 150 + (uint32_t)px[2] * 29) >> 8;
                    /* 1 = white, 0 = black */
                    if (lum >= 128) {
                        byte_val |= (uint8_t)(1 << (7 - bit));
                    }
                }
            }
            xx_image_buf_u8(out, byte_val);
        }
    }

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_wbmp = {
    "wbmp",
    "Wireless Bitmap",
    "wbmp",
    "image/vnd.wap.wbmp",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_INFO,
    0,
    wbmp_probe,
    wbmp_load,
    wbmp_save
};
