/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DirectDraw Surface (DDS) image format codec.
 * Load: uncompressed RGB / RGBA / luminance masks (8..32 bpp), DXT1 (BC1 with
 * 1-bit alpha), DXT3 (BC2), DXT5 (BC3), BC4 and BC5 (ATI1 / ATI2), DX10 header
 * with DXGI_FORMAT_R8G8B8A8_UNORM, B8G8R8A8_UNORM, and BC1..BC5 UNORM.
 * Top mip level of first face as frame 0; mipmaps, cubemap flags, fourcc in props.
 * Save: uncompressed 32-bit BGRA (A8R8G8B8 masks), no mipmaps.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDS_MAGIC 0x20534444U /* "DDS " */

static int dds_probe(const uint8_t *d, size_t n)
{
    if (n < 128) return 0;
    if (xx_image_rd_u32le(d) != DDS_MAGIC) return 0;
    if (xx_image_rd_u32le(d + 4) != 124) return 0;
    return 95;
}

static unsigned dds_mask_shift(uint32_t m)
{
    unsigned s = 0;
    if (!m) return 0;
    while (!(m & 1)) {
        m >>= 1;
        ++s;
    }
    return s;
}

static unsigned dds_mask_bits(uint32_t m)
{
    unsigned b = 0;
    m >>= dds_mask_shift(m);
    while (m & 1) {
        m >>= 1;
        ++b;
    }
    return b;
}

static uint8_t dds_scale(uint32_t v, unsigned bits)
{
    if (!bits) return 0;
    if (bits >= 8) return (uint8_t)(v >> (bits - 8));
    return (uint8_t)((v * 255U + ((1U << bits) - 1) / 2) / ((1U << bits) - 1));
}

static void dds_decode_bc1_block(const uint8_t *src, uint8_t *dst_rgba, uint32_t w, uint32_t h,
                                 uint32_t bx, uint32_t by)
{
    uint16_t c0 = xx_image_rd_u16le(src);
    uint16_t c1 = xx_image_rd_u16le(src + 2);
    uint32_t code = xx_image_rd_u32le(src + 4);
    uint8_t pal[4][4];

    /* Unpack 565 colors */
    pal[0][0] = (uint8_t)((((c0 >> 11) & 31) << 3) | (((c0 >> 11) & 31) >> 2));
    pal[0][1] = (uint8_t)((((c0 >> 5) & 63) << 2) | (((c0 >> 5) & 63) >> 4));
    pal[0][2] = (uint8_t)((((c0) & 31) << 3) | (((c0) & 31) >> 2));
    pal[0][3] = 255;

    pal[1][0] = (uint8_t)((((c1 >> 11) & 31) << 3) | (((c1 >> 11) & 31) >> 2));
    pal[1][1] = (uint8_t)((((c1 >> 5) & 63) << 2) | (((c1 >> 5) & 63) >> 4));
    pal[1][2] = (uint8_t)((((c1) & 31) << 3) | (((c1) & 31) >> 2));
    pal[1][3] = 255;

    if (c0 > c1) {
        pal[2][0] = (uint8_t)((2 * pal[0][0] + pal[1][0]) / 3);
        pal[2][1] = (uint8_t)((2 * pal[0][1] + pal[1][1]) / 3);
        pal[2][2] = (uint8_t)((2 * pal[0][2] + pal[1][2]) / 3);
        pal[2][3] = 255;

        pal[3][0] = (uint8_t)((pal[0][0] + 2 * pal[1][0]) / 3);
        pal[3][1] = (uint8_t)((pal[0][1] + 2 * pal[1][1]) / 3);
        pal[3][2] = (uint8_t)((pal[0][2] + 2 * pal[1][2]) / 3);
        pal[3][3] = 255;
    } else {
        pal[2][0] = (uint8_t)((pal[0][0] + pal[1][0]) / 2);
        pal[2][1] = (uint8_t)((pal[0][1] + pal[1][1]) / 2);
        pal[2][2] = (uint8_t)((pal[0][2] + pal[1][2]) / 2);
        pal[2][3] = 255;

        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0; /* 1-bit alpha: transparent black */
    }

    for (uint32_t py = 0; py < 4; ++py) {
        uint32_t y = by * 4 + py;
        if (y >= h) break;
        for (uint32_t px = 0; px < 4; ++px) {
            uint32_t x = bx * 4 + px;
            if (x >= w) continue;
            uint32_t idx = (code >> (2 * (py * 4 + px))) & 3;
            uint8_t *d = dst_rgba + ((size_t)y * w + x) * 4;
            d[0] = pal[idx][0];
            d[1] = pal[idx][1];
            d[2] = pal[idx][2];
            d[3] = pal[idx][3];
        }
    }
}

