/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/asn1/xx_asn1.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* memcpy */

/* ------------------------------------------------------------- identifier -- */

static bool read_identifier(const uint8_t *p, size_t n, xx_asn1_class *cls, bool *constructed, uint32_t *tag, size_t *used, bool *nonminimal)
{
    uint8_t b0;
    size_t i;
    if (n < 1U) return false;
    b0 = p[0];
    *cls = (xx_asn1_class)((b0 >> 6) & 0x03U);
    *constructed = (b0 & 0x20U) != 0U;
    i = 1U;
    if ((b0 & 0x1FU) != 0x1FU) {
        *tag = (uint32_t)(b0 & 0x1FU);
    } else {
        uint32_t value = 0U;
        bool first = true;
        bool leading_zero = false;
        for (;;) {
            uint8_t b;
            if (i >= n) return false; /* truncated high-tag-number form */
            b = p[i++];
            if (first && (b & 0x7FU) == 0U) leading_zero = true;
            first = false;
            if (value > (UINT32_MAX >> 7)) return false; /* tag overflow */
            value = (value << 7) | (uint32_t)(b & 0x7FU);
            if ((b & 0x80U) == 0U) break;
        }
        if (value <= 30U) leading_zero = true; /* should have used the low tag number form */
        if (nonminimal && leading_zero) *nonminimal = true;
        *tag = value;
    }
    *used = i;
    return true;
}

/* ----------------------------------------------------------------- length -- */

static bool read_length(const uint8_t *p, size_t n, size_t *content_len, bool *indefinite, size_t *used, bool *nonminimal)
{
    uint8_t b0;
    if (n < 1U) return false;
    b0 = p[0];
    *indefinite = false;
    if (b0 == 0x80U) {
        *indefinite = true;
        *content_len = 0U;
        *used = 1U;
        return true;
    }
    if (b0 == 0xFFU) return false; /* X.690 8.1.3.5(c): reserved */
    if ((b0 & 0x80U) == 0U) {
        *content_len = b0;
        *used = 1U;
        return true;
    }
    {
        unsigned count = (unsigned)(b0 & 0x7FU);
        uint64_t value = 0U;
        unsigned i;
        if (count == 0U || count > 8U) return false; /* count==0 is 0x80, handled above; >8 cannot fit size_t safely */
        if (1U + (size_t)count > n) return false;
        for (i = 0U; i < count; ++i) {
            uint8_t b = p[1U + i];
            if (i == 0U && b == 0x00U && count > 1U) {
                if (nonminimal) *nonminimal = true; /* padded long-form length */
            }
            value = (value << 8) | (uint64_t)b;
        }
        if (value > (uint64_t)SIZE_MAX) return false;
        if (value < 128U) {
            if (nonminimal) *nonminimal = true; /* could have used the short form */
        }
        *content_len = (size_t)value;
        *used = 1U + (size_t)count;
        return true;
    }
}

/* ------------------------------------------------- indefinite length scan -- */

/* Scans content[0..size) for the End-of-Contents marker matching an
 * indefinite-length TLV whose content starts at `content`, recursing into any
 * nested indefinite-length child along the way. `depth` is the depth budget
 * for those children (already incremented relative to the enclosing TLV). */
static bool resolve_indefinite_content(const uint8_t *content, size_t size, unsigned depth, size_t *content_size, size_t *total_with_eoc)
{
    size_t pos = 0U;
    if (depth > XX_ASN1_MAX_DEPTH) return false;
    for (;;) {
        xx_asn1_tlv child;
        size_t consumed;
        if (pos > size) return false;
        if (size - pos >= 2U && content[pos] == 0x00U && content[pos + 1U] == 0x00U) {
            *content_size = pos;
            *total_with_eoc = pos + 2U;
            return true;
        }
        if (pos >= size) return false; /* ran off the end without an EOC */
        if (!xx_asn1_parse_tlv_ex(content + pos, size - pos, depth, &child, &consumed, NULL)) return false;
        pos += consumed; /* consumed >= 2 on every successful parse: forward progress is guaranteed */
    }
}

