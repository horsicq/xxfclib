/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private checked component primitives. Concrete readers own their grammars.
 * Explicit policy variants preserve cursor, syntax, bounds and ownership rules.
 */

#ifndef XX_COMPONENT_LEXER_H

#define XX_COMPONENT_LEXER_H

#include "xx_component_text.h"

typedef struct component_lexer {
    const uint8_t *b;
    uint64_t p, n;
    xx_pd_struct *pd;
    uint32_t work;
    bool hash, commas, comments;
} component_lexer;

static __inline bool component_lexer_identifier_char(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static __inline bool component_lexer_skip_hash_bang_cpp_comments(component_lexer *q)
{
    while (q->p < q->n) {
        uint8_t c = q->b[q->p];
        if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
        if (c == 32 || c == 9 || c == 10 || c == 13 || (q->commas && c == ',')) {
            ++q->p;
            continue;
        }
        if (q->hash && (c == '#' || c == '!')) {
            while (q->p < q->n && q->b[q->p] != 10 && q->b[q->p] != 13) {
                if (++q->work > 16000000 || ((q->p & 4095) == 0 && xx_component_parser_stopped(q->pd))) return false;
                ++q->p;
            }
            continue;
        }
        if (q->comments && c == '/' && component_span(q->p, 2, q->n) && q->b[q->p + 1] == '/') {
            q->p += 2;
            while (q->p < q->n && q->b[q->p] != 10 && q->b[q->p] != 13) {
                if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
                ++q->p;
            }
            continue;
        }
        if (q->comments && c == '/' && component_span(q->p, 2, q->n) && q->b[q->p + 1] == '*') {
            q->p += 2;
            while (component_span(q->p, 2, q->n) && !(q->b[q->p] == '*' && q->b[q->p + 1] == '/')) {
                if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
                ++q->p;
            }
            if (!component_span(q->p, 2, q->n)) return false;
            q->p += 2;
            continue;
        }
        break;
    }
    return true;
}

static __inline bool component_lexer_skip_hash_block_comments(component_lexer *q)
{
    while (q->p < q->n) {
        uint8_t c = q->b[q->p];
        if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
        if (c == 32 || c == 9 || c == 10 || c == 13 || (q->commas && c == ',')) {
            ++q->p;
            continue;
        }
        if (q->hash && c == '#') {
            while (q->p < q->n && q->b[q->p] != 10 && q->b[q->p] != 13) {
                if (++q->work > 16000000 || ((q->p & 4095) == 0 && xx_component_parser_stopped(q->pd))) return false;
                ++q->p;
            }
            continue;
        }
        if (q->comments && c == '/' && component_span(q->p, 2, q->n) && q->b[q->p + 1] == '*') {
            q->p += 2;
            while (component_span(q->p, 2, q->n) && !(q->b[q->p] == '*' && q->b[q->p + 1] == '/')) {
                if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
                ++q->p;
            }
            if (!component_span(q->p, 2, q->n)) return false;
            q->p += 2;
            continue;
        }
        break;
    }
    return true;
}

static __inline bool component_lexer_skip_hash_cpp_comments(component_lexer *q)
{
    while (q->p < q->n) {
        uint8_t c = q->b[q->p];
        if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
        if (c == 32 || c == 9 || c == 10 || c == 13 || (q->commas && c == ',')) {
            ++q->p;
            continue;
        }
        if (q->hash && c == '#') {
            while (q->p < q->n && q->b[q->p] != 10 && q->b[q->p] != 13) {
                if (++q->work > 16000000 || ((q->p & 4095) == 0 && xx_component_parser_stopped(q->pd))) return false;
                ++q->p;
            }
            continue;
        }
        if (q->comments && c == '/' && component_span(q->p, 2, q->n) && q->b[q->p + 1] == '/') {
            q->p += 2;
            while (q->p < q->n && q->b[q->p] != 10 && q->b[q->p] != 13) {
                if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
                ++q->p;
            }
            continue;
        }
        if (q->comments && c == '/' && component_span(q->p, 2, q->n) && q->b[q->p + 1] == '*') {
            q->p += 2;
            while (component_span(q->p, 2, q->n) && !(q->b[q->p] == '*' && q->b[q->p + 1] == '/')) {
                if (++q->work > 16000000 || xx_component_parser_stopped(q->pd)) return false;
                ++q->p;
            }
            if (!component_span(q->p, 2, q->n)) return false;
            q->p += 2;
            continue;
        }
        break;
    }
    return true;
}

static __inline bool component_lexer_char_hash_bang_cpp_comments(component_lexer *q, uint8_t c)
{
    if (!component_lexer_skip_hash_bang_cpp_comments(q) || q->p == q->n || q->b[q->p] != c) return false;
    ++q->p;
    return true;
}

static __inline bool component_lexer_char_hash_block_comments(component_lexer *q, uint8_t c)
{
    if (!component_lexer_skip_hash_block_comments(q) || q->p == q->n || q->b[q->p] != c) return false;
    ++q->p;
    return true;
}

static __inline bool component_lexer_char_hash_cpp_comments(component_lexer *q, uint8_t c)
{
    if (!component_lexer_skip_hash_cpp_comments(q) || q->p == q->n || q->b[q->p] != c) return false;
    ++q->p;
    return true;
}

static __inline bool component_lexer_end_hash_bang_cpp_comments(component_lexer *q)
{
    return component_lexer_skip_hash_bang_cpp_comments(q) && q->p == q->n;
}

static __inline bool component_lexer_end_hash_block_comments(component_lexer *q)
{
    return component_lexer_skip_hash_block_comments(q) && q->p == q->n;
}

static __inline bool component_lexer_end_hash_cpp_comments(component_lexer *q)
{
    return component_lexer_skip_hash_cpp_comments(q) && q->p == q->n;
}

static __inline bool component_lexer_identifier_hash_bang_cpp_comments(component_lexer *q, uint64_t *at, uint64_t *size)
{
    uint64_t p;
    if (!component_lexer_skip_hash_bang_cpp_comments(q)) return false;
    p = q->p;
    if (p == q->n || !((q->b[p] >= 'A' && q->b[p] <= 'Z') || (q->b[p] >= 'a' && q->b[p] <= 'z') || q->b[p] == '_')) return false;
    while (q->p < q->n && component_lexer_identifier_char(q->b[q->p])) {
        if (q->p - p >= 255) return false;
        ++q->p;
    }
    if (at) *at = p;
    if (size) *size = q->p - p;
    return true;
}

static __inline bool component_lexer_identifier_hash_block_comments(component_lexer *q, uint64_t *at, uint64_t *size)
{
    uint64_t p;
    if (!component_lexer_skip_hash_block_comments(q)) return false;
    p = q->p;
    if (p == q->n || !((q->b[p] >= 'A' && q->b[p] <= 'Z') || (q->b[p] >= 'a' && q->b[p] <= 'z') || q->b[p] == '_')) return false;
    while (q->p < q->n && component_lexer_identifier_char(q->b[q->p])) {
        if (q->p - p >= 255) return false;
        ++q->p;
    }
    if (at) *at = p;
    if (size) *size = q->p - p;
    return true;
}

static __inline bool component_lexer_identifier_hash_cpp_comments(component_lexer *q, uint64_t *at, uint64_t *size)
{
    uint64_t p;
    if (!component_lexer_skip_hash_cpp_comments(q)) return false;
    p = q->p;
    if (p == q->n || !((q->b[p] >= 'A' && q->b[p] <= 'Z') || (q->b[p] >= 'a' && q->b[p] <= 'z') || q->b[p] == '_')) return false;
    while (q->p < q->n && component_lexer_identifier_char(q->b[q->p])) {
        if (q->p - p >= 255) return false;
        ++q->p;
    }
    if (at) *at = p;
    if (size) *size = q->p - p;
    return true;
}

static __inline bool component_lexer_integer_hash_bang_cpp_comments(component_lexer *q, int32_t *v)
{
    uint64_t start, p;
    component_text_cursor t;
    bool ok;
    if (!component_lexer_skip_hash_bang_cpp_comments(q)) return false;
    start = p = q->p;
    if (p < q->n && (q->b[p] == '+' || q->b[p] == '-')) ++p;
    while (p < q->n && q->b[p] >= '0' && q->b[p] <= '9') {
        if (p - start >= 12) return false;
        ++p;
    }
    t.b = q->b;
    t.p = start;
    t.end = p;
    t.start = start;
    t.stop = p;
    t.t = start;
    ok = component_text_integer(&t, v) && t.t == p;
    if (ok) q->p = p;
    return ok;
}

static __inline bool component_lexer_integer_hash_bang_cpp_comments_delimited(component_lexer *q, int32_t *v)
{
    uint64_t start, p;
    component_text_cursor t;
    bool ok;
    if (!component_lexer_skip_hash_bang_cpp_comments(q)) return false;
    start = p = q->p;
    if (p < q->n && (q->b[p] == '+' || q->b[p] == '-')) ++p;
    while (p < q->n && q->b[p] >= '0' && q->b[p] <= '9') {
        if (p - start >= 12) return false;
        ++p;
    }
    t.b = q->b;
    t.p = start;
    t.end = p;
    t.start = start;
    t.stop = p;
    t.t = start;
    ok = component_text_integer_delimited(&t, v) && t.t == p;
    if (ok && p < q->n &&
        !((q->b[p] == 32) || (q->b[p] == 9) || (q->b[p] == 10) || (q->b[p] == 13) || (q->b[p] == ']') || (q->b[p] == '}') || (q->b[p] == ',') || (q->b[p] == '#')))
        ok = false;
    if (ok) q->p = p;
    return ok;
}

static __inline bool component_lexer_integer_hash_block_comments(component_lexer *q, int32_t *v)
{
    uint64_t start, p;
    component_text_cursor t;
    bool ok;
    if (!component_lexer_skip_hash_block_comments(q)) return false;
    start = p = q->p;
    if (p < q->n && (q->b[p] == '+' || q->b[p] == '-')) ++p;
    while (p < q->n && q->b[p] >= '0' && q->b[p] <= '9') {
        if (p - start >= 12) return false;
        ++p;
    }
    t.b = q->b;
    t.p = start;
    t.end = p;
    t.start = start;
    t.stop = p;
    t.t = start;
    ok = component_text_integer(&t, v) && t.t == p;
    if (ok) q->p = p;
    return ok;
}

static __inline bool component_lexer_integer_hash_cpp_comments_delimited(component_lexer *q, int32_t *v)
{
    uint64_t start, p;
    component_text_cursor t;
    bool ok;
    if (!component_lexer_skip_hash_cpp_comments(q)) return false;
    start = p = q->p;
    if (p < q->n && (q->b[p] == '+' || q->b[p] == '-')) ++p;
    while (p < q->n && q->b[p] >= '0' && q->b[p] <= '9') {
        if (p - start >= 12) return false;
        ++p;
    }
    t.b = q->b;
    t.p = start;
    t.end = p;
    t.start = start;
    t.stop = p;
    t.t = start;
    ok = component_text_integer_delimited(&t, v) && t.t == p;
    if (ok && p < q->n &&
        !((q->b[p] == 32) || (q->b[p] == 9) || (q->b[p] == 10) || (q->b[p] == 13) || (q->b[p] == ']') || (q->b[p] == '}') || (q->b[p] == ',') || (q->b[p] == '#')))
        ok = false;
    if (ok) q->p = p;
    return ok;
}

static __inline bool component_lexer_keyword_hash_bang_cpp_comments(component_lexer *q, const char *s)
{
    uint64_t p;
    size_t z = xx_rt_strlen(s);
    if (!component_lexer_skip_hash_bang_cpp_comments(q)) return false;
    p = q->p;
    if (!component_span(p, z, q->n) || !component_tag(q->b + p, s, z) || (p + z < q->n && component_lexer_identifier_char(q->b[p + z]))) return false;
    q->p = p + z;
    return true;
}

static __inline bool component_lexer_keyword_hash_block_comments(component_lexer *q, const char *s)
{
    uint64_t p;
    size_t z = xx_rt_strlen(s);
    if (!component_lexer_skip_hash_block_comments(q)) return false;
    p = q->p;
    if (!component_span(p, z, q->n) || !component_tag(q->b + p, s, z) || (p + z < q->n && component_lexer_identifier_char(q->b[p + z]))) return false;
    q->p = p + z;
    return true;
}

static __inline bool component_lexer_keyword_hash_cpp_comments(component_lexer *q, const char *s)
{
    uint64_t p;
    size_t z = xx_rt_strlen(s);
    if (!component_lexer_skip_hash_cpp_comments(q)) return false;
    p = q->p;
    if (!component_span(p, z, q->n) || !component_tag(q->b + p, s, z) || (p + z < q->n && component_lexer_identifier_char(q->b[p + z]))) return false;
    q->p = p + z;
    return true;
}

static __inline bool component_lexer_number_hash_bang_cpp_comments(component_lexer *q, double *v)
{
    uint64_t start, p;
    component_text_cursor t;
    bool ok;
    if (!component_lexer_skip_hash_bang_cpp_comments(q)) return false;
    start = p = q->p;
    while (p < q->n && ((q->b[p] >= '0' && q->b[p] <= '9') || q->b[p] == '+' || q->b[p] == '-' || q->b[p] == '.' || q->b[p] == 'E' || q->b[p] == 'e')) {
        if (p - start >= 96) return false;
        ++p;
    }
    t.b = q->b;
    t.p = start;
    t.end = p;
    t.start = start;
    t.stop = p;
    t.t = start;
    ok = component_text_number_36_digits(&t, v) && t.t == p;
    if (ok) q->p = p;
    return ok;
}

static __inline bool component_lexer_number_hash_block_comments(component_lexer *q, double *v)
{
    uint64_t start, p;
    component_text_cursor t;
    bool ok;
    if (!component_lexer_skip_hash_block_comments(q)) return false;
    start = p = q->p;
    while (p < q->n && ((q->b[p] >= '0' && q->b[p] <= '9') || q->b[p] == '+' || q->b[p] == '-' || q->b[p] == '.' || q->b[p] == 'E' || q->b[p] == 'e')) {
        if (p - start >= 96) return false;
        ++p;
    }
    t.b = q->b;
    t.p = start;
    t.end = p;
    t.start = start;
    t.stop = p;
    t.t = start;
    ok = component_text_number_36_digits(&t, v) && t.t == p;
    if (ok) q->p = p;
    return ok;
}

static __inline bool component_lexer_number_hash_cpp_comments(component_lexer *q, double *v)
{
    uint64_t start, p;
    component_text_cursor t;
    bool ok;
    if (!component_lexer_skip_hash_cpp_comments(q)) return false;
    start = p = q->p;
    while (p < q->n && ((q->b[p] >= '0' && q->b[p] <= '9') || q->b[p] == '+' || q->b[p] == '-' || q->b[p] == '.' || q->b[p] == 'E' || q->b[p] == 'e')) {
        if (p - start >= 96) return false;
        ++p;
    }
    t.b = q->b;
    t.p = start;
    t.end = p;
    t.start = start;
    t.stop = p;
    t.t = start;
    ok = component_text_number_36_digits(&t, v) && t.t == p;
    if (ok) q->p = p;
    return ok;
}

static __inline bool component_lexer_quoted_hash_bang_cpp_comments(component_lexer *q, uint8_t delim, uint64_t *at, uint64_t *size)
{
    uint64_t p;
    if (!component_lexer_skip_hash_bang_cpp_comments(q) || q->p == q->n || q->b[q->p++] != delim) return false;
    p = q->p;
    while (q->p < q->n) {
        uint8_t c = q->b[q->p++];
        if (q->p - p > 8192 || xx_component_parser_stopped(q->pd)) return false;
        if (c == delim) {
            if (delim == '\'' && q->p < q->n && q->b[q->p] == delim) {
                ++q->p;
                continue;
            }
            if (at) *at = p;
            if (size) *size = q->p - p - 1;
            return true;
        }
        if (delim == '"' && c == '\\') {
            if (q->p == q->n || (q->b[q->p] != '"' && q->b[q->p] != '\\')) return false;
            ++q->p;
        } else if (delim == '\'' && c == '\\') return false;
    }
    return false;
}

static __inline bool component_lexer_quoted_hash_block_comments(component_lexer *q, uint8_t delim, uint64_t *at, uint64_t *size)
{
    uint64_t p;
    if (!component_lexer_skip_hash_block_comments(q) || q->p == q->n || q->b[q->p++] != delim) return false;
    p = q->p;
    while (q->p < q->n) {
        uint8_t c = q->b[q->p++];
        if (q->p - p > 8192 || xx_component_parser_stopped(q->pd)) return false;
        if (c == delim) {
            if (delim == '\'' && q->p < q->n && q->b[q->p] == delim) {
                ++q->p;
                continue;
            }
            if (at) *at = p;
            if (size) *size = q->p - p - 1;
            return true;
        }
        if (delim == '"' && c == '\\') {
            if (q->p == q->n || (q->b[q->p] != '"' && q->b[q->p] != '\\')) return false;
            ++q->p;
        } else if (delim == '\'' && c == '\\') return false;
    }
    return false;
}

static __inline bool component_lexer_quoted_hash_cpp_comments(component_lexer *q, uint8_t delim, uint64_t *at, uint64_t *size)
{
    uint64_t p;
    if (!component_lexer_skip_hash_cpp_comments(q) || q->p == q->n || q->b[q->p++] != delim) return false;
    p = q->p;
    while (q->p < q->n) {
        uint8_t c = q->b[q->p++];
        if (q->p - p > 8192 || xx_component_parser_stopped(q->pd)) return false;
        if (c == delim) {
            if (delim == '\'' && q->p < q->n && q->b[q->p] == delim) {
                ++q->p;
                continue;
            }
            if (at) *at = p;
            if (size) *size = q->p - p - 1;
            return true;
        }
        if (delim == '"' && c == '\\') {
            if (q->p == q->n || (q->b[q->p] != '"' && q->b[q->p] != '\\')) return false;
            ++q->p;
        } else if (delim == '\'' && c == '\\') return false;
    }
    return false;
}

#endif
