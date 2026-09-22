/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_CHARC_H
#define XXFCLIB_ALGO_CHARC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a ChArc method-1 member stream of known plaintext length.
 *
 * ChArc 1.1 / 1.2 (S.Chernivetsky, SP "Dialog", Moscow 1990) - the member
 * codec of the .CHZ container and its "ChSFX (small)" self-extractor.  It is
 * an ORDER-1 CONTEXT-MODELLED LZ77 with STATIC per-context Huffman codes:
 * nothing adapts, every table is transmitted in a model header ahead of the
 * first coded byte.  There are 259 contexts - one per previous byte, plus
 * 0x100 (match length AND distance high byte), 0x101 (distance low byte) and
 * 0x102 (the fallback table every "mode 0" context borrows).
 *
 * Bits are MSB-first out of a 16-bit accumulator topped up ONE byte at a time
 * and only when the request does not fit; no request is wider than eight bits.
 * Tables are flat [count][symbol x count] blocks, one block per code length,
 * and the symbols of a length take the LAST `count` codes still open at that
 * length - not the first.
 *
 * The container stores the member's decoded size (CHZ record field at +0x08),
 * so @p output_size IS that length and a successful call always produces
 * exactly that many bytes; no measuring entry point is provided.
 *
 * @param input       Compressed bytes (one member's stored payload).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Plaintext length; exactly this many bytes are produced.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode of @p output_size bytes.
 */
XXFC_API bool xx_charc_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

#ifdef __cplusplus
}
#endif
#endif
