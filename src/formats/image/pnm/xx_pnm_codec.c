/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Netpbm: PBM (P1/P4), PGM (P2/P5), PPM (P3/P6), PAM (P7), 8 and 16 bit
 * samples, any maxval up to 65535, several images per file (each one a frame).
 * Save: P4 for black and white, P5 for gray, P6 for colour, P7 when the frame
 * has alpha; options->ascii selects P1/P2/P3.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdlib.h>
#include <string.h>

static int pnm_probe(const uint8_t *d, size_t n)
{
    if (n < 3 || d[0] != 'P' || d[1] < '1' || d[1] > '7') return 0;
    if (d[2] != ' ' && d[2] != '\n' && d[2] != '\r' && d[2] != '\t' && d[2] != '#') return 0;
    return 60;
}

typedef struct pnm_reader {
    const uint8_t *d;
    size_t n, p;
} pnm_reader;

static void pnm_skip_ws(pnm_reader *r)
{
    while (r->p < r->n) {
        uint8_t c = r->d[r->p];
        if (c == '#') {
            while (r->p < r->n && r->d[r->p] != '\n' && r->d[r->p] != '\r') ++r->p;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f')
            ++r->p;
        else
            break;
    }
}

static bool pnm_uint(pnm_reader *r, uint32_t *v)
{
    uint64_t x = 0;
    size_t start;
    pnm_skip_ws(r);
    start = r->p;
    while (r->p < r->n && r->d[r->p] >= '0' && r->d[r->p] <= '9') {
        x = x * 10 + (r->d[r->p] - '0');
        if (x > 0xFFFFFFFFULL) return false;
        ++r->p;
    }
    *v = (uint32_t)x;
    return r->p > start;
}

static bool pnm_token(pnm_reader *r, char *buf, size_t cap)
{
    size_t k = 0;
    pnm_skip_ws(r);
    while (r->p < r->n && r->d[r->p] > ' ') {
        if (k + 1 < cap) buf[k++] = (char)r->d[r->p];
        ++r->p;
    }
    buf[k] = 0;
    return k > 0;
}

static uint8_t pnm_scale(uint32_t v, uint32_t maxval)
{
    if (v > maxval) v = maxval;
    return (uint8_t)((v * 255U + maxval / 2) / maxval);
}

static xx_image_status pnm_load_one(pnm_reader *r, const xx_image_load_options *o, xx_image *img, bool first)
{
    char type;
    uint32_t w = 0, h = 0, maxval = 1, depth = 0;
    char tupl[32] = "";
    xx_image_frame *f;
    xx_image_status st;
    bool ascii, bits;
    if (!xx_image_in_range(r->n, r->p, 2) || r->d[r->p] != 'P') return XX_IMAGE_ERR_FORMAT;
    type = (char)r->d[r->p + 1];
    if (type < '1' || type > '7') return XX_IMAGE_ERR_FORMAT;
    r->p += 2;
    if (type == '7') {
        char tok[32];
        for (;;) {
            if (!pnm_token(r, tok, sizeof(tok))) return XX_IMAGE_ERR_CORRUPT;
            if (strcmp(tok, "ENDHDR") == 0) break;
            if (strcmp(tok, "WIDTH") == 0) {
                if (!pnm_uint(r, &w)) return XX_IMAGE_ERR_CORRUPT;
            } else if (strcmp(tok, "HEIGHT") == 0) {
                if (!pnm_uint(r, &h)) return XX_IMAGE_ERR_CORRUPT;
            } else if (strcmp(tok, "DEPTH") == 0) {
                if (!pnm_uint(r, &depth)) return XX_IMAGE_ERR_CORRUPT;
            } else if (strcmp(tok, "MAXVAL") == 0) {
                if (!pnm_uint(r, &maxval)) return XX_IMAGE_ERR_CORRUPT;
            } else if (strcmp(tok, "TUPLTYPE") == 0) {
                if (!pnm_token(r, tupl, sizeof(tupl))) return XX_IMAGE_ERR_CORRUPT;
            } else {
                /* unknown header line: skip to end of line */
                while (r->p < r->n && r->d[r->p] != '\n') ++r->p;
            }
        }
        if (r->p < r->n && (r->d[r->p] == '\n' || r->d[r->p] == '\r')) {
            if (r->d[r->p] == '\r' && r->p + 1 < r->n && r->d[r->p + 1] == '\n') ++r->p;
            ++r->p;
        }
        if (depth < 1 || depth > 4) return XX_IMAGE_ERR_UNSUPPORTED;
    } else {
        if (!pnm_uint(r, &w) || !pnm_uint(r, &h)) return XX_IMAGE_ERR_CORRUPT;
        if (type != '1' && type != '4' && !pnm_uint(r, &maxval)) return XX_IMAGE_ERR_CORRUPT;
        depth = (type == '3' || type == '6') ? 3 : 1;
        if (type >= '4') {
            if (r->p >= r->n) return XX_IMAGE_ERR_CORRUPT;
            ++r->p; /* exactly one whitespace byte before raster data */
        }
    }
    if (maxval < 1 || maxval > 65535) return XX_IMAGE_ERR_CORRUPT;
    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;
    ascii = type <= '3';
    bits = type == '1' || type == '4' || (type == '7' && strcmp(tupl, "BLACKANDWHITE") == 0);
    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;
    if (type == '4') {
        size_t stride = ((size_t)w + 7) / 8;
        if (!xx_image_in_range(r->n, r->p, stride * h)) return XX_IMAGE_ERR_CORRUPT;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t v = (r->d[r->p + y * stride + x / 8] >> (7 - x % 8)) & 1 ? 0 : 255;
                uint8_t *px = f->rgba + ((size_t)y * w + x) * 4;
                px[0] = px[1] = px[2] = v;
                px[3] = 255;
            }
        r->p += stride * h;
    } else {
        size_t bps = maxval > 255 ? 2 : 1;
        if (!ascii && !xx_image_in_range(r->n, r->p, (size_t)w * h * depth * bps)) return XX_IMAGE_ERR_CORRUPT;
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            uint32_t s[4] = {0, 0, 0, 0};
            uint8_t *px = f->rgba + i * 4;
            for (uint32_t c = 0; c < depth; ++c) {
                if (ascii) {
                    if (type == '1') {
                        /* PBM ASCII digits need no separators */
                        pnm_skip_ws(r);
                        if (r->p >= r->n || (r->d[r->p] != '0' && r->d[r->p] != '1')) return XX_IMAGE_ERR_CORRUPT;
                        s[c] = r->d[r->p++] - '0';
                    } else if (!pnm_uint(r, &s[c]))
                        return XX_IMAGE_ERR_CORRUPT;
                } else if (bps == 2) {
                    s[c] = xx_image_rd_u16be(r->d + r->p);
                    r->p += 2;
                } else
                    s[c] = r->d[r->p++];
            }
            if (type == '1') {
                px[0] = px[1] = px[2] = s[0] ? 0 : 255;
                px[3] = 255;
            } else if (bits) {
                /* PAM BLACKANDWHITE: 1 = white */
                px[0] = px[1] = px[2] = s[0] ? 255 : 0;
                px[3] = depth > 1 ? (s[1] ? 255 : 0) : 255;
            } else if (depth <= 2) {
                px[0] = px[1] = px[2] = pnm_scale(s[0], maxval);
                px[3] = depth == 2 ? pnm_scale(s[1], maxval) : 255;
            } else {
                px[0] = pnm_scale(s[0], maxval);
                px[1] = pnm_scale(s[1], maxval);
                px[2] = pnm_scale(s[2], maxval);
                px[3] = depth == 4 ? pnm_scale(s[3], maxval) : 255;
            }
        }
    }
    if (first) {
        static const char *const names[7] = {"PBM ASCII", "PGM ASCII", "PPM ASCII", "PBM", "PGM", "PPM", "PAM"};
        xx_image_info *in = &img->info;
        XX_IMAGE_SET_STR(in->version, names[type - '1']);
        in->channels = depth;
        in->bits_per_sample = bits ? 1 : maxval > 255 ? 16 : 8;
        in->bits_per_pixel = in->bits_per_sample * depth;
        in->has_alpha = depth == 2 || depth == 4;
        in->color_model = bits ? XX_IMAGE_COLOR_BILEVEL : depth == 1 ? XX_IMAGE_COLOR_GRAY : depth == 2 ? XX_IMAGE_COLOR_GRAY_ALPHA : depth == 3 ? XX_IMAGE_COLOR_RGB : XX_IMAGE_COLOR_RGBA;
        XX_IMAGE_SET_STR(in->compression, "none");
        if (!o->skip_metadata) {
            xx_image_add_prop_text(img, "PNM", "magic", names[type - '1']);
            xx_image_add_prop_uint(img, "PNM", "width", w);
            xx_image_add_prop_uint(img, "PNM", "height", h);
            xx_image_add_prop_uint(img, "PNM", "maxval", maxval);
            if (type == '7') {
                xx_image_add_prop_uint(img, "PNM", "depth", depth);
                xx_image_add_prop_text(img, "PNM", "tuple type", tupl);
            }
        }
    }
    return XX_IMAGE_OK;
}

