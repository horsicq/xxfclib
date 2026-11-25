/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Truevision TGA (TARGA) format.
 * Load: image types 1, 2, 3, 9, 10, 11 (indexed, true-colour, gray, RLE),
 * 8/15/16/24/32 bpp, color maps (15/16/24/32-bit), origins, alpha, TGA 2.0 footer & ext.
 * Save: 24/32 bpp (uncompressed or RLE), origin top-left, TGA 2.0 footer.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char g_tga_signature[18] = "TRUEVISION-XFILE.";

static int tga_probe(const uint8_t *d, size_t n)
{
    uint8_t id_len, cm_type, img_type, cm_entry_size, bpp, desc;
    uint16_t cm_len, w, h;
    size_t cm_bytes, pix_off;
    bool has_footer = false;

    if (n < 18) return 0;

    if (n >= 26 && memcmp(d + n - 18, g_tga_signature, 17) == 0 && d[n - 1] == 0) {
        has_footer = true;
    }

    id_len = d[0];
    cm_type = d[1];
    img_type = d[2];
    cm_len = xx_image_rd_u16le(d + 5);
    cm_entry_size = d[7];
    w = xx_image_rd_u16le(d + 12);
    h = xx_image_rd_u16le(d + 14);
    bpp = d[16];
    desc = d[17];

    if (cm_type > 1) return 0;
    if (w == 0 || h == 0) return 0;
    if ((desc & 0xC0U) != 0) return 0; /* interleaving must be 0 */

    if (img_type == 1 || img_type == 9) {
        if (cm_type != 1 || cm_len == 0) return 0;
        if (cm_entry_size != 15 && cm_entry_size != 16 && cm_entry_size != 24 && cm_entry_size != 32) return 0;
        if (bpp != 8) return 0;
    } else if (img_type == 2 || img_type == 10) {
        if (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return 0;
    } else if (img_type == 3 || img_type == 11) {
        if (bpp != 8 && bpp != 16) return 0;
    } else {
        return 0;
    }

    cm_bytes = (cm_type == 1) ? ((size_t)cm_len * ((cm_entry_size + 7U) / 8U)) : 0;
    pix_off = 18U + (size_t)id_len + cm_bytes;
    if (pix_off > n) return 0;

    if (img_type == 1 || img_type == 2 || img_type == 3) {
        size_t bpp_bytes = (bpp + 7U) / 8U;
        if (!xx_image_in_range(n, pix_off, (size_t)w * h * bpp_bytes)) return 0;
    } else {
        if (pix_off >= n) return 0;
    }

    return has_footer ? 70 : 40;
}

static inline void tga_read_pixel(const uint8_t *p, uint8_t bpp, uint8_t img_type, const uint32_t *pal,
                                  uint8_t attr_bits, bool use_alpha,
                                  uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    *r = *g = *b = 0;
    *a = 255;
    if (img_type == 1 || img_type == 9) {
        uint32_t c = pal[p[0]];
        *r = (uint8_t)(c >> 24);
        *g = (uint8_t)(c >> 16);
        *b = (uint8_t)(c >> 8);
        *a = use_alpha ? (uint8_t)c : 255;
    } else if (img_type == 3 || img_type == 11) {
        if (bpp == 8) {
            *r = *g = *b = p[0];
            *a = 255;
        } else {
            /* 16-bit gray + alpha or 16-bit gray */
            *r = *g = *b = p[0];
            *a = use_alpha ? p[1] : 255;
        }
    } else {
        /* True colour */
        if (bpp == 15 || bpp == 16) {
            uint16_t v = xx_image_rd_u16le(p);
            *r = (uint8_t)(((v >> 10) & 0x1FU) * 255U / 31U);
            *g = (uint8_t)(((v >> 5) & 0x1FU) * 255U / 31U);
            *b = (uint8_t)((v & 0x1FU) * 255U / 31U);
            *a = (use_alpha && attr_bits == 1) ? ((v & 0x8000U) ? 255 : 0) : 255;
        } else if (bpp == 24) {
            *b = p[0];
            *g = p[1];
            *r = p[2];
            *a = 255;
        } else {
            /* 32-bit */
            *b = p[0];
            *g = p[1];
            *r = p[2];
            *a = use_alpha ? p[3] : 255;
        }
    }
}

static xx_image_status tga_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint8_t id_len, cm_type, img_type, cm_entry_size, bpp, desc, attr_bits;
    uint16_t cm_first, cm_len, w, h;
    size_t cm_bytes, pix_off, total_pixels, px_count, p;
    bool has_footer = false, is_rle, origin_top, origin_right, use_alpha;
    uint32_t pal[256];
    uint8_t attr_type = 0;
    bool has_attr_type = false;
    bool all_zero_alpha = true;
    xx_image_frame *f;
    xx_image_status st;

    if (n < 18) return XX_IMAGE_ERR_FORMAT;
    if (tga_probe(d, n) == 0) return XX_IMAGE_ERR_FORMAT;

    id_len = d[0];
    cm_type = d[1];
    img_type = d[2];
    cm_first = xx_image_rd_u16le(d + 3);
    cm_len = xx_image_rd_u16le(d + 5);
    cm_entry_size = d[7];
    w = xx_image_rd_u16le(d + 12);
    h = xx_image_rd_u16le(d + 14);
    bpp = d[16];
    desc = d[17];
    attr_bits = desc & 0x0FU;
    origin_right = (desc & 0x10U) != 0;
    origin_top = (desc & 0x20U) != 0;
    is_rle = (img_type == 9 || img_type == 10 || img_type == 11);

    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    if (n >= 26 && memcmp(d + n - 18, g_tga_signature, 17) == 0 && d[n - 1] == 0) {
        has_footer = true;
        uint32_t ext_off = xx_image_rd_u32le(d + n - 26);
        if (ext_off != 0 && xx_image_in_range(n, ext_off, 495)) {
            uint16_t ext_size = xx_image_rd_u16le(d + ext_off);
            if (ext_size >= 495) {
                attr_type = d[ext_off + 494];
                has_attr_type = true;
                if (!o->skip_metadata) {
                    if (d[ext_off + 2]) xx_image_add_prop_text(img, "TGA", "author", (const char *)(d + ext_off + 2));
                    for (int l = 0; l < 4; ++l) {
                        const char *line = (const char *)(d + ext_off + 43 + (size_t)l * 81);
                        if (*line) {
                            char key[32];
                            snprintf(key, sizeof(key), "comment %d", l + 1);
                            xx_image_add_prop_text(img, "TGA", key, line);
                        }
                    }
                    {
                        uint16_t mo = xx_image_rd_u16le(d + ext_off + 367);
                        uint16_t da = xx_image_rd_u16le(d + ext_off + 369);
                        uint16_t yr = xx_image_rd_u16le(d + ext_off + 371);
                        uint16_t hr = xx_image_rd_u16le(d + ext_off + 373);
                        uint16_t mi = xx_image_rd_u16le(d + ext_off + 375);
                        uint16_t se = xx_image_rd_u16le(d + ext_off + 377);
                        if (mo || da || yr) {
                            char dt[64];
                            snprintf(dt, sizeof(dt), "%04u-%02u-%02u %02u:%02u:%02u", yr, mo, da, hr, mi, se);
                            xx_image_add_prop_text(img, "TGA", "date", dt);
                        }
                    }
                    if (d[ext_off + 423]) xx_image_add_prop_text(img, "TGA", "software", (const char *)(d + ext_off + 423));
                }
            }
        }
    }

    if (id_len > 0) {
        if (!xx_image_in_range(n, 18, id_len)) return XX_IMAGE_ERR_CORRUPT;
        if (!o->skip_metadata) {
            xx_image_add_prop_textn(img, "TGA", "id", (const char *)(d + 18), id_len);
        }
    }

    memset(pal, 0, sizeof(pal));
    if (cm_type == 1 && cm_len > 0) {
        size_t cm_off = 18U + (size_t)id_len;
        size_t entry_bytes = (cm_entry_size + 7U) / 8U;
        if (!xx_image_in_range(n, cm_off, (size_t)cm_len * entry_bytes)) return XX_IMAGE_ERR_CORRUPT;
        for (uint32_t i = 0; i < cm_len; ++i) {
            uint32_t idx = cm_first + i;
            const uint8_t *entry = d + cm_off + (size_t)i * entry_bytes;
            uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
            if (idx >= 256) break;
            if (cm_entry_size == 15 || cm_entry_size == 16) {
                uint16_t val = xx_image_rd_u16le(entry);
                pr = (uint8_t)(((val >> 10) & 0x1FU) * 255U / 31U);
                pg = (uint8_t)(((val >> 5) & 0x1FU) * 255U / 31U);
                pb = (uint8_t)((val & 0x1FU) * 255U / 31U);
            } else if (cm_entry_size == 24) {
                pb = entry[0];
                pg = entry[1];
                pr = entry[2];
            } else if (cm_entry_size == 32) {
                pb = entry[0];
                pg = entry[1];
                pr = entry[2];
                pa = entry[3];
            }
            pal[idx] = ((uint32_t)pr << 24) | ((uint32_t)pg << 16) | ((uint32_t)pb << 8) | pa;
        }
        if (img_type == 1 || img_type == 9) {
            uint32_t cnt = cm_len > 256 ? 256 : cm_len;
            memcpy(img->palette, pal, cnt * sizeof(uint32_t));
            img->palette_count = cnt;
        }
    }

    /* Alpha handling */
    if (bpp == 32) {
        if (has_attr_type && (attr_type == 0 || attr_type == 1)) {
            use_alpha = false;
        } else {
            use_alpha = true;
        }
    } else if (bpp == 16 && attr_bits == 1) {
        use_alpha = true;
    } else if ((img_type == 1 || img_type == 9) && cm_entry_size == 32) {
        use_alpha = true;
    } else if ((img_type == 3 || img_type == 11) && bpp == 16 && attr_bits == 8) {
        use_alpha = true;
    } else {
        use_alpha = false;
    }

    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;

    total_pixels = (size_t)w * h;
    cm_bytes = (cm_type == 1) ? ((size_t)cm_len * ((cm_entry_size + 7U) / 8U)) : 0;
    pix_off = 18U + (size_t)id_len + cm_bytes;
    p = pix_off;
    px_count = 0;

    {
        size_t bpp_bytes = (bpp + 7U) / 8U;
        while (px_count < total_pixels && p < n) {
            size_t count = 1;
            bool rle_packet = false;

            if (is_rle) {
                uint8_t rep = d[p++];
                rle_packet = (rep & 0x80U) != 0;
                count = (size_t)(rep & 0x7FU) + 1U;
            }

            if (rle_packet) {
                uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
                if (p + bpp_bytes > n) break;
                tga_read_pixel(d + p, bpp, img_type, pal, attr_bits, use_alpha, &pr, &pg, &pb, &pa);
                p += bpp_bytes;

                for (size_t k = 0; k < count && px_count < total_pixels; ++k, ++px_count) {
                    size_t x = px_count % w;
                    size_t y = px_count / w;
                    size_t ry = origin_top ? y : (h - 1U - y);
                    size_t rx = origin_right ? (w - 1U - x) : x;
                    uint8_t *dst = f->rgba + (ry * w + rx) * 4U;
                    dst[0] = pr;
                    dst[1] = pg;
                    dst[2] = pb;
                    dst[3] = pa;
                    if (pa != 0) all_zero_alpha = false;
                }
            } else {
                for (size_t k = 0; k < count && px_count < total_pixels; ++k, ++px_count) {
                    uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
                    if (p + bpp_bytes > n) break;
                    tga_read_pixel(d + p, bpp, img_type, pal, attr_bits, use_alpha, &pr, &pg, &pb, &pa);
                    p += bpp_bytes;

                    size_t x = px_count % w;
                    size_t y = px_count / w;
                    size_t ry = origin_top ? y : (h - 1U - y);
                    size_t rx = origin_right ? (w - 1U - x) : x;
                    uint8_t *dst = f->rgba + (ry * w + rx) * 4U;
                    dst[0] = pr;
                    dst[1] = pg;
                    dst[2] = pb;
                    dst[3] = pa;
                    if (pa != 0) all_zero_alpha = false;
                }
            }
        }
    }

    if (px_count == 0) return XX_IMAGE_ERR_CORRUPT;

    /* If 32-bpp had all zero alpha, treat as opaque */
    if (use_alpha && all_zero_alpha && bpp == 32) {
        for (size_t i = 0; i < total_pixels; ++i) f->rgba[i * 4U + 3U] = 255;
        use_alpha = false;
    }

    img->info.bits_per_pixel = bpp;
    img->info.bits_per_sample = (bpp == 15 || bpp == 16) ? 5 : 8;
    img->info.channels = use_alpha ? 4 : (bpp <= 8 && (img_type == 3 || img_type == 11) ? 1 : (img_type == 1 || img_type == 9 ? 1 : 3));
    img->info.has_alpha = use_alpha;
    img->info.color_model = (img_type == 1 || img_type == 9) ? XX_IMAGE_COLOR_INDEXED :
                            (img_type == 3 || img_type == 11) ? XX_IMAGE_COLOR_GRAY :
                            (use_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB);
    img->info.frame_count = 1;
    img->info.is_compressed = is_rle;
    XX_IMAGE_SET_STR(img->info.compression, is_rle ? "RLE" : "none");

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, "TGA", "width", w);
        xx_image_add_prop_uint(img, "TGA", "height", h);
        xx_image_add_prop_uint(img, "TGA", "image type", img_type);
        xx_image_add_prop_uint(img, "TGA", "bits per pixel", bpp);
        xx_image_add_prop_uint(img, "TGA", "attribute bits", attr_bits);
        xx_image_add_prop_bool(img, "TGA", "origin top", origin_top);
        xx_image_add_prop_bool(img, "TGA", "origin right", origin_right);
        if (has_footer) xx_image_add_prop_bool(img, "TGA", "footer 2.0", true);
    }

    return XX_IMAGE_OK;
}

