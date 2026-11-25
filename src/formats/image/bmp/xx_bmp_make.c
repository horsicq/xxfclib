/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/image/bmp/xx_bmp_make.h"

#define XX_BMP_MAKE_MAX_DIMENSION 16384U

static bool bmp_u16(xx_buf_t *out, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return xx_buf_append(out, bytes, sizeof(bytes));
}

static bool bmp_u32(xx_buf_t *out, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return xx_buf_append(out, bytes, sizeof(bytes));
}

static uint32_t bmp_stride(uint32_t width)
{
    return ((width * 24U + 31U) / 32U) * 4U;
}

bool xx_bmp_make_dib(xx_buf_t *out, uint32_t width, uint32_t height, uint32_t rgb)
{
    uint8_t pixel[3] = {(uint8_t)rgb, (uint8_t)(rgb >> 8), (uint8_t)(rgb >> 16)};
    uint32_t stride;
    uint32_t row;
    uint32_t index;
    if (!out || !width || !height || width > XX_BMP_MAKE_MAX_DIMENSION || height > XX_BMP_MAKE_MAX_DIMENSION) return false;
    stride = bmp_stride(width);
    if (!bmp_u32(out, 40U) || !bmp_u32(out, width) || !bmp_u32(out, height) || !bmp_u16(out, 1U) || !bmp_u16(out, 24U) || !bmp_u32(out, 0U) ||
        !bmp_u32(out, stride * height) || !bmp_u32(out, 0U) || !bmp_u32(out, 0U) || !bmp_u32(out, 0U) || !bmp_u32(out, 0U))
        return false;
    for (row = 0U; row < height; ++row) {
        for (index = 0U; index < width; ++index)
            if (!xx_buf_append(out, pixel, sizeof(pixel))) return false;
        for (index = width * 3U; index < stride; ++index)
            if (!xx_buf_append(out, "", 1U)) return false;
    }
    return xx_buf_ok(out);
}

bool xx_bmp_make_file(xx_buf_t *out, uint32_t width, uint32_t height, uint32_t rgb)
{
    uint32_t dib_size;
    if (!out || !width || !height || width > XX_BMP_MAKE_MAX_DIMENSION || height > XX_BMP_MAKE_MAX_DIMENSION) return false;
    dib_size = 40U + bmp_stride(width) * height;
    /* BITMAPFILEHEADER: pixels follow the 14 + 40 header bytes directly. */
    return xx_buf_append(out, "BM", 2U) && bmp_u32(out, 14U + dib_size) && bmp_u32(out, 0U) && bmp_u32(out, 54U) && xx_bmp_make_dib(out, width, height, rgb);
}
