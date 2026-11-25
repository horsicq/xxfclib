/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * @file xx_visualization.h
 * @brief Binary visualization: a file as a grid of blocks shaded by a
 *        statistic, with the format's regions and data highlights.
 *
 * The C counterpart of XVisualization (XVisualizationWidget).  A file of N
 * bytes is split into width x height blocks of N / (width * height) bytes
 * (block i starts at floor(i * N / blocks)).  For every block the enabled
 * statistics are computed exactly as XBinary::getBinaryStatus does:
 *
 *   entropy   Shannon entropy in bits per byte, 0..8
 *   gradient  sum of the bytes / (size * 255), 0..1
 *   zeros     share of 0x00 bytes, 0..1
 *   text      share of bytes 32..126, 8, 10, 13, 0..1
 *
 * and turned into a shading value 100..300 per method (value[method]):
 *
 *   NONE            100
 *   ENTROPY         100 + 200 * entropy / 8
 *   GRADIENT        100 + 200 * gradient
 *   ZEROS           300 when the block is all zeros, else 100
 *   ZEROS_GRADIENT  100 + 200 * zeros
 *   TEXT            300 when the block is all text, else 100
 *   TEXT_GRADIENT   100 + 200 * text
 *
 * A block is drawn in its colour made darker by that value, like Qt's
 * QColor::darker(value): every channel times 100 / value (value 100 keeps the
 * colour, 300 leaves a third).  The colour is a highlight's colour when the
 * block lies in an enabled highlight, else an enabled region's colour, else
 * the base colour; a highlight one block long keeps its colour unchanged.
 *
 * Regions are the format's memory-map records for the chosen map mode that
 * are not data (headers, sections, segments, regions, overlay); highlights
 * are the records whose file part is data-like (tables, resources, debug,
 * signatures, streams, objects, values, data).  Records without a file offset
 * are skipped.  Formats without a memory map have neither.
 */

#ifndef XXFCLIB_FORMAT_VISUALIZATION_H
#define XXFCLIB_FORMAT_VISUALIZATION_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/formats/xx_memory_map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_visualization_method {
    XX_VISUALIZATION_NONE = 0,
    XX_VISUALIZATION_ENTROPY,
    XX_VISUALIZATION_GRADIENT,
    XX_VISUALIZATION_ZEROS,
    XX_VISUALIZATION_ZEROS_GRADIENT,
    XX_VISUALIZATION_TEXT,
    XX_VISUALIZATION_TEXT_GRADIENT,
    XX_VISUALIZATION_METHOD_COUNT
} xx_visualization_method;

#define XX_VISUALIZATION_FLAG(method) (1U << (unsigned)(method))
/** NONE, ENTROPY, ZEROS and ZEROS_GRADIENT, as XVisualization::getDefaultMethodFlag. */
#define XX_VISUALIZATION_DEFAULT_METHODS                                                                                       \
    (XX_VISUALIZATION_FLAG(XX_VISUALIZATION_NONE) | XX_VISUALIZATION_FLAG(XX_VISUALIZATION_ENTROPY) |                            \
     XX_VISUALIZATION_FLAG(XX_VISUALIZATION_ZEROS) | XX_VISUALIZATION_FLAG(XX_VISUALIZATION_ZEROS_GRADIENT))
#define XX_VISUALIZATION_ALL_METHODS ((1U << XX_VISUALIZATION_METHOD_COUNT) - 1U)
#define XX_VISUALIZATION_MAX_BLOCKS (4096U * 4096U)

typedef struct xx_visualization_block {
    int64_t offset;   /**< first byte, relative to the device start */
    int64_t size;     /**< bytes in the block (>= 0) */
    double entropy;   /**< computed when ENTROPY is enabled, else 0 */
    double gradient;  /**< GRADIENT */
    double zeros;     /**< ZEROS or ZEROS_GRADIENT */
    double text;      /**< TEXT or TEXT_GRADIENT */
    int16_t value[XX_VISUALIZATION_METHOD_COUNT]; /**< shading 100..300; 100 for methods not computed */
} xx_visualization_block;

typedef struct xx_visualization_area {
    bool enabled;          /**< drawn when true (all start enabled) */
    int64_t offset;        /**< file offset */
    int64_t size;          /**< bytes */
    int64_t block_offset;  /**< first block: floor(offset / block size) */
    int64_t block_count;   /**< blocks covered: ceil(size / block size), at least 1 */
    uint32_t color;        /**< 0xRRGGBB */
    xx_file_part_t file_part;
    char name[XX_MEMORY_RECORD_NAME_SIZE];
} xx_visualization_area;

