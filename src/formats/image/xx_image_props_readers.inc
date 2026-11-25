/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Abstract image interface readers for additional raster and vector formats.
 */

/* ---- additional helpers ------------------------------------------------- */

static uint64_t ip_u64le(const uint8_t *p)
{
    return (uint64_t)ip_u32le(p) | ((uint64_t)ip_u32le(p + 4) << 32);
}

static uint64_t ip_u64be(const uint8_t *p)
{
    return ((uint64_t)ip_u32be(p) << 32) | (uint64_t)ip_u32be(p + 4);
}

static bool ip_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool ip_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static void ip_skip_space(const char *buf, size_t len, size_t *pos)
{
    while (*pos < len && ip_is_space(buf[*pos])) (*pos)++;
}

static bool ip_parse_u32(const char *buf, size_t len, size_t *pos, uint32_t *val)
{
    uint64_t v = 0;
    bool found = false;
    ip_skip_space(buf, len, pos);
    while (*pos < len && ip_is_digit(buf[*pos])) {
        found = true;
        v = v * 10 + (uint32_t)(buf[*pos] - '0');
        if (v > 0xFFFFFFFFU) v = 0xFFFFFFFFU;
        (*pos)++;
    }
    if (found) *val = (uint32_t)v;
    return found;
}

static bool ip_read_bpg_ue(const uint8_t *buf, size_t cap, size_t *pos, uint32_t *val)
{
    uint32_t v = 0;
    int shift = 0;
    while (*pos < cap && shift <= 28) {
        uint8_t b = buf[(*pos)++];
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *val = v;
            return true;
        }
        shift += 7;
    }
    return false;
}

/* ---- TIFF --------------------------------------------------------------- */

static bool ip_read_tiff(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    bool is_be;
    bool is_big;
    int64_t ifd_offset;
    uint32_t ifd_count = 0;
    if (!ip_read(dev, base, h, 8)) return false;
    if (h[0] == 'I' && h[1] == 'I') is_be = false;
    else if (h[0] == 'M' && h[1] == 'M') is_be = true;
    else return false;
    {
        uint16_t ver = is_be ? ip_u16be(h + 2) : ip_u16le(h + 2);
        if (ver == 42) {
            is_big = false;
            ifd_offset = is_be ? (int64_t)ip_u32be(h + 4) : (int64_t)ip_u32le(h + 4);
        } else if (ver == 43) {
            is_big = true;
            if (is_be ? ip_u16be(h + 4) != 8 : ip_u16le(h + 4) != 8) return false;
            if (!ip_read(dev, base + 8, h + 8, 8)) return false;
            ifd_offset = (int64_t)(is_be ? ip_u64be(h + 8) : ip_u64le(h + 8));
        } else return false;
    }
    p->is_top_down = true;
    p->frame_count = 1;
    IP_SET(p->compression, "none");
    IP_SET(p->color_space, "sRGB");

    while (ifd_offset > 0 && ifd_count < 100000) {
        uint64_t num_entries = 0;
        int64_t entry_start;
        uint32_t entry_size = is_big ? 20 : 12;
        ++ifd_count;
        if (is_big) {
            uint8_t cnt[8];
            if (!ip_read(dev, base + ifd_offset, cnt, 8)) break;
            num_entries = is_be ? ip_u64be(cnt) : ip_u64le(cnt);
            entry_start = ifd_offset + 8;
        } else {
            uint8_t cnt[2];
            if (!ip_read(dev, base + ifd_offset, cnt, 2)) break;
            num_entries = is_be ? ip_u16be(cnt) : ip_u16le(cnt);
            entry_start = ifd_offset + 2;
        }
        if (num_entries > 4096) num_entries = 4096;

        for (uint64_t i = 0; i < num_entries; ++i) {
            uint8_t e[20];
            uint16_t tag, type;
            uint64_t count, val_or_off;
            if (!ip_read(dev, base + entry_start + (int64_t)(i * entry_size), e, entry_size)) break;
            if (is_big) {
                tag = is_be ? ip_u16be(e) : ip_u16le(e);
                type = is_be ? ip_u16be(e + 2) : ip_u16le(e + 2);
                count = is_be ? ip_u64be(e + 4) : ip_u64le(e + 4);
                val_or_off = is_be ? ip_u64be(e + 12) : ip_u64le(e + 12);
            } else {
                tag = is_be ? ip_u16be(e) : ip_u16le(e);
                type = is_be ? ip_u16be(e + 2) : ip_u16le(e + 2);
                count = is_be ? ip_u32be(e + 4) : ip_u32le(e + 4);
                val_or_off = is_be ? ip_u32be(e + 8) : ip_u32le(e + 8);
            }

            if (ifd_count == 1) {
                if (tag == 256) { /* ImageWidth */
                    p->width = (uint32_t)val_or_off;
                } else if (tag == 257) { /* ImageLength */
                    p->height = (uint32_t)val_or_off;
                } else if (tag == 258) { /* BitsPerSample */
                    if (count == 1) {
                        p->bits_per_sample = (uint32_t)val_or_off;
                    } else if (count > 1) {
                        uint8_t d[2];
                        if (ip_read(dev, base + (int64_t)val_or_off, d, 2)) {
                            p->bits_per_sample = is_be ? ip_u16be(d) : ip_u16le(d);
                        }
                    }
                } else if (tag == 259) { /* Compression */
                    uint32_t comp = (uint32_t)val_or_off;
                    switch (comp) {
                    case 1: IP_SET(p->compression, "none"); break;
                    case 2: IP_SET(p->compression, "CCITT 1D"); p->is_compressed = true; break;
                    case 3: IP_SET(p->compression, "CCITT Group 3"); p->is_compressed = true; break;
                    case 4: IP_SET(p->compression, "CCITT Group 4"); p->is_compressed = true; break;
                    case 5: IP_SET(p->compression, "LZW"); p->is_compressed = true; break;
                    case 6: case 7: IP_SET(p->compression, "JPEG"); p->is_compressed = true; p->is_lossy = true; break;
                    case 8: case 32946: IP_SET(p->compression, "deflate"); p->is_compressed = true; break;
                    case 32773: IP_SET(p->compression, "PackBits"); p->is_compressed = true; break;
                    default: IP_SET(p->compression, "unknown"); p->is_compressed = true; break;
                    }
                } else if (tag == 262) { /* PhotometricInterpretation */
                    uint32_t photo = (uint32_t)val_or_off;
                    switch (photo) {
                    case 0: case 1: p->color_model = (p->bits_per_sample == 1) ? XX_IMAGE_COLOR_BILEVEL : XX_IMAGE_COLOR_GRAY; break;
                    case 2: p->color_model = XX_IMAGE_COLOR_RGB; break;
                    case 3: p->color_model = XX_IMAGE_COLOR_INDEXED; break;
                    case 5: p->color_model = XX_IMAGE_COLOR_CMYK; break;
                    case 6: p->color_model = XX_IMAGE_COLOR_YCBCR; break;
                    case 8: p->color_model = XX_IMAGE_COLOR_LAB; break;
                    default: break;
                    }
                } else if (tag == 273 || tag == 324) { /* StripOffsets / TileOffsets */
                    if (count == 1) p->pixel_data_offset = (int64_t)val_or_off;
                    else if (count > 1) {
                        uint8_t d[8];
                        if (is_big) {
                            if (ip_read(dev, base + (int64_t)val_or_off, d, 8))
                                p->pixel_data_offset = (int64_t)(is_be ? ip_u64be(d) : ip_u64le(d));
                        } else {
                            if (ip_read(dev, base + (int64_t)val_or_off, d, 4))
                                p->pixel_data_offset = (int64_t)(is_be ? ip_u32be(d) : ip_u32le(d));
                        }
                    }
                } else if (tag == 277) { /* SamplesPerPixel */
                    p->channels = (uint32_t)val_or_off;
                } else if (tag == 279 && count == 1) { /* StripByteCounts */
                    p->pixel_data_size = (int64_t)val_or_off;
                } else if (tag == 282 || tag == 283) { /* XResolution / YResolution */
                    uint8_t rat[8];
                    if (ip_read(dev, base + (int64_t)val_or_off, rat, 8)) {
                        uint32_t num = is_be ? ip_u32be(rat) : ip_u32le(rat);
                        uint32_t den = is_be ? ip_u32be(rat + 4) : ip_u32le(rat + 4);
                        if (den) {
                            double dpi = (double)num / (double)den;
                            if (tag == 282) p->dpi_x = dpi; else p->dpi_y = dpi;
                        }
                    }
                } else if (tag == 320) { /* ColorMap */
                    p->palette_entries = (uint32_t)(count / 3);
                }
            }
            (void)type;
        }

        /* Read offset to next IFD */
        if (is_big) {
            uint8_t nxt[8];
            if (!ip_read(dev, base + entry_start + (int64_t)(num_entries * entry_size), nxt, 8)) break;
            ifd_offset = (int64_t)(is_be ? ip_u64be(nxt) : ip_u64le(nxt));
        } else {
            uint8_t nxt[4];
            if (!ip_read(dev, base + entry_start + (int64_t)(num_entries * entry_size), nxt, 4)) break;
            ifd_offset = (int64_t)(is_be ? ip_u32be(nxt) : ip_u32le(nxt));
        }
    }

    p->frame_count = ifd_count ? ifd_count : 1;
    if (p->channels == 0) p->channels = (p->color_model == XX_IMAGE_COLOR_RGB) ? 3 : 1;
    if (p->bits_per_sample == 0) p->bits_per_sample = 8;
    p->bits_per_pixel = p->channels * p->bits_per_sample;
    if (p->channels == 4 && p->color_model == XX_IMAGE_COLOR_RGB) {
        p->color_model = XX_IMAGE_COLOR_RGBA;
        p->has_alpha = true;
    }
    return p->width && p->height;
}

/* ---- ICO / CUR ---------------------------------------------------------- */

static bool ip_read_ico(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[6 + 16];
    uint16_t count, type;
    uint32_t w, height, bpp, off, sz;
    if (!ip_read(dev, base, h, 6 + 16)) return false;
    if (h[0] != 0 || h[1] != 0) return false;
    type = ip_u16le(h + 2);
    if (type != 1 && type != 2) return false;
    count = ip_u16le(h + 4);
    if (count == 0) return false;
    w = h[6] ? h[6] : 256;
    height = h[7] ? h[7] : 256;
    bpp = ip_u16le(h + 12);
    sz = ip_u32le(h + 14);
    off = ip_u32le(h + 18);

    /* Check if the icon entry contains a PNG */
    {
        uint8_t png_sig[8];
        if (ip_read(dev, base + (int64_t)off, png_sig, 8) && xx_rt_memcmp(png_sig, "\x89PNG\r\n\x1a\n", 8) == 0) {
            uint8_t ihdr[13];
            if (ip_read(dev, base + (int64_t)off + 16, ihdr, 13)) {
                w = ip_u32be(ihdr);
                height = ip_u32be(ihdr + 4);
                bpp = ihdr[8];
                p->is_compressed = true;
                IP_SET(p->compression, "deflate");
            }
        } else {
            /* May contain BITMAPINFOHEADER */
            uint8_t dib[16];
            if (ip_read(dev, base + (int64_t)off, dib, 16)) {
                uint32_t dib_w = ip_u32le(dib + 4);
                uint32_t dib_h = ip_u32le(dib + 8) / 2;
                uint16_t dib_bpp = ip_u16le(dib + 14);
                if (dib_w) w = dib_w;
                if (dib_h) height = dib_h;
                if (dib_bpp) bpp = dib_bpp;
            }
        }
    }

    p->width = w;
    p->height = height;
    p->bits_per_pixel = bpp ? bpp : 32;
    p->bits_per_sample = 8;
    p->channels = 4;
    p->color_model = XX_IMAGE_COLOR_MULTI;
    p->has_alpha = true;
    p->frame_count = count;
    p->pixel_data_offset = off;
    p->pixel_data_size = sz;
    IP_SET(p->version, type == 2 ? "CUR" : "ICO");
    return p->width && p->height;
}

/* ---- ICNS --------------------------------------------------------------- */

