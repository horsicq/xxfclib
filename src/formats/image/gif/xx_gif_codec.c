/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compuserve Graphics Interchange Format (GIF87a / GIF89a).
 * Load: 87a / 89a, global and local color tables, LZW decoder (2..8 bits, deferred clear),
 * interlace, multi-frame animations (size, offset, delay, disposal, transparency),
 * NETSCAPE2.0 loop count, comments.
 * Save: 89a, quantised palette (<= 256 colours, reserved transparent index), LZW encoder,
 * single or multi-frame (NETSCAPE2.0 loop block).
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdlib.h>
#include <string.h>

#define GIF_HT_SIZE 8192U
#define GIF_HT_EMPTY 0xFFFFFFFFU

static int gif_probe(const uint8_t *d, size_t n)
{
    if (n >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0)) {
        return 100;
    }
    return 0;
}

static inline int gif_read_bits(const uint8_t *src, size_t len, size_t *bit_pos, int nbits)
{
    size_t byte_idx = *bit_pos >> 3;
    size_t bit_off = *bit_pos & 7U;
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (byte_idx + i < len) {
            v |= ((uint32_t)src[byte_idx + i] << (i * 8));
        }
    }
    *bit_pos += (size_t)nbits;
    return (int)((v >> bit_off) & ((1U << nbits) - 1U));
}

