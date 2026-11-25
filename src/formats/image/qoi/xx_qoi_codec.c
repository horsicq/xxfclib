/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Quite OK Image (QOI) format specification.
 * Load: 3-channel (RGB) and 4-channel (RGBA) QOI images.
 * Save: 3-channel (RGB) or 4-channel (RGBA, if alpha < 255) QOI images.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdlib.h>
#include <string.h>

#define QOI_MAGIC 0x716F6966U /* "qoif" */
#define QOI_OP_INDEX 0x00U
#define QOI_OP_DIFF 0x40U
#define QOI_OP_LUMA 0x80U
#define QOI_OP_RUN 0xC0U
#define QOI_OP_RGB 0xFEU
#define QOI_OP_RGBA 0xFFU
#define QOI_MASK_2 0xC0U

static const uint8_t g_qoi_padding[8] = {0, 0, 0, 0, 0, 0, 0, 1};

static int qoi_probe(const uint8_t *d, size_t n)
{
    if (n < 14) return 0;
    if (xx_image_rd_u32be(d) == QOI_MAGIC) return 100;
    return 0;
}

static inline unsigned int qoi_color_hash(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (unsigned int)(r * 3U + g * 5U + b * 7U + a * 11U) % 64U;
}

static xx_image_status qoi_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint32_t w, h;
    uint8_t channels, colorspace;
    xx_image_frame *f;
    xx_image_status st;
    size_t total_pixels, px_idx, p, limit;
    uint8_t index[64][4];
    uint8_t r = 0, g = 0, b = 0, a = 255;

    if (n < 22) return XX_IMAGE_ERR_CORRUPT;
    if (xx_image_rd_u32be(d) != QOI_MAGIC) return XX_IMAGE_ERR_FORMAT;

    w = xx_image_rd_u32be(d + 4);
    h = xx_image_rd_u32be(d + 8);
    channels = d[12];
    colorspace = d[13];

    if (channels != 3 && channels != 4) return XX_IMAGE_ERR_CORRUPT;
    if (colorspace > 1) return XX_IMAGE_ERR_CORRUPT;

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    /* Verify 8-byte end padding */
    if (memcmp(d + n - 8, g_qoi_padding, 8) != 0) return XX_IMAGE_ERR_CORRUPT;

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    memset(index, 0, sizeof(index));
    total_pixels = (size_t)w * h;
    px_idx = 0;
    p = 14;
    limit = n - 8;

    while (px_idx < total_pixels && p < limit) {
        uint8_t b1 = d[p++];

        if (b1 == QOI_OP_RGB) {
            if (p + 3 > limit) return XX_IMAGE_ERR_CORRUPT;
            r = d[p++];
            g = d[p++];
            b = d[p++];
        } else if (b1 == QOI_OP_RGBA) {
            if (p + 4 > limit) return XX_IMAGE_ERR_CORRUPT;
            r = d[p++];
            g = d[p++];
            b = d[p++];
            a = d[p++];
        } else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
            unsigned int idx = b1 & 0x3FU;
            r = index[idx][0];
            g = index[idx][1];
            b = index[idx][2];
            a = index[idx][3];
        } else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
            int dr = (int)((b1 >> 4) & 0x03U) - 2;
            int dg = (int)((b1 >> 2) & 0x03U) - 2;
            int db = (int)(b1 & 0x03U) - 2;
            r = (uint8_t)(r + dr);
            g = (uint8_t)(g + dg);
            b = (uint8_t)(b + db);
        } else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
            int dg;
            int dr_dg, db_dg;
            uint8_t b2;
            if (p >= limit) return XX_IMAGE_ERR_CORRUPT;
            b2 = d[p++];
            dg = (int)(b1 & 0x3FU) - 32;
            dr_dg = (int)((b2 >> 4) & 0x0FU) - 8;
            db_dg = (int)(b2 & 0x0FU) - 8;
            r = (uint8_t)(r + dg + dr_dg);
            g = (uint8_t)(g + dg);
            b = (uint8_t)(b + dg + db_dg);
        } else {
            /* QOI_OP_RUN */
            int run = (int)(b1 & 0x3FU) + 1;
            if (px_idx + (size_t)run > total_pixels) return XX_IMAGE_ERR_CORRUPT;
            for (int k = 0; k < run; ++k) {
                uint8_t *dst = f->rgba + (px_idx++) * 4;
                dst[0] = r;
                dst[1] = g;
                dst[2] = b;
                dst[3] = a;
            }
            continue;
        }

        {
            unsigned int hval = qoi_color_hash(r, g, b, a);
            index[hval][0] = r;
            index[hval][1] = g;
            index[hval][2] = b;
            index[hval][3] = a;
        }

        {
            uint8_t *dst = f->rgba + (px_idx++) * 4;
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = a;
        }
    }

    if (px_idx != total_pixels) return XX_IMAGE_ERR_CORRUPT;

    img->info.color_model = (channels == 4) ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    img->info.bits_per_pixel = (uint32_t)channels * 8;
    img->info.bits_per_sample = 8;
    img->info.channels = channels;
    img->info.has_alpha = (channels == 4);
    img->info.frame_count = 1;
    XX_IMAGE_SET_STR(img->info.compression, "QOI");
    XX_IMAGE_SET_STR(img->info.color_space, colorspace == 0 ? "sRGB" : "linear");

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, "QOI", "width", w);
        xx_image_add_prop_uint(img, "QOI", "height", h);
        xx_image_add_prop_uint(img, "QOI", "channels", channels);
        xx_image_add_prop_uint(img, "QOI", "colorspace", colorspace);
        xx_image_add_prop_text(img, "QOI", "color space", colorspace == 0 ? "sRGB" : "linear");
    }

    return XX_IMAGE_OK;
}

