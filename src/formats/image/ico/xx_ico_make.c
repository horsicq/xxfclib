/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/image/ico/xx_ico_make.h"

static bool ico_u8(xx_buf_t *out, uint8_t value)
{
    return xx_buf_append(out, &value, 1U);
}

static bool ico_u16(xx_buf_t *out, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return xx_buf_append(out, bytes, sizeof(bytes));
}

static bool ico_u32(xx_buf_t *out, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return xx_buf_append(out, bytes, sizeof(bytes));
}

static bool ico_zeros(xx_buf_t *out, size_t count)
{
    static const uint8_t zeros[64] = {0};
    while (count) {
        size_t chunk = count < sizeof(zeros) ? count : sizeof(zeros);
        if (!xx_buf_append(out, zeros, chunk)) return false;
        count -= chunk;
    }
    return true;
}

static size_t ico_dib_size(uint32_t size)
{
    return 40U + (size_t)size * size * 4U + (size_t)size * (((size + 31U) / 32U) * 4U);
}

bool xx_ico_make_dib(xx_buf_t *out, uint32_t size, uint32_t argb)
{
    uint32_t pixel;
    if (!out || size == 0U || size > 256U) return false;
    /* BITMAPINFOHEADER: the height covers the XOR and AND bitmaps. */
    if (!ico_u32(out, 40U) || !ico_u32(out, size) || !ico_u32(out, size * 2U) || !ico_u16(out, 1U) || !ico_u16(out, 32U) || !ico_zeros(out, 24U)) return false;
    for (pixel = 0U; pixel < size * size; ++pixel)
        if (!ico_u32(out, argb)) return false;
    return ico_zeros(out, (size_t)size * (((size + 31U) / 32U) * 4U));
}

bool xx_ico_make_file(xx_buf_t *out, const xx_ico_make_image *images, size_t count)
{
    size_t index;
    uint32_t offset;
    if (!out || !images || count == 0U || count > 0xFFFFU) return false;
    for (index = 0U; index < count; ++index)
        if (images[index].size == 0U || images[index].size > 256U) return false;
    if (!ico_u16(out, 0U) || !ico_u16(out, 1U) || !ico_u16(out, (uint16_t)count)) return false;
    offset = (uint32_t)(6U + 16U * count);
    for (index = 0U; index < count; ++index) {
        uint32_t size = images[index].size;
        uint32_t bytes = (uint32_t)ico_dib_size(size);
        /* ICONDIRENTRY: 256 is stored as 0. */
        if (!ico_u8(out, (uint8_t)(size & 0xFFU)) || !ico_u8(out, (uint8_t)(size & 0xFFU)) || !ico_u8(out, 0U) || !ico_u8(out, 0U) || !ico_u16(out, 1U) ||
            !ico_u16(out, 32U) || !ico_u32(out, bytes) || !ico_u32(out, offset))
            return false;
        offset += bytes;
    }
    for (index = 0U; index < count; ++index)
        if (!xx_ico_make_dib(out, images[index].size, images[index].argb)) return false;
    return xx_buf_ok(out);
}
