/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Portable Network Graphics (PNG) & Animated Portable Network Graphics (APNG).
 * Load: colour types 0, 2, 3, 4, 6; bit depths 1, 2, 4, 8, 16; Adam7 interlace;
 * PLTE, tRNS (gray, RGB, palette alpha); 5 filters; IDAT concatenate & inflate;
 * IHDR CRC validation; 16-bit to 8-bit reduction; APNG acTL/fcTL/fdAT multi-frame;
 * chunk metadata properties.
 * Save: smallest exact representation (palette, gray, gray+alpha, RGB, RGBA),
 * per-row adaptive filtering (min SAD), zlib deflate, pHYs dpi, APNG multi-frame.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNG_MAGIC_LEN 8
static const uint8_t g_png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

static const int g_adam7_start_x[7] = {0, 4, 0, 2, 0, 1, 0};
static const int g_adam7_start_y[7] = {0, 0, 4, 0, 2, 0, 1};
static const int g_adam7_step_x[7]  = {8, 8, 4, 4, 2, 2, 1};
static const int g_adam7_step_y[7]  = {8, 8, 8, 4, 4, 2, 2};

static int png_probe(const uint8_t *d, size_t n)
{
    if (n >= PNG_MAGIC_LEN && memcmp(d, g_png_signature, PNG_MAGIC_LEN) == 0) {
        return 100;
    }
    return 0;
}

