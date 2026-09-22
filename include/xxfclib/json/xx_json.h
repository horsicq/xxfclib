/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_json.h
 * @brief A cursor over a JSON document.
 *
 * Several readers need JSON: ASAR's directory is a tree of objects, npm looks
 * up keys in package.json, and the scan engine's ZIP handling reads embedded
 * manifests. Each had grown its own parser; this is the one they share.
 *
 * The design is a cursor, not a document tree. A caller walks the text in
 * place and keeps only what it needs, which matters because an ASAR directory
 * can be sixteen megabytes describing a million entries -- materialising that
 * as a tree of allocations to read a few fields from each node would cost far
 * more than the archive it describes.
 *
 * The shape of a walk is:
 *
 * @code
 * xx_json json;
 * xx_json_init(&json, data, size);
 * if (xx_json_object_begin(&json)) {
 *     char *key;
 *     while (xx_json_object_key(&json, &key)) {
 *         if (xx_str_cmp(key, "size") == 0) {
 *             xx_json_integer(&json, &size);
 *         } else {
 *             xx_json_skip(&json);   // every value must be consumed
 *         }
 *         xx_str_free(key);
 *         if (!xx_json_more(&json)) break;
 *     }
 *     xx_json_object_end(&json);
 * }
 * @endcode
 *
 * Every function returns false on malformed input and leaves the cursor
 * unusable; there is no partial success. Strings are returned decoded to
 * UTF-8, with `\u` escapes resolved and surrogate pairs combined.
 */

#ifndef XX_JSON_H
#define XX_JSON_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the cursor is currently looking at. */
typedef enum xx_json_type_e {
    XX_JSON_TYPE_INVALID = 0, /**< Malformed, or past the end. */
    XX_JSON_TYPE_OBJECT,
    XX_JSON_TYPE_ARRAY,
    XX_JSON_TYPE_STRING,
    XX_JSON_TYPE_NUMBER,
    XX_JSON_TYPE_BOOL,
    XX_JSON_TYPE_NULL
} xx_json_type_t;

/**
 * @brief How deeply nested a document may be.
 *
 * Skipping a value recurses, so an input nesting brackets a million deep
 * would exhaust the stack before any format reader saw it. Archive readers
 * take their input from untrusted files, so the limit lives here rather than
 * being each caller's problem to remember.
 */
#define XX_JSON_MAX_DEPTH 256

/** @brief A position in a JSON document. Copy it to save a position. */
typedef struct xx_json_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    int depth;
} xx_json;

typedef xx_json xx_json_t;

/** @brief Point a cursor at @p size bytes of JSON text. */
XXFC_API void xx_json_init(xx_json *json, const void *data, size_t size);

/** @brief What the next value is, without consuming it. */
XXFC_API xx_json_type_t xx_json_peek(xx_json *json);

/** @brief Consume the next value entirely, whatever it is. */
XXFC_API bool xx_json_skip(xx_json *json);

/**
 * @brief Read a string, decoded to UTF-8.
 *
 * `\u` escapes become UTF-8 and a surrogate pair is combined into one code
 * point; an unpaired surrogate is rejected. The result is owned by the caller
 * and released with xx_str_free().
 */
XXFC_API bool xx_json_string(xx_json *json, char **out);

/**
 * @brief Read a number that must be a non-negative integer.
 *
 * Refuses anything signed, fractional or exponential, and anything above
 * @p limit. JSON numbers are doubles, so a caller reading a size or a count
 * wants exactly this rather than a rounded double.
 */
XXFC_API bool xx_json_integer(xx_json *json, int64_t limit, int64_t *out);

/** @brief Read a literal true or false. */
XXFC_API bool xx_json_bool(xx_json *json, bool *out);

/** @brief Read a literal null. */
XXFC_API bool xx_json_null(xx_json *json);

/** @brief Enter an object. Follow with xx_json_object_key(). */
XXFC_API bool xx_json_object_begin(xx_json *json);
/** @brief True when the object has at least one more member. */
XXFC_API bool xx_json_object_empty(xx_json *json);
/**
 * @brief Read the next member's key and consume the colon after it.
 *
 * The cursor is then positioned on that member's value, which the caller must
 * consume -- with a typed reader, or with xx_json_skip().
 */
XXFC_API bool xx_json_object_key(xx_json *json, char **key);
/** @brief Close an object. */
XXFC_API bool xx_json_object_end(xx_json *json);

/** @brief Enter an array. */
XXFC_API bool xx_json_array_begin(xx_json *json);
/** @brief True when the array has at least one more element. */
XXFC_API bool xx_json_array_empty(xx_json *json);
/** @brief Close an array. */
XXFC_API bool xx_json_array_end(xx_json *json);

/**
 * @brief Step over a separating comma.
 *
 * @return true when a comma was consumed and another member or element
 *         follows; false at the end of the object or array, leaving the
 *         closing brace or bracket for xx_json_object_end() /
 *         xx_json_array_end().
 */
XXFC_API bool xx_json_more(xx_json *json);

#ifdef __cplusplus
}
#endif

#endif /* XX_JSON_H */