typedef struct xx_visualization {
    /* input */
    uint32_t width;                  /**< blocks per row (1..4096) */
    uint32_t height;                 /**< rows (1..4096) */
    uint32_t methods;                /**< XX_VISUALIZATION_FLAG bits to compute */
    xx_file_type_t file_type;        /**< XX_FILE_TYPE_UNKNOWN = detect */
    xx_memory_map_mode_t map_mode;   /**< map mode for regions; UNKNOWN = the format's default */
    /* output */
    xx_file_type_t detected_type;    /**< the type used for regions / highlights */
    int64_t file_size;
    double block_size;               /**< bytes per block (file_size / blocks) */
    xx_visualization_block *blocks;  /**< width * height entries */
    size_t block_count;
    xx_visualization_area *regions;
    size_t region_count;
    xx_visualization_area *highlights;
    size_t highlight_count;
} xx_visualization;

/** Zeroes @p vis and sets width = height = 1, methods = DEFAULT. */
XXFC_API void xx_visualization_init(xx_visualization *vis);
/** Frees the blocks, regions and highlights (inputs are kept). */
XXFC_API void xx_visualization_free(xx_visualization *vis);

/** The grid XVisualizationWidget proposes for a file: pieces = size / 0x300;
 *  when pieces > 3: width = min(pieces, 100), height = min(2 * pieces, 200),
 *  else 1 x 1. */
XXFC_API void xx_visualization_default_size(int64_t file_size, uint32_t *width, uint32_t *height);

/** Computes the blocks, regions and highlights of the whole device.  Replaces
 *  any previous output.  Returns false on bad input (size 0, width / height
 *  out of range, more than XX_VISUALIZATION_MAX_BLOCKS), read errors, out of
 *  memory or cancellation through @p pd (which may be NULL); the output is then
 *  empty. */
XXFC_API bool xx_visualization_process(xx_visualization *vis, xx_io_device *device, xx_pd_struct *pd);

/** The same statistics for any byte range (the per-block computation). */
XXFC_API void xx_visualization_stats(const uint8_t *data, size_t size, double *entropy, double *gradient, double *zeros, double *text);

/** Map modes for which the detected format yields at least one record, in
 *  enum order (UNKNOWN, i.e. "default", always first); returns the count. */
XXFC_API size_t xx_visualization_map_modes(xx_io_device *device, xx_file_type_t file_type, xx_memory_map_mode_t *modes, size_t capacity);
XXFC_API const char *xx_visualization_map_mode_name(xx_memory_map_mode_t mode); /* "Default", "Regions", ... */

XXFC_API const char *xx_visualization_method_name(xx_visualization_method method); /* "None", "Entropy", "Gradient", "Zeros", "Zeros(Gradient)", "Text", "Text(Gradient)" */

/** XVisualization::getRegionColor / getHighlightColor palettes (0xRRGGBB). */
XXFC_API uint32_t xx_visualization_region_color(size_t index);
XXFC_API uint32_t xx_visualization_highlight_color(size_t index);

/** 0xRRGGBB made darker like QColor::darker(@p factor), factor >= 100. */
XXFC_API uint32_t xx_visualization_darker(uint32_t rgb, int factor);

/** Colour (0xRRGGBB) of block @p index for @p method over @p base_rgb. */
XXFC_API uint32_t xx_visualization_block_color(const xx_visualization *vis, size_t index, xx_visualization_method method, uint32_t base_rgb);

/** Renders the grid: each block a @p block_px x @p block_px square, into
 *  @p rgba ((width * block_px) x (height * block_px) x 4 bytes, alpha 255).
 *  A block with index @p marked (or SIZE_MAX for none) is filled red, like the
 *  selection marker of XVisualizationImage.  Returns false on bad arguments. */
XXFC_API bool xx_visualization_render(const xx_visualization *vis, xx_visualization_method method, uint32_t base_rgb, uint32_t block_px, size_t marked, uint8_t *rgba);

/** Block index at grid position (x, y), or SIZE_MAX outside. */
XXFC_API size_t xx_visualization_block_at(const xx_visualization *vis, uint32_t x, uint32_t y);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VISUALIZATION_H */
