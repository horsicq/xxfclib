/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Radiance HDR / RGBE (.hdr, .pic) image format codec.
 * Load: Radiance RGBE headers (#?RADIANCE / #?RGBE), FORMAT=32-bit_rle_rgbe,
 * orientation "-Y h +X w" (others return XX_IMAGE_ERR_UNSUPPORTED), new-style RLE
 * and flat scanlines. Tone mapped to 8-bit RGBA via clamp(v^(1/2.2))*255.
 * Save: RGBE with new-style RLE scanlines from 8-bit (inverse tone map).
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hdr_probe(const uint8_t *d, size_t n)
{
    if (n >= 10 && memcmp(d, "#?RADIANCE", 10) == 0) return 95;
    if (n >= 6 && memcmp(d, "#?RGBE", 6) == 0) return 95;
    if (n >= 2 && d[0] == '#' && d[1] == '?') return 70;
    return 0;
}

static bool hdr_read_line(const uint8_t *d, size_t n, size_t *p, char *line, size_t max_len)
{
    size_t k = 0;
    if (*p >= n) return false;
    while (*p < n && d[*p] != '\n') {
        if (d[*p] != '\r' && k + 1 < max_len) {
            line[k++] = (char)d[*p];
        }
        (*p)++;
    }
    if (*p < n && d[*p] == '\n') (*p)++;
    line[k] = '\0';
    return true;
}

static inline uint8_t hdr_rgbe_to_u8(uint8_t c, uint8_t e)
{
    double v, mapped;
    if (e == 0 || c == 0) return 0;
    v = (double)c * ldexp(1.0, (int)e - 136);
    if (v <= 0.0) return 0;
    mapped = pow(v, 1.0 / 2.2);
    if (mapped > 1.0) mapped = 1.0;
    return (uint8_t)(mapped * 255.0 + 0.5);
}

static xx_image_status hdr_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    size_t p = 0;
    char line[256];
    bool format_ok = false;
    uint32_t w = 0, h = 0;
    uint8_t *scan_ch = NULL;
    xx_image_frame *f;
    xx_image_status st;
    const char *g = "HDR";

    if (hdr_probe(d, n) == 0) return XX_IMAGE_ERR_FORMAT;

    /* Parse header lines */
    while (hdr_read_line(d, n, &p, line, sizeof(line))) {
        if (line[0] == '\0') {
            /* Blank line marks end of header variables */
            break;
        }
        if (strncmp(line, "FORMAT=", 7) == 0) {
            if (strcmp(line + 7, "32-bit_rle_rgbe") == 0) {
                format_ok = true;
            } else {
                return XX_IMAGE_ERR_UNSUPPORTED;
            }
        } else if (line[0] != '#') {
            char *eq = strchr(line, '=');
            if (eq && !o->skip_metadata) {
                *eq = '\0';
                xx_image_add_prop_text(img, g, line, eq + 1);
            }
        }
    }

    if (!format_ok) {
        /* Default format if not explicitly given */
        format_ok = true;
    }

    /* Parse resolution line: "-Y h +X w" */
    if (!hdr_read_line(d, n, &p, line, sizeof(line))) return XX_IMAGE_ERR_CORRUPT;
    {
        int y_val, x_val;
        char y_dir[4], x_dir[4];
        if (sscanf(line, "%3s %d %3s %d", y_dir, &y_val, x_dir, &x_val) != 4) {
            return XX_IMAGE_ERR_CORRUPT;
        }
        if (strcmp(y_dir, "-Y") != 0 || strcmp(x_dir, "+X") != 0 || y_val <= 0 || x_val <= 0) {
            return XX_IMAGE_ERR_UNSUPPORTED;
        }
        h = (uint32_t)y_val;
        w = (uint32_t)x_val;
    }

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    scan_ch = (uint8_t *)malloc((size_t)w * 4);
    if (!scan_ch) return XX_IMAGE_ERR_MEMORY;

    for (uint32_t y = 0; y < h; ++y) {
        uint8_t *dst = f->rgba + (size_t)y * w * 4;

        /* Check for new-style RLE scanline: 0x02, 0x02, (w >> 8), (w & 0xFF) */
        if (w >= 8 && w <= 0x7FFF && p + 4 <= n &&
            d[p] == 2 && d[p + 1] == 2 &&
            d[p + 2] == (uint8_t)(w >> 8) && d[p + 3] == (uint8_t)(w & 0xFF)) {

            p += 4;
            for (int c = 0; c < 4; ++c) {
                size_t done = 0;
                while (done < w && p < n) {
                    uint8_t code = d[p++];
                    if (code > 128) {
                        uint8_t count = (uint8_t)(code - 128);
                        uint8_t val = (p < n) ? d[p++] : 0;
                        while (count-- > 0 && done < w) {
                            scan_ch[(size_t)c * w + done++] = val;
                        }
                    } else {
                        uint8_t count = code;
                        while (count-- > 0 && p < n && done < w) {
                            scan_ch[(size_t)c * w + done++] = d[p++];
                        }
                    }
                }
            }

            for (uint32_t x = 0; x < w; ++x) {
                uint8_t r = scan_ch[x];
                uint8_t gval = scan_ch[(size_t)w + x];
                uint8_t b = scan_ch[(size_t)2 * w + x];
                uint8_t e = scan_ch[(size_t)3 * w + x];
                dst[x * 4]     = hdr_rgbe_to_u8(r, e);
                dst[x * 4 + 1] = hdr_rgbe_to_u8(gval, e);
                dst[x * 4 + 2] = hdr_rgbe_to_u8(b, e);
                dst[x * 4 + 3] = 255;
            }
        } else {
            /* Flat / uncompressed scanline */
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t r, gval, b, e;
                if (p + 4 > n) break;
                r = d[p++];
                gval = d[p++];
                b = d[p++];
                e = d[p++];
                dst[x * 4]     = hdr_rgbe_to_u8(r, e);
                dst[x * 4 + 1] = hdr_rgbe_to_u8(gval, e);
                dst[x * 4 + 2] = hdr_rgbe_to_u8(b, e);
                dst[x * 4 + 3] = 255;
            }
        }
    }

    free(scan_ch);

    img->info.color_model = XX_IMAGE_COLOR_HDR;
    img->info.channels = 3;
    img->info.bits_per_pixel = 32;
    img->info.bits_per_sample = 8;
    img->info.has_alpha = false;
    img->info.frame_count = 1;
    img->info.is_compressed = true;
    XX_IMAGE_SET_STR(img->info.compression, "RGBE RLE");

    return XX_IMAGE_OK;
}

