/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/freetype/freetype/master/src/bdf/bdflib.c
 * ASCII newline-terminated BDF2.1/2.2 horizontal bitmap fonts with complete traditional global/property/glyph grammar and single ENCODING values, bounded integral
 * metrics and exact hexadecimal bitmap rows. Original glyph programs and global metadata remain encoded; other BDF2.2 metrics/secondary encodings, vertical metrics and
 * rendering are unsupported. Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/font_bdf/xx_font_bdf.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[13];
    return texture_font_probe(f, n, b, 13) && (pm_tag(b, "STARTFONT 2.1", 13) || pm_tag(b, "STARTFONT 2.2", 13));
}
static bool bd_next(texture_font_text *q, xx_pd_struct *pd)
{
    while (texture_font_line(q)) {
        if (texture_font_stop(pd)) return false;
        if (texture_font_line_tag(q, "COMMENT", 7)) continue;
        return true;
    }
    return false;
}
static bool bd_nums(const texture_font_text *q, const char *tag, unsigned bytes, int32_t *v, unsigned count)
{
    return texture_font_line_tag(q, tag, bytes) && texture_font_ints(q->b, q->start + bytes, q->start + q->len, v, count);
}
static bool bd_hex(uint8_t c, uint32_t *v)
{
    if (c >= '0' && c <= '9') *v = c - '0';
    else if (c >= 'a' && c <= 'f') *v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') *v = c - 'A' + 10;
    else return false;
    return true;
}
static bool bd_property(const texture_font_text *q)
{
    uint64_t p = q->start, end = p + q->len;
    int32_t num;
    if (p == end) return false;
    while (p < end && q->b[p] != 32 && q->b[p] != 9) {
        uint8_t c = q->b[p++];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    while (p < end && (q->b[p] == 32 || q->b[p] == 9)) ++p;
    if (p == end) return false;
    if (q->b[p] != '"') return texture_font_ints(q->b, p, end, &num, 1);
    ++p;
    while (p < end) {
        if (q->b[p++] == '"') {
            if (p < end && q->b[p] == '"') {
                ++p;
                continue;
            }
            while (p < end && (q->b[p] == 32 || q->b[p] == 9)) ++p;
            return p == end;
        }
    }
    return false;
}
static bool texture_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    texture_font_text q = {b, 0, n, 0, 0};
    int32_t v[4], global[4];
    uint32_t count, i, j;
    uint64_t start, header;
    char label[64];
    int32_t *encodings;
    bool result = false;
    if (!bd_next(&q, pd) || q.len != 13 || !bd_next(&q, pd) || !texture_font_line_tag(&q, "FONT ", 5) || q.len <= 5 || !bd_next(&q, pd) ||
        !bd_nums(&q, "SIZE ", 5, v, 3) || v[0] < 1 || v[0] > 4096 || v[1] < 1 || v[2] < 1 || !bd_next(&q, pd) || !bd_nums(&q, "FONTBOUNDINGBOX ", 16, global, 4) ||
        global[0] < 0 || global[0] > 4096 || global[1] < 1 || global[1] > 4096 || global[2] < -65536 || global[2] > 65536 || global[3] < -65536 || global[3] > 65536 ||
        !bd_next(&q, pd))
        return false;
    if (texture_font_line_tag(&q, "STARTPROPERTIES ", 16)) {
        if (!bd_nums(&q, "STARTPROPERTIES ", 16, v, 1) || v[0] < 0 || v[0] > 1024) return false;
        count = (uint32_t)v[0];
        for (i = 0; i < count; ++i)
            if (!bd_next(&q, pd) || !bd_property(&q)) return false;
        if (!bd_next(&q, pd) || q.len != 13 || !texture_font_line_tag(&q, "ENDPROPERTIES", 13) || !bd_next(&q, pd)) return false;
    }
    if (!bd_nums(&q, "CHARS ", 6, v, 1) || v[0] < 1 || v[0] > 4093) {
        return false;
    }
    count = (uint32_t)v[0];
    header = q.p;
    if (!texture_font_emit(f, s, "bdf-global.bdf", 0, header, n)) return false;
    encodings = (int32_t *)xx_mem_alloc(count * sizeof(*encodings));
    if (!encodings) return false;
    for (i = 0; i < count; ++i) {
        int32_t encoding;
        uint32_t width, height, rowBytes;
        start = q.p;
        if (!bd_next(&q, pd) || !texture_font_line_tag(&q, "STARTCHAR ", 10) || q.len <= 10 || !bd_next(&q, pd)) goto done;
        if (!bd_nums(&q, "ENCODING ", 9, v, 1) || v[0] < -1 || v[0] > 0x10ffff || (v[0] >= 0 && !texture_font_scalar((uint32_t)v[0]))) {
            goto done;
        }
        encoding = v[0];
        for (j = 0; j < i; ++j)
            if (encoding >= 0 && encodings[j] == encoding) goto done;
        encodings[i] = encoding;
        if (!bd_next(&q, pd) || !bd_nums(&q, "SWIDTH ", 7, v, 2) || v[0] < -65536 || v[0] > 65536 || v[1] != 0 || !bd_next(&q, pd) || !bd_nums(&q, "DWIDTH ", 7, v, 2) ||
            v[0] < -65536 || v[0] > 65536 || v[1] != 0 || !bd_next(&q, pd) || !bd_nums(&q, "BBX ", 4, v, 4) || v[0] < 0 || v[0] > 4096 || v[1] < 0 || v[1] > 4096 ||
            v[2] < -65536 || v[2] > 65536 || v[3] < -65536 || v[3] > 65536)
            goto done;
        width = (uint32_t)v[0];
        height = (uint32_t)v[1];
        rowBytes = (width + 7) / 8;
        if (!bd_next(&q, pd)) goto done;
        if (texture_font_line_tag(&q, "ATTRIBUTES ", 11)) {
            uint32_t value;
            if (q.len != 15) goto done;
            for (j = 11; j < 15; ++j)
                if (!bd_hex(b[q.start + j], &value)) goto done;
            if (!bd_next(&q, pd)) goto done;
        }
        if (q.len != 6 || !texture_font_line_tag(&q, "BITMAP", 6)) goto done;
        for (j = 0; j < height; ++j) {
            uint32_t k, value;
            if (texture_font_stop(pd) || !texture_font_line(&q) || q.len != (uint64_t)rowBytes * 2) goto done;
            for (k = 0; k < q.len; ++k) {
                if (!bd_hex(b[q.start + k], &value)) goto done;
            }
        }
        if (!bd_next(&q, pd) || q.len != 7 || !texture_font_line_tag(&q, "ENDCHAR", 7)) {
            goto done;
        }
        xx_rt_snprintf(label, sizeof(label), "glyph-%u.bdf", i);
        if (!texture_font_emit(f, s, label, start, q.p - start, n)) goto done;
    }
    start = q.p;
    if (!bd_next(&q, pd) || q.len != 7 || !texture_font_line_tag(&q, "ENDFONT", 7) || q.p != n || b[n - 1] != 10 ||
        !texture_font_emit(f, s, "bdf-end.bin", start, n - start, n))
        goto done;
    s->size = (int64_t)n;
    result = true;
done:
    xx_mem_free(encodings);
    return result;
}

void xx_font_bdf_init(xx_font_bdf *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_FONT_BDF, "bdf");
    }
}
xx_font_bdf *xx_font_bdf_create(xx_io_device *d, int64_t at)
{
    xx_font_bdf *r = (xx_font_bdf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_font_bdf_init(r, d, at);
    return r;
}
void xx_font_bdf_destroy(xx_font_bdf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_font_bdf_free(xx_font_bdf *r)
{
    if (r) {
        xx_font_bdf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_font_bdf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_font_bdf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
