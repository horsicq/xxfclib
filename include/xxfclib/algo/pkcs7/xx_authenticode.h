/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_authenticode.h
 * @brief Authenticode-specific PKCS#7 content: SpcIndirectDataContent,
 * SpcSpOpusInfo, nested signatures and PKCS#9 countersignatures.
 */
#ifndef XX_AUTHENTICODE_H
#define XX_AUTHENTICODE_H

#include "xxfclib/algo/pkcs7/xx_pkcs7.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_AUTHENTICODE_OID_SPC_INDIRECT_DATA "1.3.6.1.4.1.311.2.1.4"
#define XX_AUTHENTICODE_OID_SPC_STATEMENT_TYPE "1.3.6.1.4.1.311.2.1.11"
#define XX_AUTHENTICODE_OID_SPC_SP_OPUS_INFO "1.3.6.1.4.1.311.2.1.12"
#define XX_AUTHENTICODE_OID_NESTED_SIGNATURE "1.3.6.1.4.1.311.2.4.1"
#define XX_AUTHENTICODE_OID_RFC3161_COUNTERSIGN "1.3.6.1.4.1.311.3.3.1" /* legacy Microsoft timestamp unsignedAttr */

/** SpcIndirectDataContent ::= SEQUENCE {
 *    data SpcAttributeTypeAndOptionalValue,           -- { type OID, value ANY OPTIONAL }
 *    messageDigest DigestInfo ::= SEQUENCE { digestAlgorithm AlgorithmIdentifier, digest OCTET STRING }
 *  }
 * `data_value_der` is the raw TLV of SpcAttributeTypeAndOptionalValue.value
 * (e.g. SpcPeImageData for a PE, SpcLink/SpcPeImageData-equivalent in other
 * contexts) - not decoded further by this layer. */
typedef struct xx_spc_indirect_data_content {
    char data_type_oid[96];
    const uint8_t *data_type_oid_der;
    size_t data_type_oid_der_size;
    bool has_data_value;
    const uint8_t *data_value_der; /**< Raw TLV, header included. */
    size_t data_value_der_size;
    xx_x509_algorithm_id digest_algorithm;
    const uint8_t *digest; /**< DigestInfo.digest, OCTET STRING content. */
    size_t digest_size;
} xx_spc_indirect_data_content;

XXFC_API bool xx_authenticode_parse_spc_indirect_data(const uint8_t *der, size_t size, xx_spc_indirect_data_content *out);

typedef enum xx_spc_string_kind {
    XX_SPC_STRING_NONE = 0,
    XX_SPC_STRING_BMP, /**< [0] IMPLICIT BMPString: raw bytes are big-endian UTF-16. */
    XX_SPC_STRING_IA5  /**< [1] IMPLICIT IA5String: raw bytes are ASCII. */
} xx_spc_string_kind;

typedef struct xx_spc_string {
    xx_spc_string_kind kind;
    const uint8_t *bytes;
    size_t size;
} xx_spc_string;

typedef enum xx_spc_link_kind {
    XX_SPC_LINK_NONE = 0,
    XX_SPC_LINK_URL,     /**< [0] IMPLICIT IA5String. */
    XX_SPC_LINK_MONIKER, /**< [1] IMPLICIT SpcSerializedObject: raw bytes only, not decoded further. */
    XX_SPC_LINK_FILE     /**< [2] EXPLICIT SpcString: raw bytes are that inner SpcString's own TLV; re-parse with a second xx_authenticode_parse_spc_string-style read if ever needed. */
} xx_spc_link_kind;

typedef struct xx_spc_link {
    xx_spc_link_kind kind;
    const uint8_t *bytes;
    size_t size;
} xx_spc_link;

/** SpcSpOpusInfo ::= SEQUENCE { programName [0] EXPLICIT SpcString OPTIONAL, moreInfo [1] EXPLICIT SpcLink OPTIONAL }
 * SpcString ::= CHOICE { unicode [0] IMPLICIT BMPString, ascii [1] IMPLICIT IA5String }
 * SpcLink ::= CHOICE { url [0] IMPLICIT IA5String, moniker [1] IMPLICIT SpcSerializedObject, file [2] EXPLICIT SpcString } */
typedef struct xx_spc_sp_opus_info {
    bool has_program_name;
    xx_spc_string program_name;
    bool has_more_info;
    xx_spc_link more_info;
} xx_spc_sp_opus_info;

XXFC_API bool xx_authenticode_parse_spc_sp_opus_info(const uint8_t *der, size_t size, xx_spc_sp_opus_info *out);

/** Iterates the attrValues of the nested-signature unsignedAttr
 * (XX_AUTHENTICODE_OID_NESTED_SIGNATURE), each a complete nested PKCS#7
 * ContentInfo (there can be more than one nested signature). */
XXFC_API bool xx_authenticode_nested_signatures_begin(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor);
XXFC_API bool xx_authenticode_nested_signatures_next(xx_asn1_cursor *cursor, xx_pkcs7_content_info *out);

/** Iterates the attrValues of the PKCS#9 countersignature unsignedAttr
 * (XX_PKCS9_OID_COUNTERSIGNATURE), each a bare SignerInfo (RFC 2985:
 * "Countersignature ::= SignerInfo"). */
XXFC_API bool xx_authenticode_countersignatures_begin(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor);
XXFC_API bool xx_authenticode_countersignatures_next(xx_asn1_cursor *cursor, xx_pkcs7_signer_info *out);

#ifdef __cplusplus
}
#endif
#endif /* XX_AUTHENTICODE_H */