static bool parse_tlv_internal(const uint8_t *data, size_t size, unsigned depth, xx_asn1_tlv *out, size_t *consumed, bool *nonminimal, bool opaque_any)
{
    xx_asn1_class cls;
    bool constructed;
    uint32_t tag;
    size_t id_used, len_used, content_len, header_size, avail;
    bool indefinite;
    const uint8_t *content;
    size_t total;
    bool nm = false;

    if (nonminimal) *nonminimal = false;
    if (!data || !out || !consumed) return false;
    if (depth > XX_ASN1_MAX_DEPTH) return false;
    if (!read_identifier(data, size, &cls, &constructed, &tag, &id_used, &nm)) return false;
    if (!read_length(data + id_used, size - id_used, &content_len, &indefinite, &len_used, &nm)) return false;
    if (indefinite && !constructed) return false; /* X.690 8.1.3.2 */
    /* End-of-Contents has precisely the two-octet encoding 00 00. */
    if (!opaque_any && cls == XX_ASN1_CLASS_UNIVERSAL && tag == 0U &&
        (constructed || indefinite || content_len != 0U || id_used != 1U || len_used != 1U)) return false;

    header_size = id_used + len_used;
    content = data + header_size;
    avail = size - header_size;

    if (indefinite) {
        size_t resolved_content, resolved_total_with_eoc;
        if (!resolve_indefinite_content(content, avail, depth + 1U, &resolved_content, &resolved_total_with_eoc)) return false;
        content_len = resolved_content;
        total = header_size + resolved_total_with_eoc;
    } else {
        if (content_len > avail) return false;
        total = header_size + content_len;
    }

    out->tag_class = cls;
    out->constructed = constructed;
    out->tag = tag;
    out->header = data;
    out->header_size = header_size;
    out->content = content;
    out->content_size = content_len;
    out->total_size = total;
    out->indefinite = indefinite;
    *consumed = total;
    if (nonminimal) *nonminimal = nm;
    return true;
}

bool xx_asn1_parse_tlv_ex(const uint8_t *data, size_t size, unsigned depth, xx_asn1_tlv *out, size_t *consumed, bool *nonminimal)
{
    return parse_tlv_internal(data, size, depth, out, consumed, nonminimal, false);
}

bool xx_asn1_parse_any_tlv_ex(const uint8_t *data, size_t size, unsigned depth, xx_asn1_tlv *out, size_t *consumed, bool *nonminimal)
{
    return parse_tlv_internal(data, size, depth, out, consumed, nonminimal, true);
}

bool xx_asn1_parse_any_tlv(const uint8_t *data, size_t size, xx_asn1_tlv *out, size_t *consumed)
{
    return xx_asn1_parse_any_tlv_ex(data, size, 0U, out, consumed, NULL);
}

bool xx_asn1_parse_tlv(const uint8_t *data, size_t size, xx_asn1_tlv *out, size_t *consumed)
{
    return xx_asn1_parse_tlv_ex(data, size, 0U, out, consumed, NULL);
}

bool xx_asn1_is_eoc(const xx_asn1_tlv *tlv)
{
    return tlv != NULL && tlv->tag_class == XX_ASN1_CLASS_UNIVERSAL && tlv->tag == (uint32_t)XX_ASN1_TAG_EOC && !tlv->constructed && tlv->content_size == 0U;
}

bool xx_asn1_is_universal(const xx_asn1_tlv *tlv, uint32_t tag)
{
    return tlv != NULL && tlv->tag_class == XX_ASN1_CLASS_UNIVERSAL && tlv->tag == tag;
}

/* --------------------------------------------------------------- cursor -- */

bool xx_asn1_cursor_init(xx_asn1_cursor *cursor, const xx_asn1_tlv *parent, unsigned depth)
{
    if (!parent || !parent->constructed) return false;
    return xx_asn1_cursor_init_span(cursor, parent->content, parent->content_size, depth);
}

