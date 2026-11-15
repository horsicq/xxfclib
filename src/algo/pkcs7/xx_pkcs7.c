/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/pkcs7/xx_pkcs7.h"

#include <string.h>
#include <stdio.h>

static bool require_sequence(const xx_asn1_tlv *tlv)
{
    return tlv != NULL && xx_asn1_is_universal(tlv, (uint32_t)XX_ASN1_TAG_SEQUENCE) && tlv->constructed;
}

/* ContentInfo converts to const_oid::ObjectIdentifier immediately: malformed
 * root/length/unterminated arcs reject the structure. Unlike AlgorithmIdentifier
 * this field cannot retain an invalid OID for certificate display. */
static bool content_type_oid_text(const uint8_t *der, size_t size, char *out, size_t capacity)
{
    size_t i = 1U, written;
    int n;
    if (!der || !size || size > 39U || der[0] > 119U || !out || !capacity) return false;
    n = snprintf(out, capacity, "%u.%u", (unsigned)(der[0]/40U), (unsigned)(der[0]%40U));
    if (n < 0 || (size_t)n >= capacity) return false;
    written = (size_t)n;
    while (i < size) {
        uint32_t arc = 0U;
        uint8_t byte;
        do {
            if (i >= size) return false;
            byte = der[i++]; arc = (arc << 7U) | (uint32_t)(byte & 0x7fU);
        } while (byte & 0x80U);
        n = snprintf(out + written, capacity - written, ".%u", (unsigned)arc);
        if (n < 0 || (size_t)n >= capacity - written) return false;
        written += (size_t)n;
    }
    return true;
}

/* ----------------------------------------------------------- ContentInfo -- */

bool xx_pkcs7_parse_content_info(const uint8_t *der, size_t size, xx_pkcs7_content_info *out)
{
    xx_asn1_tlv outer, oid_tlv;
    xx_asn1_cursor cursor;
    size_t used;

    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!xx_asn1_parse_tlv(der, size, &outer, &used)) return false;
    if (!require_sequence(&outer)) return false;
    if (!xx_asn1_cursor_init(&cursor, &outer, 1U)) return false;

    if (!xx_asn1_cursor_next(&cursor, &oid_tlv)) return false;
    if (!xx_asn1_read_oid_raw(&oid_tlv, &out->content_type_oid_der, &out->content_type_oid_der_size)) return false;
    if (!content_type_oid_text(out->content_type_oid_der, out->content_type_oid_der_size, out->content_type_oid, sizeof(out->content_type_oid))) return false;

    if (xx_asn1_cursor_at_end(&cursor)) {
        out->has_content = false;
        return true;
    }
    {
        xx_asn1_tlv explicit0, content_tlv;
        xx_asn1_cursor inner;
        if (!xx_asn1_cursor_next(&cursor, &explicit0) || !xx_asn1_cursor_at_end(&cursor)) return false;
        if (explicit0.tag_class != XX_ASN1_CLASS_CONTEXT || explicit0.tag != 0U || !explicit0.constructed) return false;
        if (!xx_asn1_cursor_init(&inner, &explicit0, 2U)) return false;
        if (!xx_asn1_cursor_next(&inner, &content_tlv) || !xx_asn1_cursor_at_end(&inner)) return false;
        out->content_tlv = content_tlv.header;
        out->content_tlv_size = content_tlv.total_size;
        out->has_content = true;
    }
    return true;
}

/* ------------------------------------------------- EncapsulatedContentInfo -- */

