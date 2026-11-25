/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Abstract image interface: the table of image formats and the header-only
 * property readers.  See include/xxfclib/formats/image/xx_image_props.h.
 *
 * Every reader works on the raw bytes through xx_io_read_at, checks each
 * length before it trusts it and never decodes pixel data.
 */

#include "xxfclib/formats/image/xx_image_props.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/rt/xx_rt.h"

/* ---- small byte helpers ------------------------------------------------- */

static uint16_t ip_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint16_t ip_u16be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t ip_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t ip_u32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void ip_set(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (src)
        for (; src[i] && i + 1 < cap; ++i) dst[i] = src[i];
    dst[i] = 0;
}
#define IP_SET(field, text) ip_set((field), sizeof(field), (text))

static bool ip_read(xx_io_device *dev, int64_t offset, void *buf, size_t n)
{
    return offset >= 0 && xx_io_read_at(dev, offset, buf, n);
}

static char ip_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* ---- PNG ---------------------------------------------------------------- */

static bool ip_read_png(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    uint8_t h[33];
    int64_t off;
    uint32_t chunks = 0;
    int64_t idat = 0;
    bool seen_idat = false;
    uint8_t ct;
    if (!ip_read(dev, base, h, sizeof(h)) || xx_rt_memcmp(h, sig, 8) != 0 || xx_rt_memcmp(h + 12, "IHDR", 4) != 0) return false;
    p->width = ip_u32be(h + 16);
    p->height = ip_u32be(h + 20);
    p->bits_per_sample = h[24];
    ct = h[25];
    p->is_interlaced = h[28] == 1;
    p->is_compressed = true;
    p->is_top_down = true;
    p->frame_count = 1;
    IP_SET(p->compression, "deflate");
    IP_SET(p->color_space, "sRGB");
    switch (ct) {
    case 0: p->color_model = p->bits_per_sample == 1 ? XX_IMAGE_COLOR_BILEVEL : XX_IMAGE_COLOR_GRAY; p->channels = 1; break;
    case 2: p->color_model = XX_IMAGE_COLOR_RGB; p->channels = 3; break;
    case 3: p->color_model = XX_IMAGE_COLOR_INDEXED; p->channels = 1; break;
    case 4: p->color_model = XX_IMAGE_COLOR_GRAY_ALPHA; p->channels = 2; p->has_alpha = true; break;
    case 6: p->color_model = XX_IMAGE_COLOR_RGBA; p->channels = 4; p->has_alpha = true; break;
    default: return false;
    }
    p->bits_per_pixel = p->bits_per_sample * p->channels;
    {
        char fmt[32];
        static const char *const names[7] = {"gray", "", "RGB", "indexed", "gray+alpha", "", "RGBA"};
        xx_rt_snprintf(fmt, sizeof(fmt), "%u-bit %s", (unsigned)p->bits_per_sample, names[ct]);
        IP_SET(p->pixel_format, fmt);
    }
    /* Walk the chunks for the palette, transparency, resolution, colour
     * space and animation.  Stop at IEND or after a sane number of chunks. */
    off = base + 8;
    while (chunks < 100000) {
        uint8_t ch[8];
        uint32_t len;
        if (!ip_read(dev, off, ch, 8)) break;
        len = ip_u32be(ch);
        if (len > 0x7FFFFFFFU) break;
        ++chunks;
        if (xx_rt_memcmp(ch + 4, "PLTE", 4) == 0) p->palette_entries = len / 3;
        else if (xx_rt_memcmp(ch + 4, "tRNS", 4) == 0) p->has_alpha = true;
        else if (xx_rt_memcmp(ch + 4, "iCCP", 4) == 0) IP_SET(p->color_space, "ICC");
        else if (xx_rt_memcmp(ch + 4, "gAMA", 4) == 0 && p->color_space[0] == 's') IP_SET(p->color_space, "gamma");
        else if (xx_rt_memcmp(ch + 4, "sRGB", 4) == 0) IP_SET(p->color_space, "sRGB");
        else if (xx_rt_memcmp(ch + 4, "pHYs", 4) == 0 && len >= 9) {
            uint8_t d[9];
            if (ip_read(dev, off + 8, d, 9) && d[8] == 1) {
                p->dpi_x = ip_u32be(d) * 0.0254;
                p->dpi_y = ip_u32be(d + 4) * 0.0254;
            }
        } else if (xx_rt_memcmp(ch + 4, "acTL", 4) == 0 && len >= 8) {
            uint8_t d[8];
            if (ip_read(dev, off + 8, d, 8)) {
                p->is_animated = true;
                p->frame_count = ip_u32be(d);
                IP_SET(p->version, "APNG");
            }
        } else if (xx_rt_memcmp(ch + 4, "IDAT", 4) == 0) {
            if (!seen_idat) p->pixel_data_offset = off - base;
            seen_idat = true;
            idat += len;
        } else if (xx_rt_memcmp(ch + 4, "IEND", 4) == 0)
            break;
        off += 12 + (int64_t)len;
    }
    if (seen_idat) p->pixel_data_size = idat;
    return p->width && p->height;
}

