/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tsp.h
 * @brief RFC 3161 TSTInfo and timestamp tokens (a ContentInfo/SignedData
 * whose encapsulated content is a TSTInfo), as found in Authenticode's
 * modern (id-smime-aa-timeStampToken) and legacy (szOID_RFC3161_countersign)
 * unsigned attributes.
 */
#ifndef XX_TSP_H
#define XX_TSP_H

#include "xxfclib/algo/pkcs7/xx_pkcs7.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_TSP_OID_TST_INFO "1.2.840.113549.1.9.16.1.4" /* id-ct-TSTInfo */

/** MessageImprint ::= SEQUENCE { hashAlgorithm AlgorithmIdentifier, hashedMessage OCTET STRING } */
typedef struct xx_tsp_message_imprint {
    xx_x509_algorithm_id hash_algorithm;
    const uint8_t *hashed_message;
    size_t hashed_message_size;
} xx_tsp_message_imprint;

/** Accuracy ::= SEQUENCE { seconds INTEGER OPTIONAL, millis [0] INTEGER(1..999) OPTIONAL, micros [1] INTEGER(1..999) OPTIONAL } */
typedef struct xx_tsp_accuracy {
    bool present;
    bool has_seconds;
    int64_t seconds;
    bool has_millis;
    int32_t millis;
    bool has_micros;
    int32_t micros;
} xx_tsp_accuracy;

/** TSTInfo ::= SEQUENCE { version INTEGER, policy TSAPolicyId, messageImprint
 * MessageImprint, serialNumber INTEGER, genTime GeneralizedTime, accuracy
 * Accuracy OPTIONAL, ordering BOOLEAN DEFAULT FALSE, nonce INTEGER OPTIONAL,
 * tsa [0] GeneralName OPTIONAL, extensions [1] Extensions OPTIONAL }.
 * Ordering/nonce/tsa/extensions are validated in schema order but not
 * exposed. Unknown, duplicate or malformed trailing fields are rejected. */
typedef struct xx_tsp_tst_info {
    int64_t version;
    char policy_oid[96];
    const uint8_t *policy_oid_der;
    size_t policy_oid_der_size;
    xx_tsp_message_imprint message_imprint;
    const uint8_t *serial;
    size_t serial_size;
    int64_t gen_time; /**< Unix seconds; GeneralizedTime fractional seconds, if any, are dropped. */
    xx_tsp_accuracy accuracy;
} xx_tsp_tst_info;

/** Parses only version, policy ANY, and messageImprint, ignoring the remaining
 * fields as YARA-X does. Does not require serialNumber/genTime or version 1. */
XXFC_API bool xx_tsp_parse_tst_info_prefix(const uint8_t *der, size_t size, xx_tsp_tst_info *out);

XXFC_API bool xx_tsp_parse_tst_info(const uint8_t *der, size_t size, xx_tsp_tst_info *out);

/** A timestamp token is a ContentInfo(signedData) whose SignedData's
 * encapsulated content (eContentType id-ct-TSTInfo) is a DER-encoded
 * TSTInfo - parses all three layers in one call. All pointer fields borrow
 * the input bytes (or the caller's flattening buffer for the _into API). */
typedef struct xx_tsp_timestamp_token {
    xx_pkcs7_content_info content_info;
    xx_pkcs7_signed_data signed_data;
    xx_tsp_tst_info tst_info;
} xx_tsp_timestamp_token;

XXFC_API bool xx_tsp_parse_timestamp_token(const uint8_t *der, size_t size, xx_tsp_timestamp_token *out);

/** The zero-copy function above rejects a BER constructed eContent OCTET
 * STRING. This variant flattens that encoding into flat[0..flat_capacity)
 * before parsing TSTInfo; its borrowed fields then require flat to remain
 * alive. *flat_size is zero when no flattening was needed. */
XXFC_API bool xx_tsp_parse_timestamp_token_into(const uint8_t *der, size_t size, uint8_t *flat, size_t flat_capacity, size_t *flat_size, xx_tsp_timestamp_token *out);

/** Iterate RFC 3161 token values from either supported unsigned attribute:
 * XX_PKCS9_OID_TIMESTAMP_TOKEN or "1.3.6.1.4.1.311.3.3.1" (Microsoft).
 * _next uses the zero-copy parser; constructed tokens can instead be read
 * with xx_asn1_cursor_next and parsed using the _into function. */
XXFC_API bool xx_tsp_timestamp_tokens_begin(const xx_pkcs7_signer_info *si, const char *attribute_oid, xx_asn1_cursor *cursor);
XXFC_API bool xx_tsp_timestamp_tokens_next(xx_asn1_cursor *cursor, xx_tsp_timestamp_token *out);

/** Hash data with messageImprint.hashAlgorithm and compare hashedMessage.
 * For an Authenticode RFC 3161 timestamp, data is the parent signature. */
XXFC_API bool xx_tsp_check_message_imprint(const xx_tsp_tst_info *info, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
#endif /* XX_TSP_H */