bool xx_asn1_cursor_init_span(xx_asn1_cursor *cursor, const uint8_t *data, size_t size, unsigned depth)
{
    if (!cursor || (!data && size != 0U)) return false;
    if (depth > XX_ASN1_MAX_DEPTH) return false;
    cursor->pos = data;
    cursor->limit = size != 0U ? data + size : data;
    cursor->depth = depth;
    return true;
}

bool xx_asn1_cursor_at_end(const xx_asn1_cursor *cursor)
{
    return !cursor || !cursor->pos || !cursor->limit || cursor->pos >= cursor->limit;
}

bool xx_asn1_cursor_next(xx_asn1_cursor *cursor, xx_asn1_tlv *out)
{
    size_t consumed;
    if (!cursor || !out || xx_asn1_cursor_at_end(cursor)) return false;
    if (!xx_asn1_parse_tlv_ex(cursor->pos, (size_t)(cursor->limit - cursor->pos), cursor->depth, out, &consumed, NULL)) return false;
    if (xx_asn1_is_eoc(out)) return false; /* Parent spans already exclude their terminator. */
    cursor->pos += consumed;
    return true;
}

/* ---------------------------------------------------------- basic types -- */

bool xx_asn1_read_boolean(const xx_asn1_tlv *tlv, bool *out)
{
    if (!tlv || !out || tlv->constructed) return false;
    if (tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL || tlv->tag != (uint32_t)XX_ASN1_TAG_BOOLEAN) return false;
    if (tlv->content_size != 1U || !tlv->content) return false;
    *out = (tlv->content[0] != 0U);
    return true;
}

bool xx_asn1_read_integer(const xx_asn1_tlv *tlv, const uint8_t **out_bytes, size_t *out_size)
{
    if (!tlv || tlv->constructed) return false;
    if (tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL || tlv->tag != (uint32_t)XX_ASN1_TAG_INTEGER) return false;
    if (tlv->content_size == 0U || !tlv->content) return false; /* X.690 8.3.1 */
    if (out_bytes) *out_bytes = tlv->content;
    if (out_size) *out_size = tlv->content_size;
    return true;
}

bool xx_asn1_integer_is_negative(const xx_asn1_tlv *tlv)
{
    const uint8_t *b;
    size_t n;
    if (!xx_asn1_read_integer(tlv, &b, &n)) return false;
    return (b[0] & 0x80U) != 0U;
}

bool xx_asn1_read_int64(const xx_asn1_tlv *tlv, int64_t *out)
{
    const uint8_t *b;
    size_t n, i;
    uint64_t value;
    if (!out) return false;
    if (!xx_asn1_read_integer(tlv, &b, &n)) return false;
    if (n > 8U) return false; /* does not fit a small integer */
    value = 0U;
    for (i = 0U; i < n; ++i) value = (value << 8) | (uint64_t)b[i];
    if (n < 8U && n > 0U) {
        uint64_t sign_bit = (uint64_t)1 << (n * 8U - 1U);
        if ((value & sign_bit) != 0U) value |= ~(((uint64_t)1 << (n * 8U)) - 1U);
    }
    *out = value <= (uint64_t)INT64_MAX ? (int64_t)value : -1 - (int64_t)(UINT64_MAX - value);
    return true;
}

bool xx_asn1_read_null(const xx_asn1_tlv *tlv)
{
    return tlv != NULL && !tlv->constructed && tlv->tag_class == XX_ASN1_CLASS_UNIVERSAL && tlv->tag == (uint32_t)XX_ASN1_TAG_NULL && tlv->content_size == 0U;
}

/* ------------------------------------------------------------------ OID -- */

bool xx_asn1_read_oid_raw(const xx_asn1_tlv *tlv, const uint8_t **out_bytes, size_t *out_size)
{
    if (!tlv || tlv->constructed) return false;
    if (tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL || tlv->tag != (uint32_t)XX_ASN1_TAG_OID) return false;
    if (tlv->content_size == 0U || !tlv->content) return false;
    if (out_bytes) *out_bytes = tlv->content;
    if (out_size) *out_size = tlv->content_size;
    return true;
}

