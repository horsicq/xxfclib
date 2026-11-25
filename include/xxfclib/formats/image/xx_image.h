/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * @file xx_image.h
 * @brief Image model and codecs: load, inspect and save raster images.
 *
 *   loadable formats --load--> xx_image --save--> saveable formats
 *
 * Every codec implements the abstract interface xx_image_codec (xx_image_codec.h).
 * Loading turns the file into one xx_image: the pixels of every frame as
 * straight-alpha RGBA8 (top-down rows), the source description (xx_image_info),
 * an ordered list of typed metadata properties, the palette, and optionally
 * the source bytes.  Saving picks a codec that has XX_IMAGE_CODEC_SAVE.
 *
 * Formats with no pixel decoder still get their header properties through
 * xxfclib's abstract image interface (xx_image_props.h): xx_image_inspect_*
 * works for every image format xxfclib knows.
 *
 * All functions are reentrant; the codec table is static.
 */

#ifndef XXFCLIB_FORMAT_IMAGE_H
#define XXFCLIB_FORMAT_IMAGE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/image/xx_image_props.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_image_status {
    XX_IMAGE_OK = 0,
    XX_IMAGE_ERR_ARGUMENT,    /**< a NULL or out-of-range argument */
    XX_IMAGE_ERR_MEMORY,      /**< allocation failed */
    XX_IMAGE_ERR_IO,          /**< a file could not be read or written */
    XX_IMAGE_ERR_FORMAT,      /**< no codec recognises the data */
    XX_IMAGE_ERR_CORRUPT,     /**< the data is truncated or inconsistent */
    XX_IMAGE_ERR_UNSUPPORTED, /**< a valid file uses a feature this codec lacks */
    XX_IMAGE_ERR_LIMIT,       /**< larger than xx_image_load_options.max_pixels */
    XX_IMAGE_ERR_NO_DECODER,  /**< the format is known (properties filled) but not decodable */
    XX_IMAGE_ERR_NO_ENCODER,  /**< the codec cannot save */
    XX_IMAGE_ERR_COUNT
} xx_image_status;

/** Value type of a metadata property. */
typedef enum xx_image_prop_type {
    XX_IMAGE_PROP_TEXT = 0, /**< UTF-8 text */
    XX_IMAGE_PROP_INT,      /**< signed integer */
    XX_IMAGE_PROP_UINT,     /**< unsigned integer */
    XX_IMAGE_PROP_REAL,     /**< double */
    XX_IMAGE_PROP_BOOL,     /**< true / false */
    XX_IMAGE_PROP_BYTES     /**< raw bytes (ICC profile, EXIF block, ...) */
} xx_image_prop_type;

/** One metadata property.  group is a section such as "IHDR", "EXIF",
 *  "header", "text"; key is the field name inside it. */
typedef struct xx_image_property {
    char *group;
    char *key;
    xx_image_prop_type type;
    union {
        int64_t i;
        uint64_t u;
        double r;
        bool b;
    } v;
    char *text;    /**< XX_IMAGE_PROP_TEXT: the value (owned) */
    uint8_t *data; /**< XX_IMAGE_PROP_BYTES: the value (owned) */
    size_t size;   /**< XX_IMAGE_PROP_BYTES: length of data */
} xx_image_property;

/** One decoded frame / page / sub-image. */
typedef struct xx_image_frame {
    uint32_t width;     /**< pixels */
    uint32_t height;    /**< pixels */
    int32_t x;          /**< position on the logical screen (animations) */
    int32_t y;
    uint32_t delay_ms;  /**< display time for animations, 0 for stills */
    uint32_t dispose;   /**< codec-specific disposal method, 0 = none */
    uint8_t *rgba;      /**< width * height * 4 bytes, R G B A, top row first */
} xx_image_frame;

