/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_string_casecmp.h
 * @brief YARA-X-exact case-insensitive string operators.
 *
 * The icontains, istartswith, iendswith and iequals operators of YARA-X
 * 1.23 over byte strings that may hold NULs and malformed UTF-8. Both
 * operands are compared as sequences of items: an ASCII byte is its ASCII
 * lowercase; a well-formed UTF-8 scalar contributes the scalars of Rust's
 * char::to_lowercase (U+0130 gives "i" and U+0307, every other scalar its
 * xx_string_scalar_to_lower mapping); every other byte is an item of its
 * own that only equals the same byte. Comparing items instead of
 * lowercased bytes means a malformed byte never matches part of a valid
 * scalar, while a match may still start inside the two-scalar lowercase of
 * U+0130.
 *
 * NULL is accepted for an operand of length zero. A NULL operand with a
 * non-zero length gives false.
 */

#ifndef XX_STRING_CASECMP_H
#define XX_STRING_CASECMP_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief YARA-X `haystack icontains needle`; an empty needle matches. */
XXFC_API bool xx_string_unicode_icontains(const uint8_t *haystack,
                                          size_t haystack_length,
                                          const uint8_t *needle,
                                          size_t needle_length);

/** @brief YARA-X `haystack istartswith prefix`. */
XXFC_API bool xx_string_unicode_istarts_with(const uint8_t *haystack,
                                             size_t haystack_length,
                                             const uint8_t *prefix,
                                             size_t prefix_length);

/**
 * @brief YARA-X `haystack iendswith suffix`.
 *
 * Items are taken from the end with bstr's decode_last_utf8 rules, as
 * YARA-X does.
 */
XXFC_API bool xx_string_unicode_iends_with(const uint8_t *haystack,
                                           size_t haystack_length,
                                           const uint8_t *suffix,
                                           size_t suffix_length);

/** @brief YARA-X `lhs iequals rhs`. */
XXFC_API bool xx_string_unicode_iequals(const uint8_t *lhs, size_t lhs_length,
                                        const uint8_t *rhs, size_t rhs_length);

#ifdef __cplusplus
}
#endif

#endif /* XX_STRING_CASECMP_H */
