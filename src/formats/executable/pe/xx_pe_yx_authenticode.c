/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
/* Implements the signature half of include/xxfclib/formats/executable/pe/
 * xx_pe_yx.h: portable Authenticode policy and rendering over xxfclib's PKI
 * layer, ported from xxyara's pe_portable_authenticode.inc. The policy
 * follows YARA-X 1.23 authenticode.rs and asn1.rs, including serial-only
 * signer selection, prefix certificate sets, and untrusted terminal
 * certificates. No operating-system trust store is consulted. The PE-format
 * logic (security directory location, WIN_CERTIFICATE envelope iteration and
 * the Authenticode file-digest byte ranges) lives here as well; the crypto
 * primitives come from xxfclib's pki/pkcs7 layer. */

#include "xxfclib/formats/executable/pe/xx_pe_yx.h"

#include "xxfclib/algo/pkcs7/xx_authenticode.h"
#include "xxfclib/algo/pkcs7/xx_tsp.h"

#include "xx_pe_yx_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* After <string.h>: the OID lookup calls strcmp. */
#include "xx_pe_yx_oid_names.inc"


typedef struct {
    uint8_t tag;
    const uint8_t *encoded, *value;
    size_t length, total;
} xx_pe_yx_auth_der;

static bool xx_pe_yx_auth_read(const uint8_t *data, size_t size, xx_pe_yx_auth_der *value)
{
    xx_asn1_tlv tlv;
    size_t consumed;
    if (!value || !xx_asn1_parse_any_tlv(data, size, &tlv, &consumed) || tlv.tag > 30) return false;
    value->tag = (uint8_t)(((unsigned)tlv.tag_class << 6) | (tlv.constructed ? 0x20 : 0) | tlv.tag);
    value->encoded = tlv.header; value->value = tlv.content;
    value->length = tlv.content_size; value->total = tlv.total_size;
    return true;
}

static bool xx_pe_yx_auth_take(const uint8_t **data, size_t *size, xx_pe_yx_auth_der *value)
{
    if (!xx_pe_yx_auth_read(*data, *size, value)) return false;
    *data += value->total; *size -= value->total;
    return true;
}

static char *xx_pe_yx_auth_text(const char *text, size_t length)
{
    char *copy;
    if (!text || length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy) { memcpy(copy, text, length); copy[length] = 0; }
    return copy;
}

static char *xx_pe_yx_auth_hex(const uint8_t *bytes, size_t length, bool colons)
{
    static const char alphabet[] = "0123456789abcdef";
    size_t used = 0, size;
    char *text;
    if (length > (SIZE_MAX - 1) / 3) return NULL;
    size = length * 2 + (colons && length ? length - 1 : 0);
    text = (char *)malloc(size + 1);
    if (!text) return NULL;
    for (size_t i = 0; i < length; ++i) {
        if (i && colons) text[used++] = ':';
        text[used++] = alphabet[bytes[i] >> 4];
        text[used++] = alphabet[bytes[i] & 15];
    }
    text[used] = 0;
    return text;
}

static char *xx_pe_yx_auth_oid(const xx_pe_yx_auth_der *oid)
{
    char output[256];
    size_t used = 0;
    uint32_t arc = 0;
    bool first = true;
    if (!oid) return NULL;
    if (!oid->length || oid->length > 39 || oid->value[0] > 119)
        return xx_pe_yx_auth_text("invalid", 7);
    for (size_t i = 0; i < oid->length; ++i) {
        int written;
        /* const_oid 0.9's decoder wraps overflowing u32 arcs and accepts
         * nonminimal base-128 arcs. Confirmed with the pinned yr.exe. */
        arc = (uint32_t)((arc << 7) | (oid->value[i] & 0x7f));
        if (oid->value[i] & 0x80) continue;
        if (first) {
            unsigned leading = arc < 40 ? 0 : arc < 80 ? 1 : 2;
            written = snprintf(output + used, sizeof(output) - used,
                "%u.%llu", leading, (unsigned long long)(arc - leading * 40));
            first = false;
        } else written = snprintf(output + used, sizeof(output) - used,
                                   ".%llu", (unsigned long long)arc);
        if (written < 0 || (size_t)written >= sizeof(output) - used) return NULL;
        used += (size_t)written; arc = 0;
    }
    if (first || (oid->value[oid->length - 1] & 0x80))
        return xx_pe_yx_auth_text("invalid", 7);
    return xx_pe_yx_auth_text(output, used);
}

typedef struct { xx_pki_hash_context context; bool ok; } xx_pe_yx_auth_hash;
static xx_pki_hash xx_pe_yx_auth_hash_algorithm(unsigned bytes, bool md2)
{
    return bytes == 16 ? (md2 ? XX_PKI_HASH_MD2 : XX_PKI_HASH_MD5) :
        bytes == 20 ? XX_PKI_HASH_SHA1 : bytes == 32 ? XX_PKI_HASH_SHA256 :
        bytes == 48 ? XX_PKI_HASH_SHA384 : bytes == 64 ? XX_PKI_HASH_SHA512 : XX_PKI_HASH_NONE;
}
static bool xx_pe_yx_auth_hash_init(xx_pe_yx_auth_hash *hash, unsigned bytes, bool md2)
{
    hash->ok = xx_pki_hash_init(&hash->context, xx_pe_yx_auth_hash_algorithm(bytes, md2));
    return hash->ok;
}
static void xx_pe_yx_auth_hash_update(xx_pe_yx_auth_hash *hash, const void *data, size_t size)
{
    if (hash->ok) hash->ok = xx_pki_hash_update(&hash->context, data, size);
}
static bool xx_pe_yx_auth_hash_finish(xx_pe_yx_auth_hash *hash, uint8_t output[64])
{
    return hash->ok && xx_pki_hash_final(&hash->context, output, 64);
}
static bool xx_pe_yx_auth_digest(const void *data, size_t size, unsigned bytes, bool md2, uint8_t output[64])
{
    return xx_pki_hash_memory(xx_pe_yx_auth_hash_algorithm(bytes, md2), data, size, output);
}