/* ---- BMP ---------------------------------------------------------------- */

static bool ip_read_bmp(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[14 + 124];
    uint32_t dib, comp = 0, clr_used = 0;
    int32_t height;
    uint16_t bpp;
    if (!ip_read(dev, base, h, 14 + 12) || h[0] != 'B' || h[1] != 'M') return false;
    dib = ip_u32le(h + 14);
    if (dib != 12 && dib != 40 && dib != 52 && dib != 56 && dib != 64 && dib != 108 && dib != 124) return false;
    if (!ip_read(dev, base, h, 14 + dib)) return false;
    p->pixel_data_offset = ip_u32le(h + 10);
    if (dib == 12) {
        p->width = ip_u16le(h + 18);
        height = (int16_t)ip_u16le(h + 20);
        bpp = ip_u16le(h + 24);
        IP_SET(p->version, "OS/2 1.x");
    } else {
        int32_t w = (int32_t)ip_u32le(h + 18);
        height = (int32_t)ip_u32le(h + 22);
        bpp = ip_u16le(h + 28);
        comp = ip_u32le(h + 30);
        if (w <= 0) return false;
        p->width = (uint32_t)w;
        p->dpi_x = ip_u32le(h + 38) * 0.0254;
        p->dpi_y = ip_u32le(h + 42) * 0.0254;
        clr_used = ip_u32le(h + 46);
        IP_SET(p->version, dib == 40 ? "BITMAPINFOHEADER" : dib == 108 ? "BITMAPV4HEADER" : dib == 124 ? "BITMAPV5HEADER" : "OS/2 2.x");
        if (dib >= 108) {
            uint32_t cs = ip_u32le(h + 14 + 56);
            if (cs == 0x73524742U) IP_SET(p->color_space, "sRGB");
            else if (cs == 0x57696E20U) IP_SET(p->color_space, "Windows");
            else if (cs == 0x4C494E4BU || cs == 0x4D424544U) IP_SET(p->color_space, "ICC");
            else if (cs == 0) IP_SET(p->color_space, "calibrated RGB");
            if ((comp == 3 || comp == 6) && ip_u32le(h + 14 + 52)) p->has_alpha = true;
        }
    }
    if (height == 0) return false;
    p->is_top_down = height < 0;
    p->height = (uint32_t)(height < 0 ? -(int64_t)height : height);
    p->bits_per_pixel = bpp;
    p->frame_count = 1;
    if (bpp <= 8 && bpp) p->palette_entries = clr_used ? clr_used : (1U << bpp);
    switch (comp) {
    case 0: IP_SET(p->compression, "none"); break;
    case 1: IP_SET(p->compression, "RLE8"); p->is_compressed = true; break;
    case 2: IP_SET(p->compression, "RLE4"); p->is_compressed = true; break;
    case 3: IP_SET(p->compression, "bitfields"); break;
    case 4: IP_SET(p->compression, "JPEG"); p->is_compressed = true; p->is_lossy = true; break;
    case 5: IP_SET(p->compression, "PNG"); p->is_compressed = true; break;
    case 6: IP_SET(p->compression, "alpha bitfields"); p->has_alpha = true; break;
    default: IP_SET(p->compression, "unknown"); break;
    }
    if (bpp == 1) p->color_model = XX_IMAGE_COLOR_INDEXED;
    else if (bpp <= 8) p->color_model = XX_IMAGE_COLOR_INDEXED;
    else if (bpp == 32 && (p->has_alpha || comp == 0)) {
        p->color_model = p->has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    } else p->color_model = XX_IMAGE_COLOR_RGB;
    p->channels = p->color_model == XX_IMAGE_COLOR_RGBA ? 4 : p->color_model == XX_IMAGE_COLOR_INDEXED ? 1 : 3;
    if (bpp >= 24) p->bits_per_sample = 8;
    {
        char fmt[32];
        xx_rt_snprintf(fmt, sizeof(fmt), "%u bpp", (unsigned)bpp);
        IP_SET(p->pixel_format, fmt);
    }
    {
        uint32_t file_size = ip_u32le(h + 2);
        if (file_size > p->pixel_data_offset) p->pixel_data_size = (int64_t)file_size - p->pixel_data_offset;
    }
    return true;
}

