/* SPDX-License-Identifier: MIT
 * Primary reference: https://vt100.net/docs/vt3xx-gp/chapter14.html
 * DEC SIXEL RGB subset: complete DCS/ST framing, declared bounded raster, checked RGB color registers/repeats/carriage returns/newlines and pixel extents. Original
 * descriptor/color program plus RGB raster exported using literal declared dimensions and black opaque background for unpainted pixels; no aspect-ratio resampling.
 * Transparent backgrounds, HLS, animation and other terminal controls declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/dec_sixel/xx_dec_sixel.h"
#include "../common/xx_component_binary.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[2];
    return n >= 12 && pm_read(f, 0, b, 2) && b[0] == 27 && b[1] == 'P';
}
static bool scene_bitmap_six_num(const uint8_t *b, uint64_t *p, uint64_t n, uint32_t *value)
{
    uint32_t v = 0;
    uint64_t start = *p;
    while (*p < n && b[*p] >= '0' && b[*p] <= '9') {
        unsigned d = b[(*p)++] - '0';
        if (v > (1000000U - d) / 10) return false;
        v = v * 10 + d;
    }
    if (*p == start) return false;
    *value = v;
    return true;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t p = 2, header = 0, x = 0, y = 0, paint = 0, z;
    uint32_t width = 0, height = 0, values[4], color = 0;
    uint8_t palette[256][3], defined[256], *pixels = NULL;
    bool ok = false;
    unsigned i, params = 0;
    xx_mem_zero(defined, sizeof(defined));
    if (n < 12 || b[0] != 27 || b[1] != 'P' || b[n - 2] != 27 || b[n - 1] != '\\') return false;
    while (p < n - 2 && b[p] != 'q') {
        uint32_t v;
        if (params >= 3 || !scene_bitmap_six_num(b, &p, n - 2, &v) || v > 9 || (params == 1 && v == 1)) return false;
        ++params;
        if (p < n - 2 && b[p] == ';') ++p;
        else if (p < n - 2 && b[p] != 'q') return false;
    }
    if (p >= n - 2 || b[p++] != 'q' || p >= n - 2 || b[p++] != '"') return false;
    for (i = 0; i < 4; ++i) {
        if (!scene_bitmap_six_num(b, &p, n - 2, &values[i]) || (i < 3 && (p == n - 2 || b[p++] != ';'))) return false;
    }
    width = values[2];
    height = values[3];
    if (!values[0] || !values[1] || values[0] > 100 || values[1] > 100 || !width || !height || width > 8192 || height > 8192 || (uint64_t)width * height > 8388608)
        return false;
    header = p;
    z = (uint64_t)width * height * 3;
    pixels = (uint8_t *)xx_mem_alloc((size_t)z);
    if (!pixels) return false;
    xx_mem_zero(pixels, (size_t)z);
    while (p < n - 2) {
        uint8_t c = b[p++];
        if (xx_component_parser_stopped(pd)) goto done;
        if (c == '#') {
            if (!scene_bitmap_six_num(b, &p, n - 2, &color) || color > 255) goto done;
            if (p < n - 2 && b[p] == ';') {
                uint32_t mode;
                ++p;
                if (!scene_bitmap_six_num(b, &p, n - 2, &mode) || mode != 2) goto done;
                for (i = 0; i < 3; ++i) {
                    if (p == n - 2 || b[p++] != ';' || !scene_bitmap_six_num(b, &p, n - 2, &values[i]) || values[i] > 100) goto done;
                    palette[color][i] = (uint8_t)((values[i] * 255 + 50) / 100);
                }
                defined[color] = 1;
            }
        } else if (c == '$') x = 0;
        else if (c == '-') {
            if (y + 6 >= height) goto done;
            y += 6;
            x = 0;
        } else {
            uint32_t repeat = 1;
            unsigned mask, k;
            if (c == '!') {
                if (!scene_bitmap_six_num(b, &p, n - 2, &repeat) || !repeat || p == n - 2) goto done;
                c = b[p++];
            }
            if (c < '?' || c > '~' || !defined[color] || repeat > width - x || (paint += (uint64_t)repeat * 6) > 67108864) goto done;
            mask = c - '?';
            for (k = 0; k < 6; ++k)
                if (mask & (1U << k)) {
                    uint32_t j;
                    if (y + k >= height) goto done;
                    for (j = 0; j < repeat; ++j) {
                        uint64_t at = ((y + k) * width + x + j) * 3;
                        xx_mem_copy(pixels + at, palette[color], 3);
                    }
                }
            x += repeat;
        }
    }
    if (!paint || p != n - 2 || !component_emit(f, s, "descriptor.six", 0, header, n) || !component_emit(f, s, "raster-program.six", header, n - 2 - header, n) ||
        !component_emit(f, s, "terminator.six", n - 2, 2, n) || !component_publish_memory_keep_on_failure(f, s, "raster.rgb", pixels, z)) {
        goto done;
    }
    pixels = NULL;
    ok = true;
done:
    if (pixels) xx_mem_free(pixels);
    return ok;
}

void xx_dec_sixel_init(xx_dec_sixel *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_DEC_SIXEL, "six");
    }
}
xx_dec_sixel *xx_dec_sixel_create(xx_io_device *d, int64_t at)
{
    xx_dec_sixel *r = (xx_dec_sixel *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dec_sixel_init(r, d, at);
    return r;
}
void xx_dec_sixel_destroy(xx_dec_sixel *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dec_sixel_free(xx_dec_sixel *r)
{
    if (r) {
        xx_dec_sixel_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dec_sixel_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dec_sixel_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
