/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_QNXBASE_H
#define XXFCLIB_ALGO_QNXBASE_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The compressed payload of a QNX Neutrino boot image (.ifs / .boot /
 * .altboot): everything after the startup code is a chain of independent UCL
 * NRV2B streams, each introduced by a BIG-endian u16 giving the byte length of
 * that stream, the chain terminated by a length word of 0.
 *
 * The length word is only a hint -- each stream carries its own
 * end-of-stream marker (a match offset that decodes to 0xffffffff), which is
 * what actually stops a block -- but it is checked here so a truncated or
 * non-QNX buffer is rejected instead of being decoded into noise.
 *
 * The codec is plain UCL NRV2B with the 8-bit bit source
 * (ucl nrv2b_decompress_8).  All blocks share one output history.
 *
 * The container stores the plaintext size (the startup header's imagefs_size),
 * so no measuring entry point is needed: the caller allocates that many bytes,
 * passes them as output_size, and compares *written against its stored size.
 */

/**
 * @brief Decode the whole QNX block chain into @p output.
 *
 * @p output_size is a hard ceiling on the produced size; a stream that would
 * exceed it fails rather than truncating.  On success @p *written holds the
 * exact decoded length, which the caller should compare with the
 * container's imagefs_size.
 */
XXFC_API bool xx_qnxbase_decode_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written);

#ifdef __cplusplus
}
#endif
#endif
