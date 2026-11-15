/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pkcs7.h
 * @brief PKCS#7 / CMS ContentInfo, SignedData, SignerInfo (RFC 5652): views
 * into the caller's DER buffer, decoded lazily via xx_asn1_cursor the same
 * way xx_x509 decodes certificates. AlgorithmIdentifier is X.509's type,
 * reused verbatim (xx_x509_algorithm_id, xx_x509_parse_algorithm_identifier).
 */
#ifndef XX_PKCS7_H
#define XX_PKCS7_H

#include "xxfclib/algo/x509/xx_x509.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_PKCS7_OID_DATA "1.2.840.113549.1.7.1"
#define XX_PKCS7_OID_SIGNED_DATA "1.2.840.113549.1.7.2"
#define XX_PKCS9_OID_CONTENT_TYPE "1.2.840.113549.1.9.3"
#define XX_PKCS9_OID_MESSAGE_DIGEST "1.2.840.113549.1.9.4"
#define XX_PKCS9_OID_SIGNING_TIME "1.2.840.113549.1.9.5"
#define XX_PKCS9_OID_COUNTERSIGNATURE "1.2.840.113549.1.9.6"
#define XX_PKCS9_OID_TIMESTAMP_TOKEN "1.2.840.113549.1.9.16.2.14" /* id-aa-timeStampToken (RFC 5035/3161) */

/** ContentInfo ::= SEQUENCE { contentType OID, content [0] EXPLICIT ANY OPTIONAL }.
 * `content_tlv` is the complete raw TLV inside [0] (header included),
 * whatever type it is - a ContentInfo's content is "ANY DEFINED BY
 * contentType", e.g. a SEQUENCE for signedData, so there is no generic
 * octet-string unwrap to do here (contrast EncapsulatedContentInfo below). */
typedef struct xx_pkcs7_content_info {
    char content_type_oid[96];
    const uint8_t *content_type_oid_der;
    size_t content_type_oid_der_size;
    bool has_content;
    const uint8_t *content_tlv;
    size_t content_tlv_size;
} xx_pkcs7_content_info;

XXFC_API bool xx_pkcs7_parse_content_info(const uint8_t *der, size_t size, xx_pkcs7_content_info *out);

/** EncapsulatedContentInfo ::= SEQUENCE { eContentType OID, eContent [0] EXPLICIT OCTET STRING OPTIONAL }.
 * PKCS#7 1.5 content (e.g. Authenticode's SpcIndirectDataContent) is looser
 * and is not always actually an OCTET STRING despite the CMS grammar saying
 * so; `content_is_octet_string` tells you which shape `content_tlv` has. */
typedef struct xx_pkcs7_encap_content_info {
    char content_type_oid[96];
    const uint8_t *content_type_oid_der;
    size_t content_type_oid_der_size;
    bool has_content;              /**< eContent is OPTIONAL (a detached signature omits it). */
    bool content_is_octet_string;
    const uint8_t *content_tlv;    /**< Raw TLV inside [0], header included, whatever its tag. */
    size_t content_tlv_size;
    const uint8_t *content_data;   /**< Content of that TLV, header excluded: Authenticode hashes this span; RFC 3161 hashes content_tlv. */
    size_t content_data_size;
    const uint8_t *octet_string_content; /**< Unwrapped content when content_is_octet_string and it is PRIMITIVE; NULL otherwise (constructed/BER-chunked - re-flatten content_tlv yourself with xx_asn1_read_octet_string_into if that ever matters). */
    size_t octet_string_content_size;
} xx_pkcs7_encap_content_info;

typedef enum xx_pkcs7_sid_kind {
    XX_PKCS7_SID_ISSUER_AND_SERIAL = 0,
    XX_PKCS7_SID_SUBJECT_KEY_IDENTIFIER = 1
} xx_pkcs7_sid_kind;

