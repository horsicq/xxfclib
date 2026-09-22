/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ASYMETRIX_H
#define XXFCLIB_ALGO_ASYMETRIX_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Block framing of the Asymetrix ToolBook Setup disk-set archive
 * (HANDLE_METHOD_ASYMETRIX_BLOCKS).
 *
 * NO NEW CODEC.  A member is a chain of independent blocks
 *
 *     uint16 method         0 = stored, 1 = PKWARE DCL "implode"
 *     uint32 packed length
 *     ...    that many packed bytes
 *
 * and each method-1 block is a COMPLETE DCL stream with its own 00 05 prelude,
 * its own fresh dictionary and its own 519 end code, so xx_dcl decodes it.
 * The unpacked length of a block is never stored: it is implied to be
 * min(4096, bytes of the member still outstanding).  That is why the declared
 * plaintext size is mandatory and why there is no scan entry point - without
 * the size the chain cannot even be walked.  Every container that uses this
 * framing carries the size in its directory (see archives/xasymetrix.cpp).
 *
 * @p output_size IS the member's declared uncompressed size: the decode
 * succeeds only when the chain produces exactly that many bytes and ends
 * exactly at the last input byte. */
XXFC_API bool xx_asymetrix_decode_memory(const uint8_t *input,
                                         size_t input_size, uint8_t *output,
                                         size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
