/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_GENIUS_H
#define XXFCLIB_ALGO_GENIUS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Block framing of a compressed member of a "GENIUS LIBRARY" (.GPL) container
 * (HANDLE_METHOD_GENIUS_BLOCKS).
 *
 * NO NEW CODEC: every block is one complete PKWARE DCL "implode" stream, which
 * xx_dcl decodes.  What this adds is the member framing:
 *
 *     repeat until 8 bytes are left:
 *       uint32  packed length of the block
 *       ...     that many bytes: one complete DCL stream, fresh dictionary
 *     uint32  plaintext length of the whole member
 *     uint32  CRC-32 of the plaintext
 *
 * The plaintext is cut into 4096-byte blocks, but the block's own end-of-stream
 * code says where it ends, so its unpacked length is never stored.
 *
 * Related to but NOT the same framing as xx_asymetrix: that one has a
 * {uint16 method, uint32 length} header, an implied 4096-byte block length and
 * no trailer; this one has a bare uint32 length, an end-code-determined block
 * length and a length+CRC trailer.  They share only the DCL payload codec.
 *
 * @p output_size IS the member's declared uncompressed size (the container's
 * directory stores it, and the trailer repeats it). */
XXFC_API bool xx_genius_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

#ifdef __cplusplus
}
#endif
#endif
