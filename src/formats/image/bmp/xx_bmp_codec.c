/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Windows / OS/2 bitmap.
 * Load: core (12), info (40), OS/2 2.x (16/64), v2/v3 (52/56), v4 (108) and
 * v5 (124) headers; 1/2/4/8 bpp indexed, 16/24/32 bpp RGB, BI_BITFIELDS and
 * BI_ALPHABITFIELDS masks, BI_RLE4 / BI_RLE8, embedded PNG (via the PNG codec);
 * bottom-up and top-down.
 * Save: 24 bpp, 32 bpp with alpha (BITMAPV4HEADER bitfields), 8 bpp indexed
 * (quantised) with optional RLE8.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int bmp_probe(const uint8_t *d, size_t n)
{
    uint32_t dib;
    if (n < 18 || d[0] != 'B' || d[1] != 'M') return 0;
    dib = xx_image_rd_u32le(d + 14);
    if (dib == 12 || dib == 16 || dib == 40 || dib == 52 || dib == 56 || dib == 64 || dib == 108 || dib == 124) return 80;
    return 0;
}

static unsigned bmp_mask_shift(uint32_t m)
{
    unsigned s = 0;
    if (!m) return 0;
    while (!(m & 1)) {
        m >>= 1;
        ++s;
    }
    return s;
}

static unsigned bmp_mask_bits(uint32_t m)
{
    unsigned b = 0;
    m >>= bmp_mask_shift(m);
    while (m & 1) {
        m >>= 1;
        ++b;
    }
    return b;
}

static uint8_t bmp_scale(uint32_t v, unsigned bits)
{
    if (!bits) return 0;
    if (bits >= 8) return (uint8_t)(v >> (bits - 8));
    return (uint8_t)((v * 255U + ((1U << bits) - 1) / 2) / ((1U << bits) - 1));
}

static xx_image_status bmp_rle(const uint8_t *src, size_t n, bool rle4, uint32_t w, uint32_t h, uint8_t *idx, uint8_t *defined)
{
    size_t p = 0;
    uint32_t x = 0, y = 0; /* y counts stored rows (bottom-up) */
    while (p + 1 < n && y < h) {
        uint8_t a = src[p], b = src[p + 1];
        p += 2;
        if (a) {
            for (uint32_t i = 0; i < a && x < w; ++i, ++x) {
                uint8_t v = rle4 ? ((i & 1) ? (b & 15) : (b >> 4)) : b;
                idx[(size_t)y * w + x] = v;
                defined[(size_t)y * w + x] = 1;
            }
        } else if (b == 0) {
            x = 0;
            ++y;
        } else if (b == 1) {
            return XX_IMAGE_OK;
        } else if (b == 2) {
            if (p + 1 >= n) break;
            x += src[p];
            y += src[p + 1];
            p += 2;
        } else {
            size_t bytes = rle4 ? (b + 1U) / 2 : b;
            if (!xx_image_in_range(n, p, bytes)) return XX_IMAGE_ERR_CORRUPT;
            for (uint32_t i = 0; i < b; ++i, ++x) {
                uint8_t v = rle4 ? ((i & 1) ? (src[p + i / 2] & 15) : (src[p + i / 2] >> 4)) : src[p + i];
                if (x < w) {
                    idx[(size_t)y * w + x] = v;
                    defined[(size_t)y * w + x] = 1;
                }
            }
            p += (bytes + 1) & ~(size_t)1;
        }
    }
    return XX_IMAGE_OK; /* a missing end marker is common; keep what was decoded */
}

