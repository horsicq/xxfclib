/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mathcad.h
 *  @brief MathSoft MathCAD packed worksheet (".MCDCOMPRESSION").
 */

/* WHERE THE LAYOUT COMES FROM.  Nothing is published about this container.
 * Searching for the banner turns up only material on the much later PTC
 * formats - .xmcdz (gzipped XML) and .mcdx (a ZIP) - neither of which this
 * is.  The layout below was established by measuring all 593 samples in
 * F:\ARC\ARC\MathCAD (458 .MCD worksheets, 133 .PAS, 2 .SMR):
 *
 *   off  size  what                                       evidence
 *   0    15    ".MCDCOMPRESSION", no terminator           identical, 593/593
 *   15   ..    packed bit stream to end of file
 *
 * That is the whole header.  There is no length, no checksum, no count and no
 * trailer: the smallest sample is 61 bytes, so a 15-byte banner plus 46 bytes
 * of payload is a complete file, which leaves no room for anything else.
 * Byte 15 takes only three values across the corpus (0x97 x435, 0x86 x156,
 * 0x9d x2) and that once looked like a method field; it is not one.  It is
 * simply the first byte of the bit stream, and worksheets that open with the
 * same boilerplate necessarily open with the same bits.
 *
 * THE CODEC.  A plain LZSS, recovered by differential analysis against known
 * plaintext and then verified byte-for-byte on every sample in the corpus:
 *
 *   - bits are consumed most-significant-first within each byte;
 *   - flag bit 1  -> literal: the next 8 bits are the byte;
 *   - flag bit 0  -> match:   12-bit window position P, then 4-bit L.
 *                             P == 0 is the end-of-stream marker and carries
 *                             no length field.  Otherwise the run is L + 2
 *                             bytes (2..17) copied out of the window
 *                             starting at P;
 *   - the window is a 4096-byte ring, zero filled, whose write pointer starts
 *     at 1 - so the n-th output byte lands at ring position (n + 1) & 0xfff,
 *     and a match position is therefore one more than the output index it
 *     refers to.  Matches may overlap the write pointer (run expansion).
 *
 * The stream carries no decompressed length; the end-of-stream marker is the
 * only terminator, which is why it must be honoured rather than decoding to
 * end of input.  Ignoring it leaves two spurious bytes decoded out of the
 * zero padding on 276 of the 593 samples.
 *
 * VERIFICATION.  Output was compared byte-for-byte against an independent
 * unpacker over the whole corpus: 593 of 593 exact, plaintext sizes 71 bytes
 * to 95448 bytes.  Decoded worksheets are the expected line oriented ASCII
 * (".EQN 0 0 0 0\r\n", "h:5*mm\x11\r\n", ...).
 *
 * WHAT THIS READER PUBLISHES.  One archive record.  A packed worksheet is one
 * stream, not a container - there is no member table and no name anywhere in
 * the file - so the record is synthesised and named "worksheet".
 */

#ifndef XXFCLIB_FORMAT_MATHCAD_H
#define XXFCLIB_FORMAT_MATHCAD_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Banner at offset 0.  Not NUL-terminated in the file. */
#define XX_MATHCAD_SIGNATURE ".MCDCOMPRESSION"
/** Bytes compared by the magic test, and the whole of the header. */
#define XX_MATHCAD_SIGNATURE_SIZE 15U
#define XX_MATHCAD_HEADER_SIZE XX_MATHCAD_SIGNATURE_SIZE

/** LZSS window, in bytes.  Fixed by the 12-bit position field. */
#define XX_MATHCAD_WINDOW_SIZE 4096U
/** Largest packed stream this reader will buffer. */
#define XX_MATHCAD_MAX_PACKED_SIZE ((int64_t)64 * 1024 * 1024)
/** Ceiling on the decoded size, derived from the coding: the densest token is
 *  a 17-bit match carrying 17 bytes, so output can never exceed 8x input. */
#define XX_MATHCAD_MAX_EXPANSION 8

typedef struct xx_mathcad xx_mathcad;
typedef struct xx_mathcad xx_mathcad_t;
typedef struct xx_mathcad XMathcad;

struct xx_mathcad {
    Abstractformat format;
    int64_t packed_offset;      /**< Absolute device offset of the stream. */
    int64_t packed_size;        /**< Packed stream length, bounded to file. */
    uint64_t uncompressed_size; /**< Decoded length, 0 if it would not
                                 *   decode. */
    bool unpackable;            /**< True when the stream decoded cleanly. */
};

XXFC_API void xx_mathcad_init(xx_mathcad *document, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_mathcad *xx_mathcad_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_mathcad_destroy(xx_mathcad *document);
XXFC_API void xx_mathcad_free(xx_mathcad *document);

XXFC_API bool xx_mathcad_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_mathcad_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_mathcad_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_mathcad_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the packed stream to @p destination.  @p destination may be NULL,
 *  in which case the stream is only validated and measured. */
XXFC_API bool xx_mathcad_unpack_to_device(xx_mathcad *document,
                                          xx_io_device *destination,
                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mathcad_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mathcad_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mathcad_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mathcad_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mathcad_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute offset of the packed stream, or -1 before handle_base_info. */
XXFC_API int64_t xx_mathcad_get_packed_offset(const xx_mathcad *document);
/** Packed stream length in bytes, or -1 before handle_base_info. */
XXFC_API int64_t xx_mathcad_get_packed_size(const xx_mathcad *document);
/** Decoded length in bytes, or 0 when the stream would not decode. */
XXFC_API uint64_t xx_mathcad_get_uncompressed_size(const xx_mathcad *document);

static inline Abstractformat *xx_mathcad_to_format(xx_mathcad *document) {
    return document ? &document->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MATHCAD_H */