bool xx_asn1_oid_to_text(const uint8_t *oid, size_t size, char *out, size_t cap, bool *nonminimal)
{
    size_t pos = 0U;
    size_t written = 0U;
    bool first_sub = true;
    bool nm = false;

    if (nonminimal) *nonminimal = false;
    if (!oid || size == 0U || !out || cap == 0U) return false;

    while (pos < size) {
        uint64_t value = 0U;
        bool first_byte = true;
        bool leading_zero = false;
        size_t start = pos;
        int n;
        bool first_overflow = false;

        for (;;) {
            uint8_t b;
            if (pos >= size) return false; /* truncated subidentifier */
            b = oid[pos++];
            if (first_byte && b == 0x80U) leading_zero = true;
            first_byte = false;
            if (value > (UINT64_MAX >> 7)) {
                /* The first subidentifier folds 2.arc2 as 80+arc2, so
                 * UINT64_MAX for arc2 needs one extra bit. It can overflow
                 * only on the last octet and by at most 80. */
                if (!first_sub || (b & 0x80U) != 0U || (value >> 57) != 1U) return false;
                first_overflow = true;
            }
            value = (value << 7) | (uint64_t)(b & 0x7FU);
            if (first_overflow && value > 79U) return false;
            if ((b & 0x80U) == 0U) break;
        }
        if (leading_zero && (pos - start) > 1U) nm = true;

        if (first_sub) {
            uint64_t arc1, arc2;
            if (first_overflow) arc1 = 2U;
            else if (value < 40U) arc1 = 0U;
            else if (value < 80U) arc1 = 1U;
            else arc1 = 2U;
            arc2 = first_overflow ? UINT64_MAX - (79U - value) : value - arc1 * 40U;
            n = snprintf(out + written, cap - written, "%llu.%llu", (unsigned long long)arc1, (unsigned long long)arc2);
            first_sub = false;
        } else {
            n = snprintf(out + written, cap - written, ".%llu", (unsigned long long)value);
        }
        if (n < 0 || (size_t)n >= cap - written) return false;
        written += (size_t)n;
    }
    if (first_sub) return false; /* no subidentifiers at all */
    if (nonminimal) *nonminimal = nm;
    return true;
}

bool xx_asn1_read_oid_text(const xx_asn1_tlv *tlv, char *out, size_t cap, bool *nonminimal)
{
    const uint8_t *b;
    size_t n;
    if (!xx_asn1_read_oid_raw(tlv, &b, &n)) return false;
    return xx_asn1_oid_to_text(b, n, out, cap, nonminimal);
}

/* ------------------------------------------------------------ BIT STRING -- */

bool xx_asn1_read_bit_string_primitive(const xx_asn1_tlv *tlv, unsigned *out_unused_bits, const uint8_t **out_bytes, size_t *out_size)
{
    uint8_t unused;
    if (!tlv || tlv->constructed) return false;
    if (tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL || tlv->tag != (uint32_t)XX_ASN1_TAG_BIT_STRING) return false;
    if (tlv->content_size < 1U || !tlv->content) return false;
    unused = tlv->content[0];
    if (unused > 7U) return false;
    if (unused != 0U && tlv->content_size == 1U) return false; /* unused bits claimed but no data byte */
    if (out_unused_bits) *out_unused_bits = unused;
    if (out_bytes) *out_bytes = tlv->content + 1;
    if (out_size) *out_size = tlv->content_size - 1U;
    return true;
}

