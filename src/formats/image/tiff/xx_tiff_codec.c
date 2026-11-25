/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tagged Image File Format (TIFF) codec.
 * Load: little- and big-endian classic TIFF (BigTIFF rejected), multi-frame IFD
 * chains, strips and tiles, compressions (1 none, 5 LZW MSB-first, 32773 PackBits,
 * 8 / 32946 Deflate), horizontal predictor 2 (8 and 16 bit), photometrics (0/1 gray,
 * 2 RGB/RGBA with associated/unassociated alpha, 3 palette), planar configs 1 and 2,
 * tags into "TIFF" / "EXIF" / "ICC" property groups.
 * Save: uncompressed or Deflate (options->level > 0), RGB or RGBA 8-bit, strips of
 * ~64 KiB, little-endian, multi-frame IFD chain with all_frames.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIFF_MAGIC_LE 0x4949U /* "II" */
#define TIFF_MAGIC_BE 0x4D4DU /* "MM" */

static inline uint16_t tf_u16(const uint8_t *p, bool le)
{
    return le ? xx_image_rd_u16le(p) : xx_image_rd_u16be(p);
}

static inline uint32_t tf_u32(const uint8_t *p, bool le)
{
    return le ? xx_image_rd_u32le(p) : xx_image_rd_u32be(p);
}

static int tiff_probe(const uint8_t *d, size_t n)
{
    uint16_t endian, magic;
    if (n < 8) return 0;
    endian = xx_image_rd_u16le(d);
    if (endian == TIFF_MAGIC_LE) {
        magic = xx_image_rd_u16le(d + 2);
        if (magic == 42) return 95;
    } else if (endian == 0x4D4D) {
        magic = xx_image_rd_u16be(d + 2);
        if (magic == 42) return 95;
    }
    return 0;
}

typedef struct tf_tag {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t offset; /* file offset or inline offset */
} tf_tag;

static size_t tf_type_size(uint16_t type)
{
    switch (type) {
    case 1: case 2: case 6: case 7: return 1;
    case 3: case 8: return 2;
    case 4: case 9: case 11: return 4;
    case 5: case 10: case 12: return 8;
    default: return 0;
    }
}

static const tf_tag *tf_find_tag(const tf_tag *tags, size_t count, uint16_t id)
{
    for (size_t i = 0; i < count; ++i) {
        if (tags[i].tag == id) return &tags[i];
    }
    return NULL;
}

static bool tf_tag_uint(const uint8_t *d, size_t n, bool le, const tf_tag *t, size_t idx, uint32_t *out)
{
    size_t off;
    if (!t || idx >= t->count) return false;
    off = t->offset;
    if (t->type == 1 || t->type == 6 || t->type == 7) {
        if (!xx_image_in_range(n, off + idx, 1)) return false;
        *out = d[off + idx];
        return true;
    }
    if (t->type == 3 || t->type == 8) {
        if (!xx_image_in_range(n, off + idx * 2, 2)) return false;
        *out = tf_u16(d + off + idx * 2, le);
        return true;
    }
    if (t->type == 4 || t->type == 9) {
        if (!xx_image_in_range(n, off + idx * 4, 4)) return false;
        *out = tf_u32(d + off + idx * 4, le);
        return true;
    }
    return false;
}

static bool tf_packbits_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size)
{
    size_t sp = 0, dp = 0;
    while (sp < src_size && dp < dst_size) {
        int8_t b = (int8_t)src[sp++];
        if (b >= 0) {
            size_t cnt = (size_t)b + 1;
            if (sp + cnt > src_size || dp + cnt > dst_size) return false;
            memcpy(dst + dp, src + sp, cnt);
            sp += cnt;
            dp += cnt;
        } else if (b != -128) {
            size_t cnt = (size_t)(1 - b);
            uint8_t val;
            if (sp >= src_size || dp + cnt > dst_size) return false;
            val = src[sp++];
            memset(dst + dp, val, cnt);
            dp += cnt;
        }
    }
    return true;
}

typedef struct tf_lzw_entry {
    int16_t prefix;
    uint8_t suffix;
    uint16_t length;
} tf_lzw_entry;

typedef struct tf_msb_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bit_buf;
    int bit_cnt;
} tf_msb_reader;

static inline int tf_lzw_next_code(tf_msb_reader *r, int code_size)
{
    while (r->bit_cnt < code_size) {
        if (r->pos >= r->size) return -1;
        r->bit_buf = (r->bit_buf << 8) | (uint32_t)r->data[r->pos++];
        r->bit_cnt += 8;
    }
    r->bit_cnt -= code_size;
    return (int)((r->bit_buf >> r->bit_cnt) & ((1U << code_size) - 1));
}