static void dds_decode_bc2_block(const uint8_t *src, uint8_t *dst_rgba, uint32_t w, uint32_t h,
                                 uint32_t bx, uint32_t by)
{
    uint16_t c0 = xx_image_rd_u16le(src + 8);
    uint16_t c1 = xx_image_rd_u16le(src + 10);
    uint32_t code = xx_image_rd_u32le(src + 12);
    uint8_t pal[4][3];

    pal[0][0] = (uint8_t)((((c0 >> 11) & 31) << 3) | (((c0 >> 11) & 31) >> 2));
    pal[0][1] = (uint8_t)((((c0 >> 5) & 63) << 2) | (((c0 >> 5) & 63) >> 4));
    pal[0][2] = (uint8_t)((((c0) & 31) << 3) | (((c0) & 31) >> 2));

    pal[1][0] = (uint8_t)((((c1 >> 11) & 31) << 3) | (((c1 >> 11) & 31) >> 2));
    pal[1][1] = (uint8_t)((((c1 >> 5) & 63) << 2) | (((c1 >> 5) & 63) >> 4));
    pal[1][2] = (uint8_t)((((c1) & 31) << 3) | (((c1) & 31) >> 2));

    pal[2][0] = (uint8_t)((2 * pal[0][0] + pal[1][0]) / 3);
    pal[2][1] = (uint8_t)((2 * pal[0][1] + pal[1][1]) / 3);
    pal[2][2] = (uint8_t)((2 * pal[0][2] + pal[1][2]) / 3);

    pal[3][0] = (uint8_t)((pal[0][0] + 2 * pal[1][0]) / 3);
    pal[3][1] = (uint8_t)((pal[0][1] + 2 * pal[1][1]) / 3);
    pal[3][2] = (uint8_t)((pal[0][2] + 2 * pal[1][2]) / 3);

    for (uint32_t py = 0; py < 4; ++py) {
        uint32_t y = by * 4 + py;
        if (y >= h) break;
        uint16_t alpha_row = xx_image_rd_u16le(src + py * 2);
        for (uint32_t px = 0; px < 4; ++px) {
            uint32_t x = bx * 4 + px;
            if (x >= w) continue;
            uint32_t idx = (code >> (2 * (py * 4 + px))) & 3;
            uint8_t a = (uint8_t)(((alpha_row >> (px * 4)) & 0x0F) * 17);
            uint8_t *d = dst_rgba + ((size_t)y * w + x) * 4;
            d[0] = pal[idx][0];
            d[1] = pal[idx][1];
            d[2] = pal[idx][2];
            d[3] = a;
        }
    }
}

static void dds_decode_bc3_alpha(const uint8_t *src, uint8_t alpha_out[16])
{
    uint8_t a0 = src[0];
    uint8_t a1 = src[1];
    uint8_t pal[8];
    uint64_t bits = 0;

    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1) {
        pal[2] = (uint8_t)((6 * a0 + 1 * a1) / 7);
        pal[3] = (uint8_t)((5 * a0 + 2 * a1) / 7);
        pal[4] = (uint8_t)((4 * a0 + 3 * a1) / 7);
        pal[5] = (uint8_t)((3 * a0 + 4 * a1) / 7);
        pal[6] = (uint8_t)((2 * a0 + 5 * a1) / 7);
        pal[7] = (uint8_t)((1 * a0 + 6 * a1) / 7);
    } else {
        pal[2] = (uint8_t)((4 * a0 + 1 * a1) / 5);
        pal[3] = (uint8_t)((3 * a0 + 2 * a1) / 5);
        pal[4] = (uint8_t)((2 * a0 + 3 * a1) / 5);
        pal[5] = (uint8_t)((1 * a0 + 4 * a1) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }

    for (int i = 0; i < 6; ++i) {
        bits |= (uint64_t)src[2 + i] << (i * 8);
    }

    for (int i = 0; i < 16; ++i) {
        uint8_t idx = (uint8_t)((bits >> (i * 3)) & 7);
        alpha_out[i] = pal[idx];
    }
}

