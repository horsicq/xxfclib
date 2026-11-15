/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/x509/xx_x509.h"

#include <string.h>
#include <stdio.h>

#define XX_X509_OID_RSASSA_PSS "1.2.840.113549.1.1.10"

/* Check only headers actually decoded by the X.509 schema. AttributeValue
 * and AlgorithmIdentifier parameters are ANY: their content stays opaque,
 * even when their constructed bit is set (x509-parser behaves the same). */
static bool x509_cursor_next(xx_asn1_cursor *cursor, xx_asn1_tlv *out)
{
    size_t used;
    bool nonminimal = false;
    if (!cursor || !out || xx_asn1_cursor_at_end(cursor) ||
        !xx_asn1_parse_any_tlv_ex(cursor->pos, (size_t)(cursor->limit-cursor->pos), cursor->depth, out, &used, &nonminimal) ||
        nonminimal || out->indefinite) return false;
    cursor->pos += used;
    return true;
}

/* x509-parser's explicit Extensions parser uses the wrapper header but
 * consumes the inner SEQUENCE from the parent span, ignoring wrapper length. */
static bool x509_extensions_wrapper(xx_asn1_cursor *cursor, xx_asn1_tlv *out)
{
    const uint8_t *p = cursor->pos;
    size_t available = (size_t)(cursor->limit-p), head = 2U, declared, count, i, used;
    xx_asn1_tlv sequence;
    bool nonminimal = false;
    if (available < 2U || p[0] != 0xa3U) return false;
    declared = p[1];
    if (declared & 0x80U) {
        count = declared & 0x7fU;
        if (!count || count > sizeof(size_t) || count > available-2U || !p[2]) return false;
        declared = 0U;
        for (i = 0U; i < count; ++i) {
            if (declared > (SIZE_MAX >> 8U)) return false;
            declared = (declared << 8U) | p[2U+i];
        }
        if (declared < 128U) return false;
        head += count;
    }
    if (!xx_asn1_parse_any_tlv_ex(p+head, available-head, cursor->depth+1U,
                                  &sequence, &used, &nonminimal) || nonminimal || sequence.indefinite) return false;
    memset(out, 0, sizeof(*out));
    out->header = p; out->header_size = head; out->content = p+head;
    out->content_size = used; out->total_size = head+used;
    out->tag_class = XX_ASN1_CLASS_CONTEXT; out->constructed = true; out->tag = 3U;
    cursor->pos += head+used;
    return true;
}

/* Match const_oid's observed YARA-X 1.23.0 rendering: first subidentifier
 * is one byte <=119, encoded size <=39, later arcs wrap at u32. Nonminimal
 * base-128 arcs are canonicalized. Official Windows PE probes pin this. */