/* ---- GIF ---------------------------------------------------------------- */

static bool ip_skip_sub_blocks(xx_io_device *dev, int64_t *off)
{
    for (int guard = 0; guard < 0x1000000; ++guard) {
        uint8_t n;
        if (!ip_read(dev, *off, &n, 1)) return false;
        *off += 1 + n;
        if (n == 0) return true;
    }
    return false;
}

static bool ip_read_gif(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t h[13];
    int64_t off;
    uint32_t frames = 0;
    if (!ip_read(dev, base, h, 13) || xx_rt_memcmp(h, "GIF8", 4) != 0 || (h[4] != '7' && h[4] != '9') || h[5] != 'a') return false;
    p->width = ip_u16le(h + 6);
    p->height = ip_u16le(h + 8);
    p->color_model = XX_IMAGE_COLOR_INDEXED;
    p->channels = 1;
    p->bits_per_pixel = (h[10] & 7) + 1U;
    p->is_compressed = true;
    p->is_top_down = true;
    IP_SET(p->compression, "LZW");
    IP_SET(p->version, h[4] == '9' ? "89a" : "87a");
    if (h[10] & 0x80) p->palette_entries = 1U << ((h[10] & 7) + 1);
    if (h[12]) {
        /* pixel aspect ratio; (n + 15) / 64 */
    }
    off = base + 13 + (int64_t)((h[10] & 0x80) ? 3U * p->palette_entries : 0U);
    for (int guard = 0; guard < 1000000; ++guard) {
        uint8_t b;
        if (!ip_read(dev, off, &b, 1)) break;
        if (b == 0x3B) break;
        if (b == 0x21) {
            uint8_t lab[3];
            if (!ip_read(dev, off + 1, lab, 3)) break;
            if (lab[0] == 0xF9 && lab[1] >= 4) {
                uint8_t gce[4];
                if (ip_read(dev, off + 3, gce, 4) && (gce[0] & 1)) p->has_alpha = true;
            }
            off += 2;
            if (!ip_skip_sub_blocks(dev, &off)) break;
        } else if (b == 0x2C) {
            uint8_t d[9];
            if (!ip_read(dev, off + 1, d, 9)) break;
            if (frames == 0) {
                p->pixel_data_offset = off - base;
                if (d[8] & 0x40) p->is_interlaced = true;
            }
            ++frames;
            off += 10;
            if (d[8] & 0x80) off += 3 * (int64_t)(1U << ((d[8] & 7) + 1));
            off += 1; /* LZW minimum code size */
            if (!ip_skip_sub_blocks(dev, &off)) break;
        } else
            break;
    }
    p->frame_count = frames ? frames : 1;
    p->is_animated = frames > 1;
    p->bits_per_sample = 8;
    {
        char fmt[32];
        xx_rt_snprintf(fmt, sizeof(fmt), "%u-bit indexed", (unsigned)p->bits_per_pixel);
        IP_SET(p->pixel_format, fmt);
    }
    return p->width && p->height;
}

/* ---- JPEG --------------------------------------------------------------- */