static bool tf_lzw_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size)
{
    /* TIFF LZW (MSB first, early change): codes grow one code before the
     * table fills, i.e. at 511, 1023 and 2047 entries. */
    tf_lzw_entry table[4096];
    uint8_t stack[4096];
    tf_msb_reader r = {src, src_size, 0, 0, 0};
    int code_size = 9, next_code = 258, old_code = -1;
    uint8_t old_first = 0;
    size_t out_pos = 0;
    for (int i = 0; i < 256; ++i) {
        table[i].prefix = -1;
        table[i].suffix = (uint8_t)i;
        table[i].length = 1;
    }
    while (out_pos < dst_size) {
        int code = tf_lzw_next_code(&r, code_size), cur, n = 0;
        uint8_t first;
        if (code < 0 || code == 257) break;
        if (code == 256) {
            code_size = 9;
            next_code = 258;
            old_code = -1;
            continue;
        }
        if (old_code < 0) {
            if (code > 255) return false;
            dst[out_pos++] = (uint8_t)code;
            old_code = code;
            old_first = (uint8_t)code;
            continue;
        }
        if (code > next_code || code >= 4096) return false;
        /* string of code (or of old_code + its first byte for KwKwK) */
        cur = code == next_code ? old_code : code;
        while (cur >= 0 && n < 4096) {
            stack[n++] = table[cur].suffix;
            cur = table[cur].prefix;
        }
        first = stack[n - 1];
        if (code == next_code) {
            /* the new string ends with its own first byte */
            for (int k = n; k > 0; --k) stack[k] = stack[k - 1];
            stack[0] = old_first;
            ++n;
            first = old_first;
        }
        while (n > 0 && out_pos < dst_size) dst[out_pos++] = stack[--n];
        if (next_code < 4096) {
            table[next_code].prefix = (int16_t)old_code;
            table[next_code].suffix = first;
            table[next_code].length = (uint16_t)(table[old_code].length + 1);
            ++next_code;
            if (next_code + 1 >= (1 << code_size) && code_size < 12) ++code_size;
        }
        old_code = code;
        old_first = first;
    }
    return true;
}

static void tf_apply_predictor(uint8_t *data, uint32_t w, uint32_t h, uint32_t spp, uint32_t bps,
                               uint32_t predictor, bool le)
{
    if (predictor != 2) return;
    if (bps == 8) {
        for (uint32_t y = 0; y < h; ++y) {
            uint8_t *row = data + (size_t)y * w * spp;
            for (size_t i = spp; i < (size_t)w * spp; ++i) {
                row[i] = (uint8_t)(row[i] + row[i - spp]);
            }
        }
    } else if (bps == 16) {
        for (uint32_t y = 0; y < h; ++y) {
            uint16_t *row = (uint16_t *)(data + (size_t)y * w * spp * 2);
            for (size_t i = spp; i < (size_t)w * spp; ++i) {
                uint16_t cur = tf_u16((const uint8_t *)&row[i], le);
                uint16_t prev = tf_u16((const uint8_t *)&row[i - spp], le);
                uint16_t res = (uint16_t)(cur + prev);
                if (le) {
                    row[i] = (uint16_t)res;
                } else {
                    row[i] = (uint16_t)((res >> 8) | (res << 8));
                }
            }
        }
    }
}

static xx_image_status tf_decompress_block(const uint8_t *src, size_t src_len, uint32_t comp,
                                        uint8_t *dst, size_t dst_len)
{
    if (comp == 1) {
        size_t cp = src_len < dst_len ? src_len : dst_len;
        memcpy(dst, src, cp);
        if (cp < dst_len) memset(dst + cp, 0, dst_len - cp);
        return XX_IMAGE_OK;
    } else if (comp == 32773) {
        return tf_packbits_decode(src, src_len, dst, dst_len) ? XX_IMAGE_OK : XX_IMAGE_ERR_CORRUPT;
    } else if (comp == 5) {
        return tf_lzw_decode(src, src_len, dst, dst_len) ? XX_IMAGE_OK : XX_IMAGE_ERR_CORRUPT;
    } else if (comp == 8 || comp == 32946) {
        uint8_t *out_buf = NULL;
        size_t out_size = 0;
        xx_image_status st = xx_image_zlib_inflate(src, src_len, dst_len, dst_len, &out_buf, &out_size);
        if (st != XX_IMAGE_OK) {
            st = xx_image_raw_inflate(src, src_len, dst_len, dst_len, &out_buf, &out_size);
        }
        if (st == XX_IMAGE_OK && out_buf) {
            size_t cp = out_size < dst_len ? out_size : dst_len;
            memcpy(dst, out_buf, cp);
            if (cp < dst_len) memset(dst + cp, 0, dst_len - cp);
            free(out_buf);
            return XX_IMAGE_OK;
        }
        free(out_buf);
        return XX_IMAGE_ERR_CORRUPT;
    }
    return XX_IMAGE_ERR_UNSUPPORTED;
}