static bool algorithm_oid_text(const uint8_t *der, size_t size, char *out, size_t capacity)
{
    size_t i, written;
    int n;
    if (!der || !size || size > 39U || der[0] > 119U || !out || !capacity) return false;
    n = snprintf(out, capacity, "%u.%u", (unsigned)(der[0]/40U), (unsigned)(der[0]%40U));
    if (n < 0 || (size_t)n >= capacity) return false;
    written = (size_t)n;
    i = 1U;
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

/* AlgorithmIdentifier ::= SEQUENCE { algorithm OBJECT IDENTIFIER, parameters ANY DEFINED BY algorithm OPTIONAL } */
bool xx_x509_parse_algorithm_identifier(const xx_asn1_tlv *seq, xx_x509_algorithm_id *out)
{
    xx_asn1_cursor cursor;
    xx_asn1_tlv oid_tlv, params_tlv;

    if (!seq || !out || !xx_asn1_is_universal(seq, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !seq->constructed) return false;
    if (!xx_asn1_cursor_init(&cursor, seq, 1U)) return false;
    if (!xx_asn1_cursor_next(&cursor, &oid_tlv)) return false;
    if (!xx_asn1_read_oid_raw(&oid_tlv, &out->oid_der, &out->oid_der_size)) return false;
    if (!algorithm_oid_text(out->oid_der, out->oid_der_size, out->oid_text, sizeof(out->oid_text))) memcpy(out->oid_text, "invalid", 8U);

    if (xx_asn1_cursor_at_end(&cursor)) {
        out->params_der = NULL;
        out->params_der_size = 0U;
        return true;
    }
    if (!xx_asn1_cursor_next(&cursor, &params_tlv)) return false;
    if (!xx_asn1_cursor_at_end(&cursor)) return false; /* at most one parameters element */
    out->params_der = params_tlv.header;
    out->params_der_size = params_tlv.total_size;
    return true;
}

static bool x509_parse_algorithm_identifier(const xx_asn1_tlv *seq, xx_x509_algorithm_id *out)
{
    xx_asn1_cursor fields;
    xx_asn1_tlv oid, parameters;
    if (!seq || !out || !xx_asn1_is_universal(seq, XX_ASN1_TAG_SEQUENCE) ||
        !xx_asn1_cursor_init(&fields, seq, 1U) || !x509_cursor_next(&fields, &oid) ||
        oid.constructed || (oid.header[0] & 0x1fU) == 0x1fU) return false;
    /* x509-parser treats primitive short-tag ANY content as the OID. */
    out->oid_der = oid.content; out->oid_der_size = oid.content_size;
    if (!algorithm_oid_text(out->oid_der, out->oid_der_size, out->oid_text, sizeof(out->oid_text))) memcpy(out->oid_text, "invalid", 8U);
    out->params_der = NULL; out->params_der_size = 0U;
    if (!xx_asn1_cursor_at_end(&fields)) {
        if (!x509_cursor_next(&fields, &parameters) || !xx_asn1_cursor_at_end(&fields)) return false;
        out->params_der = parameters.header; out->params_der_size = parameters.total_size;
    }
    return true;
}

static bool require_sequence(const xx_asn1_tlv *tlv)
{
    return tlv != NULL && xx_asn1_is_universal(tlv, (uint32_t)XX_ASN1_TAG_SEQUENCE) && tlv->constructed;
}

static bool algorithms_equal(const xx_x509_algorithm_id *a, const xx_x509_algorithm_id *b)
{
    return strcmp(a->oid_text, b->oid_text) == 0 && a->params_der_size == b->params_der_size &&
           (a->params_der_size == 0U || (a->params_der && b->params_der &&
            memcmp(a->params_der, b->params_der, a->params_der_size) == 0));
}

bool xx_x509_parse_certificate(const uint8_t *der, size_t size, xx_x509_certificate *out)
{
    xx_asn1_tlv cert_seq, tbs_seq, sig_alg_seq, sig_tlv;
    xx_asn1_cursor outer_cursor, tbs_cursor;
    size_t used;
    bool nonminimal = false;

    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!xx_asn1_parse_tlv_ex(der, size, 0U, &cert_seq, &used, &nonminimal) || used != size || nonminimal || cert_seq.indefinite) return false;
    if (!require_sequence(&cert_seq)) return false;
    out->der = cert_seq.header;
    out->der_size = cert_seq.total_size;

    if (!xx_asn1_cursor_init(&outer_cursor, &cert_seq, 1U)) return false;

    if (!x509_cursor_next(&outer_cursor, &tbs_seq) || !require_sequence(&tbs_seq)) return false;
    out->tbs_der = tbs_seq.header;
    out->tbs_der_size = tbs_seq.total_size;

    if (!x509_cursor_next(&outer_cursor, &sig_alg_seq)) return false;
    if (!x509_parse_algorithm_identifier(&sig_alg_seq, &out->signature_alg)) return false;

    if (!x509_cursor_next(&outer_cursor, &sig_tlv)) return false;
    {
        unsigned unused_bits = 0U;
        if (!xx_asn1_read_bit_string_primitive(&sig_tlv, &unused_bits, &out->signature, &out->signature_size)) return false;
        out->signature_unused_bits = unused_bits;
        if (unused_bits && (out->signature[out->signature_size-1U] & ((1U << unused_bits)-1U))) return false;
    }
    /* x509-parser ignores remaining bytes inside the Certificate wrapper. */

    /* --- TBSCertificate --- */
    if (!xx_asn1_cursor_init(&tbs_cursor, &tbs_seq, 1U)) return false;
    {
        xx_asn1_tlv field;
        if (!x509_cursor_next(&tbs_cursor, &field)) return false;
        if (field.tag_class == XX_ASN1_CLASS_CONTEXT && field.tag == 0U && field.constructed) {
            /* [0] EXPLICIT Version DEFAULT v1 */
            xx_asn1_cursor vcur;
            xx_asn1_tlv vint;
            int64_t v;
            if (!xx_asn1_cursor_init(&vcur, &field, 2U)) return false;
            if (!x509_cursor_next(&vcur, &vint) || !xx_asn1_cursor_at_end(&vcur)) return false;
            if (!xx_asn1_read_int64(&vint, &v) || v < 0 || v > 2) return false;
            out->version = v;
            if (!x509_cursor_next(&tbs_cursor, &field)) return false; /* now serialNumber */
        } else {
            out->version = 0; /* DEFAULT */
        }
        if (!xx_asn1_read_integer(&field, &out->serial, &out->serial_size)) return false;
    }
    {
        xx_asn1_tlv sig_alg_in_tbs;
        if (!x509_cursor_next(&tbs_cursor, &sig_alg_in_tbs)) return false;
        if (!x509_parse_algorithm_identifier(&sig_alg_in_tbs, &out->tbs_signature_alg)) return false;
    }
    out->signature_algorithms_match = algorithms_equal(&out->tbs_signature_alg, &out->signature_alg);
    {
        xx_asn1_tlv issuer_tlv;
        if (!x509_cursor_next(&tbs_cursor, &issuer_tlv) || issuer_tlv.tag != XX_ASN1_TAG_SEQUENCE) return false;
        out->issuer.der = issuer_tlv.header;
        out->issuer.der_size = issuer_tlv.total_size;
    }
    {
        xx_asn1_tlv validity_seq, nb, na;
        xx_asn1_cursor vcur;
        if (!x509_cursor_next(&tbs_cursor, &validity_seq) || !require_sequence(&validity_seq)) return false;
        if (!xx_asn1_cursor_init(&vcur, &validity_seq, 2U)) return false;
        if (!x509_cursor_next(&vcur, &nb) || !xx_asn1_read_time(&nb, &out->validity.not_before)) return false;
        if (!x509_cursor_next(&vcur, &na) || !xx_asn1_read_time(&na, &out->validity.not_after)) return false;
        if (!xx_asn1_cursor_at_end(&vcur)) return false;
    }
    {
        xx_asn1_tlv subject_tlv;
        if (!x509_cursor_next(&tbs_cursor, &subject_tlv) || subject_tlv.tag != XX_ASN1_TAG_SEQUENCE) return false;
        out->subject.der = subject_tlv.header;
        out->subject.der_size = subject_tlv.total_size;
    }
    {
        xx_asn1_tlv spki_tlv, key_alg, bits;
        xx_asn1_cursor spki_cursor;
        xx_x509_algorithm_id key_alg_id;
        const uint8_t *key_bytes;
        size_t key_size;
        unsigned unused_bits;
        if (!x509_cursor_next(&tbs_cursor, &spki_tlv) || !require_sequence(&spki_tlv)) return false;
        if (!xx_asn1_cursor_init(&spki_cursor, &spki_tlv, 2U) ||
            !x509_cursor_next(&spki_cursor, &key_alg) ||
            !x509_parse_algorithm_identifier(&key_alg, &key_alg_id) ||
            !x509_cursor_next(&spki_cursor, &bits) ||
            !xx_asn1_read_bit_string_primitive(&bits, &unused_bits, &key_bytes, &key_size)) return false; /* x509-parser retains short keys and ignores SPKI remainder. */
        if (unused_bits && (key_bytes[key_size-1U] & ((1U << unused_bits)-1U))) return false;
        out->spki_der = spki_tlv.header;
        out->spki_der_size = spki_tlv.total_size;
    }

    /* Optional (v2/v3): issuerUniqueID [1], subjectUniqueID [2], extensions [3] EXPLICIT. */
    {
    uint32_t previous_tag = 0U;
    while (!xx_asn1_cursor_at_end(&tbs_cursor)) {
        xx_asn1_tlv opt;
        if (tbs_cursor.pos[0] == 0xa3U) {
            if (!x509_extensions_wrapper(&tbs_cursor, &opt)) return false;
        } else if (!x509_cursor_next(&tbs_cursor, &opt)) return false;
        if (opt.tag_class != XX_ASN1_CLASS_CONTEXT || opt.tag < 1U || opt.tag > 3U || opt.tag <= previous_tag) return false;
        previous_tag = opt.tag;
        if (opt.tag == 3U) {
            xx_asn1_cursor ecur;
            xx_asn1_tlv ext_seq;
            if (!opt.constructed || out->version != 2) return false;
            if (!xx_asn1_cursor_init(&ecur, &opt, 2U)) return false;
            if (!x509_cursor_next(&ecur, &ext_seq) || !xx_asn1_cursor_at_end(&ecur)) return false;
            if (!require_sequence(&ext_seq) || ext_seq.content_size == 0U) return false;
            out->extensions_der = ext_seq.content;
            out->extensions_der_size = ext_seq.content_size;
            out->has_extensions = true;
            {
                xx_asn1_cursor extensions;
                if (!xx_x509_extensions_begin(out, &extensions)) return false;
                while (!xx_asn1_cursor_at_end(&extensions)) {
                    xx_x509_extension extension;
                    if (!xx_x509_extensions_next(&extensions, &extension)) return false;
                }
            }
        } else {
            xx_asn1_tlv bits = opt;
            const uint8_t *bytes;
            size_t byte_count;
            unsigned unused_bits;
            if (out->version == 0 || opt.constructed) return false;
            bits.tag_class = XX_ASN1_CLASS_UNIVERSAL;
            bits.tag = XX_ASN1_TAG_BIT_STRING;
            if (!xx_asn1_read_bit_string_primitive(&bits, &unused_bits, &bytes, &byte_count)) return false;
        }
        /* Unique IDs are validated but are not surfaced by this view. */
    }
    }
    return true;
}

/* ----------------------------------------------------------------------- */
/* Names                                                                    */
/* ----------------------------------------------------------------------- */

bool xx_x509_name_rdns_begin(const xx_x509_name *name, xx_asn1_cursor *cursor)
{
    xx_asn1_tlv tlv;
    size_t used;
    if (!name || !cursor) return false;
    if (!xx_asn1_parse_tlv(name->der, name->der_size, &tlv, &used) || used != name->der_size) return false;
    if (tlv.tag != XX_ASN1_TAG_SEQUENCE) return false;
    return xx_asn1_cursor_init_span(cursor, tlv.content, tlv.content_size, 1U);
}

bool xx_x509_name_rdns_next(xx_asn1_cursor *cursor, xx_asn1_tlv *out_rdn_set)
{
    if (!x509_cursor_next(cursor, out_rdn_set)) return false;
    return out_rdn_set->tag == XX_ASN1_TAG_SET;
}

bool xx_x509_rdn_attrs_begin(const xx_asn1_tlv *rdn_set, xx_asn1_cursor *cursor)
{
    if (!rdn_set || rdn_set->tag != XX_ASN1_TAG_SET || rdn_set->content_size == 0U) return false;
    return xx_asn1_cursor_init_span(cursor, rdn_set->content, rdn_set->content_size, 2U);
}

bool xx_x509_rdn_attrs_next(xx_asn1_cursor *cursor, xx_x509_rdn_attr *out)
{
    xx_asn1_tlv attr_seq, oid_tlv, value_tlv;
    xx_asn1_cursor attr_cursor;

    if (!out || !x509_cursor_next(cursor, &attr_seq)) return false;
    if (attr_seq.tag != XX_ASN1_TAG_SEQUENCE) return false;
    if (!xx_asn1_cursor_init_span(&attr_cursor, attr_seq.content, attr_seq.content_size, 3U)) return false;

    if (!x509_cursor_next(&attr_cursor, &oid_tlv)) return false;
    if (oid_tlv.constructed) return false;
    out->oid_der = oid_tlv.content;
    out->oid_der_size = oid_tlv.content_size;
    if (!xx_asn1_oid_to_text(out->oid_der, out->oid_der_size, out->oid_text, sizeof(out->oid_text), NULL)) memcpy(out->oid_text, "invalid", 8U);

    if (!x509_cursor_next(&attr_cursor, &value_tlv)) return false;
    if (!xx_asn1_cursor_at_end(&attr_cursor)) return false; /* exactly one value per attribute, as X.509 Name uses it */

    out->value_tag = value_tlv.tag;
    out->value_class = value_tlv.tag_class;
    out->value_constructed = value_tlv.constructed;
    out->value = value_tlv.content;
    out->value_size = value_tlv.content_size;
    return true;
}

bool xx_x509_name_find_attribute(const xx_x509_name *name, const char *oid_text, xx_x509_rdn_attr *out)
{
    xx_asn1_cursor rdn_cursor;
    if (!name || !oid_text || !out) return false;
    if (!xx_x509_name_rdns_begin(name, &rdn_cursor)) return false;
    while (!xx_asn1_cursor_at_end(&rdn_cursor)) {
        xx_asn1_tlv rdn;
        xx_asn1_cursor attr_cursor;
        if (!xx_x509_name_rdns_next(&rdn_cursor, &rdn)) return false;
        if (!xx_x509_rdn_attrs_begin(&rdn, &attr_cursor)) return false;
        while (!xx_asn1_cursor_at_end(&attr_cursor)) {
            xx_x509_rdn_attr attr;
            if (!xx_x509_rdn_attrs_next(&attr_cursor, &attr)) return false;
            if (strcmp(attr.oid_text, oid_text) == 0) {
                *out = attr;
                return true;
            }
        }
    }
    return false;
}

/* ----------------------------------------------------------------------- */
/* Extensions                                                               */
/* ----------------------------------------------------------------------- */

bool xx_x509_extensions_begin(const xx_x509_certificate *cert, xx_asn1_cursor *cursor)
{
    if (!cert || !cert->has_extensions) return false;
    return xx_asn1_cursor_init_span(cursor, cert->extensions_der, cert->extensions_der_size, 1U);
}

bool xx_x509_extensions_next(xx_asn1_cursor *cursor, xx_x509_extension *out)
{
    xx_asn1_tlv ext_seq, oid_tlv, next_tlv;
    xx_asn1_cursor ecur;
    bool critical = false;

    if (!out || !x509_cursor_next(cursor, &ext_seq)) return false;
    if (!require_sequence(&ext_seq)) return false;
    if (!xx_asn1_cursor_init(&ecur, &ext_seq, 2U)) return false;

    if (!x509_cursor_next(&ecur, &oid_tlv)) return false;
    if (oid_tlv.constructed || (oid_tlv.header[0] & 0x1fU) == 0x1fU) return false;
    out->oid_der = oid_tlv.content; out->oid_der_size = oid_tlv.content_size;
    if (!xx_asn1_oid_to_text(out->oid_der, out->oid_der_size, out->oid_text, sizeof(out->oid_text), NULL)) memcpy(out->oid_text, "invalid", 8U);

    if (!x509_cursor_next(&ecur, &next_tlv)) return false;
    if (xx_asn1_is_universal(&next_tlv, (uint32_t)XX_ASN1_TAG_BOOLEAN)) {
        if (!xx_asn1_read_boolean(&next_tlv, &critical)) return false;
        if (!x509_cursor_next(&ecur, &next_tlv)) return false;
    }
    out->critical = critical;
    if (!xx_asn1_read_octet_string_primitive(&next_tlv, &out->value, &out->value_size)) return false;
    if (!xx_asn1_cursor_at_end(&ecur)) return false;
    return true;
}

bool xx_x509_find_extension(const xx_x509_certificate *cert, const char *oid_text, xx_x509_extension *out)
{
    xx_asn1_cursor cursor;
    if (!oid_text || !out) return false;
    if (!xx_x509_extensions_begin(cert, &cursor)) return false;
    while (!xx_asn1_cursor_at_end(&cursor)) {
        xx_x509_extension ext;
        if (!xx_x509_extensions_next(&cursor, &ext)) return false;
        if (strcmp(ext.oid_text, oid_text) == 0) {
            *out = ext;
            return true;
        }
    }
    return false;
}

/* ----------------------------------------------------------------------- */
/* Hashing / verification                                                   */
/* ----------------------------------------------------------------------- */

bool xx_x509_thumbprint_sha1(const xx_x509_certificate *cert, uint8_t out_digest[20])
{
    if (!cert || !cert->der || !out_digest) return false;
    return xx_pki_hash_memory(XX_PKI_HASH_SHA1, cert->der, cert->der_size, out_digest);
}

static xx_pki_hash pss_hash_identifier(const xx_asn1_tlv *seq)
{
    xx_x509_algorithm_id id;
    xx_pki_hash hash;
    if (!xx_x509_parse_algorithm_identifier(seq, &id)) return XX_PKI_HASH_NONE;
    if (id.params_der_size != 0U) {
        xx_asn1_tlv null_tlv;
        size_t used;
        if (!xx_asn1_parse_tlv(id.params_der, id.params_der_size, &null_tlv, &used) ||
            used != id.params_der_size || !xx_asn1_read_null(&null_tlv)) return XX_PKI_HASH_NONE;
    }
    /* hashAlgorithm names a digest, never a signature algorithm. */
    if (strcmp(id.oid_text, "1.3.14.3.2.26") != 0 &&
        strcmp(id.oid_text, "2.16.840.1.101.3.4.2.4") != 0 &&
        strcmp(id.oid_text, "2.16.840.1.101.3.4.2.1") != 0 &&
        strcmp(id.oid_text, "2.16.840.1.101.3.4.2.2") != 0 &&
        strcmp(id.oid_text, "2.16.840.1.101.3.4.2.3") != 0) return XX_PKI_HASH_NONE;
    hash = xx_pki_hash_from_oid(id.oid_text);
    return hash;
}

xx_pki_hash xx_x509_signature_hash(const xx_x509_algorithm_id *alg)
{
    if (!alg) return XX_PKI_HASH_NONE;
    if (strcmp(alg->oid_text, XX_X509_OID_RSASSA_PSS) != 0) return xx_pki_hash_from_oid(alg->oid_text);

    /* RSASSA-PSS-params ::= SEQUENCE { hashAlgorithm [0] EXPLICIT AlgorithmIdentifier DEFAULT sha1, ... } (RFC 4055). */
    if (alg->params_der_size == 0U) return XX_PKI_HASH_SHA1;
    if (!alg->params_der) return XX_PKI_HASH_NONE;
    {
        xx_asn1_tlv params_tlv;
        size_t used;
        xx_asn1_cursor cursor;
        xx_pki_hash result = XX_PKI_HASH_SHA1;
        uint32_t previous_tag = 0U;
        bool first = true;
        if (!xx_asn1_parse_tlv(alg->params_der, alg->params_der_size, &params_tlv, &used) || used != alg->params_der_size) return XX_PKI_HASH_NONE;
        if (!require_sequence(&params_tlv)) return XX_PKI_HASH_NONE;
        if (!xx_asn1_cursor_init(&cursor, &params_tlv, 1U)) return XX_PKI_HASH_NONE;
        while (!xx_asn1_cursor_at_end(&cursor)) {
            xx_asn1_tlv field;
            xx_asn1_tlv value;
            xx_asn1_cursor explicit_cursor;
            if (!x509_cursor_next(&cursor, &field)) return XX_PKI_HASH_NONE;
            if (field.tag_class != XX_ASN1_CLASS_CONTEXT || !field.constructed || field.tag > 3U ||
                (!first && field.tag <= previous_tag)) return XX_PKI_HASH_NONE;
            first = false;
            previous_tag = field.tag;
            if (!xx_asn1_cursor_init(&explicit_cursor, &field, 2U) ||
                !x509_cursor_next(&explicit_cursor, &value) ||
                !xx_asn1_cursor_at_end(&explicit_cursor)) return XX_PKI_HASH_NONE;
            if (field.tag == 0U) {
                result = pss_hash_identifier(&value);
                if (result == XX_PKI_HASH_NONE) return result;
            } else if (field.tag == 1U) {
                xx_x509_algorithm_id mgf;
                xx_asn1_tlv hash_id;
                if (!xx_x509_parse_algorithm_identifier(&value, &mgf) ||
                    strcmp(mgf.oid_text, "1.2.840.113549.1.1.8") != 0 ||
                    !xx_asn1_parse_tlv(mgf.params_der, mgf.params_der_size, &hash_id, &used) ||
                    used != mgf.params_der_size || pss_hash_identifier(&hash_id) == XX_PKI_HASH_NONE) return XX_PKI_HASH_NONE;
            } else {
                int64_t n;
                if (!xx_asn1_read_int64(&value, &n) || n < 0 || (field.tag == 3U && n != 1)) return XX_PKI_HASH_NONE;
            }
        }
        return result;
    }
}

bool xx_x509_verify_signed_by(const xx_x509_certificate *cert, const xx_x509_certificate *issuer)
{
    xx_pki_hash alg;
    uint8_t digest[64]; /* SHA-512 is the largest digest xx_pki_hash can name */
    size_t digest_size;

    if (!cert || !issuer || !cert->tbs_der || !cert->tbs_der_size || !cert->signature ||
        !cert->signature_size || !issuer->spki_der || !issuer->spki_der_size) return false;
    if (cert->signature_alg.oid_der_size) {
        char canonical[96]; bool nonminimal = false;
        if (!xx_asn1_oid_to_text(cert->signature_alg.oid_der, cert->signature_alg.oid_der_size,
                                 canonical, sizeof(canonical), &nonminimal) || nonminimal ||
            strcmp(canonical, cert->signature_alg.oid_text)) return false;
    }
    /* PublicKey::verify in YARA-X 1.23.0. Signature params/family are ignored. */
    {
        static const char *const accepted[] = {
            "1.2.840.113549.2.2", "1.2.840.113549.1.1.2",
            "1.2.840.113549.2.5", "1.2.840.113549.1.1.4",
            "1.3.14.3.2.26", "1.2.840.10040.4.3", "1.2.840.113549.1.1.5", "1.3.14.3.2.29",
            "2.16.840.1.101.3.4.2.1", "1.2.840.113549.1.1.11", "2.16.840.1.101.3.4.3.2", "1.2.840.10045.4.3.2",
            "2.16.840.1.101.3.4.2.2", "1.2.840.113549.1.1.12", "1.2.840.10045.4.3.3",
            "2.16.840.1.101.3.4.2.3", "1.2.840.113549.1.1.13", "1.2.840.10045.4.3.4"
        };
        size_t i;
        for (i = 0U; i < sizeof(accepted)/sizeof(accepted[0]); ++i)
            if (!strcmp(cert->signature_alg.oid_text, accepted[i])) break;
        if (i == sizeof(accepted)/sizeof(accepted[0])) return false;
    }
    alg = xx_pki_hash_from_oid(cert->signature_alg.oid_text);
    if (alg == XX_PKI_HASH_NONE) return false;
    digest_size = xx_pki_hash_size(alg);
    if (digest_size == 0U || digest_size > sizeof(digest)) return false;
    if (!xx_pki_hash_memory(alg, cert->tbs_der, cert->tbs_der_size, digest)) return false;

    return xx_pki_verify_digest(issuer->spki_der, issuer->spki_der_size,
                                 NULL, NULL, 0U,
                                 alg, digest, digest_size,
                                 cert->signature, cert->signature_size);
}

bool xx_x509_verify(const xx_x509_certificate *cert, const xx_x509_certificate *issuer)
{
    return xx_x509_verify_signed_by(cert, issuer);
}

bool xx_x509_serial_equal(const uint8_t *a, size_t as, const uint8_t *b, size_t bs)
{
    if ((!a && as) || (!b && bs)) return false;
    while (as && !*a) { ++a; --as; }
    while (bs && !*b) { ++b; --bs; }
    return as == bs && (!as || !memcmp(a, b, as));
}

bool xx_x509_name_equal(const xx_x509_name *a, const xx_x509_name *b)
{
    return a && b && a->der_size == b->der_size &&
        (!a->der_size || (a->der && b->der && !memcmp(a->der, b->der, a->der_size)));
}

bool xx_x509_build_chain(const xx_x509_certificate *certs, size_t count, size_t start,
                         size_t *indices, size_t capacity, size_t *out_size)
{
    size_t n = 0U, next = start;
    if (!out_size || (!certs && count) || (!indices && capacity) || (start != SIZE_MAX && start >= count)) return false;
    while (next != SIZE_MAX) {
        size_t i, j, current = next;
        if (n >= capacity || !indices) return false;
        indices[n++] = current;
        if (xx_x509_name_equal(&certs[current].subject, &certs[current].issuer)) break;
        for (i = 0U; i < count; ++i)
            if (xx_x509_name_equal(&certs[i].subject, &certs[current].issuer)) break;
        if (i == count) break;
        /* Upstream inserts only selected parent subjects, not the start. */
        for (j = 1U; j < n; ++j)
            if (xx_x509_name_equal(&certs[indices[j]].subject, &certs[i].subject)) break;
        if (j < n) break;
        next = i;
    }
    *out_size = n;
    return true;
}

bool xx_x509_verify_chain(const xx_x509_certificate *certs, size_t count,
                          const size_t *indices, size_t chain_size)
{
    size_t i;
    if ((!certs && count) || (!indices && chain_size)) return false;
    for (i = 0U; i < chain_size; ++i) if (indices[i] >= count) return false;
    for (i = 1U; i < chain_size; ++i)
        if (!xx_x509_verify_signed_by(&certs[indices[i-1U]], &certs[indices[i]])) return false;
    return true;
}
