/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ZOOM_H
#define XXFCLIB_ALGO_ZOOM_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zoom - an Amiga floppy imager (magic "ZOM5").  A container is a whole floppy
 * and the product is one .adf image of
 * (lastCylinder - firstCylinder + 1) * 0x2C00 bytes, where 0x2C00 is a cylinder
 * of 2 tracks * 11 sectors * 512 bytes.  Everything multi-byte is BIG-ENDIAN.
 *
 * File header, 0x4C bytes:
 *   0x00   4  magic "ZOM5"
 *   0x04   1  first cylinder
 *   0x05   1  last cylinder
 *   0x06   1  format version, must be 5
 *   0x1C   4  length N of a trailing note block, 0 = none; when it is set the
 *             header is followed by N + 4 bytes (note plus its checksum)
 *   0x24   1  non zero = password protected, which is refused outright
 *   0x48   4  checksum over bytes 0..0x47 (parsed, NOT verified - as in the
 *             reference)
 *
 * Then chunk records back to back; a record is a 42-byte header plus payload:
 *    0    5  five cylinder numbers, one per slot; 0xFF = slot unused
 *    5    1  padding
 *    6   20  five u32 sector bitmasks; bit k (k = 0..21, LSB first) set means
 *            "sector k of that cylinder is stored"
 *   0x1A  2  u16 packed payload length
 *   0x1C  2  u16 length after the LZHUF stage, 0 = no RLE stage
 *   0x1E  2  u16 final decoded length
 *   0x20  2  u16 flag, != 0 = the payload is LZHUF compressed
 *   0x22  4  u32 payload checksum      (parsed, NOT verified)
 *   0x26  4  u32 record header checksum (parsed, NOT verified)
 *
 * TRAP 1 - TWO STAGES, IN THE OPPOSITE ORDER FROM THE PACKER'S.  The packer
 * does RLE and THEN LZHUF, so the unpacker runs LZHUF first, driven by the
 * "length after the LZHUF stage" field, and the RLE second.  A middle length of
 * zero means there is no RLE stage at all.
 *
 * TRAP 2 - ZOOM'S LZHUF IS NOT THE TEXTBOOK ONE.  Match length is
 * symbol - 255 with NO THRESHOLD added and the match source is (cursor -
 * distance) with NO -1 adjustment; the ring is 4096 ZERO-filled bytes and the
 * alphabet is 317 symbols whose last one, 0x13C, is the end marker.  At
 * MAX_FREQ the model simply STOPS re-weighting instead of halving and
 * rebuilding - that is Zoom's own behaviour, not an omission.
 *
 * TRAP 3 - THE HOLES ARE PART OF THE IMAGE.  Unstored sectors are 0x200 zero
 * bytes and skipped cylinders 0x2C00 of them, including the run from the last
 * record's cylinder up to lastCylinder.
 *
 * UNVERIFIED because the reference corpus does not exercise it: a first
 * cylinder other than 0 (cylinder counting starts at 0 whatever firstCylinder
 * says, which is what the reference does and what is reproduced here), a
 * non-empty note block, the password flag, and a chunk with the LZHUF flag
 * clear.
 */

/**
 * @brief Decode a whole Zoom container into its .adf image.
 *
 * @param input       The ENTIRE container, starting at "ZOM5".
 * @param input_size  Length of @p input.
 * @param output      Receives the image.
 * @param output_size Capacity of @p output; must be at least the image size
 *                    that the header implies (see xx_zoom_scan_memory()).
 * @param written     Receives the byte count produced; set on every path.
 * @return true only when the whole image was produced.
 */
XXFC_API bool xx_zoom_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

/**
 * @brief Measure a Zoom container.
 *
 * The image size follows from the header's cylinder range, but this runs the
 * FULL decode with the output discarded so that a measurement can never
 * succeed where the decode would fail.
 *
 * @param input       The ENTIRE container.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse an image larger than this.
 * @param consumed    Receives the input bytes the records occupy - trailing
 *                    bytes too short for a record header are not counted.
 *                    May be NULL.
 * @param produced    Receives the image size. May be NULL.
 * @return true when the container decodes completely within the limit.
 */
XXFC_API bool xx_zoom_scan_memory(const uint8_t *input, size_t input_size,
                                  size_t max_output, size_t *consumed,
                                  size_t *produced);

/**
 * @brief The RLE stage on its own.
 *
 * @p input is one chunk's LZHUF output INCLUDING its four-byte header: a
 * BIG-ENDIAN 24-bit copy of the expected length and then the escape byte,
 * which is chosen per chunk and is an ordinary data byte the rest of the time.
 *
 *   escape, 0          -> emit one literal escape byte
 *   escape, n (n != 0) -> emit n + 1 copies of the byte that follows
 *   anything else      -> emit it literally
 *
 * The run length is n + 1, not n.  @p output_size must equal the stream's own
 * BE24 length exactly; a disagreement is a desynchronised chunk, not a stream
 * worth trying.
 *
 * @param written Receives the byte count produced; set on every path.
 * @return true only when exactly @p output_size bytes were produced and the
 *         input was consumed to its end.
 */
XXFC_API bool xx_zoom_rle_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_ZOOM_H */