static bool ip_read_jpeg(xx_io_device *dev, int64_t base, xx_image_props *p)
{
    uint8_t m[4];
    int64_t off = base + 2;
    bool adobe_transform_seen = false;
    int adobe_transform = -1;
    bool jfif = false;
    if (!ip_read(dev, base, m, 3) || m[0] != 0xFF || m[1] != 0xD8 || m[2] != 0xFF) return false;
    p->is_compressed = true;
    p->is_lossy = true;
    p->is_top_down = true;
    p->frame_count = 1;
    p->color_model = XX_IMAGE_COLOR_YCBCR;
    for (int guard = 0; guard < 100000; ++guard) {
        uint8_t mk[4];
        uint16_t len;
        if (!ip_read(dev, off, mk, 4)) break;
        if (mk[0] != 0xFF) break;
        if (mk[1] == 0xFF) {
            ++off;
            continue;
        }
        if (mk[1] == 0xD8 || mk[1] == 0x01 || (mk[1] >= 0xD0 && mk[1] <= 0xD7)) {
            off += 2;
            continue;
        }
        if (mk[1] == 0xD9 || mk[1] == 0xDA) {
            if (mk[1] == 0xDA && p->pixel_data_offset < 0) p->pixel_data_offset = off - base;
            break;
        }
        len = ip_u16be(mk + 2);
        if (len < 2) break;
        if (mk[1] == 0xE0 && len >= 16) {
            uint8_t d[14];
            if (ip_read(dev, off + 4, d, 14) && xx_rt_memcmp(d, "JFIF\0", 5) == 0) {
                char v[32];
                jfif = true;
                xx_rt_snprintf(v, sizeof(v), "JFIF %u.%02u", (unsigned)d[5], (unsigned)d[6]);
                IP_SET(p->version, v);
                if (d[7] == 1) {
                    p->dpi_x = ip_u16be(d + 8);
                    p->dpi_y = ip_u16be(d + 10);
                } else if (d[7] == 2) {
                    p->dpi_x = ip_u16be(d + 8) * 2.54;
                    p->dpi_y = ip_u16be(d + 10) * 2.54;
                }
            }
        } else if (mk[1] == 0xE1 && len >= 8) {
            uint8_t d[6];
            if (ip_read(dev, off + 4, d, 6) && xx_rt_memcmp(d, "Exif\0\0", 6) == 0 && p->version[0] == 0) IP_SET(p->version, "Exif");
        } else if (mk[1] == 0xE2 && len >= 14) {
            uint8_t d[12];
            if (ip_read(dev, off + 4, d, 12) && xx_rt_memcmp(d, "ICC_PROFILE\0", 12) == 0) IP_SET(p->color_space, "ICC");
        } else if (mk[1] == 0xEE && len >= 14) {
            uint8_t d[12];
            if (ip_read(dev, off + 4, d, 12) && xx_rt_memcmp(d, "Adobe", 5) == 0) {
                adobe_transform_seen = true;
                adobe_transform = d[11];
            }
        } else if ((mk[1] >= 0xC0 && mk[1] <= 0xCF) && mk[1] != 0xC4 && mk[1] != 0xC8 && mk[1] != 0xCC) {
            uint8_t d[6];
            static const char *const kinds[16] = {"baseline", "extended sequential", "progressive", "lossless", "", "differential sequential", "differential progressive",
                                                  "differential lossless", "", "arithmetic sequential", "arithmetic progressive", "arithmetic lossless", "",
                                                  "arithmetic differential sequential", "arithmetic differential progressive", "arithmetic differential lossless"};
            char c[32];
            if (!ip_read(dev, off + 4, d, 6)) break;
            p->bits_per_sample = d[0];
            p->height = ip_u16be(d + 1);
            p->width = ip_u16be(d + 3);
            p->channels = d[5];
            p->bits_per_pixel = p->bits_per_sample * p->channels;
            p->is_interlaced = mk[1] == 0xC2 || mk[1] == 0xC6 || mk[1] == 0xCA || mk[1] == 0xCE;
            if (mk[1] == 0xC3 || mk[1] == 0xC7 || mk[1] == 0xCB || mk[1] == 0xCF) p->is_lossy = false;
            xx_rt_snprintf(c, sizeof(c), "JPEG %s", kinds[mk[1] - 0xC0]);
            IP_SET(p->compression, c);
        }
        off += 2 + (int64_t)len;
    }
    if (p->channels == 1) p->color_model = XX_IMAGE_COLOR_GRAY;
    else if (p->channels == 4) p->color_model = (adobe_transform_seen && adobe_transform == 2) ? XX_IMAGE_COLOR_CMYK /* YCCK */ : XX_IMAGE_COLOR_CMYK;
    else if (p->channels == 3) p->color_model = (adobe_transform_seen && adobe_transform == 0 && !jfif) ? XX_IMAGE_COLOR_RGB : XX_IMAGE_COLOR_YCBCR;
    if (p->channels) {
        char fmt[32];
        xx_rt_snprintf(fmt, sizeof(fmt), "%u x %u-bit", (unsigned)p->channels, (unsigned)p->bits_per_sample);
        IP_SET(p->pixel_format, fmt);
    }
    if (p->color_space[0] == 0) IP_SET(p->color_space, "sRGB");
    return p->width && p->height;
}

/* ---- the interface table ------------------------------------------------- */

/* Rows are kept in file-type order of first appearance; lookups are linear,
 * the table is small.  Extra rows (formats without a reader yet) describe the
 * format only. */
#include "xx_image_props_readers.inc"
#include "xx_image_props_rows.inc"

