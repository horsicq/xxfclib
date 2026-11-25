/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Sun Raster (.ras, .sun) image format codec.
 * Load: 1/8/24/32 bpp, standard (type 1), RLE (type 2), and RGB (type 3) encodings,
 * planar RGB colormaps (type 1), 16-bit row stride alignment.
 * Save: 24-bit standard (BGR byte order) or 8-bit indexed with planar colormap.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUNRAS_MAGIC 0x59A66A95U

static int sunras_probe(const uint8_t *d, size_t n)
{
    if (n < 32) return 0;
    if (xx_image_rd_u32be(d) != SUNRAS_MAGIC) return 0;
    return 95;
}

static xx_image_status sunras_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint32_t w, h, depth, len, type, maptype, maplen;
    size_t stride, data_off;
    uint32_t pal[256];
    uint32_t pal_count = 0;
    const uint8_t *pixel_data = NULL;
    uint8_t *rle_unpacked = NULL;
    xx_image_frame *f;
    xx_image_status st;
    const char *g = "SunRaster";

    if (n < 32 || xx_image_rd_u32be(d) != SUNRAS_MAGIC) return XX_IMAGE_ERR_FORMAT;
    w = xx_image_rd_u32be(d + 4);
    h = xx_image_rd_u32be(d + 8);
    depth = xx_image_rd_u32be(d + 12);
    len = xx_image_rd_u32be(d + 16);
    type = xx_image_rd_u32be(d + 20);
    maptype = xx_image_rd_u32be(d + 24);
    maplen = xx_image_rd_u32be(d + 28);

    if (depth != 1 && depth != 8 && depth != 24 && depth != 32) return XX_IMAGE_ERR_UNSUPPORTED;
    if (type > 3) return XX_IMAGE_ERR_UNSUPPORTED;

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    /* Stride is aligned to 16-bit word (2 bytes) */
    stride = (((size_t)w * depth + 15) / 16) * 2;
    if (stride == 0) return XX_IMAGE_ERR_CORRUPT;

    data_off = 32 + (size_t)maplen;
    if (!xx_image_in_range(n, 32, maplen)) return XX_IMAGE_ERR_CORRUPT;

    /* Colormap */
    for (uint32_t i = 0; i < 256; ++i) pal[i] = 0x000000FF;
    if (maplen > 0) {
        if (maptype != 1) return XX_IMAGE_ERR_UNSUPPORTED; /* Only RMT_EQUAL_RGB */
        pal_count = maplen / 3;
        if (pal_count > 256) pal_count = 256;
        for (uint32_t i = 0; i < pal_count; ++i) {
            uint8_t r = d[32 + i];
            uint8_t gval = d[32 + (maplen / 3) + i];
            uint8_t b = d[32 + 2 * (maplen / 3) + i];
            pal[i] = ((uint32_t)r << 24) | ((uint32_t)gval << 16) | ((uint32_t)b << 8) | 0xFF;
        }
        memcpy(img->palette, pal, pal_count * sizeof(uint32_t));
        img->palette_count = pal_count;
    }

    if (type == 2) {
        /* RLE Byte-encoded */
        size_t total_bytes = stride * h;
        size_t p = data_off, done = 0;

        rle_unpacked = (uint8_t *)malloc(total_bytes);
        if (!rle_unpacked) return XX_IMAGE_ERR_MEMORY;

        while (p < n && done < total_bytes) {
            uint8_t b = d[p++];
            if (b != 0x80) {
                rle_unpacked[done++] = b;
            } else {
                if (p >= n) break;
                uint8_t count = d[p++];
                if (count == 0) {
                    rle_unpacked[done++] = 0x80;
                } else {
                    if (p >= n) break;
                    uint8_t val = d[p++];
                    size_t reps = (size_t)count + 1;
                    while (reps-- > 0 && done < total_bytes) {
                        rle_unpacked[done++] = val;
                    }
                }
            }
        }
        pixel_data = rle_unpacked;
    } else {
        /* Standard or RGB uncompressed */
        if (!xx_image_in_range(n, data_off, stride * h)) {
            return XX_IMAGE_ERR_CORRUPT;
        }
        pixel_data = d + data_off;
    }

    f = xx_image_add_frame(img, w, h);
    if (!f) {
        free(rle_unpacked);
        return XX_IMAGE_ERR_MEMORY;
    }

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *s = pixel_data + (size_t)y * stride;
        uint8_t *dst = f->rgba + (size_t)y * w * 4;

        if (depth == 1) {
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t bit = (s[x / 8] >> (7 - (x % 8))) & 1;
                if (pal_count > 0) {
                    xx_image_put_rgba(dst + x * 4, pal[bit]);
                } else {
                    /* Standard Sun raster 1-bit: 1=black, 0=white */
                    uint8_t v = bit ? 0 : 255;
                    dst[x * 4] = dst[x * 4 + 1] = dst[x * 4 + 2] = v;
                    dst[x * 4 + 3] = 255;
                }
            }
        } else if (depth == 8) {
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t idx = s[x];
                if (pal_count > 0) {
                    xx_image_put_rgba(dst + x * 4, pal[idx]);
                } else {
                    dst[x * 4] = dst[x * 4 + 1] = dst[x * 4 + 2] = idx;
                    dst[x * 4 + 3] = 255;
                }
            }
        } else if (depth == 24) {
            for (uint32_t x = 0; x < w; ++x) {
                if (type == 3) {
                    /* RGB format */
                    dst[x * 4]     = s[x * 3];
                    dst[x * 4 + 1] = s[x * 3 + 1];
                    dst[x * 4 + 2] = s[x * 3 + 2];
                } else {
                    /* Standard BGR format */
                    dst[x * 4]     = s[x * 3 + 2];
                    dst[x * 4 + 1] = s[x * 3 + 1];
                    dst[x * 4 + 2] = s[x * 3];
                }
                dst[x * 4 + 3] = 255;
            }
        } else { /* depth == 32 */
            for (uint32_t x = 0; x < w; ++x) {
                if (type == 3) {
                    /* RGBX format */
                    dst[x * 4]     = s[x * 4];
                    dst[x * 4 + 1] = s[x * 4 + 1];
                    dst[x * 4 + 2] = s[x * 4 + 2];
                } else {
                    /* Standard BGRX format */
                    dst[x * 4]     = s[x * 4 + 2];
                    dst[x * 4 + 1] = s[x * 4 + 1];
                    dst[x * 4 + 2] = s[x * 4];
                }
                dst[x * 4 + 3] = 255;
            }
        }
    }

    free(rle_unpacked);

    img->info.bits_per_pixel = depth;
    img->info.bits_per_sample = (depth == 1 || depth == 8) ? depth : 8;
    img->info.channels = (depth == 1 || depth == 8) ? 1 : (depth == 24 ? 3 : 4);
    img->info.has_alpha = false;
    img->info.is_compressed = (type == 2);
    img->info.frame_count = 1;
    img->info.color_model = (depth == 1) ? XX_IMAGE_COLOR_BILEVEL :
                           (depth == 8) ? (pal_count > 0 ? XX_IMAGE_COLOR_INDEXED : XX_IMAGE_COLOR_GRAY) :
                           XX_IMAGE_COLOR_RGB;
    img->info.palette_entries = pal_count;
    XX_IMAGE_SET_STR(img->info.compression, type == 2 ? "RLE" : "none");

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, g, "type", type);
        xx_image_add_prop_uint(img, g, "depth", depth);
        xx_image_add_prop_uint(img, g, "length", len);
        xx_image_add_prop_uint(img, g, "colormap type", maptype);
        xx_image_add_prop_uint(img, g, "colormap length", maplen);
    }

    return XX_IMAGE_OK;
}