static xx_image_status tga_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    int bpp;
    bool rle;
    size_t total_pixels;

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w > 0xFFFFU || h > 0xFFFFU) return XX_IMAGE_ERR_LIMIT;

    bpp = o ? o->bits_per_pixel : 0;
    if (bpp != 24 && bpp != 32) bpp = xx_image_frame_has_alpha(f) ? 32 : 24;

    rle = o ? o->rle : false;
    total_pixels = (size_t)w * h;

    if (!xx_image_buf_reserve(out, out->size + 18U + total_pixels * 5U + 26U)) return XX_IMAGE_ERR_MEMORY;

    /* Header (18 bytes) */
    xx_image_buf_u8(out, 0); /* ID length */
    xx_image_buf_u8(out, 0); /* Colormap type */
    xx_image_buf_u8(out, (uint8_t)(rle ? 10 : 2)); /* True-color (RLE or uncompressed) */
    xx_image_buf_u16le(out, 0); /* Colormap first */
    xx_image_buf_u16le(out, 0); /* Colormap length */
    xx_image_buf_u8(out, 0); /* Colormap entry size */
    xx_image_buf_u16le(out, 0); /* X origin */
    xx_image_buf_u16le(out, 0); /* Y origin */
    xx_image_buf_u16le(out, (uint16_t)w);
    xx_image_buf_u16le(out, (uint16_t)h);
    xx_image_buf_u8(out, (uint8_t)bpp);
    xx_image_buf_u8(out, (uint8_t)((bpp == 32 ? 8 : 0) | 0x20)); /* Top-left origin */

    if (!rle) {
        for (size_t i = 0; i < total_pixels; ++i) {
            const uint8_t *px = f->rgba + i * 4U;
            if (bpp == 32) {
                xx_image_buf_u8(out, px[2]); /* B */
                xx_image_buf_u8(out, px[1]); /* G */
                xx_image_buf_u8(out, px[0]); /* R */
                xx_image_buf_u8(out, px[3]); /* A */
            } else {
                /* Composite onto black */
                uint8_t a = px[3];
                xx_image_buf_u8(out, (uint8_t)((unsigned int)px[2] * a / 255U));
                xx_image_buf_u8(out, (uint8_t)((unsigned int)px[1] * a / 255U));
                xx_image_buf_u8(out, (uint8_t)((unsigned int)px[0] * a / 255U));
            }
        }
    } else {
        /* RLE packet compression */
        size_t i = 0;
        while (i < total_pixels) {
            uint8_t p0[4];
            const uint8_t *src0 = f->rgba + i * 4U;
            if (bpp == 32) {
                p0[0] = src0[2]; p0[1] = src0[1]; p0[2] = src0[0]; p0[3] = src0[3];
            } else {
                uint8_t a = src0[3];
                p0[0] = (uint8_t)((unsigned int)src0[2] * a / 255U);
                p0[1] = (uint8_t)((unsigned int)src0[1] * a / 255U);
                p0[2] = (uint8_t)((unsigned int)src0[0] * a / 255U);
            }

            /* Look ahead for run */
            size_t run = 1;
            while (i + run < total_pixels && run < 128) {
                const uint8_t *src = f->rgba + (i + run) * 4U;
                uint8_t pk[4];
                if (bpp == 32) {
                    pk[0] = src[2]; pk[1] = src[1]; pk[2] = src[0]; pk[3] = src[3];
                    if (memcmp(p0, pk, 4) != 0) break;
                } else {
                    uint8_t a = src[3];
                    pk[0] = (uint8_t)((unsigned int)src[2] * a / 255U);
                    pk[1] = (uint8_t)((unsigned int)src[1] * a / 255U);
                    pk[2] = (uint8_t)((unsigned int)src[0] * a / 255U);
                    if (memcmp(p0, pk, 3) != 0) break;
                }
                ++run;
            }

            if (run >= 2 || total_pixels - i < 2) {
                /* Run-length packet */
                xx_image_buf_u8(out, (uint8_t)(0x80U | (run - 1U)));
                xx_image_buf_append(out, p0, (size_t)bpp / 8U);
                i += run;
            } else {
                /* Raw packet */
                size_t lit = 0;
                while (i + lit < total_pixels && lit < 128) {
                    if (i + lit + 2 < total_pixels) {
                        /* Check if 3 consecutive identical pixels start */
                        const uint8_t *s1 = f->rgba + (i + lit) * 4U;
                        const uint8_t *s2 = f->rgba + (i + lit + 1) * 4U;
                        const uint8_t *s3 = f->rgba + (i + lit + 2) * 4U;
                        if (memcmp(s1, s2, 4) == 0 && memcmp(s1, s3, 4) == 0) break;
                    }
                    ++lit;
                }

                xx_image_buf_u8(out, (uint8_t)(lit - 1U));
                for (size_t k = 0; k < lit; ++k) {
                    const uint8_t *src = f->rgba + (i + k) * 4U;
                    if (bpp == 32) {
                        xx_image_buf_u8(out, src[2]);
                        xx_image_buf_u8(out, src[1]);
                        xx_image_buf_u8(out, src[0]);
                        xx_image_buf_u8(out, src[3]);
                    } else {
                        uint8_t a = src[3];
                        xx_image_buf_u8(out, (uint8_t)((unsigned int)src[2] * a / 255U));
                        xx_image_buf_u8(out, (uint8_t)((unsigned int)src[1] * a / 255U));
                        xx_image_buf_u8(out, (uint8_t)((unsigned int)src[0] * a / 255U));
                    }
                }
                i += lit;
            }
        }
    }

    /* TGA 2.0 footer (26 bytes) */
    xx_image_buf_u32le(out, 0); /* Extension area offset */
    xx_image_buf_u32le(out, 0); /* Developer directory offset */
    xx_image_buf_append(out, g_tga_signature, 17);
    xx_image_buf_u8(out, 0);

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_tga = {
    "tga",
    "Truevision TGA",
    "tga;icb;vda;vst",
    "image/x-tga",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_INFO,
    642 /* XX_FILE_TYPE_TGA */,
    tga_probe,
    tga_load,
    tga_save
};