typedef struct xx_pkcs7_signer_identifier {
    xx_pkcs7_sid_kind kind;
    xx_x509_name issuer;                  /**< Valid iff kind == ISSUER_AND_SERIAL. */
    const uint8_t *serial;
    size_t serial_size;                   /**< Valid iff kind == ISSUER_AND_SERIAL. */
    const uint8_t *subject_key_id;
    size_t subject_key_id_size;           /**< Valid iff kind == SUBJECT_KEY_IDENTIFIER. */
} xx_pkcs7_signer_identifier;

/** Attribute ::= SEQUENCE { attrType OID, attrValues SET OF AttributeValue }
 * (at least one value; `values_der` is the SET's content so a caller can
 * iterate every value with xx_pkcs7_attr_values_begin()). */
typedef struct xx_pkcs7_attribute {
    char oid_text[96];
    const uint8_t *oid_der;
    size_t oid_der_size;
    const uint8_t *values_der;
    size_t values_der_size;
} xx_pkcs7_attribute;

typedef struct xx_pkcs7_signer_info {
    int64_t version;
    xx_pkcs7_signer_identifier sid;
    xx_x509_algorithm_id digest_algorithm;
    bool has_signed_attrs;
    const uint8_t *signed_attrs_content;  /**< Content of the [0] IMPLICIT SET (the Attribute elements only, not the [0] header). */
    size_t signed_attrs_content_size;
    xx_x509_algorithm_id signature_algorithm;
    const uint8_t *signature;             /**< OCTET STRING content (zero-copy; assumes the common primitive encoding). */
    size_t signature_size;
    bool has_unsigned_attrs;
    const uint8_t *unsigned_attrs_content;
    size_t unsigned_attrs_content_size;
} xx_pkcs7_signer_info;

/** Parses a SignerInfo. Also usable standalone on a PKCS#9 countersignature
 * attribute value, which RFC 2985 defines as "Countersignature ::= SignerInfo".
 * Matches YARA-X: sid must be issuerAndSerialNumber; version is any i32. */
XXFC_API bool xx_pkcs7_parse_signer_info(const uint8_t *der, size_t size, xx_pkcs7_signer_info *out);

typedef struct xx_pkcs7_certificate_choice {
    bool is_certificate;             /**< True when this CertificateChoices was a plain Certificate (the common case). */
    xx_x509_certificate certificate; /**< Valid iff is_certificate. */
    const uint8_t *raw;              /**< Raw DER of this CertificateChoices element either way, header included. */
    size_t raw_size;
} xx_pkcs7_certificate_choice;

typedef struct xx_pkcs7_signed_data {
    const uint8_t *der;
    size_t der_size;
    int64_t version;
    const uint8_t *digest_algorithms_der; /**< Content of the SET OF DigestAlgorithmIdentifier. */
    size_t digest_algorithms_der_size;
    xx_pkcs7_encap_content_info content_info;
    bool has_certificates;
    const uint8_t *certificates_der;      /**< Content of the [0] IMPLICIT SET OF CertificateChoices. */
    size_t certificates_der_size;
    bool has_crls;
    const uint8_t *crls_der;              /**< Content of the [1] IMPLICIT SET, raw only (not in scope beyond presence/bytes). */
    size_t crls_der_size;
    const uint8_t *signer_infos_der;      /**< Content of the SET OF SignerInfo. */
    size_t signer_infos_der_size;
} xx_pkcs7_signed_data;

/** Parses a SignedData value (RFC 5652 5.1). `der`/`size` is the SignedData
 * SEQUENCE itself - typically xx_pkcs7_content_info.content_tlv after
 * checking content_type_oid == XX_PKCS7_OID_SIGNED_DATA. */
XXFC_API bool xx_pkcs7_parse_signed_data(const uint8_t *der, size_t size, xx_pkcs7_signed_data *out);
/** xx_pkcs7_parse_content_info() + the OID check + xx_pkcs7_parse_signed_data() in one call. */
XXFC_API bool xx_pkcs7_parse_signed_data_content_info(const uint8_t *der, size_t size, xx_pkcs7_content_info *out_content_info, xx_pkcs7_signed_data *out_signed_data);

