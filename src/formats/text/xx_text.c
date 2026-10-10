/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Generic original-byte text reader, corresponding to Formats/texts/XText.
 * Intentionally conservative: validates every Unicode scalar across the full
 * stream, rejects binary controls, and only guesses BOM-less UTF-16 with strong
 * alternating-zero evidence. No global signature registration is supplied.
 */
#include "xxfclib/formats/text/xx_text.h"
#include "../xx_payload_members.h"

typedef struct text_cursor {
    Abstractformat *format;
    int64_t at, total;
    size_t used, count;
    uint8_t buffer[4096];
} text_cursor;

static bool text_byte(text_cursor *c, uint8_t *value)
{
    if (c->at >= c->total) return false;
    if (c->used == c->count) {
        c->count = (size_t)(c->total - c->at < (int64_t)sizeof(c->buffer) ? c->total - c->at : (int64_t)sizeof(c->buffer));
        c->used = 0;
        if (!pm_read(c->format, c->at, c->buffer, c->count)) return false;
    }
    *value = c->buffer[c->used++];
    ++c->at;
    return true;
}

static bool text_scalar(uint32_t c)
{
    return c <= 0x10ffffU && !(c >= 0xd800U && c <= 0xdfffU) && ((c >= 0x20U && c != 0x7fU) || c == 9U || c == 10U || c == 13U);
}

static bool text_utf8(text_cursor *c, uint32_t *scalar)
{
    uint8_t b;
    uint32_t value, minimum;
    unsigned left;
    if (!text_byte(c, &b)) return false;
    if (b < 0x80U) {
        *scalar = b;
        return true;
    }
    if (b >= 0xc2U && b <= 0xdfU) {
        value = b & 0x1fU;
        minimum = 0x80U;
        left = 1;
    } else if (b >= 0xe0U && b <= 0xefU) {
        value = b & 0x0fU;
        minimum = 0x800U;
        left = 2;
    } else if (b >= 0xf0U && b <= 0xf4U) {
        value = b & 7U;
        minimum = 0x10000U;
        left = 3;
    } else return false;
    while (left--) {
        if (!text_byte(c, &b) || (b & 0xc0U) != 0x80U) return false;
        value = (value << 6) | (b & 0x3fU);
    }
    if (value < minimum) return false;
    *scalar = value;
    return true;
}

static bool text_u16(text_cursor *c, bool big, uint16_t *unit)
{
    uint8_t a, b;
    if (!text_byte(c, &a) || !text_byte(c, &b)) return false;
    *unit = big ? (uint16_t)((uint16_t)a << 8 | b) : (uint16_t)((uint16_t)b << 8 | a);
    return true;
}

static bool text_utf16(text_cursor *c, bool big, uint32_t *scalar)
{
    uint16_t unit, low;
    if (!text_u16(c, big, &unit)) return false;
    if (unit >= 0xd800U && unit <= 0xdbffU) {
        if (!text_u16(c, big, &low) || low < 0xdc00U || low > 0xdfffU) return false;
        *scalar = 0x10000U + ((uint32_t)(unit - 0xd800U) << 10) + (low - 0xdc00U);
    } else {
        if (unit >= 0xdc00U && unit <= 0xdfffU) return false;
        *scalar = unit;
    }
    return true;
}

static bool text_utf32(text_cursor *c, bool big, uint32_t *scalar)
{
    uint8_t b[4];
    unsigned i;
    for (i = 0; i < 4; ++i)
        if (!text_byte(c, &b[i])) return false;
    *scalar =
        big ? ((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]) : ((uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0]);
    return true;
}

static bool text_ascii_byte(uint8_t c)
{
    return (c >= 0x20U && c <= 0x7eU) || c == 9 || c == 10 || c == 13;
}

/* Only identify BOM-less UTF-16 from an even sample with at least two Latin
 * text code units whose high byte is zero. General non-Latin BOM-less UTF-16
 * is ambiguous with binary data and intentionally requires an explicit BOM. */