static xx_image_status qoi_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    size_t total_pixels;
    bool has_alpha;
    uint8_t channels;
    uint8_t index[64][4];
    uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
    int run = 0;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    total_pixels = (size_t)w * h;

    has_alpha = xx_image_frame_has_alpha(f);
    channels = has_alpha ? 4 : 3;

    if (!xx_image_buf_reserve(out, out->size + 14 + total_pixels * 5 + 8)) return XX_IMAGE_ERR_MEMORY;

    /* Header */
    xx_image_buf_append(out, "qoif", 4);
    xx_image_buf_u32be(out, w);
    xx_image_buf_u32be(out, h);
    xx_image_buf_u8(out, channels);
    xx_image_buf_u8(out, 0); /* sRGB */

    memset(index, 0, sizeof(index));

    for (size_t i = 0; i < total_pixels; ++i) {
        const uint8_t *px = f->rgba + i * 4;
        uint8_t r = px[0];
        uint8_t g = px[1];
        uint8_t b = px[2];
        uint8_t a = channels == 4 ? px[3] : 255;

        if (r == pr && g == pg && b == pb && a == pa) {
            ++run;
            if (run == 62) {
                xx_image_buf_u8(out, (uint8_t)(QOI_OP_RUN | (run - 1)));
                run = 0;
            }
        } else {
            unsigned int hval;
            if (run > 0) {
                xx_image_buf_u8(out, (uint8_t)(QOI_OP_RUN | (run - 1)));
                run = 0;
            }

            hval = qoi_color_hash(r, g, b, a);
            if (index[hval][0] == r && index[hval][1] == g && index[hval][2] == b && index[hval][3] == a) {
                xx_image_buf_u8(out, (uint8_t)(QOI_OP_INDEX | hval));
            } else {
                index[hval][0] = r;
                index[hval][1] = g;
                index[hval][2] = b;
                index[hval][3] = a;

                if (a == pa) {
                    int dr = (int)r - (int)pr;
                    int dg = (int)g - (int)pg;
                    int db = (int)b - (int)pb;
                    int dr_dg = dr - dg;
                    int db_dg = db - dg;

                    if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
                        xx_image_buf_u8(out, (uint8_t)(QOI_OP_DIFF | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2)));
                    } else if (dg >= -32 && dg <= 31 && dr_dg >= -8 && dr_dg <= 7 && db_dg >= -8 && db_dg <= 7) {
                        xx_image_buf_u8(out, (uint8_t)(QOI_OP_LUMA | (dg + 32)));
                        xx_image_buf_u8(out, (uint8_t)(((dr_dg + 8) << 4) | (db_dg + 8)));
                    } else {
                        xx_image_buf_u8(out, QOI_OP_RGB);
                        xx_image_buf_u8(out, r);
                        xx_image_buf_u8(out, g);
                        xx_image_buf_u8(out, b);
                    }
                } else {
                    xx_image_buf_u8(out, QOI_OP_RGBA);
                    xx_image_buf_u8(out, r);
                    xx_image_buf_u8(out, g);
                    xx_image_buf_u8(out, b);
                    xx_image_buf_u8(out, a);
                }
            }

            pr = r;
            pg = g;
            pb = b;
            pa = a;
        }
    }

    if (run > 0) {
        xx_image_buf_u8(out, (uint8_t)(QOI_OP_RUN | (run - 1)));
    }

    /* 8-byte end padding */
    xx_image_buf_append(out, g_qoi_padding, 8);

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_qoi = {
    "qoi",
    "Quite OK Image",
    "qoi",
    "image/qoi",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_INFO,
    639 /* XX_FILE_TYPE_QOI */,
    qoi_probe,
    qoi_load,
    qoi_save
};
