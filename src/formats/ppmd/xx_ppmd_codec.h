/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Private interface between the PPMd stream reader (xx_ppmd.c) and its two
 * stream decoders (xx_ppmd_varh.c for PPMd var.H, xx_ppmd_vari.c for PPMd
 * var.I rev.1).  Both use Dmitry Subbotin's carryless range coder, which the
 * library's algo/ppmd7 codec (7z range coder) does not; the context models
 * themselves are the library's algo/ppmd7 and algo/ppmd8 models.
 *
 * The decoders read from a bounded window of a device and report exactly how
 * many packed bytes the stream used, which is what locates the next member
 * and the end of the file format.
 */

#ifndef XX_PPMD_CODEC_H
#define XX_PPMD_CODEC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_PPMDFILE_IN_BUFFER  ((size_t)65536U)
#define XX_PPMDFILE_OUT_BUFFER ((size_t)65536U)

/* A read-only byte source over [position, limit) of a device.  Reading past
 * `limit` does not touch the device: it sets `overrun` and yields 0, the way
 * a range decoder must see a truncated stream. */
typedef struct xx_ppmdfile_source_s {
    xx_io_device *device;
    int64_t next;       /**< Device offset of the next unbuffered byte. */
    int64_t limit;      /**< Device offset one past the last usable byte. */
    uint8_t *buffer;    /**< XX_PPMDFILE_IN_BUFFER bytes, caller owned. */
    size_t length;
    size_t index;
    uint64_t consumed;  /**< Bytes handed to the decoder so far. */
    bool overrun;       /**< The decoder asked for a byte past `limit`. */
    bool io_error;
} xx_ppmdfile_source;

/* Receives decoded bytes.  Returning false aborts the decode. */
typedef bool (*xx_ppmdfile_write_fn)(void *context, const uint8_t *data,
                                     size_t size);

typedef enum xx_ppmdfile_status_e {
    XX_PPMDFILE_OK = 0,        /**< End marker seen, coder finished cleanly. */
    XX_PPMDFILE_TRUNCATED,     /**< The stream ran past the available bytes. */
    XX_PPMDFILE_DATA_ERROR,    /**< Impossible code value or bad finish. */
    XX_PPMDFILE_NO_MEMORY,
    XX_PPMDFILE_WRITE_ERROR,   /**< The sink refused the data. */
    XX_PPMDFILE_STOPPED,       /**< Cancelled through the progress struct. */
    XX_PPMDFILE_LIMIT,         /**< More output than `max_output`. */
    XX_PPMDFILE_BAD_PARAMS
} xx_ppmdfile_status;

void xx_ppmdfile_source_init(xx_ppmdfile_source *source, xx_io_device *device,
                             int64_t offset, int64_t limit, uint8_t *buffer);

/* Next byte, or 0 with `overrun` / `io_error` set. */
uint8_t xx_ppmdfile_source_byte(xx_ppmdfile_source *source);

/* Decode one PPMd var.H stream (Shkarin's PPMd7 model with the carryless
 * coder).  `write` may be NULL to measure only.  `max_output` bounds the
 * decoded size (0 = unbounded).  `out_size` receives the decoded length. */
xx_ppmdfile_status xx_ppmdfile_decode_varh(xx_ppmdfile_source *source,
                                           unsigned order, uint32_t mem_size,
                                           xx_ppmdfile_write_fn write,
                                           void *write_context,
                                           uint64_t max_output,
                                           uint64_t *out_size,
                                           xx_pd_struct *pd);

/* Decode one PPMd var.I rev.1 stream (PPMd8 model); `restore` is 0
 * (restart) or 1 (cut off). */
xx_ppmdfile_status xx_ppmdfile_decode_vari(xx_ppmdfile_source *source,
                                           unsigned order, uint32_t mem_size,
                                           unsigned restore,
                                           xx_ppmdfile_write_fn write,
                                           void *write_context,
                                           uint64_t max_output,
                                           uint64_t *out_size,
                                           xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_PPMD_CODEC_H */
