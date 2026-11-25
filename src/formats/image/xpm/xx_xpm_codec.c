/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * X11 Pixmap (XPM3) codec.
 * Load: XPM3 C source format, "w h ncolors cpp [x y]" values string, color table
 * with c / g / m keys, color names (None, transparent, black, white, red, green,
 * blue, gray/grey, yellow, cyan, magenta) and #RGB, #RRGGBB, #RRRRGGGGBBBB hex colors.
 * Save: XPM3 with xx_image_quantize (<= 256 colours), 1 or 2 characters per pixel,
 * None for pixels with alpha < 128.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct xpm_color {
    char sym[8];
    uint32_t rgba;
} xpm_color;

static int xpm_probe(const uint8_t *d, size_t n)
{
    size_t limit = n < 256 ? n : 256;
    if (n < 16) return 0;
    for (size_t i = 0; i + 9 <= limit; ++i) {
        if (memcmp(d + i, "/* XPM */", 9) == 0) return 90;
    }
    for (size_t i = 0; i + 11 <= limit; ++i) {
        if (memcmp(d + i, "static char", 11) == 0) {
            for (size_t j = i + 11; j + 3 <= limit; ++j) {
                if (memcmp(d + j, "xpm", 3) == 0 || memcmp(d + j, "XPM", 3) == 0) return 60;
            }
        }
    }
    return 0;
}

static bool xpm_next_string(const uint8_t *d, size_t n, size_t *p, const char **str_out, size_t *len_out)
{
    while (*p < n) {
        uint8_t c = d[*p];
        if (c == '/' && *p + 1 < n && d[*p + 1] == '*') {
            *p += 2;
            while (*p + 1 < n && !(d[*p] == '*' && d[*p + 1] == '/')) (*p)++;
            if (*p + 1 < n) *p += 2;
        } else if (c == '/' && *p + 1 < n && d[*p + 1] == '/') {
            *p += 2;
            while (*p < n && d[*p] != '\n' && d[*p] != '\r') (*p)++;
        } else if (c == '"') {
            size_t start;
            (*p)++;
            start = *p;
            while (*p < n && d[*p] != '"') {
                if (d[*p] == '\\' && *p + 1 < n) *p += 2;
                else (*p)++;
            }
            *str_out = (const char *)(d + start);
            *len_out = *p - start;
            if (*p < n && d[*p] == '"') (*p)++;
            return true;
        } else {
            (*p)++;
        }
    }
    return false;
}

static uint8_t xpm_hex_val(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}

static bool xpm_parse_color(const char *val, size_t len, uint32_t *rgba_out)
{
    char buf[64];
    size_t k = 0;
    while (k < len && k + 1 < sizeof(buf) && !isspace((unsigned char)val[k])) {
        buf[k] = (char)tolower((unsigned char)val[k]);
        k++;
    }
    buf[k] = '\0';

    if (strcmp(buf, "none") == 0 || strcmp(buf, "transparent") == 0) {
        *rgba_out = 0x00000000;
        return true;
    }
    if (strcmp(buf, "black") == 0) {
        *rgba_out = 0x000000FF;
        return true;
    }
    if (strcmp(buf, "white") == 0) {
        *rgba_out = 0xFFFFFFFF;
        return true;
    }
    if (strcmp(buf, "red") == 0) {
        *rgba_out = 0xFF0000FF;
        return true;
    }
    if (strcmp(buf, "green") == 0) {
        *rgba_out = 0x00FF00FF;
        return true;
    }
    if (strcmp(buf, "blue") == 0) {
        *rgba_out = 0x0000FFFF;
        return true;
    }
    if (strcmp(buf, "gray") == 0 || strcmp(buf, "grey") == 0) {
        *rgba_out = 0x808080FF;
        return true;
    }
    if (strcmp(buf, "yellow") == 0) {
        *rgba_out = 0xFFFF00FF;
        return true;
    }
    if (strcmp(buf, "cyan") == 0) {
        *rgba_out = 0x00FFFFFF;
        return true;
    }
    if (strcmp(buf, "magenta") == 0) {
        *rgba_out = 0xFF00FFFF;
        return true;
    }

    if (buf[0] == '#') {
        size_t hex_len = strlen(buf + 1);
        const char *h = buf + 1;
        if (hex_len == 3) {
            uint8_t r = (uint8_t)(xpm_hex_val(h[0]) * 17);
            uint8_t g = (uint8_t)(xpm_hex_val(h[1]) * 17);
            uint8_t b = (uint8_t)(xpm_hex_val(h[2]) * 17);
            *rgba_out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
            return true;
        } else if (hex_len == 6) {
            uint8_t r = (uint8_t)((xpm_hex_val(h[0]) << 4) | xpm_hex_val(h[1]));
            uint8_t g = (uint8_t)((xpm_hex_val(h[2]) << 4) | xpm_hex_val(h[3]));
            uint8_t b = (uint8_t)((xpm_hex_val(h[4]) << 4) | xpm_hex_val(h[5]));
            *rgba_out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
            return true;
        } else if (hex_len == 9) {
            uint8_t r = (uint8_t)((xpm_hex_val(h[0]) << 4) | xpm_hex_val(h[1]));
            uint8_t g = (uint8_t)((xpm_hex_val(h[3]) << 4) | xpm_hex_val(h[4]));
            uint8_t b = (uint8_t)((xpm_hex_val(h[6]) << 4) | xpm_hex_val(h[7]));
            *rgba_out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
            return true;
        } else if (hex_len == 12) {
            uint8_t r = (uint8_t)((xpm_hex_val(h[0]) << 4) | xpm_hex_val(h[1]));
            uint8_t g = (uint8_t)((xpm_hex_val(h[4]) << 4) | xpm_hex_val(h[5]));
            uint8_t b = (uint8_t)((xpm_hex_val(h[8]) << 4) | xpm_hex_val(h[9]));
            *rgba_out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
            return true;
        }
    }
    return false;
}