static void hdr_u8_to_rgbe(const uint8_t *px, uint8_t rgbe[4])
{
    double r = pow((double)px[0] / 255.0, 2.2);
    double g = pow((double)px[1] / 255.0, 2.2);
    double b = pow((double)px[2] / 255.0, 2.2);
    double max_v = r > g ? (r > b ? r : b) : (g > b ? g : b);

    if (max_v < 1e-32) {
        rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
    } else {
        int e;
        double mant = frexp(max_v, &e);
        double s = mant * 256.0 / max_v;
        double cr = r * s;
        double cg = g * s;
        double cb = b * s;
        rgbe[0] = (uint8_t)(cr > 255.0 ? 255 : cr);
        rgbe[1] = (uint8_t)(cg > 255.0 ? 255 : cg);
        rgbe[2] = (uint8_t)(cb > 255.0 ? 255 : cb);
        rgbe[3] = (uint8_t)(e + 128);
    }
}

static void hdr_rle_encode_channel(xx_image_buffer *out, const uint8_t *data, uint32_t w)
{
    uint32_t i = 0;
    while (i < w) {
        /* Check for run of identical values */
        uint32_t run = 1;
        while (i + run < w && run < 127 && data[i + run] == data[i]) {
            ++run;
        }
        if (run >= 4 || w - i < 4) {
            xx_image_buf_u8(out, (uint8_t)(128 + run));
            xx_image_buf_u8(out, data[i]);
            i += run;
        } else {
            /* Literal run */
            uint32_t lit = 0;
            while (i + lit < w && lit < 128) {
                if (i + lit + 3 < w &&
                    data[i + lit] == data[i + lit + 1] &&
                    data[i + lit] == data[i + lit + 2] &&
                    data[i + lit] == data[i + lit + 3]) {
                    break;
                }
                ++lit;
            }
            if (lit == 0) lit = 1;
            xx_image_buf_u8(out, (uint8_t)lit);
            xx_image_buf_append(out, data + i, lit);
            i += lit;
        }
    }
}

static xx_image_status hdr_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    uint8_t *ch_buf;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || (uint64_t)w * h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;

    ch_buf = (uint8_t *)malloc((size_t)w * 4);
    if (!ch_buf) return XX_IMAGE_ERR_MEMORY;

    /* Write Radiance header */
    xx_image_buf_append(out, "#?RADIANCE\n", 11);
    xx_image_buf_append(out, "FORMAT=32-bit_rle_rgbe\n\n", 24);
    xx_image_buf_printf(out, "-Y %u +X %u\n", h, w);

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *src = f->rgba + (size_t)y * w * 4;

        for (uint32_t x = 0; x < w; ++x) {
            uint8_t rgbe[4], px[4];
            const uint8_t *s = src + (size_t)x * 4;
            /* no alpha in RGBE: composite onto black like every opaque codec */
            px[0] = (uint8_t)(s[0] * s[3] / 255);
            px[1] = (uint8_t)(s[1] * s[3] / 255);
            px[2] = (uint8_t)(s[2] * s[3] / 255);
            px[3] = 255;
            hdr_u8_to_rgbe(px, rgbe);
            ch_buf[x]                 = rgbe[0];
            ch_buf[(size_t)w + x]     = rgbe[1];
            ch_buf[(size_t)2 * w + x] = rgbe[2];
            ch_buf[(size_t)3 * w + x] = rgbe[3];
        }

        if (w >= 8 && w <= 0x7FFF) {
            /* New-style RLE scanline header */
            xx_image_buf_u8(out, 2);
            xx_image_buf_u8(out, 2);
            xx_image_buf_u8(out, (uint8_t)(w >> 8));
            xx_image_buf_u8(out, (uint8_t)(w & 0xFF));

            for (int c = 0; c < 4; ++c) {
                hdr_rle_encode_channel(out, ch_buf + (size_t)c * w, w);
            }
        } else {
            /* Flat scanline */
            for (uint32_t x = 0; x < w; ++x) {
                xx_image_buf_u8(out, ch_buf[x]);
                xx_image_buf_u8(out, ch_buf[(size_t)w + x]);
                xx_image_buf_u8(out, ch_buf[(size_t)2 * w + x]);
                xx_image_buf_u8(out, ch_buf[(size_t)3 * w + x]);
            }
        }
    }

    free(ch_buf);
    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_hdr = {
    "hdr",
    "Radiance HDR",
    "hdr;pic",
    "image/vnd.radiance",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_LOSSY | XX_IMAGE_CODEC_INFO,
    0,
    hdr_probe,
    hdr_load,
    hdr_save
};
