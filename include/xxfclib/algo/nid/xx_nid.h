/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_NID_H
#define XXFCLIB_ALGO_NID_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an "NI" (.NID / .DAT / .PAC) DOS install-set member.
 *
 * @p input is the member extent starting at its first five byte block frame:
 *
 *     uint8  unknown           never read
 *     uint16 flags             0x0800 = compressed block, 0x0100 = last block
 *     uint16 compressed_size   payload bytes following the frame
 *
 * An uncompressed block is @c compressed_size verbatim bytes.  A compressed
 * block carries a self-terminating LZ77 stream over three semi-adaptive
 * Huffman models; its expanded size is stored nowhere.  The chain ends at the
 * 0x0100 flag and the member is complete when the expanded bytes reach the
 * length the directory entry declares.
 *
 * The stream layout is
 *
 *   header    3 bits LSB-first: 6 = mode 1, 7 = mode 2, anything else fails.
 *   length    257 symbols, L = symbol + 2.  L in [2, 0x100] is a match,
 *             L == 0x101 introduces a literal, L == 0x102 ends the stream.
 *   literal   mode 2 decodes the byte through a third model, mode 1 reads
 *             eight raw LSB-first bits.
 *   distance  high byte from the second model (implicitly 0 when L == 2), low
 *             byte eight raw bits; the match is copied out of a 64 KiB ring
 *             that starts zero-filled.
 *
 * Each model is semi-adaptive: for its first N symbols (3000 for the length
 * and literal models, 5000 for the distance model) decoding walks a weight
 * ordered list; on symbol N+1 a static Huffman tree is built from the weights
 * and used frozen for the rest of the stream.  Every block resets all three
 * models and the ring, so blocks are independent.
 *
 * The xnid container stores FPART_PROP_UNCOMPRESSEDSIZE for every member, so
 * @p output_size IS the declared plaintext length and a successful call always
 * produces exactly that many bytes.  No measuring entry point is provided.
 *
 * @param input       Member extent, first block frame first.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Declared plaintext length; exactly this many bytes are
 *                    produced on success.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode of @p output_size bytes.
 */
XXFC_API bool xx_nid_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif
#endif
