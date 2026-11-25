/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Windows Icon / Cursor (.ico, .cur) codec.
 * Load: every directory entry as a frame (in directory order), BMP DIB entries
 * (1/4/8/24/32 bpp, AND mask -> alpha, stored height doubled), PNG-compressed
 * icon entries via png codec, CUR hotspot coordinates as properties.
 * Save: 32-bit BMP DIB entries with AND mask, single or all frames (all_frames),
 * frames > 256x256 rejected with XX_IMAGE_ERR_LIMIT.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ico_probe(const uint8_t *d, size_t n)
{
    uint16_t reserved, type, count;
    if (n < 6) return 0;
    reserved = xx_image_rd_u16le(d);
    type = xx_image_rd_u16le(d + 2);
    count = xx_image_rd_u16le(d + 4);
    if (reserved != 0) return 0;
    if (type != 1 && type != 2) return 0;
    if (count == 0) return 0;
    if (n >= 6 + 16) {
        uint32_t size = xx_image_rd_u32le(d + 6 + 8);
        uint32_t offset = xx_image_rd_u32le(d + 6 + 12);
        if (offset >= 6 && size > 0 && xx_image_in_range(n, offset, size > 4 ? 4 : size)) {
            return 85;
        }
    }
    return 60;
}

static xx_image_status ico_load_dib(const uint8_t *d, size_t n, size_t res_off, size_t res_size,
                                 const xx_image_load_options *o, xx_image *img)
{
    uint32_t dib_size, dib_w, w, h;
    int32_t dib_h;
    uint16_t bpp;
    uint32_t clr_used;
    uint32_t num_colors;
    uint32_t pal[256];
    size_t xor_stride, and_stride, xor_offset, and_offset;
    bool has_genuine_alpha = false;
    xx_image_frame *f;
    xx_image_status st;

    if (!xx_image_in_range(n, res_off, 40) || res_size < 40) return XX_IMAGE_ERR_CORRUPT;
    dib_size = xx_image_rd_u32le(d + res_off);
    if (dib_size < 40 || !xx_image_in_range(n, res_off, dib_size)) return XX_IMAGE_ERR_CORRUPT;

    dib_w = xx_image_rd_u32le(d + res_off + 4);
    dib_h = (int32_t)xx_image_rd_u32le(d + res_off + 8);
    bpp = xx_image_rd_u16le(d + res_off + 14);
    clr_used = xx_image_rd_u32le(d + res_off + 32);

    if (dib_w == 0 || dib_h <= 1) return XX_IMAGE_ERR_CORRUPT;
    w = dib_w;
    h = (uint32_t)(dib_h / 2); /* Stored height is doubled */

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) {
        return XX_IMAGE_ERR_UNSUPPORTED;
    }

    /* Palette for indexed DIBs */
    for (uint32_t i = 0; i < 256; ++i) pal[i] = 0x000000FF;
    num_colors = 0;
    if (bpp <= 8) {
        num_colors = clr_used ? clr_used : (1U << bpp);
        if (num_colors > 256) num_colors = 256;
        if (!xx_image_in_range(n, res_off + dib_size, (size_t)num_colors * 4)) {
            return XX_IMAGE_ERR_CORRUPT;
        }
        for (uint32_t i = 0; i < num_colors; ++i) {
            size_t po = res_off + dib_size + i * 4;
            pal[i] = ((uint32_t)d[po + 2] << 24) | ((uint32_t)d[po + 1] << 16) | ((uint32_t)d[po] << 8) | 0xFF;
        }
    }

    xor_stride = (((size_t)w * bpp + 31) / 32) * 4;
    and_stride = (((size_t)w + 31) / 32) * 4;
    xor_offset = res_off + dib_size + (size_t)num_colors * 4;
    and_offset = xor_offset + xor_stride * h;

    if (!xx_image_in_range(n, xor_offset, xor_stride * h)) return XX_IMAGE_ERR_CORRUPT;

    /* Check if 32-bit DIB contains a genuine non-zero alpha channel */
    if (bpp == 32) {
        for (uint32_t y = 0; y < h && !has_genuine_alpha; ++y) {
            const uint8_t *row = d + xor_offset + (size_t)y * xor_stride;
            for (uint32_t x = 0; x < w; ++x) {
                if (row[x * 4 + 3] != 0) {
                    has_genuine_alpha = true;
                    break;
                }
            }
        }
    }

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    /* Decode bottom-up rows */
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t ry = h - 1 - y;
        uint8_t *dst = f->rgba + (size_t)ry * w * 4;
        const uint8_t *xor_row = d + xor_offset + (size_t)y * xor_stride;
        const uint8_t *and_row = xx_image_in_range(n, and_offset + (size_t)y * and_stride, and_stride) ?
                                 (d + and_offset + (size_t)y * and_stride) : NULL;

        for (uint32_t x = 0; x < w; ++x) {
            uint8_t *px = dst + (size_t)x * 4;
            if (bpp == 32) {
                px[0] = xor_row[x * 4 + 2];
                px[1] = xor_row[x * 4 + 1];
                px[2] = xor_row[x * 4];
                px[3] = has_genuine_alpha ? xor_row[x * 4 + 3] : 255;
            } else if (bpp == 24) {
                px[0] = xor_row[x * 3 + 2];
                px[1] = xor_row[x * 3 + 1];
                px[2] = xor_row[x * 3];
                px[3] = 255;
            } else {
                unsigned per = 8 / bpp;
                uint8_t v = (xor_row[x / per] >> (8 - bpp * (x % per + 1))) & ((1U << bpp) - 1);
                xx_image_put_rgba(px, pal[v]);
            }

            /* Apply 1-bit AND transparency mask */
            if (and_row) {
                uint8_t and_bit = (and_row[x / 8] >> (7 - (x % 8))) & 1;
                if (and_bit) {
                    if (!has_genuine_alpha) {
                        px[3] = 0;
                    } else if (px[3] == 0) {
                        px[3] = 0;
                    }
                }
            }
        }
    }

    return XX_IMAGE_OK;
}

