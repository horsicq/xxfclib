/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_swf.h @brief Shockwave Flash (SWF) movie reader. */

#ifndef XXFCLIB_FORMAT_SWF_H
#define XXFCLIB_FORMAT_SWF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Shockwave Flash movie, read as a container of its embedded media.
 *
 * Header (all integers little-endian):
 *
 *   0x00  char[3]  "FWS" plain, "CWS" zlib (SWF 6+), "ZWS" LZMA (SWF 13+)
 *   0x03  u8       SWF version
 *   0x04  u32      FileLength: size of the UNCOMPRESSED movie, header included
 *   CWS:  0x08     a zlib stream holding movie bytes 8..FileLength-1
 *   ZWS:  0x08     u32 size of the LZMA data that follows the properties
 *         0x0C     u8[5] LZMA properties (lc/lp/pb byte, u32 dictionary size)
 *         0x11     raw LZMA stream (no size field, end marker optional)
 *
 * The uncompressed movie continues with a RECT (5-bit field width, then four
 * signed fields of that width, byte aligned), u16 frame rate, u16 frame
 * count, and a chain of tags.  A tag header is a u16 whose top ten bits are
 * the tag code and low six bits the body length; length 0x3F means a u32
 * length follows.  Tag code 0 ends the chain.  DefineSprite (39) nests a
 * u16 id, a u16 frame count and its own tag chain.
 *
 * Members, in tag order:
 *
 *  - "movie.swf" (CWS and ZWS only): the decompressed movie with an FWS
 *    header, as 7-Zip's SWFc handler produces it.
 *  - "image_NNNNN.jpg|png|gif": DefineBits (6) joined with JPEGTables (8),
 *    DefineBitsJPEG2 (21), DefineBitsJPEG3 (35, colour part only) and
 *    DefineBitsJPEG4 (90, colour part only).  JPEG data has every
 *    "FF D9 FF D8" removed: that is both the pre-SWF-8 erroneous header and
 *    the seam between the tables stream and the image stream.  PNG and GIF
 *    payloads are copied unchanged.
 *  - "image_NNNNN.png": DefineBitsLossless (20) and DefineBitsLossless2 (36)
 *    converted to a PNG (palette, RGB or RGBA; the premultiplied colours of
 *    DefineBitsLossless2 are converted back to straight alpha).  The PNG
 *    uses stored deflate blocks, so its size is known without decoding.
 *  - "sound_NNNNN.mp3|wav": DefineSound (14) MP3 frames, or PCM data behind
 *    a RIFF/WAVE header.  ADPCM, Nellymoser and Speex sounds are not listed.
 *  - "stream_N.mp3|wav": each SoundStreamHead / SoundStreamHead2 (18/45) on
 *    the main timeline or in a sprite, with the SoundStreamBlock (19) data of
 *    the same timeline appended until the next head.
 *  - "binary_NNNNN.bin": DefineBinaryData (87) payload.
 *
 * NNNNN is the character id.  A repeated id gets "_K" (K = 1-based member
 * ordinal) appended to its stem, so names never collide and never depend on
 * file content beyond numbers.
 *
 * Compressed movies are decompressed into memory to list and read their
 * media when FileLength is at most XX_SWF_MAX_CACHED; larger ones list only
 * "movie.swf", which is always decompressed straight to its destination.
 */
typedef struct xx_swf {
    Abstractformat format;
    uint64_t number_of_records;
    uint8_t signature;        /**< 'F', 'C' or 'Z'. */
    uint8_t version;
    uint32_t file_length;     /**< Declared uncompressed size. */
    bool movie_complete;      /**< The whole declared movie is readable. */
    struct swf_parsed_s *parsed; /**< Listing, built by handle_base_info. */
} xx_swf;

typedef xx_swf xx_swf_t;

/** FileLength bound; 7-Zip's SWF handlers use the same (1 << 29). */
#define XX_SWF_MAX_FILE_LENGTH 0x20000000U
/** Largest compressed movie decompressed into memory for media listing. */
#define XX_SWF_MAX_CACHED 0x10000000U

XXFC_API void xx_swf_init(xx_swf *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_swf *xx_swf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_swf_destroy(xx_swf *archive);
XXFC_API void xx_swf_free(xx_swf *archive);

XXFC_API bool xx_swf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_swf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_swf_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_swf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_swf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_swf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_swf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_swf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_swf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Write the whole uncompressed movie (FWS header included) to
 * @p destination.  For an FWS movie this copies FileLength bytes.
 *
 * Succeeds only when exactly FileLength bytes were produced.
 */
XXFC_API bool xx_swf_unpack_movie_to_device(xx_swf *archive,
                                            xx_io_device *destination,
                                            xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SWF_H */