/** Description of the source encoding. */
typedef struct xx_image_info {
    char codec[16];                /**< id of the codec that loaded it ("png") */
    char format_name[64];          /**< display name */
    char file_type[64];            /**< xxfclib's name for the detected type, "" if none */
    xx_image_color_model color_model; /**< stored colour model */
    uint32_t bits_per_pixel;       /**< stored bits per pixel */
    uint32_t bits_per_sample;      /**< stored bits per channel, 0 if mixed */
    uint32_t channels;             /**< stored channels */
    bool has_alpha;                /**< stored alpha or transparency */
    bool is_interlaced;
    bool is_animated;
    bool is_compressed;
    bool is_lossy;
    uint32_t frame_count;          /**< frames in the source (may exceed decoded ones) */
    uint32_t mipmap_count;
    uint32_t layer_count;
    uint32_t palette_entries;
    double dpi_x;
    double dpi_y;
    char compression[32];
    char pixel_format[32];
    char color_space[32];
    char version[32];
} xx_image_info;

/** The loaded image: every byte and property the codecs produced. */
typedef struct xx_image {
    uint32_t width;           /**< logical screen / canvas size */
    uint32_t height;
    uint32_t background;      /**< 0xRRGGBBAA background colour, 0 when none */
    uint32_t loop_count;      /**< animations: 0 = forever */
    xx_image_frame *frames;      /**< frame_count frames; 0 when only inspected */
    size_t frame_count;
    size_t frame_capacity;
    uint32_t palette[256];    /**< 0xRRGGBBAA source palette */
    uint32_t palette_count;   /**< entries in palette, 0 when none */
    xx_image_property *props;    /**< metadata, in file order */
    size_t prop_count;
    size_t prop_capacity;
    uint8_t *source;          /**< the source bytes when keep_source was set */
    size_t source_size;
    xx_image_info info;
} xx_image;

/** Codec capability flags. */
#define XX_IMAGE_CODEC_LOAD 0x0001U       /**< can decode pixels */
#define XX_IMAGE_CODEC_SAVE 0x0002U       /**< can encode pixels */
#define XX_IMAGE_CODEC_MULTIFRAME 0x0004U /**< loads / saves several frames */
#define XX_IMAGE_CODEC_ALPHA 0x0008U      /**< saves an alpha channel */
#define XX_IMAGE_CODEC_LOSSY 0x0010U      /**< the encoder is lossy */
#define XX_IMAGE_CODEC_INDEXED 0x0020U    /**< the encoder writes palette images (quantises) */
#define XX_IMAGE_CODEC_INFO 0x0040U       /**< contributes header metadata */

typedef struct xx_image_codec xx_image_codec;

typedef struct xx_image_load_options {
    uint64_t max_pixels;  /**< refuse frames above this many pixels; 0 = 1 << 28 */
    bool first_frame_only;/**< decode only frame 0 */
    bool keep_source;     /**< copy the source bytes into xx_image.source */
    bool skip_metadata;   /**< do not fill props */
} xx_image_load_options;

typedef struct xx_image_save_options {
    int quality;          /**< 1..100 for lossy encoders, 0 = codec default */
    int level;            /**< compression effort 0..9, -1 = codec default */
    int bits_per_pixel;   /**< requested depth (BMP 24/32, TGA 24/32, ...); 0 = automatic */
    bool ascii;           /**< text variant where one exists (PNM P1-P3, XPM) */
    bool rle;             /**< run-length variant where one exists (TGA, BMP RLE8, SGI) */
    bool all_frames;      /**< write every frame when the codec can */
    size_t frame;         /**< frame to write otherwise */
} xx_image_save_options;

/* ---- options ------------------------------------------------------------ */

XXFC_API void xx_image_load_options_init(xx_image_load_options *options);
XXFC_API void xx_image_save_options_init(xx_image_save_options *options);

/* ---- image lifetime ----------------------------------------------------- */

