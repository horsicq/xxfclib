/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_string_escape_debug.h
 * @brief Rust Debug-format escaping of Unicode scalars (YARA-X CLI).
 *
 * Which scalars Rust's `{:?}` formatting of a str writes as a \u{...}
 * escape (char::escape_debug with grapheme-extended escaping), as the
 * official YARA-X 1.23 command line prints them in configuration
 * diagnostics. The classification table of the non-ASCII scalars was
 * observed from the official binary (xxyara's
 * tests/generate_debug_unicode.py) and is embedded.
 */

#ifndef XX_STRING_ESCAPE_DEBUG_H
#define XX_STRING_ESCAPE_DEBUG_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether Debug formatting writes @p scalar as a \u{...} escape.
 *
 * Non-ASCII: true for combining (Grapheme_Extend) scalars and the scalars
 * Rust does not consider printable (controls, format and separator
 * characters, private use, unassigned code points). ASCII: true for
 * U+0000..U+001F and U+007F; Rust gives \0, \t, \n and \r their own
 * escapes and escapes '"' and '\\' with a backslash, so callers test
 * those first.
 */
XXFC_API bool xx_string_scalar_debug_escaped(uint32_t scalar);

#ifdef __cplusplus
}
#endif

#endif /* XX_STRING_ESCAPE_DEBUG_H */
