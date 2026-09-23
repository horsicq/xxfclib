/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * @file xx_bmp.h
 * @brief Windows / OS/2 BMP (device-independent bitmap) file reader.
 *
 * A BMP file is a 14-byte BITMAPFILEHEADER
 *
 *   +0  u16 bfType       "BM"
 *   +2  u32 bfSize       size of the whole file in bytes
 *   +6  u16 bfReserved1
 *   +8  u16 bfReserved2
 *   +10 u32 bfOffBits    offset of the pixel array from the start of the file
 *
 * followed by a DIB header whose first u32 is its own size, which is also its
 * version: 12 (BITMAPCOREHEADER, OS/2 1.x: u16 width/height), 40
 * (BITMAPINFOHEADER), 108 (BITMAPV4HEADER) or 124 (BITMAPV5HEADER).  Those
 * are exactly the four sizes binwalk accepts (src/structures/bmp.rs); the
 * 16/64-byte OS/2 2.x headers and the undocumented 52/56-byte variants are
 * refused, as binwalk refuses them.
 *
 * VALIDATION.  "BM" is only two bytes, so everything else has to carry the
 * identification.  The reader applies every check binwalk's dry run makes
 * (src/signatures/bmp.rs -> src/extractors/bmp.rs):
 *
 *   bfSize != 0 and bfSize <= bytes available from the base address,
 *   bfOffBits != 0 and bfOffBits <= bytes available,
 *   DIB header size in {12, 40, 108, 124},
 *   bfOffBits >= 14 + DIB header size,
 *
 * and then the ones binwalk leaves out, all of which every real encoder
 * satisfies:
 *
 *   bfOffBits <= bfSize (the pixel array starts inside the file),
 *   planes == 1,
 *   width >= 1, height != 0, both at most XX_BMP_MAX_DIMENSION,
 *   (bits per pixel, compression) is a documented pair,
 *   uncompressed pixel rows (without their 4-byte padding, so writers that
 *   forget the padding still pass) fit between bfOffBits and bfSize,
 *   BI_RLE4 / BI_RLE8 have at least one byte of pixel data,
 *   BI_JPEG / BI_PNG carry a JPEG SOI / PNG signature at bfOffBits.
 *
 * SIZE.  The format size is bfSize - the same length binwalk carves.  Bytes
 * after it are overlay.  biSizeImage is recorded but never trusted: real
 * files (binwalk's own tests/inputs/bmp.bin among them) store a value larger
 * than the pixel array.
 *
 * Not an archive: binwalk "extracts" a BMP by carving the image itself, so
 * there are no members to publish.
 */

#ifndef XXFCLIB_FORMAT_BMP_H
#define XXFCLIB_FORMAT_BMP_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** BITMAPFILEHEADER size. */
#define XX_BMP_FILE_HEADER_SIZE 14U
/** DIB header sizes (the only ones accepted). */
#define XX_BMP_CORE_HEADER_SIZE 12U  /* BITMAPCOREHEADER (OS/2 1.x) */
#define XX_BMP_INFO_HEADER_SIZE 40U  /* BITMAPINFOHEADER */
#define XX_BMP_V4_HEADER_SIZE   108U /* BITMAPV4HEADER */
#define XX_BMP_V5_HEADER_SIZE   124U /* BITMAPV5HEADER */

/** biCompression values that may appear in a BMP file. */
#define XX_BMP_BI_RGB            0U
#define XX_BMP_BI_RLE8           1U
#define XX_BMP_BI_RLE4           2U
#define XX_BMP_BI_BITFIELDS      3U
#define XX_BMP_BI_JPEG           4U
#define XX_BMP_BI_PNG            5U
#define XX_BMP_BI_ALPHABITFIELDS 6U

/** Largest width or |height| accepted.  Keeps every size computation far
 *  inside int64 and is well above anything a real encoder writes. */
#define XX_BMP_MAX_DIMENSION 0x1000000U

typedef struct xx_bmp xx_bmp;
typedef struct xx_bmp xx_bmp_t;
typedef struct xx_bmp XBmp;

struct xx_bmp {
    Abstractformat format;     /**< Base format structure (first member) */
    uint32_t file_size;        /**< bfSize: the format size */
    uint32_t data_offset;      /**< bfOffBits, relative to the base address */
    uint32_t dib_header_size;  /**< 12, 40, 108 or 124 */
    uint32_t width;            /**< pixels */
    uint32_t height;           /**< pixels, absolute value */
    bool top_down;             /**< stored height was negative */
    uint16_t bits_per_pixel;   /**< 0 for BI_JPEG / BI_PNG */
    uint32_t compression;      /**< XX_BMP_BI_*; BI_RGB for a core header */
    uint32_t image_size;       /**< biSizeImage as stored (informational) */
    uint32_t colors_used;      /**< biClrUsed as stored (0 for a core header) */
};

XXFC_API void xx_bmp_init(xx_bmp *bmp, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_bmp *xx_bmp_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_bmp_destroy(xx_bmp *bmp);
XXFC_API void xx_bmp_free(xx_bmp *bmp);

XXFC_API bool xx_bmp_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bmp_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bmp_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);

XXFC_API uint32_t xx_bmp_get_width(const xx_bmp *bmp);
XXFC_API uint32_t xx_bmp_get_height(const xx_bmp *bmp);
XXFC_API bool xx_bmp_is_top_down(const xx_bmp *bmp);
XXFC_API uint16_t xx_bmp_get_bits_per_pixel(const xx_bmp *bmp);
XXFC_API uint32_t xx_bmp_get_compression(const xx_bmp *bmp);
XXFC_API uint32_t xx_bmp_get_dib_header_size(const xx_bmp *bmp);
XXFC_API uint32_t xx_bmp_get_data_offset(const xx_bmp *bmp);

static inline Abstractformat *xx_bmp_to_format(xx_bmp *bmp) {
    return bmp ? &bmp->format : NULL;
}
static inline void XBmp_init(xx_bmp *bmp, xx_io_device *dev,
                             int64_t base_address) {
    xx_bmp_init(bmp, dev, base_address);
}
static inline xx_bmp *XBmp_create(xx_io_device *dev, int64_t base_address) {
    return xx_bmp_create(dev, base_address);
}
static inline void XBmp_free(xx_bmp *bmp) { xx_bmp_free(bmp); }
static inline bool XBmp_is_valid(xx_bmp *bmp, xx_pd_struct *pd) {
    return bmp ? xx_format_is_valid(&bmp->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BMP_H */