static bool parse_encap_content_info(const xx_asn1_tlv *seq, xx_pkcs7_encap_content_info *out)
{
    xx_asn1_cursor cursor;
    xx_asn1_tlv oid_tlv;

    memset(out, 0, sizeof(*out));
    if (!require_sequence(seq)) return false;
    if (!xx_asn1_cursor_init(&cursor, seq, 2U)) return false;

    if (!xx_asn1_cursor_next(&cursor, &oid_tlv)) return false;
    if (!xx_asn1_read_oid_raw(&oid_tlv, &out->content_type_oid_der, &out->content_type_oid_der_size)) return false;
    if (!content_type_oid_text(out->content_type_oid_der, out->content_type_oid_der_size, out->content_type_oid, sizeof(out->content_type_oid))) return false;

    if (xx_asn1_cursor_at_end(&cursor)) {
        out->has_content = false;
        return true;
    }
    {
        xx_asn1_tlv explicit0, content_tlv;
        xx_asn1_cursor inner;
        if (!xx_asn1_cursor_next(&cursor, &explicit0) || !xx_asn1_cursor_at_end(&cursor)) return false;
        if (explicit0.tag_class != XX_ASN1_CLASS_CONTEXT || explicit0.tag != 0U || !explicit0.constructed) return false;
        if (!xx_asn1_cursor_init(&inner, &explicit0, 3U)) return false;
        if (!xx_asn1_cursor_next(&inner, &content_tlv) || !xx_asn1_cursor_at_end(&inner)) return false;
        out->content_tlv = content_tlv.header;
        out->content_tlv_size = content_tlv.total_size;
        out->content_data = content_tlv.content;
        out->content_data_size = content_tlv.content_size;
        out->has_content = true;
        out->content_is_octet_string = xx_asn1_is_universal(&content_tlv, (uint32_t)XX_ASN1_TAG_OCTET_STRING);
        if (out->content_is_octet_string && !content_tlv.constructed) {
            out->octet_string_content = content_tlv.content;
            out->octet_string_content_size = content_tlv.content_size;
        }
    }
    return true;
}

/* ----------------------------------------------------------------- SignerInfo -- */

bool xx_pkcs7_parse_signer_info(const uint8_t *der, size_t size, xx_pkcs7_signer_info *out)
{
    xx_asn1_tlv seq;
    xx_asn1_cursor cursor;
    size_t used;
    xx_asn1_tlv version_tlv, sid_tlv, digest_alg_tlv, next_tlv, sig_tlv;

    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!xx_asn1_parse_tlv(der, size, &seq, &used) || used != size) return false;
    if (!require_sequence(&seq)) return false;
    if (!xx_asn1_cursor_init(&cursor, &seq, 1U)) return false;

    if (!xx_asn1_cursor_next(&cursor, &version_tlv) || !xx_asn1_read_int64(&version_tlv, &out->version)) return false;

    if (!xx_asn1_cursor_next(&cursor, &sid_tlv)) return false;
    if (xx_asn1_is_universal(&sid_tlv, (uint32_t)XX_ASN1_TAG_SEQUENCE) && sid_tlv.constructed) {
        /* issuerAndSerialNumber ::= SEQUENCE { issuer Name, serialNumber INTEGER } */
        xx_asn1_cursor sid_cursor;
        xx_asn1_tlv issuer_tlv, serial_tlv;
        if (!xx_asn1_cursor_init(&sid_cursor, &sid_tlv, 2U)) return false;
        if (!xx_asn1_cursor_next(&sid_cursor, &issuer_tlv) || !require_sequence(&issuer_tlv)) return false;
        if (!xx_asn1_cursor_next(&sid_cursor, &serial_tlv) || !xx_asn1_read_integer(&serial_tlv, &out->sid.serial, &out->sid.serial_size)) return false;
        if (!xx_asn1_cursor_at_end(&sid_cursor)) return false;
        out->sid.kind = XX_PKCS7_SID_ISSUER_AND_SERIAL;
        out->sid.issuer.der = issuer_tlv.header;
        out->sid.issuer.der_size = issuer_tlv.total_size;
    } else {
        return false; /* YARA-X only accepts issuerAndSerialNumber. */
    }
    if (out->version < INT32_MIN || out->version > INT32_MAX) return false;

    if (!xx_asn1_cursor_next(&cursor, &digest_alg_tlv)) return false;
    if (!xx_x509_parse_algorithm_identifier(&digest_alg_tlv, &out->digest_algorithm)) return false;

    if (!xx_asn1_cursor_next(&cursor, &next_tlv)) return false;
    if (next_tlv.tag_class == XX_ASN1_CLASS_CONTEXT && next_tlv.tag == 0U && next_tlv.constructed) {
        out->has_signed_attrs = true;
        out->signed_attrs_content = next_tlv.content;
        out->signed_attrs_content_size = next_tlv.content_size;
        if (!xx_asn1_cursor_next(&cursor, &next_tlv)) return false;
    }
    if (!xx_x509_parse_algorithm_identifier(&next_tlv, &out->signature_algorithm)) return false;

    if (!xx_asn1_cursor_next(&cursor, &sig_tlv)) return false;
    if (!xx_asn1_read_octet_string_primitive(&sig_tlv, &out->signature, &out->signature_size)) return false;

    if (!xx_asn1_cursor_at_end(&cursor)) {
        xx_asn1_tlv unsigned_tlv;
        if (!xx_asn1_cursor_next(&cursor, &unsigned_tlv)) return false;
        if (unsigned_tlv.tag_class != XX_ASN1_CLASS_CONTEXT || unsigned_tlv.tag != 1U || !unsigned_tlv.constructed) return false;
        out->has_unsigned_attrs = true;
        out->unsigned_attrs_content = unsigned_tlv.content;
        out->unsigned_attrs_content_size = unsigned_tlv.content_size;
        if (!xx_asn1_cursor_at_end(&cursor)) return false;
    }
    return true;
}

