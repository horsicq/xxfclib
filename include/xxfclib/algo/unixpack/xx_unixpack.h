/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_UNIXPACK_H
#define XXFCLIB_ALGO_UNIXPACK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode the headerless modern SysV pack stream stored in AIX BFF members. */
XXFC_API bool xx_unixpack_decode_raw(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

/** Return true for a complete standalone Unix pack header (0x1f1e/0x1f1f).
 * `uncompressed_size` is written as a 64-bit value so callers can validate it
 * before allocating an output buffer.  `is_old_version` distinguishes the
 * PDP-endian, tree-encoded variant (0x1f1f) from standard SysV pack. */
XXFC_API bool xx_unixpack_parse_header(const void *input, size_t input_size,
                                       uint64_t *uncompressed_size,
                                       bool *is_old_version);

/** Decode a complete standalone Unix pack stream into an exact-size buffer.
 * The input includes the six-byte Unix pack header. */
XXFC_API bool xx_unixpack_decode_memory(const void *input, size_t input_size,
                                        void *output, size_t output_size,
                                        size_t *written);

#ifdef __cplusplus
}
#endif
#endif