XXFC_API void xx_image_init(xx_image *image);
/** Frees every frame, property and the source copy; leaves an empty image. */
XXFC_API void xx_image_free(xx_image *image);
/** Deep copy; @p dst is overwritten (call xx_image_free on it first). */
XXFC_API xx_image_status xx_image_copy(xx_image *dst, const xx_image *src);

/* ---- codec table -------------------------------------------------------- */

XXFC_API size_t xx_image_codec_count(void);
XXFC_API const xx_image_codec *xx_image_codec_at(size_t index);
/** By id ("png") or extension ("jpg", ".JPG"); NULL when none. */
XXFC_API const xx_image_codec *xx_image_codec_find(const char *id_or_extension);
/** The codec for a file name's extension; NULL when none. */
XXFC_API const xx_image_codec *xx_image_codec_for_path(const char *path);
/** Highest-scoring codec whose probe accepts @p data; NULL when none. */
XXFC_API const xx_image_codec *xx_image_codec_detect(const void *data, size_t size);

XXFC_API const char *xx_image_codec_id(const xx_image_codec *codec);
XXFC_API const char *xx_image_codec_name(const xx_image_codec *codec);
XXFC_API const char *xx_image_codec_extensions(const xx_image_codec *codec);
XXFC_API const char *xx_image_codec_mime_type(const xx_image_codec *codec);
XXFC_API uint32_t xx_image_codec_flags(const xx_image_codec *codec);
XXFC_API bool xx_image_codec_can_load(const xx_image_codec *codec);
XXFC_API bool xx_image_codec_can_save(const xx_image_codec *codec);

/* ---- load / inspect ----------------------------------------------------- */

/** Decodes @p data.  @p codec may be NULL to detect.  On XX_IMAGE_ERR_NO_DECODER
 *  the image still carries info and props from xxfclib. */
XXFC_API xx_image_status xx_image_load_memory(const void *data, size_t size, const xx_image_codec *codec, const xx_image_load_options *options, xx_image *image);
/** UTF-8 path. */
XXFC_API xx_image_status xx_image_load_file(const char *path, const xx_image_codec *codec, const xx_image_load_options *options, xx_image *image);

/** Header-only: fills info and props, no frames.  Works for every image
 *  format with an xxfclib image interface row, decodable or not. */
XXFC_API xx_image_status xx_image_inspect_memory(const void *data, size_t size, xx_image *image);
XXFC_API xx_image_status xx_image_inspect_file(const char *path, xx_image *image);

/* ---- save --------------------------------------------------------------- */

/** Encodes into a new malloc'ed buffer the caller frees with xx_image_mem_free. */
XXFC_API xx_image_status xx_image_save_memory(const xx_image *image, const xx_image_codec *codec, const xx_image_save_options *options, uint8_t **data, size_t *size);
/** @p codec NULL picks the codec from the path's extension. */
XXFC_API xx_image_status xx_image_save_file(const xx_image *image, const char *path, const xx_image_codec *codec, const xx_image_save_options *options);
XXFC_API void xx_image_mem_free(void *pointer);

/* ---- properties --------------------------------------------------------- */

XXFC_API const xx_image_property *xx_image_find_prop(const xx_image *image, const char *group, const char *key);
/** Writes the value as text ("1920", "true", "3.5", "<4096 bytes>"). */
XXFC_API size_t xx_image_prop_format(const xx_image_property *prop, char *buffer, size_t size);

/* ---- pixel helpers ------------------------------------------------------ */

/** A new single frame image of the given size (pixels set to 0). */
XXFC_API xx_image_status xx_image_create(xx_image *image, uint32_t width, uint32_t height);
/** Pixel of frame 0 as 0xRRGGBBAA; 0 outside. */
XXFC_API uint32_t xx_image_get_pixel(const xx_image *image, size_t frame, uint32_t x, uint32_t y);
/* ---- names -------------------------------------------------------------- */

XXFC_API const char *xx_image_status_string(xx_image_status status);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IMAGE_H */