static bool ip_read_icns(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[8];
    uint32_t file_len, off = 8;
    uint32_t max_w = 0, max_h = 0, icons = 0;
    if (!ip_read(dev, base, h, 8) || xx_rt_memcmp(h, "icns", 4) != 0) return false;
    file_len = ip_u32be(h + 4);
    if (file_len < 8) return false;

    while (off + 8 <= file_len && icons < 1000) {
        uint8_t eh[8];
        uint32_t elen;
        uint32_t ew = 0, ehgt = 0;
        if (!ip_read(dev, base + (int64_t)off, eh, 8)) break;
        elen = ip_u32be(eh + 4);
        if (elen < 8 || off + elen > file_len) break;
        if (xx_rt_memcmp(eh, "ICON", 4) == 0 || xx_rt_memcmp(eh, "ICN#", 4) == 0) { ew = 32; ehgt = 32; }
        else if (xx_rt_memcmp(eh, "ics#", 4) == 0 || xx_rt_memcmp(eh, "ics4", 4) == 0 || xx_rt_memcmp(eh, "ics8", 4) == 0 || xx_rt_memcmp(eh, "is32", 4) == 0) { ew = 16; ehgt = 16; }
        else if (xx_rt_memcmp(eh, "icm#", 4) == 0 || xx_rt_memcmp(eh, "icm4", 4) == 0 || xx_rt_memcmp(eh, "icm8", 4) == 0) { ew = 16; ehgt = 12; }
        else if (xx_rt_memcmp(eh, "il32", 4) == 0) { ew = 32; ehgt = 32; }
        else if (xx_rt_memcmp(eh, "ih32", 4) == 0) { ew = 48; ehgt = 48; }
        else if (xx_rt_memcmp(eh, "it32", 4) == 0) { ew = 128; ehgt = 128; }
        else if (xx_rt_memcmp(eh, "icp4", 4) == 0) { ew = 16; ehgt = 16; }
        else if (xx_rt_memcmp(eh, "icp5", 4) == 0) { ew = 32; ehgt = 32; }
        else if (xx_rt_memcmp(eh, "icp6", 4) == 0) { ew = 64; ehgt = 64; }
        else if (xx_rt_memcmp(eh, "ic07", 4) == 0) { ew = 128; ehgt = 128; }
        else if (xx_rt_memcmp(eh, "ic08", 4) == 0) { ew = 256; ehgt = 256; }
        else if (xx_rt_memcmp(eh, "ic09", 4) == 0) { ew = 512; ehgt = 512; }
        else if (xx_rt_memcmp(eh, "ic10", 4) == 0) { ew = 1024; ehgt = 1024; }
        if (ew > max_w) { max_w = ew; max_h = ehgt; }
        ++icons;
        off += elen;
    }

    if (max_w == 0) { max_w = 128; max_h = 128; }
    p->width = max_w;
    p->height = max_h;
    p->bits_per_pixel = 32;
    p->bits_per_sample = 8;
    p->channels = 4;
    p->color_model = XX_IMAGE_COLOR_MULTI;
    p->has_alpha = true;
    p->frame_count = icons ? icons : 1;
    p->pixel_data_offset = 8;
    p->pixel_data_size = file_len - 8;
    return true;
}

/* ---- Windows Animated Cursor (ANI) -------------------------------------- */

static bool ip_read_windows_ani(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[12];
    int64_t off = base + 12;
    uint32_t riff_len;
    if (!ip_read(dev, base, h, 12)) return false;
    if (xx_rt_memcmp(h, "RIFF", 4) != 0 || xx_rt_memcmp(h + 8, "ACON", 4) != 0) return false;
    riff_len = ip_u32le(h + 4);
    p->is_animated = true;
    p->has_alpha = true;
    p->color_model = XX_IMAGE_COLOR_RGBA;
    p->channels = 4;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 32;

    for (int guard = 0; guard < 1000; ++guard) {
        uint8_t ch[8];
        uint32_t len;
        if (off >= base + 8 + (int64_t)riff_len || !ip_read(dev, off, ch, 8)) break;
        len = ip_u32le(ch + 4);
        if (xx_rt_memcmp(ch, "anih", 4) == 0 && len >= 36) {
            uint8_t anih[36];
            if (ip_read(dev, off + 8, anih, 36)) {
                p->frame_count = ip_u32le(anih + 4);
                p->width = ip_u32le(anih + 12);
                p->height = ip_u32le(anih + 16);
                p->bits_per_pixel = ip_u32le(anih + 20);
                if (p->bits_per_pixel == 0) p->bits_per_pixel = 32;
            }
        }
        off += 8 + ((int64_t)len + (len & 1));
    }
    if (p->width == 0 || p->height == 0) { p->width = 32; p->height = 32; }
    if (p->frame_count == 0) p->frame_count = 1;
    return true;
}

/* ---- XCursor ------------------------------------------------------------ */

static bool ip_read_xcursor(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    uint32_t ntoc;
    if (!ip_read(dev, base, h, 16) || xx_rt_memcmp(h, "Xcur", 4) != 0) return false;
    ntoc = ip_u32le(h + 12);
    if (ntoc == 0 || ntoc > 1000) return false;

    for (uint32_t i = 0; i < ntoc; ++i) {
        uint8_t toc[12];
        if (!ip_read(dev, base + 16 + (int64_t)i * 12, toc, 12)) break;
        if (ip_u32le(toc) == 0xFFFD0002U) { /* IMAGE type */
            uint32_t pos = ip_u32le(toc + 8);
            uint8_t ih[24];
            if (ip_read(dev, base + (int64_t)pos, ih, 24)) {
                p->width = ip_u32le(ih + 16);
                p->height = ip_u32le(ih + 20);
                p->pixel_data_offset = (int64_t)pos + 36;
                p->pixel_data_size = (int64_t)p->width * p->height * 4;
                p->bits_per_pixel = 32;
                p->bits_per_sample = 8;
                p->channels = 4;
                p->color_model = XX_IMAGE_COLOR_RGBA;
                p->has_alpha = true;
                p->frame_count = ntoc;
                p->is_top_down = true;
                return true;
            }
        }
    }
    return false;
}

/* ---- Photoshop PSD / PSB ------------------------------------------------ */

static bool ip_read_photoshop_psd(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[26];
    uint16_t ver, channels, depth, mode;
    uint32_t height, width;
    if (!ip_read(dev, base, h, 26) || xx_rt_memcmp(h, "8BPS", 4) != 0) return false;
    ver = ip_u16be(h + 4);
    if (ver != 1 && ver != 2) return false;
    channels = ip_u16be(h + 12);
    height = ip_u32be(h + 14);
    width = ip_u32be(h + 18);
    depth = ip_u16be(h + 22);
    mode = ip_u16be(h + 24);
    if (width == 0 || height == 0 || channels == 0) return false;

    p->width = width;
    p->height = height;
    p->channels = channels;
    p->bits_per_sample = depth;
    p->bits_per_pixel = channels * depth;
    p->frame_count = 1;
    p->is_top_down = true;
    IP_SET(p->version, ver == 2 ? "PSB" : "PSD");

    switch (mode) {
    case 0: p->color_model = XX_IMAGE_COLOR_BILEVEL; break;
    case 1: p->color_model = (channels > 1) ? XX_IMAGE_COLOR_GRAY_ALPHA : XX_IMAGE_COLOR_GRAY; if (channels > 1) p->has_alpha = true; break;
    case 2: p->color_model = XX_IMAGE_COLOR_INDEXED; break;
    case 3:
        if (channels >= 4) { p->color_model = XX_IMAGE_COLOR_RGBA; p->has_alpha = true; }
        else p->color_model = XX_IMAGE_COLOR_RGB;
        break;
    case 4: p->color_model = XX_IMAGE_COLOR_CMYK; break;
    case 9: p->color_model = XX_IMAGE_COLOR_LAB; break;
    default: p->color_model = XX_IMAGE_COLOR_RGB; break;
    }
    return true;
}

/* ---- DirectDraw Surface (DDS) ------------------------------------------- */

static bool ip_read_dds(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[128];
    uint32_t dwSize, flags, height, width, depth, mipmaps, pfflags;
    if (!ip_read(dev, base, h, 128) || xx_rt_memcmp(h, "DDS ", 4) != 0) return false;
    dwSize = ip_u32le(h + 4);
    if (dwSize != 124) return false;
    flags = ip_u32le(h + 8);
    height = ip_u32le(h + 12);
    width = ip_u32le(h + 16);
    depth = (flags & 0x800000U) ? ip_u32le(h + 24) : 0;
    mipmaps = (flags & 0x20000U) ? ip_u32le(h + 28) : 0;
    pfflags = ip_u32le(h + 80);

    p->width = width;
    p->height = height;
    p->depth = depth;
    p->mipmap_count = mipmaps;
    p->pixel_data_offset = 128;
    p->frame_count = 1;
    p->is_top_down = true;

    if (pfflags & 0x04U) { /* FourCC */
        char fourcc[5];
        xx_rt_memcpy(fourcc, h + 84, 4);
        fourcc[4] = 0;
        if (xx_rt_memcmp(fourcc, "DX10", 4) == 0) {
            uint8_t dxt10[20];
            p->pixel_data_offset = 148;
            if (ip_read(dev, base + 128, dxt10, 20)) {
                uint32_t arraySize = ip_u32le(dxt10 + 8);
                if (arraySize > 1) p->layer_count = arraySize;
            }
            IP_SET(p->pixel_format, "DX10");
        } else {
            IP_SET(p->pixel_format, fourcc);
        }
        p->color_model = XX_IMAGE_COLOR_COMPRESSED;
        p->is_compressed = true;
        IP_SET(p->compression, fourcc);
        if (xx_rt_memcmp(fourcc, "DXT3", 4) == 0 || xx_rt_memcmp(fourcc, "DXT5", 4) == 0) p->has_alpha = true;
    } else {
        uint32_t rgb_bits = ip_u32le(h + 88);
        uint32_t a_mask = ip_u32le(h + 104);
        p->bits_per_pixel = rgb_bits;
        p->has_alpha = (a_mask != 0);
        p->color_model = p->has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
        p->channels = p->has_alpha ? 4 : 3;
    }
    return p->width && p->height;
}

/* ---- KTX 1.1 ------------------------------------------------------------ */