static const char *xx_pe_yx_auth_name_key(const char *oid)
{
    const char *name = xx_pe_yx_oid_name(oid);
    return name ? name : oid;
}

static bool xx_pe_yx_auth_printable(const uint8_t *bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        unsigned ch = bytes[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || (ch && strchr(" '()+,-./:=?", (int)ch))) continue;
        return false;
    }
    return true;
}

/* Render original DER to preserve the slash-separated, unescaped Names used
 * by the PE module, including hexadecimal values for non-text ASN.1 types. */
static char *xx_pe_yx_auth_name(const xx_x509_name *name, size_t *length)
{
    xx_pe_yx_auth_der sequence, set, attribute, oid, value;
    const uint8_t *rdns, *entries, *fields;
    size_t remaining, entries_size, fields_size, used = 0, capacity;
    char *result;
    if (!xx_pe_yx_auth_read(name->der, name->der_size, &sequence)) return NULL;
    if (name->der_size > (SIZE_MAX - 1) / 24) return NULL;
    capacity = (size_t)name->der_size * 24 + 1;
    result = (char *)malloc(capacity);
    if (!result) return NULL;
    rdns = sequence.value; remaining = sequence.length;
    while (remaining) {
        if (!xx_pe_yx_auth_take(&rdns, &remaining, &set) || (set.tag & 0x1f) != 17) break;
        bool first = true;
        entries = set.value; entries_size = set.length;
        while (entries_size) {
            if (!xx_pe_yx_auth_take(&entries, &entries_size, &attribute) || (attribute.tag & 0x1f) != 16)
                goto name_done;
            char *oid_text, *hex = NULL;
            const char *key;
            bool text = false;
            fields = attribute.value; fields_size = attribute.length;
            if (!xx_pe_yx_auth_take(&fields, &fields_size, &oid) ||
                (oid.tag & 0x20) || (oid.tag & 0x1f) == 31 ||
                !xx_pe_yx_auth_take(&fields, &fields_size, &value) ||
                (value.tag & 0x1f) == 31 ||
                !(oid_text = oid.length && oid.value[0] > 119 ?
                    xx_pe_yx_auth_text("invalid", 7) : xx_pe_yx_auth_oid(&oid))) goto name_done;
            key = xx_pe_yx_auth_name_key(oid_text);
            if (value.tag == 0x13) text = xx_pe_yx_auth_printable(value.value, value.length);
            else if (value.tag == 0x0c) text = xx_pe_yx_utf8_valid(value.value, value.length);
            else if (value.tag == 0x16 || value.tag == 0x14) {
                text = true;
                for (size_t i = 0; i < value.length; ++i)
                    if (value.value[i] > 0x7f ||
                        (value.tag == 0x14 && value.value[i] < 0x20)) text = false;
            }
            if (!text) hex = xx_pe_yx_auth_hex(value.value, value.length, false);
            if ((!text && !hex) || strlen(key) + value.length * 2 + 4 > capacity - used) {
                free(oid_text); free(hex); free(result); return NULL;
            }
            if (first) result[used++] = '/';
            first = false;
            memcpy(result + used, key, strlen(key)); used += strlen(key);
            result[used++] = '=';
            if (text) { memcpy(result + used, value.value, value.length); used += value.length; }
            else { result[used++] = '#'; memcpy(result + used, hex, value.length * 2); used += value.length * 2; }
            free(oid_text); free(hex);
        }
    }
name_done:
    result[used] = 0; *length = used;
    return result;
}

static void xx_pe_yx_auth_certificate_destroy(xx_pe_yx_certificate *certificate)
{
    free(certificate->issuer); free(certificate->subject);
    free(certificate->thumbprint); free(certificate->algorithm);
    free(certificate->algorithm_oid); free(certificate->serial);
}

void xx_pe_yx_signature_result_destroy(xx_pe_yx_signature_result *result)
{
    if (!result) return;
    for (size_t i = 0; i < result->count; ++i) {
        xx_pe_yx_signature *signature = &result->items[i];
        free(signature->digest_alg); free(signature->digest); free(signature->file_digest);
        free(signature->program_name); free(signature->more_info);
        free(signature->signer_digest); free(signature->signer_digest_alg);
        free(signature->signer_chain_indices);
        for (size_t j = 0; j < signature->certificate_count; ++j)
            xx_pe_yx_auth_certificate_destroy(&signature->certificates[j]);
        free(signature->certificates);
        for (size_t j = 0; j < signature->countersignature_count; ++j) {
            free(signature->countersignatures[j].digest);
            free(signature->countersignatures[j].digest_alg);
            free(signature->countersignatures[j].chain_indices);
        }
        free(signature->countersignatures);
    }
    free(result->items); memset(result, 0, sizeof(*result));
}

typedef struct { uint32_t offset, length; } xx_pe_yx_auth_section;
static int xx_pe_yx_auth_section_compare(const void *a, const void *b)
{
    uint32_t left = ((const xx_pe_yx_auth_section *)a)->offset, right = ((const xx_pe_yx_auth_section *)b)->offset;
    return (left > right) - (left < right);
}

