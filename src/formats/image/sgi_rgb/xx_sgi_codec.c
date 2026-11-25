/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Silicon Graphics Image (SGI / RGB) codec.
 * Load: verbatim and RLE encodings, 1 or 2 bytes per channel, 1..4 channels
 * (BW, BW+Alpha, RGB, RGBA), bottom-up rows, image name as property.
 * Save: 8-bit, verbatim or RLE (options->rle), 1 / 3 / 4 channels by content.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sgi_probe(const uint8_t *d, size_t n)
{
    if (n < 512) return 0;
    if (xx_image_rd_u16be(d) != 474) return 0;
    if (d[2] > 1) return 0; /* Storage: 0=verbatim, 1=RLE */
    if (d[3] != 1 && d[3] != 2) return 0; /* BPC: 1 or 2 */
    return 85;
}

static inline void sgi_set_pixel_chan(uint8_t *dst, uint32_t x, uint32_t z, uint32_t c, uint8_t val)
{
    uint8_t *px = dst + (size_t)x * 4;
    if (z == 1) {
        px[0] = px[1] = px[2] = val;
        px[3] = 255;
    } else if (z == 2) {
        if (c == 0) px[0] = px[1] = px[2] = val;
        else px[3] = val;
    } else if (z == 3) {
        px[c] = val;
        px[3] = 255;
    } else {
        px[c] = val;
    }
}