static xx_image_status pnm_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    pnm_reader r = {d, n, 0};
    xx_image_status st = pnm_load_one(&r, o, img, true);
    if (st != XX_IMAGE_OK) return st;
    while (!o->first_frame_only) {
        size_t save = r.p, before = img->frame_count;
        pnm_skip_ws(&r);
        if (r.p + 2 > n || d[r.p] != 'P') break;
        if (pnm_load_one(&r, o, img, false) != XX_IMAGE_OK) {
            /* a trailing image that does not parse is overlay: drop it */
            while (img->frame_count > before) {
                --img->frame_count;
                free(img->frames[img->frame_count].rgba);
                img->frames[img->frame_count].rgba = NULL;
            }
            r.p = save;
            break;
        }
    }
    img->info.frame_count = (uint32_t)img->frame_count;
    return XX_IMAGE_OK;
}

static xx_image_status pnm_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    bool alpha, gray, bw = true;
    uint32_t w, h;
    size_t n;
    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    n = (size_t)w * h;
    alpha = xx_image_frame_has_alpha(f);
    gray = xx_image_frame_is_gray(f);
    for (size_t i = 0; i < n && bw; ++i)
        if (f->rgba[i * 4] != 0 && f->rgba[i * 4] != 255) bw = false;
    bw = bw && gray && !alpha;
    if (alpha) {
        uint32_t depth = gray ? 2 : 4;
        xx_image_buf_printf(out, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH %u\nMAXVAL 255\nTUPLTYPE %s\nENDHDR\n", w, h, depth, gray ? "GRAYSCALE_ALPHA" : "RGB_ALPHA");
        for (size_t i = 0; i < n; ++i) {
            const uint8_t *p = f->rgba + i * 4;
            if (gray) {
                xx_image_buf_u8(out, p[0]);
                xx_image_buf_u8(out, p[3]);
            } else
                xx_image_buf_append(out, p, 4);
        }
        return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
    }
    if (bw) {
        xx_image_buf_printf(out, "P%c\n%u %u\n", o->ascii ? '1' : '4', w, h);
        for (uint32_t y = 0; y < h; ++y) {
            if (o->ascii) {
                for (uint32_t x = 0; x < w; ++x) {
                    xx_image_buf_u8(out, f->rgba[((size_t)y * w + x) * 4] ? '0' : '1');
                    if ((x + 1) % 70 == 0 || x + 1 == w) xx_image_buf_u8(out, '\n');
                }
            } else {
                for (uint32_t x = 0; x < w; x += 8) {
                    uint8_t b = 0;
                    for (uint32_t k = 0; k < 8 && x + k < w; ++k)
                        if (!f->rgba[((size_t)y * w + x + k) * 4]) b |= (uint8_t)(0x80 >> k);
                    xx_image_buf_u8(out, b);
                }
            }
        }
        return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
    }
    xx_image_buf_printf(out, "P%c\n%u %u\n255\n", gray ? (o->ascii ? '2' : '5') : (o->ascii ? '3' : '6'), w, h);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t *p = f->rgba + i * 4;
        if (o->ascii) {
            if (gray) xx_image_buf_printf(out, "%u%c", p[0], (i + 1) % w == 0 ? '\n' : ' ');
            else xx_image_buf_printf(out, "%u %u %u%c", p[0], p[1], p[2], (i + 1) % w == 0 ? '\n' : ' ');
        } else if (gray)
            xx_image_buf_u8(out, p[0]);
        else
            xx_image_buf_append(out, p, 3);
    }
    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_pnm = {"pnm", "Netpbm (PBM/PGM/PPM/PAM)", "pnm;ppm;pgm;pbm;pam", "image/x-portable-anymap",
                                     XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_MULTIFRAME | XX_IMAGE_CODEC_INFO,
                                     641 /* XX_FILE_TYPE_PNM */, pnm_probe, pnm_load, pnm_save};