static bool ip_read_ktx(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[64];
    static const uint8_t sig[12] = {0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    uint32_t endian, w, hgt, depth, mips, faces, elems, kvdLen;
    if (!ip_read(dev, base, h, 64) || xx_rt_memcmp(h, sig, 12) != 0) return false;
    endian = ip_u32le(h + 12);
    if (endian != 0x04030201U && endian != 0x01020304U) return false;
    w = ip_u32le(h + 36);
    hgt = ip_u32le(h + 40);
    depth = ip_u32le(h + 44);
    elems = ip_u32le(h + 48);
    faces = ip_u32le(h + 52);
    mips = ip_u32le(h + 56);
    kvdLen = ip_u32le(h + 60);

    p->width = w;
    p->height = hgt ? hgt : 1;
    p->depth = depth;
    p->mipmap_count = mips;
    p->layer_count = (faces ? faces : 1) * (elems ? elems : 1);
    p->pixel_data_offset = 64 + (int64_t)kvdLen;
    p->color_model = XX_IMAGE_COLOR_COMPRESSED;
    p->is_compressed = true;
    IP_SET(p->version, "KTX 1.1");
    return p->width && p->height;
}

/* ---- KTX 2.0 ------------------------------------------------------------ */

static bool ip_read_ktx2(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[80];
    static const uint8_t sig[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    uint32_t w, hgt, depth, layers, faces, levels, supercomp;
    if (!ip_read(dev, base, h, 80) || xx_rt_memcmp(h, sig, 12) != 0) return false;
    w = ip_u32le(h + 20);
    hgt = ip_u32le(h + 24);
    depth = ip_u32le(h + 28);
    layers = ip_u32le(h + 32);
    faces = ip_u32le(h + 36);
    levels = ip_u32le(h + 40);
    supercomp = ip_u32le(h + 44);

    p->width = w;
    p->height = hgt ? hgt : 1;
    p->depth = depth;
    p->mipmap_count = levels;
    p->layer_count = (faces ? faces : 1) * (layers ? layers : 1);
    p->color_model = XX_IMAGE_COLOR_COMPRESSED;
    p->is_compressed = (supercomp != 0);
    IP_SET(p->version, "KTX 2.0");
    return p->width && p->height;
}

/* ---- PowerVR PVR -------------------------------------------------------- */

static bool ip_read_pvr(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[52];
    if (!ip_read(dev, base, h, 52)) return false;
    if (ip_u32le(h) == 0x03525650U) { /* PVR v3 */
        p->height = ip_u32le(h + 24);
        p->width = ip_u32le(h + 28);
        p->depth = ip_u32le(h + 32);
        p->mipmap_count = ip_u32le(h + 44);
        p->layer_count = ip_u32le(h + 36) * ip_u32le(h + 40);
        p->pixel_data_offset = 52 + (int64_t)ip_u32le(h + 48);
        p->color_model = XX_IMAGE_COLOR_COMPRESSED;
        p->is_compressed = true;
        IP_SET(p->version, "PVR v3");
        return p->width && p->height;
    }
    if (xx_rt_memcmp(h + 44, "PVR!", 4) == 0) { /* PVR v2 */
        p->height = ip_u32le(h + 4);
        p->width = ip_u32le(h + 8);
        p->mipmap_count = ip_u32le(h + 12);
        p->bits_per_pixel = ip_u32le(h + 24);
        p->pixel_data_offset = 52;
        p->color_model = XX_IMAGE_COLOR_COMPRESSED;
        p->is_compressed = true;
        IP_SET(p->version, "PVR v2");
        return p->width && p->height;
    }
    return false;
}

/* ---- Valve VTF ---------------------------------------------------------- */

static bool ip_read_valve_vtf(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[68];
    uint32_t maj, min;
    if (!ip_read(dev, base, h, 68) || xx_rt_memcmp(h, "VTF\0", 4) != 0) return false;
    maj = ip_u32le(h + 4);
    min = ip_u32le(h + 8);
    if (maj != 7) return false;
    p->width = ip_u16le(h + 16);
    p->height = ip_u16le(h + 18);
    p->frame_count = ip_u16le(h + 24);
    p->mipmap_count = h[56];
    if (min >= 2) p->depth = ip_u16le(h + 64);
    p->pixel_data_offset = ip_u32le(h + 12);
    p->color_model = XX_IMAGE_COLOR_COMPRESSED;
    p->is_compressed = true;
    {
        char v[32];
        xx_rt_snprintf(v, sizeof(v), "VTF %u.%u", (unsigned)maj, (unsigned)min);
        IP_SET(p->version, v);
    }
    return p->width && p->height;
}

/* ---- ASTC Texture ------------------------------------------------------- */

static bool ip_read_astc_texture(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    static const uint8_t sig[4] = {0x13, 0xAB, 0xA1, 0x5C};
    if (!ip_read(dev, base, h, 16) || xx_rt_memcmp(h, sig, 4) != 0) return false;
    p->width = (uint32_t)h[7] | ((uint32_t)h[8] << 8) | ((uint32_t)h[9] << 16);
    p->height = (uint32_t)h[10] | ((uint32_t)h[11] << 8) | ((uint32_t)h[12] << 16);
    p->depth = (uint32_t)h[13] | ((uint32_t)h[14] << 8) | ((uint32_t)h[15] << 16);
    if (p->depth <= 1) p->depth = 0;
    p->color_model = XX_IMAGE_COLOR_COMPRESSED;
    p->is_compressed = true;
    p->pixel_data_offset = 16;
    {
        char fmt[32];
        xx_rt_snprintf(fmt, sizeof(fmt), "ASTC %ux%u", (unsigned)h[4], (unsigned)h[5]);
        IP_SET(p->pixel_format, fmt);
    }
    return p->width && p->height;
}

/* ---- PKM Texture -------------------------------------------------------- */

static bool ip_read_pkm_texture(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    if (!ip_read(dev, base, h, 16) || xx_rt_memcmp(h, "PKM ", 4) != 0) return false;
    p->width = ip_u16be(h + 12);
    p->height = ip_u16be(h + 14);
    p->color_model = XX_IMAGE_COLOR_COMPRESSED;
    p->is_compressed = true;
    p->pixel_data_offset = 16;
    IP_SET(p->pixel_format, "ETC");
    return p->width && p->height;
}


/* ---- Basis Universal ---------------------------------------------------- */

static bool ip_read_basis_texture(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[77];
    uint16_t hdr_sz, ver, total_images;
    uint32_t total_slices, descs_off;
    if (!ip_read(dev, base, h, 77)) return false;
    if (ip_u16le(h) != 0x7373U) return false; /* 's', 's' */
    hdr_sz = ip_u16le(h + 2);
    if (hdr_sz < 77) return false;
    ver = ip_u16le(h + 4);
    total_images = ip_u16le(h + 6);
    total_slices = ip_u32le(h + 8);
    (void)total_slices;
    descs_off = ip_u32le(h + 32);

    if (total_images && descs_off) {
        uint8_t desc[16];
        if (ip_read(dev, base + (int64_t)descs_off, desc, 16)) {
            p->mipmap_count = ip_u32le(desc + 4);
            p->width = ip_u32le(desc + 8);
            p->height = ip_u32le(desc + 12);
        }
    }
    p->frame_count = total_images ? total_images : 1;
    p->color_model = XX_IMAGE_COLOR_COMPRESSED;
    p->is_compressed = true;
    p->is_lossy = true;
    {
        char v[32];
        xx_rt_snprintf(v, sizeof(v), "Basis v%u", (unsigned)ver);
        IP_SET(p->version, v);
    }
    return p->width && p->height;
}

/* ---- QOI ---------------------------------------------------------------- */

static bool ip_read_qoi(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[14];
    if (!ip_read(dev, base, h, 14) || xx_rt_memcmp(h, "qoif", 4) != 0) return false;
    p->width = ip_u32be(h + 4);
    p->height = ip_u32be(h + 8);
    p->channels = h[12];
    p->bits_per_sample = 8;
    p->bits_per_pixel = p->channels * 8;
    p->color_model = (p->channels == 4) ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    p->has_alpha = (p->channels == 4);
    p->is_compressed = true;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 14;
    IP_SET(p->compression, "QOI");
    IP_SET(p->color_space, h[13] == 0 ? "sRGB" : "linear");
    return p->width && p->height && (p->channels == 3 || p->channels == 4);
}

/* ---- Farbfeld ----------------------------------------------------------- */

static bool ip_read_farbfeld(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    if (!ip_read(dev, base, h, 16) || xx_rt_memcmp(h, "farbfeld", 8) != 0) return false;
    p->width = ip_u32be(h + 8);
    p->height = ip_u32be(h + 12);
    p->channels = 4;
    p->bits_per_sample = 16;
    p->bits_per_pixel = 64;
    p->color_model = XX_IMAGE_COLOR_RGBA;
    p->has_alpha = true;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 16;
    p->pixel_data_size = (int64_t)p->width * p->height * 8;
    IP_SET(p->compression, "none");
    IP_SET(p->color_space, "sRGB");
    return p->width && p->height;
}

/* ---- PNM (Netpbm) ------------------------------------------------------- */

static bool ip_read_pnm(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[256];
    size_t pos = 2;
    uint32_t w = 0, hgt = 0, maxval = 255;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;
    if (buf[0] != 'P' || buf[1] < '1' || buf[1] > '7') return false;

    if (buf[1] == '7') { /* PAM */
        /* Scan lines in buffer for WIDTH, HEIGHT, DEPTH, MAXVAL, TUPLTYPE, ENDHDR */
        char *endhdr = buf;
        while (*endhdr && xx_rt_memcmp(endhdr, "ENDHDR\n", 7) != 0 && xx_rt_memcmp(endhdr, "ENDHDR\r\n", 8) != 0) ++endhdr;
        if (!*endhdr) return false;
        p->pixel_data_offset = (int64_t)(endhdr - buf) + (*(endhdr + 6) == '\r' ? 8 : 7);
        p->channels = 3;
        p->color_model = XX_IMAGE_COLOR_RGB;
        p->width = 1; p->height = 1; /* fallback if unparsed */
        return true;
    }

    /* P1..P6 */
    while (pos < sizeof(buf) - 1) {
        ip_skip_space(buf, sizeof(buf) - 1, &pos);
        if (pos < sizeof(buf) - 1 && buf[pos] == '#') {
            while (pos < sizeof(buf) - 1 && buf[pos] != '\n') pos++;
            continue;
        }
        break;
    }
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &w)) return false;
    while (pos < sizeof(buf) - 1) {
        ip_skip_space(buf, sizeof(buf) - 1, &pos);
        if (pos < sizeof(buf) - 1 && buf[pos] == '#') {
            while (pos < sizeof(buf) - 1 && buf[pos] != '\n') pos++;
            continue;
        }
        break;
    }
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &hgt)) return false;

    if (buf[1] != '1' && buf[1] != '4') {
        while (pos < sizeof(buf) - 1) {
            ip_skip_space(buf, sizeof(buf) - 1, &pos);
            if (pos < sizeof(buf) - 1 && buf[pos] == '#') {
                while (pos < sizeof(buf) - 1 && buf[pos] != '\n') pos++;
                continue;
            }
            break;
        }
        (void)ip_parse_u32(buf, sizeof(buf) - 1, &pos, &maxval);
    }
    ip_skip_space(buf, sizeof(buf) - 1, &pos);

    p->width = w;
    p->height = hgt;
    p->frame_count = 1;
    p->is_top_down = true;
    p->pixel_data_offset = (int64_t)pos;

    if (buf[1] == '1' || buf[1] == '4') {
        p->color_model = XX_IMAGE_COLOR_BILEVEL;
        p->channels = 1;
        p->bits_per_pixel = 1;
        p->bits_per_sample = 1;
    } else if (buf[1] == '2' || buf[1] == '5') {
        p->color_model = XX_IMAGE_COLOR_GRAY;
        p->channels = 1;
        p->bits_per_sample = maxval > 255 ? 16 : 8;
        p->bits_per_pixel = p->bits_per_sample;
    } else {
        p->color_model = XX_IMAGE_COLOR_RGB;
        p->channels = 3;
        p->bits_per_sample = maxval > 255 ? 16 : 8;
        p->bits_per_pixel = p->channels * p->bits_per_sample;
    }
    return p->width && p->height;
}

/* ---- TGA ---------------------------------------------------------------- */