static xx_image_status xpm_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    size_t p = 0;
    const char *str;
    size_t len;
    uint32_t w = 0, h = 0, ncolors = 0, cpp = 0;
    int32_t x_hot = -1, y_hot = -1;
    xpm_color *colors = NULL;
    xx_image_frame *f;
    xx_image_status st;
    bool has_alpha = false;
    const char *g = "XPM";

    if (!xpm_next_string(d, n, &p, &str, &len)) return XX_IMAGE_ERR_FORMAT;

    /* Parse values header: "w h ncolors cpp [x y]" */
    if (sscanf(str, "%u %u %u %u %d %d", &w, &h, &ncolors, &cpp, &x_hot, &y_hot) < 4) {
        return XX_IMAGE_ERR_FORMAT;
    }

    if (cpp == 0 || cpp > 4 || ncolors == 0 || ncolors > 65536) return XX_IMAGE_ERR_CORRUPT;
    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;

    colors = (xpm_color *)calloc(ncolors, sizeof(xpm_color));
    if (!colors) return XX_IMAGE_ERR_MEMORY;

    /* Parse color table entries */
    for (uint32_t i = 0; i < ncolors; ++i) {
        const char *cs;
        size_t clen;
        size_t cp;
        const char *c_val = NULL, *g_val = NULL, *m_val = NULL;
        size_t c_len = 0, g_len = 0, m_len = 0;

        if (!xpm_next_string(d, n, &p, &cs, &clen) || clen < cpp) {
            free(colors);
            return XX_IMAGE_ERR_CORRUPT;
        }

        memcpy(colors[i].sym, cs, cpp);
        colors[i].sym[cpp] = '\0';
        colors[i].rgba = 0x000000FF;

        cp = cpp;
        while (cp < clen) {
            char key;
            while (cp < clen && isspace((unsigned char)cs[cp])) cp++;
            if (cp >= clen) break;
            key = cs[cp++];
            while (cp < clen && isspace((unsigned char)cs[cp])) cp++;
            if (cp >= clen) break;
            {
                size_t val_start = cp;
                while (cp < clen && !isspace((unsigned char)cs[cp])) cp++;
                if (key == 'c') {
                    c_val = cs + val_start;
                    c_len = cp - val_start;
                } else if (key == 'g') {
                    g_val = cs + val_start;
                    g_len = cp - val_start;
                } else if (key == 'm') {
                    m_val = cs + val_start;
                    m_len = cp - val_start;
                }
            }
        }

        if (c_val) {
            xpm_parse_color(c_val, c_len, &colors[i].rgba);
        } else if (g_val) {
            xpm_parse_color(g_val, g_len, &colors[i].rgba);
        } else if (m_val) {
            xpm_parse_color(m_val, m_len, &colors[i].rgba);
        }

        if ((colors[i].rgba & 0xFF) < 255) has_alpha = true;
    }

    f = xx_image_add_frame(img, w, h);
    if (!f) {
        free(colors);
        return XX_IMAGE_ERR_MEMORY;
    }

    /* Parse pixel rows */
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t *dst = f->rgba + (size_t)y * w * 4;
        if (!xpm_next_string(d, n, &p, &str, &len) || len < (size_t)w * cpp) {
            free(colors);
            return XX_IMAGE_ERR_CORRUPT;
        }

        for (uint32_t x = 0; x < w; ++x) {
            const char *sym = str + (size_t)x * cpp;
            uint32_t rgba = 0x000000FF;

            /* Match symbol in color table */
            for (uint32_t i = 0; i < ncolors; ++i) {
                if (memcmp(colors[i].sym, sym, cpp) == 0) {
                    rgba = colors[i].rgba;
                    break;
                }
            }
            xx_image_put_rgba(dst + (size_t)x * 4, rgba);
        }
    }

    free(colors);

    img->info.color_model = has_alpha ? XX_IMAGE_COLOR_RGBA : XX_IMAGE_COLOR_RGB;
    img->info.channels = has_alpha ? 4 : 3;
    img->info.bits_per_pixel = 32;
    img->info.bits_per_sample = 8;
    img->info.has_alpha = has_alpha;
    img->info.frame_count = 1;
    XX_IMAGE_SET_STR(img->info.compression, "none");

    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, g, "colors", ncolors);
        xx_image_add_prop_uint(img, g, "characters per pixel", cpp);
        if (x_hot >= 0) xx_image_add_prop_int(img, g, "hotspot x", x_hot);
        if (y_hot >= 0) xx_image_add_prop_int(img, g, "hotspot y", y_hot);
    }

    return XX_IMAGE_OK;
}

