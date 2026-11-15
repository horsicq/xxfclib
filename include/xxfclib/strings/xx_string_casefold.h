/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_string_casefold.h
 * @brief YARA-X-exact UTF-8 lossy decoding and Unicode lowercase helpers.
 *
 * These helpers back the YARA-X-exact format functions (ELF import_md5 /
 * telfhash, Mach-O symhash, ...). The semantics mirror Rust's
 * String::from_utf8_lossy and str::to_lowercase: malformed UTF-8 sequences
 * become U+FFFD, scalar lowercase mappings follow Unicode 17, and the
 * Final Sigma context rule is applied. The Unicode data tables are
 * embedded, so there is no ICU runtime dependency.
 */

#ifndef XX_STRING_CASEFOLD_H
#define XX_STRING_CASEFOLD_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode bytes as Rust's String::from_utf8_lossy does.
 *
 * Malformed sequences are replaced by the three-byte U+FFFD encoding while
 * valid sequences pass through unchanged, including embedded NULs. With
 * out == NULL, returns the required byte length in `written`. Otherwise
 * `out` must have at least that many bytes; the caller owns its storage.
 * Returns false for invalid arguments or an insufficient capacity.
 */
XXFC_API bool xx_string_utf8_lossy(const uint8_t *bytes, size_t length,
                                   char *out, size_t capacity,
                                   size_t *written);

/**
 * @brief Lowercase bytes as Rust's str::to_lowercase does.
 *
 * The input first receives from_utf8_lossy decoding, then Unicode 17
 * scalar lowercase mappings plus the Final Sigma context rule (U+03A3 maps
 * to U+03C2 only before a cased letter followed by no cased letter, and to
 * U+03C3 otherwise; U+0130 expands to "i" + U+0307). The caller frees
 * *out. Returns false for invalid arguments or allocation failure.
 */
XXFC_API bool xx_string_to_lower_alloc(const uint8_t *bytes, size_t length,
                                       char **out, size_t *out_length);

/**
 * @brief Copy bytes with leading/trailing Unicode whitespace removed.
 *
 * Decoding follows from_utf8_lossy consumption rules and the whitespace set
 * matches Rust's char::is_whitespace (the same set the YARA-X modules trim
 * symbol and string values with). The caller frees *out. Returns false for
 * invalid arguments or allocation failure.
 */
XXFC_API bool xx_string_trim_whitespace_alloc(const uint8_t *bytes,
                                              size_t length, char **out,
                                              size_t *out_length);

/**
 * @brief Receives consecutive output chunks of a streaming conversion.
 *
 * The chunk is only valid during the call. Chunks are never empty.
 */
typedef void (*xx_string_sink_fn)(void *opaque, const char *bytes,
                                  size_t length);

/**
 * @brief Streaming form of xx_string_to_lower_alloc.
 *
 * Delivers exactly the bytes xx_string_to_lower_alloc returns (lossy
 * decoding, Unicode 17 lowercase, U+0130 expansion, Final Sigma), split
 * into one or more chunks, without allocating. All-ASCII input without
 * an uppercase letter is passed through as a single chunk; any other
 * output arrives in chunks of at most 256 bytes. The sink is not called
 * for empty output. Returns false for invalid arguments.
 */
XXFC_API bool xx_string_to_lower_stream(const uint8_t *bytes, size_t length,
                                        xx_string_sink_fn sink,
                                        void *opaque);

/**
 * @brief Trim Unicode whitespace and lowercase, as Rust's
 * String::from_utf8_lossy(bytes).trim().to_lowercase() does.
 *
 * The decoded scalars are trimmed with the char::is_whitespace set, then
 * lowercased like xx_string_to_lower_alloc. With final_sigma false the
 * Final Sigma rule is not applied (U+03A3 always becomes U+03C3), which is
 * bstr's per-scalar ByteSlice::to_lowercase of the trimmed string. The
 * caller frees *out. Returns false for invalid arguments or allocation
 * failure.
 */
XXFC_API bool xx_string_trim_lower_alloc(const uint8_t *bytes, size_t length,
                                         bool final_sigma, char **out,
                                         size_t *out_length);

/**
 * @brief Unicode 17 simple lowercase mapping of one scalar.
 *
 * This is char::to_lowercase for every scalar except U+0130, whose
 * two-scalar lowercase ("i" + U+0307) is not a simple mapping: U+0130 and
 * unmapped scalars are returned unchanged.
 */
XXFC_API uint32_t xx_string_scalar_to_lower(uint32_t scalar);

/** @brief Number of entries of the simple lowercase mapping table. */
XXFC_API size_t xx_string_lower_mapping_count(void);

/**
 * @brief Entry @p index of the simple lowercase mapping table.
 *
 * Entries are sorted by source scalar. Returns false when @p index is out
 * of range or an output pointer is NULL.
 */
XXFC_API bool xx_string_lower_mapping_at(size_t index, uint32_t *source,
                                         uint32_t *lower);

/**
 * @brief Scalars that share the simple lowercase of @p scalar.
 *
 * variants[0] receives xx_string_scalar_to_lower(scalar); then every table
 * source with that lowercase, other than the lowercase itself, follows in
 * table order. This is the scalar class YARA-X's trailing /i regex
 * modifier matches. Returns the number of variants, or 0 when there would
 * be more than four or variants is NULL.
 */
XXFC_API size_t xx_string_scalar_lower_variants(uint32_t scalar,
                                                uint32_t variants[4]);

#ifdef __cplusplus
}
#endif

#endif /* XX_STRING_CASEFOLD_H */
