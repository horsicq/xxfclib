/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/pkcs7/xx_tsp.h"

#include <string.h>

/* Accuracy's millis [0] / micros [1] are IMPLICIT INTEGER in every real-world
 * RFC 3161 token this lane has seen (matching OpenSSL's own ASN1_IMP_OPT
 * template for TS_ACCURACY) - but to be safe against a TSA that encoded them
 * EXPLICIT instead (wrapping a nested universal INTEGER TLV), this accepts
 * either shape: primitive content is read as the INTEGER directly; a
 * constructed wrapper is unwrapped one level first. */
static bool read_optional_tagged_int(const xx_asn1_tlv *tlv, int64_t *out)
{
    if (!tlv) return false;
    if (!tlv->constructed) {
        xx_asn1_tlv fake;
        memset(&fake, 0, sizeof(fake));
        fake.tag_class = XX_ASN1_CLASS_UNIVERSAL;
        fake.tag = (uint32_t)XX_ASN1_TAG_INTEGER;
        fake.content = tlv->content;
        fake.content_size = tlv->content_size;
        return xx_asn1_read_int64(&fake, out);
    }
    {
        xx_asn1_cursor cursor;
        xx_asn1_tlv inner;
        if (!xx_asn1_cursor_init(&cursor, tlv, 3U)) return false;
        if (!xx_asn1_cursor_next(&cursor, &inner) || !xx_asn1_cursor_at_end(&cursor)) return false;
        return xx_asn1_read_int64(&inner, out);
    }
}

bool xx_tsp_parse_tst_info_prefix(const uint8_t *der, size_t size, xx_tsp_tst_info *out)
{
    xx_asn1_tlv seq, version, policy, imprint, algorithm, hashed;
    xx_asn1_cursor cursor, fields;
    size_t used;
    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));
    if (!xx_asn1_parse_tlv(der, size, &seq, &used) ||
        !xx_asn1_is_universal(&seq, XX_ASN1_TAG_SEQUENCE) ||
        !xx_asn1_cursor_init(&cursor, &seq, 1U) ||
        !xx_asn1_cursor_next(&cursor, &version) || !xx_asn1_read_int64(&version, &out->version) ||
        !xx_asn1_cursor_next(&cursor, &policy) ||
        !xx_asn1_cursor_next(&cursor, &imprint) || !xx_asn1_is_universal(&imprint, XX_ASN1_TAG_SEQUENCE) ||
        !xx_asn1_cursor_init(&fields, &imprint, 2U) ||
        !xx_asn1_cursor_next(&fields, &algorithm) ||
        !xx_x509_parse_algorithm_identifier(&algorithm, &out->message_imprint.hash_algorithm) ||
        !xx_asn1_cursor_next(&fields, &hashed)) return false;
    if (xx_asn1_is_universal(&policy, XX_ASN1_TAG_OID)) {
        out->policy_oid_der = policy.content; out->policy_oid_der_size = policy.content_size;
        if (!xx_asn1_oid_to_text(policy.content, policy.content_size, out->policy_oid, sizeof(out->policy_oid), NULL))
            memcpy(out->policy_oid, "invalid", 8U);
    }
    /* BerObjectContent::as_slice accepts primitive OCTET/BIT/string/integer. */
    if (hashed.constructed || hashed.tag_class != XX_ASN1_CLASS_UNIVERSAL) return false;
    if (hashed.tag == XX_ASN1_TAG_BIT_STRING) {
        unsigned unused;
        return xx_asn1_read_bit_string_primitive(&hashed, &unused, &out->message_imprint.hashed_message, &out->message_imprint.hashed_message_size);
    }
    if (hashed.tag != XX_ASN1_TAG_OCTET_STRING && hashed.tag != XX_ASN1_TAG_INTEGER &&
        !xx_asn1_is_string_tag(hashed.tag)) return false;
    out->message_imprint.hashed_message = hashed.content;
    out->message_imprint.hashed_message_size = hashed.content_size;
    return true;
}