static bool ip_read_tga(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[18];
    uint8_t id_len, cmap_type, img_type, bpp, desc;
    uint32_t w, hgt, cmap_len;
    if (!ip_read(dev, base, h, 18)) return false;
    id_len = h[0];
    cmap_type = h[1];
    img_type = h[2];
    cmap_len = ip_u16le(h + 5);
    w = ip_u16le(h + 12);
    hgt = ip_u16le(h + 14);
    bpp = h[16];
    desc = h[17];

    if (img_type != 1 && img_type != 2 && img_type != 3 && img_type != 9 && img_type != 10 && img_type != 11) return false;
    if (w == 0 || hgt == 0) return false;
    if (bpp != 1 && bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = bpp;
    p->is_top_down = (desc & 0x20) != 0;
    p->frame_count = 1;
    p->pixel_data_offset = 18 + id_len + (cmap_type ? (int64_t)cmap_len * ((h[7] + 7) / 8) : 0);

    if (cmap_type) p->palette_entries = cmap_len;
    if (img_type >= 9) {
        p->is_compressed = true;
        IP_SET(p->compression, "RLE");
    } else {
        IP_SET(p->compression, "none");
    }

    if (img_type == 1 || img_type == 9) {
        p->color_model = XX_IMAGE_COLOR_INDEXED;
        p->channels = 1;
        p->bits_per_sample = 8;
    } else if (img_type == 3 || img_type == 11) {
        p->color_model = XX_IMAGE_COLOR_GRAY;
        p->channels = 1;
        p->bits_per_sample = bpp;
    } else {
        if (bpp == 32 || (desc & 0x0F) > 0) {
            p->color_model = XX_IMAGE_COLOR_RGBA;
            p->channels = 4;
            p->has_alpha = true;
        } else {
            p->color_model = XX_IMAGE_COLOR_RGB;
            p->channels = 3;
        }
        p->bits_per_sample = 8;
    }
    return true;
}

/* ---- PCX ---------------------------------------------------------------- */

static bool ip_read_pcx(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[128];
    uint16_t xmin, ymin, xmax, ymax;
    uint8_t bpp_plane, planes;
    if (!ip_read(dev, base, h, 128)) return false;
    if (h[0] != 0x0A || h[1] > 5 || h[2] > 1) return false;
    bpp_plane = h[3];
    xmin = ip_u16le(h + 4);
    ymin = ip_u16le(h + 6);
    xmax = ip_u16le(h + 8);
    ymax = ip_u16le(h + 10);
    planes = h[65];
    if (xmax < xmin || ymax < ymin || planes == 0) return false;

    p->width = xmax - xmin + 1;
    p->height = ymax - ymin + 1;
    p->bits_per_sample = bpp_plane;
    p->channels = planes;
    p->bits_per_pixel = bpp_plane * planes;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 128;
    p->dpi_x = ip_u16le(h + 12);
    p->dpi_y = ip_u16le(h + 14);

    if (h[2] == 1) {
        p->is_compressed = true;
        IP_SET(p->compression, "RLE");
    } else {
        IP_SET(p->compression, "none");
    }

    if (planes == 1) {
        if (bpp_plane == 1) p->color_model = XX_IMAGE_COLOR_BILEVEL;
        else if (bpp_plane <= 8) {
            p->color_model = XX_IMAGE_COLOR_INDEXED;
            p->palette_entries = 1U << bpp_plane;
        }
    } else if (planes == 3) {
        p->color_model = XX_IMAGE_COLOR_RGB;
    } else if (planes == 4) {
        p->color_model = XX_IMAGE_COLOR_RGBA;
        p->has_alpha = true;
    }
    return p->width && p->height;
}

/* ---- SGI RGB ------------------------------------------------------------ */

static bool ip_read_sgi_rgb(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[12];
    uint16_t xsize, ysize, zsize;
    uint8_t storage, bpc;
    if (!ip_read(dev, base, h, 12)) return false;
    if (ip_u16be(h) != 0x01DAU && ip_u16le(h) != 0x01DAU) return false;
    storage = h[2];
    bpc = h[3];
    xsize = ip_u16be(h + 6);
    ysize = ip_u16be(h + 8);
    zsize = ip_u16be(h + 10);
    if (xsize == 0 || ysize == 0 || zsize == 0) return false;

    p->width = xsize;
    p->height = ysize;
    p->channels = zsize;
    p->bits_per_sample = bpc * 8;
    p->bits_per_pixel = p->bits_per_sample * zsize;
    p->frame_count = 1;
    p->pixel_data_offset = 512;
    if (storage == 1) {
        p->is_compressed = true;
        IP_SET(p->compression, "RLE");
    } else {
        IP_SET(p->compression, "none");
    }

    if (zsize == 1) p->color_model = XX_IMAGE_COLOR_GRAY;
    else if (zsize == 2) { p->color_model = XX_IMAGE_COLOR_GRAY_ALPHA; p->has_alpha = true; }
    else if (zsize == 3) p->color_model = XX_IMAGE_COLOR_RGB;
    else if (zsize >= 4) { p->color_model = XX_IMAGE_COLOR_RGBA; p->has_alpha = true; }
    return true;
}

/* ---- Sun Raster --------------------------------------------------------- */

static bool ip_read_sun_raster(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[32];
    uint32_t w, hgt, depth, len, type, maptype, maplen;
    if (!ip_read(dev, base, h, 32)) return false;
    if (ip_u32be(h) != 0x59A66A95U) return false;
    w = ip_u32be(h + 4);
    hgt = ip_u32be(h + 8);
    depth = ip_u32be(h + 12);
    len = ip_u32be(h + 16);
    type = ip_u32be(h + 20);
    maptype = ip_u32be(h + 24);
    maplen = ip_u32be(h + 28);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = depth;
    p->frame_count = 1;
    p->is_top_down = true;
    p->pixel_data_offset = 32 + (int64_t)maplen;
    p->pixel_data_size = len ? (int64_t)len : -1;

    if (type == 2) {
        p->is_compressed = true;
        IP_SET(p->compression, "RLE");
    } else {
        IP_SET(p->compression, "none");
    }

    if (depth == 1) {
        p->color_model = XX_IMAGE_COLOR_BILEVEL;
        p->channels = 1;
        p->bits_per_sample = 1;
    } else if (depth <= 8) {
        p->color_model = maptype ? XX_IMAGE_COLOR_INDEXED : XX_IMAGE_COLOR_GRAY;
        p->channels = 1;
        p->bits_per_sample = depth;
        if (maptype && maplen) p->palette_entries = maplen / 3;
    } else if (depth == 24) {
        p->color_model = XX_IMAGE_COLOR_RGB;
        p->channels = 3;
        p->bits_per_sample = 8;
    } else if (depth == 32) {
        p->color_model = XX_IMAGE_COLOR_RGBA;
        p->channels = 4;
        p->bits_per_sample = 8;
        p->has_alpha = true;
    }
    return true;
}

/* ---- XWD (X Window Dump) ------------------------------------------------ */

static bool ip_read_xwd(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[100];
    uint32_t hdr_sz, ver, w, hgt, bpp, vclass, cmap_entries;
    bool is_be = true;
    if (!ip_read(dev, base, h, 100)) return false;
    hdr_sz = ip_u32be(h);
    ver = ip_u32be(h + 4);
    if (ver != 7) {
        hdr_sz = ip_u32le(h);
        ver = ip_u32le(h + 4);
        is_be = false;
        if (ver != 7) return false;
    }
    if (hdr_sz < 100) return false;
    w = is_be ? ip_u32be(h + 16) : ip_u32le(h + 16);
    hgt = is_be ? ip_u32be(h + 20) : ip_u32le(h + 20);
    bpp = is_be ? ip_u32be(h + 44) : ip_u32le(h + 44);
    vclass = is_be ? ip_u32be(h + 52) : ip_u32le(h + 52);
    cmap_entries = is_be ? ip_u32be(h + 68) : ip_u32le(h + 68);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = bpp;
    p->frame_count = 1;
    p->is_top_down = true;
    p->palette_entries = cmap_entries;
    p->pixel_data_offset = (int64_t)hdr_sz + (int64_t)cmap_entries * 12;

    if (bpp == 1) {
        p->color_model = XX_IMAGE_COLOR_BILEVEL;
        p->channels = 1;
        p->bits_per_sample = 1;
    } else if (bpp <= 8) {
        p->color_model = (vclass == 0 || vclass == 1) ? XX_IMAGE_COLOR_GRAY : XX_IMAGE_COLOR_INDEXED;
        p->channels = 1;
        p->bits_per_sample = bpp;
    } else {
        p->color_model = (bpp == 32) ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
        p->channels = (bpp == 32) ? 4 : 3;
        p->bits_per_sample = 8;
        if (bpp == 32) p->has_alpha = true;
    }
    return true;
}

/* ---- X11 XBM ------------------------------------------------------------ */

static bool ip_read_x11_xbm(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[512];
    size_t i = 0;
    uint32_t w = 0, hgt = 0;
    bool found_w = false, found_h = false;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;

    while (i + 14 < sizeof(buf) - 1) {
        if (xx_rt_memcmp(buf + i, "#define", 7) == 0) {
            i += 7;
            while (i < sizeof(buf) - 1 && buf[i] != '\n') {
                if (xx_rt_memcmp(buf + i, "_width", 6) == 0 && (ip_is_space(buf[i + 6]) || buf[i + 6] == 0)) {
                    size_t pos = i + 6;
                    if (ip_parse_u32(buf, sizeof(buf) - 1, &pos, &w)) found_w = true;
                    i = pos;
                    break;
                } else if (xx_rt_memcmp(buf + i, "_height", 7) == 0 && (ip_is_space(buf[i + 7]) || buf[i + 7] == 0)) {
                    size_t pos = i + 7;
                    if (ip_parse_u32(buf, sizeof(buf) - 1, &pos, &hgt)) found_h = true;
                    i = pos;
                    break;
                }
                i++;
            }
        } else {
            i++;
        }
    }
    if (!found_w || !found_h || w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = 1;
    p->bits_per_sample = 1;
    p->channels = 1;
    p->color_model = XX_IMAGE_COLOR_BILEVEL;
    p->is_top_down = true;
    p->frame_count = 1;
    IP_SET(p->compression, "none");
    return true;
}

/* ---- X11 XPM ------------------------------------------------------------ */

static bool ip_read_xpm(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[512];
    size_t i = 0, quote_pos = 0;
    uint32_t w = 0, hgt = 0, ncolors = 0, cpp = 0;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;
    if (xx_rt_memcmp(buf, "/* XPM */", 9) != 0) return false;

    /* Find the opening quote of the geometry string */
    while (i < sizeof(buf) - 1 && buf[i] != '"') i++;
    if (i >= sizeof(buf) - 1) return false;
    quote_pos = i + 1;

    if (!ip_parse_u32(buf, sizeof(buf) - 1, &quote_pos, &w)) return false;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &quote_pos, &hgt)) return false;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &quote_pos, &ncolors)) return false;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &quote_pos, &cpp)) return false;
    (void)cpp;
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->palette_entries = ncolors;
    p->channels = 3;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 8;
    p->color_model = XX_IMAGE_COLOR_INDEXED;
    p->is_top_down = true;
    p->frame_count = 1;
    return true;
}

/* ---- Wireless Bitmap (WBMP) --------------------------------------------- */