static xx_image_status sunras_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    int req_bpp;
    bool save_pal = false;
    uint8_t *idx = NULL;
    uint32_t pal[256];
    uint32_t pal_count = 0;
    size_t stride;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || (uint64_t)w * h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;

    req_bpp = o ? o->bits_per_pixel : 0;
    if (req_bpp == 8) {
        save_pal = true;
    } else if (req_bpp == 24 || req_bpp == 32) {
        save_pal = false;
    } else {
        /* Check if 8-bit palette quantisation works */
        idx = (uint8_t *)malloc((size_t)w * h);
        if (!idx) return XX_IMAGE_ERR_MEMORY;
        pal_count = xx_image_quantize(f, 256, idx, pal);
        if (pal_count > 0 && pal_count <= 256) {
            size_t n = (size_t)w * h;
            bool exact = true;
            for (size_t i = 0; i < n; ++i) {
                if (pal[idx[i]] != xx_image_get_rgba(f->rgba + i * 4)) {
                    exact = false;
                    break;
                }
            }
            if (exact) {
                save_pal = true;
            } else {
                free(idx);
                idx = NULL;
                save_pal = false;
            }
        } else {
            free(idx);
            idx = NULL;
            save_pal = false;
        }
    }

    if (save_pal) {
        if (!idx) {
            idx = (uint8_t *)malloc((size_t)w * h);
            if (!idx) return XX_IMAGE_ERR_MEMORY;
            pal_count = xx_image_quantize(f, 256, idx, pal);
            if (pal_count == 0) {
                free(idx);
                return XX_IMAGE_ERR_MEMORY;
            }
        }

        stride = (((size_t)w + 1) / 2) * 2;
        if ((uint64_t)stride * h > 0xFFFFFFFFULL) {
            free(idx);
            return XX_IMAGE_ERR_LIMIT;
        }

        /* 32-byte header */
        xx_image_buf_u32be(out, SUNRAS_MAGIC);
        xx_image_buf_u32be(out, w);
        xx_image_buf_u32be(out, h);
        xx_image_buf_u32be(out, 8);                    /* Depth */
        xx_image_buf_u32be(out, (uint32_t)(stride * h)); /* Length */
        xx_image_buf_u32be(out, 1);                    /* Type 1: Standard */
        xx_image_buf_u32be(out, 1);                    /* ColorMapType: RMT_EQUAL_RGB */
        xx_image_buf_u32be(out, pal_count * 3);        /* ColorMapLength */

        /* Planar Colormap: R, then G, then B */
        for (uint32_t i = 0; i < pal_count; ++i) xx_image_buf_u8(out, (uint8_t)(pal[i] >> 24));
        for (uint32_t i = 0; i < pal_count; ++i) xx_image_buf_u8(out, (uint8_t)(pal[i] >> 16));
        for (uint32_t i = 0; i < pal_count; ++i) xx_image_buf_u8(out, (uint8_t)(pal[i] >> 8));

        /* Rows */
        for (uint32_t y = 0; y < h; ++y) {
            xx_image_buf_append(out, idx + (size_t)y * w, w);
            if (stride > w) xx_image_buf_fill(out, 0, stride - w);
        }
        free(idx);
    } else {
        /* 24-bit standard (BGR byte order) */
        stride = (((size_t)w * 3 + 1) / 2) * 2;
        if ((uint64_t)stride * h > 0xFFFFFFFFULL) return XX_IMAGE_ERR_LIMIT;

        xx_image_buf_u32be(out, SUNRAS_MAGIC);
        xx_image_buf_u32be(out, w);
        xx_image_buf_u32be(out, h);
        xx_image_buf_u32be(out, 24);                   /* Depth */
        xx_image_buf_u32be(out, (uint32_t)(stride * h)); /* Length */
        xx_image_buf_u32be(out, 1);                    /* Type 1: Standard */
        xx_image_buf_u32be(out, 0);                    /* ColorMapType: None */
        xx_image_buf_u32be(out, 0);                    /* ColorMapLength */

        if (!xx_image_buf_reserve(out, out->size + stride * h)) return XX_IMAGE_ERR_MEMORY;

        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *src = f->rgba + (size_t)y * w * 4;
            uint8_t *dst = out->data + out->size;
            memset(dst, 0, stride);
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t *px = src + (size_t)x * 4;
                uint8_t a = px[3];
                /* Composite onto black */
                dst[x * 3]     = (uint8_t)(px[2] * a / 255); /* B */
                dst[x * 3 + 1] = (uint8_t)(px[1] * a / 255); /* G */
                dst[x * 3 + 2] = (uint8_t)(px[0] * a / 255); /* R */
            }
            out->size += stride;
        }
    }

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_sunras = {
    "sunras",
    "Sun Raster",
    "ras;sun;im1;im8;im24;im32",
    "image/x-sun-raster",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_INFO,
    643,
    sunras_probe,
    sunras_load,
    sunras_save
};
