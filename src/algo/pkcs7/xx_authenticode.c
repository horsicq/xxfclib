/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/pkcs7/xx_authenticode.h"

#include <string.h>

/* ------------------------------------------------- SpcIndirectDataContent -- */

bool xx_authenticode_parse_spc_indirect_data(const uint8_t *der, size_t size, xx_spc_indirect_data_content *out)
{
    xx_asn1_tlv seq;
    size_t used;
    xx_asn1_cursor cursor;
    xx_asn1_tlv data_tlv, digest_info_tlv;

    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!xx_asn1_parse_tlv(der, size, &seq, &used) || used != size) return false;
    if (!xx_asn1_is_universal(&seq, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !seq.constructed) return false;
    if (!xx_asn1_cursor_init(&cursor, &seq, 1U)) return false;

    /* data SpcAttributeTypeAndOptionalValue ::= SEQUENCE { type OID, value ANY OPTIONAL } */
    if (!xx_asn1_cursor_next(&cursor, &data_tlv)) return false;
    if (!xx_asn1_is_universal(&data_tlv, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !data_tlv.constructed) return false;
    {
        xx_asn1_cursor data_cursor;
        xx_asn1_tlv type_tlv;
        if (!xx_asn1_cursor_init(&data_cursor, &data_tlv, 2U)) return false;
        if (!xx_asn1_cursor_next(&data_cursor, &type_tlv)) return false;
        if (!xx_asn1_read_oid_raw(&type_tlv, &out->data_type_oid_der, &out->data_type_oid_der_size)) return false;
        if (!xx_asn1_oid_to_text(out->data_type_oid_der, out->data_type_oid_der_size, out->data_type_oid, sizeof(out->data_type_oid), NULL)) out->data_type_oid[0] = '\0';
        if (!xx_asn1_cursor_at_end(&data_cursor)) {
            xx_asn1_tlv value_tlv;
            if (!xx_asn1_cursor_next(&data_cursor, &value_tlv) || !xx_asn1_cursor_at_end(&data_cursor)) return false;
            out->data_value_der = value_tlv.header;
            out->data_value_der_size = value_tlv.total_size;
            out->has_data_value = true;
        }
    }

    /* messageDigest DigestInfo ::= SEQUENCE { digestAlgorithm AlgorithmIdentifier, digest OCTET STRING } */
    if (!xx_asn1_cursor_next(&cursor, &digest_info_tlv)) return false;
    if (!xx_asn1_is_universal(&digest_info_tlv, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !digest_info_tlv.constructed) return false;
    {
        xx_asn1_cursor di_cursor;
        xx_asn1_tlv alg_tlv, digest_tlv;
        if (!xx_asn1_cursor_init(&di_cursor, &digest_info_tlv, 2U)) return false;
        if (!xx_asn1_cursor_next(&di_cursor, &alg_tlv)) return false;
        if (!xx_x509_parse_algorithm_identifier(&alg_tlv, &out->digest_algorithm)) return false;
        if (!xx_asn1_cursor_next(&di_cursor, &digest_tlv) || !xx_asn1_cursor_at_end(&di_cursor)) return false;
        if (!xx_asn1_read_octet_string_primitive(&digest_tlv, &out->digest, &out->digest_size)) return false;
    }
    if (!xx_asn1_cursor_at_end(&cursor)) return false;
    return true;
}

/* ------------------------------------------------------------ SpcSpOpusInfo -- */

static bool parse_spc_string(const xx_asn1_tlv *tlv, xx_spc_string *out)
{
    size_t i;
    if (!tlv || tlv->constructed) return false;
    if (tlv->tag_class == XX_ASN1_CLASS_CONTEXT && tlv->tag == 0U) {
        if ((tlv->content_size & 1U) != 0U) return false;
        for (i = 0U; i < tlv->content_size; i += 2U) {
            if (tlv->content[i] >= 0xd8U && tlv->content[i] <= 0xdfU) return false;
        }
        out->kind = XX_SPC_STRING_BMP;
        out->bytes = tlv->content;
        out->size = tlv->content_size;
        return true;
    }
    if (tlv->tag_class == XX_ASN1_CLASS_CONTEXT && tlv->tag == 1U) {
        for (i = 0U; i < tlv->content_size; ++i) if (tlv->content[i] > 0x7fU) return false;
        out->kind = XX_SPC_STRING_IA5;
        out->bytes = tlv->content;
        out->size = tlv->content_size;
        return true;
    }
    return false;
}

static bool parse_spc_link(const xx_asn1_tlv *tlv, xx_spc_link *out)
{
    size_t i;
    if (!tlv) return false;
    if (tlv->tag_class == XX_ASN1_CLASS_CONTEXT && tlv->tag == 0U) {
        if (tlv->constructed) return false;
        for (i = 0U; i < tlv->content_size; ++i) if (tlv->content[i] > 0x7fU) return false;
        out->kind = XX_SPC_LINK_URL;
        out->bytes = tlv->content;
        out->size = tlv->content_size;
        return true;
    }
    if (tlv->tag_class == XX_ASN1_CLASS_CONTEXT && tlv->tag == 1U) {
        xx_asn1_cursor cursor;
        xx_asn1_tlv class_id, serialized;
        const uint8_t *bytes;
        size_t bytes_size;
        if (!xx_asn1_cursor_init(&cursor, tlv, 3U) || !xx_asn1_cursor_next(&cursor, &class_id) || !xx_asn1_read_octet_string_primitive(&class_id, &bytes, &bytes_size) || bytes_size != 16U) return false;
        if (!xx_asn1_cursor_next(&cursor, &serialized) || !xx_asn1_read_octet_string_primitive(&serialized, &bytes, &bytes_size) || !xx_asn1_cursor_at_end(&cursor)) return false;
        out->kind = XX_SPC_LINK_MONIKER;
        out->bytes = tlv->content;
        out->size = tlv->content_size;
        return true;
    }
    if (tlv->tag_class == XX_ASN1_CLASS_CONTEXT && tlv->tag == 2U) {
        xx_asn1_cursor cursor;
        xx_asn1_tlv inner;
        xx_spc_string string;
        if (!xx_asn1_cursor_init(&cursor, tlv, 3U) || !xx_asn1_cursor_next(&cursor, &inner) || !xx_asn1_cursor_at_end(&cursor) || !parse_spc_string(&inner, &string)) return false;
        out->kind = XX_SPC_LINK_FILE; /* content is the inner SpcString's own TLV (re-parse with parse_spc_string if needed) */
        out->bytes = tlv->content;
        out->size = tlv->content_size;
        return true;
    }
    return false;
}

bool xx_authenticode_parse_spc_sp_opus_info(const uint8_t *der, size_t size, xx_spc_sp_opus_info *out)
{
    xx_asn1_tlv seq;
    size_t used;
    xx_asn1_cursor cursor;

    if (!der || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!xx_asn1_parse_tlv(der, size, &seq, &used) || used != size) return false;
    if (!xx_asn1_is_universal(&seq, (uint32_t)XX_ASN1_TAG_SEQUENCE) || !seq.constructed) return false;
    if (!xx_asn1_cursor_init(&cursor, &seq, 1U)) return false;

    while (!xx_asn1_cursor_at_end(&cursor)) {
        xx_asn1_tlv field, choice_tlv;
        xx_asn1_cursor inner;
        if (!xx_asn1_cursor_next(&cursor, &field)) return false;
        if (field.tag_class != XX_ASN1_CLASS_CONTEXT || !field.constructed) return false; /* both fields are EXPLICIT */
        if (!xx_asn1_cursor_init(&inner, &field, 2U)) return false;
        if (!xx_asn1_cursor_next(&inner, &choice_tlv) || !xx_asn1_cursor_at_end(&inner)) return false;

        if (field.tag == 0U) {
            if (out->has_program_name || out->has_more_info) return false;
            if (!parse_spc_string(&choice_tlv, &out->program_name)) return false;
            out->has_program_name = true;
        } else if (field.tag == 1U) {
            if (out->has_more_info) return false;
            if (!parse_spc_link(&choice_tlv, &out->more_info)) return false;
            out->has_more_info = true;
        } else {
            return false;
        }
    }
    return true;
}

/* ----------------------------------------- nested signatures / countersignatures -- */

bool xx_authenticode_nested_signatures_begin(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor)
{
    xx_pkcs7_attribute attr;
    if (!xx_pkcs7_find_attr_unsigned(si, XX_AUTHENTICODE_OID_NESTED_SIGNATURE, &attr)) return false;
    return xx_pkcs7_attr_values_begin(&attr, cursor);
}

bool xx_authenticode_nested_signatures_next(xx_asn1_cursor *cursor, xx_pkcs7_content_info *out)
{
    xx_asn1_tlv value_tlv;
    if (!xx_asn1_cursor_next(cursor, &value_tlv)) return false;
    return xx_pkcs7_parse_content_info(value_tlv.header, value_tlv.total_size, out);
}

bool xx_authenticode_countersignatures_begin(const xx_pkcs7_signer_info *si, xx_asn1_cursor *cursor)
{
    xx_pkcs7_attribute attr;
    if (!xx_pkcs7_find_attr_unsigned(si, XX_PKCS9_OID_COUNTERSIGNATURE, &attr)) return false;
    return xx_pkcs7_attr_values_begin(&attr, cursor);
}

bool xx_authenticode_countersignatures_next(xx_asn1_cursor *cursor, xx_pkcs7_signer_info *out)
{
    xx_asn1_tlv value_tlv;
    if (!xx_asn1_cursor_next(cursor, &value_tlv)) return false;
    return xx_pkcs7_parse_signer_info(value_tlv.header, value_tlv.total_size, out);
}