static bool ip_read_wbmp_image(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    size_t pos = 2;
    uint32_t w = 0, hgt = 0;
    if (!ip_read(dev, base, h, 16)) return false;
    if (h[0] != 0 || h[1] != 0) return false; /* Type 0, FixHeader 0 */

    /* Read variable-length width */
    while (pos < 8) {
        uint8_t b = h[pos++];
        w = (w << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    /* Read variable-length height */
    while (pos < 14) {
        uint8_t b = h[pos++];
        hgt = (hgt << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    if (w == 0 || hgt == 0 || w > 0x100000 || hgt > 0x100000) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = 1;
    p->bits_per_sample = 1;
    p->channels = 1;
    p->color_model = XX_IMAGE_COLOR_BILEVEL;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = (int64_t)pos;
    p->pixel_data_size = (int64_t)((w + 7) / 8) * hgt;
    IP_SET(p->compression, "none");
    return true;
}

/* ---- Radiance HDR ------------------------------------------------------- */

static bool ip_read_radiance_hdr(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[1024];
    size_t i = 0;
    uint32_t w = 0, hgt = 0;
    bool found_res = false;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;
    if (xx_rt_memcmp(buf, "#?RADIANCE", 10) != 0 && xx_rt_memcmp(buf, "#?RGBE", 6) != 0) return false;

    while (i + 4 < sizeof(buf) - 1) {
        if ((buf[i] == '-' || buf[i] == '+') && buf[i + 1] == 'Y') {
            size_t pos = i + 2;
            if (ip_parse_u32(buf, sizeof(buf) - 1, &pos, &hgt)) {
                ip_skip_space(buf, sizeof(buf) - 1, &pos);
                if (pos + 2 < sizeof(buf) - 1 && (buf[pos] == '-' || buf[pos] == '+') && buf[pos + 1] == 'X') {
                    pos += 2;
                    if (ip_parse_u32(buf, sizeof(buf) - 1, &pos, &w)) {
                        found_res = true;
                        break;
                    }
                }
            }
        }
        i++;
    }
    if (!found_res || w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->channels = 4;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 32;
    p->color_model = XX_IMAGE_COLOR_HDR;
    p->is_top_down = true;
    p->frame_count = 1;
    IP_SET(p->pixel_format, "RGBE 32-bit");
    return true;
}

/* ---- OpenEXR ------------------------------------------------------------ */

static bool ip_read_openexr(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[8];
    int64_t off = base + 8;
    uint32_t w = 0, hgt = 0;
    if (!ip_read(dev, base, h, 8)) return false;
    if (h[0] != 0x76 || h[1] != 0x2F || h[2] != 0x31 || h[3] != 0x01) return false;

    for (int guard = 0; guard < 200; ++guard) {
        char name[32];
        char type[32];
        uint8_t sz_buf[4];
        uint32_t sz;
        size_t nlen = 0, tlen = 0;
        char c;
        while (nlen + 1 < sizeof(name) && ip_read(dev, off++, &c, 1) && c) name[nlen++] = c;
        name[nlen] = 0;
        if (nlen == 0) break; /* End of header attribute list */
        while (tlen + 1 < sizeof(type) && ip_read(dev, off++, &c, 1) && c) type[tlen++] = c;
        type[tlen] = 0;
        if (!ip_read(dev, off, sz_buf, 4)) break;
        off += 4;
        sz = ip_u32le(sz_buf);
        if (xx_rt_memcmp(name, "dataWindow", 10) == 0 && xx_rt_memcmp(type, "box2i", 5) == 0 && sz >= 16) {
            uint8_t b[16];
            if (ip_read(dev, off, b, 16)) {
                int32_t xmin = (int32_t)ip_u32le(b);
                int32_t ymin = (int32_t)ip_u32le(b + 4);
                int32_t xmax = (int32_t)ip_u32le(b + 8);
                int32_t ymax = (int32_t)ip_u32le(b + 12);
                if (xmax >= xmin && ymax >= ymin) {
                    w = (uint32_t)(xmax - xmin + 1);
                    hgt = (uint32_t)(ymax - ymin + 1);
                }
            }
        }
        off += sz;
    }
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->channels = 4;
    p->bits_per_sample = 16;
    p->bits_per_pixel = 64;
    p->color_model = XX_IMAGE_COLOR_HDR;
    p->is_top_down = true;
    p->frame_count = 1;
    return true;
}

/* ---- JPEG 2000 JP2 ------------------------------------------------------ */

static bool ip_read_jpeg2000_jp2(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[12];
    int64_t off = base + 12;
    static const uint8_t sig[12] = {0x00, 0x00, 0x00, 0x0C, 'j', 'P', ' ', ' ', 0x0D, 0x0A, 0x87, 0x0A};
    if (!ip_read(dev, base, h, 12) || xx_rt_memcmp(h, sig, 12) != 0) return false;

    for (int guard = 0; guard < 1000; ++guard) {
        uint8_t b[8];
        uint32_t blen;
        if (!ip_read(dev, off, b, 8)) break;
        blen = ip_u32be(b);
        if (blen < 8) break;
        if (xx_rt_memcmp(b + 4, "jp2h", 4) == 0) {
            /* Look inside header box for ihdr */
            int64_t ioff = off + 8;
            for (int iguard = 0; iguard < 50; ++iguard) {
                uint8_t ib[8];
                uint32_t iblen;
                if (ioff >= off + blen || !ip_read(dev, ioff, ib, 8)) break;
                iblen = ip_u32be(ib);
                if (iblen < 8) break;
                if (xx_rt_memcmp(ib + 4, "ihdr", 4) == 0 && iblen >= 22) {
                    uint8_t ihdr[14];
                    if (ip_read(dev, ioff + 8, ihdr, 14)) {
                        p->height = ip_u32be(ihdr);
                        p->width = ip_u32be(ihdr + 4);
                        p->channels = ip_u16be(ihdr + 8);
                        p->bits_per_sample = (ihdr[10] & 0x7FU) + 1U;
                        p->bits_per_pixel = p->channels * p->bits_per_sample;
                        p->is_compressed = true;
                        p->is_lossy = true;
                        p->frame_count = 1;
                        p->color_model = p->channels == 1 ? XX_IMAGE_COLOR_GRAY : p->channels >= 4 ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
                        if (p->channels >= 4) p->has_alpha = true;
                        IP_SET(p->compression, "JPEG 2000");
                        return p->width && p->height;
                    }
                }
                ioff += iblen;
            }
        }
        off += blen;
    }
    return false;
}

/* ---- JBIG2 -------------------------------------------------------------- */

static bool ip_read_jbig2(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[9];
    int64_t off;
    static const uint8_t sig[8] = {0x97, 'J', 'B', '2', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!ip_read(dev, base, h, 9) || xx_rt_memcmp(h, sig, 8) != 0) return false;
    off = base + 9;
    if (!(h[8] & 1)) { /* file header contains number of pages */
        uint8_t np[4];
        if (ip_read(dev, off, np, 4)) {
            p->frame_count = ip_u32be(np);
        }
        off += 4;
    }

    for (int guard = 0; guard < 1000; ++guard) {
        uint8_t seg[6];
        uint8_t flags;
        uint32_t dlen;
        if (!ip_read(dev, off + 4, seg, 2)) break; /* read flags */
        flags = seg[0];
        if ((flags & 0x3F) == 48) { /* Page Information */
            uint8_t pinfo[8];
            /* Find segment data: skip referred segments */
            int64_t data_off = off + 4 + 1;
            uint32_t rcount = (flags >> 5) & 7;
            if (rcount == 7) {
                uint8_t rc[4];
                if (ip_read(dev, data_off, rc, 4)) rcount = ip_u32be(rc);
                data_off += 4;
            }
            data_off += (rcount <= 4) ? 1 : (4 + (rcount - 1) / 8);
            data_off += 4; /* page assoc */
            if (ip_read(dev, data_off, pinfo, 4)) dlen = ip_u32be(pinfo); else break;
            (void)dlen;
            data_off += 4;
            if (ip_read(dev, data_off, pinfo, 8)) {
                p->width = ip_u32be(pinfo);
                p->height = ip_u32be(pinfo + 4);
                p->color_model = XX_IMAGE_COLOR_BILEVEL;
                p->channels = 1;
                p->bits_per_pixel = 1;
                p->bits_per_sample = 1;
                p->is_compressed = true;
                p->is_top_down = true;
                IP_SET(p->compression, "JBIG2");
                if (p->frame_count == 0) p->frame_count = 1;
                return p->width && p->height;
            }
            break;
        }
        off += 10; /* advance */
    }
    return false;
}


/* ---- Amiga IFF ILBM ----------------------------------------------------- */

static bool ip_read_iff_ilbm(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[12];
    int64_t off = base + 12;
    uint32_t form_sz;
    if (!ip_read(dev, base, h, 12) || xx_rt_memcmp(h, "FORM", 4) != 0) return false;
    form_sz = ip_u32be(h + 4);
    if (xx_rt_memcmp(h + 8, "ILBM", 4) != 0 && xx_rt_memcmp(h + 8, "PBM ", 4) != 0 && xx_rt_memcmp(h + 8, "ACBM", 4) != 0) return false;
    p->frame_count = 1;
    p->is_top_down = true;

    for (int guard = 0; guard < 500; ++guard) {
        uint8_t ch[8];
        uint32_t clen;
        if (off >= base + 8 + (int64_t)form_sz || !ip_read(dev, off, ch, 8)) break;
        clen = ip_u32be(ch + 4);
        if (xx_rt_memcmp(ch, "BMHD", 4) == 0 && clen >= 20) {
            uint8_t bmhd[20];
            if (ip_read(dev, off + 8, bmhd, 20)) {
                p->width = ip_u16be(bmhd);
                p->height = ip_u16be(bmhd + 2);
                p->bits_per_pixel = bmhd[8];
                p->channels = bmhd[8] >= 24 ? 3 : 1;
                p->bits_per_sample = bmhd[8] >= 24 ? 8 : bmhd[8];
                if (bmhd[9] == 1 || bmhd[9] == 2) p->has_alpha = true;
                if (bmhd[10] == 1) {
                    p->is_compressed = true;
                    IP_SET(p->compression, "ByteRun1");
                } else {
                    IP_SET(p->compression, "none");
                }
                if (bmhd[8] <= 8) {
                    p->color_model = XX_IMAGE_COLOR_INDEXED;
                    p->palette_entries = 1U << bmhd[8];
                } else if (bmhd[8] == 24) {
                    p->color_model = XX_IMAGE_COLOR_RGB;
                } else if (bmhd[8] == 32) {
                    p->color_model = XX_IMAGE_COLOR_RGBA;
                    p->has_alpha = true;
                    p->channels = 4;
                }
            }
        } else if (xx_rt_memcmp(ch, "CMAP", 4) == 0) {
            p->palette_entries = clen / 3;
        } else if (xx_rt_memcmp(ch, "BODY", 4) == 0) {
            p->pixel_data_offset = off + 8 - base;
            p->pixel_data_size = clen;
        }
        off += 8 + ((int64_t)clen + (clen & 1));
    }
    return p->width && p->height;
}

/* ---- GIMP XCF ----------------------------------------------------------- */

static bool ip_read_gimp_xcf(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[26];
    uint32_t w, hgt, base_type;
    if (!ip_read(dev, base, h, 26) || xx_rt_memcmp(h, "gimp xcf ", 9) != 0) return false;
    w = ip_u32be(h + 14);
    hgt = ip_u32be(h + 18);
    base_type = ip_u32be(h + 22);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->frame_count = 1;
    p->is_top_down = true;

    switch (base_type) {
    case 0: p->color_model = XX_IMAGE_COLOR_RGBA; p->channels = 4; p->has_alpha = true; break;
    case 1: p->color_model = XX_IMAGE_COLOR_GRAY_ALPHA; p->channels = 2; p->has_alpha = true; break;
    case 2: p->color_model = XX_IMAGE_COLOR_INDEXED; p->channels = 1; break;
    default: p->color_model = XX_IMAGE_COLOR_RGBA; p->channels = 4; break;
    }
    p->bits_per_sample = 8;
    p->bits_per_pixel = p->channels * 8;
    return true;
}

/* ---- Paint Shop Pro (PSP) ----------------------------------------------- */

static bool ip_read_paintshop_psp(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[32];
    int64_t off = base + 32;
    static const char sig[32] = "Paint Shop Pro Image File\n\x1a\0\0\0";
    if (!ip_read(dev, base, h, 32) || xx_rt_memcmp(h, sig, 32) != 0) return false;

    for (int guard = 0; guard < 200; ++guard) {
        uint8_t blk[6];
        uint16_t bid;
        uint32_t blen;
        if (!ip_read(dev, off, blk, 6)) break;
        bid = ip_u16le(blk);
        blen = ip_u32le(blk + 2);
        if (bid == 0x0000) { /* General image attributes */
            uint8_t attr[26];
            if (ip_read(dev, off + 6, attr, 26)) {
                p->width = ip_u32le(attr);
                p->height = ip_u32le(attr + 4);
                p->bits_per_pixel = ip_u16le(attr + 20);
                p->frame_count = 1;
                p->is_top_down = true;
                p->channels = p->bits_per_pixel >= 24 ? 3 : 1;
                p->bits_per_sample = p->bits_per_pixel >= 24 ? 8 : p->bits_per_pixel;
                p->color_model = p->bits_per_pixel <= 8 ? XX_IMAGE_COLOR_INDEXED : XX_IMAGE_COLOR_RGB;
                return p->width && p->height;
            }
        }
        off += 6 + (int64_t)blen;
    }
    return false;
}

/* ---- Photoshop Brush (ABR) ---------------------------------------------- */

static bool ip_read_photoshop_abr(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[4];
    uint16_t ver, count;
    if (!ip_read(dev, base, h, 4)) return false;
    ver = ip_u16be(h);
    count = ip_u16be(h + 2);
    if ((ver != 1 && ver != 2) || count == 0) return false;

    /* Read first brush descriptor */
    {
        uint8_t b[24];
        if (ip_read(dev, base + 4, b, 24)) {
            uint32_t type = ip_u32be(b);
            if (type == 1) { /* Sampled brush */
                int32_t top = (int32_t)ip_u32be(b + 8);
                int32_t left = (int32_t)ip_u32be(b + 12);
                int32_t bottom = (int32_t)ip_u32be(b + 16);
                int32_t right = (int32_t)ip_u32be(b + 20);
                if (right > left && bottom > top) {
                    p->width = (uint32_t)(right - left);
                    p->height = (uint32_t)(bottom - top);
                }
            }
        }
    }
    if (p->width == 0 || p->height == 0) { p->width = 64; p->height = 64; }
    p->frame_count = count;
    p->channels = 1;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 8;
    p->color_model = XX_IMAGE_COLOR_GRAY;
    p->is_top_down = true;
    return true;
}

/* ---- Photoshop Pattern (PAT) -------------------------------------------- */

static bool ip_read_photoshop_pat(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[28];
    uint32_t count, ver, hgt, w;
    if (!ip_read(dev, base, h, 28)) return false;
    count = ip_u32be(h);
    if (count == 0 || count > 10000) return false;
    ver = ip_u32be(h + 4);
    if (ver != 1) return false;
    /* Record 1 */
    hgt = ip_u32be(h + 12);
    w = ip_u32be(h + 16);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->frame_count = count;
    p->channels = 3;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 24;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->is_top_down = true;
    return true;
}

/* ---- Aseprite ----------------------------------------------------------- */

static bool ip_read_aseprite(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    uint16_t magic, frames, w, hgt, depth;
    if (!ip_read(dev, base, h, 16)) return false;
    magic = ip_u16le(h + 4);
    if (magic != 0xA5E0U) return false;
    frames = ip_u16le(h + 6);
    w = ip_u16le(h + 8);
    hgt = ip_u16le(h + 10);
    depth = ip_u16le(h + 12);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->frame_count = frames ? frames : 1;
    p->is_animated = (frames > 1);
    p->bits_per_pixel = depth;
    p->is_top_down = true;

    switch (depth) {
    case 32:
        p->color_model = XX_IMAGE_COLOR_RGBA;
        p->channels = 4;
        p->bits_per_sample = 8;
        p->has_alpha = true;
        break;
    case 16:
        p->color_model = XX_IMAGE_COLOR_GRAY_ALPHA;
        p->channels = 2;
        p->bits_per_sample = 8;
        p->has_alpha = true;
        break;
    case 8:
        p->color_model = XX_IMAGE_COLOR_INDEXED;
        p->channels = 1;
        p->bits_per_sample = 8;
        p->palette_entries = 256;
        break;
    default:
        p->color_model = XX_IMAGE_COLOR_RGBA;
        break;
    }
    return true;
}

/* ---- Better Portable Graphics (BPG) ------------------------------------- */

static bool ip_read_bpg_image(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    size_t pos = 6;
    uint32_t w = 0, hgt = 0;
    static const uint8_t sig[4] = {'B', 'P', 'G', 0xFB};
    if (!ip_read(dev, base, h, 16) || xx_rt_memcmp(h, sig, 4) != 0) return false;
    if (!ip_read_bpg_ue(h, 16, &pos, &w)) return false;
    if (!ip_read_bpg_ue(h, 16, &pos, &hgt)) return false;
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->frame_count = 1;
    p->is_compressed = true;
    p->is_lossy = true;
    p->color_model = XX_IMAGE_COLOR_YCBCR;
    p->channels = 3;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 24;
    IP_SET(p->compression, "HEVC");
    return true;
}

/* ---- DPX ---------------------------------------------------------------- */

static bool ip_read_dpx(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[800];
    bool is_be;
    uint32_t w, hgt, off_data;
    uint8_t desc, bsz;
    if (!ip_read(dev, base, h, 800)) return false;
    if (xx_rt_memcmp(h, "SDPX", 4) == 0) is_be = true;
    else if (xx_rt_memcmp(h, "XPDS", 4) == 0) is_be = false;
    else return false;

    off_data = is_be ? ip_u32be(h + 4) : ip_u32le(h + 4);
    w = is_be ? ip_u32be(h + 776) : ip_u32le(h + 776);
    hgt = is_be ? ip_u32be(h + 780) : ip_u32le(h + 780);
    desc = h[796];
    bsz = h[799];
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_sample = bsz ? bsz : 10;
    p->pixel_data_offset = (int64_t)off_data;
    p->frame_count = 1;
    p->is_top_down = true;

    if (desc == 50) { /* RGB */
        p->color_model = XX_IMAGE_COLOR_RGB;
        p->channels = 3;
    } else if (desc == 51 || desc == 52) { /* RGBA / ABGR */
        p->color_model = XX_IMAGE_COLOR_RGBA;
        p->channels = 4;
        p->has_alpha = true;
    } else if (desc == 6) { /* Luma */
        p->color_model = XX_IMAGE_COLOR_GRAY;
        p->channels = 1;
    } else {
        p->color_model = XX_IMAGE_COLOR_RGB;
        p->channels = 3;
    }
    p->bits_per_pixel = p->channels * p->bits_per_sample;
    return true;
}

/* ---- Kodak Cineon ------------------------------------------------------- */

static bool ip_read_cineon(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[216];
    uint32_t off_data, w, hgt;
    if (!ip_read(dev, base, h, 216)) return false;
    if (ip_u32be(h) != 0x802A5FD7U) return false;
    off_data = ip_u32be(h + 4);
    w = ip_u32be(h + 200);
    hgt = ip_u32be(h + 204);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->channels = 3;
    p->bits_per_sample = 10;
    p->bits_per_pixel = 30;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->frame_count = 1;
    p->is_top_down = true;
    p->pixel_data_offset = (int64_t)off_data;
    return true;
}

/* ---- Sony PlayStation TIM ----------------------------------------------- */

static bool ip_read_sony_tim(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[8];
    uint32_t flags, mode, has_clut, img_off = 8;
    if (!ip_read(dev, base, h, 8)) return false;
    if (h[0] != 0x10 || h[1] != 0x00 || h[2] != 0x00 || h[3] != 0x00) return false;
    flags = ip_u32le(h + 4);
    mode = flags & 7;
    has_clut = (flags >> 3) & 1;

    if (has_clut) {
        uint8_t clut_hdr[12];
        if (!ip_read(dev, base + 8, clut_hdr, 12)) return false;
        img_off = 8 + ip_u32le(clut_hdr);
        p->palette_entries = (uint32_t)ip_u16le(clut_hdr + 8) * ip_u16le(clut_hdr + 10);
    }

    {
        uint8_t img_hdr[12];
        uint32_t w_word, hgt;
        if (!ip_read(dev, base + (int64_t)img_off, img_hdr, 12)) return false;
        w_word = ip_u16le(img_hdr + 8);
        hgt = ip_u16le(img_hdr + 10);
        if (w_word == 0 || hgt == 0) return false;
        p->height = hgt;
        p->pixel_data_offset = (int64_t)img_off + 12;
        p->pixel_data_size = (int64_t)ip_u32le(img_hdr) - 12;
        p->frame_count = 1;

        switch (mode) {
        case 0: /* 4-bit indexed */
            p->width = w_word * 4;
            p->bits_per_pixel = 4;
            p->bits_per_sample = 4;
            p->channels = 1;
            p->color_model = XX_IMAGE_COLOR_INDEXED;
            break;
        case 1: /* 8-bit indexed */
            p->width = w_word * 2;
            p->bits_per_pixel = 8;
            p->bits_per_sample = 8;
            p->channels = 1;
            p->color_model = XX_IMAGE_COLOR_INDEXED;
            break;
        case 2: /* 16-bit RGB */
            p->width = w_word;
            p->bits_per_pixel = 16;
            p->bits_per_sample = 5;
            p->channels = 3;
            p->color_model = XX_IMAGE_COLOR_RGB;
            break;
        case 3: /* 24-bit RGB */
            p->width = (w_word * 2) / 3;
            p->bits_per_pixel = 24;
            p->bits_per_sample = 8;
            p->channels = 3;
            p->color_model = XX_IMAGE_COLOR_RGB;
            break;
        default:
            return false;
        }
    }
    return p->width && p->height;
}

/* ---- Sony PlayStation 2 TIM2 -------------------------------------------- */

static bool ip_read_sony_tim2(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16 + 48];
    uint16_t pics;
    uint32_t w, hgt, img_type;
    if (!ip_read(dev, base, h, 16 + 48) || xx_rt_memcmp(h, "TIM2", 4) != 0) return false;
    pics = ip_u16le(h + 6);
    w = ip_u16le(h + 16 + 36);
    hgt = ip_u16le(h + 16 + 38);
    img_type = h[16 + 35];
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->frame_count = pics ? pics : 1;
    p->is_top_down = true;

    switch (img_type) {
    case 1: /* 16-bit RGB1555 */
        p->bits_per_pixel = 16;
        p->bits_per_sample = 5;
        p->channels = 3;
        p->color_model = XX_IMAGE_COLOR_RGB;
        break;
    case 2: /* 24-bit RGB888 */
        p->bits_per_pixel = 24;
        p->bits_per_sample = 8;
        p->channels = 3;
        p->color_model = XX_IMAGE_COLOR_RGB;
        break;
    case 3: /* 32-bit RGBA8888 */
        p->bits_per_pixel = 32;
        p->bits_per_sample = 8;
        p->channels = 4;
        p->color_model = XX_IMAGE_COLOR_RGBA;
        p->has_alpha = true;
        break;
    case 4: /* 4-bit indexed */
        p->bits_per_pixel = 4;
        p->bits_per_sample = 4;
        p->channels = 1;
        p->color_model = XX_IMAGE_COLOR_INDEXED;
        p->palette_entries = 16;
        break;
    case 5: /* 8-bit indexed */
        p->bits_per_pixel = 8;
        p->bits_per_sample = 8;
        p->channels = 1;
        p->color_model = XX_IMAGE_COLOR_INDEXED;
        p->palette_entries = 256;
        break;
    default:
        p->bits_per_pixel = 32;
        p->color_model = XX_IMAGE_COLOR_RGBA;
        break;
    }
    return true;
}

/* ---- Nintendo GameCube / Wii TPL ---------------------------------------- */

static bool ip_read_nintendo_tpl(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[12];
    uint32_t ntex, hdr_sz, tex_hdr_off;
    if (!ip_read(dev, base, h, 12) || ip_u32be(h) != 0x0020AF30U) return false;
    ntex = ip_u32be(h + 4);
    hdr_sz = ip_u32be(h + 8);
    if (ntex == 0) return false;

    /* Read first texture descriptor table entry */
    {
        uint8_t desc[8];
        if (!ip_read(dev, base + (int64_t)hdr_sz, desc, 8)) return false;
        tex_hdr_off = ip_u32be(desc);
    }

    /* Read texture header */
    {
        uint8_t th[12];
        uint32_t fmt;
        if (!ip_read(dev, base + (int64_t)tex_hdr_off, th, 12)) return false;
        p->height = ip_u16be(th);
        p->width = ip_u16be(th + 2);
        fmt = ip_u32be(th + 4);
        p->pixel_data_offset = (int64_t)ip_u32be(th + 8);
        p->frame_count = ntex;

        switch (fmt) {
        case 0: p->color_model = XX_IMAGE_COLOR_GRAY; p->bits_per_pixel = 4; p->channels = 1; break;
        case 1: p->color_model = XX_IMAGE_COLOR_GRAY; p->bits_per_pixel = 8; p->channels = 1; break;
        case 2: p->color_model = XX_IMAGE_COLOR_GRAY_ALPHA; p->bits_per_pixel = 8; p->channels = 2; p->has_alpha = true; break;
        case 3: p->color_model = XX_IMAGE_COLOR_GRAY_ALPHA; p->bits_per_pixel = 16; p->channels = 2; p->has_alpha = true; break;
        case 4: p->color_model = XX_IMAGE_COLOR_RGB; p->bits_per_pixel = 16; p->channels = 3; break;
        case 5: p->color_model = XX_IMAGE_COLOR_RGBA; p->bits_per_pixel = 16; p->channels = 4; p->has_alpha = true; break;
        case 6: p->color_model = XX_IMAGE_COLOR_RGBA; p->bits_per_pixel = 32; p->channels = 4; p->has_alpha = true; break;
        case 8: p->color_model = XX_IMAGE_COLOR_INDEXED; p->bits_per_pixel = 4; p->channels = 1; break;
        case 9: p->color_model = XX_IMAGE_COLOR_INDEXED; p->bits_per_pixel = 8; p->channels = 1; break;
        case 14: p->color_model = XX_IMAGE_COLOR_COMPRESSED; p->is_compressed = true; IP_SET(p->compression, "CMPR"); break;
        default: p->color_model = XX_IMAGE_COLOR_RGB; break;
        }
    }
    return p->width && p->height;
}

/* ---- Nintendo Switch BNTX ----------------------------------------------- */

static bool ip_read_nintendo_bntx(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[32];
    uint32_t data_blk_off;
    if (!ip_read(dev, base, h, 32) || xx_rt_memcmp(h, "BNTX", 4) != 0) return false;
    data_blk_off = ip_u32le(h + 24); /* or following header */
    if (data_blk_off == 0) data_blk_off = 32;

    /* Read NX block */
    {
        uint8_t nx[16];
        uint32_t ntex, first_brti;
        if (!ip_read(dev, base + (int64_t)data_blk_off, nx, 16)) return false;
        if (xx_rt_memcmp(nx, "NX  ", 4) != 0) return false;
        ntex = ip_u32le(nx + 4);
        first_brti = ip_u32le(nx + 8);
        p->frame_count = ntex ? ntex : 1;

        if (first_brti) {
            uint8_t brti[48];
            if (ip_read(dev, base + (int64_t)first_brti, brti, 48) && xx_rt_memcmp(brti, "BRTI", 4) == 0) {
                p->mipmap_count = ip_u16le(brti + 22);
                p->width = ip_u32le(brti + 28);
                p->height = ip_u32le(brti + 32);
                p->depth = ip_u32le(brti + 36);
                p->color_model = XX_IMAGE_COLOR_COMPRESSED;
                p->is_compressed = true;
                return p->width && p->height;
            }
        }
    }
    return false;
}

/* ---- Sega Dreamcast PVR ------------------------------------------------- */

static bool ip_read_sega_pvr2(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[32];
    int64_t off = base;
    uint32_t data_len;
    uint16_t w, hgt;
    uint8_t pfmt;
    if (!ip_read(dev, off, h, 16)) return false;
    if (xx_rt_memcmp(h, "GBIX", 4) == 0) off += 16;
    if (!ip_read(dev, off, h, 16) || xx_rt_memcmp(h, "PVRT", 4) != 0) return false;
    data_len = ip_u32le(h + 4);
    pfmt = h[8];
    w = ip_u16le(h + 12);
    hgt = ip_u16le(h + 14);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = (pfmt == 5) ? 4 : (pfmt == 6) ? 8 : 16;
    p->bits_per_sample = (pfmt <= 2) ? 4 : 8;
    p->channels = (pfmt == 0 || pfmt == 2) ? 4 : 3;
    p->color_model = (pfmt == 0 || pfmt == 2) ? XX_IMAGE_COLOR_RGBA : (pfmt == 5 || pfmt == 6) ? XX_IMAGE_COLOR_INDEXED : XX_IMAGE_COLOR_RGB;
    if (pfmt == 0 || pfmt == 2) p->has_alpha = true;
    p->pixel_data_offset = (off - base) + 16;
    p->pixel_data_size = data_len;
    p->frame_count = 1;
    IP_SET(p->version, "Dreamcast PVR");
    return true;
}

/* ---- Sega GameCube GVR -------------------------------------------------- */

static bool ip_read_sega_gvr(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[32];
    int64_t off = base;
    uint32_t data_len;
    uint16_t w, hgt;
    if (!ip_read(dev, off, h, 16)) return false;
    if (xx_rt_memcmp(h, "GCIX", 4) == 0 || xx_rt_memcmp(h, "GBIX", 4) == 0) off += 16;
    if (!ip_read(dev, off, h, 16) || xx_rt_memcmp(h, "GVRT", 4) != 0) return false;
    data_len = ip_u32be(h + 4);
    w = ip_u16be(h + 12);
    hgt = ip_u16be(h + 14);
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = 16;
    p->bits_per_sample = 8;
    p->channels = 4;
    p->color_model = XX_IMAGE_COLOR_RGBA;
    p->has_alpha = true;
    p->pixel_data_offset = (off - base) + 16;
    p->pixel_data_size = data_len;
    p->frame_count = 1;
    IP_SET(p->version, "GameCube GVR");
    return true;
}

/* ---- Utah RLE ----------------------------------------------------------- */

static bool ip_read_utah_rle(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[12];
    int16_t xlen, ylen;
    uint8_t flags, ncolors, pixel_bits;
    if (!ip_read(dev, base, h, 12)) return false;
    if (h[0] != 0x52 || h[1] != 0xCC) return false; /* 0xCC52 little-endian */
    xlen = (int16_t)ip_u16le(h + 6);
    ylen = (int16_t)ip_u16le(h + 8);
    flags = h[10];
    ncolors = h[11];
    if (xlen <= 0 || ylen <= 0) return false;

    pixel_bits = 8;
    p->width = (uint32_t)xlen;
    p->height = (uint32_t)ylen;
    p->channels = ncolors ? ncolors : 3;
    p->bits_per_sample = pixel_bits;
    p->bits_per_pixel = p->channels * pixel_bits;
    p->is_compressed = true;
    p->frame_count = 1;
    IP_SET(p->compression, "RLE");

    if (flags & 0x04) p->has_alpha = true;
    if (p->channels == 1) p->color_model = XX_IMAGE_COLOR_GRAY;
    else if (p->has_alpha) p->color_model = XX_IMAGE_COLOR_RGBA;
    else p->color_model = XX_IMAGE_COLOR_RGB;
    return true;
}

/* ---- MTV Raytracing Image ----------------------------------------------- */

static bool ip_read_mtv_image(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[64];
    size_t pos = 0;
    uint32_t w = 0, hgt = 0;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &w)) return false;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &hgt)) return false;
    ip_skip_space(buf, sizeof(buf) - 1, &pos);
    if (w == 0 || hgt == 0 || w > 65535 || hgt > 65535) return false;

    p->width = w;
    p->height = hgt;
    p->channels = 3;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 24;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = (int64_t)pos;
    p->pixel_data_size = (int64_t)w * hgt * 3;
    IP_SET(p->compression, "none");
    return true;
}


