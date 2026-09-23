/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gif.h @brief GIF (87a / 89a) image: validation and exact size. */

/* A GIF is a 13-byte header, an optional global colour table, a sequence of
 * blocks and a one-byte trailer.  This reader walks every block to the
 * trailer, which is the only way to know where a GIF ends: there is no length
 * field anywhere in the format.
 *
 *   header (13 bytes, little endian)
 *     +0x00  "GIF87a" or "GIF89a"
 *     +0x06  u16  logical screen width
 *     +0x08  u16  logical screen height
 *     +0x0A  u8   flags: bit 7 = global colour table present,
 *                 bits 0-2 = n, the table holds 2^(n+1) RGB triplets
 *     +0x0B  u8   background colour index
 *     +0x0C  u8   pixel aspect ratio
 *     +0x0D  global colour table, 3 * 2^(n+1) bytes, when bit 7 is set
 *
 *   blocks, repeated until the trailer
 *     0x2C  image descriptor: 10 bytes (the last is a flags byte laid out
 *           like the header's, announcing a local colour table), the
 *           optional local colour table, one LZW minimum-code-size byte,
 *           then data sub-blocks
 *     0x21  extension: 0x21, a label byte, then data sub-blocks.  For the
 *           Application (0xFF) and Plain Text (0x01) labels the first
 *           sub-block is a fixed-size field and is skipped by its own
 *           length byte even when that byte is zero.
 *     0x3B  trailer: the GIF ends immediately after this byte
 *   data sub-blocks: <u8 n><n bytes>..., ended by a zero length byte.
 *
 * Any other byte where a block type is expected, or a structure that runs
 * past the end of the device, makes the image invalid.  So does a GIF with
 * no trailer: a truncated GIF has no provable end.
 *
 * Source: binwalk's src/signatures/gif.rs, src/structures/gif.rs and
 * src/extractors/gif.rs, whose carve length this reader reproduces exactly
 * (including the Application/Plain Text first-field rule above), and the
 * GIF89a specification.  binwalk extracts the GIF itself ("image.gif") and
 * declines even that at offset 0, so this is NOT an archive: it validates,
 * reports the image size, and leaves everything after the trailer as
 * overlay.  Pixel data is not decoded; the LZW stream is only walked.
 */

#ifndef XXFCLIB_FORMAT_GIF_H
#define XXFCLIB_FORMAT_GIF_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_GIF_SIGNATURE_87A "GIF87a"
#define XX_GIF_SIGNATURE_89A "GIF89a"
#define XX_GIF_SIGNATURE_SIZE 6U
#define XX_GIF_HEADER_SIZE 13U
/** 10-byte image descriptor, without its colour table and LZW byte. */
#define XX_GIF_IMAGE_DESCRIPTOR_SIZE 10U

#define XX_GIF_BLOCK_EXTENSION 0x21U
#define XX_GIF_BLOCK_IMAGE 0x2CU
#define XX_GIF_BLOCK_TRAILER 0x3BU

#define XX_GIF_EXTENSION_PLAIN_TEXT 0x01U
#define XX_GIF_EXTENSION_GRAPHIC_CONTROL 0xF9U
#define XX_GIF_EXTENSION_COMMENT 0xFEU
#define XX_GIF_EXTENSION_APPLICATION 0xFFU

typedef struct xx_gif xx_gif;
typedef struct xx_gif xx_gif_t;
typedef struct xx_gif XGif;

struct xx_gif {
    Abstractformat format;
    uint16_t version;          /**< 87 or 89, from the signature. */
    uint16_t width;            /**< Logical screen width. */
    uint16_t height;           /**< Logical screen height. */
    uint8_t flags;             /**< Logical screen descriptor flags. */
    uint8_t bg_color_index;
    uint8_t aspect_ratio;
    uint32_t global_color_table_size; /**< Bytes; 0 when absent. */
    uint64_t number_of_images;        /**< Image descriptors (frames). */
    uint64_t number_of_extensions;    /**< Extension blocks of any label. */
    int64_t trailer_offset;           /**< Absolute offset of 0x3B, or -1. */
};

XXFC_API void xx_gif_init(xx_gif *gif, xx_io_device *dev, int64_t base_address);
XXFC_API xx_gif *xx_gif_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_gif_destroy(xx_gif *gif);
XXFC_API void xx_gif_free(xx_gif *gif);

XXFC_API bool xx_gif_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gif_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_gif_get_format_size(Abstractformat *self, xx_pd_struct *pd);

XXFC_API uint16_t xx_gif_get_version(const xx_gif *gif);
XXFC_API uint16_t xx_gif_get_width(const xx_gif *gif);
XXFC_API uint16_t xx_gif_get_height(const xx_gif *gif);
XXFC_API uint32_t xx_gif_get_global_color_table_size(const xx_gif *gif);
XXFC_API uint64_t xx_gif_get_number_of_images(const xx_gif *gif);
XXFC_API uint64_t xx_gif_get_number_of_extensions(const xx_gif *gif);
XXFC_API int64_t xx_gif_get_trailer_offset(const xx_gif *gif);

static inline Abstractformat *xx_gif_to_format(xx_gif *gif) {
    return gif ? &gif->format : NULL;
}
static inline void XGif_init(xx_gif *gif, xx_io_device *dev,
                             int64_t base_address) {
    xx_gif_init(gif, dev, base_address);
}
static inline xx_gif *XGif_create(xx_io_device *dev, int64_t base_address) {
    return xx_gif_create(dev, base_address);
}
static inline void XGif_free(xx_gif *gif) { xx_gif_free(gif); }
static inline bool XGif_is_valid(xx_gif *gif, xx_pd_struct *pd) {
    return gif ? xx_format_is_valid(&gif->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GIF_H */
