/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JPEG (ISO/IEC 10918-1 / ITU-T T.81).
 * Load: baseline (SOF0), extended sequential (SOF1) and progressive (SOF2);
 * 1, 3 or 4 components; any sampling factors 1..4; DRI restart intervals;
 * JFIF, Adobe APP14, Exif TIFF metadata, ICC profile chunks; YCbCr, RGB,
 * CMYK and YCCK colour conversion with fancy triangle upsampling.
 * Save: baseline sequential (SOF0), JFIF APP0, 4:2:0 (< 90 quality) or
 * 4:4:4 (>= 90 quality), grayscale, Annex K quantization and Huffman tables,
 * alpha composited onto black.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Constants and Tables ----------------------------------------------- */

static const uint8_t g_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static const double g_idct_mat[8][8] = {
    {  0.35355339059327373,   0.49039264020161522,   0.46193976625564337,   0.41573480615127262,   0.35355339059327379,   0.27778511650980114,   0.19134171618254492,   0.09754516100806417},
    {  0.35355339059327373,   0.41573480615127262,   0.19134171618254492,  -0.09754516100806410,  -0.35355339059327373,  -0.49039264020161522,  -0.46193976625564342,  -0.27778511650980109},
    {  0.35355339059327373,   0.27778511650980114,  -0.19134171618254486,  -0.49039264020161522,  -0.35355339059327384,   0.09754516100806415,   0.46193976625564326,   0.41573480615127278},
    {  0.35355339059327373,   0.09754516100806417,  -0.46193976625564337,  -0.27778511650980109,   0.35355339059327368,   0.41573480615127278,  -0.19134171618254495,  -0.49039264020161533},
    {  0.35355339059327373,  -0.09754516100806410,  -0.46193976625564342,   0.27778511650980092,   0.35355339059327384,  -0.41573480615127256,  -0.19134171618254528,   0.49039264020161527},
    {  0.35355339059327373,  -0.27778511650980098,  -0.19134171618254517,   0.49039264020161522,  -0.35355339059327334,  -0.09754516100806401,   0.46193976625564337,  -0.41573480615127251},
    {  0.35355339059327373,  -0.41573480615127267,   0.19134171618254500,   0.09754516100806439,  -0.35355339059327356,   0.49039264020161533,  -0.46193976625564320,   0.27778511650980076},
    {  0.35355339059327373,  -0.49039264020161522,   0.46193976625564326,  -0.41573480615127256,   0.35355339059327329,  -0.27778511650980076,   0.19134171618254478,  -0.09754516100806429}
};

/* Standard Annex K.1 luminance quantization table */
static const uint8_t g_std_dqt_lum[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
};

