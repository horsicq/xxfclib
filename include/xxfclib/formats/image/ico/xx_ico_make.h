/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://learn.microsoft.com/en-us/previous-versions/ms997538(v=msdn.10)
 * Builders for Windows icon images and .ico files (fixtures, round trips).
 */
#ifndef XX_ICO_MAKE_H
#define XX_ICO_MAKE_H

#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One square, single-color image of an .ico file. */
typedef struct xx_ico_make_image {
    uint32_t size; /**< Width and height in pixels, 1..256. */
    uint32_t argb; /**< Pixel color as 0xAARRGGBB. */
} xx_ico_make_image;

/** Appends a 32-bpp icon DIB: BITMAPINFOHEADER with doubled height, the
 * color pixels (bottom-up) and an all-opaque 1-bpp AND mask. This is the
 * payload of one ICON resource or one .ico directory entry. */
XXFC_API bool xx_ico_make_dib(xx_buf_t *out, uint32_t size, uint32_t argb);

/** Appends a complete .ico file (ICONDIR, entries, DIB payloads in order). */
XXFC_API bool xx_ico_make_file(xx_buf_t *out, const xx_ico_make_image *images, size_t count);

#ifdef __cplusplus
}
#endif

#endif