static void tf_read_properties(const uint8_t *d, size_t n, bool le, const tf_tag *tags, size_t count,
                               xx_image *img, const char *grp)
{
    for (size_t i = 0; i < count; ++i) {
        const tf_tag *t = &tags[i];
        const char *name = NULL;
        switch (t->tag) {
        case 254: name = "NewSubfileType"; break;
        case 256: name = "ImageWidth"; break;
        case 257: name = "ImageLength"; break;
        case 258: name = "BitsPerSample"; break;
        case 259: name = "Compression"; break;
        case 262: name = "PhotometricInterpretation"; break;
        case 266: name = "FillOrder"; break;
        case 273: name = "StripOffsets"; break;
        case 277: name = "SamplesPerPixel"; break;
        case 278: name = "RowsPerStrip"; break;
        case 279: name = "StripByteCounts"; break;
        case 284: name = "PlanarConfiguration"; break;
        case 317: name = "Predictor"; break;
        case 320: name = "ColorMap"; break;
        case 322: name = "TileWidth"; break;
        case 323: name = "TileLength"; break;
        case 324: name = "TileOffsets"; break;
        case 325: name = "TileByteCounts"; break;
        case 338: name = "ExtraSamples"; break;
        case 339: name = "SampleFormat"; break;
        case 269: name = "DocumentName"; break;
        case 270: name = "ImageDescription"; break;
        case 271: name = "Make"; break;
        case 272: name = "Model"; break;
        case 274: name = "Orientation"; break;
        case 296: name = "ResolutionUnit"; break;
        case 305: name = "Software"; break;
        case 306: name = "DateTime"; break;
        case 315: name = "Artist"; break;
        case 316: name = "HostComputer"; break;
        case 33432: name = "Copyright"; break;
        default: break;
        }

        if (t->tag == 34675) {
            /* ICC Profile */
            if (xx_image_in_range(n, t->offset, t->count)) {
                xx_image_add_prop_bytes(img, "ICC", "profile", d + t->offset, t->count);
            }
            continue;
        }

        if (t->type == 2 && t->count > 0 && xx_image_in_range(n, t->offset, t->count)) {
            size_t slen = t->count;
            while (slen > 0 && d[t->offset + slen - 1] == '\0') slen--;
            if (name) {
                xx_image_add_prop_textn(img, grp, name, (const char *)(d + t->offset), slen);
            } else {
                char tag_key[32];
                snprintf(tag_key, sizeof(tag_key), "tag %u", t->tag);
                xx_image_add_prop_textn(img, grp, tag_key, (const char *)(d + t->offset), slen);
            }
        } else if (t->type == 5 && t->count >= 1 && xx_image_in_range(n, t->offset, 8)) {
            uint32_t num = tf_u32(d + t->offset, le);
            uint32_t den = tf_u32(d + t->offset + 4, le);
            double val = den ? (double)num / den : 0.0;
            if (t->tag == 282) {
                xx_image_add_prop_real(img, grp, "XResolution", val);
                img->info.dpi_x = val;
            } else if (t->tag == 283) {
                xx_image_add_prop_real(img, grp, "YResolution", val);
                img->info.dpi_y = val;
            }
        } else {
            uint32_t uval;
            if (tf_tag_uint(d, n, le, t, 0, &uval)) {
                if (name) {
                    xx_image_add_prop_uint(img, grp, name, uval);
                } else {
                    char tag_key[32];
                    snprintf(tag_key, sizeof(tag_key), "tag %u", t->tag);
                    xx_image_add_prop_uint(img, grp, tag_key, uval);
                }
            }
        }
    }
}