/* ----------------------------------------------------------------- SignedData -- */

bool xx_pkcs7_parse_signed_data(const uint8_t *der, size_t size, xx_pkcs7_signed_data *out)
{
    xx_asn1_tlv seq;
    size_t used;
    xx_asn1_cursor cursor;
    xx_asn1_tlv version_tlv, digest_algs_tlv, encap_tlv;

    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!xx_asn1_parse_tlv(der, size, &seq, &used) || used != size) return false;
    if (!require_sequence(&seq)) return false;
    out->der = seq.header;
    out->der_size = seq.total_size;
    if (!xx_asn1_cursor_init(&cursor, &seq, 1U)) return false;

    if (!xx_asn1_cursor_next(&cursor, &version_tlv) || !xx_asn1_read_int64(&version_tlv, &out->version)) return false;

    if (!xx_asn1_cursor_next(&cursor, &digest_algs_tlv)) return false;
    if (!xx_asn1_is_universal(&digest_algs_tlv, (uint32_t)XX_ASN1_TAG_SET) || !digest_algs_tlv.constructed) return false;
    out->digest_algorithms_der = digest_algs_tlv.content;
    out->digest_algorithms_der_size = digest_algs_tlv.content_size;

    if (!xx_asn1_cursor_next(&cursor, &encap_tlv)) return false;
    if (!parse_encap_content_info(&encap_tlv, &out->content_info)) return false;

    for (;;) {
        xx_asn1_tlv field;
        if (!xx_asn1_cursor_next(&cursor, &field)) return false; /* signerInfos is mandatory */
        if (field.tag_class == XX_ASN1_CLASS_CONTEXT && field.constructed) {
            if (field.tag == 0U) {
                if (out->has_certificates || out->has_crls) return false;
                out->certificates_der = field.content;
                out->certificates_der_size = field.content_size;
                out->has_certificates = true;
                continue;
            }
            if (field.tag == 1U) {
                if (out->has_crls) return false;
                out->crls_der = field.content;
                out->crls_der_size = field.content_size;
                out->has_crls = true;
                continue;
            }
            return false; /* no other context-tagged field is legal here */
        }
        if (!xx_asn1_is_universal(&field, (uint32_t)XX_ASN1_TAG_SET) || !field.constructed) return false;
        out->signer_infos_der = field.content;
        out->signer_infos_der_size = field.content_size;
        break;
    }
    if (!xx_asn1_cursor_at_end(&cursor)) return false;
    return true;
}