/* Shared flattening for BIT STRING / OCTET STRING, primitive or BER-constructed. */
static bool flatten_string(const xx_asn1_tlv *tlv, uint32_t expected_tag, unsigned depth, uint8_t *out, size_t cap, size_t *out_size, unsigned *out_unused_bits)
{
    if (!tlv || depth > XX_ASN1_MAX_DEPTH || (!out && cap != 0U) ||
        (!tlv->content && tlv->content_size != 0U) ||
        tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL || tlv->tag != expected_tag) return false;

    if (!tlv->constructed) {
        if (expected_tag == (uint32_t)XX_ASN1_TAG_BIT_STRING) {
            unsigned unused = 0U;
            const uint8_t *bytes;
            size_t n;
            if (!xx_asn1_read_bit_string_primitive(tlv, &unused, &bytes, &n)) return false;
            if (n > cap) return false;
            if (n != 0U) memcpy(out, bytes, n);
            *out_size = n;
            if (out_unused_bits) *out_unused_bits = unused;
            return true;
        }
        if (tlv->content_size > cap) return false;
        if (tlv->content_size != 0U) memcpy(out, tlv->content, tlv->content_size);
        *out_size = tlv->content_size;
        if (out_unused_bits) *out_unused_bits = 0U;
        return true;
    }

    {
        xx_asn1_cursor cursor;
        size_t written = 0U;
        unsigned last_unused = 0U;
        bool saw_unused_before_last = false;
        bool any = false;

        if (depth > XX_ASN1_MAX_DEPTH) return false;
        if (!xx_asn1_cursor_init(&cursor, tlv, depth + 1U)) return false;

        while (!xx_asn1_cursor_at_end(&cursor)) {
            xx_asn1_tlv child;
            size_t piece_size;
            unsigned piece_unused = 0U;
            if (!xx_asn1_cursor_next(&cursor, &child)) return false;
            if (any && last_unused != 0U) saw_unused_before_last = true;
            if (written > cap) return false;
            if (!flatten_string(&child, expected_tag, depth + 1U, out ? out + written : NULL, cap - written, &piece_size, &piece_unused)) return false;
            written += piece_size;
            last_unused = piece_unused;
            any = true;
        }
        if (saw_unused_before_last) return false; /* X.690 8.6.4: only the LAST chunk may have unused bits */
        *out_size = written;
        if (out_unused_bits) *out_unused_bits = last_unused;
        return true;
    }
}

bool xx_asn1_read_bit_string_into(const xx_asn1_tlv *tlv, unsigned depth, uint8_t *out_buffer, size_t out_capacity, size_t *out_size, unsigned *out_unused_bits)
{
    if (!out_size) return false;
    return flatten_string(tlv, (uint32_t)XX_ASN1_TAG_BIT_STRING, depth, out_buffer, out_capacity, out_size, out_unused_bits);
}

/* --------------------------------------------------------- OCTET STRING -- */

bool xx_asn1_read_octet_string_primitive(const xx_asn1_tlv *tlv, const uint8_t **out_bytes, size_t *out_size)
{
    if (!tlv || tlv->constructed) return false;
    if (tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL || tlv->tag != (uint32_t)XX_ASN1_TAG_OCTET_STRING) return false;
    if (!tlv->content && tlv->content_size != 0U) return false;
    if (out_bytes) *out_bytes = tlv->content;
    if (out_size) *out_size = tlv->content_size;
    return true;
}

bool xx_asn1_read_octet_string_into(const xx_asn1_tlv *tlv, unsigned depth, uint8_t *out_buffer, size_t out_capacity, size_t *out_size)
{
    if (!out_size) return false;
    return flatten_string(tlv, (uint32_t)XX_ASN1_TAG_OCTET_STRING, depth, out_buffer, out_capacity, out_size, NULL);
}

/* ----------------------------------------------------------------- time -- */

static bool is_leap_year(int64_t y)
{
    return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
}