static xx_image_status tiff_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    bool le;
    uint16_t magic;
    uint32_t ifd_offset;
    xx_image_status st = XX_IMAGE_OK;
    const char *g = "TIFF";

    if (n < 8) return XX_IMAGE_ERR_FORMAT;
    if (xx_image_rd_u16le(d) == TIFF_MAGIC_LE) {
        le = true;
    } else if (xx_image_rd_u16le(d) == 0x4D4D) {
        le = false;
    } else {
        return XX_IMAGE_ERR_FORMAT;
    }

    magic = tf_u16(d + 2, le);
    if (magic == 43) return XX_IMAGE_ERR_UNSUPPORTED; /* BigTIFF */
    if (magic != 42) return XX_IMAGE_ERR_FORMAT;

    ifd_offset = tf_u32(d + 4, le);

    while (ifd_offset != 0) {
        uint16_t num_entries;
        tf_tag *tags = NULL;
        uint32_t w = 0, h = 0, comp = 1, photo = 1, spp = 1, bps = 1, planar = 1, pred = 1, extra = 0;
        uint32_t rps = 0;
        uint32_t tile_w = 0, tile_h = 0;
        const tf_tag *t_offsets, *t_counts;
        bool is_tiled;
        xx_image_frame *f;
        uint32_t pal[256];
        uint32_t pal_count = 0;

        if (!xx_image_in_range(n, ifd_offset, 2)) break;
        num_entries = tf_u16(d + ifd_offset, le);
        if (!xx_image_in_range(n, ifd_offset + 2, (size_t)num_entries * 12 + 4)) break;

        tags = (tf_tag *)calloc(num_entries, sizeof(tf_tag));
        if (!tags) return XX_IMAGE_ERR_MEMORY;

        for (uint16_t i = 0; i < num_entries; ++i) {
            size_t entry = ifd_offset + 2 + (size_t)i * 12;
            uint16_t tag_id = tf_u16(d + entry, le);
            uint16_t type = tf_u16(d + entry + 2, le);
            uint32_t count = tf_u32(d + entry + 4, le);
            size_t sz = tf_type_size(type);
            uint64_t total = (uint64_t)count * sz;

            tags[i].tag = tag_id;
            tags[i].type = type;
            tags[i].count = count;
            if (total <= 4) {
                tags[i].offset = (uint32_t)(entry + 8);
            } else {
                tags[i].offset = tf_u32(d + entry + 8, le);
            }
        }

        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 256), 0, &w);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 257), 0, &h);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 259), 0, &comp);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 262), 0, &photo);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 277), 0, &spp);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 284), 0, &planar);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 317), 0, &pred);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 338), 0, &extra);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 278), 0, &rps);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 322), 0, &tile_w);
        tf_tag_uint(d, n, le, tf_find_tag(tags, num_entries, 323), 0, &tile_h);

        {
            const tf_tag *t_bps = tf_find_tag(tags, num_entries, 258);
            if (t_bps) tf_tag_uint(d, n, le, t_bps, 0, &bps);
        }

        if (rps == 0 || rps > h) rps = h;

        st = xx_image_check_dimensions(w, h, o);
        if (st != XX_IMAGE_OK) {
            free(tags);
            return st;
        }

        is_tiled = (tile_w > 0 && tile_h > 0);
        if (is_tiled) {
            t_offsets = tf_find_tag(tags, num_entries, 324);
            t_counts = tf_find_tag(tags, num_entries, 325);
        } else {
            t_offsets = tf_find_tag(tags, num_entries, 273);
            t_counts = tf_find_tag(tags, num_entries, 279);
        }

        if (!t_offsets || !t_counts) {
            free(tags);
            return XX_IMAGE_ERR_CORRUPT;
        }

        /* Palette for Photometric 3 */
        for (uint32_t i = 0; i < 256; ++i) pal[i] = 0x000000FF;
        if (photo == 3) {
            const tf_tag *t_map = tf_find_tag(tags, num_entries, 320);
            if (t_map && t_map->count >= 3 * (1U << bps)) {
                pal_count = 1U << bps;
                if (pal_count > 256) pal_count = 256;
                for (uint32_t i = 0; i < pal_count; ++i) {
                    uint32_t r16, g16, b16;
                    tf_tag_uint(d, n, le, t_map, i, &r16);
                    tf_tag_uint(d, n, le, t_map, pal_count + i, &g16);
                    tf_tag_uint(d, n, le, t_map, 2 * pal_count + i, &b16);
                    pal[i] = ((uint32_t)(r16 >> 8) << 24) | ((uint32_t)(g16 >> 8) << 16) |
                             ((uint32_t)(b16 >> 8) << 8) | 0xFF;
                }
            }
        }

        f = xx_image_add_frame(img, w, h);
        if (!f) {
            free(tags);
            return XX_IMAGE_ERR_MEMORY;
        }

        if (is_tiled) {
            uint32_t tiles_across = (w + tile_w - 1) / tile_w;
            uint32_t tiles_down = (h + tile_h - 1) / tile_h;
            size_t tile_raw_size = ((size_t)tile_w * bps * spp + 7) / 8 * tile_h;
            uint8_t *tile_buf = (uint8_t *)malloc(tile_raw_size);

            if (!tile_buf) {
                free(tags);
                return XX_IMAGE_ERR_MEMORY;
            }

            for (uint32_t ty = 0; ty < tiles_down; ++ty) {
                for (uint32_t tx = 0; tx < tiles_across; ++tx) {
                    size_t t_idx = (size_t)ty * tiles_across + tx;
                    uint32_t toff = 0, tcnt = 0;
                    tf_tag_uint(d, n, le, t_offsets, t_idx, &toff);
                    tf_tag_uint(d, n, le, t_counts, t_idx, &tcnt);

                    if (!xx_image_in_range(n, toff, tcnt)) continue;

                    if (tf_decompress_block(d + toff, tcnt, comp, tile_buf, tile_raw_size) != XX_IMAGE_OK) {
                        continue;
                    }

                    tf_apply_predictor(tile_buf, tile_w, tile_h, spp, bps, pred, le);

                    /* Copy pixels to frame */
                    for (uint32_t py = 0; py < tile_h; ++py) {
                        uint32_t dy = ty * tile_h + py;
                        if (dy >= h) break;
                        for (uint32_t px = 0; px < tile_w; ++px) {
                            uint32_t dx = tx * tile_w + px;
                            uint8_t *dst = f->rgba + ((size_t)dy * w + dx) * 4;
                            if (dx >= w) break;

                            if (photo == 2) {
                                if (bps == 8) {
                                    const uint8_t *s = tile_buf + ((size_t)py * tile_w + px) * spp;
                                    dst[0] = s[0];
                                    dst[1] = s[1];
                                    dst[2] = s[2];
                                    dst[3] = (spp >= 4) ? s[3] : 255;
                                } else if (bps == 16) {
                                    const uint8_t *s = tile_buf + ((size_t)py * tile_w + px) * spp * 2;
                                    dst[0] = (uint8_t)(tf_u16(s, le) >> 8);
                                    dst[1] = (uint8_t)(tf_u16(s + 2, le) >> 8);
                                    dst[2] = (uint8_t)(tf_u16(s + 4, le) >> 8);
                                    dst[3] = (spp >= 4) ? (uint8_t)(tf_u16(s + 6, le) >> 8) : 255;
                                }
                                if (extra == 1 && dst[3] > 0 && dst[3] < 255) {
                                    dst[0] = (uint8_t)((dst[0] * 255) / dst[3]);
                                    dst[1] = (uint8_t)((dst[1] * 255) / dst[3]);
                                    dst[2] = (uint8_t)((dst[2] * 255) / dst[3]);
                                }
                            } else if (photo <= 1) {
                                uint8_t gray = 0;
                                if (bps == 8) {
                                    gray = tile_buf[((size_t)py * tile_w + px) * spp];
                                } else if (bps == 16) {
                                    gray = (uint8_t)(tf_u16(tile_buf + ((size_t)py * tile_w + px) * spp * 2, le) >> 8);
                                }
                                if (photo == 0) gray = (uint8_t)(255 - gray);
                                dst[0] = dst[1] = dst[2] = gray;
                                dst[3] = 255;
                            }
                        }
                    }
                }
            }
            free(tile_buf);
        } else if (planar == 2) {
            /* Planar strips (planar configuration 2) */
            uint32_t strips_per_sample = (h + rps - 1) / rps;
            size_t strip_row_bytes = ((size_t)w * bps + 7) / 8;
            size_t strip_raw_size = strip_row_bytes * rps;
            uint8_t *strip_buf = (uint8_t *)malloc(strip_raw_size);

            if (!strip_buf) {
                free(tags);
                return XX_IMAGE_ERR_MEMORY;
            }

            for (uint32_t c = 0; c < spp; ++c) {
                for (uint32_t s_idx = 0; s_idx < strips_per_sample; ++s_idx) {
                    size_t strip_total_idx = (size_t)c * strips_per_sample + s_idx;
                    uint32_t soff = 0, scnt = 0;
                    uint32_t start_y = s_idx * rps;
                    uint32_t rows_in_strip = (start_y + rps <= h) ? rps : (h - start_y);
                    size_t actual_raw_size = strip_row_bytes * rows_in_strip;

                    tf_tag_uint(d, n, le, t_offsets, strip_total_idx, &soff);
                    tf_tag_uint(d, n, le, t_counts, strip_total_idx, &scnt);

                    if (!xx_image_in_range(n, soff, scnt)) continue;

                    if (tf_decompress_block(d + soff, scnt, comp, strip_buf, actual_raw_size) != XX_IMAGE_OK) {
                        continue;
                    }

                    tf_apply_predictor(strip_buf, w, rows_in_strip, 1, bps, pred, le);

                    for (uint32_t ry = 0; ry < rows_in_strip; ++ry) {
                        uint32_t y = start_y + ry;
                        uint8_t *dst = f->rgba + (size_t)y * w * 4;
                        const uint8_t *srow = strip_buf + (size_t)ry * strip_row_bytes;

                        for (uint32_t x = 0; x < w; ++x) {
                            uint8_t *px = dst + (size_t)x * 4;
                            uint8_t val = (bps == 8) ? srow[x] : (uint8_t)(tf_u16(srow + (size_t)x * 2, le) >> 8);
                            if (photo == 2) {
                                if (c < 3) px[c] = val;
                                else px[3] = val;
                                if (spp == 3) px[3] = 255;
                            } else if (photo <= 1) {
                                if (photo == 0) val = (uint8_t)(255 - val);
                                if (c == 0) px[0] = px[1] = px[2] = val;
                                else px[3] = val;
                                if (spp == 1) px[3] = 255;
                            }
                        }
                    }
                }
            }

            if (photo == 2 && extra == 1 && spp >= 4) {
                for (size_t i = 0; i < (size_t)w * h; ++i) {
                    uint8_t *px = f->rgba + i * 4;
                    if (px[3] > 0 && px[3] < 255) {
                        px[0] = (uint8_t)((px[0] * 255) / px[3]);
                        px[1] = (uint8_t)((px[1] * 255) / px[3]);
                        px[2] = (uint8_t)((px[2] * 255) / px[3]);
                    }
                }
            }
            free(strip_buf);
        } else {
            /* Strips */
            uint32_t num_strips = (h + rps - 1) / rps;
            size_t strip_row_bytes = ((size_t)w * bps * spp + 7) / 8;
            size_t strip_raw_size = strip_row_bytes * rps;
            uint8_t *strip_buf = (uint8_t *)malloc(strip_raw_size);

            if (!strip_buf) {
                free(tags);
                return XX_IMAGE_ERR_MEMORY;
            }

            for (uint32_t s_idx = 0; s_idx < num_strips; ++s_idx) {
                uint32_t soff = 0, scnt = 0;
                uint32_t start_y = s_idx * rps;
                uint32_t rows_in_strip = (start_y + rps <= h) ? rps : (h - start_y);
                size_t actual_raw_size = strip_row_bytes * rows_in_strip;

                tf_tag_uint(d, n, le, t_offsets, s_idx, &soff);
                tf_tag_uint(d, n, le, t_counts, s_idx, &scnt);

                if (!xx_image_in_range(n, soff, scnt)) continue;

                if (tf_decompress_block(d + soff, scnt, comp, strip_buf, actual_raw_size) != XX_IMAGE_OK) {
                    continue;
                }

                tf_apply_predictor(strip_buf, w, rows_in_strip, spp, bps, pred, le);

                for (uint32_t ry = 0; ry < rows_in_strip; ++ry) {
                    uint32_t y = start_y + ry;
                    uint8_t *dst = f->rgba + (size_t)y * w * 4;
                    const uint8_t *srow = strip_buf + (size_t)ry * strip_row_bytes;

                    for (uint32_t x = 0; x < w; ++x) {
                        uint8_t *px = dst + (size_t)x * 4;
                        if (photo == 2) {
                            if (bps == 8) {
                                const uint8_t *p = srow + (size_t)x * spp;
                                px[0] = p[0];
                                px[1] = p[1];
                                px[2] = p[2];
                                px[3] = (spp >= 4) ? p[3] : 255;
                            } else if (bps == 16) {
                                const uint8_t *p = srow + (size_t)x * spp * 2;
                                px[0] = (uint8_t)(tf_u16(p, le) >> 8);
                                px[1] = (uint8_t)(tf_u16(p + 2, le) >> 8);
                                px[2] = (uint8_t)(tf_u16(p + 4, le) >> 8);
                                px[3] = (spp >= 4) ? (uint8_t)(tf_u16(p + 6, le) >> 8) : 255;
                            }
                            if (extra == 1 && px[3] > 0 && px[3] < 255) {
                                px[0] = (uint8_t)((px[0] * 255) / px[3]);
                                px[1] = (uint8_t)((px[1] * 255) / px[3]);
                                px[2] = (uint8_t)((px[2] * 255) / px[3]);
                            }
                        } else if (photo == 3) {
                            uint8_t idx = 0;
                            if (bps == 8) {
                                idx = srow[x];
                            } else if (bps == 4) {
                                idx = (x & 1) ? (srow[x / 2] & 0x0F) : (srow[x / 2] >> 4);
                            } else if (bps == 1) {
                                idx = (uint8_t)((srow[x / 8] >> (7 - (x % 8))) & 1);
                            }
                            xx_image_put_rgba(px, pal[idx]);
                        } else if (photo <= 1) {
                            uint8_t gray = 0;
                            if (bps == 8) {
                                gray = srow[x * spp];
                            } else if (bps == 16) {
                                gray = (uint8_t)(tf_u16(srow + x * spp * 2, le) >> 8);
                            } else if (bps == 4) {
                                uint8_t nib = (x & 1) ? (srow[x / 2] & 0x0F) : (srow[x / 2] >> 4);
                                gray = (uint8_t)(nib * 17);
                            } else if (bps == 1) {
                                uint8_t bit = (uint8_t)((srow[x / 8] >> (7 - (x % 8))) & 1);
                                gray = bit ? 255 : 0;
                            }
                            if (photo == 0) gray = (uint8_t)(255 - gray);
                            px[0] = px[1] = px[2] = gray;
                            px[3] = (spp >= 2) ? srow[x * spp + 1] : 255;
                        }
                    }
                }
            }
            free(strip_buf);
        }

        if (img->frame_count == 1) {
            img->info.bits_per_sample = bps;
            img->info.channels = spp;
            img->info.bits_per_pixel = bps * spp;
            img->info.has_alpha = (spp == 4 || (spp == 2 && photo <= 1));
            img->info.color_model = (photo == 2) ? (img->info.has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB) :
                                   (photo == 3) ? XX_IMAGE_COLOR_INDEXED :
                                   (bps == 1) ? XX_IMAGE_COLOR_BILEVEL : XX_IMAGE_COLOR_GRAY;
            XX_IMAGE_SET_STR(img->info.compression, comp == 5 ? "LZW" : comp == 32773 ? "PackBits" : (comp == 8 || comp == 32946) ? "Deflate" : "none");

            if (!o->skip_metadata) {
                tf_read_properties(d, n, le, tags, num_entries, img, g);

                /* Check for EXIF sub-IFD */
                {
                    const tf_tag *t_exif = tf_find_tag(tags, num_entries, 34665);
                    uint32_t exif_off = 0;
                    if (t_exif && tf_tag_uint(d, n, le, t_exif, 0, &exif_off) && exif_off > 0 && xx_image_in_range(n, exif_off, 2)) {
                        uint16_t exif_cnt = tf_u16(d + exif_off, le);
                        if (xx_image_in_range(n, exif_off + 2, (size_t)exif_cnt * 12)) {
                            tf_tag *exif_tags = (tf_tag *)calloc(exif_cnt, sizeof(tf_tag));
                            if (exif_tags) {
                                for (uint16_t ei = 0; ei < exif_cnt; ++ei) {
                                    size_t eent = exif_off + 2 + (size_t)ei * 12;
                                    exif_tags[ei].tag = tf_u16(d + eent, le);
                                    exif_tags[ei].type = tf_u16(d + eent + 2, le);
                                    exif_tags[ei].count = tf_u32(d + eent + 4, le);
                                    if ((uint64_t)exif_tags[ei].count * tf_type_size(exif_tags[ei].type) <= 4) {
                                        exif_tags[ei].offset = (uint32_t)(eent + 8);
                                    } else {
                                        exif_tags[ei].offset = tf_u32(d + eent + 8, le);
                                    }
                                }
                                tf_read_properties(d, n, le, exif_tags, exif_cnt, img, "EXIF");
                                free(exif_tags);
                            }
                        }
                    }
                }
            }
        }

        /* Next IFD */
        ifd_offset = tf_u32(d + ifd_offset + 2 + (size_t)num_entries * 12, le);
        free(tags);

        if (o->first_frame_only) break;
    }

    img->info.frame_count = (uint32_t)img->frame_count;
    return (img->frame_count > 0) ? XX_IMAGE_OK : XX_IMAGE_ERR_CORRUPT;
}

