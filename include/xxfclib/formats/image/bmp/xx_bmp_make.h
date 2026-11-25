/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-storage
 * Builders for Windows DIBs and .bmp files (fixtures, round trips).
 */
#ifndef XX_BMP_MAKE_H
#define XX_BMP_MAKE_H

#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Appends a single-color 24-bpp bottom-up DIB (BITMAPINFOHEADER and
 * DWORD-aligned rows), the payload of a BITMAP resource. rgb is 0xRRGGBB. */
XXFC_API bool xx_bmp_make_dib(xx_buf_t *out, uint32_t width, uint32_t height, uint32_t rgb);

/** Appends a .bmp file: BITMAPFILEHEADER followed by xx_bmp_make_dib. */
XXFC_API bool xx_bmp_make_file(xx_buf_t *out, uint32_t width, uint32_t height, uint32_t rgb);

#ifdef __cplusplus
}
#endif

#endif