XXFC_API bool xx_pkcs7_digest_algorithms_begin(const xx_pkcs7_signed_data *sd, xx_asn1_cursor *cursor);
XXFC_API bool xx_pkcs7_digest_algorithms_next(xx_asn1_cursor *cursor, xx_x509_algorithm_id *out);

XXFC_API bool xx_pkcs7_certificates_begin(const xx_pkcs7_signed_data *sd, xx_asn1_cursor *cursor);
/** Returns the valid X.509 certificate prefix. First malformed/other choice
 * ends iteration (false and cursor at end); earlier certificates stay usable. */
XXFC_API bool xx_pkcs7_certificates_next(xx_asn1_cursor *cursor, xx_pkcs7_certificate_choice *out);

XXFC_API bool xx_pkcs7_signer_infos_begin(const xx_pkcs7_signed_data *sd, xx_asn1_cursor *cursor);
XXFC_API bool xx_pkcs7_signer_infos_next(xx_asn1_cursor *cursor, xx_pkcs7_signer_info *out);

XXFC_API bool xx_pkcs7_attrs_begin_signed(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor);
XXFC_API bool xx_pkcs7_attrs_begin_unsigned(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor);
XXFC_API bool xx_pkcs7_attrs_next(xx_asn1_cursor *cursor, xx_pkcs7_attribute *out);
XXFC_API bool xx_pkcs7_attr_values_begin(const xx_pkcs7_attribute *attr, xx_asn1_cursor *cursor);
XXFC_API bool xx_pkcs7_find_attr_signed(const xx_pkcs7_signer_info *si, const char *oid_text, xx_pkcs7_attribute *out);
XXFC_API bool xx_pkcs7_find_attr_unsigned(const xx_pkcs7_signer_info *si, const char *oid_text, xx_pkcs7_attribute *out);

/* --------------------------------------------------------------- helpers -- */

/** Re-tags signedAttrs (an IMPLICIT [0] SET) as a universal SET OF (RFC 5652
 * 5.4) into a caller buffer, ready to be hashed. False if signedAttrs is
 * absent or `out_capacity` is too small. Attribute children must already be
 * left byte-for-byte as received, without sorting or recursive normalization,
 * as in YARA-X's Set::new(raw_signed_attrs).write_der(). */
XXFC_API bool xx_pkcs7_signed_attrs_der_set(const xx_pkcs7_signer_info *si, uint8_t *out, size_t out_capacity, size_t *out_size);

/** Finds the certificate among sd's certificates matching si's SignerIdentifier
 * (by issuer+serial, or by subjectKeyIdentifier extension). */
XXFC_API bool xx_pkcs7_find_signer_certificate(const xx_pkcs7_signed_data *sd, const xx_pkcs7_signer_info *si, xx_x509_certificate *out);

/** Compares the messageDigest signed attribute (if present) against an
 * independently computed digest of the encapsulated content. */
XXFC_API bool xx_pkcs7_check_message_digest(const xx_pkcs7_signer_info *si, const uint8_t *content_digest, size_t content_digest_size);

/** Verifies si's signature: hashes the DER-SET-reencoded signedAttrs when
 * present (RFC 5652 5.4), otherwise hashes `econtent`/`econtent_size`
 * directly (required in that case - pass NULL/0 to fail safely rather than
 * guess; a non-NULL pointer with size zero represents empty content). The
 * digest algorithm is si->digest_algorithm; the asymmetric operation is
 * derived from the SPKI and signature_algorithm is ignored, as in YARA-X. This
 * verifies the SignerInfo signature only: callers separately check the
 * messageDigest attribute against the content. Internally
 * hashes a minimal SET header followed by the original attribute bytes. */
XXFC_API bool xx_pkcs7_verify_signer_info(const xx_pkcs7_signer_info *si, const xx_x509_certificate *signer_cert, const uint8_t *econtent, size_t econtent_size);

#ifdef __cplusplus
}
#endif
#endif /* XX_PKCS7_H */