static inline uint8_t png_paeth_predict(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static void png_write_chunk(xx_image_buffer *out, const char *type, const void *data, size_t len)
{
    uint32_t crc;
    xx_image_buf_u32be(out, (uint32_t)len);
    xx_image_buf_append(out, type, 4);
    if (len > 0 && data) {
        xx_image_buf_append(out, data, len);
    }
    crc = xx_image_crc32(0, type, 4);
    if (len > 0 && data) {
        crc = xx_image_crc32(crc, data, len);
    }
    xx_image_buf_u32be(out, crc);
}

typedef struct png_frame_info {
    uint32_t width;
    uint32_t height;
    int32_t x_offset;
    int32_t y_offset;
    uint32_t delay_ms;
    uint32_t dispose;
    xx_image_buffer data;
} png_frame_info;

static void png_unpack_scanline(const uint8_t *recon, uint32_t width,
                                uint8_t color_type, uint8_t bit_depth,
                                const uint8_t (*pal)[3], uint32_t pal_count,
                                bool has_trns, uint16_t trns_gray,
                                uint16_t trns_r, uint16_t trns_g, uint16_t trns_b,
                                const uint8_t *trns_pal, uint32_t trns_pal_count,
                                uint8_t *dst_rgba)
{
    for (uint32_t x = 0; x < width; ++x) {
        uint8_t *px = dst_rgba + (size_t)x * 4U;
        uint8_t r = 0, g = 0, b = 0, a = 255;

        switch (color_type) {
        case 0: /* Grayscale */
            if (bit_depth == 1) {
                uint8_t raw = (recon[x / 8U] >> (7U - (x % 8U))) & 1U;
                uint8_t v = raw ? 255 : 0;
                r = g = b = v;
                if (has_trns && raw == (uint8_t)trns_gray) a = 0;
            } else if (bit_depth == 2) {
                uint8_t raw = (recon[x / 4U] >> (6U - (x % 4U) * 2U)) & 3U;
                uint8_t v = (uint8_t)((raw * 255U + 1U) / 3U);
                r = g = b = v;
                if (has_trns && raw == (uint8_t)trns_gray) a = 0;
            } else if (bit_depth == 4) {
                uint8_t raw = (recon[x / 2U] >> (4U - (x % 2U) * 4U)) & 15U;
                uint8_t v = (uint8_t)((raw * 255U + 7U) / 15U);
                r = g = b = v;
                if (has_trns && raw == (uint8_t)trns_gray) a = 0;
            } else if (bit_depth == 8) {
                uint8_t raw = recon[x];
                r = g = b = raw;
                if (has_trns && raw == (uint8_t)trns_gray) a = 0;
            } else if (bit_depth == 16) {
                uint16_t raw = ((uint16_t)recon[x * 2U] << 8) | recon[x * 2U + 1U];
                uint8_t v = recon[x * 2U]; /* high byte */
                r = g = b = v;
                if (has_trns && raw == trns_gray) a = 0;
            }
            break;

        case 2: /* RGB */
            if (bit_depth == 8) {
                r = recon[x * 3U];
                g = recon[x * 3U + 1U];
                b = recon[x * 3U + 2U];
                if (has_trns && r == (uint8_t)trns_r && g == (uint8_t)trns_g && b == (uint8_t)trns_b) a = 0;
            } else if (bit_depth == 16) {
                uint16_t raw_r = ((uint16_t)recon[x * 6U] << 8) | recon[x * 6U + 1U];
                uint16_t raw_g = ((uint16_t)recon[x * 6U + 2U] << 8) | recon[x * 6U + 3U];
                uint16_t raw_b = ((uint16_t)recon[x * 6U + 4U] << 8) | recon[x * 6U + 5U];
                r = recon[x * 6U];
                g = recon[x * 6U + 2U];
                b = recon[x * 6U + 4U];
                if (has_trns && raw_r == trns_r && raw_g == trns_g && raw_b == trns_b) a = 0;
            }
            break;

        case 3: /* Indexed */
            {
                uint8_t idx = 0;
                if (bit_depth == 1) {
                    idx = (recon[x / 8U] >> (7U - (x % 8U))) & 1U;
                } else if (bit_depth == 2) {
                    idx = (recon[x / 4U] >> (6U - (x % 4U) * 2U)) & 3U;
                } else if (bit_depth == 4) {
                    idx = (recon[x / 2U] >> (4U - (x % 2U) * 4U)) & 15U;
                } else if (bit_depth == 8) {
                    idx = recon[x];
                }
                if (idx < pal_count) {
                    r = pal[idx][0];
                    g = pal[idx][1];
                    b = pal[idx][2];
                }
                if (has_trns && idx < trns_pal_count) {
                    a = trns_pal[idx];
                }
            }
            break;

        case 4: /* Gray + Alpha */
            if (bit_depth == 8) {
                r = g = b = recon[x * 2U];
                a = recon[x * 2U + 1U];
            } else if (bit_depth == 16) {
                r = g = b = recon[x * 4U];
                a = recon[x * 4U + 2U];
            }
            break;

        case 6: /* RGBA */
            if (bit_depth == 8) {
                r = recon[x * 4U];
                g = recon[x * 4U + 1U];
                b = recon[x * 4U + 2U];
                a = recon[x * 4U + 3U];
            } else if (bit_depth == 16) {
                r = recon[x * 8U];
                g = recon[x * 8U + 2U];
                b = recon[x * 8U + 4U];
                a = recon[x * 8U + 6U];
            }
            break;

        default:
            break;
        }

        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = a;
    }
}

static xx_image_status png_decode_raster(const uint8_t *deflated, size_t deflated_size,
                                      uint32_t fw, uint32_t fh,
                                      uint8_t color_type, uint8_t bit_depth, bool interlaced,
                                      const uint8_t (*pal)[3], uint32_t pal_count,
                                      bool has_trns, uint16_t trns_gray,
                                      uint16_t trns_r, uint16_t trns_g, uint16_t trns_b,
                                      const uint8_t *trns_pal, uint32_t trns_pal_count,
                                      xx_image_frame *frame)
{
    uint32_t channels = 1;
    if (color_type == 2) channels = 3;
    else if (color_type == 4) channels = 2;
    else if (color_type == 6) channels = 4;

    uint32_t bpp = channels * bit_depth;
    size_t bpp_bytes = (bpp + 7U) / 8U;
    if (bpp_bytes < 1) bpp_bytes = 1;

    if (!interlaced) {
        size_t row_bytes = ((size_t)fw * bpp + 7U) / 8U;
        size_t expected = (size_t)fh * (1U + row_bytes);
        uint8_t *uncomp = NULL;
        size_t uncomp_len = 0;
        xx_image_status st = xx_image_zlib_inflate(deflated, deflated_size, expected, 0, &uncomp, &uncomp_len);
        if (st != XX_IMAGE_OK) return st;
        if (uncomp_len < expected) {
            free(uncomp);
            return XX_IMAGE_ERR_CORRUPT;
        }

        uint8_t *prev = (uint8_t *)calloc(1, row_bytes);
        uint8_t *recon = (uint8_t *)malloc(row_bytes);
        if (!prev || !recon) {
            free(prev);
            free(recon);
            free(uncomp);
            return XX_IMAGE_ERR_MEMORY;
        }

        for (uint32_t y = 0; y < fh; ++y) {
            const uint8_t *filt_row = uncomp + (size_t)y * (1U + row_bytes);
            uint8_t ftype = filt_row[0];
            const uint8_t *src_row = filt_row + 1U;

            for (size_t x = 0; x < row_bytes; ++x) {
                uint8_t raw = src_row[x];
                uint8_t a = (x >= bpp_bytes) ? recon[x - bpp_bytes] : 0;
                uint8_t b = prev[x];
                uint8_t c = (x >= bpp_bytes) ? prev[x - bpp_bytes] : 0;

                switch (ftype) {
                case 0: recon[x] = raw; break;
                case 1: recon[x] = (uint8_t)(raw + a); break;
                case 2: recon[x] = (uint8_t)(raw + b); break;
                case 3: recon[x] = (uint8_t)(raw + ((a + b) >> 1)); break;
                case 4: recon[x] = (uint8_t)(raw + png_paeth_predict(a, b, c)); break;
                default:
                    free(prev);
                    free(recon);
                    free(uncomp);
                    return XX_IMAGE_ERR_CORRUPT;
                }
            }

            uint8_t *dst = frame->rgba + (size_t)y * fw * 4U;
            png_unpack_scanline(recon, fw, color_type, bit_depth,
                                pal, pal_count,
                                has_trns, trns_gray, trns_r, trns_g, trns_b,
                                trns_pal, trns_pal_count, dst);

            memcpy(prev, recon, row_bytes);
        }

        free(prev);
        free(recon);
        free(uncomp);
    } else {
        /* Adam7 Interlace */
        size_t expected = 0;
        for (int p = 0; p < 7; ++p) {
            uint32_t pw = (fw > (uint32_t)g_adam7_start_x[p]) ?
                          (fw - (uint32_t)g_adam7_start_x[p] + (uint32_t)g_adam7_step_x[p] - 1U) / (uint32_t)g_adam7_step_x[p] : 0;
            uint32_t ph = (fh > (uint32_t)g_adam7_start_y[p]) ?
                          (fh - (uint32_t)g_adam7_start_y[p] + (uint32_t)g_adam7_step_y[p] - 1U) / (uint32_t)g_adam7_step_y[p] : 0;
            if (pw > 0 && ph > 0) {
                size_t p_row_bytes = ((size_t)pw * bpp + 7U) / 8U;
                expected += (size_t)ph * (1U + p_row_bytes);
            }
        }

        uint8_t *uncomp = NULL;
        size_t uncomp_len = 0;
        xx_image_status st = xx_image_zlib_inflate(deflated, deflated_size, expected, 0, &uncomp, &uncomp_len);
        if (st != XX_IMAGE_OK) return st;
        if (uncomp_len < expected) {
            free(uncomp);
            return XX_IMAGE_ERR_CORRUPT;
        }

        size_t uncomp_off = 0;
        for (int p = 0; p < 7; ++p) {
            uint32_t pw = (fw > (uint32_t)g_adam7_start_x[p]) ?
                          (fw - (uint32_t)g_adam7_start_x[p] + (uint32_t)g_adam7_step_x[p] - 1U) / (uint32_t)g_adam7_step_x[p] : 0;
            uint32_t ph = (fh > (uint32_t)g_adam7_start_y[p]) ?
                          (fh - (uint32_t)g_adam7_start_y[p] + (uint32_t)g_adam7_step_y[p] - 1U) / (uint32_t)g_adam7_step_y[p] : 0;
            if (pw == 0 || ph == 0) continue;

            size_t p_row_bytes = ((size_t)pw * bpp + 7U) / 8U;
            uint8_t *prev = (uint8_t *)calloc(1, p_row_bytes);
            uint8_t *recon = (uint8_t *)malloc(p_row_bytes);
            uint8_t *scanline_rgba = (uint8_t *)malloc((size_t)pw * 4U);
            if (!prev || !recon || !scanline_rgba) {
                free(prev);
                free(recon);
                free(scanline_rgba);
                free(uncomp);
                return XX_IMAGE_ERR_MEMORY;
            }

            for (uint32_t py = 0; py < ph; ++py) {
                uint8_t ftype = uncomp[uncomp_off++];
                const uint8_t *src_row = uncomp + uncomp_off;
                uncomp_off += p_row_bytes;

                for (size_t x = 0; x < p_row_bytes; ++x) {
                    uint8_t raw = src_row[x];
                    uint8_t a = (x >= bpp_bytes) ? recon[x - bpp_bytes] : 0;
                    uint8_t b = prev[x];
                    uint8_t c = (x >= bpp_bytes) ? prev[x - bpp_bytes] : 0;

                    switch (ftype) {
                    case 0: recon[x] = raw; break;
                    case 1: recon[x] = (uint8_t)(raw + a); break;
                    case 2: recon[x] = (uint8_t)(raw + b); break;
                    case 3: recon[x] = (uint8_t)(raw + ((a + b) >> 1)); break;
                    case 4: recon[x] = (uint8_t)(raw + png_paeth_predict(a, b, c)); break;
                    default:
                        free(prev);
                        free(recon);
                        free(scanline_rgba);
                        free(uncomp);
                        return XX_IMAGE_ERR_CORRUPT;
                    }
                }

                png_unpack_scanline(recon, pw, color_type, bit_depth,
                                    pal, pal_count,
                                    has_trns, trns_gray, trns_r, trns_g, trns_b,
                                    trns_pal, trns_pal_count, scanline_rgba);

                uint32_t iy = (uint32_t)g_adam7_start_y[p] + py * (uint32_t)g_adam7_step_y[p];
                for (uint32_t px = 0; px < pw; ++px) {
                    uint32_t ix = (uint32_t)g_adam7_start_x[p] + px * (uint32_t)g_adam7_step_x[p];
                    uint8_t *dst = frame->rgba + ((size_t)iy * fw + ix) * 4U;
                    memcpy(dst, scanline_rgba + (size_t)px * 4U, 4);
                }

                memcpy(prev, recon, p_row_bytes);
            }

            free(prev);
            free(recon);
            free(scanline_rgba);
        }

        free(uncomp);
    }

    return XX_IMAGE_OK;
}

static xx_image_status png_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint32_t w = 0, h = 0;
    uint8_t bit_depth = 0, color_type = 0, compression = 0, filter = 0, interlace = 0;
    size_t p = 8;
    xx_image_status st;

    uint8_t pal[256][3];
    uint32_t pal_count = 0;
    bool has_trns = false;
    uint16_t trns_gray = 0, trns_r = 0, trns_g = 0, trns_b = 0;
    uint8_t trns_pal[256];
    uint32_t trns_pal_count = 0;

    bool has_actl = false;
    uint32_t actl_num_frames = 0;
    uint32_t actl_num_plays = 0;
    bool fctl_seen_before_idat = false;
    bool in_first_idat = false;

    png_frame_info fctl0;
    memset(&fctl0, 0, sizeof(fctl0));

    png_frame_info *anim_frames = NULL;
    size_t anim_frame_count = 0;
    size_t anim_frame_cap = 0;

    xx_image_buffer idat_buf;
    memset(&idat_buf, 0, sizeof(idat_buf));

    if (n < 33 || png_probe(d, n) == 0) return XX_IMAGE_ERR_FORMAT;

    /* IHDR must be first */
    {
        uint32_t clen = xx_image_rd_u32be(d + p);
        uint32_t ctype = xx_image_rd_u32be(d + p + 4);
        if (ctype != 0x49484452U /* "IHDR" */ || clen != 13) return XX_IMAGE_ERR_FORMAT;
        if (!xx_image_in_range(n, p + 8, 17)) return XX_IMAGE_ERR_CORRUPT;

        uint32_t ihdr_crc = xx_image_crc32(0, d + p + 4, 17);
        if (ihdr_crc != xx_image_rd_u32be(d + p + 21)) return XX_IMAGE_ERR_CORRUPT;

        w = xx_image_rd_u32be(d + p + 8);
        h = xx_image_rd_u32be(d + p + 12);
        bit_depth = d[p + 16];
        color_type = d[p + 17];
        compression = d[p + 18];
        filter = d[p + 19];
        interlace = d[p + 20];
        p += 25;
    }

    if (compression != 0 || filter != 0 || interlace > 1) return XX_IMAGE_ERR_CORRUPT;
    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    img->width = w;
    img->height = h;

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, "IHDR", "width", w);
        xx_image_add_prop_uint(img, "IHDR", "height", h);
        xx_image_add_prop_uint(img, "IHDR", "bit depth", bit_depth);
        xx_image_add_prop_uint(img, "IHDR", "color type", color_type);
        xx_image_add_prop_uint(img, "IHDR", "compression method", compression);
        xx_image_add_prop_uint(img, "IHDR", "filter method", filter);
        xx_image_add_prop_uint(img, "IHDR", "interlace method", interlace);
    }

    /* Iterate chunks */
    while (xx_image_in_range(n, p, 8)) {
        uint32_t clen = xx_image_rd_u32be(d + p);
        uint32_t ctype = xx_image_rd_u32be(d + p + 4);
        const uint8_t *cdata = d + p + 8;
        if (!xx_image_in_range(n, p + 8, (size_t)clen + 4U)) break;

        switch (ctype) {
        case 0x504C5445U: /* PLTE */
            if (clen % 3U != 0 || clen > 768U) break;
            pal_count = clen / 3U;
            for (uint32_t i = 0; i < pal_count; ++i) {
                pal[i][0] = cdata[i * 3U];
                pal[i][1] = cdata[i * 3U + 1U];
                pal[i][2] = cdata[i * 3U + 2U];
                img->palette[i] = ((uint32_t)pal[i][0] << 24) |
                                  ((uint32_t)pal[i][1] << 16) |
                                  ((uint32_t)pal[i][2] << 8) | 0xFFU;
            }
            img->palette_count = pal_count;
            if (!o->skip_metadata) {
                xx_image_add_prop_uint(img, "PLTE", "size", pal_count);
            }
            break;

        case 0x74524E53U: /* tRNS */
            has_trns = true;
            if (color_type == 0 && clen >= 2) {
                trns_gray = xx_image_rd_u16be(cdata);
            } else if (color_type == 2 && clen >= 6) {
                trns_r = xx_image_rd_u16be(cdata);
                trns_g = xx_image_rd_u16be(cdata + 2);
                trns_b = xx_image_rd_u16be(cdata + 4);
            } else if (color_type == 3) {
                trns_pal_count = clen > 256 ? 256 : clen;
                memcpy(trns_pal, cdata, trns_pal_count);
                for (uint32_t i = 0; i < trns_pal_count && i < pal_count; ++i) {
                    img->palette[i] = (img->palette[i] & 0xFFFFFF00U) | trns_pal[i];
                }
            }
            if (!o->skip_metadata) {
                xx_image_add_prop_bytes(img, "tRNS", "data", cdata, clen);
            }
            break;

        case 0x67414D41U: /* gAMA */
            if (clen >= 4 && !o->skip_metadata) {
                uint32_t gval = xx_image_rd_u32be(cdata);
                xx_image_add_prop_real(img, "gAMA", "gamma", (double)gval / 100000.0);
            }
            break;

        case 0x6348524DU: /* cHRM */
            if (clen >= 32 && !o->skip_metadata) {
                xx_image_add_prop_real(img, "cHRM", "white x", (double)xx_image_rd_u32be(cdata) / 100000.0);
                xx_image_add_prop_real(img, "cHRM", "white y", (double)xx_image_rd_u32be(cdata + 4) / 100000.0);
                xx_image_add_prop_real(img, "cHRM", "red x", (double)xx_image_rd_u32be(cdata + 8) / 100000.0);
                xx_image_add_prop_real(img, "cHRM", "red y", (double)xx_image_rd_u32be(cdata + 12) / 100000.0);
                xx_image_add_prop_real(img, "cHRM", "green x", (double)xx_image_rd_u32be(cdata + 16) / 100000.0);
                xx_image_add_prop_real(img, "cHRM", "green y", (double)xx_image_rd_u32be(cdata + 20) / 100000.0);
                xx_image_add_prop_real(img, "cHRM", "blue x", (double)xx_image_rd_u32be(cdata + 24) / 100000.0);
                xx_image_add_prop_real(img, "cHRM", "blue y", (double)xx_image_rd_u32be(cdata + 28) / 100000.0);
            }
            break;

        case 0x73524742U: /* sRGB */
            if (clen >= 1 && !o->skip_metadata) {
                xx_image_add_prop_uint(img, "sRGB", "intent", cdata[0]);
            }
            break;

        case 0x70485973U: /* pHYs */
            if (clen >= 9) {
                uint32_t px = xx_image_rd_u32be(cdata);
                uint32_t py = xx_image_rd_u32be(cdata + 4);
                uint8_t unit = cdata[8];
                if (unit == 1) {
                    img->info.dpi_x = (double)px * 0.0254;
                    img->info.dpi_y = (double)py * 0.0254;
                }
                if (!o->skip_metadata) {
                    xx_image_add_prop_uint(img, "pHYs", "pixels per unit x", px);
                    xx_image_add_prop_uint(img, "pHYs", "pixels per unit y", py);
                    xx_image_add_prop_uint(img, "pHYs", "unit", unit);
                }
            }
            break;

        case 0x74494D45U: /* tIME */
            if (clen >= 7 && !o->skip_metadata) {
                uint16_t yr = xx_image_rd_u16be(cdata);
                char dt[64];
                snprintf(dt, sizeof(dt), "%04u-%02u-%02u %02u:%02u:%02u",
                         yr, cdata[2], cdata[3], cdata[4], cdata[5], cdata[6]);
                xx_image_add_prop_text(img, "tIME", "time", dt);
            }
            break;

        case 0x74455874U: /* tEXt */
            if (!o->skip_metadata) {
                size_t klen = 0;
                while (klen < clen && cdata[klen] != 0) ++klen;
                if (klen < clen) {
                    char kw[80];
                    size_t cp = klen < 79 ? klen : 79;
                    memcpy(kw, cdata, cp);
                    kw[cp] = 0;
                    xx_image_add_prop_textn(img, "text", kw, (const char *)(cdata + klen + 1), clen - klen - 1);
                }
            }
            break;

        case 0x7A545874U: /* zTXt */
            if (!o->skip_metadata) {
                size_t klen = 0;
                while (klen < clen && cdata[klen] != 0) ++klen;
                if (klen + 2 < clen && cdata[klen + 1] == 0) {
                    char kw[80];
                    size_t cp = klen < 79 ? klen : 79;
                    memcpy(kw, cdata, cp);
                    kw[cp] = 0;
                    uint8_t *infl = NULL;
                    size_t infl_len = 0;
                    if (xx_image_zlib_inflate(cdata + klen + 2, clen - klen - 2, 0, 1024 * 1024, &infl, &infl_len) == XX_IMAGE_OK) {
                        xx_image_add_prop_textn(img, "text", kw, (const char *)infl, infl_len);
                        free(infl);
                    }
                }
            }
            break;

        case 0x69545874U: /* iTXt */
            if (!o->skip_metadata) {
                size_t klen = 0;
                while (klen < clen && cdata[klen] != 0) ++klen;
                if (klen + 3 < clen) {
                    char kw[80];
                    size_t cp = klen < 79 ? klen : 79;
                    memcpy(kw, cdata, cp);
                    kw[cp] = 0;
                    uint8_t cflag = cdata[klen + 1];
                    uint8_t cmeth = cdata[klen + 2];
                    size_t pos = klen + 3;
                    while (pos < clen && cdata[pos] != 0) ++pos;
                    if (pos < clen) ++pos; /* skip null */
                    while (pos < clen && cdata[pos] != 0) ++pos;
                    if (pos < clen) ++pos; /* skip null */
                    if (pos <= clen) {
                        if (cflag == 1 && cmeth == 0) {
                            uint8_t *infl = NULL;
                            size_t infl_len = 0;
                            if (xx_image_zlib_inflate(cdata + pos, clen - pos, 0, 1024 * 1024, &infl, &infl_len) == XX_IMAGE_OK) {
                                xx_image_add_prop_textn(img, "text", kw, (const char *)infl, infl_len);
                                free(infl);
                            }
                        } else {
                            xx_image_add_prop_textn(img, "text", kw, (const char *)(cdata + pos), clen - pos);
                        }
                    }
                }
            }
            break;

        case 0x69434350U: /* iCCP */
            {
                size_t nlen = 0;
                while (nlen < clen && cdata[nlen] != 0) ++nlen;
                if (nlen + 2 < clen && cdata[nlen + 1] == 0) {
                    char pname[80];
                    size_t cp = nlen < 79 ? nlen : 79;
                    memcpy(pname, cdata, cp);
                    pname[cp] = 0;
                    uint8_t *infl = NULL;
                    size_t infl_len = 0;
                    if (xx_image_zlib_inflate(cdata + nlen + 2, clen - nlen - 2, 0, 16 * 1024 * 1024, &infl, &infl_len) == XX_IMAGE_OK) {
                        if (!o->skip_metadata) {
                            xx_image_add_prop_text(img, "ICC", "name", pname);
                            xx_image_add_prop_bytes(img, "ICC", "profile", infl, infl_len);
                        }
                        free(infl);
                    }
                }
            }
            break;

        case 0x624B4744U: /* bKGD */
            if (color_type == 0 || color_type == 4) {
                if (clen >= 2) {
                    uint16_t gval = xx_image_rd_u16be(cdata);
                    uint8_t g8 = (bit_depth == 16) ? (uint8_t)(gval >> 8) : (uint8_t)gval;
                    img->background = ((uint32_t)g8 << 24) | ((uint32_t)g8 << 16) | ((uint32_t)g8 << 8) | 0xFFU;
                    if (!o->skip_metadata) xx_image_add_prop_uint(img, "bKGD", "gray", gval);
                }
            } else if (color_type == 2 || color_type == 6) {
                if (clen >= 6) {
                    uint16_t rv = xx_image_rd_u16be(cdata);
                    uint16_t gv = xx_image_rd_u16be(cdata + 2);
                    uint16_t bv = xx_image_rd_u16be(cdata + 4);
                    uint8_t r8 = (bit_depth == 16) ? (uint8_t)(rv >> 8) : (uint8_t)rv;
                    uint8_t g8 = (bit_depth == 16) ? (uint8_t)(gv >> 8) : (uint8_t)gv;
                    uint8_t b8 = (bit_depth == 16) ? (uint8_t)(bv >> 8) : (uint8_t)bv;
                    img->background = ((uint32_t)r8 << 24) | ((uint32_t)g8 << 16) | ((uint32_t)b8 << 8) | 0xFFU;
                    if (!o->skip_metadata) {
                        xx_image_add_prop_uint(img, "bKGD", "red", rv);
                        xx_image_add_prop_uint(img, "bKGD", "green", gv);
                        xx_image_add_prop_uint(img, "bKGD", "blue", bv);
                    }
                }
            } else if (color_type == 3) {
                if (clen >= 1) {
                    uint8_t idx = cdata[0];
                    if (idx < pal_count) {
                        img->background = ((uint32_t)pal[idx][0] << 24) |
                                          ((uint32_t)pal[idx][1] << 16) |
                                          ((uint32_t)pal[idx][2] << 8) | 0xFFU;
                    }
                    if (!o->skip_metadata) xx_image_add_prop_uint(img, "bKGD", "index", idx);
                }
            }
            break;

        case 0x65584966U: /* eXIf */
            if (!o->skip_metadata) {
                xx_image_add_prop_bytes(img, "eXIf", "data", cdata, clen);
            }
            break;

        case 0x6163544CU: /* acTL */
            if (clen >= 8) {
                has_actl = true;
                actl_num_frames = xx_image_rd_u32be(cdata);
                actl_num_plays = xx_image_rd_u32be(cdata + 4);
                img->loop_count = actl_num_plays;
                img->info.is_animated = true;
                img->info.frame_count = actl_num_frames;
                if (!o->skip_metadata) {
                    xx_image_add_prop_uint(img, "acTL", "num_frames", actl_num_frames);
                    xx_image_add_prop_uint(img, "acTL", "num_plays", actl_num_plays);
                }
            }
            break;

        case 0x6663544CU: /* fcTL */
            if (clen >= 26) {
                uint32_t fw = xx_image_rd_u32be(cdata + 4);
                uint32_t fh = xx_image_rd_u32be(cdata + 8);
                uint32_t fx = xx_image_rd_u32be(cdata + 12);
                uint32_t fy = xx_image_rd_u32be(cdata + 16);
                uint16_t dnum = xx_image_rd_u16be(cdata + 20);
                uint16_t dden = xx_image_rd_u16be(cdata + 22);
                uint8_t disp = cdata[24];
                uint32_t den = dden ? dden : 100U;
                uint32_t dms = (uint32_t)(((uint64_t)dnum * 1000U + den / 2U) / den);

                if (!in_first_idat && idat_buf.size == 0) {
                    fctl_seen_before_idat = true;
                    fctl0.width = fw;
                    fctl0.height = fh;
                    fctl0.x_offset = (int32_t)fx;
                    fctl0.y_offset = (int32_t)fy;
                    fctl0.delay_ms = dms;
                    fctl0.dispose = (disp == 1 ? 2 : disp == 2 ? 3 : 0);
                } else {
                    if (anim_frame_count == anim_frame_cap) {
                        size_t ncap = anim_frame_cap ? anim_frame_cap * 2 : 4;
                        png_frame_info *narr = (png_frame_info *)realloc(anim_frames, ncap * sizeof(*narr));
                        if (!narr) {
                            xx_image_buf_free(&idat_buf);
                            free(anim_frames);
                            return XX_IMAGE_ERR_MEMORY;
                        }
                        anim_frames = narr;
                        anim_frame_cap = ncap;
                    }
                    png_frame_info *cur = &anim_frames[anim_frame_count++];
                    memset(cur, 0, sizeof(*cur));
                    cur->width = fw;
                    cur->height = fh;
                    cur->x_offset = (int32_t)fx;
                    cur->y_offset = (int32_t)fy;
                    cur->delay_ms = dms;
                    cur->dispose = (disp == 1 ? 2 : disp == 2 ? 3 : 0);
                }
            }
            break;

        case 0x49444154U: /* IDAT */
            in_first_idat = true;
            xx_image_buf_append(&idat_buf, cdata, clen);
            break;

        case 0x66644154U: /* fdAT */
            if (clen > 4 && anim_frame_count > 0) {
                /* Skip 4-byte sequence number */
                xx_image_buf_append(&anim_frames[anim_frame_count - 1].data, cdata + 4, clen - 4);
            }
            break;

        case 0x49454E44U: /* IEND */
            break;

        default:
            break;
        }

        p += 12U + (size_t)clen;
    }

    if (has_actl && !o->first_frame_only && (anim_frame_count > 0 || fctl_seen_before_idat)) {
        if (fctl_seen_before_idat) {
            xx_image_frame *f = xx_image_add_frame(img, fctl0.width, fctl0.height);
            if (!f) {
                xx_image_buf_free(&idat_buf);
                for (size_t i = 0; i < anim_frame_count; ++i) xx_image_buf_free(&anim_frames[i].data);
                free(anim_frames);
                return XX_IMAGE_ERR_MEMORY;
            }
            f->x = fctl0.x_offset;
            f->y = fctl0.y_offset;
            f->delay_ms = fctl0.delay_ms;
            f->dispose = fctl0.dispose;

            st = png_decode_raster(idat_buf.data, idat_buf.size, fctl0.width, fctl0.height,
                                   color_type, bit_depth, interlace != 0,
                                   (const uint8_t (*)[3])pal, pal_count,
                                   has_trns, trns_gray, trns_r, trns_g, trns_b,
                                   trns_pal, trns_pal_count, f);
            if (st != XX_IMAGE_OK) {
                xx_image_buf_free(&idat_buf);
                for (size_t i = 0; i < anim_frame_count; ++i) xx_image_buf_free(&anim_frames[i].data);
                free(anim_frames);
                return st;
            }
        }

        for (size_t i = 0; i < anim_frame_count; ++i) {
            png_frame_info *af = &anim_frames[i];
            xx_image_frame *f = xx_image_add_frame(img, af->width, af->height);
            if (!f) {
                xx_image_buf_free(&idat_buf);
                for (size_t k = 0; k < anim_frame_count; ++k) xx_image_buf_free(&anim_frames[k].data);
                free(anim_frames);
                return XX_IMAGE_ERR_MEMORY;
            }
            f->x = af->x_offset;
            f->y = af->y_offset;
            f->delay_ms = af->delay_ms;
            f->dispose = af->dispose;

            st = png_decode_raster(af->data.data, af->data.size, af->width, af->height,
                                   color_type, bit_depth, interlace != 0,
                                   (const uint8_t (*)[3])pal, pal_count,
                                   has_trns, trns_gray, trns_r, trns_g, trns_b,
                                   trns_pal, trns_pal_count, f);
            if (st != XX_IMAGE_OK) {
                xx_image_buf_free(&idat_buf);
                for (size_t k = 0; k < anim_frame_count; ++k) xx_image_buf_free(&anim_frames[k].data);
                free(anim_frames);
                return st;
            }
        }
    } else {
        /* Default single frame from IDAT */
        xx_image_frame *f = xx_image_add_frame(img, w, h);
        if (!f) {
            xx_image_buf_free(&idat_buf);
            for (size_t i = 0; i < anim_frame_count; ++i) xx_image_buf_free(&anim_frames[i].data);
            free(anim_frames);
            return XX_IMAGE_ERR_MEMORY;
        }

        st = png_decode_raster(idat_buf.data, idat_buf.size, w, h,
                               color_type, bit_depth, interlace != 0,
                               (const uint8_t (*)[3])pal, pal_count,
                               has_trns, trns_gray, trns_r, trns_g, trns_b,
                               trns_pal, trns_pal_count, f);
        if (st != XX_IMAGE_OK) {
            xx_image_buf_free(&idat_buf);
            for (size_t i = 0; i < anim_frame_count; ++i) xx_image_buf_free(&anim_frames[i].data);
            free(anim_frames);
            return st;
        }
    }

    xx_image_buf_free(&idat_buf);
    for (size_t i = 0; i < anim_frame_count; ++i) xx_image_buf_free(&anim_frames[i].data);
    free(anim_frames);

    if (img->frame_count == 0) return XX_IMAGE_ERR_CORRUPT;

    /* Fill info */
    bool frame_has_alpha = false;
    for (size_t i = 0; i < img->frame_count; ++i) {
        if (xx_image_frame_has_alpha(&img->frames[i])) {
            frame_has_alpha = true;
            break;
        }
    }

    img->info.bits_per_sample = bit_depth;
    img->info.channels = (color_type == 0 || color_type == 3) ? 1 :
                         (color_type == 2 ? 3 : (color_type == 4 ? 2 : 4));
    img->info.bits_per_pixel = img->info.channels * bit_depth;
    img->info.has_alpha = frame_has_alpha;
    img->info.is_interlaced = (interlace != 0);
    img->info.is_animated = (img->frame_count > 1);
    img->info.frame_count = (uint32_t)img->frame_count;
    img->info.palette_entries = pal_count;
    XX_IMAGE_SET_STR(img->info.compression, "deflate");

    if (color_type == 3) {
        img->info.color_model = XX_IMAGE_COLOR_INDEXED;
    } else if (color_type == 0) {
        img->info.color_model = (bit_depth == 1) ? XX_IMAGE_COLOR_BILEVEL : XX_IMAGE_COLOR_GRAY;
    } else if (color_type == 4) {
        img->info.color_model = XX_IMAGE_COLOR_GRAY_ALPHA;
    } else if (color_type == 2) {
        img->info.color_model = frame_has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    } else {
        img->info.color_model = XX_IMAGE_COLOR_RGBA;
    }

    return XX_IMAGE_OK;
}