/* Standard Annex K.2 chrominance quantization table */
static const uint8_t g_std_dqt_chrom[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

/* Standard Annex K.3 Huffman tables for encoder and fallback */
static const uint8_t g_std_dc_lum_bits[17] = {0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t g_std_dc_lum_val[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t g_std_dc_chrom_bits[17] = {0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t g_std_dc_chrom_val[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t g_std_ac_lum_bits[17] = {0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125};
static const uint8_t g_std_ac_lum_val[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

static const uint8_t g_std_ac_chrom_bits[17] = {0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119};
static const uint8_t g_std_ac_chrom_val[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
    0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
    0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

/* Encoder code tables, entry = (code << 8) | length, built once per save
 * from the canonical Annex K lists above (C.2: codes in order of length). */
static void jpeg_build_enc_table(const uint8_t bits[17], const uint8_t *vals, uint32_t out[256])
{
    uint32_t code = 0;
    size_t k = 0;
    memset(out, 0, 256 * sizeof(uint32_t));
    for (int len = 1; len <= 16; ++len) {
        for (int i = 0; i < bits[len]; ++i, ++k, ++code) out[vals[k]] = (code << 8) | (uint32_t)len;
        code <<= 1;
    }
}

static inline uint8_t clamp8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* ---- Bitstream and Huffman Decoding -------------------------------------- */

typedef struct {
    uint16_t fast[512];    /**< (len << 8) | val; 0 if len > 9 or invalid */
    int32_t maxcode[18];   /**< max code of length l; -1 if none */
    int32_t mincode[18];   /**< min code of length l */
    int32_t valptr[18];    /**< offset into values for length l */
    uint8_t values[256];
    uint8_t bits[17];
    bool valid;
} jpeg_huff_table;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bit_buf;
    int bits_left;
    int pad_bits;          /**< zero bits appended after the real data (capped) */
    uint8_t marker;        /**< non-zero if marker was encountered */
    bool truncated;
} jpeg_bits;

static xx_image_status jpeg_build_huff_table(jpeg_huff_table *ht, const uint8_t *bits, const uint8_t *values)
{
    int count = 0;
    int p = 0;
    int code = 0;
    uint16_t huffcode[256];
    uint8_t huffsize[256];

    memset(ht, 0, sizeof(*ht));
    memcpy(ht->bits, bits, 17);

    for (int l = 1; l <= 16; ++l) count += bits[l];
    if (count > 256) return XX_IMAGE_ERR_CORRUPT;
    memcpy(ht->values, values, count);

    for (int l = 1; l <= 16; ++l) {
        if (code + bits[l] > (1 << l)) return XX_IMAGE_ERR_CORRUPT;
        ht->valptr[l] = p;
        if (bits[l]) {
            ht->mincode[l] = code;
            ht->maxcode[l] = code + bits[l] - 1;
            for (int i = 0; i < bits[l]; ++i) {
                huffsize[p] = (uint8_t)l;
                huffcode[p] = (uint16_t)(code + i);
                ++p;
            }
            code += bits[l];
        } else {
            ht->mincode[l] = 0;
            ht->maxcode[l] = -1;
        }
        code <<= 1;
    }
    ht->valptr[17] = 0;
    ht->maxcode[17] = -1;

    for (int i = 0; i < count; ++i) {
        int l = huffsize[i];
        if (l <= 9) {
            int c = huffcode[i] << (9 - l);
            int num_entries = 1 << (9 - l);
            uint16_t entry = (uint16_t)((l << 8) | ht->values[i]);
            for (int j = 0; j < num_entries; ++j) {
                ht->fast[c + j] = entry;
            }
        }
    }
    ht->valid = true;
    return XX_IMAGE_OK;
}

static void jpeg_bits_init(jpeg_bits *b, const uint8_t *data, size_t size, size_t start_pos)
{
    b->data = data;
    b->size = size;
    b->pos = start_pos;
    b->bit_buf = 0;
    b->bits_left = 0;
    b->pad_bits = 0;
    b->marker = 0;
    b->truncated = false;
}

/* Appends one zero byte after the end of the entropy data (marker or EOF). */
static inline void jpeg_bits_pad(jpeg_bits *b)
{
    b->bit_buf <<= 8;
    b->bits_left += 8;
    if (b->pad_bits < 64) b->pad_bits += 8;
}

/* True once bits that are not in the stream have been consumed. */
static inline bool jpeg_bits_overrun(const jpeg_bits *b)
{
    return b->bits_left < b->pad_bits;
}

static void jpeg_bits_refill(jpeg_bits *b)
{
    while (b->bits_left <= 24) {
        if (b->marker != 0) {
            jpeg_bits_pad(b);
            continue;
        }
        if (b->pos >= b->size) {
            b->truncated = true;
            jpeg_bits_pad(b);
            continue;
        }
        uint8_t c = b->data[b->pos++];
        if (c == 0xFF) {
            while (b->pos < b->size && b->data[b->pos] == 0xFF) ++b->pos;
            if (b->pos >= b->size) {
                b->truncated = true;
                jpeg_bits_pad(b);
                continue;
            }
            uint8_t m = b->data[b->pos++];
            if (m != 0x00) {
                b->marker = m;
                jpeg_bits_pad(b);
                continue;
            }
        }
        b->bit_buf = (b->bit_buf << 8) | c;
        b->bits_left += 8;
    }
}

static inline uint32_t jpeg_bits_get(jpeg_bits *b, int n)
{
    if (n <= 0) return 0;
    if (b->bits_left < n) jpeg_bits_refill(b);
    uint32_t res = (b->bit_buf >> (b->bits_left - n)) & ((1U << n) - 1);
    b->bits_left -= n;
    return res;
}

static inline int jpeg_bits_get1(jpeg_bits *b)
{
    if (b->bits_left < 1) jpeg_bits_refill(b);
    --b->bits_left;
    return (b->bit_buf >> b->bits_left) & 1;
}

static int jpeg_huff_decode(jpeg_bits *b, const jpeg_huff_table *ht)
{
    if (!ht->valid) return 0;
    if (b->bits_left < 9) jpeg_bits_refill(b);
    if (b->bits_left >= 9) {
        uint32_t peek = (b->bit_buf >> (b->bits_left - 9)) & 0x1FF;
        uint16_t entry = ht->fast[peek];
        if (entry != 0) {
            int len = entry >> 8;
            b->bits_left -= len;
            return entry & 0xFF;
        }
    }
    /* Slow path */
    int32_t code = 0;
    for (int l = 1; l <= 16; ++l) {
        if (b->bits_left < 1) jpeg_bits_refill(b);
        int bit = (b->bit_buf >> (b->bits_left - 1)) & 1;
        --b->bits_left;
        code = (code << 1) | bit;
        if (code <= ht->maxcode[l]) {
            int idx = ht->valptr[l] + (code - ht->mincode[l]);
            if (idx >= 0 && idx < 256) return ht->values[idx];
            return 0;
        }
    }
    return 0;
}

static inline int jpeg_read_signed(jpeg_bits *b, int s)
{
    if (s <= 0) return 0;
    if (s > 16) s = 16;
    int v = (int)jpeg_bits_get(b, s);
    if (v < (1 << (s - 1))) v -= (1 << s) - 1;
    return v;
}

/* Called where a restart marker is due: drops the rest of the interval and
 * consumes the RSTn marker.  Any other marker is left pending, so the rest
 * of the scan reads as overrun. */
static void jpeg_bits_restart(jpeg_bits *b)
{
    b->bits_left = 0;
    b->bit_buf = 0;
    b->pad_bits = 0;
    if (b->marker != 0) {
        if (b->marker >= 0xD0 && b->marker <= 0xD7) b->marker = 0;
        return;
    }
    while (b->pos < b->size) {
        if (b->data[b->pos++] != 0xFF) continue;
        while (b->pos < b->size && b->data[b->pos] == 0xFF) ++b->pos;
        if (b->pos >= b->size) break;
        uint8_t m = b->data[b->pos++];
        if (m == 0x00) continue;
        if (m < 0xD0 || m > 0xD7) b->marker = m;
        return;
    }
    b->truncated = true;
}

/* ---- IDCT & Forward DCT -------------------------------------------------- */

static inline uint8_t jpeg_sample(double v)
{
    v += 128.0;
    if (!(v > 0.0)) return 0;
    if (v >= 255.0) return 255;
    return (uint8_t)(v + 0.5);
}

/* Dequantises (both arrays in natural order) and inverse transforms. */
static void jpeg_idct_block(const int16_t in[64], const uint16_t qt[64], uint8_t *out, size_t out_stride)
{
    double C[64];
    double T[8][8];
    bool all_zero = true;
    for (int i = 0; i < 64; ++i) {
        C[i] = (double)in[i] * (double)qt[i];
        if (i > 0 && in[i] != 0) all_zero = false;
    }
    if (all_zero) {
        uint8_t c = jpeg_sample(C[0] / 8.0);
        for (int y = 0; y < 8; ++y) memset(out + (size_t)y * out_stride, c, 8);
        return;
    }
    for (int y = 0; y < 8; ++y) {
        for (int v = 0; v < 8; ++v) {
            double s = 0.0;
            for (int u = 0; u < 8; ++u) s += g_idct_mat[y][u] * C[u * 8 + v];
            T[y][v] = s;
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            double s = 0.0;
            for (int v = 0; v < 8; ++v) s += T[y][v] * g_idct_mat[x][v];
            out[(size_t)y * out_stride + x] = jpeg_sample(s);
        }
    }
}

static void jpeg_fdct_block(const uint8_t *in, size_t in_stride, int16_t out[64])
{
    double T[8][8];
    for (int u = 0; u < 8; ++u) {
        for (int x = 0; x < 8; ++x) {
            double s = 0.0;
            for (int y = 0; y < 8; ++y) {
                s += g_idct_mat[y][u] * (in[y * in_stride + x] - 128.0);
            }
            T[u][x] = s;
        }
    }
    for (int u = 0; u < 8; ++u) {
        for (int v = 0; v < 8; ++v) {
            double s = 0.0;
            for (int x = 0; x < 8; ++x) {
                s += T[u][x] * g_idct_mat[x][v];
            }
            out[u * 8 + v] = (int16_t)(s >= 0.0 ? (s + 0.5) : (s - 0.5));
        }
    }
}

/* ---- Upsampling & Colour Conversion -------------------------------------- */

/* Upsamples one component plane to the full image size.  A 2:1 ratio uses
 * the triangle filter of libjpeg's "fancy" upsampling (same weights and
 * rounding as libjpeg-turbo h2v1 / h2v2 / h1v2); other ratios replicate. */
static void jpeg_upsample_plane(const uint8_t *src, size_t src_stride, uint32_t src_w, uint32_t src_h, uint8_t *dst, uint32_t dst_w, uint32_t dst_h, uint8_t h_fact, uint8_t v_fact, uint8_t h_max, uint8_t v_max)
{
    bool h2 = (h_max == 2 * h_fact);
    bool v2 = (v_max == 2 * v_fact);
    if (src_w == 0 || src_h == 0) return;
    for (uint32_t y = 0; y < dst_h; ++y) {
        uint32_t r0, r1; /* near row, far row */
        if (v2) {
            r0 = y / 2;
            if (y & 1) r1 = r0 + 1;
            else r1 = r0 > 0 ? r0 - 1 : 0;
        } else {
            r0 = r1 = (uint32_t)(((uint64_t)y * v_fact) / v_max);
        }
        if (r0 >= src_h) r0 = src_h - 1;
        if (r1 >= src_h) r1 = src_h - 1;
        const uint8_t *s0 = src + (size_t)r0 * src_stride;
        const uint8_t *s1 = src + (size_t)r1 * src_stride;
        uint8_t *drow = dst + (size_t)y * dst_w;
        if (!h2 && !v2) {
            if (h_fact == h_max) {
                memcpy(drow, s0, dst_w);
            } else {
                for (uint32_t x = 0; x < dst_w; ++x) {
                    uint32_t sx = (uint32_t)(((uint64_t)x * h_fact) / h_max);
                    drow[x] = s0[sx < src_w ? sx : src_w - 1];
                }
            }
        } else if (!h2) {
            int bias = (y & 1) ? 2 : 1;
            for (uint32_t x = 0; x < dst_w; ++x) {
                uint32_t sx = (uint32_t)(((uint64_t)x * h_fact) / h_max);
                if (sx >= src_w) sx = src_w - 1;
                drow[x] = (uint8_t)((3 * s0[sx] + s1[sx] + bias) >> 2);
            }
        } else {
            for (uint32_t x = 0; x < dst_w; ++x) {
                uint32_t c0 = x / 2, c1; /* near column, far column */
                if (x & 1) c1 = c0 + 1;
                else c1 = c0 > 0 ? c0 - 1 : 0;
                if (c0 >= src_w) c0 = src_w - 1;
                if (c1 >= src_w) c1 = src_w - 1;
                if (v2) {
                    int near_sum = 3 * s0[c0] + s1[c0];
                    int far_sum = 3 * s0[c1] + s1[c1];
                    drow[x] = (uint8_t)((3 * near_sum + far_sum + ((x & 1) ? 7 : 8)) >> 4);
                } else {
                    drow[x] = (uint8_t)((3 * s0[c0] + s0[c1] + ((x & 1) ? 2 : 1)) >> 2);
                }
            }
        }
    }
}

/* Full-range BT.601 YCbCr -> RGB conversion */
static inline void jpeg_ycbcr_to_rgb(uint8_t y_val, uint8_t cb_val, uint8_t cr_val, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int y = y_val;
    int cb = (int)cb_val - 128;
    int cr = (int)cr_val - 128;

    int r_val = (int)(y + 1.40200 * cr + 0.5);
    int g_val = (int)(y - 0.344136 * cb - 0.714136 * cr + 0.5);
    int b_val = (int)(y + 1.77200 * cb + 0.5);

    *r = clamp8(r_val);
    *g = clamp8(g_val);
    *b = clamp8(b_val);
}

/* ---- Metadata Parsing (JFIF, Adobe, ICC, EXIF, COM) ---------------------- */

static void jpeg_parse_jfif(xx_image *img, const uint8_t *d, size_t len)
{
    if (len < 14) return;
    uint8_t maj = d[5], min = d[6], units = d[7];
    uint32_t xd = xx_image_rd_u16be(d + 8), yd = xx_image_rd_u16be(d + 10);
    char ver[16];
    snprintf(ver, sizeof(ver), "%u.%02u", maj, min);
    xx_image_add_prop_text(img, "JFIF", "version", ver);
    xx_image_add_prop_uint(img, "JFIF", "units", units);
    xx_image_add_prop_uint(img, "JFIF", "density x", xd);
    xx_image_add_prop_uint(img, "JFIF", "density y", yd);
    if (units == 1) {
        img->info.dpi_x = (double)xd;
        img->info.dpi_y = (double)yd;
    } else if (units == 2) {
        img->info.dpi_x = (double)xd * 2.54;
        img->info.dpi_y = (double)yd * 2.54;
    }
}

static void jpeg_parse_adobe(xx_image *img, const uint8_t *d, size_t len, int *out_transform)
{
    if (len < 12) return;
    uint16_t ver = xx_image_rd_u16be(d + 5);
    uint8_t trans = d[11];
    if (out_transform) *out_transform = (int)trans;
    xx_image_add_prop_uint(img, "Adobe", "version", ver);
    xx_image_add_prop_uint(img, "Adobe", "transform", trans);
}

typedef struct {
    const char *name;
    uint16_t tag;
} exif_known_tag;

static const exif_known_tag g_exif_tags[] = {
    {"Make", 0x010F},
    {"Model", 0x0110},
    {"Orientation", 0x0112},
    {"Software", 0x0131},
    {"DateTime", 0x0132},
    {"ExposureTime", 0x829A},
    {"FNumber", 0x829D},
    {"ISOSpeedRatings", 0x8827},
    {"DateTimeOriginal", 0x9003},
    {"FocalLength", 0x920A},
    {"PixelXDimension", 0xA002},
    {"PixelYDimension", 0xA003}
};

static const char *jpeg_exif_tag_name(uint16_t tag, char *buf, size_t buf_cap)
{
    for (size_t i = 0; i < sizeof(g_exif_tags) / sizeof(g_exif_tags[0]); ++i) {
        if (g_exif_tags[i].tag == tag) return g_exif_tags[i].name;
    }
    snprintf(buf, buf_cap, "tag %u", tag);
    return buf;
}

static uint16_t tiff_u16(const uint8_t *p, bool le) { return le ? xx_image_rd_u16le(p) : xx_image_rd_u16be(p); }
static uint32_t tiff_u32(const uint8_t *p, bool le) { return le ? xx_image_rd_u32le(p) : xx_image_rd_u32be(p); }

static void jpeg_parse_ifd(xx_image *img, const uint8_t *tiff, size_t tiff_len, uint32_t ifd_off, bool le, int depth)
{
    if (depth > 2 || !xx_image_in_range(tiff_len, ifd_off, 2)) return;
    uint16_t count = tiff_u16(tiff + ifd_off, le);
    size_t pos = ifd_off + 2;
    uint32_t exif_sub_ifd = 0;

    for (uint16_t i = 0; i < count; ++i) {
        if (!xx_image_in_range(tiff_len, pos, 12)) break;
        uint16_t tag = tiff_u16(tiff + pos, le);
        uint16_t type = tiff_u16(tiff + pos + 2, le);
        uint32_t cnt = tiff_u32(tiff + pos + 4, le);
        const uint8_t *val_ptr = tiff + pos + 8;
        size_t type_size = 1;
        if (type == 3 || type == 8) type_size = 2;       /* SHORT / SSHORT */
        else if (type == 4 || type == 9 || type == 11) type_size = 4; /* LONG / SLONG / FLOAT */
        else if (type == 5 || type == 10 || type == 12) type_size = 8; /* RATIONAL / SRATIONAL / DOUBLE */

        uint64_t total_bytes = (uint64_t)cnt * type_size;
        if (total_bytes > 4) {
            uint32_t off = tiff_u32(tiff + pos + 8, le);
            if (!xx_image_in_range(tiff_len, off, (size_t)total_bytes)) {
                pos += 12;
                continue;
            }
            val_ptr = tiff + off;
        }

        if (tag == 0x8769 && cnt > 0) {
            exif_sub_ifd = tiff_u32(tiff + pos + 8, le);
        } else {
            char name_buf[32];
            const char *key = jpeg_exif_tag_name(tag, name_buf, sizeof(name_buf));
            if (type == 2) { /* ASCII */
                size_t slen = cnt;
                while (slen > 0 && val_ptr[slen - 1] == 0) --slen;
                xx_image_add_prop_textn(img, "EXIF", key, (const char *)val_ptr, slen);
            } else if (type == 3) { /* SHORT */
                if (cnt > 0) xx_image_add_prop_uint(img, "EXIF", key, tiff_u16(val_ptr, le));
            } else if (type == 4) { /* LONG */
                if (cnt > 0) xx_image_add_prop_uint(img, "EXIF", key, tiff_u32(val_ptr, le));
            } else if (type == 5) { /* RATIONAL */
                if (cnt > 0) {
                    uint32_t num = tiff_u32(val_ptr, le);
                    uint32_t den = tiff_u32(val_ptr + 4, le);
                    if (den) xx_image_add_prop_real(img, "EXIF", key, (double)num / den);
                }
            } else if (type == 1 || type == 7) {
                if (cnt > 0 && total_bytes <= 1024) {
                    xx_image_add_prop_bytes(img, "EXIF", key, val_ptr, (size_t)total_bytes);
                }
            }
        }
        pos += 12;
    }

    if (exif_sub_ifd != 0) {
        jpeg_parse_ifd(img, tiff, tiff_len, exif_sub_ifd, le, depth + 1);
    }
}

static void jpeg_parse_exif(xx_image *img, const uint8_t *d, size_t len)
{
    if (len < 14 || memcmp(d, "Exif\0\0", 6) != 0) return;
    const uint8_t *tiff = d + 6;
    size_t tiff_len = len - 6;
    if (tiff_len < 8) return;
    bool le;
    if (tiff[0] == 'I' && tiff[1] == 'I') le = true;
    else if (tiff[0] == 'M' && tiff[1] == 'M') le = false;
    else return;

    if (tiff_u16(tiff + 2, le) != 42) return;
    uint32_t ifd0_off = tiff_u32(tiff + 4, le);
    jpeg_parse_ifd(img, tiff, tiff_len, ifd0_off, le, 0);
}

/* ---- Component and Progressive Decoding ---------------------------------- */

typedef struct {
    uint8_t id;
    uint8_t h;
    uint8_t v;
    uint8_t q_idx;
    uint32_t samp_w;       /**< real size in samples: ceil(width * h / h_max) */
    uint32_t samp_h;
    uint32_t comp_w;       /**< plane size, whole MCUs */
    uint32_t comp_h;
    uint32_t blocks_w;
    uint32_t blocks_h;
    int16_t *coeffs;       /**< 64 coeffs per block for progressive */
    uint8_t *plane;        /**< decoded 8-bit samples */
} jpeg_comp_info;

static inline int16_t jpeg_coef_shift(int v, int al)
{
    return (int16_t)(uint16_t)((uint32_t)v << al);
}

static void jpeg_decode_dc_first(jpeg_bits *b, int16_t *block, int al, const jpeg_huff_table *ht, int *last_dc)
{
    int s = jpeg_huff_decode(b, ht);
    int diff = jpeg_read_signed(b, s);
    *last_dc = (int)((uint32_t)*last_dc + (uint32_t)diff);
    block[0] = jpeg_coef_shift(*last_dc, al);
}

static void jpeg_decode_dc_refine(jpeg_bits *b, int16_t *block, int al)
{
    if (jpeg_bits_get1(b)) block[0] = (int16_t)(block[0] | (1 << al));
}

static void jpeg_decode_ac_first(jpeg_bits *b, int16_t *block, int ss, int se, int al, const jpeg_huff_table *ht, int *eob_run)
{
    if (*eob_run > 0) {
        --(*eob_run);
        return;
    }
    for (int k = ss; k <= se; ++k) {
        int rs = jpeg_huff_decode(b, ht);
        int r = rs >> 4;
        int s = rs & 0x0F;
        if (s == 0) {
            if (r < 15) {
                *eob_run = 1 << r;
                if (r > 0) *eob_run += (int)jpeg_bits_get(b, r);
                --(*eob_run);
                break;
            }
            k += 15;
        } else {
            k += r;
            int val = jpeg_read_signed(b, s);
            if (k <= se) block[g_zigzag[k]] = jpeg_coef_shift(val, al);
        }
    }
}

/* Correction bit for an already non-zero coefficient (G.1.2.3). */
static inline void jpeg_refine_coef(jpeg_bits *b, int16_t *c, int p1)
{
    if (jpeg_bits_get1(b) && (*c & p1) == 0) {
        *c = (int16_t)(*c > 0 ? *c + p1 : *c - p1);
    }
}

static void jpeg_decode_ac_refine(jpeg_bits *b, int16_t *block, int ss, int se, int al, const jpeg_huff_table *ht, int *eob_run)
{
    int p1 = 1 << al;
    int k = ss;

    if (*eob_run == 0) {
        for (; k <= se; ++k) {
            int rs = jpeg_huff_decode(b, ht);
            int r = rs >> 4;
            int s = rs & 0x0F;
            int newval = 0;
            if (s == 0) {
                if (r < 15) {
                    *eob_run = 1 << r;
                    if (r > 0) *eob_run += (int)jpeg_bits_get(b, r);
                    break; /* the rest of this block is handled below */
                }
                /* ZRL: skip 16 zero coefficients */
            } else {
                /* a new coefficient of magnitude 1; its sign bit comes first */
                newval = jpeg_bits_get1(b) ? p1 : -p1;
            }
            for (; k <= se; ++k) {
                int16_t *c = &block[g_zigzag[k]];
                if (*c != 0) {
                    jpeg_refine_coef(b, c, p1);
                } else {
                    if (--r < 0) break;
                }
            }
            if (newval != 0 && k <= se) block[g_zigzag[k]] = (int16_t)newval;
        }
    }

    if (*eob_run > 0) {
        for (; k <= se; ++k) {
            int16_t *c = &block[g_zigzag[k]];
            if (*c != 0) jpeg_refine_coef(b, c, p1);
        }
        --(*eob_run);
    }
}

/* ---- Main Decoder -------------------------------------------------------- */

static xx_image_status jpeg_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return XX_IMAGE_ERR_FORMAT;

    uint32_t width = 0, height = 0;
    int num_components = 0;
    int sof_marker = 0;
    bool is_progressive = false;
    uint16_t restart_interval = 0;
    int adobe_transform = -1;
    bool has_adobe = false;

    uint16_t qtable[4][64];
    memset(qtable, 0, sizeof(qtable));
    bool qtable_defined[4] = {false, false, false, false};
    jpeg_huff_table dc_tables[4];
    jpeg_huff_table ac_tables[4];
    memset(dc_tables, 0, sizeof(dc_tables));
    memset(ac_tables, 0, sizeof(ac_tables));

    jpeg_comp_info comps[4];
    memset(comps, 0, sizeof(comps));
    uint8_t h_max = 1, v_max = 1;
    uint32_t mcus_w = 0, mcus_h = 0;

    /* ICC Profile chunks */
    const uint8_t *icc_chunks[256] = {NULL};
    size_t icc_chunk_sizes[256] = {0};
    uint8_t icc_count = 0;

    int progressive_scans = 0;
    bool truncated = false;
    size_t pos = 2;
    xx_image_status status = XX_IMAGE_OK;

    while (pos < n) {
        /* find the next marker: 0xFF, optional 0xFF fill, a non-zero code */
        while (pos < n && d[pos] != 0xFF) ++pos;
        while (pos < n && d[pos] == 0xFF) ++pos;
        if (pos >= n) break;
        uint8_t m = d[pos++];
        if (m == 0xD9) break; /* EOI */
        if (m == 0xD8) continue; /* SOI */
        if (m >= 0xD0 && m <= 0xD7) continue; /* RST */
        if (m == 0x00) continue;

        if (!xx_image_in_range(n, pos, 2)) {
            truncated = true;
            break;
        }
        uint16_t len = xx_image_rd_u16be(d + pos);
        if (len < 2 || !xx_image_in_range(n, pos, len)) {
            truncated = true;
            break;
        }
        const uint8_t *payload = d + pos + 2;
        size_t payload_len = len - 2;
        size_t next_pos = pos + len;

        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            /* SOF0, SOF1, SOF2 */
            if (sof_marker) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
            sof_marker = m;
            is_progressive = (m == 0xC2);
            if (payload_len < 6) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
            uint8_t precision = payload[0];
            if (precision != 8) { status = XX_IMAGE_ERR_UNSUPPORTED; goto cleanup; }
            height = xx_image_rd_u16be(payload + 1);
            width = xx_image_rd_u16be(payload + 3);
            num_components = payload[5];
            if (num_components != 1 && num_components != 3 && num_components != 4) {
                status = XX_IMAGE_ERR_UNSUPPORTED;
                goto cleanup;
            }
            if (payload_len < 6 + (size_t)num_components * 3) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
            status = xx_image_check_dimensions(width, height, o);
            if (status != XX_IMAGE_OK) goto cleanup;

            for (int i = 0; i < num_components; ++i) {
                comps[i].id = payload[6 + i * 3];
                uint8_t hv = payload[7 + i * 3];
                comps[i].h = hv >> 4;
                comps[i].v = hv & 0x0F;
                comps[i].q_idx = payload[8 + i * 3];
                if (comps[i].h < 1 || comps[i].h > 4 || comps[i].v < 1 || comps[i].v > 4 || comps[i].q_idx >= 4) {
                    status = XX_IMAGE_ERR_CORRUPT;
                    goto cleanup;
                }
                if (comps[i].h > h_max) h_max = comps[i].h;
                if (comps[i].v > v_max) v_max = comps[i].v;
            }

            uint32_t mcu_w = (uint32_t)h_max * 8;
            uint32_t mcu_h = (uint32_t)v_max * 8;
            mcus_w = (width + mcu_w - 1) / mcu_w;
            mcus_h = (height + mcu_h - 1) / mcu_h;

            for (int i = 0; i < num_components; ++i) {
                comps[i].blocks_w = mcus_w * comps[i].h;
                comps[i].blocks_h = mcus_h * comps[i].v;
                comps[i].samp_w = (width * comps[i].h + h_max - 1) / h_max;
                comps[i].samp_h = (height * comps[i].v + v_max - 1) / v_max;
                comps[i].comp_w = comps[i].blocks_w * 8;
                comps[i].comp_h = comps[i].blocks_h * 8;
                if ((uint64_t)comps[i].comp_w * comps[i].comp_h > (uint64_t)(SIZE_MAX / 4)) { status = XX_IMAGE_ERR_LIMIT; goto cleanup; }
                size_t plane_bytes = (size_t)comps[i].comp_w * comps[i].comp_h;
                comps[i].plane = (uint8_t *)malloc(plane_bytes);
                if (!comps[i].plane) { status = XX_IMAGE_ERR_MEMORY; goto cleanup; }
                memset(comps[i].plane, 128, plane_bytes);

                if (is_progressive) {
                    size_t coeff_count = (size_t)comps[i].blocks_w * comps[i].blocks_h * 64;
                    comps[i].coeffs = (int16_t *)calloc(coeff_count, sizeof(int16_t));
                    if (!comps[i].coeffs) { status = XX_IMAGE_ERR_MEMORY; goto cleanup; }
                }
            }
            pos = next_pos;
        } else if (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            /* SOF3..SOF15 unsupported */
            status = XX_IMAGE_ERR_UNSUPPORTED;
            goto cleanup;
        } else if (m == 0xDB) {
            /* DQT */
            size_t off = 0;
            while (off < payload_len) {
                uint8_t pq_tq = payload[off++];
                int pq = pq_tq >> 4;
                int tq = pq_tq & 0x0F;
                if (tq >= 4) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                if (pq == 0) {
                    if (off + 64 > payload_len) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                    for (int i = 0; i < 64; ++i) qtable[tq][g_zigzag[i]] = payload[off + i];
                    off += 64;
                } else if (pq == 1) {
                    if (off + 128 > payload_len) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                    for (int i = 0; i < 64; ++i) qtable[tq][g_zigzag[i]] = xx_image_rd_u16be(payload + off + i * 2);
                    off += 128;
                } else {
                    status = XX_IMAGE_ERR_CORRUPT;
                    goto cleanup;
                }
                qtable_defined[tq] = true;
            }
            pos = next_pos;
        } else if (m == 0xC4) {
            /* DHT */
            size_t off = 0;
            while (off < payload_len) {
                uint8_t tc_th = payload[off++];
                int tc = tc_th >> 4;
                int th = tc_th & 0x0F;
                if (th >= 4 || tc > 1) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                if (off + 16 > payload_len) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                uint8_t bits[17];
                bits[0] = 0;
                int sum = 0;
                for (int i = 1; i <= 16; ++i) {
                    bits[i] = payload[off + i - 1];
                    sum += bits[i];
                }
                off += 16;
                if (sum > 256 || off + sum > payload_len) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                status = jpeg_build_huff_table(tc ? &ac_tables[th] : &dc_tables[th], bits, payload + off);
                if (status != XX_IMAGE_OK) goto cleanup;
                off += sum;
            }
            pos = next_pos;
        } else if (m == 0xDD) {
            /* DRI */
            if (payload_len < 2) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
            restart_interval = xx_image_rd_u16be(payload);
            pos = next_pos;
        } else if (m == 0xE0) {
            /* APP0 (JFIF) */
            if (payload_len >= 5 && memcmp(payload, "JFIF\0", 5) == 0 && !o->skip_metadata) {
                jpeg_parse_jfif(img, payload, payload_len);
            }
            pos = next_pos;
        } else if (m == 0xEE) {
            /* APP14 (Adobe) */
            if (payload_len >= 5 && memcmp(payload, "Adobe", 5) == 0) {
                has_adobe = true;
                if (!o->skip_metadata) jpeg_parse_adobe(img, payload, payload_len, &adobe_transform);
                else {
                    if (payload_len >= 12) adobe_transform = (int)payload[11];
                }
            }
            pos = next_pos;
        } else if (m == 0xE1) {
            /* APP1 (Exif) */
            if (payload_len >= 6 && memcmp(payload, "Exif\0\0", 6) == 0 && !o->skip_metadata) {
                jpeg_parse_exif(img, payload, payload_len);
            }
            pos = next_pos;
        } else if (m == 0xE2) {
            /* APP2 (ICC) */
            if (payload_len >= 14 && memcmp(payload, "ICC_PROFILE\0", 12) == 0) {
                uint8_t seq = payload[12];
                uint8_t tot = payload[13];
                if (tot > 0 && seq >= 1 && seq <= tot) {
                    icc_count = tot;
                    icc_chunks[seq - 1] = payload + 14;
                    icc_chunk_sizes[seq - 1] = payload_len - 14;
                }
            }
            pos = next_pos;
        } else if (m == 0xFE) {
            /* COM */
            if (!o->skip_metadata && payload_len > 0) {
                xx_image_add_prop_textn(img, "comment", "comment", (const char *)payload, payload_len);
            }
            pos = next_pos;
        } else if (m == 0xDA) {
            /* SOS */
            if (!sof_marker || payload_len < 3) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
            uint8_t ns = payload[0];
            if (ns < 1 || ns > 4 || payload_len < 3 + (size_t)ns * 2) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }

            int scan_comp_idx[4];
            uint8_t dc_tbl[4], ac_tbl[4];
            for (int i = 0; i < ns; ++i) {
                uint8_t cs = payload[1 + i * 2];
                uint8_t td_ta = payload[2 + i * 2];
                dc_tbl[i] = td_ta >> 4;
                ac_tbl[i] = td_ta & 0x0F;
                if (dc_tbl[i] > 3 || ac_tbl[i] > 3) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                int found = -1;
                for (int c = 0; c < num_components; ++c) {
                    if (comps[c].id == cs) { found = c; break; }
                }
                if (found < 0) { status = XX_IMAGE_ERR_CORRUPT; goto cleanup; }
                scan_comp_idx[i] = found;
            }

            int ss = payload[1 + ns * 2];
            int se = payload[2 + ns * 2];
            int ah = payload[3 + ns * 2] >> 4;
            int al = payload[3 + ns * 2] & 0x0F;

            if (is_progressive &&
                (ss > se || se > 63 || al > 13 || (ss == 0 && se != 0) || (ss > 0 && ns != 1))) {
                status = XX_IMAGE_ERR_CORRUPT;
                goto cleanup;
            }

            /* Advance pos to the start of entropy data */
            pos = next_pos;
            jpeg_bits reader;
            jpeg_bits_init(&reader, d, n, pos);

            int last_dc[4] = {0, 0, 0, 0};
            int eob_run = 0;
            uint32_t mcu_count = 0;

            /* An interleaved scan walks whole MCUs; a scan with one
             * component walks only the blocks that hold real samples (A.2.3). */
            uint32_t scan_w = mcus_w, scan_h = mcus_h;
            if (ns == 1) {
                scan_w = (comps[scan_comp_idx[0]].samp_w + 7) / 8;
                scan_h = (comps[scan_comp_idx[0]].samp_h + 7) / 8;
            }
            if (is_progressive) ++progressive_scans;

            for (uint32_t my = 0; my < scan_h; ++my) {
                for (uint32_t mx = 0; mx < scan_w; ++mx) {
                    if (restart_interval > 0 && mcu_count == restart_interval) {
                        jpeg_bits_restart(&reader);
                        memset(last_dc, 0, sizeof(last_dc));
                        eob_run = 0;
                        mcu_count = 0;
                    }
                    ++mcu_count;
                    if (jpeg_bits_overrun(&reader)) {
                        /* Out of data: the rest of the interval keeps what it
                         * has (as libjpeg does).  Only a pending RSTn lets the
                         * scan go on, at the next restart point. */
                        if (reader.truncated || restart_interval == 0 || reader.marker < 0xD0 || reader.marker > 0xD7) goto scan_done;
                        continue;
                    }
                    for (int i = 0; i < ns; ++i) {
                        int ci = scan_comp_idx[i];
                        jpeg_comp_info *comp = &comps[ci];
                        const jpeg_huff_table *dc_ht = &dc_tables[dc_tbl[i]];
                        const jpeg_huff_table *ac_ht = &ac_tables[ac_tbl[i]];
                        uint32_t nh = (ns == 1) ? 1 : comp->h;
                        uint32_t nv = (ns == 1) ? 1 : comp->v;
                        for (uint32_t vy = 0; vy < nv; ++vy) {
                            for (uint32_t hx = 0; hx < nh; ++hx) {
                                uint32_t bx = mx * nh + hx;
                                uint32_t by = my * nv + vy;
                                if (is_progressive) {
                                    int16_t *blk = comp->coeffs + ((size_t)by * comp->blocks_w + bx) * 64;
                                    if (ss == 0) {
                                        if (ah == 0) jpeg_decode_dc_first(&reader, blk, al, dc_ht, &last_dc[ci]);
                                        else jpeg_decode_dc_refine(&reader, blk, al);
                                    } else {
                                        if (ah == 0) jpeg_decode_ac_first(&reader, blk, ss, se, al, ac_ht, &eob_run);
                                        else jpeg_decode_ac_refine(&reader, blk, ss, se, al, ac_ht, &eob_run);
                                    }
                                } else {
                                    int16_t blk[64];
                                    memset(blk, 0, sizeof(blk));
                                    int s = jpeg_huff_decode(&reader, dc_ht);
                                    int dc = (int)((uint32_t)last_dc[ci] + (uint32_t)jpeg_read_signed(&reader, s));
                                    last_dc[ci] = dc;
                                    blk[0] = (int16_t)(dc < -32768 ? -32768 : (dc > 32767 ? 32767 : dc));
                                    for (int k = 1; k < 64; ++k) {
                                        int rs = jpeg_huff_decode(&reader, ac_ht);
                                        int r = rs >> 4;
                                        int ac_s = rs & 0x0F;
                                        if (ac_s == 0) {
                                            if (r != 15) break; /* EOB */
                                            k += 15;
                                        } else {
                                            k += r;
                                            int val = jpeg_read_signed(&reader, ac_s);
                                            if (k < 64) blk[g_zigzag[k]] = (int16_t)val;
                                        }
                                    }
                                    uint8_t *dst = comp->plane + ((size_t)by * 8 * comp->comp_w + (size_t)bx * 8);
                                    jpeg_idct_block(blk, qtable[comp->q_idx], dst, comp->comp_w);
                                }
                            }
                        }
                    }
                }
            }
        scan_done:
            if (reader.truncated) truncated = true;
            /* Advance marker search after scan data */
            pos = reader.pos;
            if (reader.marker != 0 && pos >= 2) pos -= 2;
        } else {
            /* Skip unknown / unsupported application markers cleanly */
            pos = next_pos;
        }
    }

    if (!sof_marker || width == 0 || height == 0) {
        status = XX_IMAGE_ERR_CORRUPT;
        goto cleanup;
    }

    /* For progressive mode, perform IDCT on all blocks */
    if (is_progressive) {
        for (int c = 0; c < num_components; ++c) {
            jpeg_comp_info *comp = &comps[c];
            for (uint32_t by = 0; by < comp->blocks_h; ++by) {
                for (uint32_t bx = 0; bx < comp->blocks_w; ++bx) {
                    const int16_t *blk = comp->coeffs + ((size_t)by * comp->blocks_w + bx) * 64;
                    uint8_t *dst = comp->plane + ((size_t)by * 8 * comp->comp_w + (size_t)bx * 8);
                    jpeg_idct_block(blk, qtable[comp->q_idx], dst, comp->comp_w);
                }
            }
        }
    }

    /* Allocate output frame */
    xx_image_frame *f = xx_image_add_frame(img, width, height);
    if (!f) { status = XX_IMAGE_ERR_MEMORY; goto cleanup; }

    /* Upsample and Colour Convert to RGBA */
    if (num_components == 1) {
        /* Grayscale */
        const uint8_t *y_plane = comps[0].plane;
        size_t stride = comps[0].comp_w;
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t *srow = y_plane + (size_t)y * stride;
            uint8_t *dpx = f->rgba + (size_t)y * width * 4;
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t v = srow[x];
                dpx[x * 4 + 0] = v;
                dpx[x * 4 + 1] = v;
                dpx[x * 4 + 2] = v;
                dpx[x * 4 + 3] = 255;
            }
        }
        img->info.color_model = XX_IMAGE_COLOR_GRAY;
    } else {
        size_t total_px = (size_t)width * height;
        uint8_t *up[4] = {NULL, NULL, NULL, NULL};
        bool up_ok = true;
        for (int c = 0; c < num_components; ++c) {
            up[c] = (uint8_t *)malloc(total_px);
            if (!up[c]) { up_ok = false; break; }
            jpeg_upsample_plane(comps[c].plane, comps[c].comp_w, comps[c].samp_w, comps[c].samp_h, up[c], width, height, comps[c].h, comps[c].v, h_max, v_max);
        }
        if (!up_ok) {
            for (int c = 0; c < 4; ++c) free(up[c]);
            status = XX_IMAGE_ERR_MEMORY;
            goto cleanup;
        }
        const uint8_t *c0 = up[0], *c1 = up[1], *c2 = up[2], *c3 = up[3];
        uint8_t *px = f->rgba;

        if (num_components == 3) {
            /* As libjpeg: JFIF means YCbCr; else Adobe transform 0 or the
             * component ids 'R','G','B' mean RGB. */
            bool is_rgb = false;
            if (has_adobe) is_rgb = (adobe_transform == 0);
            else if (comps[0].id == 'R' && comps[1].id == 'G' && comps[2].id == 'B') is_rgb = true;
            if (is_rgb) {
                for (size_t i = 0; i < total_px; ++i, px += 4) {
                    px[0] = c0[i];
                    px[1] = c1[i];
                    px[2] = c2[i];
                    px[3] = 255;
                }
                img->info.color_model = XX_IMAGE_COLOR_RGB;
            } else {
                for (size_t i = 0; i < total_px; ++i, px += 4) {
                    jpeg_ycbcr_to_rgb(c0[i], c1[i], c2[i], &px[0], &px[1], &px[2]);
                    px[3] = 255;
                }
                img->info.color_model = XX_IMAGE_COLOR_YCBCR;
            }
        } else {
            /* 4 components: CMYK, or YCCK with Adobe transform 2.  The
             * samples are taken as inverted (255 - ink), the Adobe convention
             * that Pillow also assumes for every CMYK JPEG.  With true inks
             * R = (255 - C) * (255 - K) / 255, rounded the way Pillow does. */
            bool is_ycck = has_adobe && adobe_transform == 2;
            for (size_t i = 0; i < total_px; ++i, px += 4) {
                uint8_t s[3];
                uint32_t nk = c3[i];
                if (is_ycck) {
                    jpeg_ycbcr_to_rgb(c0[i], c1[i], c2[i], &s[0], &s[1], &s[2]);
                    s[0] = (uint8_t)(255 - s[0]);
                    s[1] = (uint8_t)(255 - s[1]);
                    s[2] = (uint8_t)(255 - s[2]);
                } else {
                    s[0] = c0[i];
                    s[1] = c1[i];
                    s[2] = c2[i];
                }
                for (int c = 0; c < 3; ++c) {
                    uint32_t t = (255u - s[c]) * nk + 128u;
                    px[c] = (uint8_t)(nk - (((t >> 8) + t) >> 8));
                }
                px[3] = 255;
            }
            img->info.color_model = XX_IMAGE_COLOR_CMYK;
        }
        for (int c = 0; c < 4; ++c) free(up[c]);
    }

    /* Reassemble ICC Profile chunks */
    if (!o->skip_metadata && icc_count > 0) {
        size_t total_icc = 0;
        bool complete = true;
        for (int i = 0; i < icc_count; ++i) {
            if (!icc_chunks[i] || icc_chunk_sizes[i] == 0) { complete = false; break; }
            total_icc += icc_chunk_sizes[i];
        }
        if (complete && total_icc > 0) {
            uint8_t *icc_buf = (uint8_t *)malloc(total_icc);
            if (icc_buf) {
                size_t wr = 0;
                for (int i = 0; i < icc_count; ++i) {
                    memcpy(icc_buf + wr, icc_chunks[i], icc_chunk_sizes[i]);
                    wr += icc_chunk_sizes[i];
                }
                xx_image_add_prop_bytes(img, "ICC", "profile", icc_buf, total_icc);
                free(icc_buf);
            }
        }
    }

    /* Fill xx_image_info */
    img->info.channels = (uint32_t)num_components;
    img->info.bits_per_sample = 8;
    img->info.bits_per_pixel = (uint32_t)(num_components * 8);
    img->info.is_lossy = true;
    img->info.is_compressed = true;
    img->info.is_interlaced = is_progressive;
    img->info.frame_count = 1;
    XX_IMAGE_SET_STR(img->info.compression, is_progressive ? "JPEG progressive" : "JPEG baseline");

    /* Add JPEG group properties */
    if (!o->skip_metadata) {
        xx_image_add_prop_text(img, "JPEG", "process", is_progressive ? "progressive" : (sof_marker == 0xC1 ? "extended sequential" : "baseline sequential"));
        xx_image_add_prop_uint(img, "JPEG", "precision", 8);
        xx_image_add_prop_uint(img, "JPEG", "components", (uint64_t)num_components);
        for (int i = 0; i < num_components; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "component %d id", i);
            xx_image_add_prop_uint(img, "JPEG", key, comps[i].id);
            snprintf(key, sizeof(key), "component %d h_sample", i);
            xx_image_add_prop_uint(img, "JPEG", key, comps[i].h);
            snprintf(key, sizeof(key), "component %d v_sample", i);
            xx_image_add_prop_uint(img, "JPEG", key, comps[i].v);
        }
        int qcnt = 0;
        for (int i = 0; i < 4; ++i) if (qtable_defined[i]) ++qcnt;
        xx_image_add_prop_uint(img, "JPEG", "quantisation tables", (uint64_t)qcnt);
        xx_image_add_prop_uint(img, "JPEG", "restart interval", restart_interval);
        if (is_progressive) {
            xx_image_add_prop_uint(img, "JPEG", "progressive scans", (uint64_t)progressive_scans);
        }
        if (truncated) {
            xx_image_add_prop_text(img, "JPEG", "warning", "truncated");
        }
    }

cleanup:
    for (int i = 0; i < 4; ++i) {
        free(comps[i].coeffs);
        free(comps[i].plane);
    }
    return status;
}

