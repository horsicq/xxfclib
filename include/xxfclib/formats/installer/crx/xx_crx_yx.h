/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact CRX helpers.
 *
 * The regular reader in xx_crx.h implements the archive framing only. The
 * functions in this header instead reproduce, bit for bit, the parsing
 * behavior of YARA-X 1.21.0's crx module (and of the xxyara port of it):
 * Cr24 magic with version 2/3 headers, the minimal protobuf decoder for
 * CrxFileHeader / AsymmetricKeyProof / SignedData (crx_id), the nested ZIP
 * payload access, manifest.json parsing with __MSG_xxx__ locale resolution
 * via _locales/<default_locale>/messages.json, the lossy JSON string views,
 * signature verification against the signed header bytes, and the permhash
 * SHA-256 over the required runtime permissions in declared array order.
 *
 * The parser never modifies its input and only reads inside the supplied
 * buffer; all strings in the result own heap copies which
 * xx_crx_yx_result_free releases. The permhash cache is thread-local.
 */

#ifndef XXFCLIB_CRX_YX_H
#define XXFCLIB_CRX_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char **items;
    size_t *lengths;           /* Each UTF-8 string may contain NUL bytes. */
    size_t count;
} xx_crx_yx_string_list;

typedef struct {
    char *key;                  /* Base64 of the DER public key. */
    size_t key_length;
    bool verified;
} xx_crx_yx_signature;

typedef struct {
    bool is_crx;
    bool is_crx_present;
    uint32_t crx_version;       /* See crx_version_present. */
    bool crx_version_present;
    uint32_t header_size;       /* See header_size_present. */
    bool header_size_present;
    char *id;
    size_t id_length;
    char *version;
    size_t version_length;
    char *name;
    size_t name_length;
    char *description;
    size_t description_length;
    char *raw_name;
    size_t raw_name_length;
    char *raw_description;
    size_t raw_description_length;
    char *minimum_chrome_version;
    size_t minimum_chrome_version_length;
    char *homepage_url;
    size_t homepage_url_length;
    xx_crx_yx_string_list permissions;
    xx_crx_yx_string_list host_permissions;
    xx_crx_yx_string_list optional_permissions;
    xx_crx_yx_string_list optional_host_permissions;
    xx_crx_yx_signature *signatures;
    size_t signature_count;
} xx_crx_yx_result;

/* Returns false only for invalid arguments or allocation failure. For an
 * unrecognized or malformed CRX, returns true with is_crx == false. Initialize
 * the result to zero before its first use and free it after each parse. */
XXFC_API bool xx_crx_yx_parse(const uint8_t *data, size_t size,
                              xx_crx_yx_result *result);
/* Decode a validated crx.Crx protobuf provided by the scanner output API. */
XXFC_API bool xx_crx_yx_parse_output(const uint8_t *data, size_t size,
                                     xx_crx_yx_result *result);
/* Normalize an overlong terminal CrxSignature length when the bytes at EOF
 * form a complete nested message. The caller owns the returned buffer. */
XXFC_API uint8_t *xx_crx_yx_complete_terminal_signature(
    const uint8_t *data, size_t size, size_t *adjusted_size);
/* Releases every owned string, list and signature in the result, then zeroes
 * it. Safe on a zero-initialized or already-freed result. */
XXFC_API void xx_crx_yx_result_free(xx_crx_yx_result *result);

/* SHA-256 of the concatenated required runtime permissions, in array order.
 * Returns false for non-CRX inputs or invalid arguments. */
XXFC_API bool xx_crx_yx_permhash(const xx_crx_yx_result *result, char hex[65]);
/* The YARA-X module caches permhash per thread until native CRX main runs. */
XXFC_API bool xx_crx_yx_permhash_cached(const xx_crx_yx_result *result,
                                        char hex[65]);
XXFC_API void xx_crx_yx_permhash_cache_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_CRX_YX_H */