static xx_image_status sgi_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint8_t storage, bpc;
    uint16_t dim, xsize, ysize, zsize;
    uint32_t w, h, z;
    char name[81];
    xx_image_frame *f;
    xx_image_status st;
    const char *g = "SGI";

    if (n < 512 || xx_image_rd_u16be(d) != 474) return XX_IMAGE_ERR_FORMAT;
    storage = d[2];
    bpc = d[3];
    dim = xx_image_rd_u16be(d + 4);
    xsize = xx_image_rd_u16be(d + 6);
    ysize = xx_image_rd_u16be(d + 8);
    zsize = xx_image_rd_u16be(d + 10);

    if (storage > 1 || (bpc != 1 && bpc != 2)) return XX_IMAGE_ERR_FORMAT;
    w = xsize;
    h = (dim == 1) ? 1 : ysize;
    z = (dim <= 2) ? 1 : zsize;

    if (z < 1 || z > 4) return XX_IMAGE_ERR_UNSUPPORTED;
    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    memcpy(name, d + 24, 80);
    name[80] = '\0';

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    if (storage == 0) {
        /* Verbatim storage */
        size_t total_bytes = (size_t)w * h * z * bpc;
        if (!xx_image_in_range(n, 512, total_bytes)) return XX_IMAGE_ERR_CORRUPT;

        for (uint32_t c = 0; c < z; ++c) {
            for (uint32_t y = 0; y < h; ++y) {
                uint32_t ry = h - 1 - y; /* Bottom-up */
                uint8_t *dst = f->rgba + (size_t)ry * w * 4;
                size_t row_off = 512 + ((size_t)c * h + y) * w * bpc;

                for (uint32_t x = 0; x < w; ++x) {
                    uint8_t val = (bpc == 1) ? d[row_off + x] : d[row_off + x * 2];
                    sgi_set_pixel_chan(dst, x, z, c, val);
                }
            }
        }
    } else {
        /* RLE storage */
        size_t num_rows = (size_t)h * z;
        size_t tab_size = num_rows * 4;
        if (!xx_image_in_range(n, 512, tab_size * 2)) return XX_IMAGE_ERR_CORRUPT;

        for (uint32_t c = 0; c < z; ++c) {
            for (uint32_t y = 0; y < h; ++y) {
                uint32_t ry = h - 1 - y; /* Bottom-up */
                uint8_t *dst = f->rgba + (size_t)ry * w * 4;
                size_t row_idx = y + (size_t)c * h;
                uint32_t rle_off = xx_image_rd_u32be(d + 512 + row_idx * 4);
                uint32_t rle_len = xx_image_rd_u32be(d + 512 + tab_size + row_idx * 4);
                size_t rp, r_end;
                uint32_t x = 0;

                if (!xx_image_in_range(n, rle_off, rle_len)) return XX_IMAGE_ERR_CORRUPT;

                rp = rle_off;
                r_end = rle_off + rle_len;

                if (bpc == 1) {
                    while (rp < r_end && x < w) {
                        uint8_t code = d[rp++];
                        uint8_t count = code & 0x7F;
                        if (count == 0) break;
                        if (code & 0x80) {
                            /* Literal run */
                            while (count-- > 0 && rp < r_end && x < w) {
                                sgi_set_pixel_chan(dst, x++, z, c, d[rp++]);
                            }
                        } else {
                            /* Replicate run */
                            if (rp >= r_end) break;
                            uint8_t val = d[rp++];
                            while (count-- > 0 && x < w) {
                                sgi_set_pixel_chan(dst, x++, z, c, val);
                            }
                        }
                    }
                } else {
                    /* bpc == 2: uint16be tokens */
                    while (rp + 1 < r_end && x < w) {
                        uint16_t code = xx_image_rd_u16be(d + rp);
                        uint16_t count = code & 0x7F;
                        rp += 2;
                        if (count == 0) break;
                        if (code & 0x80) {
                            /* Literal run */
                            while (count-- > 0 && rp + 1 < r_end && x < w) {
                                sgi_set_pixel_chan(dst, x++, z, c, d[rp]);
                                rp += 2;
                            }
                        } else {
                            /* Replicate run */
                            if (rp + 1 >= r_end) break;
                            uint8_t val = d[rp];
                            rp += 2;
                            while (count-- > 0 && x < w) {
                                sgi_set_pixel_chan(dst, x++, z, c, val);
                            }
                        }
                    }
                }
            }
        }
    }

    img->info.bits_per_sample = bpc * 8;
    img->info.channels = z;
    img->info.bits_per_pixel = z * bpc * 8;
    img->info.has_alpha = (z == 2 || z == 4);
    img->info.is_compressed = (storage == 1);
    img->info.frame_count = 1;
    img->info.color_model = (z == 1) ? XX_IMAGE_COLOR_GRAY :
                           (z == 2) ? XX_IMAGE_COLOR_GRAY_ALPHA :
                           (z == 3) ? XX_IMAGE_COLOR_RGB : XX_IMAGE_COLOR_RGBA;
    XX_IMAGE_SET_STR(img->info.compression, storage == 1 ? "RLE" : "none");

    if (!o->skip_metadata) {
        if (name[0] != '\0') xx_image_add_prop_text(img, g, "name", name);
        xx_image_add_prop_uint(img, g, "storage", storage);
        xx_image_add_prop_uint(img, g, "bytes per channel", bpc);
        xx_image_add_prop_uint(img, g, "dimension", dim);
        xx_image_add_prop_uint(img, g, "channels", z);
        xx_image_add_prop_uint(img, g, "min pixel", (uint32_t)xx_image_rd_u32be(d + 12));
        xx_image_add_prop_uint(img, g, "max pixel", (uint32_t)xx_image_rd_u32be(d + 16));
    }

    return XX_IMAGE_OK;
}

static void sgi_encode_rle_row(xx_image_buffer *out, const uint8_t *row, uint32_t w)
{
    uint32_t x = 0;
    while (x < w) {
        /* Check for repeat run */
        uint32_t run = 1;
        while (x + run < w && run < 126 && row[x + run] == row[x]) {
            ++run;
        }
        if (run >= 3 || w - x < 3) {
            xx_image_buf_u8(out, (uint8_t)run);
            xx_image_buf_u8(out, row[x]);
            x += run;
        } else {
            /* Literal run */
            uint32_t lit = 0;
            while (x + lit < w && lit < 126) {
                if (x + lit + 2 < w && row[x + lit] == row[x + lit + 1] && row[x + lit] == row[x + lit + 2]) {
                    break;
                }
                ++lit;
            }
            if (lit == 0) lit = 1;
            xx_image_buf_u8(out, (uint8_t)(0x80 | lit));
            xx_image_buf_append(out, row + x, lit);
            x += lit;
        }
    }
    xx_image_buf_u8(out, 0); /* End of line token */
}

