/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ROMPAQ_H
#define XXFCLIB_ALGO_ROMPAQ_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compaq ROMPAQ multi-part firmware image.
 *
 * A multi-part image is not one compressed stream: it is a chain of banks,
 * each introduced by its own complete 0x48-byte ROMPAQ header and each
 * carrying an independent PKWARE Data Compression Library stream (or stored
 * bytes).  A bank header stores that bank's PACKED length at offset 0x3f; a
 * bank's unpacked length is stored nowhere - only the whole image's size, in
 * the first header at offset 0x00 - so the chain is decoded until the input
 * runs out and the total is then checked against that size.
 *
 * Because that total is what the reader already has in hand, this codec needs
 * no scan entry point: the plaintext length is stored.
 *
 * Single-part images carry no chain and are plain DCL; they do not come here.
 */

/**
 * @brief Decode a chained ROMPAQ image.
 *
 * @p input is the whole file, starting at the first bank header (which is
 * also the container header).  @p output_size is the image size from that
 * header.  Succeeds only when the bank chain lands exactly on the end of
 * @p input and produces exactly @p output_size bytes.
 */
XXFC_API bool xx_rompaq_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

#ifdef __cplusplus
}
#endif
#endif