static uint8_t *png_filter_scanlines(const uint8_t *raw_data, uint32_t w, uint32_t h,
                                     size_t bpp_bytes, size_t row_bytes, size_t *out_len)
{
    size_t filtered_row_len = 1U + row_bytes;
    size_t total_len = (size_t)h * filtered_row_len;
    uint8_t *out = (uint8_t *)malloc(total_len);
    uint8_t *prev = (uint8_t *)calloc(1, row_bytes);
    if (!out || !prev) {
        free(out);
        free(prev);
        return NULL;
    }

    *out_len = total_len;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *curr = raw_data + (size_t)y * row_bytes;
        uint8_t *dst_row = out + (size_t)y * filtered_row_len;

        /* Try all 5 filters and select minimum sum of absolute differences */
        uint8_t best_f = 0;
        uint64_t best_sad = UINT64_MAX;

        for (uint8_t f = 0; f < 5; ++f) {
            uint64_t sad = 0;
            for (size_t x = 0; x < row_bytes; ++x) {
                uint8_t raw = curr[x];
                uint8_t a = (x >= bpp_bytes) ? curr[x - bpp_bytes] : 0;
                uint8_t b = prev[x];
                uint8_t c = (x >= bpp_bytes) ? prev[x - bpp_bytes] : 0;
                uint8_t filt = 0;

                switch (f) {
                case 0: filt = raw; break;
                case 1: filt = (uint8_t)(raw - a); break;
                case 2: filt = (uint8_t)(raw - b); break;
                case 3: filt = (uint8_t)(raw - ((a + b) >> 1)); break;
                case 4: filt = (uint8_t)(raw - png_paeth_predict(a, b, c)); break;
                }
                sad += (uint64_t)abs((int8_t)filt);
            }

            if (sad < best_sad) {
                best_sad = sad;
                best_f = f;
            }
        }

        dst_row[0] = best_f;
        for (size_t x = 0; x < row_bytes; ++x) {
            uint8_t raw = curr[x];
            uint8_t a = (x >= bpp_bytes) ? curr[x - bpp_bytes] : 0;
            uint8_t b = prev[x];
            uint8_t c = (x >= bpp_bytes) ? prev[x - bpp_bytes] : 0;
            uint8_t filt = 0;

            switch (best_f) {
            case 0: filt = raw; break;
            case 1: filt = (uint8_t)(raw - a); break;
            case 2: filt = (uint8_t)(raw - b); break;
            case 3: filt = (uint8_t)(raw - ((a + b) >> 1)); break;
            case 4: filt = (uint8_t)(raw - png_paeth_predict(a, b, c)); break;
            }
            dst_row[1U + x] = filt;
        }

        memcpy(prev, curr, row_bytes);
    }

    free(prev);
    (void)w;
    return out;
}

