/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_json_writer.h
 * @brief Streaming JSON writer for xxfclib.
 *
 * A streaming, non-recursive JSON writer that formats JSON documents
 * directly to an abstract xx_io_device. Supports configurable indentation
 * (0 = compact), strict nesting validation, standard JSON escaping,
 * UTF-8 validation (invalid sequences replaced with U+FFFD), and buffered I/O.
 *
 * Default layout (indent 2): "key": value, one member per line, LF line
 * ends, a trailing newline after the document; empty containers are {} / [].
 * Escapes: \" \\ \n \r \t, other bytes < 0x20 and 0x7f as \u00xx (lower
 * case hex); every byte that does not start a well formed UTF-8 sequence
 * (overlong, surrogate, above U+10FFFF, truncated) as \ufffd. Misuse (a key
 * in an array, none in an object, unbalanced ends, deeper than
 * XX_JSON_WRITER_MAX_DEPTH) sets the sticky failed flag. First user:
 * CPDBRipper -j, whose local writer it replaced in 2026-11.
 */

#ifndef XXFCLIB_XX_JSON_WRITER_H
#define XXFCLIB_XX_JSON_WRITER_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum supported nesting depth of JSON containers (objects/arrays). */
#define XX_JSON_WRITER_MAX_DEPTH 1024

typedef struct xx_json_writer xx_json_writer;

/**
 * @brief Create a new streaming JSON writer targeting @p device.
 *
 * @param device Abstract I/O device receiving the JSON bytes (borrowed).
 * @param indent Indentation spaces per level: 0 = compact (no newlines/indentation),
 *               > 0 = number of spaces per indentation level (e.g. 2 or 4).
 * @return Allocated writer pointer, or NULL on allocation/parameter error.
 */
XXFC_API xx_json_writer *xx_json_writer_create(xx_io_device *device, int indent);

/**
 * @brief Finish document generation, flush buffered writes, and report success.
 *
 * Checks that all opened containers have been closed and at least one top-level
 * value was written. Appends a trailing newline (configurable) and flushes to device.
 *
 * @param w Writer pointer.
 * @return true if document is complete and all writes succeeded; false on misuse or I/O failure.
 */
XXFC_API bool xx_json_writer_finish(xx_json_writer *w);

/**
 * @brief Destroy the writer and release its internal buffers.
 *
 * Note: Does not close the underlying xx_io_device (owned by caller).
 * Safe to pass NULL.
 */
XXFC_API void xx_json_writer_destroy(xx_json_writer *w);

/**
 * @brief Begin an object ('{').
 * @param w Writer pointer.
 * @param key Member key if inside an enclosing object; NULL at top level or inside arrays.
 */
XXFC_API void xx_json_writer_begin_object(xx_json_writer *w, const char *key);

/**
 * @brief End the current object ('}').
 * Sets failure flag if current container is not an object or stack is empty.
 */
XXFC_API void xx_json_writer_end_object(xx_json_writer *w);

/**
 * @brief Begin an array ('[').
 * @param w Writer pointer.
 * @param key Member key if inside an enclosing object; NULL at top level or inside arrays.
 */
XXFC_API void xx_json_writer_begin_array(xx_json_writer *w, const char *key);

/**
 * @brief End the current array (']').
 * Sets failure flag if current container is not an array or stack is empty.
 */
XXFC_API void xx_json_writer_end_array(xx_json_writer *w);

/**
 * @brief Write a NUL-terminated UTF-8 string value.
 *
 * Properly escapes quotes, backslashes, control characters (< 0x20, 0x7f),
 * and replaces invalid UTF-8 byte sequences with \ufffd. NULL is written as "".
 */
XXFC_API void xx_json_writer_string(xx_json_writer *w, const char *key, const char *utf8);

/**
 * @brief Write an explicit-length UTF-8 string value.
 *
 * Binary-safe: embedded NUL bytes inside length are escaped as \u0000.
 */
XXFC_API void xx_json_writer_string_n(xx_json_writer *w, const char *key, const char *utf8, size_t length);

/**
 * @brief Write a signed 64-bit integer.
 */
XXFC_API void xx_json_writer_int(xx_json_writer *w, const char *key, int64_t v);

/**
 * @brief Write an unsigned 64-bit integer.
 */
XXFC_API void xx_json_writer_uint(xx_json_writer *w, const char *key, uint64_t v);

/**
 * @brief Write a double-precision floating point number.
 *
 * Formatted with "%.17g" (decimal dot guaranteed regardless of locale).
 * Non-finite values (NaN, +Inf, -Inf) are written as null.
 */
XXFC_API void xx_json_writer_double(xx_json_writer *w, const char *key, double v);

/**
 * @brief Write a boolean value ("true" or "false").
 */
XXFC_API void xx_json_writer_bool(xx_json_writer *w, const char *key, bool v);

/**
 * @brief Write a null literal ("null").
 */
XXFC_API void xx_json_writer_null(xx_json_writer *w, const char *key);

/**
 * @brief Write a raw, pre-encoded JSON snippet verbatim.
 *
 * If json_text is NULL, writes null.
 */
XXFC_API void xx_json_writer_raw(xx_json_writer *w, const char *key, const char *json_text);

/**
 * @brief Write a raw, pre-encoded JSON snippet of explicit length verbatim.
 */
XXFC_API void xx_json_writer_raw_n(xx_json_writer *w, const char *key, const char *json_text, size_t length);

/**
 * @brief Check whether any error or misuse has occurred on @p w.
 *
 * Sticky: once true, remains true until destruction.
 * Returns true if w is NULL.
 */
XXFC_API bool xx_json_writer_failed(const xx_json_writer *w);

/**
 * @brief Standalone string escaping helper matching the writer's escaping rules.
 *
 * Escapes characters into @p out (bounded by @p capacity). The output is always
 * NUL-terminated if capacity > 0.
 *
 * @param out Destination buffer (may be NULL if capacity is 0 to query needed size).
 * @param capacity Available bytes in @p out including terminating NUL.
 * @param utf8 Input byte sequence.
 * @param length Input length in bytes.
 * @return Total bytes needed for escaped content, excluding terminating NUL (snprintf-style).
 */
XXFC_API size_t xx_json_escape(char *out, size_t capacity, const char *utf8, size_t length);

/**
 * @brief Configure line endings for indented output (LF vs CRLF).
 *
 * Default is false (LF "\n", matching cpdbr_json_current.c).
 */
XXFC_API void xx_json_writer_set_crlf(xx_json_writer *w, bool crlf);

/**
 * @brief Configure whether finish() emits a trailing newline.
 *
 * Default is true (matching cpdbr_json_finish).
 */
XXFC_API void xx_json_writer_set_trailing_newline(xx_json_writer *w, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_JSON_WRITER_H */
