/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_x509.h
 * @brief X.509 Certificate (RFC 5280) parsing: views into the caller's DER buffer.
 *
 * xx_x509_parse_certificate() only walks the top-level TBSCertificate fields;
 * deeper structure (RDN attributes inside a Name, the extension list) is
 * exposed through the xx_asn1_cursor-based iterators below and is decoded
 * lazily, on demand, by whichever caller needs it. Every "_der"/"value"
 * field is a pointer into the buffer originally passed to
 * xx_x509_parse_certificate(); nothing here allocates or copies.
 */
#ifndef XX_X509_H
#define XX_X509_H

#include "xxfclib/algo/asn1/xx_asn1.h"
#include "xxfclib/algo/pki/xx_pki_verify.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The complete raw DER of an X.509 Name (its SEQUENCE OF RDN, header included). */
typedef struct xx_x509_name {
    const uint8_t *der;
    size_t der_size;
} xx_x509_name;

/** One AttributeTypeAndValue inside a RelativeDistinguishedName. */
typedef struct xx_x509_rdn_attr {
    const uint8_t *oid_der;
    size_t oid_der_size;
    char oid_text[96];
    uint32_t value_tag;  /**< The value's own ASN.1 tag (e.g. PrintableString, UTF8String, ...). */
    xx_asn1_class value_class;
    bool value_constructed;
    const uint8_t *value; /**< Raw content bytes of the value (not re-decoded by this layer). */
    size_t value_size;
} xx_x509_rdn_attr;

typedef struct xx_x509_validity {
    int64_t not_before; /**< Unix seconds. */
    int64_t not_after;
} xx_x509_validity;

/** AlgorithmIdentifier ::= SEQUENCE { algorithm OID, parameters ANY OPTIONAL }. */
typedef struct xx_x509_algorithm_id {
    char oid_text[96];
    const uint8_t *oid_der;
    size_t oid_der_size;
    const uint8_t *params_der;  /**< Complete raw TLV (header+content) of `parameters`, or NULL if absent. */
    size_t params_der_size;
} xx_x509_algorithm_id;

/** Extension ::= SEQUENCE { extnID OID, critical BOOLEAN DEFAULT FALSE, extnValue OCTET STRING }. */
typedef struct xx_x509_extension {
    char oid_text[96];
    const uint8_t *oid_der;
    size_t oid_der_size;
    bool critical;
    const uint8_t *value; /**< Raw content of extnValue (the extension-specific DER, not decoded further). */
    size_t value_size;
} xx_x509_extension;

typedef struct xx_x509_certificate {
    const uint8_t *der;     /**< Whole Certificate SEQUENCE, header included. */
    size_t der_size;
    const uint8_t *tbs_der; /**< Whole TBSCertificate SEQUENCE, header included - this is what gets hashed and verified. */
    size_t tbs_der_size;
    int64_t version;        /**< 0, 1 or 2 for v1/v2/v3 (the DEFAULT when absent is 0). */
    const uint8_t *serial;  /**< Raw big-endian two's complement bytes. */
    size_t serial_size;
    xx_x509_algorithm_id tbs_signature_alg; /**< tbsCertificate.signature. */
    xx_x509_name issuer;
    xx_x509_validity validity;
    xx_x509_name subject;
    const uint8_t *spki_der; /**< Whole SubjectPublicKeyInfo SEQUENCE, header included. */
    size_t spki_der_size;
    bool has_extensions;
    const uint8_t *extensions_der; /**< Content of the Extensions SEQUENCE (each child is one Extension). */
    size_t extensions_der_size;
    xx_x509_algorithm_id signature_alg; /**< Certificate.signatureAlgorithm (the outer one, actually used to sign). */
    const uint8_t *signature;           /**< Raw signature octets, excluding BIT STRING unused-bit count. */
    size_t signature_size;
    unsigned signature_unused_bits;   /**< Retained for inspection; YARA-X verification uses the raw octets and ignores this count. */
    bool signature_algorithms_match;   /**< Inner and outer AlgorithmIdentifiers agree byte-for-byte. */
} xx_x509_certificate;

/** Parses a Certificate. Reports whether tbsCertificate.signature and the
 * outer signatureAlgorithm agree (RFC 5280 4.1.1.2) and checks every field it
 * reports has the expected tag; rejects trailing input. Name attributes and
 * extension values are decoded lazily by the iterators. BER/nonminimal
 * certificate encodings are rejected. This does not perform certificate path, trust, policy,
 * revocation, name matching, or validity-at-current-time checks. */
XXFC_API bool xx_x509_parse_certificate(const uint8_t *der, size_t size, xx_x509_certificate *out);

/** AlgorithmIdentifier ::= SEQUENCE { algorithm OID, parameters ANY OPTIONAL }.
 * Exported because PKCS#7/CMS (SignerInfo.digestAlgorithm/signatureAlgorithm,
 * SignedData.digestAlgorithms) reuses this exact X.509 type verbatim. */
