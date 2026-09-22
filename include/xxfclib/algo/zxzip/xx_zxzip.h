/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ZXZIP_H
#define XXFCLIB_ALGO_ZXZIP_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ZXZIP - the ZX Spectrum "ZIP" archiver.  This is COMPRESSION, not
 * encryption: nothing here takes a key and no member is obfuscated.
 *
 * The member codecs and the Hobeta wrapper cannot be separated, because a
 * member is not emitted as its own bytes: the reference writes a 17-byte
 * HOBETA HEADER built from the directory entry, then the data, then zero
 * padding up to the entry's sector count.  Every method goes through this,
 * stored members included.
 *
 * METHODS (entry byte 0x14)
 *   0  store
 *   1  UNREACHABLE.  No file in the reference corpus uses it and the reference
 *      implementation of it is a transliteration of a Z80 depacker whose
 *      behaviour cannot be checked against anything, so it is refused cleanly
 *      here rather than guessed at.
 *   2  stock PKZIP "Shrink": 9..13 bit LZW where code 0x100 is an escape,
 *      escape+1 widens the code and escape+2 does a PARTIAL clear that frees
 *      only the codes with no children.
 *   3  ZXZIP LZH: an 8 KiB ring, LSB-first bits, flag bit 1 = literal, three
 *      static table sets picked by the sub-method byte (entry 0x15) and the
 *      output size (A when sub == 0, B when the output is under 0x1600 bytes,
 *      else C, and only C Huffman-codes its literals).
 *
 * THE TREE DECODER INVERTS EVERY CODE BIT relative to how the tree is built: a
 * stream bit of 0 takes the child stored for code bit 1.  The tables are
 * canonical (ascending length, then ascending symbol) so the inversion is not
 * visible in the table data, and getting it wrong decodes a plausible-looking
 * but wrong stream.  Do not "simplify" it away.
 *
 * UNPACKED SIZE.  For type 'B'/'b' (BASIC) the size is the u16 at entry +0x09
 * plus 4, otherwise the u16 at +0x0b.  That is rounded up to 256 bytes, and if
 * the rounded value disagrees with sectors*256 (entry +0x0d) then BOTH the
 * size and the padded size become sectors*256.
 */

#define XX_ZXZIP_ENTRY_SIZE 0x16
#define XX_ZXZIP_HOBETA_SIZE 0x11
#define XX_ZXZIP_MAX_UNCOMPRESSED_SIZE 0x01000000

/**
 * @brief Compute a member's decoded and zero-padded sizes from its entry.
 *
 * The emitted member is XX_ZXZIP_HOBETA_SIZE + *padded_size bytes long; that
 * is the value to pass as @p output_size to xx_zxzip_decode_memory().
 *
 * @param entry       The member's 0x16-byte directory entry, verbatim.
 * @param entry_size  Must be XX_ZXZIP_ENTRY_SIZE.
 * @param data_size   Receives the decoded byte count. May be NULL.
 * @param padded_size Receives the zero-padded byte count. May be NULL.
 * @return true when the entry is well formed.
 */
XXFC_API bool xx_zxzip_member_size(const uint8_t *entry, size_t entry_size,
                                   size_t *data_size, size_t *padded_size);

/**
 * @brief Build the 17-byte Hobeta header a member is prefixed with.
 *
 * Bytes 0..12 of the entry, then a zero, then the sector count at entry +0x0d,
 * then a u16 little-endian check of ((sum of those 15 bytes) * 0x101 + 0x69).
 *
 * @param entry       The member's 0x16-byte directory entry.
 * @param entry_size  Must be XX_ZXZIP_ENTRY_SIZE.
 * @param header      Receives XX_ZXZIP_HOBETA_SIZE bytes.
 * @return true on success.
 */
XXFC_API bool xx_zxzip_hobeta_header(const uint8_t *entry, size_t entry_size,
                                     uint8_t *header);

/**
 * @brief Decode one ZXZIP member into its Hobeta-wrapped form.
 *
 * The plaintext length IS stored - it follows from the directory entry, so
 * there is no measuring entry point; call xx_zxzip_member_size() and allocate
 * XX_ZXZIP_HOBETA_SIZE + padded_size.
 *
 * @param input       The member's stored bytes.
 * @param input_size  Length of @p input.
 * @param entry       The member's 0x16-byte directory entry, verbatim.  The
 *                    method, the sub-method, the type letter, the sizes and
 *                    the Hobeta header all come out of it.
 * @param entry_size  Must be XX_ZXZIP_ENTRY_SIZE.
 * @param output      Receives the Hobeta header, the data and the padding.
 * @param output_size Capacity; must be at least
 *                    XX_ZXZIP_HOBETA_SIZE + padded_size.
 * @param written     Receives the byte count produced; set on every path.
 * @return true only when the whole member decoded.
 */
XXFC_API bool xx_zxzip_decode_memory(const uint8_t *input, size_t input_size,
                                     const uint8_t *entry, size_t entry_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_ZXZIP_H */
