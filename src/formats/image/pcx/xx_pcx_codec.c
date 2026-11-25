/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZSoft PCX image format codec.
 * Load: 1/2/4/8 bpp with 1..4 planes, 24-bit (3 planes x 8 bit), RLE decoding,
 * 16-colour header palette, 256-colour VGA palette at end of file (0x0C marker),
 * and grayscale PCX.
 * Save: 24-bit (3 planes) or 8-bit palette (when <= 256 colours), RLE encoded.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pcx_probe(const uint8_t *d, size_t n)
{
    if (n < 128) return 0;
    if (d[0] != 0x0A) return 0;
    if (d[1] > 5) return 0;
    if (d[2] != 1) return 0;
    if (d[3] != 1 && d[3] != 2 && d[3] != 4 && d[3] != 8) return 0;
    if (d[65] < 1 || d[65] > 4) return 0;
    return 80;
}

static xx_image_status pcx_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint8_t version, bpp, nplanes;
    uint16_t xmin, ymin, xmax, ymax, hdpi, vdpi, bpl, pal_info;
    uint32_t w, h;
    uint32_t total_bpp;
    size_t scanline_bytes;
    uint8_t *scanline = NULL;
    uint32_t pal[256];
    uint32_t pal_count = 0;
    bool is_gray = false;
    xx_image_frame *f;
    xx_image_status st;
    size_t p = 128;
    const char *g = "PCX";

    if (n < 128 || d[0] != 0x0A || d[2] != 1) return XX_IMAGE_ERR_FORMAT;
    version = d[1];
    bpp = d[3];
    xmin = xx_image_rd_u16le(d + 4);
    ymin = xx_image_rd_u16le(d + 6);
    xmax = xx_image_rd_u16le(d + 8);
    ymax = xx_image_rd_u16le(d + 10);
    hdpi = xx_image_rd_u16le(d + 12);
    vdpi = xx_image_rd_u16le(d + 14);
    nplanes = d[65];
    bpl = xx_image_rd_u16le(d + 66);
    pal_info = xx_image_rd_u16le(d + 68);

    if (xmax < xmin || ymax < ymin) return XX_IMAGE_ERR_CORRUPT;
    w = (uint32_t)(xmax - xmin + 1);
    h = (uint32_t)(ymax - ymin + 1);
    if (nplanes < 1 || nplanes > 4) return XX_IMAGE_ERR_UNSUPPORTED;
    if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) return XX_IMAGE_ERR_UNSUPPORTED;
    total_bpp = (uint32_t)bpp * nplanes;
    if (total_bpp != 1 && total_bpp != 2 && total_bpp != 4 && total_bpp != 8 && total_bpp != 24) {
        return XX_IMAGE_ERR_UNSUPPORTED;
    }

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    scanline_bytes = (size_t)nplanes * bpl;
    if (scanline_bytes == 0 || (size_t)bpl < ((size_t)w * bpp + 7) / 8) {
        return XX_IMAGE_ERR_CORRUPT;
    }

    scanline = (uint8_t *)malloc(scanline_bytes);
    if (!scanline) return XX_IMAGE_ERR_MEMORY;

    /* Palette initialization */
    for (uint32_t i = 0; i < 256; ++i) pal[i] = 0x000000FF;

    if (total_bpp == 8) {
        /* Check 256-color palette at EOF */
        if (n >= 769 && d[n - 769] == 0x0C) {
            const uint8_t *cp = d + n - 768;
            bool linear_gray = true;
            for (uint32_t i = 0; i < 256; ++i) {
                uint8_t r = cp[i * 3];
                uint8_t gval = cp[i * 3 + 1];
                uint8_t b = cp[i * 3 + 2];
                pal[i] = ((uint32_t)r << 24) | ((uint32_t)gval << 16) | ((uint32_t)b << 8) | 0xFF;
                if (r != i || gval != i || b != i) linear_gray = false;
            }
            pal_count = 256;
            if (linear_gray) is_gray = true;
        } else if (pal_info == 2) {
            is_gray = true;
            for (uint32_t i = 0; i < 256; ++i) {
                pal[i] = ((uint32_t)i << 24) | ((uint32_t)i << 16) | ((uint32_t)i << 8) | 0xFF;
            }
            pal_count = 256;
        } else {
            /* Fall back to header 16-color palette */
            for (uint32_t i = 0; i < 16; ++i) {
                pal[i] = ((uint32_t)d[16 + i * 3] << 24) | ((uint32_t)d[16 + i * 3 + 1] << 16) | ((uint32_t)d[16 + i * 3 + 2] << 8) | 0xFF;
            }
            pal_count = 16;
        }
    } else if (total_bpp <= 4) {
        uint32_t count = 1U << total_bpp;
        bool all_zero = true;
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t r = d[16 + i * 3];
            uint8_t gval = d[16 + i * 3 + 1];
            uint8_t b = d[16 + i * 3 + 2];
            if (r || gval || b) all_zero = false;
            pal[i] = ((uint32_t)r << 24) | ((uint32_t)gval << 16) | ((uint32_t)b << 8) | 0xFF;
        }
        pal_count = count;
        if (all_zero && total_bpp == 1) {
            pal[0] = 0x000000FF;
            pal[1] = 0xFFFFFFFF;
        }
    }

    if (pal_count > 0) {
        memcpy(img->palette, pal, pal_count * sizeof(uint32_t));
        img->palette_count = pal_count;
    }

    f = xx_image_add_frame(img, w, h);
    if (!f) {
        free(scanline);
        return XX_IMAGE_ERR_MEMORY;
    }

    /* RLE scanline decoding */
    for (uint32_t y = 0; y < h; ++y) {
        size_t done = 0;
        uint8_t *dst = f->rgba + (size_t)y * w * 4;
        memset(scanline, 0, scanline_bytes);

        while (done < scanline_bytes && p < n) {
            uint8_t byte = d[p++];
            if ((byte & 0xC0) == 0xC0) {
                uint8_t count = byte & 0x3F;
                uint8_t val = (p < n) ? d[p++] : 0;
                while (count-- > 0 && done < scanline_bytes) {
                    scanline[done++] = val;
                }
            } else {
                scanline[done++] = byte;
            }
        }

        if (total_bpp == 24) {
            const uint8_t *r_plane = scanline;
            const uint8_t *g_plane = scanline + bpl;
            const uint8_t *b_plane = scanline + 2 * (size_t)bpl;
            for (uint32_t x = 0; x < w; ++x) {
                dst[x * 4] = r_plane[x];
                dst[x * 4 + 1] = g_plane[x];
                dst[x * 4 + 2] = b_plane[x];
                dst[x * 4 + 3] = 255;
            }
        } else if (total_bpp == 8) {
            for (uint32_t x = 0; x < w; ++x) {
                xx_image_put_rgba(dst + x * 4, pal[scanline[x]]);
            }
        } else if (total_bpp == 4) {
            if (nplanes == 4) {
                for (uint32_t x = 0; x < w; ++x) {
                    size_t bi = x / 8;
                    unsigned sh = 7 - (x % 8);
                    uint8_t idx = (uint8_t)(((scanline[bi] >> sh) & 1) |
                                            (((scanline[bpl + bi] >> sh) & 1) << 1) |
                                            (((scanline[2 * (size_t)bpl + bi] >> sh) & 1) << 2) |
                                            (((scanline[3 * (size_t)bpl + bi] >> sh) & 1) << 3));
                    xx_image_put_rgba(dst + x * 4, pal[idx]);
                }
            } else {
                for (uint32_t x = 0; x < w; ++x) {
                    uint8_t byte = scanline[x / 2];
                    uint8_t idx = (x & 1) ? (byte & 0x0F) : (byte >> 4);
                    xx_image_put_rgba(dst + x * 4, pal[idx]);
                }
            }
        } else if (total_bpp == 2) {
            if (nplanes == 2) {
                for (uint32_t x = 0; x < w; ++x) {
                    size_t bi = x / 8;
                    unsigned sh = 7 - (x % 8);
                    uint8_t idx = (uint8_t)(((scanline[bi] >> sh) & 1) |
                                            (((scanline[bpl + bi] >> sh) & 1) << 1));
                    xx_image_put_rgba(dst + x * 4, pal[idx]);
                }
            } else {
                for (uint32_t x = 0; x < w; ++x) {
                    uint8_t idx = (scanline[x / 4] >> (6 - 2 * (x % 4))) & 3;
                    xx_image_put_rgba(dst + x * 4, pal[idx]);
                }
            }
        } else { /* total_bpp == 1 */
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t bit = (scanline[x / 8] >> (7 - (x % 8))) & 1;
                xx_image_put_rgba(dst + x * 4, pal[bit]);
            }
        }
    }

    free(scanline);

    img->info.bits_per_pixel = total_bpp;
    img->info.bits_per_sample = bpp;
    img->info.channels = total_bpp == 24 ? 3 : 1;
    img->info.has_alpha = false;
    img->info.frame_count = 1;
    img->info.is_compressed = true;
    img->info.color_model = total_bpp == 24 ? XX_IMAGE_COLOR_RGB :
                           total_bpp == 1 ? XX_IMAGE_COLOR_BILEVEL :
                           is_gray ? XX_IMAGE_COLOR_GRAY : XX_IMAGE_COLOR_INDEXED;
    img->info.palette_entries = pal_count;
    img->info.dpi_x = hdpi;
    img->info.dpi_y = vdpi;
    XX_IMAGE_SET_STR(img->info.compression, "RLE");

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, g, "version", version);
        xx_image_add_prop_uint(img, g, "bits per pixel", bpp);
        xx_image_add_prop_uint(img, g, "planes", nplanes);
        xx_image_add_prop_uint(img, g, "bytes per line", bpl);
        xx_image_add_prop_uint(img, g, "palette info", pal_info);
        xx_image_add_prop_uint(img, g, "dpi x", hdpi);
        xx_image_add_prop_uint(img, g, "dpi y", vdpi);
    }

    return XX_IMAGE_OK;
}

