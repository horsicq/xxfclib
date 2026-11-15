/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_asn1.h
 * @brief Minimal, allocation-free DER/BER TLV reader.
 *
 * Every accessor returns pointer+length views into the caller's original
 * buffer (never copies, never allocates) except the explicit "_into" flatten
 * helpers, which assemble a BER constructed string into a caller-supplied
 * buffer, and the "_text"/"_encode"/"_reencode" helpers, which format into a
 * caller-supplied buffer. Nothing here validates application-level grammar
 * (that is the job of the x509/pkcs7 layers) - this module only knows about
 * identifier octets, length octets and content octets (ITU-T X.690).
 *
 * BER is accepted (indefinite lengths, constructed BIT STRING/OCTET STRING,
 * high tag numbers); DER-specific minimality is not enforced but is reported
 * through optional "nonminimal" out-flags the caller may query and reject on.
 * Every function that could otherwise recurse without bound (resolving an
 * indefinite length, flattening a constructed string) takes/threads a depth
 * counter and fails past #XX_ASN1_MAX_DEPTH.
 */
#ifndef XX_ASN1_H
#define XX_ASN1_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Nesting depth accepted while resolving indefinite lengths or flattening
 * constructed strings. Bounds stack/iteration cost against crafted input. */
#define XX_ASN1_MAX_DEPTH 32u

typedef enum xx_asn1_class {
    XX_ASN1_CLASS_UNIVERSAL = 0,
    XX_ASN1_CLASS_APPLICATION = 1,
    XX_ASN1_CLASS_CONTEXT = 2,
    XX_ASN1_CLASS_PRIVATE = 3
} xx_asn1_class;

/** Universal tag numbers (ITU-T X.680 clause 8, table 1). Context/application
 * tag numbers reuse this same type; interpret them only together with the
 * tag_class the TLV reports. */
typedef enum xx_asn1_tag {
    XX_ASN1_TAG_EOC = 0,
    XX_ASN1_TAG_BOOLEAN = 1,
    XX_ASN1_TAG_INTEGER = 2,
    XX_ASN1_TAG_BIT_STRING = 3,
    XX_ASN1_TAG_OCTET_STRING = 4,
    XX_ASN1_TAG_NULL = 5,
    XX_ASN1_TAG_OID = 6,
    XX_ASN1_TAG_OBJECT_DESCRIPTOR = 7,
    XX_ASN1_TAG_EXTERNAL = 8,
    XX_ASN1_TAG_REAL = 9,
    XX_ASN1_TAG_ENUMERATED = 10,
    XX_ASN1_TAG_EMBEDDED_PDV = 11,
    XX_ASN1_TAG_UTF8_STRING = 12,
    XX_ASN1_TAG_RELATIVE_OID = 13,
    XX_ASN1_TAG_SEQUENCE = 16,
    XX_ASN1_TAG_SET = 17,
    XX_ASN1_TAG_NUMERIC_STRING = 18,
    XX_ASN1_TAG_PRINTABLE_STRING = 19,
    XX_ASN1_TAG_T61_STRING = 20,
    XX_ASN1_TAG_VIDEOTEX_STRING = 21,
    XX_ASN1_TAG_IA5_STRING = 22,
    XX_ASN1_TAG_UTCTIME = 23,
    XX_ASN1_TAG_GENERALIZEDTIME = 24,
    XX_ASN1_TAG_GRAPHIC_STRING = 25,
    XX_ASN1_TAG_VISIBLE_STRING = 26,
    XX_ASN1_TAG_GENERAL_STRING = 27,
    XX_ASN1_TAG_UNIVERSAL_STRING = 28,
    XX_ASN1_TAG_CHARACTER_STRING = 29,
    XX_ASN1_TAG_BMP_STRING = 30
} xx_asn1_tag;

/** One decoded TLV. `header`/`content` point into the buffer the caller
 * passed to xx_asn1_parse_tlv(); their lifetime is the caller's buffer's. */
typedef struct xx_asn1_tlv {
    xx_asn1_class tag_class;
    bool constructed;
    uint32_t tag;          /**< Tag number (low or high tag number form). */
    const uint8_t *header; /**< Start of the identifier octet. */
    size_t header_size;    /**< Identifier + length octets. */
    const uint8_t *content;
    size_t content_size;   /**< For an indefinite-length TLV, resolved by scanning to the matching EOC; never includes the EOC octets themselves. */
    size_t total_size;     /**< header_size + content_size (+2 EOC octets if indefinite). */
    bool indefinite;       /**< True if the original encoding used the indefinite length form (0x80). */
} xx_asn1_tlv;

