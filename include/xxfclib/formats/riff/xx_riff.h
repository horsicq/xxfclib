/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * @file xx_riff.h
 * @brief RIFF (Resource Interchange File Format) container reader.
 *
 * A RIFF file is one chunk whose ID is "RIFF":
 *
 *   +0  char[4] "RIFF"
 *   +4  u32 LE  riff_size  bytes that follow this field (form type included)
 *   +8  char[4] form type  "WAVE", "AVI ", "WEBP", "ACON", "RMID", "sfbk" ...
 *   +12 chunks  <id:char[4]><size:u32 LE><data:size>[pad byte if size odd]
 *
 * (Microsoft "Multimedia Programming Interface and Data Specifications 1.0",
 * 1991, chapter 2.)  Everything WAV, AVI, WebP, animated cursors, RMID MIDI,
 * DLS/SoundFont banks and many others store sits inside that one chunk.
 *
 * Source: binwalk's "RIFF image" signature - src/signatures/riff.rs,
 * src/structures/riff.rs, src/extractors/riff.rs.  binwalk checks:
 *
 *   the four bytes "RIFF" (u32 LE 0x46464952),
 *   at least 12 bytes present (it parses magic, riff_size and form type),
 *   the form type is valid UTF-8,
 *   riff_size + 8 <= bytes available (the analysis loop drops any signature
 *   whose size runs past the end of the data: src/binwalk.rs).
 *
 * and reports result.size = riff_size + 8.
 *
 * VALIDATION.  The reader applies all of those and, because the magic is
 * only four bytes, also the RIFF rules binwalk does not look at:
 *
 *   riff_size >= 12: the body holds the form type and at least one chunk,
 *   the form type is four printable ASCII bytes (0x20..0x7E), not all spaces,
 *   the top-level chunk list tiles the declared body exactly: every chunk ID
 *   is four printable ASCII bytes, every chunk fits inside riff_size + 8, the
 *   odd-size pad byte is honoured, and the walk ends on the declared end.
 *   The single tolerated deviation is the one real writers make: an odd last
 *   chunk whose pad byte lies outside riff_size (the walk ends one byte past
 *   the declared end).  At most XX_RIFF_MAX_CHUNKS top-level chunks are
 *   walked; LIST bodies are not descended into.
 *
 * SIZE.  The format size is riff_size + 8 - binwalk's carve length.  Bytes
 * after it (the AVIX extension RIFFs of an OpenDML AVI, a missing-pad byte,
 * anything appended) are overlay.
 *
 * Not an archive: binwalk's extractor carves the RIFF itself ("image.riff",
 * or "video.wav" for WAVE), so there are no members to publish.
 */

#ifndef XXFCLIB_FORMAT_RIFF_H
#define XXFCLIB_FORMAT_RIFF_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** "RIFF", riff_size, form type. */
#define XX_RIFF_HEADER_SIZE 12U
/** Chunk ID and chunk size. */
#define XX_RIFF_CHUNK_HEADER_SIZE 8U
/** Smallest accepted riff_size: form type plus one empty chunk. */
#define XX_RIFF_MIN_RIFF_SIZE 12U
/** Cap on the top-level chunks walked; a longer list is refused. */
#define XX_RIFF_MAX_CHUNKS 65536U

typedef struct xx_riff xx_riff;
typedef struct xx_riff xx_riff_t;
typedef struct xx_riff XRiff;

struct xx_riff {
    Abstractformat format;
    uint32_t riff_size;         /**< The u32 at +4. */
    char form_type[5];          /**< The four bytes at +8, NUL terminated. */
    char first_chunk_id[5];     /**< ID of the first top-level chunk. */
    uint32_t number_of_chunks;  /**< Top-level chunks after the form type. */
    bool pad_outside;           /**< Last odd chunk's pad byte is past riff_size. */
};

XXFC_API void xx_riff_init(xx_riff *riff, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_riff *xx_riff_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_riff_destroy(xx_riff *riff);
XXFC_API void xx_riff_free(xx_riff *riff);

XXFC_API bool xx_riff_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_riff_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_riff_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);

XXFC_API uint32_t xx_riff_get_riff_size(const xx_riff *riff);
XXFC_API const char *xx_riff_get_form_type(const xx_riff *riff);
XXFC_API const char *xx_riff_get_first_chunk_id(const xx_riff *riff);
XXFC_API uint32_t xx_riff_get_number_of_chunks(const xx_riff *riff);

static inline Abstractformat *xx_riff_to_format(xx_riff *riff) {
    return riff ? &riff->format : NULL;
}
static inline void XRiff_init(xx_riff *riff, xx_io_device *dev,
                              int64_t base_address) {
    xx_riff_init(riff, dev, base_address);
}
static inline xx_riff *XRiff_create(xx_io_device *dev, int64_t base_address) {
    return xx_riff_create(dev, base_address);
}
static inline void XRiff_free(xx_riff *riff) {
    xx_riff_free(riff);
}
static inline bool XRiff_is_valid(xx_riff *riff, xx_pd_struct *pd) {
    return riff ? xx_format_is_valid(&riff->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RIFF_H */
