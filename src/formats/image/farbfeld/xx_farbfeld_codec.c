/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * farbfeld (https://tools.suckless.org/farbfeld/): "farbfeld", u32 BE width,
 * u32 BE height, then RGBA with 16-bit big-endian samples.  Load and save.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include <string.h>

static int ff_probe(const uint8_t *d, size_t n)
{
    return (n >= 16 && memcmp(d, "farbfeld", 8) == 0) ? 100 : 0;
}

static xx_image_status ff_load(const uint8_t *d, size_t n, const xx_image_load_options *o, xx_image *img)
{
    uint32_t w, h;
    xx_image_frame *f;
    xx_image_status st;
    if (n < 16 || memcmp(d, "farbfeld", 8) != 0) return XX_IMAGE_ERR_FORMAT;
    w = xx_image_rd_u32be(d + 8);
    h = xx_image_rd_u32be(d + 12);
    st = xx_image_check_dimensions(w, h, o);
    if (st != XX_IMAGE_OK) return st;
    if (!xx_image_in_range(n, 16, (size_t)w * h * 8)) return XX_IMAGE_ERR_CORRUPT;
    f = xx_image_add_frame(img, w, h);
    if (!f) return XX_IMAGE_ERR_MEMORY;
    for (size_t i = 0; i < (size_t)w * h * 4; ++i) f->rgba[i] = d[16 + i * 2]; /* high byte of each 16-bit sample */
    img->info.color_model = XX_IMAGE_COLOR_RGBA;
    img->info.channels = 4;
    img->info.bits_per_sample = 16;
    img->info.bits_per_pixel = 64;
    img->info.has_alpha = true;
    XX_IMAGE_SET_STR(img->info.compression, "none");
    XX_IMAGE_SET_STR(img->info.color_space, "sRGB");
    if (!o->skip_metadata) {
        xx_image_add_prop_uint(img, "farbfeld", "width", w);
        xx_image_add_prop_uint(img, "farbfeld", "height", h);
    }
    return XX_IMAGE_OK;
}

static xx_image_status ff_save(const xx_image *img, const xx_image_save_options *o, xx_image_buffer *out)
{
    const xx_image_frame *f = xx_image_save_frame(img, o);
    size_t n;
    if (!f) return XX_IMAGE_ERR_ARGUMENT;
    n = (size_t)f->width * f->height * 4;
    xx_image_buf_append(out, "farbfeld", 8);
    xx_image_buf_u32be(out, f->width);
    xx_image_buf_u32be(out, f->height);
    if (!xx_image_buf_reserve(out, out->size + n * 2)) return XX_IMAGE_ERR_MEMORY;
    for (size_t i = 0; i < n; ++i) {
        out->data[out->size++] = f->rgba[i];
        out->data[out->size++] = f->rgba[i]; /* 8 -> 16 bit: v * 257 */
    }
    return XX_IMAGE_OK;
}

const xx_image_codec xx_image_codec_farbfeld = {"farbfeld", "farbfeld", "ff;farbfeld", "image/x-farbfeld",
                                          XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA | XX_IMAGE_CODEC_INFO,
                                          640 /* XX_FILE_TYPE_FARBFELD */, ff_probe, ff_load, ff_save};