/** Walks the children of one constructed TLV, or an arbitrary byte span,
 * without allocation. `limit` already excludes any trailing EOC octets, so
 * xx_asn1_cursor_at_end() needs no separate EOC test. */
typedef struct xx_asn1_cursor {
    const uint8_t *pos;
    const uint8_t *limit;
    unsigned depth; /**< Depth to use (and bounds-check) when decoding each child. */
} xx_asn1_cursor;

/* ------------------------------------------------------------- parsing -- */

/** Decodes exactly one TLV at the start of data[0..size). On success,
 * `*consumed` is the TLV's total size (>=2) and `*nonminimal` (if non-NULL)
 * is reset to false and set to true when the header used a non-minimal
 * encoding (high tag number for a tag <=30, a padded long-form length, or a
 * long-form length that fit in short form). Never recurses past depth
 * #XX_ASN1_MAX_DEPTH while resolving an indefinite length. Returns false
 * (never crashes) on truncation, an invalid length, a primitive TLV claiming
 * an indefinite length, or depth exhaustion. */
XXFC_API bool xx_asn1_parse_tlv_ex(const uint8_t *data, size_t size, unsigned depth, xx_asn1_tlv *out, size_t *consumed, bool *nonminimal);

/** xx_asn1_parse_tlv_ex() starting at depth 0, discarding the nonminimal flag. */
XXFC_API bool xx_asn1_parse_tlv(const uint8_t *data, size_t size, xx_asn1_tlv *out, size_t *consumed);

/** Opaque ANY counterpart. Also retains universal tag0 with content or
 * constructed form, as x509-parser does for AttributeValue/algorithm ANY.
 * This does not reinterpret such values as EOC; indefinite scanning still
 * requires an exact00 00 terminator. Other bounds/minimality flags are shared. */
XXFC_API bool xx_asn1_parse_any_tlv_ex(const uint8_t *data, size_t size, unsigned depth, xx_asn1_tlv *out, size_t *consumed, bool *nonminimal);
XXFC_API bool xx_asn1_parse_any_tlv(const uint8_t *data, size_t size, xx_asn1_tlv *out, size_t *consumed);

/** True for the universal, primitive, zero-length End-of-Contents marker. */
XXFC_API bool xx_asn1_is_eoc(const xx_asn1_tlv *tlv);
/** True when tlv->tag_class is UNIVERSAL and tlv->tag == tag. */
XXFC_API bool xx_asn1_is_universal(const xx_asn1_tlv *tlv, uint32_t tag);

/* --------------------------------------------------- children iteration -- */

/** Begins iterating the children of a constructed TLV. Fails if `parent` is
 * not constructed or `depth` already exceeds #XX_ASN1_MAX_DEPTH. Pass
 * parent's own depth + 1. */
XXFC_API bool xx_asn1_cursor_init(xx_asn1_cursor *cursor, const xx_asn1_tlv *parent, unsigned depth);
/** Begins iterating TLVs directly within a raw byte span (no wrapping TLV). */
XXFC_API bool xx_asn1_cursor_init_span(xx_asn1_cursor *cursor, const uint8_t *data, size_t size, unsigned depth);
XXFC_API bool xx_asn1_cursor_at_end(const xx_asn1_cursor *cursor);
/** Decodes the next child and advances the cursor. False (cursor unchanged)
 * at end of input or on a malformed child. */
XXFC_API bool xx_asn1_cursor_next(xx_asn1_cursor *cursor, xx_asn1_tlv *out);

/* --------------------------------------------------------- typed readers -- */

XXFC_API bool xx_asn1_read_boolean(const xx_asn1_tlv *tlv, bool *out);

/** Raw big-endian two's complement content bytes (length >= 1 always). */
XXFC_API bool xx_asn1_read_integer(const xx_asn1_tlv *tlv, const uint8_t **out_bytes, size_t *out_size);
XXFC_API bool xx_asn1_integer_is_negative(const xx_asn1_tlv *tlv);
/** Small-integer helper: fails (returns false) if the INTEGER has more than 8 content bytes. */
XXFC_API bool xx_asn1_read_int64(const xx_asn1_tlv *tlv, int64_t *out);

XXFC_API bool xx_asn1_read_null(const xx_asn1_tlv *tlv);