/* ---- Palm Bitmap -------------------------------------------------------- */

static bool ip_read_palm_bitmap(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    uint16_t w, hgt, flags;
    uint8_t pixel_size, comp_type;
    if (!ip_read(dev, base, h, 16)) return false;
    w = ip_u16be(h);
    hgt = ip_u16be(h + 2);
    flags = ip_u16be(h + 6);
    pixel_size = h[8];
    comp_type = h[13];
    if (w == 0 || hgt == 0 || pixel_size == 0 || pixel_size > 32) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = pixel_size;
    p->bits_per_sample = pixel_size <= 8 ? pixel_size : 8;
    p->channels = pixel_size <= 8 ? 1 : (pixel_size == 16 ? 3 : 4);
    p->frame_count = 1;
    p->is_top_down = true;
    p->pixel_data_offset = 16;

    if (flags & 0x08) p->has_alpha = true;
    if (flags & 0x02) {
        p->is_compressed = true;
        IP_SET(p->compression, comp_type == 1 ? "RLE" : "Scanline");
    } else {
        IP_SET(p->compression, "none");
    }

    if (pixel_size == 1) p->color_model = XX_IMAGE_COLOR_BILEVEL;
    else if (pixel_size <= 8) {
        p->color_model = (flags & 0x04) ? XX_IMAGE_COLOR_INDEXED : XX_IMAGE_COLOR_GRAY;
        p->palette_entries = 1U << pixel_size;
    } else {
        p->color_model = p->has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    }
    return true;
}

