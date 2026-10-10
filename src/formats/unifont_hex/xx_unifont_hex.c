/* SPDX-License-Identifier: MIT
 * Primary reference: https://unifoundry.com/unifont/index.html
 * GNU Unifont HEX: complete ascending unique Unicode scalar codepoint records with exact8x16 or16x16 monochrome bitmap hex data; original natural256-codepoint-page components exported; unsupported glyph dimensions/comments/dialects declined. Signatureless fallback after structured readers.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/unifont_hex/xx_unifont_hex.h"
#include "../common/xx_component_binary.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static unsigned image_document_hex(uint8_t c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 16;
}
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[7];
    unsigned i;
    if (n < 38 || !pm_read(f, 0, b, 7))
        return false;
    for (i = 0; i < 7; ++i) {
        if (b[i] == ':')
            return i >= 4 && i <= 6;
        if (image_document_hex(b[i]) >= 16)
            return false;
    }
    return false;
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    uint64_t p = 0, start = 0;
    uint32_t page = 0, previous = 0, count = 0;
    char label[48];
    while (p < n) {
        uint64_t at = p;
        uint32_t code = 0, digits = 0, bits = 0;
        while (p < n && b[p] != ':') {
            unsigned d = image_document_hex(b[p++]);
            if (d >= 16 || ++digits > 6)
                return false;
            code = (code << 4) | d;
        }
        if (digits < 4 || p == n || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
            (count && code <= previous) || ++count > 65536)
            return false;
        ++p;
        while (p < n && b[p] != 10 && b[p] != 13) {
            if (image_document_hex(b[p++]) >= 16 || ++bits > 64)
                return false;
        }
        if ((bits != 32 && bits != 64) || p == n || xx_component_parser_stopped(pd))
            return false;
        if (b[p] == 13) {
            ++p;
            if (p == n || b[p] != 10)
                return false;
        }
        ++p;
        if (count == 1)
            page = code >> 8;
        else if (page != (code >> 8)) {
            xx_rt_snprintf(label, sizeof(label), "unicode-page-%04x.hex", page);
            if (!component_emit(f, s, label, start, at - start, n))
                return false;
            start = at;
            page = code >> 8;
        }
        previous = code;
    }
    if (!count)
        return false;
    xx_rt_snprintf(label, sizeof(label), "unicode-page-%04x.hex", page);
    return component_emit(f, s, label, start, n - start, n);
}

void xx_unifont_hex_init(xx_unifont_hex *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_UNIFONT_HEX, "hex");
    }
}
xx_unifont_hex *xx_unifont_hex_create(xx_io_device *d, int64_t at) {
    xx_unifont_hex *r = (xx_unifont_hex *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_unifont_hex_init(r, d, at);
    return r;
}
void xx_unifont_hex_destroy(xx_unifont_hex *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_unifont_hex_free(xx_unifont_hex *r) {
    if (r) {
        xx_unifont_hex_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_unifont_hex_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_unifont_hex_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
