/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact DEX helpers.
 *
 * The regular reader in xx_dex.h follows the flat inspection semantics that
 * DIE and the script API rely on. The functions in this header instead
 * reproduce, bit for bit, the parsing behavior of YARA-X 1.21.0's dex module
 * (and of the xxyara port of it): dex\n035-041 magic with the 112-byte header
 * and endian-tag validation, DEX 041 container fields, bounds checks against
 * data length truncated to u32, lazy string/type/proto/field/method/class
 * tables with MUTF-8 decoding, the map_list placeholder semantics (absent
 * map_list has one all-undefined item), method/class existence predicates
 * restricted to the corresponding id tables, and the lowercase-hex header
 * signature.
 *
 * The result is opaque and lazily parsed; it borrows the scanned buffer
 * (valid UTF-8 strings point into it), so data must outlive the result. One
 * result must not be used by two threads at once.
 */

#ifndef XXFCLIB_DEX_YX_H
#define XXFCLIB_DEX_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_dex_yx_result xx_dex_yx_result;

/* Returns a result for both DEX and non-DEX data; NULL means an invalid
   argument or allocation failure. The caller can parse once per scan and
   share the result across condition evaluations of that scan; the accessors
   fill the lazy collections, so one result must not be used by two threads
   at once. A collection whose lazy parse fails to allocate reads as
   undefined. */
XXFC_API xx_dex_yx_result *xx_dex_yx_parse(const uint8_t *data, size_t size);
XXFC_API void xx_dex_yx_destroy(xx_dex_yx_result *result);

/* Names are relative to the `dex.` module prefix. Optional values return
   false when absent, while `is_dex` and repeated-field lengths are defined
   even for arbitrary non-DEX input. */
XXFC_API bool xx_dex_yx_numeric_field_supported(const char *path);
XXFC_API bool xx_dex_yx_numeric_field(const xx_dex_yx_result *result,
                                      const char *path, uint64_t *value);
XXFC_API bool xx_dex_yx_string_field_supported(const char *path);
XXFC_API bool xx_dex_yx_string_field(const xx_dex_yx_result *result,
                                     const char *path, const uint8_t **bytes,
                                     size_t *length);
XXFC_API bool xx_dex_yx_constant(const char *name, uint64_t *value);

/* Collection names: strings, types, protos, fields, methods, class_defs,
   and map_list.items. Scalar strings/types use the synthetic member `value`
   for integration with the condition evaluator's indexed-field model.
   YARA-X exposes map_list.items.len() == 1 when map_list is absent, although
   all fields of that placeholder item are undefined. */
XXFC_API bool xx_dex_yx_collection_supported(const char *collection);
XXFC_API bool xx_dex_yx_collection_count(const xx_dex_yx_result *result,
                                         const char *collection,
                                         uint64_t *count);
XXFC_API bool xx_dex_yx_indexed_numeric_field_supported(const char *collection,
                                                        const char *member);
XXFC_API bool xx_dex_yx_indexed_numeric_field(const xx_dex_yx_result *result,
                                              const char *collection,
                                              uint64_t index,
                                              const char *member,
                                              uint64_t *value);
XXFC_API bool xx_dex_yx_indexed_string_field_supported(const char *collection,
                                                       const char *member);
XXFC_API bool xx_dex_yx_indexed_string_field(const xx_dex_yx_result *result,
                                             const char *collection,
                                             uint64_t index,
                                             const char *member,
                                             const uint8_t **bytes,
                                             size_t *length);

/* Nested arrays are `protos[i].parameters[j]` and
  `methods[i].proto.parameters[j]`. The nested member is `parameters` or
  `proto.parameters`, respectively. */
XXFC_API bool xx_dex_yx_nested_collection_count(const xx_dex_yx_result *result,
                                                const char *collection,
                                                uint64_t index,
                                                const char *nested,
                                                uint64_t *count);
XXFC_API bool xx_dex_yx_nested_indexed_string(
    const xx_dex_yx_result *result, const char *collection, uint64_t index,
    const char *nested, uint64_t nested_index, const uint8_t **bytes,
    size_t *length);

/* Exported module functions. Predicates are defined as false for non-DEX
   input. checksum/signature are undefined on non-DEX input. The SHA-1 digest
   and header signature are lowercase hexadecimal strings. */
XXFC_API bool xx_dex_yx_contains_string(const xx_dex_yx_result *result,
                                        const uint8_t *bytes, size_t length,
                                        bool *found);
XXFC_API bool xx_dex_yx_contains_method(const xx_dex_yx_result *result,
                                        const uint8_t *bytes, size_t length,
                                        bool *found);
XXFC_API bool xx_dex_yx_contains_class(const xx_dex_yx_result *result,
                                       const uint8_t *bytes, size_t length,
                                       bool *found);
XXFC_API bool xx_dex_yx_checksum(const xx_dex_yx_result *result,
                                 uint64_t *value);
XXFC_API bool xx_dex_yx_signature(const xx_dex_yx_result *result,
                                  char out[41]);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_DEX_YX_H */