static xx_image_status gif_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint16_t screen_w, screen_h;
    uint8_t screen_packed, bg_idx;
    bool has_gct;
    uint32_t gct_size = 0;
    uint8_t gct[256][3];
    size_t p = 6;
    bool gce_valid = false;
    uint32_t gce_delay_ms = 0;
    uint32_t gce_dispose = 0;
    bool gce_has_trns = false;
    uint8_t gce_trns_idx = 0;
    bool has_any_alpha = false;

    if (n < 13) return XX_IMAGE_ERR_CORRUPT;
    if (gif_probe(d, n) == 0) return XX_IMAGE_ERR_FORMAT;

    screen_w = xx_image_rd_u16le(d + p);
    screen_h = xx_image_rd_u16le(d + p + 2);
    screen_packed = d[p + 4];
    bg_idx = d[p + 5];
    p += 7;

    img->width = screen_w;
    img->height = screen_h;

    has_gct = (screen_packed & 0x80U) != 0;
    if (has_gct) {
        gct_size = 1U << ((screen_packed & 7U) + 1U);
        if (!xx_image_in_range(n, p, (size_t)gct_size * 3U)) return XX_IMAGE_ERR_CORRUPT;
        for (uint32_t i = 0; i < gct_size; ++i) {
            gct[i][0] = d[p + i * 3U];
            gct[i][1] = d[p + i * 3U + 1];
            gct[i][2] = d[p + i * 3U + 2];
        }
        p += (size_t)gct_size * 3U;

        if (bg_idx < gct_size) {
            img->background = ((uint32_t)gct[bg_idx][0] << 24) |
                              ((uint32_t)gct[bg_idx][1] << 16) |
                              ((uint32_t)gct[bg_idx][2] << 8) | 0xFFU;
        }

        for (uint32_t i = 0; i < gct_size && i < 256; ++i) {
            img->palette[i] = ((uint32_t)gct[i][0] << 24) |
                              ((uint32_t)gct[i][1] << 16) |
                              ((uint32_t)gct[i][2] << 8) | 0xFFU;
        }
        img->palette_count = gct_size > 256 ? 256 : gct_size;
    }

    while (p < n) {
        uint8_t block_type = d[p++];
        if (block_type == 0x3B) {
            /* Trailer */
            break;
        } else if (block_type == 0x21) {
            /* Extension */
            if (p >= n) return XX_IMAGE_ERR_CORRUPT;
            uint8_t ext_fn = d[p++];
            if (ext_fn == 0xF9) {
                /* Graphic Control Extension */
                if (p >= n) return XX_IMAGE_ERR_CORRUPT;
                uint8_t bsize = d[p++];
                if (bsize != 4 || !xx_image_in_range(n, p, 5)) return XX_IMAGE_ERR_CORRUPT;
                uint8_t packed = d[p];
                uint16_t delay_val = xx_image_rd_u16le(d + p + 1);
                gce_trns_idx = d[p + 3];
                p += 5; /* 4 data + 1 terminator */

                gce_dispose = (packed >> 2U) & 7U;
                gce_delay_ms = (uint32_t)delay_val * 10U;
                gce_has_trns = (packed & 1U) != 0;
                gce_valid = true;
            } else if (ext_fn == 0xFF) {
                /* Application Extension */
                if (p >= n) return XX_IMAGE_ERR_CORRUPT;
                uint8_t bsize = d[p++];
                bool is_netscape = (bsize == 11 && xx_image_in_range(n, p, 11) &&
                                    (memcmp(d + p, "NETSCAPE2.0", 11) == 0 || memcmp(d + p, "ANIMEXTS1.0", 11) == 0));
                p += bsize;
                while (p < n && d[p] > 0) {
                    uint8_t sub_len = d[p++];
                    if (!xx_image_in_range(n, p, sub_len)) return XX_IMAGE_ERR_CORRUPT;
                    if (is_netscape && sub_len == 3 && d[p] == 1) {
                        img->loop_count = xx_image_rd_u16le(d + p + 1);
                    }
                    p += sub_len;
                }
                if (p < n && d[p] == 0) ++p;
            } else if (ext_fn == 0xFE) {
                /* Comment Extension */
                xx_image_buffer cbuf;
                memset(&cbuf, 0, sizeof(cbuf));
                while (p < n && d[p] > 0) {
                    uint8_t sub_len = d[p++];
                    if (!xx_image_in_range(n, p, sub_len)) {
                        xx_image_buf_free(&cbuf);
                        return XX_IMAGE_ERR_CORRUPT;
                    }
                    xx_image_buf_append(&cbuf, d + p, sub_len);
                    p += sub_len;
                }
                if (p < n && d[p] == 0) ++p;
                if (cbuf.size > 0 && !o->skip_metadata) {
                    xx_image_add_prop_textn(img, "comment", "comment", (const char *)cbuf.data, cbuf.size);
                }
                xx_image_buf_free(&cbuf);
            } else {
                /* Skip other extensions */
                while (p < n && d[p] > 0) {
                    uint8_t sub_len = d[p++];
                    if (!xx_image_in_range(n, p, sub_len)) return XX_IMAGE_ERR_CORRUPT;
                    p += sub_len;
                }
                if (p < n && d[p] == 0) ++p;
            }
        } else if (block_type == 0x2C) {
            /* Image Descriptor */
            uint16_t left, top, fw, fh;
            uint8_t img_packed, min_code_size;
            bool has_lct, interlaced;
            uint32_t lct_size = 0;
            uint8_t lct[256][3];
            xx_image_status st;
            xx_image_frame *f;
            size_t total_px;
            uint8_t *lzw_buf;
            size_t lzw_len = 0;
            uint8_t *indices;

            if (!xx_image_in_range(n, p, 9)) return XX_IMAGE_ERR_CORRUPT;
            left = xx_image_rd_u16le(d + p);
            top = xx_image_rd_u16le(d + p + 2);
            fw = xx_image_rd_u16le(d + p + 4);
            fh = xx_image_rd_u16le(d + p + 6);
            img_packed = d[p + 8];
            p += 9;

            has_lct = (img_packed & 0x80U) != 0;
            interlaced = (img_packed & 0x40U) != 0;
            if (has_lct) {
                lct_size = 1U << ((img_packed & 7U) + 1U);
                if (!xx_image_in_range(n, p, (size_t)lct_size * 3U)) return XX_IMAGE_ERR_CORRUPT;
                for (uint32_t i = 0; i < lct_size; ++i) {
                    lct[i][0] = d[p + i * 3U];
                    lct[i][1] = d[p + i * 3U + 1];
                    lct[i][2] = d[p + i * 3U + 2];
                }
                p += (size_t)lct_size * 3U;
            }

            st = xx_image_check_dimensions(fw, fh, o);
            if (st != XX_IMAGE_OK) return st;

            if (p >= n) return XX_IMAGE_ERR_CORRUPT;
            min_code_size = d[p++];
            if (min_code_size < 2 || min_code_size > 8) return XX_IMAGE_ERR_CORRUPT;

            /* Gather sub-block data */
            lzw_buf = (uint8_t *)malloc(n - p + 1);
            if (!lzw_buf) return XX_IMAGE_ERR_MEMORY;
            while (p < n && d[p] > 0) {
                uint8_t sub_len = d[p++];
                if (!xx_image_in_range(n, p, sub_len)) {
                    free(lzw_buf);
                    return XX_IMAGE_ERR_CORRUPT;
                }
                memcpy(lzw_buf + lzw_len, d + p, sub_len);
                lzw_len += sub_len;
                p += sub_len;
            }
            if (p < n && d[p] == 0) ++p;

            total_px = (size_t)fw * fh;
            indices = (uint8_t *)malloc(total_px);
            if (!indices) {
                free(lzw_buf);
                return XX_IMAGE_ERR_MEMORY;
            }

            /* LZW decompression */
            {
                int clear_code = 1 << min_code_size;
                int end_code = clear_code + 1;
                int code_size = min_code_size + 1;
                int max_code = 1 << code_size;
                int next_code = end_code + 1;
                uint16_t prefix[4096];
                uint8_t suffix[4096];
                uint8_t stack[4097];
                size_t bit_pos = 0;
                size_t out_idx = 0;
                int old_code = -1;
                int first_char = 0;

                for (int i = 0; i < clear_code; ++i) {
                    prefix[i] = 4096;
                    suffix[i] = (uint8_t)i;
                }

                while (out_idx < total_px && (bit_pos + (size_t)code_size <= (lzw_len * 8U + 8U))) {
                    int code = gif_read_bits(lzw_buf, lzw_len, &bit_pos, code_size);
                    if (code == clear_code) {
                        code_size = min_code_size + 1;
                        max_code = 1 << code_size;
                        next_code = end_code + 1;
                        old_code = -1;
                        continue;
                    }
                    if (code == end_code) break;

                    if (old_code == -1) {
                        if (code >= next_code) break;
                        first_char = suffix[code];
                        indices[out_idx++] = (uint8_t)first_char;
                        old_code = code;
                        continue;
                    }

                    int in_code = code;
                    int sp = 0;

                    if (code >= next_code) {
                        stack[sp++] = (uint8_t)first_char;
                        in_code = old_code;
                    }

                    while (in_code >= clear_code && in_code < 4096) {
                        stack[sp++] = suffix[in_code];
                        in_code = prefix[in_code];
                    }

                    if (in_code < clear_code) {
                        first_char = suffix[in_code];
                        stack[sp++] = (uint8_t)first_char;
                    }

                    while (sp > 0 && out_idx < total_px) {
                        indices[out_idx++] = stack[--sp];
                    }

                    if (next_code < 4096) {
                        prefix[next_code] = (uint16_t)old_code;
                        suffix[next_code] = (uint8_t)first_char;
                        ++next_code;
                        if (next_code >= max_code && code_size < 12) {
                            ++code_size;
                            max_code = 1 << code_size;
                        }
                    }
                    old_code = code;
                }

                /* Pad if truncated */
                while (out_idx < total_px) {
                    indices[out_idx++] = 0;
                }
            }

            free(lzw_buf);

            /* Interlace pass reordering */
            if (interlaced) {
                uint8_t *ordered = (uint8_t *)malloc(total_px);
                if (ordered) {
                    static const int pass_start[4] = {0, 4, 2, 1};
                    static const int pass_step[4] = {8, 8, 4, 2};
                    size_t src_i = 0;
                    for (int pass = 0; pass < 4; ++pass) {
                        for (uint32_t row = (uint32_t)pass_start[pass]; row < fh; row += (uint32_t)pass_step[pass]) {
                            for (uint32_t col = 0; col < fw && src_i < total_px; ++col) {
                                ordered[(size_t)row * fw + col] = indices[src_i++];
                            }
                        }
                    }
                    memcpy(indices, ordered, total_px);
                    free(ordered);
                }
            }

            f = xx_image_add_frame(img, fw, fh);
            if (!f) {
                free(indices);
                return XX_IMAGE_ERR_MEMORY;
            }

            f->x = (int32_t)left;
            f->y = (int32_t)top;
            f->delay_ms = gce_valid ? gce_delay_ms : 0;
            f->dispose = gce_valid ? gce_dispose : 0;

            {
                const uint8_t (*active_pal)[3] = has_lct ? (const uint8_t (*)[3])lct : (const uint8_t (*)[3])gct;
                uint32_t active_pal_size = has_lct ? lct_size : gct_size;

                for (size_t i = 0; i < total_px; ++i) {
                    uint8_t idx = indices[i];
                    uint8_t *px = f->rgba + i * 4U;
                    if (gce_valid && gce_has_trns && idx == gce_trns_idx) {
                        px[0] = px[1] = px[2] = px[3] = 0;
                        has_any_alpha = true;
                    } else {
                        if (idx < active_pal_size) {
                            px[0] = active_pal[idx][0];
                            px[1] = active_pal[idx][1];
                            px[2] = active_pal[idx][2];
                        } else {
                            px[0] = px[1] = px[2] = 0;
                        }
                        px[3] = 255;
                    }
                }
            }

            free(indices);
            gce_valid = false;

            if (o->first_frame_only) {
                break;
            }
        } else {
            /* Unknown block: break or advance */
            break;
        }
    }

    if (img->frame_count == 0) return XX_IMAGE_ERR_CORRUPT;

    img->info.color_model = XX_IMAGE_COLOR_INDEXED;
    img->info.bits_per_pixel = 8;
    img->info.bits_per_sample = 8;
    img->info.channels = 4;
    img->info.has_alpha = has_any_alpha;
    img->info.is_animated = (img->frame_count > 1);
    img->info.frame_count = (uint32_t)img->frame_count;
    XX_IMAGE_SET_STR(img->info.compression, "LZW");

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, "GIF", "width", img->width);
        xx_image_add_prop_uint(img, "GIF", "height", img->height);
        xx_image_add_prop_uint(img, "GIF", "frames", img->frame_count);
        xx_image_add_prop_uint(img, "GIF", "loop count", img->loop_count);
    }

    return XX_IMAGE_OK;
}