bool xx_pkcs7_parse_signed_data_content_info(const uint8_t *der, size_t size, xx_pkcs7_content_info *out_content_info, xx_pkcs7_signed_data *out_signed_data)
{
    if (!xx_pkcs7_parse_content_info(der, size, out_content_info)) return false;
    if (strcmp(out_content_info->content_type_oid, XX_PKCS7_OID_SIGNED_DATA) != 0) return false;
    if (!out_content_info->has_content) return false;
    return xx_pkcs7_parse_signed_data(out_content_info->content_tlv, out_content_info->content_tlv_size, out_signed_data);
}

bool xx_pkcs7_digest_algorithms_begin(const xx_pkcs7_signed_data *sd, xx_asn1_cursor *cursor)
{
    if (!sd) return false;
    return xx_asn1_cursor_init_span(cursor, sd->digest_algorithms_der, sd->digest_algorithms_der_size, 1U);
}

bool xx_pkcs7_digest_algorithms_next(xx_asn1_cursor *cursor, xx_x509_algorithm_id *out)
{
    xx_asn1_tlv alg_seq;
    if (!xx_asn1_cursor_next(cursor, &alg_seq)) return false;
    return xx_x509_parse_algorithm_identifier(&alg_seq, out);
}

bool xx_pkcs7_certificates_begin(const xx_pkcs7_signed_data *sd, xx_asn1_cursor *cursor)
{
    if (!sd || !sd->has_certificates) return false;
    return xx_asn1_cursor_init_span(cursor, sd->certificates_der, sd->certificates_der_size, 1U);
}

bool xx_pkcs7_certificates_next(xx_asn1_cursor *cursor, xx_pkcs7_certificate_choice *out)
{
    xx_asn1_tlv choice;
    if (!cursor || !out) return false;
    memset(out, 0, sizeof(*out));
    if (!xx_asn1_cursor_next(cursor, &choice) ||
        !xx_x509_parse_certificate(choice.header, choice.total_size, &out->certificate)) {
        cursor->pos = cursor->limit; /* valid prefix, as parse_certificates upstream */
        return false;
    }
    out->raw = choice.header;
    out->raw_size = choice.total_size;
    out->is_certificate = true;
    return true;
}

bool xx_pkcs7_signer_infos_begin(const xx_pkcs7_signed_data *sd, xx_asn1_cursor *cursor)
{
    if (!sd) return false;
    return xx_asn1_cursor_init_span(cursor, sd->signer_infos_der, sd->signer_infos_der_size, 1U);
}

bool xx_pkcs7_signer_infos_next(xx_asn1_cursor *cursor, xx_pkcs7_signer_info *out)
{
    xx_asn1_tlv si_seq;
    if (!xx_asn1_cursor_next(cursor, &si_seq)) return false;
    return xx_pkcs7_parse_signer_info(si_seq.header, si_seq.total_size, out);
}

/* ----------------------------------------------------------------- Attributes -- */

bool xx_pkcs7_attrs_begin_signed(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor)
{
    if (!si || !si->has_signed_attrs) return false;
    return xx_asn1_cursor_init_span(cursor, si->signed_attrs_content, si->signed_attrs_content_size, 1U);
}

bool xx_pkcs7_attrs_begin_unsigned(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor)
{
    if (!si || !si->has_unsigned_attrs) return false;
    return xx_asn1_cursor_init_span(cursor, si->unsigned_attrs_content, si->unsigned_attrs_content_size, 1U);
}

bool xx_pkcs7_attrs_next(xx_asn1_cursor *cursor, xx_pkcs7_attribute *out)
{
    xx_asn1_tlv attr_seq, oid_tlv, values_tlv;
    xx_asn1_cursor attr_cursor;

    if (!out || !xx_asn1_cursor_next(cursor, &attr_seq)) return false;
    if (!require_sequence(&attr_seq)) return false;
    if (!xx_asn1_cursor_init(&attr_cursor, &attr_seq, 2U)) return false;

    if (!xx_asn1_cursor_next(&attr_cursor, &oid_tlv)) return false;
    if (!xx_asn1_read_oid_raw(&oid_tlv, &out->oid_der, &out->oid_der_size)) return false;
    if (!content_type_oid_text(out->oid_der, out->oid_der_size, out->oid_text, sizeof(out->oid_text))) return false;

    if (!xx_asn1_cursor_next(&attr_cursor, &values_tlv)) return false;
    if (!xx_asn1_is_universal(&values_tlv, (uint32_t)XX_ASN1_TAG_SET) || !values_tlv.constructed) return false;
    if (values_tlv.content_size == 0U) return false;
    if (!xx_asn1_cursor_at_end(&attr_cursor)) return false;
    out->values_der = values_tlv.content;
    out->values_der_size = values_tlv.content_size;
    return true;
}