/* Count complete years and months, then subtract the count at 1970-01-01.
 * Years are bounded to 0000..9999 by the four-digit time syntax. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    static const unsigned preceding_days[12] = {0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U};
    int64_t days = 365 * y + (y + 3) / 4 - (y + 99) / 100 + (y + 399) / 400;
    days += (int64_t)preceding_days[m - 1U] + (int64_t)d - 1;
    if (m > 2U && is_leap_year(y)) ++days;
    return days - 719528;
}

bool xx_asn1_read_time(const xx_asn1_tlv *tlv, int64_t *out)
{
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint8_t *s;
    size_t n, i;
    bool is_generalized;
    bool has_seconds = false;
    int64_t year;
    unsigned month, day, hour, minute, second = 0U, maxd;
    int tz_sign = 0;
    unsigned tz_hour = 0U, tz_minute = 0U;

    if (!tlv || !out || tlv->constructed) return false;
    if (tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL) return false;
    if (tlv->tag == (uint32_t)XX_ASN1_TAG_UTCTIME) is_generalized = false;
    else if (tlv->tag == (uint32_t)XX_ASN1_TAG_GENERALIZEDTIME) is_generalized = true;
    else return false;

    s = tlv->content;
    n = tlv->content_size;
    if (!s) return false;
    i = 0U;

#define XX_ASN1_TIME_DIGIT(idx) ((unsigned)(s[idx]) - (unsigned)'0')
    if (is_generalized) {
        if (i + 4U > n) return false;
        if (s[i] < '0' || s[i] > '9' || s[i + 1] < '0' || s[i + 1] > '9' || s[i + 2] < '0' || s[i + 2] > '9' || s[i + 3] < '0' || s[i + 3] > '9') return false;
        year = (int64_t)(XX_ASN1_TIME_DIGIT(i) * 1000U + XX_ASN1_TIME_DIGIT(i + 1U) * 100U + XX_ASN1_TIME_DIGIT(i + 2U) * 10U + XX_ASN1_TIME_DIGIT(i + 3U));
        i += 4U;
    } else {
        unsigned yy;
        if (i + 2U > n) return false;
        if (s[i] < '0' || s[i] > '9' || s[i + 1] < '0' || s[i + 1] > '9') return false;
        yy = XX_ASN1_TIME_DIGIT(i) * 10U + XX_ASN1_TIME_DIGIT(i + 1U);
        i += 2U;
        year = (yy >= 50U) ? (int64_t)(1900U + yy) : (int64_t)(2000U + yy);
    }

    if (i + 8U > n) return false; /* MMDDHHMM: month, day, hour, minute all mandatory */
    {
        unsigned k;
        for (k = 0U; k < 8U; ++k) {
            if (s[i + k] < '0' || s[i + k] > '9') return false;
        }
    }
    month = XX_ASN1_TIME_DIGIT(i) * 10U + XX_ASN1_TIME_DIGIT(i + 1U);
    day = XX_ASN1_TIME_DIGIT(i + 2U) * 10U + XX_ASN1_TIME_DIGIT(i + 3U);
    hour = XX_ASN1_TIME_DIGIT(i + 4U) * 10U + XX_ASN1_TIME_DIGIT(i + 5U);
    minute = XX_ASN1_TIME_DIGIT(i + 6U) * 10U + XX_ASN1_TIME_DIGIT(i + 7U);
    i += 8U;

    if (month < 1U || month > 12U) return false;
    if (hour > 23U) return false;
    if (minute > 59U) return false;
    maxd = days_in_month[month - 1U];
    if (month == 2U && is_leap_year(year)) maxd = 29U;
    if (day < 1U || day > maxd) return false;

    if (i + 2U <= n && s[i] >= '0' && s[i] <= '9' && s[i + 1] >= '0' && s[i + 1] <= '9') {
        second = XX_ASN1_TIME_DIGIT(i) * 10U + XX_ASN1_TIME_DIGIT(i + 1U);
        if (second > 59U) return false;
        i += 2U;
        has_seconds = true;
    }

    if (is_generalized && i < n && s[i] == '.') {
        if (!has_seconds) return false; /* A minute fraction is not a seconds fraction. */
        size_t fraction_start;
        ++i; fraction_start = i;
        while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
        if (i - fraction_start > 3U) return false; /* asn1-rs 0.7.2 milliseconds */
    }

    if (i >= n) return false;
    if (s[i] == 'Z') {
        tz_sign = 0;
        ++i;
    } else if (s[i] == '+' || s[i] == '-') {
        if (is_generalized || !has_seconds) return false;
        tz_sign = (s[i] == '+') ? 1 : -1;
        ++i;
        if (i + 4U > n) return false;
        if (s[i] < '0' || s[i] > '9' || s[i + 1] < '0' || s[i + 1] > '9' || s[i + 2] < '0' || s[i + 2] > '9' || s[i + 3] < '0' || s[i + 3] > '9') return false;
        tz_hour = XX_ASN1_TIME_DIGIT(i) * 10U + XX_ASN1_TIME_DIGIT(i + 1U);
        tz_minute = XX_ASN1_TIME_DIGIT(i + 2U) * 10U + XX_ASN1_TIME_DIGIT(i + 3U);
        i += 4U;
        if (tz_hour > 25U || tz_minute > 59U) return false;
    } else {
        return false; /* no zone: cannot resolve to a definite UTC instant */
    }
    if (i != n) return false; /* trailing junk */