static xx_image_status bmp_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint32_t off, dib, comp = 0, clr_used = 0, w;
    int32_t hs;
    uint16_t bpp;
    uint32_t masks[4] = {0, 0, 0, 0};
    uint32_t pal[256];
    uint32_t pal_n = 0;
    size_t pal_off, entry;
    bool top_down, core;
    uint32_t h;
    xx_image_frame *f;
    xx_image_status st;
    const char *g = "BMP";

    if (n < 26 || d[0] != 'B' || d[1] != 'M') return XX_IMAGE_ERR_FORMAT;
    off = xx_image_rd_u32le(d + 10);
    dib = xx_image_rd_u32le(d + 14);
    if (dib < 12 || !xx_image_in_range(n, 14, dib)) return XX_IMAGE_ERR_CORRUPT;
    core = dib == 12;
    if (core) {
        w = xx_image_rd_u16le(d + 18);
        hs = (int16_t)xx_image_rd_u16le(d + 20);
        bpp = xx_image_rd_u16le(d + 24);
    } else {
        if (dib < 16) return XX_IMAGE_ERR_CORRUPT;
        w = xx_image_rd_u32le(d + 18);
        hs = (int32_t)xx_image_rd_u32le(d + 22);
        bpp = xx_image_rd_u16le(d + 28);
        if (dib >= 20) comp = xx_image_rd_u32le(d + 30);
        if (dib >= 36) clr_used = xx_image_rd_u32le(d + 46);
        if (dib >= 40) {
            img->info.dpi_x = xx_image_rd_u32le(d + 38) * 0.0254;
            img->info.dpi_y = xx_image_rd_u32le(d + 42) * 0.0254;
        }
        if (dib == 64 && comp == 3) comp = 100; /* OS/2 2.x Huffman 1D */
        if (dib == 64 && comp == 4) comp = 101; /* OS/2 2.x RLE24 */
    }
    if ((int32_t)w <= 0 || hs == 0 || hs == INT32_MIN) return XX_IMAGE_ERR_CORRUPT;
    top_down = hs < 0;
    h = (uint32_t)(hs < 0 ? -hs : hs);
    if (comp == 4 || comp == 5) {
        /* BI_JPEG / BI_PNG: the pixel array is a whole JPEG / PNG file */
        if (comp == 5 && off < n && xx_image_codec_find("png")) {
            xx_image inner;
            st = xx_image_load_memory(d + off, n - off, xx_image_codec_find("png"), o, &inner);
            if (st != XX_IMAGE_OK) {
                xx_image_free(&inner);
                return st;
            }
            /* move the frames over, keep our own info */
            img->frames = inner.frames;
            img->frame_count = inner.frame_count;
            img->frame_capacity = inner.frame_capacity;
            img->width = inner.width;
            img->height = inner.height;
            inner.frames = NULL;
            inner.frame_count = inner.frame_capacity = 0;
            xx_image_free(&inner);
            XX_IMAGE_SET_STR(img->info.compression, "PNG");
            img->info.is_compressed = true;
            return XX_IMAGE_OK;
        }
        return XX_IMAGE_ERR_UNSUPPORTED;
    }
    if (comp > 6) return XX_IMAGE_ERR_UNSUPPORTED;
    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    /* masks */
    if (comp == 3 || comp == 6) {
        if (dib >= 52) {
            masks[0] = xx_image_rd_u32le(d + 54);
            masks[1] = xx_image_rd_u32le(d + 58);
            masks[2] = xx_image_rd_u32le(d + 62);
            if (dib >= 56) masks[3] = xx_image_rd_u32le(d + 66);
        } else {
            size_t mo = 14 + dib;
            size_t cnt = comp == 6 ? 4 : 3;
            if (!xx_image_in_range(n, mo, cnt * 4)) return XX_IMAGE_ERR_CORRUPT;
            for (size_t i = 0; i < cnt; ++i) masks[i] = xx_image_rd_u32le(d + mo + i * 4);
        }
    } else if (bpp == 16) {
        masks[0] = 0x7C00;
        masks[1] = 0x03E0;
        masks[2] = 0x001F;
    } else if (bpp == 32) {
        masks[0] = 0x00FF0000;
        masks[1] = 0x0000FF00;
        masks[2] = 0x000000FF;
    }

    /* palette */
    entry = core ? 3 : 4;
    pal_off = 14 + dib + ((comp == 3 && dib == 40) ? 12 : (comp == 6 && dib == 40) ? 16 : 0);
    if (bpp <= 8) {
        uint32_t want = clr_used ? clr_used : (1U << bpp);
        if (want > 256) want = 256;
        for (uint32_t i = 0; i < want; ++i) {
            size_t p = pal_off + i * entry;
            if (!xx_image_in_range(n, p, 3) || (off && p + 3 > off)) break;
            pal[i] = ((uint32_t)d[p + 2] << 24) | ((uint32_t)d[p + 1] << 16) | ((uint32_t)d[p] << 8) | 0xFF;
            pal_n = i + 1;
        }
        if (!pal_n) return XX_IMAGE_ERR_CORRUPT;
        memcpy(img->palette, pal, pal_n * sizeof(uint32_t));
        img->palette_count = pal_n;
        for (uint32_t i = pal_n; i < 256; ++i) pal[i] = 0x000000FF;
    }

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;
    if (off >= n) return XX_IMAGE_ERR_CORRUPT;

    if (comp == 1 || comp == 2) {
        uint8_t *idx = (uint8_t *)calloc((size_t)w * h, 1), *def = (uint8_t *)calloc((size_t)w * h, 1);
        if (!idx || !def) {
            free(idx);
            free(def);
            return XX_IMAGE_ERR_MEMORY;
        }
        st = bmp_rle(d + off, n - off, comp == 2, w, h, idx, def);
        for (uint32_t y = 0; y < h && st == XX_IMAGE_OK; ++y) {
            uint32_t ry = top_down ? y : h - 1 - y;
            for (uint32_t x = 0; x < w; ++x) {
                size_t i = (size_t)y * w + x;
                uint8_t *px = f->rgba + ((size_t)ry * w + x) * 4;
                if (def[i]) xx_image_put_rgba(px, pal[idx[i]]);
                else memset(px, 0, 4); /* skipped pixels are transparent */
            }
        }
        free(idx);
        free(def);
        if (st != XX_IMAGE_OK) return st;
        img->info.has_alpha = true;
    } else {
        size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
        size_t row_bytes = ((size_t)w * bpp + 7) / 8;
        unsigned sh[4], bits[4];
        bool use_alpha = masks[3] != 0;
        bool all_zero_alpha = true;
        if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return XX_IMAGE_ERR_UNSUPPORTED;
        for (int k = 0; k < 4; ++k) {
            sh[k] = bmp_mask_shift(masks[k]);
            bits[k] = bmp_mask_bits(masks[k]);
        }
        for (uint32_t y = 0; y < h; ++y) {
            size_t ro = off + (size_t)y * stride;
            uint32_t ry = top_down ? y : h - 1 - y;
            uint8_t *dst = f->rgba + (size_t)ry * w * 4;
            if (!xx_image_in_range(n, ro, row_bytes)) {
                if (y == 0) return XX_IMAGE_ERR_CORRUPT;
                break; /* truncated: keep the rows we have */
            }
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t *s = d + ro;
                uint8_t *px = dst + (size_t)x * 4;
                if (bpp <= 8) {
                    unsigned per = 8 / bpp, v = (s[x / per] >> (8 - bpp * (x % per + 1))) & ((1U << bpp) - 1);
                    xx_image_put_rgba(px, pal[v]);
                } else if (bpp == 24) {
                    px[0] = s[x * 3 + 2];
                    px[1] = s[x * 3 + 1];
                    px[2] = s[x * 3];
                    px[3] = 255;
                } else {
                    uint32_t v = bpp == 16 ? xx_image_rd_u16le(s + x * 2) : xx_image_rd_u32le(s + x * 4);
                    for (int k = 0; k < 3; ++k) px[k] = bmp_scale((v & masks[k]) >> sh[k], bits[k]);
                    px[3] = use_alpha ? bmp_scale((v & masks[3]) >> sh[3], bits[3]) : 255;
                    if (use_alpha && px[3]) all_zero_alpha = false;
                }
            }
        }
        /* Many writers declare an alpha mask and leave it zero: treat as opaque. */
        if (use_alpha && all_zero_alpha) {
            size_t cnt = (size_t)w * h;
            for (size_t i = 0; i < cnt; ++i) f->rgba[i * 4 + 3] = 255;
            use_alpha = false;
        }
        img->info.has_alpha = use_alpha;
    }

    img->info.bits_per_pixel = bpp;
    img->info.channels = bpp <= 8 ? 1 : (img->info.has_alpha ? 4 : 3);
    img->info.color_model = bpp <= 8 ? XX_IMAGE_COLOR_INDEXED : img->info.has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    img->info.palette_entries = pal_n;
    img->info.frame_count = 1;
    img->info.is_compressed = comp == 1 || comp == 2;
    XX_IMAGE_SET_STR(img->info.compression, comp == 1 ? "RLE8" : comp == 2 ? "RLE4" : comp == 3 ? "bitfields" : comp == 6 ? "alpha bitfields" : "none");
    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, g, "file size", xx_image_rd_u32le(d + 2));
        xx_image_add_prop_uint(img, g, "pixel offset", off);
        xx_image_add_prop_uint(img, g, "header size", dib);
        xx_image_add_prop_text(img, g, "header", dib == 12 ? "BITMAPCOREHEADER" : dib == 40 ? "BITMAPINFOHEADER" : dib == 108 ? "BITMAPV4HEADER" : dib == 124 ? "BITMAPV5HEADER" : "OS/2 2.x / v2 / v3");
        xx_image_add_prop_int(img, g, "width", (int32_t)w);
        xx_image_add_prop_int(img, g, "height", hs);
        xx_image_add_prop_uint(img, g, "bits per pixel", bpp);
        xx_image_add_prop_uint(img, g, "compression", comp);
        if (!core) xx_image_add_prop_uint(img, g, "colors used", clr_used);
        if (comp == 3 || comp == 6) {
            char m[64];
            for (int k = 0; k < 4; ++k) {
                static const char *const names[4] = {"red mask", "green mask", "blue mask", "alpha mask"};
                snprintf(m, sizeof(m), "0x%08X", masks[k]);
                xx_image_add_prop_text(img, g, names[k], m);
            }
        }
        if (dib >= 108) {
            uint32_t cs = xx_image_rd_u32le(d + 14 + 56);
            char t[8] = {(char)(cs >> 24), (char)(cs >> 16), (char)(cs >> 8), (char)cs, 0};
            xx_image_add_prop_text(img, g, "color space", cs ? t : "calibrated");
        }
        if (dib >= 124) {
            uint32_t intent = xx_image_rd_u32le(d + 14 + 108), pofs = xx_image_rd_u32le(d + 14 + 112), psize = xx_image_rd_u32le(d + 14 + 116);
            xx_image_add_prop_uint(img, g, "intent", intent);
            if (psize && xx_image_in_range(n, 14 + (size_t)pofs, psize)) xx_image_add_prop_bytes(img, "ICC", "profile", d + 14 + pofs, psize);
        }
    }
    return XX_IMAGE_OK;
}

