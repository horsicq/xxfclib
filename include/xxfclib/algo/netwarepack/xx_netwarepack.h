/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_NETWAREPACK_H
#define XXFCLIB_ALGO_NETWAREPACK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Novell "Packed File" codec, format version 0x01 / method 0x0A.
 *
 * The same stream shape carries both NetWare corpora: the Personal NetWare /
 * Novell DOS "Packed File " single-member container (stream at 0x1F) and the
 * NetWare installation-disk container's "PackedData" chunk.  Both declare
 * 01 0A and both store the uncompressed size in the container, so no
 * measuring entry point is provided: the caller always knows output_size.
 *
 * BIT ORDER IS LSB-FIRST: bytes enter the accumulator at the top, fields come
 * off the bottom, so a field's first-read bit is its least significant one.
 * Reading MSB-first yields a plausible first tree and then desynchronises.
 *
 * Stream layout: three self-describing trees (literals; match lengths, where
 * symbol 0xFE escapes to a 13-bit length; match-distance high part), then the
 * token stream.  A 1 bit is a literal, a 0 bit a match whose distance is
 * (5 raw bits) + 32 * (tree-2 symbol).  The window is 16384 bytes and
 * zero-initialised; a match may legally reach behind the start of output and
 * must then produce NUL bytes.  The declared size is the only end marker.
 *
 * Succeeds only when exactly output_size bytes are produced; *written is set
 * on every path (0 on failure).
 */
XXFC_API bool xx_netwarepack_decode_memory(const uint8_t *input,
                                           size_t input_size, uint8_t *output,
                                           size_t output_size,
                                           size_t *written);

#ifdef __cplusplus
}
#endif
#endif