static xx_image_status ico_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint16_t type, count;
    xx_image_status st = XX_IMAGE_OK;
    const char *g = "ICO";

    if (n < 6 || xx_image_rd_u16le(d) != 0) return XX_IMAGE_ERR_FORMAT;
    type = xx_image_rd_u16le(d + 2);
    count = xx_image_rd_u16le(d + 4);
    if ((type != 1 && type != 2) || count == 0) return XX_IMAGE_ERR_FORMAT;
    if (!xx_image_in_range(n, 6, (size_t)count * 16)) return XX_IMAGE_ERR_CORRUPT;

    for (uint16_t i = 0; i < count; ++i) {
        size_t entry_off = 6 + (size_t)i * 16;
        uint32_t res_size = xx_image_rd_u32le(d + entry_off + 8);
        uint32_t res_off = xx_image_rd_u32le(d + entry_off + 12);
        uint16_t w_planes = xx_image_rd_u16le(d + entry_off + 4);
        uint16_t w_bpp = xx_image_rd_u16le(d + entry_off + 6);

        if (!xx_image_in_range(n, res_off, res_size) || res_size < 4) continue;

        if (type == 2 && !o->skip_metadata) {
            char key[32];
            snprintf(key, sizeof(key), "frame %u hotspot x", (unsigned)i);
            xx_image_add_prop_uint(img, "CUR", key, w_planes);
            snprintf(key, sizeof(key), "frame %u hotspot y", (unsigned)i);
            xx_image_add_prop_uint(img, "CUR", key, w_bpp);
        }

        /* Check for embedded PNG */
        if (res_size >= 8 && memcmp(d + res_off, "\x89PNG\r\n\x1a\n", 8) == 0) {
            const xx_image_codec *png = xx_image_codec_find("png");
            if (png) {
                xx_image inner;
                st = xx_image_load_memory(d + res_off, res_size, png, o, &inner);
                if (st == XX_IMAGE_OK && inner.frame_count > 0) {
                    for (size_t fi = 0; fi < inner.frame_count; ++fi) {
                        const xx_image_frame *src = &inner.frames[fi];
                        xx_image_frame *dst = xx_image_add_frame(img, src->width, src->height);
                        if (dst) {
                            memcpy(dst->rgba, src->rgba, (size_t)src->width * src->height * 4);
                        }
                    }
                }
                xx_image_free(&inner);
            } else {
                return XX_IMAGE_ERR_UNSUPPORTED;
            }
        } else {
            st = ico_load_dib(d, n, res_off, res_size, o, img);
            if (st != XX_IMAGE_OK && img->frame_count == 0) return st;
        }

        if (o->first_frame_only && img->frame_count > 0) break;
    }

    if (img->frame_count == 0) return st != XX_IMAGE_OK ? st : XX_IMAGE_ERR_CORRUPT;

    img->info.color_model = XX_IMAGE_COLOR_RGBA;
    img->info.channels = 4;
    img->info.bits_per_pixel = 32;
    img->info.bits_per_sample = 8;
    img->info.has_alpha = true;
    img->info.frame_count = (uint32_t)img->frame_count;
    XX_IMAGE_SET_STR(img->info.compression, "none");

    if (!o->skip_metadata) {
        xx_image_add_prop_text(img, g, "type", type == 2 ? "CUR" : "ICO");
        xx_image_add_prop_uint(img, g, "icon count", count);
    }

    return XX_IMAGE_OK;
}