static xx_image_status tiff_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    size_t first_idx = 0;
    size_t count = 1;
    bool all = (o && o->all_frames);
    size_t start_off = out->size;
    uint32_t last_next_ifd_pos = 0;

    if (!img || img->frame_count == 0) return XX_IMAGE_ERR_ARGUMENT;

    if (all) {
        first_idx = 0;
        count = img->frame_count;
    } else {
        first_idx = (o && o->frame < img->frame_count) ? o->frame : 0;
        count = 1;
    }

    /* 8-byte TIFF Little-Endian Header */
    xx_image_buf_u16le(out, TIFF_MAGIC_LE); /* "II" */
    xx_image_buf_u16le(out, 42);             /* Magic 42 */
    xx_image_buf_u32le(out, 8);              /* First IFD at offset 8 */

    for (size_t fi = 0; fi < count; ++fi) {
        const xx_image_frame *f = &img->frames[first_idx + fi];
        uint32_t w = f->width;
        uint32_t h = f->height;
        bool has_alpha = xx_image_frame_has_alpha(f);
        uint32_t spp = has_alpha ? 4 : 3;
        size_t row_bytes = (size_t)w * spp;
        uint32_t rps = (uint32_t)(65536 / row_bytes);
        uint32_t num_strips;
        uint32_t *strip_offsets;
        uint32_t *strip_counts;
        size_t ifd_pos, next_ifd_pos;
        uint16_t num_entries = 12 + (has_alpha ? 1 : 0);
        int level = o ? o->level : -1;
        bool deflate = (level > 0);

        if (w == 0 || h == 0 || (uint64_t)w * h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;
        if (rps == 0) rps = 1;
        if (rps > h) rps = h;
        num_strips = (h + rps - 1) / rps;

        strip_offsets = (uint32_t *)calloc(num_strips, sizeof(uint32_t));
        strip_counts = (uint32_t *)calloc(num_strips, sizeof(uint32_t));
        if (!strip_offsets || !strip_counts) {
            free(strip_offsets);
            free(strip_counts);
            return XX_IMAGE_ERR_MEMORY;
        }

        /* Write IFD */
        ifd_pos = out->size;
        if (last_next_ifd_pos != 0) {
            xx_image_buf_put_u32le_at(out, last_next_ifd_pos, (uint32_t)(ifd_pos - start_off));
        }

        xx_image_buf_u16le(out, num_entries);
        /* Reserve 12 bytes per tag entry */
        xx_image_buf_fill(out, 0, (size_t)num_entries * 12);
        next_ifd_pos = out->size;
        xx_image_buf_u32le(out, 0); /* Patched if next frame exists */
        last_next_ifd_pos = (uint32_t)next_ifd_pos;

        /* Write strips */
        for (uint32_t s = 0; s < num_strips; ++s) {
            uint32_t sy = s * rps;
            uint32_t nrows = (sy + rps <= h) ? rps : (h - sy);
            size_t uncompressed_size = row_bytes * nrows;
            uint8_t *raw_strip = (uint8_t *)malloc(uncompressed_size);

            if (!raw_strip) {
                free(strip_offsets);
                free(strip_counts);
                return XX_IMAGE_ERR_MEMORY;
            }

            for (uint32_t ry = 0; ry < nrows; ++ry) {
                const uint8_t *src = f->rgba + ((size_t)(sy + ry) * w) * 4;
                uint8_t *dst = raw_strip + (size_t)ry * row_bytes;
                for (uint32_t x = 0; x < w; ++x) {
                    dst[x * spp]     = src[x * 4];
                    dst[x * spp + 1] = src[x * 4 + 1];
                    dst[x * spp + 2] = src[x * 4 + 2];
                    if (has_alpha) dst[x * spp + 3] = src[x * 4 + 3];
                }
            }

            strip_offsets[s] = (uint32_t)(out->size - start_off);

            if (deflate) {
                size_t strip_before = out->size;
                xx_image_status def_st = xx_image_zlib_deflate(raw_strip, uncompressed_size, level, out);
                if (def_st != XX_IMAGE_OK) {
                    free(raw_strip);
                    free(strip_offsets);
                    free(strip_counts);
                    return def_st;
                }
                strip_counts[s] = (uint32_t)(out->size - strip_before);
            } else {
                xx_image_buf_append(out, raw_strip, uncompressed_size);
                strip_counts[s] = (uint32_t)uncompressed_size;
            }

            free(raw_strip);
        }

        /* Write auxiliary tag data (BitsPerSample, StripOffsets, StripByteCounts, X/YResolution) */
        {
            size_t bps_off = out->size - start_off;
            size_t offsets_off, counts_off, res_off;
            size_t e = ifd_pos + 2;

            for (uint32_t k = 0; k < spp; ++k) xx_image_buf_u16le(out, 8);

            offsets_off = out->size - start_off;
            if (num_strips > 1) {
                for (uint32_t k = 0; k < num_strips; ++k) xx_image_buf_u32le(out, strip_offsets[k]);
            }

            counts_off = out->size - start_off;
            if (num_strips > 1) {
                for (uint32_t k = 0; k < num_strips; ++k) xx_image_buf_u32le(out, strip_counts[k]);
            }

            res_off = out->size - start_off;
            xx_image_buf_u32le(out, 72); /* 72 dpi */
            xx_image_buf_u32le(out, 1);

            /* Helper macro to patch a 12-byte tag entry */
#define WRITE_ENTRY(tag, type, cnt, val_or_off) do { \
    xx_image_buf_put_u32le_at(out, e, (uint32_t)((tag) | ((type) << 16))); \
    xx_image_buf_put_u32le_at(out, e + 4, (uint32_t)(cnt)); \
    xx_image_buf_put_u32le_at(out, e + 8, (uint32_t)(val_or_off)); \
    e += 12; \
} while(0)

            WRITE_ENTRY(256, 4, 1, w);                      /* ImageWidth */
            WRITE_ENTRY(257, 4, 1, h);                      /* ImageLength */
            WRITE_ENTRY(258, 3, spp, bps_off);              /* BitsPerSample */
            WRITE_ENTRY(259, 3, 1, deflate ? 8 : 1);        /* Compression */
            WRITE_ENTRY(262, 3, 1, 2);                      /* Photometric: RGB */
            WRITE_ENTRY(273, 4, num_strips, num_strips == 1 ? strip_offsets[0] : offsets_off); /* StripOffsets */
            WRITE_ENTRY(277, 3, 1, spp);                    /* SamplesPerPixel */
            WRITE_ENTRY(278, 4, 1, rps);                    /* RowsPerStrip */
            WRITE_ENTRY(279, 4, num_strips, num_strips == 1 ? strip_counts[0] : counts_off);   /* StripByteCounts */
            WRITE_ENTRY(282, 5, 1, res_off);                /* XResolution */
            WRITE_ENTRY(283, 5, 1, res_off);                /* YResolution */
            WRITE_ENTRY(296, 3, 1, 2);                      /* ResolutionUnit: Inch */
            if (has_alpha) {
                WRITE_ENTRY(338, 3, 1, 2);                  /* ExtraSamples: Unassociated alpha */
            }
#undef WRITE_ENTRY
        }

        free(strip_offsets);
        free(strip_counts);
    }

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_tiff = {
    "tiff",
    "Tagged Image File Format",
    "tif;tiff",
    "image/tiff",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_MULTIFRAME | XX_IMAGE_CODEC_INFO,
    593,
    tiff_probe,
    tiff_load,
    tiff_save
};