static void pcx_encode_bytes(xx_image_buffer *out, const uint8_t *data, size_t size)
{
    size_t i = 0;
    while (i < size) {
        uint8_t val = data[i];
        size_t run = 1;
        while (i + run < size && run < 63 && data[i + run] == val) {
            ++run;
        }
        if (run > 1 || (val & 0xC0) == 0xC0) {
            xx_image_buf_u8(out, (uint8_t)(0xC0 | run));
            xx_image_buf_u8(out, val);
        } else {
            xx_image_buf_u8(out, val);
        }
        i += run;
    }
}

static xx_image_status pcx_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    int req_bpp;
    bool save_pal = false;
    uint8_t *idx = NULL;
    uint32_t pal[256];
    uint32_t pal_count = 0;
    uint16_t bpl;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || w > 0xFFFF || h > 0xFFFF) return XX_IMAGE_ERR_LIMIT;

    req_bpp = o->bits_per_pixel;
    if (req_bpp == 8) {
        save_pal = true;
    } else if (req_bpp == 24) {
        save_pal = false;
    } else {
        /* Check if image fits within 256 colours */
        idx = (uint8_t *)malloc((size_t)w * h);
        if (!idx) return XX_IMAGE_ERR_MEMORY;
        pal_count = xx_image_quantize(f, 256, idx, pal);
        if (pal_count > 0 && pal_count <= 256 && !xx_image_frame_has_alpha(f)) {
            /* Check if quantize was exact */
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

    if (save_pal && !idx) {
        idx = (uint8_t *)malloc((size_t)w * h);
        if (!idx) return XX_IMAGE_ERR_MEMORY;
        pal_count = xx_image_quantize(f, 256, idx, pal);
        if (pal_count == 0) {
            free(idx);
            return XX_IMAGE_ERR_MEMORY;
        }
    }

    bpl = (uint16_t)((w + 1) & ~1U); /* Bytes per line must be even */

    /* 128-byte Header */
    xx_image_buf_u8(out, 0x0A);  /* Manufacturer */
    xx_image_buf_u8(out, 5);     /* Version 3.0 */
    xx_image_buf_u8(out, 1);     /* RLE Encoding */
    xx_image_buf_u8(out, 8);     /* BitsPerPixel */
    xx_image_buf_u16le(out, 0);  /* Xmin */
    xx_image_buf_u16le(out, 0);  /* Ymin */
    xx_image_buf_u16le(out, (uint16_t)(w - 1)); /* Xmax */
    xx_image_buf_u16le(out, (uint16_t)(h - 1)); /* Ymax */
    xx_image_buf_u16le(out, (uint16_t)(img->info.dpi_x > 0 ? img->info.dpi_x : 72));
    xx_image_buf_u16le(out, (uint16_t)(img->info.dpi_y > 0 ? img->info.dpi_y : 72));
    /* 16-color colormap (48 bytes) */
    if (save_pal && pal_count <= 16) {
        for (uint32_t i = 0; i < 16; ++i) {
            if (i < pal_count) {
                xx_image_buf_u8(out, (uint8_t)(pal[i] >> 24));
                xx_image_buf_u8(out, (uint8_t)(pal[i] >> 16));
                xx_image_buf_u8(out, (uint8_t)(pal[i] >> 8));
            } else {
                xx_image_buf_fill(out, 0, 3);
            }
        }
    } else {
        xx_image_buf_fill(out, 0, 48);
    }
    xx_image_buf_u8(out, 0);                      /* Reserved */
    xx_image_buf_u8(out, save_pal ? 1 : 3);        /* NPlanes */
    xx_image_buf_u16le(out, bpl);                 /* BytesPerLine */
    xx_image_buf_u16le(out, 1);                   /* PaletteInfo (color/BW) */
    xx_image_buf_u16le(out, (uint16_t)w);         /* HScreenSize */
    xx_image_buf_u16le(out, (uint16_t)h);         /* VScreenSize */
    xx_image_buf_fill(out, 0, 54);                /* Filler */

    if (save_pal) {
        uint8_t *row_buf = (uint8_t *)calloc(bpl, 1);
        if (!row_buf) {
            free(idx);
            return XX_IMAGE_ERR_MEMORY;
        }
        for (uint32_t y = 0; y < h; ++y) {
            memcpy(row_buf, idx + (size_t)y * w, w);
            pcx_encode_bytes(out, row_buf, bpl);
        }
        free(row_buf);
        free(idx);

        /* 256-color palette at end of file */
        xx_image_buf_u8(out, 0x0C);
        for (uint32_t i = 0; i < 256; ++i) {
            if (i < pal_count) {
                xx_image_buf_u8(out, (uint8_t)(pal[i] >> 24));
                xx_image_buf_u8(out, (uint8_t)(pal[i] >> 16));
                xx_image_buf_u8(out, (uint8_t)(pal[i] >> 8));
            } else {
                xx_image_buf_fill(out, 0, 3);
            }
        }
    } else {
        uint8_t *plane_buf = (uint8_t *)calloc(bpl, 1);
        if (!plane_buf) return XX_IMAGE_ERR_MEMORY;

        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *src = f->rgba + (size_t)y * w * 4;
            /* Plane 0: Red */
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t a = src[x * 4 + 3];
                plane_buf[x] = (uint8_t)(src[x * 4] * a / 255);
            }
            pcx_encode_bytes(out, plane_buf, bpl);

            /* Plane 1: Green */
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t a = src[x * 4 + 3];
                plane_buf[x] = (uint8_t)(src[x * 4 + 1] * a / 255);
            }
            pcx_encode_bytes(out, plane_buf, bpl);

            /* Plane 2: Blue */
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t a = src[x * 4 + 3];
                plane_buf[x] = (uint8_t)(src[x * 4 + 2] * a / 255);
            }
            pcx_encode_bytes(out, plane_buf, bpl);
        }
        free(plane_buf);
    }

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_pcx = {
    "pcx",
    "ZSoft PCX",
    "pcx;pcc",
    "image/x-pcx",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_INFO,
    0,
    pcx_probe,
    pcx_load,
    pcx_save
};
