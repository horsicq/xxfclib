/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * @file xx_image_codec.h
 * @brief The abstract image codec interface and the helpers codecs use.
 *
 * A codec is one constant xx_image_codec object:
 *
 *   const xx_image_codec xx_image_codec_qoi = {
 *       "qoi", "Quite OK Image", "qoi", "image/qoi",
 *       XX_IMAGE_CODEC_LOAD | XX_IMAGE_CODEC_SAVE | XX_IMAGE_CODEC_ALPHA,
 *       XX_FILE_TYPE_QOI, qoi_probe, qoi_load, qoi_save};
 *
 * and one line in src/formats/image/xx_image_codecs.inc.  Rules for every codec:
 *
 *  - probe(data, size) returns 0 (not this format) .. 100 (certain).  It
 *    looks only at the bytes given (size may be small) and never allocates.
 *  - load() decodes from memory into an image that arrives initialised
 *    (xx_image_init).  It checks every length and offset against size
 *    before reading, never trusts header sizes, refuses frames larger than
 *    xx_image_codec_max_pixels(options) with XX_IMAGE_ERR_LIMIT, fills image->info
 *    (codec id and format name are set by the caller) and adds metadata with
 *    the xx_image_add_prop_* helpers unless options->skip_metadata is set.  On
 *    error it returns the status and may leave partial data; the caller frees.
 *  - save() encodes image frame options->frame (or every frame when
 *    options->all_frames and the codec has XX_IMAGE_CODEC_MULTIFRAME) into the
 *    growable buffer @p out.  Frames are RGBA8; a codec without alpha
 *    composites onto black, an indexed codec quantises (xx_image_quantize).
 *  - No global state, no stdio, no file I/O: memory in, memory out.
 */

#ifndef XXFCLIB_FORMAT_IMAGE_CODEC_H
#define XXFCLIB_FORMAT_IMAGE_CODEC_H

#include "xxfclib/formats/image/xx_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Growable output buffer for encoders. */
typedef struct xx_image_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool failed; /**< sticky allocation failure */
} xx_image_buffer;

typedef int (*xx_image_probe_fn)(const uint8_t *data, size_t size);
typedef xx_image_status (*xx_image_load_fn)(const uint8_t *data, size_t size, const xx_image_load_options *options, xx_image *image);
typedef xx_image_status (*xx_image_save_fn)(const xx_image *image, const xx_image_save_options *options, xx_image_buffer *out);

/** The abstract codec interface. */
struct xx_image_codec {
    const char *id;         /**< short lower-case id: "png" */
    const char *name;       /**< display name */
    const char *extensions; /**< ';'-separated, lower case, no dots; the first is the default */
    const char *mime_type;  /**< "" when none */
    uint32_t flags;         /**< XX_IMAGE_CODEC_* */
    int file_type;          /**< xx_file_type_t of xxfclib, 0 when none */
    xx_image_probe_fn probe;   /**< NULL: never auto-detected */
    xx_image_load_fn load;     /**< NULL unless XX_IMAGE_CODEC_LOAD */
    xx_image_save_fn save;     /**< NULL unless XX_IMAGE_CODEC_SAVE */
};

/* ---- buffer ------------------------------------------------------------- */

XXFC_API bool xx_image_buf_reserve(xx_image_buffer *b, size_t capacity);
XXFC_API bool xx_image_buf_append(xx_image_buffer *b, const void *data, size_t size);
XXFC_API bool xx_image_buf_u8(xx_image_buffer *b, uint8_t v);
XXFC_API bool xx_image_buf_u16le(xx_image_buffer *b, uint16_t v);
XXFC_API bool xx_image_buf_u16be(xx_image_buffer *b, uint16_t v);
XXFC_API bool xx_image_buf_u32le(xx_image_buffer *b, uint32_t v);
XXFC_API bool xx_image_buf_u32be(xx_image_buffer *b, uint32_t v);
XXFC_API bool xx_image_buf_fill(xx_image_buffer *b, uint8_t v, size_t count);
XXFC_API bool xx_image_buf_printf(xx_image_buffer *b, const char *format, ...);
/** Overwrites bytes already written (for back-patched sizes). */
XXFC_API void xx_image_buf_put_u32le_at(xx_image_buffer *b, size_t offset, uint32_t v);
XXFC_API void xx_image_buf_put_u32be_at(xx_image_buffer *b, size_t offset, uint32_t v);
XXFC_API void xx_image_buf_free(xx_image_buffer *b);

/* ---- reading ------------------------------------------------------------ */