/** Raw content bytes of an OBJECT IDENTIFIER (the base-128 arc encoding, unparsed). */
XXFC_API bool xx_asn1_read_oid_raw(const xx_asn1_tlv *tlv, const uint8_t **out_bytes, size_t *out_size);
/** Decodes raw OID content bytes into a NUL-terminated dotted string ("1.2.840...").
 * Arc values up to UINT64_MAX are supported. `*nonminimal` (if non-NULL) is set
 * to true when any arc used a non-minimal base-128 encoding; decoding still
 * succeeds so the caller can choose whether to reject it. */
XXFC_API bool xx_asn1_oid_to_text(const uint8_t *oid_bytes, size_t oid_size, char *out, size_t out_capacity, bool *nonminimal);
/** xx_asn1_read_oid_raw() + xx_asn1_oid_to_text() in one call. */
XXFC_API bool xx_asn1_read_oid_text(const xx_asn1_tlv *tlv, char *out, size_t out_capacity, bool *nonminimal);

/** Zero-copy accessor; fails if `tlv` is a constructed (chunked) BIT STRING. */
XXFC_API bool xx_asn1_read_bit_string_primitive(const xx_asn1_tlv *tlv, unsigned *out_unused_bits, const uint8_t **out_bytes, size_t *out_size);
/** Flattens a primitive or BER-constructed BIT STRING into out_buffer. Only the
 * final chunk of a constructed BIT STRING may carry unused bits (X.690 8.6.4);
 * any earlier chunk with unused bits is rejected. */
XXFC_API bool xx_asn1_read_bit_string_into(const xx_asn1_tlv *tlv, unsigned depth, uint8_t *out_buffer, size_t out_capacity, size_t *out_size, unsigned *out_unused_bits);

/** Zero-copy accessor; fails if `tlv` is a constructed (chunked) OCTET STRING. */
XXFC_API bool xx_asn1_read_octet_string_primitive(const xx_asn1_tlv *tlv, const uint8_t **out_bytes, size_t *out_size);
/** Flattens a primitive or BER-constructed OCTET STRING into out_buffer. */
XXFC_API bool xx_asn1_read_octet_string_into(const xx_asn1_tlv *tlv, unsigned depth, uint8_t *out_buffer, size_t out_capacity, size_t *out_size);

/** Decodes primitive UTCTime/GeneralizedTime with YARA-X's asn1-rs 0.7.2
 * grammar. UTCTime years >=50 mean 19xx, otherwise 20xx. Seconds may be
 * omitted before Z; a UTC +/-HHMM offset requires seconds. GeneralizedTime
 * requires Z, accepts a dot plus 0..3 fractional digits only after seconds,
 * and discards those milliseconds. Calendar ranges are checked, second 60
 * and comma fractions are rejected. Generalized year 0000 is accepted. */
XXFC_API bool xx_asn1_read_time(const xx_asn1_tlv *tlv, int64_t *out_unix_seconds);

/** True for a universal tag naming one of the ASN.1 restricted/unrestricted character string types. */
XXFC_API bool xx_asn1_is_string_tag(uint32_t universal_tag);
/** Raw string value and its universal tag, with no character-set conversion
 * or validation. Constructed string values are rejected by this accessor. */
XXFC_API bool xx_asn1_read_string_primitive(const xx_asn1_tlv *tlv, uint32_t *out_tag, const uint8_t **out_bytes, size_t *out_size);
/** Debug name for a universal tag ("SEQUENCE", "INTEGER", ...), or "UNKNOWN"/a class label. Never NULL. */
XXFC_API const char *xx_asn1_tag_name(xx_asn1_class tag_class, uint32_t tag);

/* --------------------------------------------------------- re-encoding --- */

/** Writes just the identifier+length octets for (cls, constructed, tag,
 * content_size) into out[0..out_capacity). Returns the header size (>=2) or
 * 0 on failure (capacity, or a tag/length too large for this encoder). */
XXFC_API size_t xx_asn1_encode_header(xx_asn1_class cls, bool constructed, uint32_t tag, size_t content_size, uint8_t *out, size_t out_capacity);

/** Writes a new header (cls/constructed/tag) in front of the SAME content
 * bytes as `tlv`. Used to re-tag an IMPLICIT [n] SET (e.g. CMS signedAttrs)
 * as a universal SET OF before hashing it, per RFC 5652 5.4. Input and output
 * may overlap. This retags the outer header only: it does not recursively
 * canonicalize BER content or sort SET OF elements. */
XXFC_API bool xx_asn1_reencode(const xx_asn1_tlv *tlv, xx_asn1_class new_class, bool new_constructed, uint32_t new_tag, uint8_t *out, size_t out_capacity, size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif /* XX_ASN1_H */
