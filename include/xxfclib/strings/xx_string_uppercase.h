/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_string_uppercase.h
 * @brief YARA-X-exact Unicode uppercase and case-property helpers.
 *
 * Rust's char::to_uppercase (Unicode 17, full mappings of up to three
 * scalars) applied as bstr's ByteSlice::to_uppercase does, plus the
 * Uppercase / Lowercase property test YARA-X's diagnostics use. The data
 * tables are embedded.
 */

#ifndef XX_STRING_UPPERCASE_H
#define XX_STRING_UPPERCASE_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Uppercase bytes as bstr's ByteSlice::to_uppercase does.
 *
 * Every well-formed UTF-8 scalar is replaced by its char::to_uppercase
 * scalars; bytes that do not start a well-formed scalar are copied
 * unchanged, so valid UTF-8 input gets Rust's str::to_uppercase. The
 * result is NUL-terminated and the caller frees *out. Returns false for
 * invalid arguments or allocation failure.
 */
XXFC_API bool xx_string_to_upper_alloc(const uint8_t *bytes, size_t length,
                                       char **out, size_t *out_length);

/**
 * @brief Whether a well-formed scalar of the bytes has a case property.
 *
 * Tests Rust's char::is_uppercase (Uppercase) when @p uppercase is true and
 * char::is_lowercase (Lowercase) otherwise. Malformed bytes are skipped.
 */
XXFC_API bool xx_string_has_case_property(const uint8_t *bytes, size_t length,
                                          bool uppercase);

#ifdef __cplusplus
}
#endif

#endif /* XX_STRING_UPPERCASE_H */