static xx_image_status xpm_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    uint32_t w, h;
    uint8_t *idx;
    uint32_t pal[256];
    uint32_t num_colors;
    uint32_t cpp;
    static const char sym_chars[] =
        " .+#@$%&*=-;:>,<1234567890qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM"
        "`~!^()_[]{}|/?";
    size_t num_sym_chars = sizeof(sym_chars) - 1;
    char sym_map[256][4];

    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    w = f->width;
    h = f->height;
    if (w == 0 || h == 0 || (uint64_t)w * h > 0x7FFFFFFF) return XX_IMAGE_ERR_LIMIT;

    idx = (uint8_t *)malloc((size_t)w * h);
    if (!idx) return XX_IMAGE_ERR_MEMORY;

    num_colors = xx_image_quantize(f, 256, idx, pal);
    if (num_colors == 0) {
        free(idx);
        return XX_IMAGE_ERR_MEMORY;
    }

    cpp = (num_colors <= (uint32_t)num_sym_chars) ? 1 : 2;

    /* Build symbol mapping */
    for (uint32_t i = 0; i < num_colors; ++i) {
        if (cpp == 1) {
            sym_map[i][0] = sym_chars[i];
            sym_map[i][1] = '\0';
        } else {
            sym_map[i][0] = sym_chars[i / num_sym_chars];
            sym_map[i][1] = sym_chars[i % num_sym_chars];
            sym_map[i][2] = '\0';
        }
    }

    xx_image_buf_append(out, "/* XPM */\nstatic char *image[] = {\n", 35);
    xx_image_buf_printf(out, "\"%u %u %u %u\",\n", w, h, num_colors, cpp);

    /* Write color definitions */
    for (uint32_t i = 0; i < num_colors; ++i) {
        uint8_t a = (uint8_t)(pal[i] & 0xFF);
        if (a < 128) {
            xx_image_buf_printf(out, "\"%s c None\",\n", sym_map[i]);
        } else {
            uint8_t r = (uint8_t)(pal[i] >> 24);
            uint8_t gval = (uint8_t)(pal[i] >> 16);
            uint8_t b = (uint8_t)(pal[i] >> 8);
            xx_image_buf_printf(out, "\"%s c #%02X%02X%02X\",\n", sym_map[i], r, gval, b);
        }
    }

    /* Write pixel rows */
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *row = idx + (size_t)y * w;
        xx_image_buf_u8(out, '"');
        for (uint32_t x = 0; x < w; ++x) {
            xx_image_buf_append(out, sym_map[row[x]], cpp);
        }
        if (y + 1 < h) {
            xx_image_buf_append(out, "\",\n", 3);
        } else {
            xx_image_buf_append(out, "\"\n", 2);
        }
    }

    xx_image_buf_append(out, "};\n", 3);
    free(idx);

    return out->failed ? XX_IMAGE_ERR_MEMORY : XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_xpm = {
    "xpm",
    "X11 Pixmap",
    "xpm",
    "image/x-xpixmap",
    XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_INDEXED | XX_IMAGE_CODEC_INFO,
    0,
    xpm_probe,
    xpm_load,
    xpm_save
};