bool xx_pkcs7_attr_values_begin(const xx_pkcs7_attribute *attr, xx_asn1_cursor *cursor)
{
    if (!attr) return false;
    return xx_asn1_cursor_init_span(cursor, attr->values_der, attr->values_der_size, 2U);
}

static bool find_attr(const uint8_t *content, size_t content_size, const char *oid_text, xx_pkcs7_attribute *out)
{
    xx_asn1_cursor cursor;
    bool found = false;
    if (!xx_asn1_cursor_init_span(&cursor, content, content_size, 1U)) return false;
    while (!xx_asn1_cursor_at_end(&cursor)) {
        xx_pkcs7_attribute attr;
        if (!xx_pkcs7_attrs_next(&cursor, &attr)) return false;
        if (strcmp(attr.oid_text, oid_text) == 0) {
            if (found) return false;
            *out = attr;
            found = true;
        }
    }
    return found;
}

bool xx_pkcs7_find_attr_signed(const xx_pkcs7_signer_info *si, const char *oid_text, xx_pkcs7_attribute *out)
{
    if (!si || !si->has_signed_attrs || !oid_text || !out) return false;
    return find_attr(si->signed_attrs_content, si->signed_attrs_content_size, oid_text, out);
}

bool xx_pkcs7_find_attr_unsigned(const xx_pkcs7_signer_info *si, const char *oid_text, xx_pkcs7_attribute *out)
{
    if (!si || !si->has_unsigned_attrs || !oid_text || !out) return false;
    return find_attr(si->unsigned_attrs_content, si->unsigned_attrs_content_size, oid_text, out);
}

/* --------------------------------------------------------------- helpers -- */

bool xx_pkcs7_signed_attrs_der_set(const xx_pkcs7_signer_info *si, uint8_t *out, size_t out_capacity, size_t *out_size)
{
    xx_asn1_tlv fake_tlv;
    if (!si || !si->has_signed_attrs || !out_size) return false;
    if (!si->signed_attrs_content && si->signed_attrs_content_size) return false;
    memset(&fake_tlv, 0, sizeof(fake_tlv));
    fake_tlv.constructed = true;
    fake_tlv.content = si->signed_attrs_content;
    fake_tlv.content_size = si->signed_attrs_content_size;
    return xx_asn1_reencode(&fake_tlv, XX_ASN1_CLASS_UNIVERSAL, true, (uint32_t)XX_ASN1_TAG_SET, out, out_capacity, out_size);
}