static xx_image_status bmp_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    int bpp;
    size_t stride, pix_off, start = out->size;
    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w > 0x7FFFFFFF || h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;
    bpp = o->bits_per_pixel;
    if (bpp != 8 && bpp != 24 && bpp != 32) bpp = xx_image_frame_has_alpha(f) ? 32 : 24;

    if (bpp == 8) {
        uint8_t *idx = (uint8_t *)malloc((size_t)w * h);
        uint32_t pal[256];
        uint32_t cnt;
        bool rle = o->rle;
        if (!idx) return XX_IMAGE_ERR_MEMORY;
        cnt = xx_image_quantize(f, 256, idx, pal);
        if (!cnt) {
            free(idx);
            return XX_IMAGE_ERR_MEMORY;
        }
        stride = ((size_t)w + 3) & ~(size_t)3;
        pix_off = 14 + 40 + (size_t)cnt * 4;
        xx_image_buf_append(out, "BM", 2);
        xx_image_buf_u32le(out, 0); /* patched */
        xx_image_buf_u32le(out, 0);
        xx_image_buf_u32le(out, (uint32_t)pix_off);
        xx_image_buf_u32le(out, 40);
        xx_image_buf_u32le(out, w);
        xx_image_buf_u32le(out, h);
        xx_image_buf_u16le(out, 1);
        xx_image_buf_u16le(out, 8);
        xx_image_buf_u32le(out, rle ? 1 : 0);
        xx_image_buf_u32le(out, 0);
        xx_image_buf_u32le(out, 2835);
        xx_image_buf_u32le(out, 2835);
        xx_image_buf_u32le(out, cnt);
        xx_image_buf_u32le(out, 0);
        for (uint32_t i = 0; i < cnt; ++i) {
            uint8_t e[4] = {(uint8_t)(pal[i] >> 8), (uint8_t)(pal[i] >> 16), (uint8_t)(pal[i] >> 24), 0};
            xx_image_buf_append(out, e, 4);
        }
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *row = idx + (size_t)(h - 1 - y) * w;
            if (!rle) {
                xx_image_buf_append(out, row, w);
                xx_image_buf_fill(out, 0, stride - w);
                continue;
            }
            for (uint32_t x = 0; x < w;) {
                uint32_t run = 1;
                while (x + run < w && run < 255 && row[x + run] == row[x]) ++run;
                if (run >= 3 || w - x < 3) {
                    xx_image_buf_u8(out, (uint8_t)run);
                    xx_image_buf_u8(out, row[x]);
                    x += run;
                } else {
                    /* literal run: stop where a repeat of 3 starts */
                    uint32_t lit = 0;
                    while (x + lit < w && lit < 255) {
                        if (x + lit + 2 < w && row[x + lit] == row[x + lit + 1] && row[x + lit] == row[x + lit + 2]) break;
                        ++lit;
                    }
                    if (lit < 3) {
                        for (uint32_t i = 0; i < lit; ++i) {
                            xx_image_buf_u8(out, 1);
                            xx_image_buf_u8(out, row[x + i]);
                        }
                    } else {
                        xx_image_buf_u8(out, 0);
                        xx_image_buf_u8(out, (uint8_t)lit);
                        xx_image_buf_append(out, row + x, lit);
                        if (lit & 1) xx_image_buf_u8(out, 0);
                    }
                    x += lit;
                }
            }
            xx_image_buf_u8(out, 0);
            xx_image_buf_u8(out, y + 1 == h ? 1 : 0);
        }
        free(idx);
        if (!out->failed) {
            xx_image_buf_put_u32le_at(out, start + 2, (uint32_t)(out->size - start));
            xx_image_buf_put_u32le_at(out, start + 34, (uint32_t)(out->size - start - pix_off));
        }
        return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
    }

    stride = (((size_t)w * (size_t)bpp + 31) / 32) * 4;
    if ((uint64_t)stride * h > 0xFFFFFFFFULL - 200) return XX_IMAGE_ERR_LIMIT;
    {
        uint32_t dib = bpp == 32 ? 108 : 40;
        pix_off = 14 + dib;
        xx_image_buf_append(out, "BM", 2);
        xx_image_buf_u32le(out, (uint32_t)(pix_off + stride * h));
        xx_image_buf_u32le(out, 0);
        xx_image_buf_u32le(out, (uint32_t)pix_off);
        xx_image_buf_u32le(out, dib);
        xx_image_buf_u32le(out, w);
        xx_image_buf_u32le(out, h);
        xx_image_buf_u16le(out, 1);
        xx_image_buf_u16le(out, (uint16_t)bpp);
        xx_image_buf_u32le(out, bpp == 32 ? 3 : 0);
        xx_image_buf_u32le(out, (uint32_t)(stride * h));
        xx_image_buf_u32le(out, 2835);
        xx_image_buf_u32le(out, 2835);
        xx_image_buf_u32le(out, 0);
        xx_image_buf_u32le(out, 0);
        if (bpp == 32) {
            xx_image_buf_u32le(out, 0x00FF0000);
            xx_image_buf_u32le(out, 0x0000FF00);
            xx_image_buf_u32le(out, 0x000000FF);
            xx_image_buf_u32le(out, 0xFF000000);
            xx_image_buf_u32le(out, 0x73524742); /* 'sRGB' */
            xx_image_buf_fill(out, 0, 36 + 12);
        }
    }
    if (!xx_image_buf_reserve(out, out->size + stride * h)) return XX_IMAGE_ERR_MEMORY;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *s = f->rgba + (size_t)(h - 1 - y) * w * 4;
        uint8_t *dst = out->data + out->size;
        memset(dst, 0, stride);
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t *p = s + (size_t)x * 4;
            if (bpp == 32) {
                dst[x * 4] = p[2];
                dst[x * 4 + 1] = p[1];
                dst[x * 4 + 2] = p[0];
                dst[x * 4 + 3] = p[3];
            } else {
                /* composite onto black */
                dst[x * 3] = (uint8_t)(p[2] * p[3] / 255);
                dst[x * 3 + 1] = (uint8_t)(p[1] * p[3] / 255);
                dst[x * 3 + 2] = (uint8_t)(p[0] * p[3] / 255);
            }
        }
        out->size += stride;
    }
    return XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_bmp = {"bmp", "Windows Bitmap", "bmp;dib", "image/bmp", XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_INFO,
                                     526 /* XX_FILE_TYPE_BMP */, bmp_probe, bmp_load, bmp_save};