bool xx_tsp_parse_tst_info(const uint8_t *der, size_t size, xx_tsp_tst_info *out)
{
    xx_asn1_tlv seq;
    size_t used;
    xx_asn1_cursor cursor;
    xx_asn1_tlv version_tlv, policy_tlv, mi_tlv, serial_tlv, gentime_tlv;

    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!xx_asn1_parse_tlv(der, size, &seq, &used) || used != size) return false;
    if (!xx_asn1_is_universal(&seq, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !seq.constructed) return false;
    if (!xx_asn1_cursor_init(&cursor, &seq, 1U)) return false;

    if (!xx_asn1_cursor_next(&cursor, &version_tlv) || !xx_asn1_read_int64(&version_tlv, &out->version)) return false;
    if (out->version != 1) return false;

    if (!xx_asn1_cursor_next(&cursor, &policy_tlv)) return false;
    if (!xx_asn1_read_oid_raw(&policy_tlv, &out->policy_oid_der, &out->policy_oid_der_size)) return false;
    if (!xx_asn1_oid_to_text(out->policy_oid_der, out->policy_oid_der_size, out->policy_oid, sizeof(out->policy_oid), NULL)) out->policy_oid[0] = '\0';

    if (!xx_asn1_cursor_next(&cursor, &mi_tlv)) return false;
    if (!xx_asn1_is_universal(&mi_tlv, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !mi_tlv.constructed) return false;
    {
        xx_asn1_cursor mi_cursor;
        xx_asn1_tlv alg_tlv, hashed_tlv;
        if (!xx_asn1_cursor_init(&mi_cursor, &mi_tlv, 2U)) return false;
        if (!xx_asn1_cursor_next(&mi_cursor, &alg_tlv)) return false;
        if (!xx_x509_parse_algorithm_identifier(&alg_tlv, &out->message_imprint.hash_algorithm)) return false;
        if (!xx_asn1_cursor_next(&mi_cursor, &hashed_tlv) || !xx_asn1_cursor_at_end(&mi_cursor)) return false;
        if (!xx_asn1_read_octet_string_primitive(&hashed_tlv, &out->message_imprint.hashed_message, &out->message_imprint.hashed_message_size)) return false;
    }

    if (!xx_asn1_cursor_next(&cursor, &serial_tlv) || !xx_asn1_read_integer(&serial_tlv, &out->serial, &out->serial_size)) return false;
    if (xx_asn1_integer_is_negative(&serial_tlv)) return false;
    if (!xx_asn1_cursor_next(&cursor, &gentime_tlv) || !xx_asn1_is_universal(&gentime_tlv, (uint32_t)XX_ASN1_TAG_GENERALIZEDTIME) || !xx_asn1_read_time(&gentime_tlv, &out->gen_time)) return false;

    /* Validate every optional field in schema order, including fields that
     * are not exposed by this API. This prevents malformed trailing bytes or
     * duplicate accuracy components from silently becoming accepted input. */
    {
        unsigned last_field = 0U;
        while (!xx_asn1_cursor_at_end(&cursor)) {
            xx_asn1_tlv next_tlv;
            unsigned field_number;
            if (!xx_asn1_cursor_next(&cursor, &next_tlv)) return false;
            if (xx_asn1_is_universal(&next_tlv, (uint32_t)XX_ASN1_TAG_SEQUENCE) && next_tlv.constructed) {
                xx_asn1_cursor acur;
                unsigned last_accuracy = 0U;
                field_number = 1U;
                if (field_number <= last_field) return false;
                out->accuracy.present = true;
                if (!xx_asn1_cursor_init(&acur, &next_tlv, 2U)) return false;
                while (!xx_asn1_cursor_at_end(&acur)) {
                    xx_asn1_tlv field;
                    if (!xx_asn1_cursor_next(&acur, &field)) return false;
                    if (xx_asn1_is_universal(&field, (uint32_t)XX_ASN1_TAG_INTEGER) && !field.constructed) {
                        if (last_accuracy >= 1U) return false;
                        if (!xx_asn1_read_int64(&field, &out->accuracy.seconds)) return false;
                        if (out->accuracy.seconds < 0) return false;
                        out->accuracy.has_seconds = true;
                        last_accuracy = 1U;
                    } else if (field.tag_class == XX_ASN1_CLASS_CONTEXT && field.tag == 0U) {
                        int64_t v;
                        if (last_accuracy >= 2U || !read_optional_tagged_int(&field, &v) || v < 1 || v > 999) return false;
                        out->accuracy.millis = (int32_t)v;
                        out->accuracy.has_millis = true;
                        last_accuracy = 2U;
                    } else if (field.tag_class == XX_ASN1_CLASS_CONTEXT && field.tag == 1U) {
                        int64_t v;
                        if (last_accuracy >= 3U || !read_optional_tagged_int(&field, &v) || v < 1 || v > 999) return false;
                        out->accuracy.micros = (int32_t)v;
                        out->accuracy.has_micros = true;
                        last_accuracy = 3U;
                    } else {
                        return false;
                    }
                }
            } else if (xx_asn1_is_universal(&next_tlv, (uint32_t)XX_ASN1_TAG_BOOLEAN)) {
                bool ordering;
                field_number = 2U;
                if (!xx_asn1_read_boolean(&next_tlv, &ordering)) return false;
            } else if (xx_asn1_is_universal(&next_tlv, (uint32_t)XX_ASN1_TAG_INTEGER)) {
                const uint8_t *nonce;
                size_t nonce_size;
                field_number = 3U;
                if (!xx_asn1_read_integer(&next_tlv, &nonce, &nonce_size) || xx_asn1_integer_is_negative(&next_tlv)) return false;
            } else if (next_tlv.tag_class == XX_ASN1_CLASS_CONTEXT && next_tlv.constructed && next_tlv.tag == 0U) {
                xx_asn1_cursor tsa_cursor;
                xx_asn1_tlv general_name;
                field_number = 4U;
                if (!xx_asn1_cursor_init(&tsa_cursor, &next_tlv, 2U) || !xx_asn1_cursor_next(&tsa_cursor, &general_name) || !xx_asn1_cursor_at_end(&tsa_cursor)) return false;
                if (general_name.tag_class != XX_ASN1_CLASS_CONTEXT || general_name.tag > 8U) return false;
            } else if (next_tlv.tag_class == XX_ASN1_CLASS_CONTEXT && next_tlv.constructed && next_tlv.tag == 1U) {
                xx_asn1_cursor extension_cursor;
                field_number = 5U;
                if (!xx_asn1_cursor_init(&extension_cursor, &next_tlv, 2U) || xx_asn1_cursor_at_end(&extension_cursor)) return false;
                while (!xx_asn1_cursor_at_end(&extension_cursor)) {
                    xx_asn1_cursor extension_fields;
                    xx_asn1_tlv extension, oid, value;
                    const uint8_t *bytes;
                    size_t bytes_size;
                    if (!xx_asn1_cursor_next(&extension_cursor, &extension) || !xx_asn1_is_universal(&extension, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !xx_asn1_cursor_init(&extension_fields, &extension, 3U)) return false;
                    if (!xx_asn1_cursor_next(&extension_fields, &oid) || !xx_asn1_read_oid_raw(&oid, &bytes, &bytes_size) || !xx_asn1_cursor_next(&extension_fields, &value)) return false;
                    if (xx_asn1_is_universal(&value, (uint32_t)XX_ASN1_TAG_BOOLEAN)) {
                        bool critical;
                        if (!xx_asn1_read_boolean(&value, &critical) || !xx_asn1_cursor_next(&extension_fields, &value)) return false;
                    }
                    if (!xx_asn1_read_octet_string_primitive(&value, &bytes, &bytes_size) || !xx_asn1_cursor_at_end(&extension_fields)) return false;
                }
            } else {
                return false;
            }
            if (field_number <= last_field) return false;
            last_field = field_number;
        }
    }
    return true;
}

bool xx_tsp_parse_timestamp_token_into(const uint8_t *der, size_t size, uint8_t *flat, size_t flat_capacity, size_t *flat_size, xx_tsp_timestamp_token *out)
{
    if (!der || !out || !flat_size) return false;
    memset(out, 0, sizeof(*out));
    *flat_size = 0U;

    if (!xx_pkcs7_parse_signed_data_content_info(der, size, &out->content_info, &out->signed_data)) return false;
    if (strcmp(out->signed_data.content_info.content_type_oid, XX_TSP_OID_TST_INFO) != 0) return false;
    if (!out->signed_data.content_info.has_content) return false;

    if (out->signed_data.content_info.content_is_octet_string) {
        if (out->signed_data.content_info.octet_string_content) {
            return xx_tsp_parse_tst_info(out->signed_data.content_info.octet_string_content, out->signed_data.content_info.octet_string_content_size, &out->tst_info);
        }
        {
            /* A constructed (BER-chunked) OCTET STRING wrapper: flatten it first. */
            xx_asn1_tlv content_tlv;
            size_t used;
            if (!xx_asn1_parse_tlv(out->signed_data.content_info.content_tlv, out->signed_data.content_info.content_tlv_size, &content_tlv, &used)) return false;
            if (!flat || !xx_asn1_read_octet_string_into(&content_tlv, 1U, flat, flat_capacity, flat_size)) return false;
            return xx_tsp_parse_tst_info(flat, *flat_size, &out->tst_info);
        }
    }
    /* Tolerate eContent encoded directly as the TSTInfo SEQUENCE, with no octet-string wrapper. */
    return xx_tsp_parse_tst_info(out->signed_data.content_info.content_tlv, out->signed_data.content_info.content_tlv_size, &out->tst_info);
}

bool xx_tsp_parse_timestamp_token(const uint8_t *der, size_t size, xx_tsp_timestamp_token *out)
{
    size_t flat_size;
    return xx_tsp_parse_timestamp_token_into(der, size, NULL, 0U, &flat_size, out);
}

bool xx_tsp_timestamp_tokens_begin(const xx_pkcs7_signer_info *si, const char *attribute_oid, xx_asn1_cursor *cursor)
{
    xx_pkcs7_attribute attr;
    if (!attribute_oid || (strcmp(attribute_oid, XX_PKCS9_OID_TIMESTAMP_TOKEN) != 0 && strcmp(attribute_oid, "1.3.6.1.4.1.311.3.3.1") != 0)) return false;
    if (!xx_pkcs7_find_attr_unsigned(si, attribute_oid, &attr)) return false;
    return xx_pkcs7_attr_values_begin(&attr, cursor);
}

bool xx_tsp_timestamp_tokens_next(xx_asn1_cursor *cursor, xx_tsp_timestamp_token *out)
{
    xx_asn1_tlv token;
    if (!xx_asn1_cursor_next(cursor, &token)) return false;
    return xx_tsp_parse_timestamp_token(token.header, token.total_size, out);
}

bool xx_tsp_check_message_imprint(const xx_tsp_tst_info *info, const uint8_t *data, size_t size)
{
    uint8_t digest[64];
    xx_pki_hash hash;
    size_t digest_size;
    if (!info || (!data && size != 0U) || !info->message_imprint.hashed_message) return false;
    hash = xx_pki_hash_from_oid(info->message_imprint.hash_algorithm.oid_text);
    digest_size = xx_pki_hash_size(hash);
    if (digest_size == 0U || digest_size > sizeof(digest) || digest_size != info->message_imprint.hashed_message_size) return false;
    return xx_pki_hash_memory(hash, data, size, digest) && memcmp(digest, info->message_imprint.hashed_message, digest_size) == 0;
}