static const xx_image_interface g_image_interfaces[] = {
    {XX_FILE_TYPE_PNG, "png", "Portable Network Graphics", "png;apng", "image/png", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_png},
    {XX_FILE_TYPE_BMP, "bmp", "Windows Bitmap", "bmp;dib", "image/bmp", XX_IMAGE_IF_RASTER, ip_read_bmp},
    {XX_FILE_TYPE_GIF, "gif", "Graphics Interchange Format", "gif", "image/gif", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_MULTI, ip_read_gif},
    {XX_FILE_TYPE_JPEG, "jpeg", "JPEG image", "jpg;jpeg;jpe;jfif", "image/jpeg", XX_IMAGE_IF_RASTER | XX_IMAGE_IF_LOSSY, ip_read_jpeg},
    XX_IMAGE_PROPS_EXTRA_ROWS
};

void xx_image_props_init(xx_image_props *props)
{
    if (!props) return;
    xx_rt_memset(props, 0, sizeof(*props));
    props->file_type = XX_FILE_TYPE_UNKNOWN;
    props->pixel_data_offset = -1;
    props->pixel_data_size = -1;
}

size_t xx_image_interface_count(void)
{
    return sizeof(g_image_interfaces) / sizeof(g_image_interfaces[0]);
}

const xx_image_interface *xx_image_interface_at(size_t index)
{
    return index < xx_image_interface_count() ? &g_image_interfaces[index] : NULL;
}

const xx_image_interface *xx_image_interface_get(xx_file_type_t type)
{
    for (size_t i = 0; i < xx_image_interface_count(); ++i)
        if (g_image_interfaces[i].file_type == type) return &g_image_interfaces[i];
    return NULL;
}

static bool ip_list_has(const char *list, const char *name)
{
    size_t n = 0;
    if (!list || !name) return false;
    while (name[n]) ++n;
    while (*list) {
        size_t k = 0;
        while (list[k] && list[k] != ';') ++k;
        if (k == n) {
            size_t i = 0;
            while (i < n && ip_lower(list[i]) == ip_lower(name[i])) ++i;
            if (i == n) return true;
        }
        list += k;
        if (*list == ';') ++list;
    }
    return false;
}

const xx_image_interface *xx_image_interface_find(const char *name)
{
    if (!name) return NULL;
    if (*name == '.') ++name;
    if (!*name) return NULL;
    for (size_t i = 0; i < xx_image_interface_count(); ++i) {
        const xx_image_interface *r = &g_image_interfaces[i];
        if (ip_list_has(r->id, name) || ip_list_has(r->extensions, name)) return r;
    }
    return NULL;
}

bool xx_image_props_is_supported(xx_file_type_t type)
{
    const xx_image_interface *r = xx_image_interface_get(type);
    return r && r->read;
}

static bool ip_try(const xx_image_interface *r, xx_io_device *device, int64_t base, xx_image_props *props)
{
    if (!r || !r->read) return false;
    xx_image_props_init(props);
    props->file_type = r->file_type;
    if (r->read(device, base, props)) return true;
    xx_image_props_init(props);
    return false;
}

bool xx_image_props_read(xx_io_device *device, int64_t base_address, xx_file_type_t type, xx_image_props *props)
{
    xx_file_type_t types[32];
    size_t n;
    if (!device || !props || base_address < 0) return false;
    xx_image_props_init(props);
    if (type != XX_FILE_TYPE_UNKNOWN) return ip_try(xx_image_interface_get(type), device, base_address, props);
    n = xx_format_fast_signature_types(device, base_address, types, sizeof(types) / sizeof(types[0]));
    for (size_t i = 0; i < n; ++i)
        if (ip_try(xx_image_interface_get(types[i]), device, base_address, props)) return true;
    if (base_address == 0) {
        xx_file_type_t t = xx_format_get_file_type_device(device);
        if (t != XX_FILE_TYPE_UNKNOWN && ip_try(xx_image_interface_get(t), device, 0, props)) return true;
    }
    return false;
}

bool xx_image_props_read_memory(const void *data, size_t size, xx_file_type_t type, xx_image_props *props)
{
    xx_io_device *dev;
    bool ok;
    if (!data || !size || !props) return false;
    dev = xx_io_mem_open_ro(data, size);
    if (!dev) return false;
    ok = xx_image_props_read(dev, 0, type, props);
    xx_io_close(dev);
    return ok;
}

const char *xx_image_color_model_to_string(xx_image_color_model model)
{
    static const char *const names[XX_IMAGE_COLOR_COUNT] = {"unknown", "bilevel", "gray", "gray+alpha", "RGB", "RGBA", "indexed",
                                                            "CMYK", "YCbCr", "Lab", "HDR", "block compressed", "vector", "mixed"};
    return ((unsigned)model < XX_IMAGE_COLOR_COUNT) ? names[model] : names[0];
}
