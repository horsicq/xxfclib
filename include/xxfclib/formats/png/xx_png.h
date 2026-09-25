/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_png.h @brief PNG image (and APNG), validated by walking its
 *  chunks to the IEND chunk. */

/* A port of binwalk's "PNG image" signature: src/signatures/png.rs (the
 * 16-byte magic), src/structures/png.rs (the chunk header) and
 * src/extractors/png.rs (get_png_data_size, the chunk walk that finds IEND
 * and therefore the carve length).
 *
 *   file
 *     +0x00  89 50 4E 47 0D 0A 1A 0A     PNG signature
 *     +0x08  chunk, chunk, ... IEND chunk
 *
 *   chunk
 *     +0x00  u32 BE  data length (not counting itself, the type or the CRC)
 *     +0x04  4 bytes chunk type, ASCII letters ("IHDR", "IDAT", "IEND", ...)
 *     +0x08  data
 *     +len   u32 BE  CRC-32 over the type and the data
 *
 * binwalk's magic already includes the first chunk header, 00 00 00 0D
 * "IHDR": IHDR must come first and its data is always 13 bytes (width u32,
 * height u32, bit depth, colour type, compression, filter, interlace).  The
 * image ends right after the CRC of the IEND chunk; bytes past it are
 * overlay.  An APNG is an ordinary PNG with extra acTL / fcTL / fdAT chunks
 * and is walked the same way.
 *
 * Not an archive: binwalk carves the image itself (image.png) and nothing
 * else, so this reader validates and reports the format size.
 *
 * This is not the Detect-It-Easy metadata helper in src/formats/png/xpng.h
 * (type XPNG); the two share a directory and nothing else.  The user-facing
 * alias here is therefore XPngImage. */

#ifndef XXFCLIB_FORMAT_PNG_H
#define XXFCLIB_FORMAT_PNG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** binwalk's magic: the 8-byte signature plus the IHDR chunk header. */
#define XX_PNG_MAGIC_SIZE 16U
/** The 8-byte PNG file signature alone. */
#define XX_PNG_SIGNATURE_SIZE 8U
/** Chunk header (length + type) and trailing CRC. */
#define XX_PNG_CHUNK_HEADER_SIZE 8U
#define XX_PNG_CHUNK_CRC_SIZE 4U
/** IHDR data is always 13 bytes. */
#define XX_PNG_IHDR_DATA_SIZE 13U
/** Smallest stream that can validate: signature, IHDR chunk, IEND chunk. */
#define XX_PNG_MIN_SIZE 45

/** IHDR colour types. */
#define XX_PNG_COLOUR_GRAYSCALE 0U
#define XX_PNG_COLOUR_RGB 2U
#define XX_PNG_COLOUR_PALETTE 3U
#define XX_PNG_COLOUR_GRAYSCALE_ALPHA 4U
#define XX_PNG_COLOUR_RGBA 6U

typedef struct xx_png xx_png;
typedef struct xx_png xx_png_t;
typedef struct xx_png XPngImage;

struct xx_png {
    Abstractformat format;   /**< Base format structure (first member) */
    int64_t image_end;       /**< Absolute offset just past the IEND CRC */
    uint32_t width;          /**< IHDR width, pixels */
    uint32_t height;         /**< IHDR height, pixels */
    uint8_t bit_depth;       /**< IHDR bit depth */
    uint8_t colour_type;     /**< IHDR colour type (XX_PNG_COLOUR_*) */
    uint8_t interlace;       /**< IHDR interlace method (0 none, 1 Adam7) */
    uint32_t chunk_count;    /**< Chunks walked, IHDR and IEND included */
    uint32_t idat_count;     /**< IDAT chunks */
    int64_t idat_size;       /**< Sum of the IDAT data lengths */
    bool is_animated;        /**< An acTL chunk is present (APNG) */
    uint32_t frame_count;    /**< acTL num_frames, 0 when not animated */
    uint32_t play_count;     /**< acTL num_plays (0 = forever) */
};

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_png_init(xx_png *png, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_png *xx_png_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_png_destroy(xx_png *png);
XXFC_API void xx_png_free(xx_png *png);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_png_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_png_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_png_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);

/* --- Prefilter helper --- */
/** True when @p data (the first @p size bytes of a candidate) starts with
 *  binwalk's 16-byte PNG magic.  Pure byte test, no I/O. */
XXFC_API bool xx_png_check_magic(const uint8_t *data, size_t size);

/* --- Getters --- */
XXFC_API int64_t xx_png_get_image_end(const xx_png *png);
XXFC_API uint32_t xx_png_get_width(const xx_png *png);
XXFC_API uint32_t xx_png_get_height(const xx_png *png);
XXFC_API uint8_t xx_png_get_bit_depth(const xx_png *png);
XXFC_API uint8_t xx_png_get_colour_type(const xx_png *png);
XXFC_API uint8_t xx_png_get_interlace(const xx_png *png);
XXFC_API uint32_t xx_png_get_chunk_count(const xx_png *png);
XXFC_API uint32_t xx_png_get_idat_count(const xx_png *png);
XXFC_API bool xx_png_is_animated(const xx_png *png);
XXFC_API uint32_t xx_png_get_frame_count(const xx_png *png);

/* Cast helpers */
static inline Abstractformat *xx_png_to_format(xx_png *png) {
    return png ? &png->format : NULL;
}

static inline const Abstractformat *xx_png_to_format_const(const xx_png *png) {
    return png ? &png->format : NULL;
}

/* User-facing aliases.  XPngImage, not XPNG: see the note at the top. */
static inline void XPngImage_init(xx_png *png, xx_io_device *dev,
                                  int64_t base_address) {
    xx_png_init(png, dev, base_address);
}

static inline xx_png *XPngImage_create(xx_io_device *dev,
                                       int64_t base_address) {
    return xx_png_create(dev, base_address);
}

static inline void XPngImage_free(xx_png *png) { xx_png_free(png); }

static inline bool XPngImage_is_valid(xx_png *png, xx_pd_struct *pd) {
    return png ? xx_format_is_valid(&png->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PNG_H */