/* ---- Encoder ------------------------------------------------------------- */

typedef struct {
    xx_image_buffer *out;
    uint32_t bit_buf;
    int bits_left;
} jpeg_writer;

static inline void jpeg_enc_put_bits(jpeg_writer *w, uint32_t bits, int n)
{
    while (n > 0) {
        int space = 32 - w->bits_left;
        int take = n < space ? n : space;
        w->bit_buf = (w->bit_buf << take) | ((bits >> (n - take)) & ((1U << take) - 1));
        w->bits_left += take;
        n -= take;

        while (w->bits_left >= 8) {
            uint8_t byte = (uint8_t)(w->bit_buf >> (w->bits_left - 8));
            w->bits_left -= 8;
            xx_image_buf_u8(w->out, byte);
            if (byte == 0xFF) {
                xx_image_buf_u8(w->out, 0x00);
            }
        }
    }
}

static inline void jpeg_enc_flush(jpeg_writer *w)
{
    if (w->bits_left > 0) {
        uint8_t byte = (uint8_t)(w->bit_buf << (8 - w->bits_left));
        byte |= (uint8_t)((1U << (8 - w->bits_left)) - 1); /* Pad with 1s */
        xx_image_buf_u8(w->out, byte);
        if (byte == 0xFF) {
            xx_image_buf_u8(w->out, 0x00);
        }
        w->bit_buf = 0;
        w->bits_left = 0;
    }
}