static void dds_decode_bc3_block(const uint8_t *src, uint8_t *dst_rgba, uint32_t w, uint32_t h,
                                 uint32_t bx, uint32_t by)
{
    uint16_t c0 = xx_image_rd_u16le(src + 8);
    uint16_t c1 = xx_image_rd_u16le(src + 10);
    uint32_t code = xx_image_rd_u32le(src + 12);
    uint8_t pal[4][3];
    uint8_t alpha[16];

    dds_decode_bc3_alpha(src, alpha);

    pal[0][0] = (uint8_t)((((c0 >> 11) & 31) << 3) | (((c0 >> 11) & 31) >> 2));
    pal[0][1] = (uint8_t)((((c0 >> 5) & 63) << 2) | (((c0 >> 5) & 63) >> 4));
    pal[0][2] = (uint8_t)((((c0) & 31) << 3) | (((c0) & 31) >> 2));

    pal[1][0] = (uint8_t)((((c1 >> 11) & 31) << 3) | (((c1 >> 11) & 31) >> 2));
    pal[1][1] = (uint8_t)((((c1 >> 5) & 63) << 2) | (((c1 >> 5) & 63) >> 4));
    pal[1][2] = (uint8_t)((((c1) & 31) << 3) | (((c1) & 31) >> 2));

    pal[2][0] = (uint8_t)((2 * pal[0][0] + pal[1][0]) / 3);
    pal[2][1] = (uint8_t)((2 * pal[0][1] + pal[1][1]) / 3);
    pal[2][2] = (uint8_t)((2 * pal[0][2] + pal[1][2]) / 3);

    pal[3][0] = (uint8_t)((pal[0][0] + 2 * pal[1][0]) / 3);
    pal[3][1] = (uint8_t)((pal[0][1] + 2 * pal[1][1]) / 3);
    pal[3][2] = (uint8_t)((pal[0][2] + 2 * pal[1][2]) / 3);

    for (uint32_t py = 0; py < 4; ++py) {
        uint32_t y = by * 4 + py;
        if (y >= h) break;
        for (uint32_t px = 0; px < 4; ++px) {
            uint32_t x = bx * 4 + px;
            if (x >= w) continue;
            uint32_t idx = (code >> (2 * (py * 4 + px))) & 3;
            uint8_t *d = dst_rgba + ((size_t)y * w + x) * 4;
            d[0] = pal[idx][0];
            d[1] = pal[idx][1];
            d[2] = pal[idx][2];
            d[3] = alpha[py * 4 + px];
        }
    }
}

static void dds_decode_bc4_block(const uint8_t *src, uint8_t *dst_rgba, uint32_t w, uint32_t h,
                                 uint32_t bx, uint32_t by)
{
    uint8_t vals[16];
    dds_decode_bc3_alpha(src, vals);
    for (uint32_t py = 0; py < 4; ++py) {
        uint32_t y = by * 4 + py;
        if (y >= h) break;
        for (uint32_t px = 0; px < 4; ++px) {
            uint32_t x = bx * 4 + px;
            if (x >= w) continue;
            uint8_t v = vals[py * 4 + px];
            uint8_t *d = dst_rgba + ((size_t)y * w + x) * 4;
            d[0] = d[1] = d[2] = v;
            d[3] = 255;
        }
    }
}

static void dds_decode_bc5_block(const uint8_t *src, uint8_t *dst_rgba, uint32_t w, uint32_t h,
                                 uint32_t bx, uint32_t by)
{
    uint8_t r_vals[16];
    uint8_t g_vals[16];
    dds_decode_bc3_alpha(src, r_vals);
    dds_decode_bc3_alpha(src + 8, g_vals);

    for (uint32_t py = 0; py < 4; ++py) {
        uint32_t y = by * 4 + py;
        if (y >= h) break;
        for (uint32_t px = 0; px < 4; ++px) {
            uint32_t x = bx * 4 + px;
            if (x >= w) continue;
            uint8_t *d = dst_rgba + ((size_t)y * w + x) * 4;
            d[0] = r_vals[py * 4 + px];
            d[1] = g_vals[py * 4 + px];
            d[2] = 0;
            d[3] = 255;
        }
    }
}