XXFC_API bool xx_x509_parse_algorithm_identifier(const xx_asn1_tlv *seq, xx_x509_algorithm_id *out);

/* --------------------------------------------------------------- names -- */

XXFC_API bool xx_x509_name_rdns_begin(const xx_x509_name *name, xx_asn1_cursor *cursor);
/** Advances to the next RelativeDistinguishedName (a SET); fails if it is not one. */
XXFC_API bool xx_x509_name_rdns_next(xx_asn1_cursor *cursor, xx_asn1_tlv *out_rdn_set);
XXFC_API bool xx_x509_rdn_attrs_begin(const xx_asn1_tlv *rdn_set, xx_asn1_cursor *cursor);
XXFC_API bool xx_x509_rdn_attrs_next(xx_asn1_cursor *cursor, xx_x509_rdn_attr *out);
/** Convenience: the first attribute anywhere in `name` whose OID text matches exactly. */
XXFC_API bool xx_x509_name_find_attribute(const xx_x509_name *name, const char *oid_text, xx_x509_rdn_attr *out);

/* ---------------------------------------------------------- extensions -- */

XXFC_API bool xx_x509_extensions_begin(const xx_x509_certificate *cert, xx_asn1_cursor *cursor);
XXFC_API bool xx_x509_extensions_next(xx_asn1_cursor *cursor, xx_x509_extension *out);
/** Convenience: the first extension whose OID text matches exactly. */
XXFC_API bool xx_x509_find_extension(const xx_x509_certificate *cert, const char *oid_text, xx_x509_extension *out);

/* ------------------------------------------------------------- hashing -- */

/** SHA-1 over the complete Certificate DER (cert->der), via xx_pki_hash_memory(). */
XXFC_API bool xx_x509_thumbprint_sha1(const xx_x509_certificate *cert, uint8_t out_digest[20]);

/** The hash algorithm implied by a SignatureAlgorithmIdentifier (AlgorithmIdentifier
 * is shared verbatim between X.509 and PKCS#7/CMS, so pkcs7 reuses this too). For
 * RSASSA-PSS (1.2.840.113549.1.1.10), the OID alone is not enough - the hash is
 * named by the hashAlgorithm field of the RSASSA-PSS-params SEQUENCE in `params_der`
 * (RFC 4055), defaulting to SHA-1 if that field, or `params_der` itself, is absent.
 * Every other OID is resolved directly through xx_pki_hash_from_oid(). Returns
 * XX_PKI_HASH_NONE if the OID is unrecognized or the PSS parameters are malformed. */
XXFC_API xx_pki_hash xx_x509_signature_hash(const xx_x509_algorithm_id *alg);

/** Hashes the TBSCertificate with the hash implied by cert->signature_alg and
 * verifies cert->signature against it using issuer's public key, via
 * xx_pki_verify_digest() in auto mode, matching YARA-X's PublicKey::verify:
 * the outer algorithm selects a supported hash, and the issuer SPKI selects
 * the asymmetric operation. Inner/outer mismatch does not prevent verifying.
 * Does not check validity, names, trust, BasicConstraints or key usage. */
XXFC_API bool xx_x509_verify_signed_by(const xx_x509_certificate *cert, const xx_x509_certificate *issuer);

/** Equivalent to xx_x509_verify_signed_by(); verifies the signature only. */
XXFC_API bool xx_x509_verify(const xx_x509_certificate *cert, const xx_x509_certificate *issuer);

/** BigUint comparison: ignores leading zero bytes and interprets all bytes
 * as unsigned, including serials whose high bit is set. */
XXFC_API bool xx_x509_serial_equal(const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size);
/** Parsed strict-DER X509Name equality is raw Name DER equality upstream. */
XXFC_API bool xx_x509_name_equal(const xx_x509_name *a, const xx_x509_name *b);
/** YARA-X chain iteration: append start, follow first matching subject/issuer,
 * stop at self-issued/external or a previously selected parent subject DER.
 * SIZE_MAX starts an empty chain. The seen set initially excludes the start,
 * so a cycle can append the start twice; count+1 indices always suffice.
 * Returns false on invalid arguments/capacity; *out_size is written on success. */
XXFC_API bool xx_x509_build_chain(const xx_x509_certificate *certs, size_t count, size_t start_index,
                                 size_t *indices, size_t capacity, size_t *out_size);
/** Verifies adjacent pairs only. Empty/singleton chains succeed, and the
 * terminal self-issued certificate is not independently signature-checked. */
XXFC_API bool xx_x509_verify_chain(const xx_x509_certificate *certs, size_t count,
                                  const size_t *indices, size_t chain_size);

#ifdef __cplusplus
}
#endif
#endif /* XX_X509_H */