static inline int jpeg_enc_category(int v)
{
    if (v < 0) v = -v;
    if (v == 0) return 0;
    int s = 0;
    while (v > 0) {
        ++s;
        v >>= 1;
    }
    return s;
}

static void jpeg_enc_encode_block(jpeg_writer *w, const int16_t in[64], int *last_dc, const uint32_t *dc_tbl, const uint32_t *ac_tbl)
{
    /* DC coefficient */
    int diff = in[0] - *last_dc;
    *last_dc = in[0];
    int dc_s = jpeg_enc_category(diff);
    uint32_t dc_entry = dc_tbl[dc_s];
    jpeg_enc_put_bits(w, dc_entry >> 8, dc_entry & 0xFF);
    if (dc_s > 0) {
        uint32_t vbits = (diff < 0) ? (uint32_t)(diff + (1 << dc_s) - 1) : (uint32_t)diff;
        jpeg_enc_put_bits(w, vbits, dc_s);
    }

    /* AC coefficients */
    int r = 0;
    for (int k = 1; k < 64; ++k) {
        int val = in[g_zigzag[k]];
        if (val == 0) {
            ++r;
        } else {
            while (r >= 16) {
                uint32_t zrl = ac_tbl[0xF0];
                jpeg_enc_put_bits(w, zrl >> 8, zrl & 0xFF);
                r -= 16;
            }
            int ac_s = jpeg_enc_category(val);
            uint32_t symbol = (uint32_t)((r << 4) | ac_s);
            uint32_t ac_entry = ac_tbl[symbol];
            jpeg_enc_put_bits(w, ac_entry >> 8, ac_entry & 0xFF);
            uint32_t vbits = (val < 0) ? (uint32_t)(val + (1 << ac_s) - 1) : (uint32_t)val;
            jpeg_enc_put_bits(w, vbits, ac_s);
            r = 0;
        }
    }
    if (r > 0) {
        uint32_t eob = ac_tbl[0x00];
        jpeg_enc_put_bits(w, eob >> 8, eob & 0xFF);
    }
}

