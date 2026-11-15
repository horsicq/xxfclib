/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_string_width.h
 * @brief Terminal cell width of Unicode scalars (unicode-width 0.2.2).
 *
 * The Unicode 17 widths of the Rust unicode-width 0.2.2 crate, as YARA-X's
 * diagnostic renderer uses them to align source excerpts and carets. The
 * table is embedded.
 */

#ifndef XX_STRING_WIDTH_H
#define XX_STRING_WIDTH_H

#include "xxfclib/xxfc_defs.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display width of one scalar: 0, 1 or 2 terminal cells.
 *
 * Zero-width scalars (combining marks, most format characters, ...) give
 * 0 and wide East Asian scalars give 2. Every scalar outside the table,
 * control characters included, is one cell: the diagnostic renderer's
 * fallback for scalars unicode-width gives no width.
 */
XXFC_API unsigned xx_string_scalar_display_width(uint32_t scalar);

#ifdef __cplusplus
}
#endif

#endif /* XX_STRING_WIDTH_H */