#undef XX_ASN1_TIME_DIGIT

    {
        int64_t days = days_from_civil(year, month, day);
        int64_t result = days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + (int64_t)second;
        if (tz_sign > 0) result -= (int64_t)tz_hour * 3600 + (int64_t)tz_minute * 60;
        else if (tz_sign < 0) result += (int64_t)tz_hour * 3600 + (int64_t)tz_minute * 60;
        *out = result;
    }
    return true;
}

/* ----------------------------------------------------------------- misc -- */

bool xx_asn1_is_string_tag(uint32_t tag)
{
    switch (tag) {
    case (uint32_t)XX_ASN1_TAG_UTF8_STRING:
    case (uint32_t)XX_ASN1_TAG_NUMERIC_STRING:
    case (uint32_t)XX_ASN1_TAG_PRINTABLE_STRING:
    case (uint32_t)XX_ASN1_TAG_T61_STRING:
    case (uint32_t)XX_ASN1_TAG_VIDEOTEX_STRING:
    case (uint32_t)XX_ASN1_TAG_IA5_STRING:
    case (uint32_t)XX_ASN1_TAG_GRAPHIC_STRING:
    case (uint32_t)XX_ASN1_TAG_VISIBLE_STRING:
    case (uint32_t)XX_ASN1_TAG_GENERAL_STRING:
    case (uint32_t)XX_ASN1_TAG_UNIVERSAL_STRING:
    case (uint32_t)XX_ASN1_TAG_CHARACTER_STRING:
    case (uint32_t)XX_ASN1_TAG_BMP_STRING:
        return true;
    default:
        return false;
    }
}

const char *xx_asn1_tag_name(xx_asn1_class tag_class, uint32_t tag)
{
    if (tag_class != XX_ASN1_CLASS_UNIVERSAL) return "CONTEXT/APPLICATION/PRIVATE";
    switch (tag) {
    case (uint32_t)XX_ASN1_TAG_EOC: return "EOC";
    case (uint32_t)XX_ASN1_TAG_BOOLEAN: return "BOOLEAN";
    case (uint32_t)XX_ASN1_TAG_INTEGER: return "INTEGER";
    case (uint32_t)XX_ASN1_TAG_BIT_STRING: return "BIT STRING";
    case (uint32_t)XX_ASN1_TAG_OCTET_STRING: return "OCTET STRING";
    case (uint32_t)XX_ASN1_TAG_NULL: return "NULL";
    case (uint32_t)XX_ASN1_TAG_OID: return "OBJECT IDENTIFIER";
    case (uint32_t)XX_ASN1_TAG_ENUMERATED: return "ENUMERATED";
    case (uint32_t)XX_ASN1_TAG_UTF8_STRING: return "UTF8String";
    case (uint32_t)XX_ASN1_TAG_SEQUENCE: return "SEQUENCE";
    case (uint32_t)XX_ASN1_TAG_SET: return "SET";
    case (uint32_t)XX_ASN1_TAG_PRINTABLE_STRING: return "PrintableString";
    case (uint32_t)XX_ASN1_TAG_T61_STRING: return "T61String";
    case (uint32_t)XX_ASN1_TAG_IA5_STRING: return "IA5String";
    case (uint32_t)XX_ASN1_TAG_UTCTIME: return "UTCTime";
    case (uint32_t)XX_ASN1_TAG_GENERALIZEDTIME: return "GeneralizedTime";
    case (uint32_t)XX_ASN1_TAG_BMP_STRING: return "BMPString";
    default: return "UNKNOWN";
    }
}