static xx_text_encoding text_utf16_hint(Abstractformat *f, int64_t total)
{
    uint8_t sample[4096];
    size_t i, n, le = 0, be = 0, units;
    if (total < 4 || (total & 1)) return XX_TEXT_ENCODING_UNKNOWN;
    n = (size_t)(total < (int64_t)sizeof(sample) ? total : (int64_t)sizeof(sample));
    if (!pm_read(f, 0, sample, n)) return XX_TEXT_ENCODING_UNKNOWN;
    units = n / 2;
    for (i = 0; i < n; i += 2) {
        if (sample[i + 1] == 0 && text_ascii_byte(sample[i])) ++le;
        if (sample[i] == 0 && text_ascii_byte(sample[i + 1])) ++be;
    }
    if (le >= 2 && le * 2 >= units && be == 0) return XX_TEXT_ENCODING_UTF16_LE;
    if (be >= 2 && be * 2 >= units && le == 0) return XX_TEXT_ENCODING_UTF16_BE;
    return XX_TEXT_ENCODING_UNKNOWN;
}

static bool text_analyze(Abstractformat *f, xx_text_info *info, xx_pd_struct *pd)
{
    uint8_t prefix[4];
    size_t prefix_size;
    uint32_t scalar;
    bool ascii = true;
    text_cursor cursor;
    int64_t total = pm_available(f);
    xx_mem_zero(info, sizeof(*info));
    if ((pd && xx_pd_is_stopped(pd)) || total <= 0) return false;
    prefix_size = (size_t)(total < 4 ? total : 4);
    if (!pm_read(f, 0, prefix, prefix_size)) return false;
    if (prefix_size >= 4 && prefix[0] == 0xff && prefix[1] == 0xfe && prefix[2] == 0 && prefix[3] == 0) {
        info->encoding = XX_TEXT_ENCODING_UTF32_LE;
        info->bom_size = 4;
    } else if (prefix_size >= 4 && prefix[0] == 0 && prefix[1] == 0 && prefix[2] == 0xfe && prefix[3] == 0xff) {
        info->encoding = XX_TEXT_ENCODING_UTF32_BE;
        info->bom_size = 4;
    } else if (prefix_size >= 3 && prefix[0] == 0xef && prefix[1] == 0xbb && prefix[2] == 0xbf) {
        info->encoding = XX_TEXT_ENCODING_UTF8;
        info->bom_size = 3;
    } else if (prefix_size >= 2 && prefix[0] == 0xff && prefix[1] == 0xfe) {
        info->encoding = XX_TEXT_ENCODING_UTF16_LE;
        info->bom_size = 2;
    } else if (prefix_size >= 2 && prefix[0] == 0xfe && prefix[1] == 0xff) {
        info->encoding = XX_TEXT_ENCODING_UTF16_BE;
        info->bom_size = 2;
    } else {
        info->encoding = text_utf16_hint(f, total);
        if (info->encoding == XX_TEXT_ENCODING_UNKNOWN) info->encoding = XX_TEXT_ENCODING_UTF8;
    }
    info->has_bom = info->bom_size != 0;
    xx_mem_zero(&cursor, sizeof(cursor));
    cursor.format = f;
    cursor.at = info->bom_size;
    cursor.total = total;
    while (cursor.at < total) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        switch (info->encoding) {
            case XX_TEXT_ENCODING_UTF8:
                if (!text_utf8(&cursor, &scalar)) return false;
                break;
            case XX_TEXT_ENCODING_UTF16_LE:
            case XX_TEXT_ENCODING_UTF16_BE:
                if (!text_utf16(&cursor, info->encoding == XX_TEXT_ENCODING_UTF16_BE, &scalar)) return false;
                break;
            case XX_TEXT_ENCODING_UTF32_LE:
            case XX_TEXT_ENCODING_UTF32_BE:
                if (!text_utf32(&cursor, info->encoding == XX_TEXT_ENCODING_UTF32_BE, &scalar)) return false;
                break;
            default: return false;
        }
        if (!text_scalar(scalar)) return false;
        if (scalar > 0x7fU) ascii = false;
        ++info->character_count;
    }
    if (info->encoding == XX_TEXT_ENCODING_UTF8 && !info->has_bom && ascii) info->encoding = XX_TEXT_ENCODING_ASCII;
    return !(pd && xx_pd_is_stopped(pd));
}

