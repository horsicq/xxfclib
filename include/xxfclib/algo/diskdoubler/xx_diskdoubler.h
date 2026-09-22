/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_diskdoubler.h
 *  @brief DiskDoubler (.dd / DDAR / DDA2) member stream decoders.
 *
 * DiskDoubler member headers store the plaintext length of every fork, so
 * these entry points take the exact expected plaintext size as @p output_size
 * and no measuring entry point is provided.
 *
 * Method byte (masked with 0x7f) to codec:
 *   0      stored
 *   1      xx_diskdoubler_lzw_decode_memory()
 *   6, 9   xx_diskdoubler_adn_decode_memory()
 *   8      Compact Pro LZH -- byte-for-byte the same algorithm as the codec
 *          used by Compact Pro archives, so it is NOT duplicated here; call
 *          the compactpro module's LZH entry point instead.
 *   10     xx_diskdoubler_ddn_decode_memory()
 */

#ifndef XXFCLIB_ALGO_DISKDOUBLER_H
#define XXFCLIB_ALGO_DISKDOUBLER_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a DiskDoubler "ADn" member (methods 6 and 9).
 *
 * The stream is a chain of 12-byte-headed blocks, each holding at most 8 KiB
 * of plaintext, either stored or coded with a small LZSS variant whose
 * back-references never reach outside their own block.
 *
 * @param input        Packed fork bytes.
 * @param input_size   Length of @p input.
 * @param output       Destination buffer.
 * @param output_size  Exact plaintext size from the member header.
 * @param written      Receives the produced byte count (0 on failure).
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_diskdoubler_adn_decode_memory(const uint8_t *input,
                                               size_t input_size,
                                               uint8_t *output,
                                               size_t output_size,
                                               size_t *written);

/**
 * @brief Decode a DiskDoubler "DDn" member (method 10).
 *
 * A chain of 22-byte-headed blocks of at most 64 KiB plaintext.  Each block
 * carries three independently Huffman-coded streams (match offsets, literals
 * and a length/opcode stream); match offsets may reach back into earlier
 * blocks, so the whole output acts as the window.
 *
 * @param input        Packed fork bytes.
 * @param input_size   Length of @p input.
 * @param output       Destination buffer.
 * @param output_size  Exact plaintext size from the member header.
 * @param written      Receives the produced byte count (0 on failure).
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_diskdoubler_ddn_decode_memory(const uint8_t *input,
                                               size_t input_size,
                                               uint8_t *output,
                                               size_t output_size,
                                               size_t *written);

/**
 * @brief Decode a DiskDoubler LZW member (method 1).
 *
 * The payload is a Unix-compress (.Z) stream whose three-byte header may be
 * XOR-masked with 0x5a, decided by two header bytes; the decoded plaintext
 * carries the same mask.  A 16-bit additive checksum over the three header
 * bytes plus the unmasked plaintext is verified before success is reported.
 *
 * @param input        Packed fork bytes, starting at the 1F 9D magic.
 * @param input_size   Length of @p input.
 * @param info1        Member header byte 22 (format generation).
 * @param info2        Member header byte 52.
 * @param checksum     Big-endian 16-bit checksum from the member header.
 * @param output       Destination buffer.
 * @param output_size  Exact plaintext size from the member header.
 * @param written      Receives the produced byte count (0 on failure).
 * @return true only when exactly @p output_size bytes were produced and the
 *         checksum matched.
 */
XXFC_API bool xx_diskdoubler_lzw_decode_memory(const uint8_t *input,
                                               size_t input_size,
                                               uint8_t info1, uint8_t info2,
                                               uint16_t checksum,
                                               uint8_t *output,
                                               size_t output_size,
                                               size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_DISKDOUBLER_H */