typedef struct gif_bit_writer {
    xx_image_buffer *out;
    uint32_t accum;
    int nbits;
    uint8_t sub_block[256];
    int sub_len;
} gif_bit_writer;

static inline void gif_bw_init(gif_bit_writer *bw, xx_image_buffer *out)
{
    bw->out = out;
    bw->accum = 0;
    bw->nbits = 0;
    bw->sub_len = 0;
}

static inline void gif_bw_flush_sub(gif_bit_writer *bw)
{
    if (bw->sub_len > 0) {
        bw->sub_block[0] = (uint8_t)bw->sub_len;
        xx_image_buf_append(bw->out, bw->sub_block, (size_t)bw->sub_len + 1U);
        bw->sub_len = 0;
    }
}

static inline void gif_bw_emit(gif_bit_writer *bw, int code, int code_size)
{
    bw->accum |= ((uint32_t)code << bw->nbits);
    bw->nbits += code_size;
    while (bw->nbits >= 8) {
        bw->sub_block[1 + bw->sub_len++] = (uint8_t)(bw->accum & 0xFFU);
        bw->accum >>= 8;
        bw->nbits -= 8;
        if (bw->sub_len == 254) {
            gif_bw_flush_sub(bw);
        }
    }
}

static inline void gif_bw_finish(gif_bit_writer *bw)
{
    if (bw->nbits > 0) {
        bw->sub_block[1 + bw->sub_len++] = (uint8_t)(bw->accum & 0xFFU);
        bw->accum = 0;
        bw->nbits = 0;
    }
    gif_bw_flush_sub(bw);
    xx_image_buf_u8(bw->out, 0); /* 0-byte terminator block */
}

