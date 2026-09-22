/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_IVT_H
#define XXFCLIB_ALGO_IVT_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file xx_ivt.h
 * @brief MediaView (IVT) internal file stream: MSZIP under a 12-byte header.
 *
 * The member is a 12-byte "mszp"/"nszp" header (magic, u32 uncompressed size,
 * u32 reserved) followed by MSZIP blocks of
 *
 *   [u16 blockUncompressed][u16 blockCompressed]["CK"][raw DEFLATE]
 *
 * where blockCompressed counts the "CK" signature and every block inherits the
 * previous 32 KiB of output as its dictionary - the same scheme CAB uses.  A
 * blockUncompressed of zero ends the chain, as does running out of input on a
 * block boundary; the decode succeeds only when the blocks sum to exactly the
 * size the header declares.
 *
 * The plaintext length is stored (header +4, and the container republishes it),
 * so there is no measuring entry point: a reader can always allocate.
 */

/**
 * @brief Decode a complete IVT member, header included.
 * @param input       The member, starting at the "mszp"/"nszp" magic.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity; must be at least the header's declared size.
 * @param written     Receives the produced size; set on every path.
 * @return true only when the chain decoded to exactly the declared size.
 */
XXFC_API bool xx_ivt_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

/**
 * @brief The uncompressed size an IVT member declares, without decoding it.
 * @return false when @p input is not a well-formed 12-byte IVT header.
 */
XXFC_API bool xx_ivt_declared_size(const uint8_t *input, size_t input_size,
                                   size_t *declared);

#ifdef __cplusplus
}
#endif
#endif