static char *xx_pe_yx_auth_file_digest(const uint8_t *data, size_t size, unsigned bytes,
                                  bool md2, uint32_t certificate_size)
{
    uint64_t header_size;
    size_t pe, optional, checksum, security, section_table, count, sum;
    xx_pe_yx_auth_section *sections = NULL;
    xx_pe_yx_auth_hash hash;
    uint8_t digest[64];
    if (!xx_pe_yx_is_pe(data, size)) return NULL;
    pe = (size_t)xx_pe_yx_read(data, 60, 4); optional = pe + 24; checksum = optional + 64;
    security = optional + (xx_pe_yx_read(data, optional, 2) == 0x20b ? 112u : 96u) + 32;
    if (!xx_pe_yx_optional_field32(data, size, 60, 60, &header_size) ||
        checksum > size || security > size || checksum + 4 > security ||
        security + 8 > size || security + 8 > header_size || certificate_size > size) return NULL;
    count = xx_pe_yx_sections(data, size, pe, &section_table);
    if (count) {
        sections = (xx_pe_yx_auth_section *)calloc(count, sizeof(*sections));
        if (!sections) return NULL;
        for (size_t i = 0; i < count; ++i) {
            sections[i].offset = (uint32_t)xx_pe_yx_read(data, section_table + i * 40 + 20, 4);
            sections[i].length = (uint32_t)xx_pe_yx_read(data, section_table + i * 40 + 16, 4);
        }
        qsort(sections, count, sizeof(*sections), xx_pe_yx_auth_section_compare);
    }
    xx_pe_yx_auth_hash_init(&hash, bytes, md2);
    xx_pe_yx_auth_hash_update(&hash, data, checksum);
    xx_pe_yx_auth_hash_update(&hash, data + checksum + 4, security - checksum - 4);
    if (header_size > size) goto finalize;
    xx_pe_yx_auth_hash_update(&hash, data + security + 8, (size_t)header_size - security - 8);
    sum = (size_t)header_size;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = sections[i].offset, length = sections[i].length;
        if (offset > size || length > size - offset) goto finalize;
        xx_pe_yx_auth_hash_update(&hash, data + offset, length);
        if (sum > SIZE_MAX - length) goto finalize;
        sum += length;
    }
    if (sum <= size - certificate_size) xx_pe_yx_auth_hash_update(&hash, data + sum, size - certificate_size - sum);
finalize:
    free(sections);
    return xx_pe_yx_auth_hash_finish(&hash, digest) ? xx_pe_yx_auth_hex(digest, bytes, false) : NULL;
}

static void xx_pe_yx_auth_hasher_update(void *state, const uint8_t *data, size_t size)
{
    xx_pe_yx_auth_hash_update((xx_pe_yx_auth_hash *)state, data, size);
}

/* AuthenticodeHasher digest for non-PE inputs. Like the reference, a hasher
 * that stops early still yields the digest of the bytes fed so far. */
static char *xx_pe_yx_auth_hasher_digest(xx_pe_yx_authenticode_hasher_fn hasher, void *opaque,
                                  unsigned bytes, bool md2)
{
    xx_pe_yx_auth_hash hash;
    uint8_t digest[64];
    xx_pe_yx_auth_hash_init(&hash, bytes, md2);
    hasher(opaque, xx_pe_yx_auth_hasher_update, &hash);
    return xx_pe_yx_auth_hash_finish(&hash, digest) ? xx_pe_yx_auth_hex(digest, bytes, false) : NULL;
}

static bool xx_pe_yx_auth_digest_info(const xx_pe_yx_auth_der *info, char **oid, xx_pe_yx_auth_der *digest)
{
    const uint8_t *cursor = info->value;
    size_t remaining = info->length;
    xx_pe_yx_auth_der algorithm, object;
    if (!xx_pe_yx_auth_take(&cursor, &remaining, &algorithm) ||
        !xx_pe_yx_auth_take(&cursor, &remaining, digest) ||
        !xx_pe_yx_auth_read(algorithm.value, algorithm.length, &object)) return false;
    *oid = xx_pe_yx_auth_oid(&object);
    return *oid != NULL && digest->tag == 4;
}

static bool xx_pe_yx_auth_image_valid(xx_pe_yx_auth_der image)
{
    static const uint8_t image_oid[] = {
        0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0f
    };
    const uint8_t *cursor = image.value;
    size_t remaining = image.length;
    xx_pe_yx_auth_der oid, value, child;
    if (image.tag != 0x30 || !xx_pe_yx_auth_take(&cursor, &remaining, &oid) ||
        oid.tag != 6 || oid.length != sizeof(image_oid) ||
        memcmp(oid.value, image_oid, sizeof(image_oid))) return true;
    if (!remaining) return true;
    if (!xx_pe_yx_auth_take(&cursor, &remaining, &value)) return false;
    if (value.tag != 0x30) return true;
    cursor = value.value; remaining = value.length;
    while (remaining) if (!xx_pe_yx_auth_take(&cursor, &remaining, &child)) return false;
    return true;
}

/* The digestAlgorithm allowlist is deliberately narrower than the generic
 * PKI hash dispatcher: Authenticode accepts the digest and RSA aliases. */
