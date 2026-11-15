/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_tlsh.h
 * @brief TLSH fuzzy hashing (128 buckets, one checksum byte, version T1).
 *
 * Constants and algorithm adapted from YARA-X v1.21.0.
 * The YARA-X Authors' BSD-3-Clause notice is in LICENSE.YARA-X.
 */

#ifndef XX_TLSH_H
#define XX_TLSH_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hash data into its 72-character TLSH T1 digest (NUL-terminated).
 *
 * Follows YARA-X's TLSH profile and quartile selection, including the
 * path-dependent zero quartile when a shortcut stack on one side is empty.
 * Returns false for inputs shorter than 50 bytes or longer than the TLSH
 * maximum, and when the quartiles collapse (q3 == 0).
 */
XXFC_API bool xx_tlsh_hash128(const uint8_t *data, size_t length, char out[73]);

#ifdef __cplusplus
}
#endif

#endif /* XX_TLSH_H */