static void gif_lzw_encode(const uint8_t *indices, size_t total_px, xx_image_buffer *out)
{
    gif_bit_writer bw;
    int clear_code = 256;
    int end_code = 257;
    int next_code = 258;
    int code_size = 9;
    int max_code = 512;
    uint32_t *ht_key;
    uint16_t *ht_code;

    ht_key = (uint32_t *)malloc(GIF_HT_SIZE * sizeof(uint32_t));
    ht_code = (uint16_t *)malloc(GIF_HT_SIZE * sizeof(uint16_t));
    if (!ht_key || !ht_code) {
        free(ht_key);
        free(ht_code);
        return;
    }

    memset(ht_key, 0xFF, GIF_HT_SIZE * sizeof(uint32_t));
    gif_bw_init(&bw, out);
    gif_bw_emit(&bw, clear_code, code_size);

    if (total_px > 0) {
        int prefix = indices[0];
        for (size_t i = 1; i < total_px; ++i) {
            uint8_t c = indices[i];
            uint32_t key = ((uint32_t)prefix << 8U) | (uint32_t)c;
            uint32_t slot = (key * 2654435761U) & (GIF_HT_SIZE - 1U);
            bool found = false;

            while (ht_key[slot] != GIF_HT_EMPTY) {
                if (ht_key[slot] == key) {
                    prefix = ht_code[slot];
                    found = true;
                    break;
                }
                slot = (slot + 1U) & (GIF_HT_SIZE - 1U);
            }

            if (!found) {
                gif_bw_emit(&bw, prefix, code_size);

                if (next_code < 4096) {
                    ht_key[slot] = key;
                    ht_code[slot] = (uint16_t)next_code;
                    ++next_code;
                    if (next_code > max_code && code_size < 12) {
                        ++code_size;
                        max_code = 1 << code_size;
                    }
                } else {
                    gif_bw_emit(&bw, clear_code, code_size);
                    memset(ht_key, 0xFF, GIF_HT_SIZE * sizeof(uint32_t));
                    code_size = 9;
                    max_code = 512;
                    next_code = 258;
                }
                prefix = c;
            }
        }
        gif_bw_emit(&bw, prefix, code_size);
    }

    gif_bw_emit(&bw, end_code, code_size);
    gif_bw_finish(&bw);

    free(ht_key);
    free(ht_code);
}