static xx_image_status dds_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint32_t h, w, mipmaps, caps2;
    uint32_t pf_flags, fourcc, rgb_bits;
    uint32_t r_mask, g_mask, b_mask, a_mask;
    uint32_t dxgi_format = 0;
    size_t data_off = 128;
    xx_image_frame *f;
    xx_image_status st;
    char fourcc_str[5] = "";
    const char *g = "DDS";

    if (dds_probe(d, n) == 0) return XX_IMAGE_ERR_FORMAT;

    h = xx_image_rd_u32le(d + 12);
    w = xx_image_rd_u32le(d + 16);
    mipmaps = xx_image_rd_u32le(d + 28);
    caps2 = xx_image_rd_u32le(d + 112);

    pf_flags = xx_image_rd_u32le(d + 80);
    fourcc = xx_image_rd_u32le(d + 84);
    rgb_bits = xx_image_rd_u32le(d + 88);
    r_mask = xx_image_rd_u32le(d + 92);
    g_mask = xx_image_rd_u32le(d + 96);
    b_mask = xx_image_rd_u32le(d + 100);
    a_mask = xx_image_rd_u32le(d + 104);

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    /* Handle DX10 header */
    if ((pf_flags & 0x04) && fourcc == 0x30315844) { /* "DX10" */
        if (n < 148) return XX_IMAGE_ERR_CORRUPT;
        dxgi_format = xx_image_rd_u32le(d + 128);
        data_off = 148;
    }

    memcpy(fourcc_str, d + 84, 4);
    fourcc_str[4] = '\0';

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    /* Check block compressed formats */
    if ((pf_flags & 0x04) || dxgi_format != 0) {
        uint32_t bx = (w + 3) / 4;
        uint32_t by = (h + 3) / 4;

        if (fourcc == 0x31545844 || dxgi_format == 70 || dxgi_format == 71) { /* "DXT1" / BC1 */
            size_t req = (size_t)bx * by * 8;
            if (!xx_image_in_range(n, data_off, req)) return XX_IMAGE_ERR_CORRUPT;
            for (uint32_t y = 0; y < by; ++y) {
                for (uint32_t x = 0; x < bx; ++x) {
                    dds_decode_bc1_block(d + data_off + ((size_t)y * bx + x) * 8, f->rgba, w, h, x, y);
                }
            }
            img->info.has_alpha = true;
            XX_IMAGE_SET_STR(img->info.compression, "DXT1");
        } else if (fourcc == 0x32545844 || fourcc == 0x33545844 || dxgi_format == 73 || dxgi_format == 74) { /* "DXT3" / BC2 */
            size_t req = (size_t)bx * by * 16;
            if (!xx_image_in_range(n, data_off, req)) return XX_IMAGE_ERR_CORRUPT;
            for (uint32_t y = 0; y < by; ++y) {
                for (uint32_t x = 0; x < bx; ++x) {
                    dds_decode_bc2_block(d + data_off + ((size_t)y * bx + x) * 16, f->rgba, w, h, x, y);
                }
            }
            img->info.has_alpha = true;
            XX_IMAGE_SET_STR(img->info.compression, "DXT3");
        } else if (fourcc == 0x34545844 || fourcc == 0x35545844 || dxgi_format == 76 || dxgi_format == 77) { /* "DXT5" / BC3 */
            size_t req = (size_t)bx * by * 16;
            if (!xx_image_in_range(n, data_off, req)) return XX_IMAGE_ERR_CORRUPT;
            for (uint32_t y = 0; y < by; ++y) {
                for (uint32_t x = 0; x < bx; ++x) {
                    dds_decode_bc3_block(d + data_off + ((size_t)y * bx + x) * 16, f->rgba, w, h, x, y);
                }
            }
            img->info.has_alpha = true;
            XX_IMAGE_SET_STR(img->info.compression, "DXT5");
        } else if (fourcc == 0x31495441 || fourcc == 0x55344342 || dxgi_format == 79 || dxgi_format == 80) { /* "ATI1" / "BC4U" */
            size_t req = (size_t)bx * by * 8;
            if (!xx_image_in_range(n, data_off, req)) return XX_IMAGE_ERR_CORRUPT;
            for (uint32_t y = 0; y < by; ++y) {
                for (uint32_t x = 0; x < bx; ++x) {
                    dds_decode_bc4_block(d + data_off + ((size_t)y * bx + x) * 8, f->rgba, w, h, x, y);
                }
            }
            img->info.has_alpha = false;
            XX_IMAGE_SET_STR(img->info.compression, "BC4");
        } else if (fourcc == 0x32495441 || fourcc == 0x55354342 || dxgi_format == 82 || dxgi_format == 83) { /* "ATI2" / "BC5U" */
            size_t req = (size_t)bx * by * 16;
            if (!xx_image_in_range(n, data_off, req)) return XX_IMAGE_ERR_CORRUPT;
            for (uint32_t y = 0; y < by; ++y) {
                for (uint32_t x = 0; x < bx; ++x) {
                    dds_decode_bc5_block(d + data_off + ((size_t)y * bx + x) * 16, f->rgba, w, h, x, y);
                }
            }
            img->info.has_alpha = false;
            XX_IMAGE_SET_STR(img->info.compression, "BC5");
        } else if (dxgi_format == 28) { /* DXGI_FORMAT_R8G8B8A8_UNORM */
            size_t req = (size_t)w * h * 4;
            if (!xx_image_in_range(n, data_off, req)) return XX_IMAGE_ERR_CORRUPT;
            memcpy(f->rgba, d + data_off, req);
            img->info.has_alpha = true;
            XX_IMAGE_SET_STR(img->info.compression, "none");
        } else if (dxgi_format == 87) { /* DXGI_FORMAT_B8G8R8A8_UNORM */
            size_t req = (size_t)w * h * 4;
            if (!xx_image_in_range(n, data_off, req)) return XX_IMAGE_ERR_CORRUPT;
            for (size_t i = 0; i < (size_t)w * h; ++i) {
                f->rgba[i * 4]     = d[data_off + i * 4 + 2];
                f->rgba[i * 4 + 1] = d[data_off + i * 4 + 1];
                f->rgba[i * 4 + 2] = d[data_off + i * 4];
                f->rgba[i * 4 + 3] = d[data_off + i * 4 + 3];
            }
            img->info.has_alpha = true;
            XX_IMAGE_SET_STR(img->info.compression, "none");
        } else {
            return XX_IMAGE_ERR_UNSUPPORTED;
        }
    } else {
        /* Uncompressed via masks */
        unsigned r_sh, g_sh, b_sh, a_sh;
        unsigned r_b, g_b, b_b, a_b;
        bool is_lum = (pf_flags & 0x20000) != 0;
        bool use_alpha = (a_mask != 0) || (pf_flags & 0x01);
        size_t bpp_bytes = (rgb_bits + 7) / 8;
        size_t row_bytes = (size_t)w * bpp_bytes;

        if (rgb_bits != 8 && rgb_bits != 16 && rgb_bits != 24 && rgb_bits != 32) {
            return XX_IMAGE_ERR_UNSUPPORTED;
        }
        if (!xx_image_in_range(n, data_off, row_bytes * h)) return XX_IMAGE_ERR_CORRUPT;

        r_sh = dds_mask_shift(r_mask);
        g_sh = dds_mask_shift(g_mask);
        b_sh = dds_mask_shift(b_mask);
        a_sh = dds_mask_shift(a_mask);

        r_b = dds_mask_bits(r_mask);
        g_b = dds_mask_bits(g_mask);
        b_b = dds_mask_bits(b_mask);
        a_b = dds_mask_bits(a_mask);

        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *src_row = d + data_off + (size_t)y * row_bytes;
            uint8_t *dst_row = f->rgba + (size_t)y * w * 4;

            for (uint32_t x = 0; x < w; ++x) {
                uint32_t val = 0;
                uint8_t *px = dst_row + (size_t)x * 4;
                if (rgb_bits == 8) {
                    val = src_row[x];
                } else if (rgb_bits == 16) {
                    val = xx_image_rd_u16le(src_row + x * 2);
                } else if (rgb_bits == 24) {
                    val = (uint32_t)src_row[x * 3] | ((uint32_t)src_row[x * 3 + 1] << 8) | ((uint32_t)src_row[x * 3 + 2] << 16);
                } else {
                    val = xx_image_rd_u32le(src_row + x * 4);
                }

                if (is_lum) {
                    uint8_t lum = dds_scale((val & r_mask) >> r_sh, r_b);
                    px[0] = px[1] = px[2] = lum;
                    px[3] = use_alpha ? dds_scale((val & a_mask) >> a_sh, a_b) : 255;
                } else {
                    px[0] = dds_scale((val & r_mask) >> r_sh, r_b);
                    px[1] = dds_scale((val & g_mask) >> g_sh, g_b);
                    px[2] = dds_scale((val & b_mask) >> b_sh, b_b);
                    px[3] = use_alpha ? dds_scale((val & a_mask) >> a_sh, a_b) : 255;
                }
            }
        }
        img->info.has_alpha = use_alpha;
        XX_IMAGE_SET_STR(img->info.compression, "none");
    }

    img->info.color_model = img->info.has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    img->info.channels = img->info.has_alpha ? 4 : 3;
    img->info.bits_per_pixel = 32;
    img->info.bits_per_sample = 8;
    img->info.frame_count = 1;
    img->info.mipmap_count = mipmaps;

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, g, "mipmaps", mipmaps);
        if (fourcc) xx_image_add_prop_text(img, g, "fourcc", fourcc_str);
        if (caps2 & 0x200) xx_image_add_prop_bool(img, g, "cubemap", true);
        if (dxgi_format) xx_image_add_prop_uint(img, g, "dxgi format", dxgi_format);
    }

    return XX_IMAGE_OK;
}