static xx_image_status ico_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    size_t first_idx = 0;
    size_t count = 1;
    size_t start_off = out->size;
    size_t dir_pos;

    if (!img || img->frame_count == 0) return XX_IMAGE_ERR_ARGUMENT;

    if (o && o->all_frames) {
        first_idx = 0;
        count = img->frame_count;
    } else {
        first_idx = (o && o->frame < img->frame_count) ? o->frame : 0;
        count = 1;
    }

    if (count > 0xFFFF) return XX_IMAGE_ERR_LIMIT;

    /* Verify all frames are <= 256x256 */
    for (size_t i = 0; i < count; ++i) {
        const xx_image_frame *f = &img->frames[first_idx + i];
        if (f->width > 256 || f->height > 256) return XX_IMAGE_ERR_LIMIT;
    }

    /* 6-byte ICO header */
    xx_image_buf_u16le(out, 0);                   /* Reserved */
    xx_image_buf_u16le(out, 1);                   /* Type: ICO */
    xx_image_buf_u16le(out, (uint16_t)count);     /* Frame count */

    dir_pos = out->size;
    /* Reserve directory entries (16 bytes each) */
    xx_image_buf_fill(out, 0, count * 16);

    for (size_t i = 0; i < count; ++i) {
        const xx_image_frame *f = &img->frames[first_idx + i];
        uint32_t w = f->width;
        uint32_t h = f->height;
        size_t xor_stride = (((size_t)w * 32 + 31) / 32) * 4;
        size_t and_stride = (((size_t)w + 31) / 32) * 4;
        size_t dib_data_size = 40 + xor_stride * h + and_stride * h;
        size_t dib_offset = out->size - start_off;
        size_t entry = dir_pos + i * 16;

        if (dib_data_size > 0xFFFFFFFFULL || dib_offset > 0xFFFFFFFFULL) return XX_IMAGE_ERR_LIMIT;

        /* Write directory entry */
        out->data[entry] = (uint8_t)(w == 256 ? 0 : w);
        out->data[entry + 1] = (uint8_t)(h == 256 ? 0 : h);
        out->data[entry + 2] = 0;  /* Palette count */
        out->data[entry + 3] = 0;  /* Reserved */
        out->data[entry + 4] = 1;  /* Planes */
        out->data[entry + 5] = 0;
        out->data[entry + 6] = 32; /* Bit count */
        out->data[entry + 7] = 0;
        xx_image_buf_put_u32le_at(out, entry + 8, (uint32_t)dib_data_size);
        xx_image_buf_put_u32le_at(out, entry + 12, (uint32_t)dib_offset);

        /* 40-byte BITMAPINFOHEADER */
        xx_image_buf_u32le(out, 40);
        xx_image_buf_u32le(out, w);
        xx_image_buf_u32le(out, h * 2); /* Doubled height for XOR + AND */
        xx_image_buf_u16le(out, 1);     /* Planes */
        xx_image_buf_u16le(out, 32);    /* BitsPerPixel */
        xx_image_buf_u32le(out, 0);     /* BI_RGB */
        xx_image_buf_u32le(out, (uint32_t)(xor_stride * h + and_stride * h));
        xx_image_buf_u32le(out, 0);     /* XPelsPerMeter */
        xx_image_buf_u32le(out, 0);     /* YPelsPerMeter */
        xx_image_buf_u32le(out, 0);     /* ClrUsed */
        xx_image_buf_u32le(out, 0);     /* ClrImportant */

        /* XOR mask (BGRA, bottom-up) */
        if (!xx_image_buf_reserve(out, out->size + xor_stride * h + and_stride * h)) return XX_IMAGE_ERR_MEMORY;
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *src = f->rgba + (size_t)(h - 1 - y) * w * 4;
            uint8_t *dst = out->data + out->size;
            memset(dst, 0, xor_stride);
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t *px = src + (size_t)x * 4;
                dst[x * 4] = px[2];     /* B */
                dst[x * 4 + 1] = px[1]; /* G */
                dst[x * 4 + 2] = px[0]; /* R */
                dst[x * 4 + 3] = px[3]; /* A */
            }
            out->size += xor_stride;
        }

        /* AND mask (1-bit, bottom-up: 1 = transparent, 0 = opaque) */
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *src = f->rgba + (size_t)(h - 1 - y) * w * 4;
            uint8_t *dst = out->data + out->size;
            memset(dst, 0, and_stride);
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t *px = src + (size_t)x * 4;
                if (px[3] < 128) {
                    dst[x / 8] |= (uint8_t)(0x80 >> (x % 8));
                }
            }
            out->size += and_stride;
        }
    }

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_ico = {
    "ico",
    "Windows Icon / Cursor",
    "ico;cur",
    "image/x-icon",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_MULTIFRAME | XX_IMAGE_CODEC_INFO,
    554,
    ico_probe,
    ico_load,
    ico_save
};
