/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/KiCad/kicad-source-mirror/master/pcbnew/exporters/excellon_writer.cpp
 * Excellon drill subset: complete M48 header, INCH/METRIC zero-suppression and tool diameters, resolved tool selections and six-digit integer drill positions (INCH2:4/METRIC3:3, leading or trailing suppression) terminated by M30. Original encoded tools/positions exported; routing/repeat/machine-control extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/excellon_drill/xx_excellon_drill.h"
#include "../common/xx_component_lexer.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[3];
    return n >= 16 && pm_read(f, 0, b, 3) && component_tag(b, "M48", 3);
}
static bool drill_coord(component_lexer *q, unsigned digits, bool trailing, int32_t *value) {
    uint64_t start, p;
    unsigned count = 0;
    bool neg = false;
    int32_t v;
    if (q->p < q->n && (q->b[q->p] == '-' || q->b[q->p] == '+'))
        neg = q->b[q->p++] == '-';
    start = p = q->p;
    while (p < q->n && q->b[p] >= '0' && q->b[p] <= '9') {
        ++count;
        ++p;
    }
    if (!count || count > digits)
        return false;
    q->p = start;
    if (!component_lexer_integer_hash_block_comments(q, &v))
        return false;
    if (trailing)
        while (count++ < digits)
            v *= 10;
    *value = neg ? -v : v;
    return true;
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_text_cursor line = {b, 0, n, 0, 0, 0};
    uint8_t tools[1000];
    bool unit = false, trailing = false, body = false, ended = false, pos = false;
    unsigned digits = 6, hits = 0;
    int32_t selected = 0, x = 0, y = 0;
    xx_mem_zero(tools, sizeof(tools));
    if (!component_utf8(b, n, true, pd) || !component_text_line(&line) || !component_text_word(&line, "M48") ||
        !component_text_done(&line) || !component_emit(f, s, "header.drl", 0, line.p, n))
        return false;
    while (line.p < n) {
        component_lexer q;
        int32_t id;
        bool cx = false, cy = false;
        if (xx_component_parser_stopped(pd) || !component_text_line(&line))
            return false;
        component_text_space(&line);
        if (line.t == line.stop || b[line.t] == ';')
            continue;
        if (ended)
            return false;
        q.b = b;
        q.p = line.t;
        q.n = line.stop;
        q.pd = pd;
        q.work = 0;
        q.hash = false;
        q.commas = false;
        q.comments = false;
        if (!body) {
            if (component_lexer_keyword_hash_block_comments(&q, "INCH") ||
                component_lexer_keyword_hash_block_comments(&q, "METRIC")) {
                if (unit || !component_lexer_char_hash_block_comments(&q, ','))
                    return false;
                if (component_lexer_keyword_hash_block_comments(&q, "LZ"))
                    trailing = false;
                else if (component_lexer_keyword_hash_block_comments(&q, "TZ"))
                    trailing = true;
                else
                    return false;
                if (!component_lexer_end_hash_block_comments(&q))
                    return false;
                unit = true;
            } else if (component_lexer_char_hash_block_comments(&q, 'T')) {
                double diameter;
                if (!unit || !component_lexer_integer_hash_block_comments(&q, &id) || id < 1 || id > 999 || tools[id] ||
                    !component_lexer_char_hash_block_comments(&q, 'C') ||
                    !component_lexer_number_hash_block_comments(&q, &diameter) || diameter <= 0 || diameter > 1000000 ||
                    !component_lexer_end_hash_block_comments(&q))
                    return false;
                tools[id] = 1;
            } else if (component_lexer_char_hash_block_comments(&q, '%') ||
                       component_lexer_keyword_hash_block_comments(&q, "M95")) {
                if (!unit || !component_lexer_end_hash_block_comments(&q))
                    return false;
                body = true;
            } else
                return false;
        } else {
            if (component_lexer_char_hash_block_comments(&q, 'T')) {
                if (!component_lexer_integer_hash_block_comments(&q, &id) || id < 1 || id > 999 || !tools[id] ||
                    !component_lexer_end_hash_block_comments(&q))
                    return false;
                selected = id;
            } else if (component_lexer_keyword_hash_block_comments(&q, "M30")) {
                if (!hits || !component_lexer_end_hash_block_comments(&q))
                    return false;
                ended = true;
            } else {
                if (component_lexer_char_hash_block_comments(&q, 'X')) {
                    if (!drill_coord(&q, digits, trailing, &x))
                        return false;
                    cx = true;
                }
                if (component_lexer_char_hash_block_comments(&q, 'Y')) {
                    if (!drill_coord(&q, digits, trailing, &y))
                        return false;
                    cy = true;
                }
                if (!selected || (!cx && !cy) || (!pos && (!cx || !cy)) || !component_lexer_end_hash_block_comments(&q))
                    return false;
                pos = true;
                ++hits;
            }
        }
        if (!component_emit(f, s, body ? "drill-command.drl" : "tool-descriptor.drl", line.start, line.p - line.start,
                            n))
            return false;
    }
    if (!ended || !component_cover(f, s, "comments.drl", n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_excellon_drill_init(xx_excellon_drill *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_EXCELLON_DRILL, "drl");
    }
}
xx_excellon_drill *xx_excellon_drill_create(xx_io_device *d, int64_t at) {
    xx_excellon_drill *r = (xx_excellon_drill *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_excellon_drill_init(r, d, at);
    return r;
}
void xx_excellon_drill_destroy(xx_excellon_drill *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_excellon_drill_free(xx_excellon_drill *r) {
    if (r) {
        xx_excellon_drill_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_excellon_drill_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_excellon_drill_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