static xx_image_status sgi_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h, ch;
    bool rle;
    size_t start_off = out->size;
    size_t tab_pos = 0;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || w > 0xFFFF || h > 0xFFFF) return XX_IMAGE_ERR_LIMIT;

    if (xx_image_frame_has_alpha(f)) {
        ch = 4;
    } else if (xx_image_frame_is_gray(f)) {
        ch = 1;
    } else {
        ch = 3;
    }

    rle = o ? o->rle : false;

    /* 512-byte Header */
    xx_image_buf_u16be(out, 474);                      /* Magic */
    xx_image_buf_u8(out, rle ? 1 : 0);                 /* Storage */
    xx_image_buf_u8(out, 1);                           /* BPC */
    xx_image_buf_u16be(out, (uint16_t)(ch == 1 ? 2 : 3)); /* Dimension */
    xx_image_buf_u16be(out, (uint16_t)w);              /* Xsize */
    xx_image_buf_u16be(out, (uint16_t)h);              /* Ysize */
    xx_image_buf_u16be(out, (uint16_t)ch);             /* Zsize */
    xx_image_buf_u32be(out, 0);                        /* Pixmin */
    xx_image_buf_u32be(out, 255);                      /* Pixmax */
    xx_image_buf_fill(out, 0, 4);                      /* Dummy */
    xx_image_buf_append(out, "xxfc", 4);               /* Name */
    xx_image_buf_fill(out, 0, 76);                     /* Name padding (80 total) */
    xx_image_buf_u32be(out, 0);                        /* Colormap (normal) */
    xx_image_buf_fill(out, 0, 404);                    /* Dummy padding to 512 */

    if (!rle) {
        /* Verbatim save: channel by channel, row by row (bottom-up) */
        uint8_t *row_buf = (uint8_t *)malloc(w);
        if (!row_buf) return XX_IMAGE_ERR_MEMORY;

        for (uint32_t c = 0; c < ch; ++c) {
            for (uint32_t y = 0; y < h; ++y) {
                uint32_t sy = h - 1 - y; /* Bottom-up */
                const uint8_t *src = f->rgba + (size_t)sy * w * 4;
                for (uint32_t x = 0; x < w; ++x) {
                    row_buf[x] = src[x * 4 + c];
                }
                xx_image_buf_append(out, row_buf, w);
            }
        }
        free(row_buf);
    } else {
        /* RLE save: reserve start_tab and length_tab */
        size_t num_rows = (size_t)h * ch;
        uint8_t *row_buf = (uint8_t *)malloc(w);
        if (!row_buf) return XX_IMAGE_ERR_MEMORY;

        tab_pos = out->size;
        xx_image_buf_fill(out, 0, num_rows * 8);

        for (uint32_t c = 0; c < ch; ++c) {
            for (uint32_t y = 0; y < h; ++y) {
                size_t row_idx = y + (size_t)c * h;
                size_t row_start = out->size - start_off;
                size_t row_len;
                uint32_t sy = h - 1 - y; /* Bottom-up */
                const uint8_t *src = f->rgba + (size_t)sy * w * 4;

                for (uint32_t x = 0; x < w; ++x) {
                    row_buf[x] = src[x * 4 + c];
                }

                sgi_encode_rle_row(out, row_buf, w);
                row_len = (out->size - start_off) - row_start;

                xx_image_buf_put_u32be_at(out, tab_pos + row_idx * 4, (uint32_t)row_start);
                xx_image_buf_put_u32be_at(out, tab_pos + num_rows * 4 + row_idx * 4, (uint32_t)row_len);
            }
        }
        free(row_buf);
    }

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_sgi = {
    "sgi",
    "Silicon Graphics Image",
    "sgi;rgb;rgba;bw;int;inta",
    "image/sgi",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_INFO,
    0,
    sgi_probe,
    sgi_load,
    sgi_save
};