bool xx_asn1_read_string_primitive(const xx_asn1_tlv *tlv, uint32_t *out_tag, const uint8_t **out_bytes, size_t *out_size)
{
    if (!tlv || tlv->constructed || tlv->tag_class != XX_ASN1_CLASS_UNIVERSAL ||
        !xx_asn1_is_string_tag(tlv->tag) || (!tlv->content && tlv->content_size != 0U)) return false;
    if (out_tag) *out_tag = tlv->tag;
    if (out_bytes) *out_bytes = tlv->content;
    if (out_size) *out_size = tlv->content_size;
    return true;
}

/* ------------------------------------------------------------ re-encode -- */

size_t xx_asn1_encode_header(xx_asn1_class cls, bool constructed, uint32_t tag, size_t content_size, uint8_t *out, size_t out_capacity)
{
    uint8_t buf[16];
    size_t n = 0U;

    if ((unsigned)cls > (unsigned)XX_ASN1_CLASS_PRIVATE) return 0U;
    if (tag <= 30U) {
        buf[n++] = (uint8_t)(((unsigned)cls << 6) | (constructed ? 0x20U : 0U) | (tag & 0x1FU));
    } else {
        uint8_t tagbytes[8];
        unsigned tn = 0U;
        uint32_t t = tag;
        if (n >= sizeof(buf)) return 0U;
        buf[n++] = (uint8_t)(((unsigned)cls << 6) | (constructed ? 0x20U : 0U) | 0x1FU);
        do {
            if (tn >= (unsigned)sizeof(tagbytes)) return 0U;
            tagbytes[tn++] = (uint8_t)(t & 0x7FU);
            t >>= 7;
        } while (t != 0U);
        while (tn > 0U) {
            --tn;
            if (n >= sizeof(buf)) return 0U;
            buf[n++] = (uint8_t)(tagbytes[tn] | (tn > 0U ? 0x80U : 0U));
        }
    }

    if (content_size < 128U) {
        if (n >= sizeof(buf)) return 0U;
        buf[n++] = (uint8_t)content_size;
    } else {
        uint8_t lb[8];
        unsigned ln = 0U;
        uint64_t v = (uint64_t)content_size;
        do {
            if (ln >= (unsigned)sizeof(lb)) return 0U;
            lb[ln++] = (uint8_t)(v & 0xFFU);
            v >>= 8;
        } while (v != 0U);
        if (n + 1U + ln > sizeof(buf)) return 0U;
        buf[n++] = (uint8_t)(0x80U | ln);
        while (ln > 0U) {
            --ln;
            buf[n++] = lb[ln];
        }
    }

    if (!out || n > out_capacity) return 0U;
    memcpy(out, buf, n);
    return n;
}

bool xx_asn1_reencode(const xx_asn1_tlv *tlv, xx_asn1_class new_class, bool new_constructed, uint32_t new_tag, uint8_t *out, size_t out_capacity, size_t *out_size)
{
    size_t header_size;
    uint8_t header[16];
    if (!tlv || !out || !out_size || (!tlv->content && tlv->content_size != 0U)) return false;
    header_size = xx_asn1_encode_header(new_class, new_constructed, new_tag, tlv->content_size, header, sizeof(header));
    if (header_size == 0U) return false;
    if (header_size > out_capacity || tlv->content_size > out_capacity - header_size) return false;
    if (tlv->content_size != 0U) memmove(out + header_size, tlv->content, tlv->content_size);
    memcpy(out, header, header_size);
    *out_size = header_size + tlv->content_size;
    return true;
}