/* ---- Acorn RISC OS Sprite ----------------------------------------------- */

static bool ip_read_risc_os_sprite(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[48];
    uint32_t count, first_off, w_words, h_rows, mode;
    if (!ip_read(dev, base, h, 48)) return false;
    count = ip_u32le(h);
    first_off = ip_u32le(h + 4);
    if (count == 0 || count > 10000 || first_off < 16) return false;

    {
        uint8_t s[44];
        if (!ip_read(dev, base + (int64_t)first_off - 4, s, 44)) return false;
        w_words = ip_u32le(s + 16) + 1;
        h_rows = ip_u32le(s + 20) + 1;
        mode = ip_u32le(s + 40);
        if (w_words == 0 || h_rows == 0) return false;

        p->height = h_rows;
        p->frame_count = count;
        p->is_top_down = false; /* RISC OS stores bottom-up by default */

        if (mode > 256) {
            uint32_t type = (mode >> 27) & 0x0F;
            static const uint8_t bpp_tab[8] = {1, 1, 2, 4, 8, 16, 32, 32};
            p->bits_per_pixel = bpp_tab[type < 8 ? type : 0];
            p->width = (w_words * 32) / (p->bits_per_pixel ? p->bits_per_pixel : 1);
        } else {
            p->bits_per_pixel = 8;
            p->width = w_words * 4;
        }

        if (p->bits_per_pixel == 1) p->color_model = XX_IMAGE_COLOR_BILEVEL;
        else if (p->bits_per_pixel <= 8) {
            p->color_model = XX_IMAGE_COLOR_INDEXED;
            p->palette_entries = 1U << p->bits_per_pixel;
        } else p->color_model = XX_IMAGE_COLOR_RGB;
        p->channels = p->bits_per_pixel <= 8 ? 1 : 3;
        p->bits_per_sample = p->bits_per_pixel <= 8 ? p->bits_per_pixel : 8;
    }
    return p->width && p->height;
}

/* ---- Amiga DiskObject Icon ---------------------------------------------- */

static bool ip_read_amiga_diskobject(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[48];
    int16_t w, hgt;
    if (!ip_read(dev, base, h, 48)) return false;
    if (h[0] != 0xE3 || h[1] != 0x10) return false;
    w = (int16_t)ip_u16be(h + 8);
    hgt = (int16_t)ip_u16be(h + 10);
    if (w <= 0 || hgt <= 0) return false;

    p->width = (uint32_t)w;
    p->height = (uint32_t)hgt;
    p->channels = 1;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 8;
    p->color_model = XX_IMAGE_COLOR_INDEXED;
    p->has_alpha = true;
    p->frame_count = 1;
    p->is_top_down = true;
    IP_SET(p->version, "Amiga DiskObject");
    return true;
}

/* ---- Windows Metafile (WMF) --------------------------------------------- */

static bool ip_read_wmf(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[22];
    if (!ip_read(dev, base, h, 22)) return false;
    if (ip_u32le(h) == 0x9AC6CDD7U) { /* Aldus Placeable WMF */
        int16_t left = (int16_t)ip_u16le(h + 6);
        int16_t top = (int16_t)ip_u16le(h + 8);
        int16_t right = (int16_t)ip_u16le(h + 10);
        int16_t bottom = (int16_t)ip_u16le(h + 12);
        uint16_t inch = ip_u16le(h + 14);
        p->width = (uint32_t)(right > left ? (right - left) : (left - right));
        p->height = (uint32_t)(bottom > top ? (bottom - top) : (top - bottom));
        if (inch) {
            p->dpi_x = inch;
            p->dpi_y = inch;
        }
        IP_SET(p->version, "Aldus Placeable WMF");
    } else {
        uint16_t ftype = ip_u16le(h);
        uint16_t hsz = ip_u16le(h + 2);
        if ((ftype == 1 || ftype == 2) && hsz == 9) {
            p->width = 100;
            p->height = 100;
            IP_SET(p->version, "Standard WMF");
        } else {
            return false;
        }
    }
    p->color_model = XX_IMAGE_COLOR_VECTOR;
    p->frame_count = 1;
    return true;
}

/* ---- Enhanced Metafile (EMF) -------------------------------------------- */

static bool ip_read_emf(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[88];
    uint32_t rtype, rsz;
    if (!ip_read(dev, base, h, 88)) return false;
    rtype = ip_u32le(h);
    rsz = ip_u32le(h + 4);
    if (rtype != 1 || rsz < 88 || xx_rt_memcmp(h + 40, " EMF", 4) != 0) return false;
    {
        int32_t left = (int32_t)ip_u32le(h + 8);
        int32_t top = (int32_t)ip_u32le(h + 12);
        int32_t right = (int32_t)ip_u32le(h + 16);
        int32_t bottom = (int32_t)ip_u32le(h + 20);
        p->width = (uint32_t)(right >= left ? (right - left + 1) : 0);
        p->height = (uint32_t)(bottom >= top ? (bottom - top + 1) : 0);
    }
    p->color_model = XX_IMAGE_COLOR_VECTOR;
    p->frame_count = 1;
    IP_SET(p->version, "EMF");
    return true;
}

/* ---- Scalable Vector Graphics (SVG) ------------------------------------- */

static bool ip_read_svg(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[1024];
    size_t pos = 0;
    bool found_svg = false;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;

    /* Check XML prolog / BOM and locate "<svg" */
    while (pos + 4 < sizeof(buf) - 1) {
        if (xx_rt_memcmp(buf + pos, "<svg", 4) == 0 && (ip_is_space(buf[pos + 4]) || buf[pos + 4] == '>')) {
            found_svg = true;
            pos += 4;
            break;
        }
        pos++;
    }
    if (!found_svg) return false;

    p->color_model = XX_IMAGE_COLOR_VECTOR;
    p->frame_count = 1;

    /* Search for width="..." and height="..." within the opening <svg ...> */
    while (pos + 6 < sizeof(buf) - 1 && buf[pos] != '>') {
        if (xx_rt_memcmp(buf + pos, "width=", 6) == 0) {
            pos += 6;
            if (buf[pos] == '"' || buf[pos] == '\'') pos++;
            (void)ip_parse_u32(buf, sizeof(buf) - 1, &pos, &p->width);
        } else if (xx_rt_memcmp(buf + pos, "height=", 7) == 0) {
            pos += 7;
            if (buf[pos] == '"' || buf[pos] == '\'') pos++;
            (void)ip_parse_u32(buf, sizeof(buf) - 1, &pos, &p->height);
        } else if (xx_rt_memcmp(buf + pos, "viewBox=", 8) == 0 && (p->width == 0 || p->height == 0)) {
            pos += 8;
            if (buf[pos] == '"' || buf[pos] == '\'') pos++;
            {
                uint32_t minx = 0, miny = 0, vw = 0, vh = 0;
                (void)ip_parse_u32(buf, sizeof(buf) - 1, &pos, &minx);
                (void)ip_parse_u32(buf, sizeof(buf) - 1, &pos, &miny);
                if (ip_parse_u32(buf, sizeof(buf) - 1, &pos, &vw) && ip_parse_u32(buf, sizeof(buf) - 1, &pos, &vh)) {
                    if (p->width == 0) p->width = vw;
                    if (p->height == 0) p->height = vh;
                }
            }
        }
        pos++;
    }
    if (p->width == 0) p->width = 100;
    if (p->height == 0) p->height = 100;
    return true;
}

/* ---- Camera RAW: Fujifilm RAF ------------------------------------------- */

static bool ip_read_fujifilm_raf(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[128];
    uint32_t jpeg_off, jpeg_len;
    if (!ip_read(dev, base, h, 128) || xx_rt_memcmp(h, "FUJIFILMCCD-RAW ", 16) != 0) return false;
    jpeg_off = ip_u32be(h + 84);
    jpeg_len = ip_u32be(h + 88);
    (void)jpeg_len;

    /* If embedded JPEG preview exists, read its SOF marker to obtain dimensions */
    if (jpeg_off) {
        uint8_t jm[4];
        if (ip_read(dev, base + (int64_t)jpeg_off, jm, 4) && jm[0] == 0xFF && jm[1] == 0xD8) {
            int64_t joff = base + (int64_t)jpeg_off + 2;
            for (int guard = 0; guard < 200; ++guard) {
                uint8_t mk[4];
                uint16_t mlen;
                if (!ip_read(dev, joff, mk, 4) || mk[0] != 0xFF) break;
                if (mk[1] == 0xD9 || mk[1] == 0xDA) break;
                mlen = ip_u16be(mk + 2);
                if (mlen < 2) break;
                if ((mk[1] >= 0xC0 && mk[1] <= 0xCF) && mk[1] != 0xC4 && mk[1] != 0xC8 && mk[1] != 0xCC) {
                    uint8_t d[6];
                    if (ip_read(dev, joff + 4, d, 6)) {
                        p->height = ip_u16be(d + 1);
                        p->width = ip_u16be(d + 3);
                        break;
                    }
                }
                joff += 2 + (int64_t)mlen;
            }
        }
    }
    if (p->width == 0) { p->width = 4000; p->height = 3000; }
    p->channels = 3;
    p->bits_per_sample = 14;
    p->bits_per_pixel = 14;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->frame_count = 1;
    IP_SET(p->version, "Fujifilm RAF");
    return true;
}

