/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_VMARC_H
#define XXFCLIB_ALGO_VMARC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VM/CMS VMARC (John Fisher, Rice University) member codecs.
 *
 * The member content is written RAW EBCDIC with the CMS records simply
 * concatenated: no record separators, no length prefixes and NO code-page
 * translation.  The reference extractor builds its record sink with the
 * translate flag OFF, and anything else corrupts every byte of the output.
 *
 * Two codecs, selected per member by the container's flag byte at header+0x25:
 *
 *   STORED / "ASIS" (flags & 0x40) - big-endian u16 length-prefixed records, a
 *   zero length ending the member.
 *
 *   LZW - 12-bit MSB-first codes over a 4096-entry trie with LEAF RECYCLING:
 *   a round-robin rover hands out the first entry whose child count is zero,
 *   and the entry's character is filled in LAZILY on the FOLLOWING iteration
 *   (char[pending] = char[first node of the string just decoded]).  That
 *   deferred write is exactly what makes the KwKwK case resolve for free -
 *   there is no special case for it anywhere in the loop, and adding one
 *   breaks it.
 *
 * Symbol 0 is end-of-record and symbols 1..256 are data bytes value - 1.  A
 * single end-of-record finishes a fixed-format ('F') member; a variable format
 * ('V') member needs two CONSECUTIVE ones, because any data byte resets the
 * counter.  LRECL is carried through because the reference sink tracks a column
 * for 'F' members, but that tracking emits nothing: LRECL cannot change the
 * output and is accepted only so the shape of the port matches.
 *
 * NEITHER CODEC STORES AN OUTPUT LENGTH - XVMARCArchive::parseContext() learns
 * each member's size by decoding it - which is why the scan entry points exist.
 * They also report the byte the member ends on, which is what lets a container
 * walk find the next 80-byte-aligned header.
 */

#define XX_VMARC_MODE_LZW 0U
#define XX_VMARC_MODE_STORED 1U

/* Largest member the decoder will consider; mirrors the reference ceiling. */
#define XX_VMARC_MAX_UNCOMPRESSED_SIZE 0x20000000U

/** Per-member parameters taken from the container's member header. */
typedef struct xx_vmarc_params {
    uint16_t lrecl; /**< header+0x1C, big-endian. Does not affect output.   */
    bool fixed;     /**< header+0x24 == 'F' (EBCDIC 0xC6): record format.   */
    uint8_t mode;   /**< XX_VMARC_MODE_LZW or XX_VMARC_MODE_STORED.         */
} xx_vmarc_params;

/**
 * @brief Decode one VMARC member into a buffer of known size.
 *
 * @param input        The member's data, starting at its first data byte.
 * @param input_size   Bytes available from there (the rest of the container is
 *                     fine: the member stops on its own terminator).
 * @param params       Member parameters; NULL means LZW, variable format.
 * @param output       Destination, never NULL.
 * @param output_size  Exact expected decoded size (from a scan).
 * @param written      Receives the decoded size; 0 on any failure.
 * @param consumed     Receives the input bytes the member occupies - i.e. the
 *                     member's end offset relative to @p input.  Reported even
 *                     when the call fails, so a container walk can still step
 *                     past a damaged member. May be NULL.
 * @return true only when the member terminated cleanly having produced exactly
 *         @p output_size bytes.
 */
XXFC_API bool xx_vmarc_decode_memory_ex(const uint8_t *input,
                                        size_t input_size,
                                        const xx_vmarc_params *params,
                                        uint8_t *output, size_t output_size,
                                        size_t *written, size_t *consumed);

/**
 * @brief Measure one VMARC member, which stores no decoded length.
 *
 * Runs the very same core routine with no output buffer.  Neither codec ever
 * reads back what it has emitted - LZW walks its trie, STORED copies input - so
 * discarding the output cannot change the result and the measure and the decode
 * cannot disagree.  No window is needed.
 *
 * @param params      Member parameters; NULL means LZW, variable format.
 * @param max_output  Refuse a member that would decode to more than this.
 * @param consumed    Receives the member's end offset relative to @p input.
 *                    Reported even on failure. May be NULL.
 * @param produced    Receives the decoded size, 0 on failure. May be NULL.
 * @return true when the member terminated cleanly within the limit.
 */
XXFC_API bool xx_vmarc_scan_memory_ex(const uint8_t *input, size_t input_size,
                                      const xx_vmarc_params *params,
                                      size_t max_output, size_t *consumed,
                                      size_t *produced);

/** Library-standard form of xx_vmarc_decode_memory_ex(): LZW, variable format
 *  ('V'), which is what an unqualified "VMARC stream" means.  A STORED or 'F'
 *  member MUST go through the _ex form - the terminator rule differs. */
XXFC_API bool xx_vmarc_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

/** Library-standard form of xx_vmarc_scan_memory_ex(): LZW, variable format.
 *  Zeroes both outputs on failure. */
XXFC_API bool xx_vmarc_scan_memory(const uint8_t *input, size_t input_size,
                                   size_t max_output, size_t *consumed,
                                   size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