static xx_pki_hash xx_pe_yx_auth_digest_algorithm(const xx_x509_algorithm_id *algorithm)
{
    const char *oid = algorithm->oid_text;
    char canonical[128];
    bool nonminimal = false;
    /* Display canonicalizes malformed arcs, but ObjectIdentifier equality
     * against an algorithm constant compares its original encoded bytes. */
    if (!xx_asn1_oid_to_text(algorithm->oid_der, algorithm->oid_der_size,
            canonical, sizeof(canonical), &nonminimal) || nonminimal || strcmp(canonical, oid))
        return XX_PKI_HASH_NONE;
    static const char *const allowed[] = {
        "1.2.840.113549.2.2", "1.2.840.113549.2.5", "1.3.14.3.2.26",
        "2.16.840.1.101.3.4.2.1", "2.16.840.1.101.3.4.2.2", "2.16.840.1.101.3.4.2.3",
        "1.2.840.113549.1.1.2", "1.2.840.113549.1.1.4", "1.2.840.113549.1.1.5",
        "1.3.14.3.2.29", "1.2.840.113549.1.1.11", "1.2.840.113549.1.1.12", "1.2.840.113549.1.1.13"
    };
    for (size_t i = 0; oid && i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (!strcmp(oid, allowed[i])) return xx_pki_hash_from_oid(oid);
    return XX_PKI_HASH_NONE;
}

static char *xx_pe_yx_auth_algorithm_name(const char *oid)
{
    const char *name = xx_pe_yx_oid_name(oid);
    if (!name) name = oid;
    return xx_pe_yx_auth_text(name, strlen(name));
}

static bool xx_pe_yx_auth_attribute_value(const xx_pkcs7_signer_info *signer, const char *oid,
                                    xx_asn1_tlv *value)
{
    xx_pkcs7_attribute attr;
    xx_asn1_cursor cursor;
    return xx_pkcs7_find_attr_signed(signer, oid, &attr) &&
        xx_pkcs7_attr_values_begin(&attr, &cursor) && xx_asn1_cursor_next(&cursor, value);
}

/* PKCS#9's counter field loop overwrites each field as it encounters a
 * matching attribute; ordinary SignerInfo lookups instead select the first. */
static bool xx_pe_yx_auth_counter_value(const xx_pkcs7_signer_info *signer, const char *oid, xx_asn1_tlv *value)
{
    xx_asn1_cursor attrs, values;
    xx_pkcs7_attribute attr;
    bool found = false;
    if (!xx_pkcs7_attrs_begin_signed(signer, &attrs)) return false;
    while (xx_pkcs7_attrs_next(&attrs, &attr)) if (!strcmp(attr.oid_text, oid))
        found = xx_pkcs7_attr_values_begin(&attr, &values) && xx_asn1_cursor_next(&values, value);
    return found;
}

static bool xx_pe_yx_auth_attributes_valid(const xx_pkcs7_signer_info *signer)
{
    for (unsigned i = 0; i < 2; ++i) {
        xx_asn1_cursor attrs, values;
        xx_pkcs7_attribute attr;
        xx_asn1_tlv value;
        bool begun = i ? xx_pkcs7_attrs_begin_unsigned(signer, &attrs) :
                         xx_pkcs7_attrs_begin_signed(signer, &attrs);
        if (!begun) continue;
        while (!xx_asn1_cursor_at_end(&attrs)) {
            if (!xx_pkcs7_attrs_next(&attrs, &attr) || !xx_pkcs7_attr_values_begin(&attr, &values)) return false;
            while (!xx_asn1_cursor_at_end(&values))
                if (!xx_asn1_cursor_next(&values, &value)) return false;
        }
    }
    return true;
}

static bool xx_pe_yx_auth_oid_matches(const uint8_t *der, size_t size, const char *expected)
{
    char canonical[128];
    bool nonminimal = false;
    return xx_asn1_oid_to_text(der, size, canonical, sizeof(canonical), &nonminimal) &&
        !nonminimal && !strcmp(canonical, expected);
}

typedef struct {
    xx_pkcs7_signed_data data;
    const uint8_t *content;
    size_t content_size;
} xx_pe_yx_auth_message;

static bool xx_pe_yx_auth_message_read(const uint8_t *data, size_t size, xx_pe_yx_auth_message *message,
                                 bool require_signed_data)
{
    xx_pkcs7_content_info ci;
    memset(message, 0, sizeof(*message));
    if (!xx_pkcs7_parse_content_info(data, size, &ci) || !ci.has_content ||
        (require_signed_data && !xx_pe_yx_auth_oid_matches(ci.content_type_oid_der, ci.content_type_oid_der_size, XX_PKCS7_OID_SIGNED_DATA)) ||
        !xx_pkcs7_parse_signed_data(ci.content_tlv, ci.content_tlv_size, &message->data) ||
        !message->data.content_info.has_content) return false;
    {
        xx_asn1_cursor cursor;
        xx_pkcs7_signer_info signer;
        xx_x509_algorithm_id algorithm;
        /* SignedData's ASN.1 parser eagerly parses every algorithm and
         * SignerInfo upstream, even though MS timestamp policy uses the
         * first signer only. Validate lazy views before adding its certs. */
        if (message->data.version < INT32_MIN || message->data.version > INT32_MAX ||
            !xx_pkcs7_digest_algorithms_begin(&message->data, &cursor)) return false;
        while (!xx_asn1_cursor_at_end(&cursor))
            if (!xx_pkcs7_digest_algorithms_next(&cursor, &algorithm)) return false;
        if (!xx_pkcs7_signer_infos_begin(&message->data, &cursor)) return false;
        while (!xx_asn1_cursor_at_end(&cursor))
            if (!xx_pkcs7_signer_infos_next(&cursor, &signer) || !xx_pe_yx_auth_attributes_valid(&signer)) return false;
    }
    message->content = message->data.content_info.content_tlv;
    message->content_size = message->data.content_info.content_tlv_size;
    if (message->data.content_info.content_is_octet_string) {
        if (!message->data.content_info.octet_string_content) return false;
        message->content = message->data.content_info.octet_string_content;
        message->content_size = message->data.content_info.octet_string_content_size;
    }
    return true;
}

typedef struct { xx_x509_certificate *items; size_t count; } xx_pe_yx_auth_certificates;

static bool xx_pe_yx_auth_add_certificates(xx_pe_yx_auth_certificates *pool, const xx_pkcs7_signed_data *data)
{
    xx_asn1_cursor cursor;
    xx_pkcs7_certificate_choice choice;
    if (!data->has_certificates) return true;
    if (!xx_pkcs7_certificates_begin(data, &cursor)) return true;
    while (xx_pkcs7_certificates_next(&cursor, &choice)) {
        xx_x509_certificate *grown;
        if (!choice.is_certificate) break;
        if (pool->count >= SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_x509_certificate *)realloc(pool->items, (pool->count + 1) * sizeof(*grown));
        if (!grown) return false;
        pool->items = grown; pool->items[pool->count++] = choice.certificate;
    }
    return true;
}

static bool xx_pe_yx_auth_certificate_fill(const xx_x509_certificate *certificate, xx_pe_yx_certificate *result)
{
    uint8_t thumbprint[20];
    memset(result, 0, sizeof(*result));
    result->issuer = xx_pe_yx_auth_name(&certificate->issuer, &result->issuer_length);
    result->subject = xx_pe_yx_auth_name(&certificate->subject, &result->subject_length);
    if (!xx_x509_thumbprint_sha1(certificate, thumbprint)) return false;
    result->thumbprint = xx_pe_yx_auth_hex(thumbprint, sizeof(thumbprint), false);
    {
        char display_oid[256];
        if (!xx_asn1_oid_to_text(certificate->signature_alg.oid_der, certificate->signature_alg.oid_der_size,
                display_oid, sizeof(display_oid), NULL)) strcpy(display_oid, "invalid");
        result->algorithm_oid = xx_pe_yx_auth_text(display_oid, strlen(display_oid));
    }
    result->algorithm = xx_pe_yx_auth_algorithm_name(certificate->signature_alg.oid_text);
    result->serial = xx_pe_yx_auth_hex(certificate->serial, certificate->serial_size, true);
    result->version = certificate->version + 1;
    result->not_before = certificate->validity.not_before;
    result->not_after = certificate->validity.not_after;
    return result->issuer && result->subject && result->thumbprint && result->algorithm_oid && result->algorithm && result->serial;
}

static size_t xx_pe_yx_auth_serial_certificate(const xx_pe_yx_auth_certificates *pool, const xx_pkcs7_signer_info *signer)
{
    for (size_t i = 0; i < pool->count; ++i)
        if (xx_x509_serial_equal(pool->items[i].serial, pool->items[i].serial_size,
                                 signer->sid.serial, signer->sid.serial_size)) return i;
    return SIZE_MAX;
}

static size_t *xx_pe_yx_auth_chain(const xx_pe_yx_auth_certificates *pool, size_t first, size_t *length)
{
    size_t *indices;
    *length = 0;
    if (first >= pool->count || pool->count >= SIZE_MAX / sizeof(*indices) - 1) return NULL;
    /* The upstream subject seen-set starts empty; a cycle may return its
     * initial certificate twice. The library preserves that behavior. */
    indices = (size_t *)malloc((pool->count + 1) * sizeof(*indices));
    if (!indices) return NULL;
    if (!xx_x509_build_chain(pool->items, pool->count, first, indices, pool->count + 1, length)) {
        free(indices); return NULL;
    }
    return indices;
}

static bool xx_pe_yx_auth_message_digest(const xx_x509_algorithm_id *algorithm, const uint8_t *data,
                                   size_t size, const uint8_t *expected, size_t expected_size)
{
    uint8_t digest[64];
    xx_pki_hash hash = xx_pe_yx_auth_digest_algorithm(algorithm);
    return hash && expected_size == xx_pki_hash_size(hash) &&
        xx_pki_hash_memory(hash, data, size, digest) && !memcmp(digest, expected, expected_size);
}

static bool xx_pe_yx_auth_signer_verify(const xx_pkcs7_signer_info *signer, const xx_pe_yx_auth_certificates *pool)
{
    size_t first = xx_pe_yx_auth_serial_certificate(pool, signer), chain_size = 0;
    size_t *chain;
    uint8_t header[16], digest[64];
    xx_pki_hash_context hash;
    xx_pki_hash algorithm = xx_pe_yx_auth_digest_algorithm(&signer->digest_algorithm);
    size_t header_size = xx_asn1_encode_header(XX_ASN1_CLASS_UNIVERSAL, true, XX_ASN1_TAG_SET,
                                               signer->signed_attrs_content_size, header, sizeof(header));
    bool valid;
    if (first >= pool->count || !algorithm || !header_size) return false;
    chain = xx_pe_yx_auth_chain(pool, first, &chain_size);
    valid = chain && xx_x509_verify_chain(pool->items, pool->count, chain, chain_size);
    free(chain);
    if (!valid || !xx_pki_hash_init(&hash, algorithm) ||
        !xx_pki_hash_update(&hash, header, header_size) ||
        !xx_pki_hash_update(&hash, signer->signed_attrs_content, signer->signed_attrs_content_size) ||
        !xx_pki_hash_final(&hash, digest, sizeof(digest))) return false;
    return xx_pki_verify_digest(pool->items[first].spki_der, pool->items[first].spki_der_size,
        NULL, NULL, 0, algorithm, digest, xx_pki_hash_size(algorithm), signer->signature, signer->signature_size);
}

static char *xx_pe_yx_auth_utf16be(const uint8_t *data, size_t size)
{
    char *result;
    size_t used = 0;
    if ((size & 1) || size > (SIZE_MAX - 1) / 2) return NULL;
    result = (char *)malloc(size * 2 + 1);
    if (!result) return NULL;
    for (size_t i = 0; i < size; i += 2) {
        uint32_t code = ((uint32_t)data[i] << 8) | data[i + 1];
        if (code >= 0xd800 && code <= 0xdbff) {
            uint32_t low;
            if (size - i < 4) { free(result); return NULL; }
            low = ((uint32_t)data[i + 2] << 8) | data[i + 3];
            if (low < 0xdc00 || low > 0xdfff) { free(result); return NULL; }
            code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
            i += 2;
        } else if (code >= 0xdc00 && code <= 0xdfff) { free(result); return NULL; }
        if (code < 0x80) result[used++] = (char)code;
        else if (code < 0x800) {
            result[used++] = (char)(0xc0 | (code >> 6)); result[used++] = (char)(0x80 | (code & 63));
        } else if (code < 0x10000) {
            result[used++] = (char)(0xe0 | (code >> 12)); result[used++] = (char)(0x80 | ((code >> 6) & 63));
            result[used++] = (char)(0x80 | (code & 63));
        } else {
            result[used++] = (char)(0xf0 | (code >> 18)); result[used++] = (char)(0x80 | ((code >> 12) & 63));
            result[used++] = (char)(0x80 | ((code >> 6) & 63)); result[used++] = (char)(0x80 | (code & 63));
        }
    }
    result[used] = 0;
    return result;
}

static void xx_pe_yx_auth_opus(const xx_pkcs7_signer_info *signer, xx_pe_yx_signature *signature)
{
    xx_asn1_tlv blob;
    xx_pe_yx_auth_der sequence, outer, inner;
    const uint8_t *cursor;
    size_t remaining;
    char *program_name = NULL;
    char *more_info = NULL;
    bool program_name_failed = false;
    unsigned field = 0;
    if (!xx_pe_yx_auth_attribute_value(signer, XX_AUTHENTICODE_OID_SPC_SP_OPUS_INFO, &blob) ||
        !xx_pe_yx_auth_read(blob.header, blob.total_size, &sequence)) return;
    cursor = sequence.value; remaining = sequence.length;
    while (xx_pe_yx_auth_take(&cursor, &remaining, &outer)) {
        if (outer.tag == 0xa0 && field == 0) {
            field = 1;
            /* programName [0] EXPLICIT SpcString, where
             * SpcString ::= CHOICE { unicode [0] IMPLICIT BMPSTRING,
             *                        ascii   [1] IMPLICIT IA5STRING }.
             * Upstream's `?` on a failed parse drops the whole
             * SpcSpOpusInfo, including moreInfo (asn1.rs parse_spc_string /
             * parse_inner), so a malformed programName suppresses moreInfo too. */
            if (!xx_pe_yx_auth_read(outer.value, outer.length, &inner))
                program_name_failed = true;
            else if (inner.tag == 0x80) {
                free(program_name);
                program_name = xx_pe_yx_auth_utf16be(inner.value, inner.length);
                if (!program_name) program_name_failed = true;
            } else if (inner.tag == 0x81) {
                for (size_t i = 0; i < inner.length; ++i)
                    if (inner.value[i] > 0x7f) program_name_failed = true;
                if (!program_name_failed)
                    program_name = xx_pe_yx_auth_text((const char *)inner.value, inner.length);
                if (!program_name) program_name_failed = true;
            } else program_name_failed = true;
        } else if (outer.tag == 0xa1 && field <= 1) {
            bool ascii = true;
            field = 2;
            if (xx_pe_yx_auth_read(outer.value, outer.length, &inner) && inner.tag == 0x80) {
                for (size_t i = 0; i < inner.length; ++i)
                    if (inner.value[i] > 0x7f) ascii = false;
                if (ascii) more_info = xx_pe_yx_auth_text((const char *)inner.value, inner.length);
            }
        } else break;
    }
    if (program_name_failed) {
        free(program_name);
        free(more_info);
        return;
    }
    free(signature->program_name);
    free(signature->more_info);
    signature->program_name = program_name;
    signature->more_info = more_info;
}

typedef struct { xx_pkcs7_signer_info signer; } xx_pe_yx_auth_counter_record;

/* Returns -1 for allocation failure, 0 for the missing digest that aborts
 * the parent signature, and 1 after appending the observable counter. */
static int xx_pe_yx_auth_counter_append(const xx_pkcs7_signer_info *signer, xx_pe_yx_signature *signature,
                                   xx_pe_yx_auth_counter_record **records, const uint8_t *digest, size_t digest_size,
                                   const char *digest_oid, bool verified)
{
    xx_asn1_tlv signed_digest, signing_time;
    size_t count = signature->countersignature_count;
    xx_pe_yx_countersignature *grown;
    xx_pe_yx_auth_counter_record *grown_records;
    if (!xx_pe_yx_auth_counter_value(signer, XX_PKCS9_OID_MESSAGE_DIGEST, &signed_digest)) return 0;
    if (!digest) { digest = signed_digest.content; digest_size = signed_digest.content_size; }
    if (count >= SIZE_MAX / sizeof(*grown) || count >= SIZE_MAX / sizeof(*grown_records)) return -1;
    grown = (xx_pe_yx_countersignature *)realloc(signature->countersignatures, (count + 1) * sizeof(*grown));
    if (!grown) return -1;
    signature->countersignatures = grown;
    grown_records = (xx_pe_yx_auth_counter_record *)realloc(*records, (count + 1) * sizeof(*grown_records));
    if (!grown_records) return -1;
    *records = grown_records; grown_records[count].signer = *signer;
    memset(grown + count, 0, sizeof(*grown));
    signature->countersignature_count++;
    grown[count].digest = xx_pe_yx_auth_hex(digest, digest_size, false);
    grown[count].digest_alg = xx_pe_yx_auth_algorithm_name(digest_oid ? digest_oid : signer->digest_algorithm.oid_text);
    grown[count].verified = verified;
    if (xx_pe_yx_auth_counter_value(signer, XX_PKCS9_OID_SIGNING_TIME, &signing_time) &&
        signing_time.tag_class == XX_ASN1_CLASS_UNIVERSAL && signing_time.tag == XX_ASN1_TAG_UTCTIME)
        grown[count].has_sign_time = xx_asn1_read_time(&signing_time, &grown[count].sign_time);
    return grown[count].digest && grown[count].digest_alg ? 1 : -1;
}

static int xx_pe_yx_auth_counters(const xx_pkcs7_signer_info *signer, xx_pe_yx_auth_certificates *pool,
                            xx_pe_yx_signature *signature, xx_pe_yx_auth_counter_record **records)
{
    xx_asn1_cursor attrs, values;
    xx_pkcs7_attribute attr;
    xx_asn1_tlv value;
    if (!xx_pkcs7_attrs_begin_unsigned(signer, &attrs)) return 1;
    while (xx_pkcs7_attrs_next(&attrs, &attr)) {
        bool timestamp = !strcmp(attr.oid_text, XX_AUTHENTICODE_OID_RFC3161_COUNTERSIGN);
        if (!timestamp && strcmp(attr.oid_text, XX_PKCS9_OID_COUNTERSIGNATURE)) continue;
        if (!xx_pkcs7_attr_values_begin(&attr, &values)) continue;
        while (xx_asn1_cursor_next(&values, &value)) {
            xx_pkcs7_signer_info counter;
            xx_asn1_tlv digest;
            bool verified;
            int status;
            if (!timestamp) {
                if (!xx_pkcs7_parse_signer_info(value.header, value.total_size, &counter)) continue;
                if (!xx_pe_yx_auth_counter_value(&counter, XX_PKCS9_OID_MESSAGE_DIGEST, &digest)) return 0;
                verified = xx_pe_yx_auth_message_digest(&counter.digest_algorithm, signer->signature, signer->signature_size,
                    digest.content, digest.content_size) && xx_pe_yx_auth_signer_verify(&counter, pool);
                status = xx_pe_yx_auth_counter_append(&counter, signature, records, NULL, 0, NULL, verified);
            } else {
                xx_pe_yx_auth_message message;
                xx_asn1_cursor signers;
                xx_tsp_tst_info tst;
                if (!xx_pe_yx_auth_message_read(value.header, value.total_size, &message, false)) continue;
                if (!xx_pe_yx_auth_add_certificates(pool, &message.data)) return -1;
                if (!xx_pkcs7_signer_infos_begin(&message.data, &signers) ||
                    !xx_pkcs7_signer_infos_next(&signers, &counter)) continue;
                if (!xx_pe_yx_auth_attribute_value(&counter, XX_PKCS9_OID_MESSAGE_DIGEST, &digest)) return 0;
                if (!xx_tsp_parse_tst_info_prefix(message.content, message.content_size, &tst)) continue;
                verified = xx_pe_yx_auth_message_digest(&tst.message_imprint.hash_algorithm, signer->signature, signer->signature_size,
                    tst.message_imprint.hashed_message, tst.message_imprint.hashed_message_size) &&
                    xx_pe_yx_auth_message_digest(&counter.digest_algorithm, message.content, message.content_size,
                        digest.content, digest.content_size) && xx_pe_yx_auth_signer_verify(&counter, pool);
                status = xx_pe_yx_auth_counter_append(&counter, signature, records,
                    tst.message_imprint.hashed_message, tst.message_imprint.hashed_message_size,
                    tst.message_imprint.hash_algorithm.oid_text, verified);
            }
            if (status != 1) return status;
        }
    }
    return 1;
}

static bool xx_pe_yx_auth_append_signature(const uint8_t *data, size_t size, uint32_t certificate_size,
                                      const uint8_t *encoded, size_t encoded_size,
                                      xx_pe_yx_signature_result *result, unsigned depth,
                                      xx_pe_yx_authenticode_hasher_fn hasher, void *hasher_opaque)
{
    xx_pe_yx_auth_message message;
    xx_pkcs7_signer_info signer;
    xx_x509_algorithm_id listed_algorithm;
    xx_asn1_cursor cursor;
    xx_asn1_tlv signer_digest, content_type;
    xx_pe_yx_auth_der sequence, image, info, digest;
    const uint8_t *fields;
    size_t remaining, first = SIZE_MAX;
    char *oid = NULL;
    xx_pe_yx_auth_certificates pool = {0};
    xx_pe_yx_signature_result staged = {0};
    xx_pe_yx_auth_counter_record *records = NULL;
    bool ok = true;
    xx_pki_hash algorithm;
    xx_pe_yx_signature *signature;
    if (depth > 32 || !xx_pe_yx_auth_message_read(encoded, encoded_size, &message, true)) return true;
    if (message.data.version != 1 || !xx_pe_yx_auth_oid_matches(message.data.content_info.content_type_oid_der,
            message.data.content_info.content_type_oid_der_size, XX_AUTHENTICODE_OID_SPC_INDIRECT_DATA) ||
        !xx_pkcs7_digest_algorithms_begin(&message.data, &cursor) ||
        !xx_pkcs7_digest_algorithms_next(&cursor, &listed_algorithm) || !xx_asn1_cursor_at_end(&cursor) ||
        !xx_pkcs7_signer_infos_begin(&message.data, &cursor) ||
        !xx_pkcs7_signer_infos_next(&cursor, &signer) || !xx_asn1_cursor_at_end(&cursor) ||
        !xx_pe_yx_auth_attributes_valid(&signer) ||
        !(algorithm = xx_pe_yx_auth_digest_algorithm(&signer.digest_algorithm)) ||
        !xx_pe_yx_auth_attribute_value(&signer, XX_PKCS9_OID_MESSAGE_DIGEST, &signer_digest) ||
        !xx_pe_yx_auth_attribute_value(&signer, XX_PKCS9_OID_CONTENT_TYPE, &content_type) ||
        !xx_pe_yx_auth_read(message.content, message.content_size, &sequence) || sequence.tag != 0x30) return true;
    fields = sequence.value; remaining = sequence.length;
    if (!xx_pe_yx_auth_take(&fields, &remaining, &image) || !xx_pe_yx_auth_take(&fields, &remaining, &info) ||
        !xx_pe_yx_auth_image_valid(image) || !xx_pe_yx_auth_digest_info(&info, &oid, &digest)) goto done;
    staged.items = (xx_pe_yx_signature *)calloc(1, sizeof(*staged.items));
    if (!staged.items) { ok = false; goto done; }
    staged.count = 1; signature = staged.items;
    signature->digest_alg = xx_pe_yx_auth_algorithm_name(oid);
    signature->digest = xx_pe_yx_auth_hex(digest.value, digest.length, false);
    signature->file_digest = hasher ? xx_pe_yx_auth_hasher_digest(hasher, hasher_opaque,
        (unsigned)xx_pki_hash_size(algorithm), algorithm == XX_PKI_HASH_MD2) :
        xx_pe_yx_auth_file_digest(data, size, (unsigned)xx_pki_hash_size(algorithm), algorithm == XX_PKI_HASH_MD2, certificate_size);
    signature->signer_digest = xx_pe_yx_auth_hex(signer_digest.content, signer_digest.content_size, false);
    signature->signer_digest_alg = xx_pe_yx_auth_algorithm_name(signer.digest_algorithm.oid_text);
    if (!signature->digest_alg || !signature->digest || !signature->file_digest ||
        !signature->signer_digest || !signature->signer_digest_alg || !xx_pe_yx_auth_add_certificates(&pool, &message.data)) {
        ok = false; goto done;
    }
    {
        int status = xx_pe_yx_auth_counters(&signer, &pool, signature, &records);
        if (status != 1) { ok = status == 0; goto done; }
    }
    if (pool.count) {
        signature->certificates = (xx_pe_yx_certificate *)calloc(pool.count, sizeof(*signature->certificates));
        if (!signature->certificates) { ok = false; goto done; }
    }
    signature->certificate_count = pool.count;
    for (size_t i = 0; i < pool.count; ++i) {
        if (!xx_pe_yx_auth_certificate_fill(&pool.items[i], &signature->certificates[i])) { ok = false; goto done; }
        if (first == SIZE_MAX && xx_x509_name_equal(&pool.items[i].issuer, &signer.sid.issuer)) first = i;
    }
    signature->signer_chain_indices = xx_pe_yx_auth_chain(&pool, first, &signature->signer_chain_count);
    for (size_t i = 0; i < signature->countersignature_count; ++i)
        signature->countersignatures[i].chain_indices = xx_pe_yx_auth_chain(&pool,
            xx_pe_yx_auth_serial_certificate(&pool, &records[i].signer), &signature->countersignatures[i].chain_count);
    signature->verified = !strcmp(signature->digest, signature->file_digest) &&
        xx_pe_yx_auth_message_digest(&signer.digest_algorithm, sequence.value, sequence.length,
            signer_digest.content, signer_digest.content_size) && xx_pe_yx_auth_signer_verify(&signer, &pool);
    xx_pe_yx_auth_opus(&signer, signature);
    {
        xx_pe_yx_signature *grown;
        if (result->count >= SIZE_MAX / sizeof(*grown)) { ok = false; goto done; }
        grown = (xx_pe_yx_signature *)realloc(result->items, (result->count + 1) * sizeof(*grown));
        if (!grown) { ok = false; goto done; }
        result->items = grown; grown[result->count++] = *signature;
        free(staged.items); memset(&staged, 0, sizeof(staged));
    }
    /* A nested value is independent: invalid nested signatures are ignored. */
    if (xx_pkcs7_attrs_begin_unsigned(&signer, &cursor)) {
        xx_pkcs7_attribute attr;
        while (ok && xx_pkcs7_attrs_next(&cursor, &attr)) if (!strcmp(attr.oid_text, XX_AUTHENTICODE_OID_NESTED_SIGNATURE)) {
            xx_asn1_cursor values;
            xx_asn1_tlv value;
            if (xx_pkcs7_attr_values_begin(&attr, &values)) while (ok && xx_asn1_cursor_next(&values, &value))
                ok = xx_pe_yx_auth_append_signature(data, size, certificate_size, value.header, value.total_size,
                    result, depth + 1, hasher, hasher_opaque);
        }
    }
done:
    free(oid); free(pool.items); free(records);
    xx_pe_yx_signature_result_destroy(&staged);
    return ok;
}

bool xx_pe_yx_parse_signatures(const uint8_t *data, size_t size,
                             xx_pe_yx_signature_result *result)
{
    size_t pe, optional, directory, at, end, readable;
    uint32_t table_offset, table_size;
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (!xx_pe_yx_is_pe(data, size)) return true;
    pe = (size_t)xx_pe_yx_read(data, 60, 4); optional = pe + 24;
    if (size - optional < 2) return true;
    directory = optional + (xx_pe_yx_read(data, optional, 2) == 0x20b ? 112u : 96u) + 32;
    if (directory > size || size - directory < 8) return true;
    table_offset = (uint32_t)xx_pe_yx_read(data, directory, 4);
    table_size = (uint32_t)xx_pe_yx_read(data, directory + 4, 4);
    if (!table_offset || table_offset > size) return true;
    readable = size - table_offset; if (readable > table_size) readable = table_size;
    at = table_offset; end = at + readable;
    while (at <= end && end - at >= 8) {
        uint32_t record_size = (uint32_t)xx_pe_yx_read(data, at, 4);
        size_t advance;
        if (record_size < 8 || record_size > end - at) break;
        /* Upstream commits this record only after its complete aligned
         * span fits inside the readable certificate table. */
        advance = ((size_t)record_size + 7u) & ~(size_t)7u;
        if (!advance || advance > end - at) break;
        if (!xx_pe_yx_auth_append_signature(data, size, (uint32_t)readable,
            data + at + 8, record_size - 8, result, 0, NULL, NULL)) {
            xx_pe_yx_signature_result_destroy(result); return false;
        }
        at += advance;
    }
    return true;
}

bool xx_pe_yx_parse_blob(const uint8_t *blob, size_t blob_size,
                                xx_pe_yx_authenticode_hasher_fn hasher,
                                void *hasher_opaque,
                                xx_pe_yx_signature_result *result)
{
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (!hasher || (!blob && blob_size)) return false;
    if (!blob_size) return true;
    /* The PE ranges are unused: `hasher` computes every file digest. */
    if (!xx_pe_yx_auth_append_signature(NULL, 0, 0, blob, blob_size, result, 0,
                                 hasher, hasher_opaque)) {
        xx_pe_yx_signature_result_destroy(result);
        return false;
    }
    return true;
}