static inline uint16_t xx_image_rd_u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint16_t xx_image_rd_u16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t xx_image_rd_u32le(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint32_t xx_image_rd_u32be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }

/** True when [offset, offset + length) lies inside size (overflow-safe). */
static inline bool xx_image_in_range(size_t size, size_t offset, size_t length) { return offset <= size && length <= size - offset; }

/** options->max_pixels, or the default when options is NULL or it is 0. */
XXFC_API uint64_t xx_image_codec_max_pixels(const xx_image_load_options *options);
/** XX_IMAGE_OK when width*height is non-zero and within the limit. */
XXFC_API xx_image_status xx_image_check_dimensions(uint32_t width, uint32_t height, const xx_image_load_options *options);

/* ---- building the image -------------------------------------------------- */

/** Appends a zeroed frame of width*height RGBA8 pixels; NULL on failure. */
XXFC_API xx_image_frame *xx_image_add_frame(xx_image *image, uint32_t width, uint32_t height);

XXFC_API xx_image_status xx_image_add_prop_text(xx_image *image, const char *group, const char *key, const char *value);
XXFC_API xx_image_status xx_image_add_prop_textn(xx_image *image, const char *group, const char *key, const char *value, size_t length);
XXFC_API xx_image_status xx_image_add_prop_int(xx_image *image, const char *group, const char *key, int64_t value);
XXFC_API xx_image_status xx_image_add_prop_uint(xx_image *image, const char *group, const char *key, uint64_t value);
XXFC_API xx_image_status xx_image_add_prop_real(xx_image *image, const char *group, const char *key, double value);
XXFC_API xx_image_status xx_image_add_prop_bool(xx_image *image, const char *group, const char *key, bool value);
XXFC_API xx_image_status xx_image_add_prop_bytes(xx_image *image, const char *group, const char *key, const void *data, size_t size);

/** Copies a C string into a fixed char array field, truncating. */
XXFC_API void xx_image_set_str(char *dst, size_t capacity, const char *src);
#define XX_IMAGE_SET_STR(field, text) xx_image_set_str((field), sizeof(field), (text))

/* ---- pixels for encoders ------------------------------------------------- */

/** The frame to encode (options->frame clamped), or NULL when the image is empty. */
XXFC_API const xx_image_frame *xx_image_save_frame(const xx_image *image, const xx_image_save_options *options);
/** True when any pixel of @p frame has alpha below 255. */
XXFC_API bool xx_image_frame_has_alpha(const xx_image_frame *frame);
/** True when every pixel is gray (r == g == b). */
XXFC_API bool xx_image_frame_is_gray(const xx_image_frame *frame);

/** Palette quantisation for indexed encoders.  Writes width*height indices
 *  into @p indices and up to @p max_colors (2..256) 0xRRGGBBAA entries into
 *  @p palette; returns the number of entries, 0 on failure.  Exact when the
 *  frame has at most max_colors distinct colours, median cut otherwise. */
XXFC_API uint32_t xx_image_quantize(const xx_image_frame *frame, uint32_t max_colors, uint8_t *indices, uint32_t *palette);

/** Unpacks a 0xRRGGBBAA value into 4 bytes and back. */
static inline void xx_image_put_rgba(uint8_t *p, uint32_t c) { p[0] = (uint8_t)(c >> 24); p[1] = (uint8_t)(c >> 16); p[2] = (uint8_t)(c >> 8); p[3] = (uint8_t)c; }
static inline uint32_t xx_image_get_rgba(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* ---- shared compression -------------------------------------------------- */

/** zlib (RFC 1950) inflate of @p data into a new malloc'ed buffer; *out_size
 *  receives its length.  @p expected is a size hint (0 when unknown); the
 *  result never grows past @p limit bytes.  Uses xxfclib's deflate. */
XXFC_API xx_image_status xx_image_zlib_inflate(const uint8_t *data, size_t size, size_t expected, size_t limit, uint8_t **out, size_t *out_size);
/** Raw deflate (RFC 1951) inflate, same contract. */
XXFC_API xx_image_status xx_image_raw_inflate(const uint8_t *data, size_t size, size_t expected, size_t limit, uint8_t **out, size_t *out_size);
/** zlib deflate of @p data, appended to @p out.  level 0..9, -1 default. */
XXFC_API xx_image_status xx_image_zlib_deflate(const uint8_t *data, size_t size, int level, xx_image_buffer *out);
XXFC_API uint32_t xx_image_crc32(uint32_t crc, const void *data, size_t size);
XXFC_API uint32_t xx_image_adler32(uint32_t adler, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IMAGE_CODEC_H */
