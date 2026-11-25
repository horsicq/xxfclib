/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * @file xx_image_props.h
 * @brief Abstract image interface: format-independent image properties.
 *
 * The image readers under formats/image validate and carve their formats;
 * each one keeps its own header fields.  This interface gives every image
 * format the same small, read-only description:
 *
 *   dimensions, bits per pixel / sample, channels, colour model, alpha,
 *   interlacing, frame / mipmap / layer counts, palette size, resolution,
 *   compression and pixel-format names, and where the pixel data lies.
 *
 * A format takes part by registering one xx_image_interface row (see
 * src/formats/image/xx_image_props.c).  A row's reader parses only the
 * headers it needs; it never decodes pixels.  The table is static and
 * needs no initialisation, so it is safe from any thread.
 *
 * Nothing here changes Abstractformat, so adding formats keeps the ABI.
 */

#ifndef XXFCLIB_FORMAT_IMAGE_PROPS_H
#define XXFCLIB_FORMAT_IMAGE_PROPS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How the stored samples describe colour. */
typedef enum xx_image_color_model {
    XX_IMAGE_COLOR_UNKNOWN = 0,
    XX_IMAGE_COLOR_BILEVEL,    /**< 1 bit per pixel, black and white */
    XX_IMAGE_COLOR_GRAY,       /**< luminance only */
    XX_IMAGE_COLOR_GRAY_ALPHA, /**< luminance and alpha */
    XX_IMAGE_COLOR_RGB,        /**< red, green, blue */
    XX_IMAGE_COLOR_RGBA,       /**< red, green, blue, alpha */
    XX_IMAGE_COLOR_INDEXED,    /**< palette indices */
    XX_IMAGE_COLOR_CMYK,       /**< cyan, magenta, yellow, key */
    XX_IMAGE_COLOR_YCBCR,      /**< luma and chroma (JPEG and video formats) */
    XX_IMAGE_COLOR_LAB,        /**< CIE L*a*b* */
    XX_IMAGE_COLOR_HDR,        /**< floating point or shared-exponent RGB */
    XX_IMAGE_COLOR_COMPRESSED, /**< GPU block compression (BCn, ETC, ASTC, PVRTC) */
    XX_IMAGE_COLOR_VECTOR,     /**< vector graphics: no pixel grid */
    XX_IMAGE_COLOR_MULTI,      /**< several models in one file (for example ICO) */
    XX_IMAGE_COLOR_COUNT
} xx_image_color_model;

/** Flags of an xx_image_interface row. */
#define XX_IMAGE_IF_RASTER 0x0001U   /**< the format stores a pixel grid */
#define XX_IMAGE_IF_VECTOR 0x0002U   /**< the format stores drawing commands */
#define XX_IMAGE_IF_PALETTE 0x0004U  /**< the format is a colour palette, not an image */
#define XX_IMAGE_IF_TEXTURE 0x0008U  /**< GPU texture container (mipmaps, faces) */
#define XX_IMAGE_IF_MULTI 0x0010U    /**< may hold several images / frames */
#define XX_IMAGE_IF_LOSSY 0x0020U    /**< the usual encoding is lossy */