bool xx_pkcs7_find_signer_certificate(const xx_pkcs7_signed_data *sd, const xx_pkcs7_signer_info *si, xx_x509_certificate *out)
{
    xx_asn1_cursor cursor;
    if (!sd || !si || !out) return false;
    if (!xx_pkcs7_certificates_begin(sd, &cursor)) return false;
    while (!xx_asn1_cursor_at_end(&cursor)) {
        xx_pkcs7_certificate_choice choice;
        if (!xx_pkcs7_certificates_next(&cursor, &choice)) return false;
        if (!choice.is_certificate) continue;

        if (si->sid.kind == XX_PKCS7_SID_ISSUER_AND_SERIAL) {
            if (xx_x509_serial_equal(choice.certificate.serial, choice.certificate.serial_size, si->sid.serial, si->sid.serial_size) &&
                choice.certificate.issuer.der_size == si->sid.issuer.der_size && memcmp(choice.certificate.issuer.der, si->sid.issuer.der, si->sid.issuer.der_size) == 0) {
                *out = choice.certificate;
                return true;
            }
        } else {
            xx_x509_extension ski_ext;
            if (xx_x509_find_extension(&choice.certificate, "2.5.29.14", &ski_ext)) {
                /* SubjectKeyIdentifier ::= KeyIdentifier ::= OCTET STRING, itself DER-encoded
                 * inside extnValue (which is ALSO an OCTET STRING) - two layers to unwrap. */
                xx_asn1_tlv ski_tlv;
                size_t used;
                if (xx_asn1_parse_tlv(ski_ext.value, ski_ext.value_size, &ski_tlv, &used) && used == ski_ext.value_size) {
                    const uint8_t *ski_bytes;
                    size_t ski_size;
                    if (xx_asn1_read_octet_string_primitive(&ski_tlv, &ski_bytes, &ski_size)) {
                        if (ski_size == si->sid.subject_key_id_size && memcmp(ski_bytes, si->sid.subject_key_id, ski_size) == 0) {
                            *out = choice.certificate;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool xx_pkcs7_check_message_digest(const xx_pkcs7_signer_info *si, const uint8_t *content_digest, size_t content_digest_size)
{
    xx_pkcs7_attribute attr;
    xx_asn1_cursor values_cursor;
    xx_asn1_tlv value_tlv;
    const uint8_t *attr_digest;
    size_t attr_digest_size;

    if (!si || !content_digest || content_digest_size == 0U) return false;
    if (!xx_pkcs7_find_attr_signed(si, XX_PKCS9_OID_MESSAGE_DIGEST, &attr)) return false;
    if (!xx_pkcs7_attr_values_begin(&attr, &values_cursor)) return false;
    if (!xx_asn1_cursor_next(&values_cursor, &value_tlv) || !xx_asn1_cursor_at_end(&values_cursor)) return false; /* exactly one value */
    if (!xx_asn1_read_octet_string_primitive(&value_tlv, &attr_digest, &attr_digest_size)) return false;
    if (attr_digest_size != content_digest_size) return false;
    return memcmp(attr_digest, content_digest, content_digest_size) == 0;
}

bool xx_pkcs7_verify_signer_info(const xx_pkcs7_signer_info *si, const xx_x509_certificate *signer_cert, const uint8_t *econtent, size_t econtent_size)
{
    xx_pki_hash alg;
    uint8_t digest[64];
    size_t digest_size;

    if (!si || !signer_cert) return false;
    if (si->digest_algorithm.oid_der_size) {
        char canonical[96]; bool nonminimal = false;
        if (!xx_asn1_oid_to_text(si->digest_algorithm.oid_der, si->digest_algorithm.oid_der_size,
                                 canonical, sizeof(canonical), &nonminimal) || nonminimal ||
            strcmp(canonical, si->digest_algorithm.oid_text)) return false;
    }
    alg = xx_pki_hash_from_oid(si->digest_algorithm.oid_text);
    if (alg == XX_PKI_HASH_NONE) return false;
    digest_size = xx_pki_hash_size(alg);
    if (digest_size == 0U || digest_size > sizeof(digest)) return false;

    if (si->has_signed_attrs) {
        uint8_t header[16];
        size_t header_size;
        xx_pki_hash_part parts[2];
        header_size = xx_asn1_encode_header(XX_ASN1_CLASS_UNIVERSAL, true, XX_ASN1_TAG_SET,
                                           si->signed_attrs_content_size, header, sizeof(header));
        if (!header_size) return false;
        parts[0].data = header; parts[0].size = header_size;
        parts[1].data = si->signed_attrs_content; parts[1].size = si->signed_attrs_content_size;
        if (!xx_pki_hash_memoryv(alg, parts, 2U, digest)) return false;
    } else {
        if (!econtent) return false;
        if (!xx_pki_hash_memory(alg, econtent, econtent_size, digest)) return false;
    }

    return xx_pki_verify_digest(signer_cert->spki_der, signer_cert->spki_der_size,
                                 NULL, NULL, 0U,
                                 alg, digest, digest_size,
                                 si->signature, si->signature_size);
}