static xx_image_status jpeg_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    uint32_t g_enc_dc_lum[256], g_enc_ac_lum[256], g_enc_dc_chrom[256], g_enc_ac_chrom[256];
    jpeg_build_enc_table(g_std_dc_lum_bits, g_std_dc_lum_val, g_enc_dc_lum);
    jpeg_build_enc_table(g_std_ac_lum_bits, g_std_ac_lum_val, g_enc_ac_lum);
    jpeg_build_enc_table(g_std_dc_chrom_bits, g_std_dc_chrom_val, g_enc_dc_chrom);
    jpeg_build_enc_table(g_std_ac_chrom_bits, g_std_ac_chrom_val, g_enc_ac_chrom);
    const xx_image_frame *f = xx_image_save_frame(img, o);
    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    uint32_t w = f->width, h = f->height;
    if (w == 0 || h == 0 || w > 65535 || h > 65535) return XX_IMAGE_ERR_LIMIT;

    bool is_gray = xx_image_frame_is_gray(f);
    int quality = o ? o->quality : 0;
    if (quality <= 0) quality = 90;
    if (quality > 100) quality = 100;
    bool sub420 = !is_gray && (quality < 90);

    /* Scale quantization tables with IJG formula */
    int scale = (quality < 50) ? (5000 / quality) : (200 - quality * 2);
    uint8_t q_lum[64], q_chrom[64];
    for (int i = 0; i < 64; ++i) {
        int v_l = (g_std_dqt_lum[i] * scale + 50) / 100;
        if (v_l < 1) v_l = 1;
        if (v_l > 255) v_l = 255;
        q_lum[i] = (uint8_t)v_l;

        int v_c = (g_std_dqt_chrom[i] * scale + 50) / 100;
        if (v_c < 1) v_c = 1;
        if (v_c > 255) v_c = 255;
        q_chrom[i] = (uint8_t)v_c;
    }

    /* Convert RGBA (composited on black) to Y / YCbCr planes */
    size_t total_px = (size_t)w * h;
    uint8_t *y_plane = (uint8_t *)malloc(total_px);
    uint8_t *cb_plane = NULL, *cr_plane = NULL;
    uint32_t cw = sub420 ? (w + 1) / 2 : w;
    uint32_t ch = sub420 ? (h + 1) / 2 : h;

    if (!is_gray) {
        cb_plane = (uint8_t *)malloc((size_t)cw * ch);
        cr_plane = (uint8_t *)malloc((size_t)cw * ch);
        if (!cb_plane || !cr_plane) {
            free(y_plane); free(cb_plane); free(cr_plane);
            return XX_IMAGE_ERR_MEMORY;
        }
    }
    if (!y_plane) {
        free(cb_plane); free(cr_plane);
        return XX_IMAGE_ERR_MEMORY;
    }

    if (is_gray) {
        for (size_t i = 0; i < total_px; ++i) {
            const uint8_t *p = f->rgba + i * 4;
            uint8_t a = p[3];
            uint8_t r = (uint8_t)(((uint32_t)p[0] * a + 127) / 255);
            y_plane[i] = r;
        }
    } else if (!sub420) {
        for (size_t i = 0; i < total_px; ++i) {
            const uint8_t *p = f->rgba + i * 4;
            uint8_t a = p[3];
            uint8_t r = (uint8_t)(((uint32_t)p[0] * a + 127) / 255);
            uint8_t g = (uint8_t)(((uint32_t)p[1] * a + 127) / 255);
            uint8_t b = (uint8_t)(((uint32_t)p[2] * a + 127) / 255);
            y_plane[i]  = (uint8_t)(( 19595 * r + 38470 * g +  7471 * b + 32768) >> 16);
            cb_plane[i] = (uint8_t)((-11059 * r - 21709 * g + 32768 * b + 8421375) >> 16);
            cr_plane[i] = (uint8_t)(( 32768 * r - 27439 * g -  5329 * b + 8421375) >> 16);
        }
    } else {
        uint8_t *cb_full = (uint8_t *)malloc(total_px);
        uint8_t *cr_full = (uint8_t *)malloc(total_px);
        if (!cb_full || !cr_full) {
            free(y_plane); free(cb_plane); free(cr_plane); free(cb_full); free(cr_full);
            return XX_IMAGE_ERR_MEMORY;
        }
        for (size_t i = 0; i < total_px; ++i) {
            const uint8_t *p = f->rgba + i * 4;
            uint8_t a = p[3];
            uint8_t r = (uint8_t)(((uint32_t)p[0] * a + 127) / 255);
            uint8_t g = (uint8_t)(((uint32_t)p[1] * a + 127) / 255);
            uint8_t b = (uint8_t)(((uint32_t)p[2] * a + 127) / 255);
            y_plane[i] = (uint8_t)(( 19595 * r + 38470 * g +  7471 * b + 32768) >> 16);
            cb_full[i] = (uint8_t)((-11059 * r - 21709 * g + 32768 * b + 8421375) >> 16);
            cr_full[i] = (uint8_t)(( 32768 * r - 27439 * g -  5329 * b + 8421375) >> 16);
        }
        /* 2x2 box downsampling for chroma */
        for (uint32_t cy = 0; cy < ch; ++cy) {
            for (uint32_t cx = 0; cx < cw; ++cx) {
                uint32_t x0 = cx * 2, x1 = (x0 + 1 < w) ? x0 + 1 : x0;
                uint32_t y0 = cy * 2, y1 = (y0 + 1 < h) ? y0 + 1 : y0;
                cb_plane[cy * cw + cx] = (uint8_t)((cb_full[y0 * w + x0] + cb_full[y0 * w + x1] +
                                                    cb_full[y1 * w + x0] + cb_full[y1 * w + x1] + 2) / 4);
                cr_plane[cy * cw + cx] = (uint8_t)((cr_full[y0 * w + x0] + cr_full[y0 * w + x1] +
                                                    cr_full[y1 * w + x0] + cr_full[y1 * w + x1] + 2) / 4);
            }
        }
        free(cb_full); free(cr_full);
    }

    /* Output SOI */
    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xD8);

    /* Output APP0 (JFIF) */
    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xE0);
    xx_image_buf_u16be(out, 16);
    xx_image_buf_append(out, "JFIF\0", 5);
    xx_image_buf_u8(out, 1); xx_image_buf_u8(out, 1); /* version 1.01 */
    xx_image_buf_u8(out, 0);                       /* no units */
    xx_image_buf_u16be(out, 1); xx_image_buf_u16be(out, 1);
    xx_image_buf_u8(out, 0); xx_image_buf_u8(out, 0);

    /* Output DQT (Luminance) */
    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xDB);
    xx_image_buf_u16be(out, 67);
    xx_image_buf_u8(out, 0x00); /* table 0 */
    for (int i = 0; i < 64; ++i) xx_image_buf_u8(out, q_lum[g_zigzag[i]]);

    if (!is_gray) {
        /* Output DQT (Chrominance) */
        xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xDB);
        xx_image_buf_u16be(out, 67);
        xx_image_buf_u8(out, 0x01); /* table 1 */
        for (int i = 0; i < 64; ++i) xx_image_buf_u8(out, q_chrom[g_zigzag[i]]);
    }

    /* Output SOF0 (Baseline Sequential) */
    int num_comps = is_gray ? 1 : 3;
    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xC0);
    xx_image_buf_u16be(out, (uint16_t)(8 + num_comps * 3));
    xx_image_buf_u8(out, 8); /* precision */
    xx_image_buf_u16be(out, (uint16_t)h);
    xx_image_buf_u16be(out, (uint16_t)w);
    xx_image_buf_u8(out, (uint8_t)num_comps);

    if (is_gray) {
        xx_image_buf_u8(out, 1);    /* comp 1 */
        xx_image_buf_u8(out, 0x11); /* 1x1 */
        xx_image_buf_u8(out, 0);    /* qtable 0 */
    } else {
        xx_image_buf_u8(out, 1);    /* Y */
        xx_image_buf_u8(out, sub420 ? 0x22 : 0x11);
        xx_image_buf_u8(out, 0);
        xx_image_buf_u8(out, 2);    /* Cb */
        xx_image_buf_u8(out, 0x11);
        xx_image_buf_u8(out, 1);
        xx_image_buf_u8(out, 3);    /* Cr */
        xx_image_buf_u8(out, 0x11);
        xx_image_buf_u8(out, 1);
    }

    /* Output DHT */
    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xC4);
    xx_image_buf_u16be(out, (uint16_t)(2 + 1 + 16 + sizeof(g_std_dc_lum_val)));
    xx_image_buf_u8(out, 0x00); /* DC table 0 */
    xx_image_buf_append(out, g_std_dc_lum_bits + 1, 16);
    xx_image_buf_append(out, g_std_dc_lum_val, sizeof(g_std_dc_lum_val));

    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xC4);
    xx_image_buf_u16be(out, (uint16_t)(2 + 1 + 16 + sizeof(g_std_ac_lum_val)));
    xx_image_buf_u8(out, 0x10); /* AC table 0 */
    xx_image_buf_append(out, g_std_ac_lum_bits + 1, 16);
    xx_image_buf_append(out, g_std_ac_lum_val, sizeof(g_std_ac_lum_val));

    if (!is_gray) {
        xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xC4);
        xx_image_buf_u16be(out, (uint16_t)(2 + 1 + 16 + sizeof(g_std_dc_chrom_val)));
        xx_image_buf_u8(out, 0x01); /* DC table 1 */
        xx_image_buf_append(out, g_std_dc_chrom_bits + 1, 16);
        xx_image_buf_append(out, g_std_dc_chrom_val, sizeof(g_std_dc_chrom_val));

        xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xC4);
        xx_image_buf_u16be(out, (uint16_t)(2 + 1 + 16 + sizeof(g_std_ac_chrom_val)));
        xx_image_buf_u8(out, 0x11); /* AC table 1 */
        xx_image_buf_append(out, g_std_ac_chrom_bits + 1, 16);
        xx_image_buf_append(out, g_std_ac_chrom_val, sizeof(g_std_ac_chrom_val));
    }

    /* Output SOS */
    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xDA);
    xx_image_buf_u16be(out, (uint16_t)(6 + num_comps * 2));
    xx_image_buf_u8(out, (uint8_t)num_comps);
    if (is_gray) {
        xx_image_buf_u8(out, 1); xx_image_buf_u8(out, 0x00);
    } else {
        xx_image_buf_u8(out, 1); xx_image_buf_u8(out, 0x00);
        xx_image_buf_u8(out, 2); xx_image_buf_u8(out, 0x11);
        xx_image_buf_u8(out, 3); xx_image_buf_u8(out, 0x11);
    }
    xx_image_buf_u8(out, 0);  /* Ss */
    xx_image_buf_u8(out, 63); /* Se */
    xx_image_buf_u8(out, 0);  /* Ah=0, Al=0 */

    /* Entropy Encode MCUs */
    jpeg_writer wr = {out, 0, 0};
    int last_dc[3] = {0, 0, 0};

    uint32_t mcu_w = (is_gray || !sub420) ? 8 : 16;
    uint32_t mcu_h = (is_gray || !sub420) ? 8 : 16;
    uint32_t mcus_x = (w + mcu_w - 1) / mcu_w;
    uint32_t mcus_y = (h + mcu_h - 1) / mcu_h;

    for (uint32_t my = 0; my < mcus_y; ++my) {
        for (uint32_t mx = 0; mx < mcus_x; ++mx) {
            if (is_gray) {
                uint8_t block[64];
                uint32_t bx = mx * 8, by = my * 8;
                for (uint32_t y = 0; y < 8; ++y) {
                    uint32_t sy = (by + y < h) ? (by + y) : (h - 1);
                    for (uint32_t x = 0; x < 8; ++x) {
                        uint32_t sx = (bx + x < w) ? (bx + x) : (w - 1);
                        block[y * 8 + x] = y_plane[sy * w + sx];
                    }
                }
                int16_t fdct[64];
                jpeg_fdct_block(block, 8, fdct);
                for (int i = 0; i < 64; ++i) {
                    int val = fdct[i];
                    fdct[i] = (int16_t)(val >= 0 ? ((val + q_lum[i] / 2) / q_lum[i]) : ((val - q_lum[i] / 2) / q_lum[i]));
                }
                jpeg_enc_encode_block(&wr, fdct, &last_dc[0], g_enc_dc_lum, g_enc_ac_lum);
            } else if (!sub420) {
                /* 4:4:4 MCU */
                uint32_t bx = mx * 8, by = my * 8;
                uint8_t blk_y[64], blk_cb[64], blk_cr[64];
                for (uint32_t y = 0; y < 8; ++y) {
                    uint32_t sy = (by + y < h) ? (by + y) : (h - 1);
                    for (uint32_t x = 0; x < 8; ++x) {
                        uint32_t sx = (bx + x < w) ? (bx + x) : (w - 1);
                        blk_y[y * 8 + x]  = y_plane[sy * w + sx];
                        blk_cb[y * 8 + x] = cb_plane[sy * w + sx];
                        blk_cr[y * 8 + x] = cr_plane[sy * w + sx];
                    }
                }
                int16_t fdct[64];
                /* Y */
                jpeg_fdct_block(blk_y, 8, fdct);
                for (int i = 0; i < 64; ++i) {
                    int val = fdct[i];
                    fdct[i] = (int16_t)(val >= 0 ? ((val + q_lum[i] / 2) / q_lum[i]) : ((val - q_lum[i] / 2) / q_lum[i]));
                }
                jpeg_enc_encode_block(&wr, fdct, &last_dc[0], g_enc_dc_lum, g_enc_ac_lum);
                /* Cb */
                jpeg_fdct_block(blk_cb, 8, fdct);
                for (int i = 0; i < 64; ++i) {
                    int val = fdct[i];
                    fdct[i] = (int16_t)(val >= 0 ? ((val + q_chrom[i] / 2) / q_chrom[i]) : ((val - q_chrom[i] / 2) / q_chrom[i]));
                }
                jpeg_enc_encode_block(&wr, fdct, &last_dc[1], g_enc_dc_chrom, g_enc_ac_chrom);
                /* Cr */
                jpeg_fdct_block(blk_cr, 8, fdct);
                for (int i = 0; i < 64; ++i) {
                    int val = fdct[i];
                    fdct[i] = (int16_t)(val >= 0 ? ((val + q_chrom[i] / 2) / q_chrom[i]) : ((val - q_chrom[i] / 2) / q_chrom[i]));
                }
                jpeg_enc_encode_block(&wr, fdct, &last_dc[2], g_enc_dc_chrom, g_enc_ac_chrom);
            } else {
                /* 4:2:0 MCU: 4 Y blocks, 1 Cb block, 1 Cr block */
                for (int sub_y = 0; sub_y < 2; ++sub_y) {
                    for (int sub_x = 0; sub_x < 2; ++sub_x) {
                        uint32_t bx = mx * 16 + sub_x * 8;
                        uint32_t by = my * 16 + sub_y * 8;
                        uint8_t blk_y[64];
                        for (uint32_t y = 0; y < 8; ++y) {
                            uint32_t sy = (by + y < h) ? (by + y) : (h - 1);
                            for (uint32_t x = 0; x < 8; ++x) {
                                uint32_t sx = (bx + x < w) ? (bx + x) : (w - 1);
                                blk_y[y * 8 + x] = y_plane[sy * w + sx];
                            }
                        }
                        int16_t fdct[64];
                        jpeg_fdct_block(blk_y, 8, fdct);
                        for (int i = 0; i < 64; ++i) {
                            int val = fdct[i];
                            fdct[i] = (int16_t)(val >= 0 ? ((val + q_lum[i] / 2) / q_lum[i]) : ((val - q_lum[i] / 2) / q_lum[i]));
                        }
                        jpeg_enc_encode_block(&wr, fdct, &last_dc[0], g_enc_dc_lum, g_enc_ac_lum);
                    }
                }
                /* Cb block */
                uint32_t cx = mx * 8, cy = my * 8;
                uint8_t blk_cb[64], blk_cr[64];
                for (uint32_t y = 0; y < 8; ++y) {
                    uint32_t sy = (cy + y < ch) ? (cy + y) : (ch - 1);
                    for (uint32_t x = 0; x < 8; ++x) {
                        uint32_t sx = (cx + x < cw) ? (cx + x) : (cw - 1);
                        blk_cb[y * 8 + x] = cb_plane[sy * cw + sx];
                        blk_cr[y * 8 + x] = cr_plane[sy * cw + sx];
                    }
                }
                int16_t fdct[64];
                jpeg_fdct_block(blk_cb, 8, fdct);
                for (int i = 0; i < 64; ++i) {
                    int val = fdct[i];
                    fdct[i] = (int16_t)(val >= 0 ? ((val + q_chrom[i] / 2) / q_chrom[i]) : ((val - q_chrom[i] / 2) / q_chrom[i]));
                }
                jpeg_enc_encode_block(&wr, fdct, &last_dc[1], g_enc_dc_chrom, g_enc_ac_chrom);

                jpeg_fdct_block(blk_cr, 8, fdct);
                for (int i = 0; i < 64; ++i) {
                    int val = fdct[i];
                    fdct[i] = (int16_t)(val >= 0 ? ((val + q_chrom[i] / 2) / q_chrom[i]) : ((val - q_chrom[i] / 2) / q_chrom[i]));
                }
                jpeg_enc_encode_block(&wr, fdct, &last_dc[2], g_enc_dc_chrom, g_enc_ac_chrom);
            }
        }
    }

    jpeg_enc_flush(&wr);

    /* Output EOI */
    xx_image_buf_u8(out, 0xFF); xx_image_buf_u8(out, 0xD9);

    free(y_plane); free(cb_plane); free(cr_plane);
    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

/* ---- Probe and Registration ---------------------------------------------- */

static int jpeg_probe(const uint8_t *d, size_t n)
{
    if (n < 2) return 0;
    if (d[0] == 0xFF && d[1] == 0xD8) {
        if (n >= 4 && d[2] == 0xFF) return 95;
        return 90;
    }
    return 0;
}

const xx_image_codec xx_image_codec_jpeg = {
    "jpeg",
    "JPEG",
    "jpg;jpeg;jpe;jfif",
    "image/jpeg",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_LOSSY | XX_IMAGE_CODEC_INFO,
    530,
    jpeg_probe,
    jpeg_load,
    jpeg_save
};
