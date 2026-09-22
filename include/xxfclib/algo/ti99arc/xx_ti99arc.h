/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_TI99ARC_H
#define XXFCLIB_ALGO_TI99ARC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TI99 ARC (.ARK) LZW payload codec.
 *
 * The parameter block this family uses is: maxBits 12, CLEAR code 0x100,
 * END code 0x101, MSB-FIRST bit order, no RLE90 filter, no block padding,
 * width-step bias 0.  MSB-first is the trap: reading this LSB-first still
 * decodes a few hundred plausible bytes before desyncing, which looks like a
 * truncated archive rather than a wrong bit order.
 *
 * The archive's CATALOGUE LIVES INSIDE THE COMPRESSED STREAM, so a member is
 * not a byte range of the file: the payload is expanded and then sliced.  That
 * is why xx_ti99arc_expand_memory() exists next to the strict decoder - the
 * reader deliberately expands only a PREFIX of the stream (the first
 * memberOffset+memberSize bytes, or a 0x40100 probe when walking the
 * catalogue) and a stop at the buffer limit is the intended outcome there,
 * not an error.
 */

/* Strict whole-stream decode.
 *
 * Returns true only when the LZW stream reached its end (an END code, or the
 * code stream running out at a code boundary) with every produced byte fitting
 * in @p output_size.  Running out of output capacity is a failure here.
 * @p written is set on every path (0 on failure).
 */
XXFC_API bool xx_ti99arc_decode_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written);

/* Expand at most @p output_size bytes of the stream - the entry point the ARK
 * reader needs.
 *
 * Stopping because @p output is full is a success here: the caller asked for a
 * prefix on purpose.  @p complete (may be NULL) receives whether the stream
 * actually ended, so a caller that wanted the whole thing can still tell.
 * Returns false only on a structurally broken stream, or when nothing at all
 * was produced.
 */
XXFC_API bool xx_ti99arc_expand_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written, bool *complete);

/* Measure a stream without a buffer.  Same core routine as the decoders, so
 * the measure and the decode cannot disagree.  @p consumed receives the input
 * bytes the code stream occupies, rounded up to a byte; @p produced receives
 * the decoded size.  Both may be NULL.  Returns true when the stream decoded
 * to its end within @p max_output.
 */
XXFC_API bool xx_ti99arc_scan_memory(const uint8_t *input, size_t input_size,
                                     size_t max_output, size_t *consumed,
                                     size_t *produced);

/* Full member extraction, driven by the properties blob the reader builds.
 * All fields little endian:
 *
 *   +0x00 u32  plain_size     bytes of the expanded stream the member needs
 *   +0x04 u32  member_offset  member start inside the expanded stream
 *   +0x08 u32  member_size    member length inside the expanded stream
 *   +0x0c u32  prefix_size    length of the synthesised TIFILES header
 *   +0x10      prefix         that header, emitted verbatim before the data
 *
 * Needs scratch for the expanded stream, so it allocates internally.  Returns
 * true only when prefix_size + member_size bytes were produced.
 */
XXFC_API bool xx_ti99arc_decode_member(const uint8_t *input, size_t input_size,
                                       const uint8_t *props, size_t props_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written);

/* 64 MiB.  The corpus tops out near 100 KiB; the cap only exists so a crafted
 * properties blob cannot ask for an unbounded buffer. */
#define XX_TI99ARC_MAX_PLAIN_SIZE 0x4000000u
/* The catalogue is a chain of at most 0x400 sectors of 0x100 bytes, so
 * expanding this much is always enough to walk it. */
#define XX_TI99ARC_PROBE_SIZE 0x40100u

#ifdef __cplusplus
}
#endif
#endif