const char *xx_text_encoding_name(xx_text_encoding encoding)
{
    switch (encoding) {
        case XX_TEXT_ENCODING_ASCII: return "ASCII";
        case XX_TEXT_ENCODING_UTF8: return "UTF-8";
        case XX_TEXT_ENCODING_UTF16_LE: return "UTF-16LE";
        case XX_TEXT_ENCODING_UTF16_BE: return "UTF-16BE";
        case XX_TEXT_ENCODING_UTF32_LE: return "UTF-32LE";
        case XX_TEXT_ENCODING_UTF32_BE: return "UTF-32BE";
        default: return "Unknown";
    }
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_text_info info;
    const char *mime;
    int64_t total = pm_available(f);
    if (!text_analyze(f, &info, pd)) return false;
    if (info.has_bom && !pm_add(f, s, "bom.bin", 0, info.bom_size)) return false;
    if (!pm_add(f, s, "content.bin", info.bom_size, total - info.bom_size)) return false;
    ((xx_text *)f)->info = info;
    xx_format_set_version(f, xx_text_encoding_name(info.encoding));
    switch (info.encoding) {
        case XX_TEXT_ENCODING_ASCII: mime = "text/plain; charset=us-ascii"; break;
        case XX_TEXT_ENCODING_UTF8: mime = "text/plain; charset=utf-8"; break;
        case XX_TEXT_ENCODING_UTF16_LE: mime = "text/plain; charset=utf-16le"; break;
        case XX_TEXT_ENCODING_UTF16_BE: mime = "text/plain; charset=utf-16be"; break;
        case XX_TEXT_ENCODING_UTF32_LE: mime = "text/plain; charset=utf-32le"; break;
        case XX_TEXT_ENCODING_UTF32_BE: mime = "text/plain; charset=utf-32be"; break;
        default: return false;
    }
    f->endian = info.encoding == XX_TEXT_ENCODING_UTF16_LE || info.encoding == XX_TEXT_ENCODING_UTF32_LE   ? XX_ENDIAN_LITTLE
                : info.encoding == XX_TEXT_ENCODING_UTF16_BE || info.encoding == XX_TEXT_ENCODING_UTF32_BE ? XX_ENDIAN_BIG
                                                                                                           : XX_ENDIAN_UNKNOWN;
    xx_format_set_mime_type(f, mime);
    s->size = total;
    return true;
}

static const char *text_version(Abstractformat *f)
{
    return f && (f->base_info_handled || pm_handle(f, NULL)) ? f->version : "";
}
static const char *text_mime(Abstractformat *f)
{
    return f && (f->base_info_handled || pm_handle(f, NULL)) ? f->mime_type : "";
}
void xx_text_init(xx_text *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TEXT, "txt");
        r->format.is_archive = false;
        r->format.format_type = XX_TYPE_RAW;
        r->format.get_version = text_version;
        r->format.get_mime_type = text_mime;
    }
}
xx_text *xx_text_create(xx_io_device *d, int64_t b)
{
    xx_text *r = (xx_text *)xx_mem_alloc(sizeof(*r));
    if (r) xx_text_init(r, d, b);
    return r;
}
void xx_text_destroy(xx_text *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_text_free(xx_text *r)
{
    if (r) {
        xx_text_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_text_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_text_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
bool xx_text_get_info(xx_text *r, xx_text_info *out, xx_pd_struct *pd)
{
    if (out) xx_mem_zero(out, sizeof(*out));
    if (!r || !out || !pm_valid(&r->format, pd)) return false;
    *out = r->info;
    return true;
}
xx_file_type_t xx_text_detect(xx_io_device *d, int64_t b)
{
    xx_text r;
    bool valid;
    xx_text_init(&r, d, b);
    valid = pm_valid(&r.format, NULL);
    xx_text_destroy(&r);
    return valid ? XX_FILE_TYPE_TEXT : XX_FILE_TYPE_UNKNOWN;
}