static xx_image_status dds_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    size_t pitch;
    size_t total_data;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || (uint64_t)w * h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;

    pitch = (size_t)w * 4;
    total_data = pitch * h;
    if (total_data > 0xFFFFFFFFULL - 128) return XX_IMAGE_ERR_LIMIT;

    /* 128-byte DDS Header */
    xx_image_buf_u32le(out, DDS_MAGIC);          /* dwMagic */
    xx_image_buf_u32le(out, 124);                /* dwSize */
    xx_image_buf_u32le(out, 0x100F);             /* dwFlags: CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT */
    xx_image_buf_u32le(out, h);                  /* dwHeight */
    xx_image_buf_u32le(out, w);                  /* dwWidth */
    xx_image_buf_u32le(out, (uint32_t)pitch);    /* dwPitchOrLinearSize */
    xx_image_buf_u32le(out, 0);                  /* dwDepth */
    xx_image_buf_u32le(out, 0);                  /* dwMipMapCount */
    xx_image_buf_fill(out, 0, 11 * 4);           /* dwReserved1 */

    /* DDS_PIXELFORMAT (32 bytes) */
    xx_image_buf_u32le(out, 32);                 /* dwSize */
    xx_image_buf_u32le(out, 0x41);               /* dwFlags: DDPF_RGB | DDPF_ALPHAPIXELS */
    xx_image_buf_u32le(out, 0);                  /* dwFourCC */
    xx_image_buf_u32le(out, 32);                 /* dwRGBBitCount */
    xx_image_buf_u32le(out, 0x00FF0000);         /* dwRBitMask */
    xx_image_buf_u32le(out, 0x0000FF00);         /* dwGBitMask */
    xx_image_buf_u32le(out, 0x000000FF);         /* dwBBitMask */
    xx_image_buf_u32le(out, 0xFF000000);         /* dwABitMask */

    xx_image_buf_u32le(out, 0x1000);             /* dwCaps: DDSCAPS_TEXTURE */
    xx_image_buf_fill(out, 0, 4 * 4);            /* dwCaps2..dwReserved2 */

    /* Write 32-bit BGRA uncompressed pixels (top-down) */
    if (!xx_image_buf_reserve(out, out->size + total_data)) return XX_IMAGE_ERR_MEMORY;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *src = f->rgba + (size_t)y * w * 4;
        uint8_t *dst = out->data + out->size;
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t *px = src + (size_t)x * 4;
            dst[x * 4]     = px[2]; /* B */
            dst[x * 4 + 1] = px[1]; /* G */
            dst[x * 4 + 2] = px[0]; /* R */
            dst[x * 4 + 3] = px[3]; /* A */
        }
        out->size += pitch;
    }

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_dds = {
    "dds",
    "DirectDraw Surface",
    "dds",
    "image/x-dds",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_INFO,
    582,
    dds_probe,
    dds_load,
    dds_save
};