/* ---- Camera RAW: Minolta MRW -------------------------------------------- */

static bool ip_read_minolta_mrw(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[8];
    int64_t off = base + 8;
    if (!ip_read(dev, base, h, 8)) return false;
    if (h[0] != 0x00 || h[1] != 'M' || h[2] != 'R' || h[3] != 'M') return false;

    for (int guard = 0; guard < 50; ++guard) {
        uint8_t b[8];
        uint32_t blen;
        if (!ip_read(dev, off, b, 8)) break;
        blen = ip_u32be(b + 4);
        if (xx_rt_memcmp(b, "PRD\0", 4) == 0 && blen >= 12) {
            uint8_t d[8];
            if (ip_read(dev, off + 8, d, 8)) {
                p->height = ip_u16be(d);
                p->width = ip_u16be(d + 2);
                break;
            }
        }
        off += 8 + (int64_t)blen;
    }
    if (p->width == 0) { p->width = 3000; p->height = 2000; }
    p->channels = 3;
    p->bits_per_sample = 12;
    p->bits_per_pixel = 12;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->frame_count = 1;
    IP_SET(p->version, "Minolta MRW");
    return true;
}

/* ---- Camera RAW: Sigma X3F ---------------------------------------------- */

static bool ip_read_sigma_x3f(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[36];
    if (!ip_read(dev, base, h, 36) || xx_rt_memcmp(h, "FOVb", 4) != 0) return false;
    p->width = ip_u32le(h + 28);
    p->height = ip_u32le(h + 32);
    if (p->width == 0 || p->height == 0) { p->width = 2640; p->height = 1760; }
    p->channels = 3;
    p->bits_per_sample = 12;
    p->bits_per_pixel = 36;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->frame_count = 1;
    IP_SET(p->version, "Sigma X3F");
    return true;
}

/* ---- Alias PIX ---------------------------------------------------------- */

static bool ip_read_alias_pix(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[10];
    uint16_t w, hgt, bpp;
    if (!ip_read(dev, base, h, 10)) return false;
    w = ip_u16be(h);
    hgt = ip_u16be(h + 2);
    bpp = ip_u16be(h + 8);
    if (w == 0 || hgt == 0 || (bpp != 8 && bpp != 24)) return false;

    p->width = w;
    p->height = hgt;
    p->bits_per_pixel = bpp;
    p->bits_per_sample = 8;
    p->channels = bpp == 24 ? 3 : 1;
    p->color_model = bpp == 24 ? XX_IMAGE_COLOR_RGB : XX_IMAGE_COLOR_GRAY;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 10;
    return true;
}

/* ---- Apple PICT --------------------------------------------------------- */

static bool ip_read_apple_pict(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[512 + 10];
    int16_t top, left, bottom, right;
    if (!ip_read(dev, base, h, 512 + 10)) return false;
    /* Check 512-byte header (zeroes for standalone PICT file) */
    top = (int16_t)ip_u16be(h + 512 + 2);
    left = (int16_t)ip_u16be(h + 512 + 4);
    bottom = (int16_t)ip_u16be(h + 512 + 6);
    right = (int16_t)ip_u16be(h + 512 + 8);
    if (bottom <= top || right <= left) return false;

    p->width = (uint32_t)(right - left);
    p->height = (uint32_t)(bottom - top);
    p->channels = 3;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 24;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->frame_count = 1;
    p->is_top_down = true;
    IP_SET(p->version, "PICT");
    return true;
}

/* ---- AVS X Image -------------------------------------------------------- */

static bool ip_read_avs_image(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[8];
    uint32_t w, hgt;
    if (!ip_read(dev, base, h, 8)) return false;
    w = ip_u32be(h);
    hgt = ip_u32be(h + 4);
    if (w == 0 || hgt == 0 || w > 32768 || hgt > 32768) return false;

    p->width = w;
    p->height = hgt;
    p->channels = 4;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 32;
    p->color_model = XX_IMAGE_COLOR_RGBA;
    p->has_alpha = true;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 8;
    p->pixel_data_size = (int64_t)w * hgt * 4;
    IP_SET(p->compression, "none");
    return true;
}

/* ---- DEC Sixel ---------------------------------------------------------- */

static bool ip_read_dec_sixel(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[8];
    if (!ip_read(dev, base, h, 8)) return false;
    if (h[0] == 0x1B && h[1] == 'P' && h[2] == 'q') { /* ESC P q */
        p->width = 640;
        p->height = 480;
    } else if (h[0] == 0x90 && h[1] == 'q') { /* 8-bit DCS q */
        p->width = 640;
        p->height = 480;
    } else {
        return false;
    }
    p->channels = 1;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 8;
    p->color_model = XX_IMAGE_COLOR_INDEXED;
    p->frame_count = 1;
    p->is_top_down = true;
    IP_SET(p->version, "Sixel");
    return true;
}

/* ---- Electronic Arts FSH / SHPI ----------------------------------------- */

static bool ip_read_ea_fsh(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[16];
    uint32_t pack_sz, num_entries;
    if (!ip_read(dev, base, h, 16)) return false;
    if (xx_rt_memcmp(h, "SHPI", 4) != 0 && xx_rt_memcmp(h, "SHPP", 4) != 0 && xx_rt_memcmp(h, "SHPS", 4) != 0 && xx_rt_memcmp(h, "SHPX", 4) != 0) return false;
    pack_sz = ip_u32le(h + 4);
    num_entries = ip_u32le(h + 8);
    (void)pack_sz;
    if (num_entries == 0) return false;

    /* Read first directory entry to find bitmap header offset */
    {
        uint8_t ent[8];
        uint32_t ent_off;
        if (!ip_read(dev, base + 16, ent, 8)) return false;
        ent_off = ip_u32le(ent + 4);
        if (ent_off >= 16) {
            uint8_t bh[16];
            if (ip_read(dev, base + (int64_t)ent_off, bh, 16)) {
                p->width = ip_u16le(bh + 4);
                p->height = ip_u16le(bh + 6);
                p->bits_per_pixel = 16;
                p->bits_per_sample = 8;
                p->channels = 3;
                p->color_model = XX_IMAGE_COLOR_RGB;
                p->frame_count = num_entries;
                p->is_top_down = true;
                IP_SET(p->version, "EA FSH");
                return p->width && p->height;
            }
        }
    }
    return false;
}

/* ---- GIMP Brush (GBR) --------------------------------------------------- */

static bool ip_read_gimp_gbr(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[28];
    uint32_t hsz, ver, w, hgt, bytes;
    if (!ip_read(dev, base, h, 28)) return false;
    hsz = ip_u32be(h);
    ver = ip_u32be(h + 4);
    w = ip_u32be(h + 8);
    hgt = ip_u32be(h + 12);
    bytes = ip_u32be(h + 16);
    if (ver != 2 || hsz < 28 || w == 0 || hgt == 0 || (bytes != 1 && bytes != 4)) return false;

    p->width = w;
    p->height = hgt;
    p->channels = bytes;
    p->bits_per_sample = 8;
    p->bits_per_pixel = bytes * 8;
    p->color_model = (bytes == 4) ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_GRAY;
    if (bytes == 4) p->has_alpha = true;
    p->frame_count = 1;
    p->is_top_down = true;
    p->pixel_data_offset = (int64_t)hsz;
    return true;
}

/* ---- GIMP Pattern (PAT) ------------------------------------------------- */

static bool ip_read_gimp_pat(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[28];
    uint32_t hsz, ver, w, hgt, bytes;
    if (!ip_read(dev, base, h, 28)) return false;
    hsz = ip_u32be(h);
    ver = ip_u32be(h + 4);
    w = ip_u32be(h + 8);
    hgt = ip_u32be(h + 12);
    bytes = ip_u32be(h + 16);
    if (ver != 1 || hsz < 28 || w == 0 || hgt == 0 || (bytes != 1 && bytes != 3 && bytes != 4)) return false;

    p->width = w;
    p->height = hgt;
    p->channels = bytes;
    p->bits_per_sample = 8;
    p->bits_per_pixel = bytes * 8;
    p->color_model = (bytes == 1) ? XX_IMAGE_COLOR_GRAY : (bytes == 4) ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    if (bytes == 4) p->has_alpha = true;
    p->frame_count = 1;
    p->is_top_down = true;
    p->pixel_data_offset = (int64_t)hsz;
    return true;
}

/* ---- JPEG 2000 PGX ------------------------------------------------------ */

static bool ip_read_jpeg2000_pgx(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[64];
    size_t pos = 4;
    uint32_t bsz = 0, w = 0, hgt = 0;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;
    if (buf[0] != 'P' || buf[1] != 'G' || !ip_is_space(buf[2])) return false;

    ip_skip_space(buf, sizeof(buf) - 1, &pos);
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &bsz)) return false;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &w)) return false;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &hgt)) return false;
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->channels = 1;
    p->bits_per_sample = bsz ? bsz : 8;
    p->bits_per_pixel = p->bits_per_sample;
    p->color_model = XX_IMAGE_COLOR_GRAY;
    p->is_top_down = true;
    p->frame_count = 1;
    return true;
}

/* ---- Khoros Visualization VIFF ------------------------------------------ */

static bool ip_read_khoros_viff(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[128];
    uint32_t row_sz, col_sz, bands;
    if (!ip_read(dev, base, h, 128)) return false;
    if (h[0] != 0xAB || h[1] != 0x01) return false;
    row_sz = ip_u32be(h + 20);
    col_sz = ip_u32be(h + 24);
    bands = ip_u32be(h + 40);
    if (row_sz == 0 || col_sz == 0) return false;

    p->width = col_sz;
    p->height = row_sz;
    p->channels = bands ? bands : 1;
    p->bits_per_sample = 8;
    p->bits_per_pixel = p->channels * 8;
    p->color_model = (bands >= 3) ? XX_IMAGE_COLOR_RGB : XX_IMAGE_COLOR_GRAY;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 1024;
    return true;
}

/* ---- Netpbm Portable Float Map (PFM) ------------------------------------ */

static bool ip_read_netpbm_pfm(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    char buf[128];
    size_t pos = 2;
    uint32_t w = 0, hgt = 0;
    if (!ip_read(dev, base, buf, sizeof(buf) - 1)) return false;
    buf[sizeof(buf) - 1] = 0;
    if (buf[0] != 'P' || (buf[1] != 'F' && buf[1] != 'f')) return false;

    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &w)) return false;
    if (!ip_parse_u32(buf, sizeof(buf) - 1, &pos, &hgt)) return false;
    if (w == 0 || hgt == 0) return false;

    p->width = w;
    p->height = hgt;
    p->channels = (buf[1] == 'F') ? 3 : 1;
    p->bits_per_sample = 32;
    p->bits_per_pixel = p->channels * 32;
    p->color_model = XX_IMAGE_COLOR_HDR;
    p->frame_count = 1;
    p->is_top_down = false; /* PFM stores bottom to top */
    return true;
}

/* ---- Nokia OTA Bitmap --------------------------------------------------- */

static bool ip_read_nokia_ota_bitmap(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[4];
    if (!ip_read(dev, base, h, 4)) return false;
    if (h[0] != 0x00 || h[3] != 0x01 || h[1] == 0 || h[2] == 0) return false;
    p->width = h[1];
    p->height = h[2];
    p->channels = 1;
    p->bits_per_sample = 1;
    p->bits_per_pixel = 1;
    p->color_model = XX_IMAGE_COLOR_BILEVEL;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 4;
    p->pixel_data_size = (int64_t)((h[1] + 7) / 8) * h[2];
    return true;
}

/* ---- Softimage PIC -------------------------------------------------------- */

static bool ip_read_softimage_pic(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[32];
    if (!ip_read(dev, base, h, 32)) return false;
    if (ip_u32be(h) != 0x5380F634U) return false;
    p->width = ip_u16be(h + 8);
    p->height = ip_u16be(h + 10);
    p->channels = 3;
    p->bits_per_sample = 8;
    p->bits_per_pixel = 24;
    p->color_model = XX_IMAGE_COLOR_RGB;
    p->is_top_down = true;
    p->frame_count = 1;
    p->pixel_data_offset = 32;
    return p->width && p->height;
}
