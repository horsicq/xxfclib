/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_csdpack.h @brief IBM OS/2 service-pack ("CSD") member codec. */
#ifndef XXFCLIB_ALGO_CSDPACK_H
#define XXFCLIB_ALGO_CSDPACK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the member header: one of six 6-byte magics. */
#define XX_CSDPACK_HEADER_SIZE 6U

/**
 * @brief Key of a CSD member header, or 0 when @p header is not one of the
 * six known 6-byte magics.  The key seeds the stream's scrambler.
 */
XXFC_API uint32_t xx_csdpack_header_key(const uint8_t *header, size_t size);

/**
 * @brief Decode one CSD member (header included).
 *
 * The stream carries an end code and no length field.  @p output may be NULL
 * to measure only.  Decoding fails when the plaintext would exceed
 * @p max_output, when the input ends before the end code, or when the stream
 * is malformed; trailing bytes after the end code are ignored.
 *
 * @param input       The whole member, starting with its 6-byte magic.
 * @param input_size  Bytes available.
 * @param output      Destination (capacity @p max_output) or NULL.
 * @param max_output  Plaintext ceiling.
 * @param consumed    Receives the bytes used, header included (may be NULL).
 * @param produced    Receives the plaintext size (may be NULL).
 * @return true when the end code was reached.
 */
XXFC_API bool xx_csdpack_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t max_output, size_t *consumed, size_t *produced);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_CSDPACK_H */
