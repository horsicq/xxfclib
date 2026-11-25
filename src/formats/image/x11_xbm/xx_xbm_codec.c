/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * X11 Bitmap (XBM) codec.
 * Load: C source text parsing (#define name_width, _height, optional _x_hot,
 * _y_hot hotspots, static char/short bits[] array with hex constants),
 * LSB first bit ordering.
 * Save: X11 XBM C source text, luminance thresholding at 128.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int xbm_probe(const uint8_t *d, size_t n)
{
    size_t limit = n < 512 ? n : 512;
    if (n < 20) return 0;
    for (size_t i = 0; i + 7 <= limit; ++i) {
        if (memcmp(d + i, "#define", 7) == 0) {
            for (size_t j = i + 7; j + 5 <= limit; ++j) {
                if (memcmp(d + j, "width", 5) == 0 ||
                    memcmp(d + j, "height", 6) == 0 ||
                    memcmp(d + j, "bits", 4) == 0) {
                    return 75;
                }
            }
        }
    }
    return 0;
}

static bool xbm_skip_ws(const uint8_t *d, size_t n, size_t *p)
{
    while (*p < n) {
        uint8_t c = d[*p];
        if (c == '/' && *p + 1 < n && d[*p + 1] == '*') {
            /* C block comment */
            *p += 2;
            while (*p + 1 < n && !(d[*p] == '*' && d[*p + 1] == '/')) (*p)++;
            if (*p + 1 < n) *p += 2;
        } else if (c == '/' && *p + 1 < n && d[*p + 1] == '/') {
            /* C++ line comment */
            *p += 2;
            while (*p < n && d[*p] != '\n' && d[*p] != '\r') (*p)++;
        } else if (isspace(c)) {
            (*p)++;
        } else {
            return true;
        }
    }
    return false;
}

static xx_image_status xbm_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint32_t w = 0, h = 0;
    int32_t x_hot = -1, y_hot = -1;
    size_t p = 0;
    size_t bits_pos = 0;
    size_t stride;
    xx_image_frame *f;
    xx_image_status st;
    const char *g = "XBM";

    if (n < 20) return XX_IMAGE_ERR_FORMAT;

    /* Parse #define directives */
    while (xbm_skip_ws(d, n, &p)) {
        if (p + 7 <= n && memcmp(d + p, "#define", 7) == 0) {
            size_t key_start, key_len;
            char key[64];
            uint32_t val = 0;

            p += 7;
            if (!xbm_skip_ws(d, n, &p)) break;

            key_start = p;
            while (p < n && (isalnum(d[p]) || d[p] == '_')) p++;
            key_len = p - key_start;
            if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
            memcpy(key, d + key_start, key_len);
            key[key_len] = '\0';

            if (!xbm_skip_ws(d, n, &p)) break;

            while (p < n && isdigit(d[p])) {
                val = val * 10 + (uint32_t)(d[p] - '0');
                p++;
            }

            if (strstr(key, "_width") || strcmp(key, "width") == 0) {
                w = val;
            } else if (strstr(key, "_height") || strcmp(key, "height") == 0) {
                h = val;
            } else if (strstr(key, "_x_hot")) {
                x_hot = (int32_t)val;
            } else if (strstr(key, "_y_hot")) {
                y_hot = (int32_t)val;
            }
        } else {
            /* Check for bits[] array declaration */
            if (p + 5 <= n && memcmp(d + p, "bits[", 5) == 0) {
                while (p < n && d[p] != '{') p++;
                if (p < n && d[p] == '{') {
                    p++;
                    bits_pos = p;
                    break;
                }
            } else if (p + 6 <= n && memcmp(d + p, "_bits[", 6) == 0) {
                while (p < n && d[p] != '{') p++;
                if (p < n && d[p] == '{') {
                    p++;
                    bits_pos = p;
                    break;
                }
            } else {
                p++;
            }
        }
    }

    if (w == 0 || h == 0 || bits_pos == 0) return XX_IMAGE_ERR_FORMAT;
    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    stride = ((size_t)w + 7) / 8;
    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    /* Parse hex bytes from bits array */
    p = bits_pos;
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t *dst = f->rgba + (size_t)y * w * 4;
        for (size_t byte_idx = 0; byte_idx < stride; ++byte_idx) {
            uint32_t val = 0;
            if (!xbm_skip_ws(d, n, &p) || d[p] == '}') break;

            if (p + 2 <= n && d[p] == '0' && (d[p + 1] == 'x' || d[p + 1] == 'X')) {
                p += 2;
                while (p < n && isxdigit(d[p])) {
                    uint8_t c = d[p++];
                    val = (val << 4) | (uint32_t)(c >= '0' && c <= '9' ? c - '0' :
                                                 c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                                                 c - 'A' + 10);
                }
            } else if (isdigit(d[p])) {
                while (p < n && isdigit(d[p])) {
                    val = val * 10 + (uint32_t)(d[p++] - '0');
                }
            }

            /* Skip delimiter (comma or semicolon) */
            xbm_skip_ws(d, n, &p);
            if (p < n && (d[p] == ',' || d[p] == ';')) p++;

            /* Unpack bits LSB-first into destination row */
            for (uint32_t bit = 0; bit < 8; ++bit) {
                uint32_t x = (uint32_t)(byte_idx * 8 + bit);
                if (x < w) {
                    uint8_t bit_val = (uint8_t)((val >> bit) & 1);
                    /* In Pillow decode: bit 1 is white, bit 0 is black */
                    uint8_t c = bit_val ? 255 : 0;
                    dst[x * 4]     = c;
                    dst[x * 4 + 1] = c;
                    dst[x * 4 + 2] = c;
                    dst[x * 4 + 3] = 255;
                }
            }
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
        if (x_hot >= 0) xx_image_add_prop_int(img, g, "hotspot x", x_hot);
        if (y_hot >= 0) xx_image_add_prop_int(img, g, "hotspot y", y_hot);
    }

    return XX_IMAGE_OK;
}

static xx_image_status xbm_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    size_t stride;
    size_t total_bytes;
    size_t byte_count = 0;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || (uint64_t)w * h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;

    stride = ((size_t)w + 7) / 8;
    total_bytes = stride * h;

    xx_image_buf_printf(out, "#define image_width %u\n", w);
    xx_image_buf_printf(out, "#define image_height %u\n", h);
    xx_image_buf_append(out, "static char image_bits[] = {\n", 28);

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *src = f->rgba + (size_t)y * w * 4;
        for (size_t b = 0; b < stride; ++b) {
            uint8_t val = 0;
            for (uint32_t bit = 0; bit < 8; ++bit) {
                uint32_t x = (uint32_t)(b * 8 + bit);
                if (x < w) {
                    const uint8_t *px = src + (size_t)x * 4;
                    /* Luminance threshold at 128 */
                    uint32_t lum = ((uint32_t)px[0] * 77 + (uint32_t)px[1] * 150 + (uint32_t)px[2] * 29) >> 8;
                    if (lum >= 128) {
                        val |= (uint8_t)(1 << bit);
                    }
                }
            }

            xx_image_buf_printf(out, " 0x%02x%s", val, (byte_count + 1 < total_bytes) ? "," : "");
            byte_count++;
            if (byte_count % 12 == 0 || byte_count == total_bytes) {
                xx_image_buf_u8(out, '\n');
            }
        }
    }

    xx_image_buf_append(out, "};\n", 3);
    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_xbm = {
    "xbm",
    "X11 Bitmap",
    "xbm",
    "image/x-xbitmap",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_INFO,
    0,
    xbm_probe,
    xbm_load,
    xbm_save
};