static xx_image_status gif_save_frame_internal(const xx_image_frame *f, xx_image_buffer *out)
{
    uint32_t w = f->width;
    uint32_t h = f->height;
    size_t total_px = (size_t)w * h;
    bool has_alpha = false;
    uint8_t *indices;
    uint8_t lct[256][3];
    uint32_t q_pal[256];

    memset(lct, 0, sizeof(lct));
    indices = (uint8_t *)malloc(total_px);
    if (!indices) return XX_IMAGE_ERR_MEMORY;

    for (size_t i = 0; i < total_px; ++i) {
        if (f->rgba[i * 4U + 3U] < 128U) {
            has_alpha = true;
            break;
        }
    }

    if (has_alpha) {
        uint8_t *q_idx = (uint8_t *)malloc(total_px);
        if (!q_idx) {
            free(indices);
            return XX_IMAGE_ERR_MEMORY;
        }
        uint32_t cnt = xx_image_quantize(f, 255, q_idx, q_pal);
        if (cnt == 0) {
            free(q_idx);
            free(indices);
            return XX_IMAGE_ERR_MEMORY;
        }
        for (uint32_t k = 0; k < cnt; ++k) {
            lct[k + 1][0] = (uint8_t)(q_pal[k] >> 24);
            lct[k + 1][1] = (uint8_t)(q_pal[k] >> 16);
            lct[k + 1][2] = (uint8_t)(q_pal[k] >> 8);
        }
        for (size_t i = 0; i < total_px; ++i) {
            if (f->rgba[i * 4U + 3U] < 128U) {
                indices[i] = 0;
            } else {
                indices[i] = (uint8_t)(q_idx[i] + 1U);
            }
        }
        free(q_idx);

        /* Graphic Control Extension: disposal 2 (background), transparent flag 1, index 0 */
        uint16_t delay_val = (uint16_t)(f->delay_ms / 10U);
        xx_image_buf_u8(out, 0x21);
        xx_image_buf_u8(out, 0xF9);
        xx_image_buf_u8(out, 4);
        xx_image_buf_u8(out, (uint8_t)((2U << 2U) | 1U));
        xx_image_buf_u16le(out, delay_val);
        xx_image_buf_u8(out, 0); /* transparent index */
        xx_image_buf_u8(out, 0);
    } else {
        uint32_t cnt = xx_image_quantize(f, 256, indices, q_pal);
        if (cnt == 0) {
            free(indices);
            return XX_IMAGE_ERR_MEMORY;
        }
        for (uint32_t k = 0; k < cnt; ++k) {
            lct[k][0] = (uint8_t)(q_pal[k] >> 24);
            lct[k][1] = (uint8_t)(q_pal[k] >> 16);
            lct[k][2] = (uint8_t)(q_pal[k] >> 8);
        }

        uint16_t delay_val = (uint16_t)(f->delay_ms / 10U);
        uint8_t disp = (uint8_t)((f->dispose & 7U) << 2U);
        xx_image_buf_u8(out, 0x21);
        xx_image_buf_u8(out, 0xF9);
        xx_image_buf_u8(out, 4);
        xx_image_buf_u8(out, disp);
        xx_image_buf_u16le(out, delay_val);
        xx_image_buf_u8(out, 0);
        xx_image_buf_u8(out, 0);
    }

    /* Image Descriptor */
    xx_image_buf_u8(out, 0x2C);
    xx_image_buf_u16le(out, (uint16_t)(f->x < 0 ? 0 : f->x));
    xx_image_buf_u16le(out, (uint16_t)(f->y < 0 ? 0 : f->y));
    xx_image_buf_u16le(out, (uint16_t)w);
    xx_image_buf_u16le(out, (uint16_t)h);
    xx_image_buf_u8(out, 0x87); /* Local color table present, 256 entries */

    /* Local Color Table (256 * 3 bytes) */
    for (int k = 0; k < 256; ++k) {
        xx_image_buf_u8(out, lct[k][0]);
        xx_image_buf_u8(out, lct[k][1]);
        xx_image_buf_u8(out, lct[k][2]);
    }

    /* Minimum LZW code size = 8 */
    xx_image_buf_u8(out, 8);

    /* LZW compressed stream */
    gif_lzw_encode(indices, total_px, out);

    free(indices);
    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

static xx_image_status gif_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    bool all_frames = (o && o->all_frames && img->frame_count > 1);
    uint32_t screen_w = img->width ? img->width : img->frames[0].width;
    uint32_t screen_h = img->height ? img->height : img->frames[0].height;

    if (!img->frame_count) return XX_IMAGE_ERR_ARGUMENT;
    if (screen_w > 0xFFFFU || screen_h > 0xFFFFU) return XX_IMAGE_ERR_LIMIT;

    /* Header */
    xx_image_buf_append(out, "GIF89a", 6);
    xx_image_buf_u16le(out, (uint16_t)screen_w);
    xx_image_buf_u16le(out, (uint16_t)screen_h);
    xx_image_buf_u8(out, 0x70); /* No Global Color Table, 8 bits/pixel */
    xx_image_buf_u8(out, 0);    /* Background color index */
    xx_image_buf_u8(out, 0);    /* Aspect ratio */

    if (all_frames) {
        /* NETSCAPE2.0 application extension for looping */
        uint16_t loop = (uint16_t)img->loop_count;
        xx_image_buf_u8(out, 0x21);
        xx_image_buf_u8(out, 0xFF);
        xx_image_buf_u8(out, 11);
        xx_image_buf_append(out, "NETSCAPE2.0", 11);
        xx_image_buf_u8(out, 3);
        xx_image_buf_u8(out, 1);
        xx_image_buf_u16le(out, loop);
        xx_image_buf_u8(out, 0);

        for (size_t i = 0; i < img->frame_count; ++i) {
            xx_image_status st = gif_save_frame_internal(&img->frames[i], out);
            if (st != XX_IMAGE_OK) return st;
        }
    } else {
        const xx_image_frame *f = xx_image_save_frame(img, o);
        xx_image_status st;
        if (!f) return XX_IMAGE_ERR_ARGUMENT;
        st = gif_save_frame_internal(f, out);
        if (st != XX_IMAGE_OK) return st;
    }

    /* Trailer */
    xx_image_buf_u8(out, 0x3B);

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_gif = {
    "gif",
    "Graphics Interchange Format",
    "gif",
    "image/gif",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_MULTIFRAME | XX_IMAGE_CODEC_INDEXED | XX_IMAGE_CODEC_INFO,
    529 /* XX_FILE_TYPE_GIF */,
    gif_probe,
    gif_load,
    gif_save
};