/** Format-independent properties; fields that do not apply stay 0 / "". */
typedef struct xx_image_props {
    xx_file_type_t file_type;         /**< the format that filled the record */
    uint32_t width;                   /**< pixels */
    uint32_t height;                  /**< pixels */
    uint32_t depth;                   /**< volume textures; 0 otherwise */
    uint32_t bits_per_pixel;          /**< stored bits per pixel, 0 if unknown */
    uint32_t bits_per_sample;         /**< bits of one channel, 0 if mixed / unknown */
    uint32_t channels;                /**< stored channels including alpha */
    xx_image_color_model color_model; /**< see xx_image_color_model */
    bool has_alpha;                   /**< an alpha channel or transparency key */
    bool is_interlaced;               /**< interlaced or progressive storage */
    bool is_animated;                 /**< frames play as an animation */
    bool is_compressed;               /**< pixel data is compressed */
    bool is_lossy;                    /**< the compression in use is lossy */
    bool is_top_down;                 /**< first stored row is the top row */
    uint32_t frame_count;             /**< images / frames / pages, 1 for a still image */
    uint32_t mipmap_count;            /**< mip levels, 0 when none */
    uint32_t layer_count;             /**< layers / array slices / cube faces, 0 when none */
    uint32_t palette_entries;         /**< colours in the palette, 0 when none */
    double dpi_x;                     /**< horizontal resolution, 0 when not stored */
    double dpi_y;                     /**< vertical resolution, 0 when not stored */
    int64_t pixel_data_offset;        /**< relative to the base address, -1 when unknown */
    int64_t pixel_data_size;          /**< bytes, -1 when unknown */
    char compression[32];             /**< "none", "deflate", "LZW", "RLE", "JPEG baseline", ... */
    char pixel_format[32];            /**< "RGB888", "BC1", "1-bit indexed", ... */
    char color_space[32];             /**< "sRGB", "linear", "ICC", "Adobe RGB", ... */
    char version[32];                 /**< format version / variant, "" when none */
} xx_image_props;

/** Reads the properties of the image at @p base_address of @p device.  The
 *  record arrives initialised by xx_image_props_init and with file_type set.
 *  Returns false when the data is not this format or is too short.  Must not
 *  move the device cursor in a way the caller depends on (use xx_io_read_at). */
typedef bool (*xx_image_props_reader)(xx_io_device *device, int64_t base_address, xx_image_props *props);

/** One row of the abstract image interface table. */
typedef struct xx_image_interface {
    xx_file_type_t file_type;   /**< xxfclib file type this row describes */
    const char *id;             /**< short lower-case id, e.g. "png" */
    const char *name;           /**< display name, e.g. "Portable Network Graphics" */
    const char *extensions;     /**< ';'-separated, lower case, no dots: "jpg;jpeg;jpe" */
    const char *mime_type;      /**< e.g. "image/png"; "" when none is registered */
    uint32_t flags;             /**< XX_IMAGE_IF_* */
    xx_image_props_reader read; /**< NULL when only the description is known */
} xx_image_interface;

/** Resets @p props: numbers 0, strings "", offsets -1, file_type UNKNOWN. */
XXFC_API void xx_image_props_init(xx_image_props *props);

/** Number of rows in the interface table, and row @p index (NULL past the end). */
XXFC_API size_t xx_image_interface_count(void);
XXFC_API const xx_image_interface *xx_image_interface_at(size_t index);

/** The row for @p type, or NULL when the type is not an image format. */
XXFC_API const xx_image_interface *xx_image_interface_get(xx_file_type_t type);

/** The row whose id or one of whose extensions equals @p name (ASCII
 *  case-insensitive, a leading '.' is ignored), or NULL. */
XXFC_API const xx_image_interface *xx_image_interface_find(const char *name);

/** True when @p type has a row with a reader. */
XXFC_API bool xx_image_props_is_supported(xx_file_type_t type);

/** Fills @p props for the image of type @p type at @p base_address.  With
 *  @p type XX_FILE_TYPE_UNKNOWN the type is detected first: the fast
 *  signatures at @p base_address are tried in order and the first row whose
 *  reader accepts the data wins.  Returns false when nothing matched. */
XXFC_API bool xx_image_props_read(xx_io_device *device, int64_t base_address, xx_file_type_t type, xx_image_props *props);

/** Same, for bytes in memory. */
XXFC_API bool xx_image_props_read_memory(const void *data, size_t size, xx_file_type_t type, xx_image_props *props);

/** "RGB", "indexed", ...; never NULL. */
XXFC_API const char *xx_image_color_model_to_string(xx_image_color_model model);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IMAGE_PROPS_H */
