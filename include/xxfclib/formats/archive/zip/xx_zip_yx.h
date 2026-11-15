/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact ZIP helpers.
 *
 * The regular reader in xx_zip.h follows the archive-inspection semantics
 * that DIE and the script API rely on. The functions in this header instead
 * reproduce, bit for bit, the parsing behavior of YARA-X 1.21.0's zip module
 * (and of the xxyara port of it): EOCD scan with comment-length validation,
 * ZIP64 locator/record recovery, rebased/SFX central-directory handling, the
 * 100000-entry cap, tolerant truncation that keeps the valid entry prefix,
 * and file_path decoding with Rust's String::from_utf8_lossy semantics while
 * preserving embedded NULs through the explicit byte length.
 *
 * These functions never modify their input and only read inside the supplied
 * buffer. Entries returned by xx_zip_yx_parse borrow no memory from the
 * input; release them with xx_zip_yx_result_free.
 */

#ifndef XXFCLIB_ZIP_YX_H
#define XXFCLIB_ZIP_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** YARA-X 1.21 zip.Entry. file_path is UTF-8 with invalid sequences replaced
 * by U+FFFD; the length is separate because ZIP names may contain NUL bytes. */
typedef struct {
    char *file_path;
    size_t file_path_length;
    uint32_t compression;
    uint64_t uncompressed_size;
    uint64_t compressed_size;
    bool file_path_present;
    bool compression_present;
    bool uncompressed_size_present;
    bool compressed_size_present;
} xx_zip_yx_entry;

typedef struct {
    bool is_zip;
    bool is_zip_present;
    xx_zip_yx_entry *entries;
    size_t entry_count;
} xx_zip_yx_result;

/** Parses the scanned byte buffer. Non-ZIP and malformed input produce a
 * defined is_zip=false value. A malformed central entry may leave a prefix
 * of valid entries, just as the upstream forward iterator does. Returns
 * false only for invalid API arguments or an allocation failure. */
XXFC_API bool xx_zip_yx_parse(const uint8_t *data, size_t size,
                              xx_zip_yx_result *result);

/** Decode the validated zip.Zip protobuf supplied by the scanner output API.
 * Optional fields remain absent when their protobuf field is absent. */
XXFC_API bool xx_zip_yx_parse_output(const uint8_t *data, size_t size,
                                     xx_zip_yx_result *result);

/** Releases the entry array and each entry file_path, then zeroes the
 * result. Safe on a zero-initialized or already-freed result. */
XXFC_API void xx_zip_yx_result_free(xx_zip_yx_result *result);

/** Inline zip.Compression enum constants, e.g. "DEFLATED" and "UNKNOWN"
 * (also accepts the "zip.Compression." prefix). */
XXFC_API bool xx_zip_yx_constant(const char *member, uint64_t *value);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ZIP_YX_H */
