/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/uil.c
 * Motif UIL static icon tables: complete typed color-table/icon definitions, unique identifiers/symbols and resolved color-table references, equal bounded row widths and defined pixel symbols. Original palette/icon records and decoded RGBA bitmap exported; arbitrary UIL modules/procedures/includes declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/motif_uil/xx_motif_uil.h"
#include "../common/xx_component_lexer.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[9];
    return n >= 32 && pm_read(f, 0, b, 9) && component_tag(b, "/* UIL */", 9);
}
static unsigned image_document_uil_hex(uint8_t c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 16;
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_lexer q = {b, 0, n, pd, 0, false, false, true}, rows;
    uint64_t table, z, icon, iz, start, at, len;
    uint32_t colors[256];
    uint8_t defined[256];
    uint32_t count = 0, w = 0, h = 0, i;
    uint8_t *pixels = NULL;
    bool ok = false;
    if (!component_utf8(b, n, true, pd) || !component_lexer_keyword_hash_cpp_comments(&q, "value"))
        return false;
    xx_mem_zero(defined, sizeof(defined));
    if (!component_lexer_skip_hash_cpp_comments(&q))
        return false;
    start = q.p;
    if (!component_lexer_identifier_hash_cpp_comments(&q, &table, &z) ||
        !component_lexer_char_hash_cpp_comments(&q, ':') ||
        !component_lexer_keyword_hash_cpp_comments(&q, "color_table") ||
        !component_lexer_char_hash_cpp_comments(&q, '('))
        return false;
    do {
        uint32_t rgba = 0, symbol;
        unsigned j;
        if (!component_lexer_keyword_hash_cpp_comments(&q, "color") ||
            !component_lexer_char_hash_cpp_comments(&q, '(') ||
            !component_lexer_quoted_hash_cpp_comments(&q, '\'', &at, &len) || (len != 7 && len != 9) || b[at] != '#')
            return false;
        for (j = 1; j < len; ++j) {
            unsigned d = image_document_uil_hex(b[at + j]);
            if (d > 15)
                return false;
            rgba = (rgba << 4) | d;
        }
        if (len == 7)
            rgba = (rgba << 8) | 255;
        if (!component_lexer_char_hash_cpp_comments(&q, ','))
            return false;
        {
            component_lexer copy = q;
            if (!component_lexer_keyword_hash_cpp_comments(&q, "background")) {
                q = copy;
                if (!component_lexer_keyword_hash_cpp_comments(&q, "foreground"))
                    return false;
            }
        }
        if (!component_lexer_char_hash_cpp_comments(&q, ')') || !component_lexer_char_hash_cpp_comments(&q, '=') ||
            !component_lexer_quoted_hash_cpp_comments(&q, '\'', &at, &len) || len != 1 || (symbol = b[at]) < 32 ||
            symbol > 126 || defined[symbol] || ++count > 256)
            return false;
        defined[symbol] = 1;
        colors[symbol] = rgba;
        if (!component_lexer_skip_hash_cpp_comments(&q))
            return false;
        if (q.p < n && b[q.p] == ',') {
            ++q.p;
            continue;
        }
        break;
    } while (true);
    if (!count || !component_lexer_char_hash_cpp_comments(&q, ')') ||
        !component_lexer_char_hash_cpp_comments(&q, ';') ||
        !component_emit(f, s, "color-table.uil", start, q.p - start, n) || !component_lexer_skip_hash_cpp_comments(&q))
        return false;
    start = q.p;
    if (!component_lexer_identifier_hash_cpp_comments(&q, &icon, &iz) ||
        (iz == z && component_tag(b + icon, (const char *)b + table, (size_t)z)) ||
        !component_lexer_char_hash_cpp_comments(&q, ':') || !component_lexer_keyword_hash_cpp_comments(&q, "icon") ||
        !component_lexer_char_hash_cpp_comments(&q, '(') ||
        !component_lexer_keyword_hash_cpp_comments(&q, "color_table") ||
        !component_lexer_char_hash_cpp_comments(&q, '=') ||
        !component_lexer_identifier_hash_cpp_comments(&q, &at, &len) || len != z ||
        !component_tag(b + at, (const char *)b + table, (size_t)z) || !component_lexer_char_hash_cpp_comments(&q, ','))
        return false;
    rows = q;
    while (true) {
        if (!component_lexer_quoted_hash_cpp_comments(&q, '"', &at, &len) || !len || len > 8192 || (++h) > 4093)
            return false;
        if (!w)
            w = (uint32_t)len;
        else if (len != w)
            return false;
        for (i = 0; i < w; ++i)
            if (!defined[b[at + i]])
                return false;
        if (!component_lexer_skip_hash_cpp_comments(&q))
            return false;
        if (q.p < n && b[q.p] == ',') {
            ++q.p;
            continue;
        }
        break;
    }
    if (!component_lexer_char_hash_cpp_comments(&q, ')') || !component_lexer_char_hash_cpp_comments(&q, ';') ||
        !component_lexer_end_hash_cpp_comments(&q) || (uint64_t)w * h > 8388608 ||
        !component_emit(f, s, "icon.uil", start, q.p - start, n))
        return false;
    pixels = (uint8_t *)xx_mem_alloc((size_t)w * h * 4);
    if (!pixels)
        return false;
    for (i = 0; i < h; ++i) {
        uint32_t j;
        if (!component_lexer_quoted_hash_cpp_comments(&rows, '"', &at, &len) || len != w)
            goto done;
        for (j = 0; j < w; ++j) {
            uint32_t c = colors[b[at + j]];
            uint64_t p = ((uint64_t)i * w + j) * 4;
            pixels[p] = (uint8_t)(c >> 24);
            pixels[p + 1] = (uint8_t)(c >> 16);
            pixels[p + 2] = (uint8_t)(c >> 8);
            pixels[p + 3] = (uint8_t)c;
        }
        if (i + 1 < h && !component_lexer_char_hash_cpp_comments(&rows, ','))
            goto done;
    }
    if (!component_publish_memory_keep_on_failure(f, s, "decoded-rgba8.bin", pixels, (uint64_t)w * h * 4))
        goto done;
    pixels = NULL;
    ok = component_cover(f, s, "uil-framing.txt", n);
done:
    xx_mem_free(pixels);
    return ok;
}

void xx_motif_uil_init(xx_motif_uil *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_MOTIF_UIL, "uil");
    }
}
xx_motif_uil *xx_motif_uil_create(xx_io_device *d, int64_t at) {
    xx_motif_uil *r = (xx_motif_uil *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_motif_uil_init(r, d, at);
    return r;
}
void xx_motif_uil_destroy(xx_motif_uil *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_motif_uil_free(xx_motif_uil *r) {
    if (r) {
        xx_motif_uil_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_motif_uil_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_motif_uil_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