static xx_image_status png_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    bool is_apng = (o && o->all_frames && img->frame_count > 1);

    if (!img->frame_count) return XX_IMAGE_ERR_ARGUMENT;

    /* Write 8-byte signature */
    xx_image_buf_append(out, g_png_signature, 8);

    if (is_apng) {
        uint32_t sw = img->width ? img->width : img->frames[0].width;
        uint32_t sh = img->height ? img->height : img->frames[0].height;

        /* Write IHDR (RGBA, 8-bit) */
        uint8_t ihdr[13];
        ihdr[0] = (uint8_t)(sw >> 24); ihdr[1] = (uint8_t)(sw >> 16); ihdr[2] = (uint8_t)(sw >> 8); ihdr[3] = (uint8_t)sw;
        ihdr[4] = (uint8_t)(sh >> 24); ihdr[5] = (uint8_t)(sh >> 16); ihdr[6] = (uint8_t)(sh >> 8); ihdr[7] = (uint8_t)sh;
        ihdr[8] = 8; /* bit depth */
        ihdr[9] = 6; /* RGBA */
        ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
        png_write_chunk(out, "IHDR", ihdr, 13);

        /* Write acTL */
        uint8_t actl[8];
        uint32_t nframes = (uint32_t)img->frame_count;
        uint32_t nplays = img->loop_count;
        actl[0] = (uint8_t)(nframes >> 24); actl[1] = (uint8_t)(nframes >> 16); actl[2] = (uint8_t)(nframes >> 8); actl[3] = (uint8_t)nframes;
        actl[4] = (uint8_t)(nplays >> 24);  actl[5] = (uint8_t)(nplays >> 16);  actl[6] = (uint8_t)(nplays >> 8);  actl[7] = (uint8_t)nplays;
        png_write_chunk(out, "acTL", actl, 8);

        uint32_t seq = 0;

        for (size_t i = 0; i < img->frame_count; ++i) {
            const xx_image_frame *f = &img->frames[i];
            uint32_t fw = f->width;
            uint32_t fh = f->height;
            uint32_t fx = f->x >= 0 ? (uint32_t)f->x : 0;
            uint32_t fy = f->y >= 0 ? (uint32_t)f->y : 0;
            uint16_t dnum = (uint16_t)f->delay_ms;
            uint16_t dden = 1000;
            uint8_t disp = (uint8_t)(f->dispose == 2 ? 1 : f->dispose == 3 ? 2 : 0);

            /* Write fcTL */
            uint8_t fctl[26];
            fctl[0] = (uint8_t)(seq >> 24); fctl[1] = (uint8_t)(seq >> 16); fctl[2] = (uint8_t)(seq >> 8); fctl[3] = (uint8_t)seq;
            ++seq;
            fctl[4] = (uint8_t)(fw >> 24); fctl[5] = (uint8_t)(fw >> 16); fctl[6] = (uint8_t)(fw >> 8); fctl[7] = (uint8_t)fw;
            fctl[8] = (uint8_t)(fh >> 24); fctl[9] = (uint8_t)(fh >> 16); fctl[10] = (uint8_t)(fh >> 8); fctl[11] = (uint8_t)fh;
            fctl[12] = (uint8_t)(fx >> 24); fctl[13] = (uint8_t)(fx >> 16); fctl[14] = (uint8_t)(fx >> 8); fctl[15] = (uint8_t)fx;
            fctl[16] = (uint8_t)(fy >> 24); fctl[17] = (uint8_t)(fy >> 16); fctl[18] = (uint8_t)(fy >> 8); fctl[19] = (uint8_t)fy;
            fctl[20] = (uint8_t)(dnum >> 8); fctl[21] = (uint8_t)dnum;
            fctl[22] = (uint8_t)(dden >> 8); fctl[23] = (uint8_t)dden;
            fctl[24] = disp;
            fctl[25] = 0; /* blend op source */
            png_write_chunk(out, "fcTL", fctl, 26);

            /* Filter and compress */
            size_t row_bytes = (size_t)fw * 4U;
            size_t filtered_len = 0;
            uint8_t *filtered = png_filter_scanlines(f->rgba, fw, fh, 4, row_bytes, &filtered_len);
            if (!filtered) return XX_IMAGE_ERR_MEMORY;

            xx_image_buffer comp;
            memset(&comp, 0, sizeof(comp));
            xx_image_status st = xx_image_zlib_deflate(filtered, filtered_len, o ? o->level : -1, &comp);
            free(filtered);
            if (st != XX_IMAGE_OK) {
                xx_image_buf_free(&comp);
                return st;
            }

            if (i == 0) {
                png_write_chunk(out, "IDAT", comp.data, comp.size);
            } else {
                /* fdAT: 4-byte sequence number + compressed data */
                uint8_t seq_bytes[4];
                seq_bytes[0] = (uint8_t)(seq >> 24); seq_bytes[1] = (uint8_t)(seq >> 16);
                seq_bytes[2] = (uint8_t)(seq >> 8);  seq_bytes[3] = (uint8_t)seq;
                ++seq;

                xx_image_buffer fdat;
                memset(&fdat, 0, sizeof(fdat));
                xx_image_buf_append(&fdat, seq_bytes, 4);
                xx_image_buf_append(&fdat, comp.data, comp.size);
                png_write_chunk(out, "fdAT", fdat.data, fdat.size);
                xx_image_buf_free(&fdat);
            }
            xx_image_buf_free(&comp);
        }

        /* IEND */
        png_write_chunk(out, "IEND", NULL, 0);
        return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
    }

    /* Single frame PNG */
    const xx_image_frame *f = xx_image_save_frame(img, o);
    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    uint32_t w = f->width;
    uint32_t h = f->height;
    size_t total_px = (size_t)w * h;

    bool has_alpha = xx_image_frame_has_alpha(f);
    bool is_gray = xx_image_frame_is_gray(f);

    /* Check number of unique colors */
    uint8_t *q_indices = (uint8_t *)malloc(total_px);
    uint32_t q_pal[256];
    uint32_t unique_cnt = 0;
    if (q_indices) {
        unique_cnt = xx_image_quantize(f, 256, q_indices, q_pal);
    }

    /* Check if palette is exact */
    bool exact_palette = false;
    if (unique_cnt > 0 && unique_cnt <= 256) {
        exact_palette = true;
        for (size_t i = 0; i < total_px; ++i) {
            uint32_t orig_c = xx_image_get_rgba(f->rgba + i * 4U);
            if (q_pal[q_indices[i]] != orig_c) {
                exact_palette = false;
                break;
            }
        }
    }

    uint8_t color_type = 6;
    size_t bpp_bytes = 4;
    size_t row_bytes = (size_t)w * 4U;
    uint8_t *raw_buf = NULL;

    if (exact_palette) {
        color_type = 3; /* Indexed */
        bpp_bytes = 1;
        row_bytes = (size_t)w;
        raw_buf = q_indices;
    } else {
        free(q_indices);
        q_indices = NULL;

        if (is_gray) {
            if (has_alpha) {
                color_type = 4; /* Gray + Alpha */
                bpp_bytes = 2;
                row_bytes = (size_t)w * 2U;
                raw_buf = (uint8_t *)malloc(row_bytes * h);
                if (!raw_buf) return XX_IMAGE_ERR_MEMORY;
                for (size_t i = 0; i < total_px; ++i) {
                    raw_buf[i * 2U] = f->rgba[i * 4U];
                    raw_buf[i * 2U + 1U] = f->rgba[i * 4U + 3U];
                }
            } else {
                color_type = 0; /* Grayscale */
                bpp_bytes = 1;
                row_bytes = (size_t)w;
                raw_buf = (uint8_t *)malloc(row_bytes * h);
                if (!raw_buf) return XX_IMAGE_ERR_MEMORY;
                for (size_t i = 0; i < total_px; ++i) {
                    raw_buf[i] = f->rgba[i * 4U];
                }
            }
        } else {
            if (has_alpha) {
                color_type = 6; /* RGBA */
                bpp_bytes = 4;
                row_bytes = (size_t)w * 4U;
                raw_buf = (uint8_t *)malloc(row_bytes * h);
                if (!raw_buf) return XX_IMAGE_ERR_MEMORY;
                memcpy(raw_buf, f->rgba, row_bytes * h);
            } else {
                color_type = 2; /* RGB */
                bpp_bytes = 3;
                row_bytes = (size_t)w * 3U;
                raw_buf = (uint8_t *)malloc(row_bytes * h);
                if (!raw_buf) return XX_IMAGE_ERR_MEMORY;
                for (size_t i = 0; i < total_px; ++i) {
                    raw_buf[i * 3U] = f->rgba[i * 4U];
                    raw_buf[i * 3U + 1U] = f->rgba[i * 4U + 1U];
                    raw_buf[i * 3U + 2U] = f->rgba[i * 4U + 2U];
                }
            }
        }
    }

    /* Write IHDR */
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16); ihdr[2] = (uint8_t)(w >> 8); ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16); ihdr[6] = (uint8_t)(h >> 8); ihdr[7] = (uint8_t)h;
    ihdr[8] = 8; /* bit depth 8 */
    ihdr[9] = color_type;
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_write_chunk(out, "IHDR", ihdr, 13);

    /* Write PLTE & tRNS for palette */
    if (color_type == 3) {
        uint8_t plte[256 * 3];
        for (uint32_t i = 0; i < unique_cnt; ++i) {
            plte[i * 3U] = (uint8_t)(q_pal[i] >> 24);
            plte[i * 3U + 1U] = (uint8_t)(q_pal[i] >> 16);
            plte[i * 3U + 2U] = (uint8_t)(q_pal[i] >> 8);
        }
        png_write_chunk(out, "PLTE", plte, (size_t)unique_cnt * 3U);

        /* Find last index with alpha < 255 */
        int last_trns = -1;
        for (int i = (int)unique_cnt - 1; i >= 0; --i) {
            if ((q_pal[i] & 0xFFU) < 255U) {
                last_trns = i;
                break;
            }
        }
        if (last_trns >= 0) {
            uint8_t trns[256];
            for (int i = 0; i <= last_trns; ++i) {
                trns[i] = (uint8_t)(q_pal[i] & 0xFFU);
            }
            png_write_chunk(out, "tRNS", trns, (size_t)last_trns + 1U);
        }
    }

    /* Write pHYs when dpi is set */
    if (img->info.dpi_x > 0 && img->info.dpi_y > 0) {
        uint32_t px = (uint32_t)(img->info.dpi_x / 0.0254 + 0.5);
        uint32_t py = (uint32_t)(img->info.dpi_y / 0.0254 + 0.5);
        uint8_t phys[9];
        phys[0] = (uint8_t)(px >> 24); phys[1] = (uint8_t)(px >> 16); phys[2] = (uint8_t)(px >> 8); phys[3] = (uint8_t)px;
        phys[4] = (uint8_t)(py >> 24); phys[5] = (uint8_t)(py >> 16); phys[6] = (uint8_t)(py >> 8); phys[7] = (uint8_t)py;
        phys[8] = 1; /* meter */
        png_write_chunk(out, "pHYs", phys, 9);
    }

    /* Filter rows */
    size_t filtered_len = 0;
    uint8_t *filtered = png_filter_scanlines(raw_buf, w, h, bpp_bytes, row_bytes, &filtered_len);
    free(raw_buf);
    if (!filtered) return XX_IMAGE_ERR_MEMORY;

    /* Deflate */
    xx_image_buffer comp;
    memset(&comp, 0, sizeof(comp));
    xx_image_status st = xx_image_zlib_deflate(filtered, filtered_len, o ? o->level : -1, &comp);
    free(filtered);
    if (st != XX_IMAGE_OK) {
        xx_image_buf_free(&comp);
        return st;
    }

    /* Write IDAT */
    png_write_chunk(out, "IDAT", comp.data, comp.size);
    xx_image_buf_free(&comp);

    /* Write IEND */
    png_write_chunk(out, "IEND", NULL, 0);

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_png = {
    "png",
    "Portable Network Graphics",
    "png;apng",
    "image/png",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_MULTIFRAME | XX_IMAGE_CODEC_INFO,
    536 /* XX_FILE_TYPE_PNG */,
    png_probe,
    png_load,
    png_save
};
