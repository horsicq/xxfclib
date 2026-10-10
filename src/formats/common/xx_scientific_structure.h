/* SPDX-License-Identifier: MIT. Bounded scientific text and token helpers. */
#ifndef XX_SCIENTIFIC_STRUCTURE_H
#define XX_SCIENTIFIC_STRUCTURE_H
#include "xx_molecular_text.h"
static bool structure_eq(memory_blob *b, scientific_text_token t, const char *s)
{
    size_t n = xx_rt_strlen(s);
    uint64_t i;
    if (t.n != n) return false;
    for (i = 0; i < t.n; ++i) {
        unsigned a = b->p[(size_t)(t.at + i)], z = (unsigned char)s[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (z >= 'A' && z <= 'Z') z += 32;
        if (a != z) return false;
    }
    return true;
}
static scientific_text_token structure_comment(memory_blob *b, scientific_text_token t, const char *chars)
{
    uint64_t i;
    for (i = 0; i < t.n; ++i) {
        size_t j;
        for (j = 0; chars[j]; ++j)
            if (b->p[(size_t)(t.at + i)] == (uint8_t)chars[j]) {
                t.n = i;
                return scientific_text_trim(b, t);
            }
    }
    return scientific_text_trim(b, t);
}
static bool structure_words(scientific_text_lines *c, scientific_text_token *line, scientific_text_token *t, unsigned cap, unsigned *nt, const char *comment)
{
    for (;;) {
        uint64_t begin = c->at;
        scientific_text_token v;
        if (!scientific_text_line(c, line)) {
            if (begin < c->b->n) {
                uint64_t i;
                bool whitespace = c->at == c->b->n;
                for (i = begin; i < c->b->n && whitespace; ++i)
                    if (c->b->p[(size_t)i] != 32 && c->b->p[(size_t)i] != 9 && c->b->p[(size_t)i] != 13) whitespace = false;
                if (!whitespace) c->at = c->b->n + 1;
            }
            return false;
        }
        v = structure_comment(c->b, *line, comment);
        if (v.n) {
            if (scientific_text_split(c->b, v, t, cap, nt, false)) return true;
            c->at = c->b->n + 1;
            return false;
        }
    }
}
static XXFC_MAYBE_UNUSED bool structure_symbol(memory_blob *b, scientific_text_token t)
{
    unsigned i;
    for (i = 1; i <= 118; ++i)
        if (structure_eq(b, t, molecular_elements[i])) return true;
    return false;
}
static XXFC_MAYBE_UNUSED bool structure_positive(memory_blob *b, scientific_text_token t)
{
    return scientific_text_float(b, t) && molecular_value(b, t) > 0;
}
static bool structure_floats(memory_blob *b, scientific_text_token *t, unsigned n)
{
    return molecular_floats(b, t, 0, n);
}
static XXFC_MAYBE_UNUSED bool structure_same(memory_blob *b, scientific_text_token a, scientific_text_token z)
{
    return a.n == z.n && !xx_rt_memcmp(b->p + (size_t)a.at, b->p + (size_t)z.at, (size_t)a.n);
}
static XXFC_MAYBE_UNUSED bool structure_ascii(memory_blob *b)
{
    uint64_t at = 0;
    while (at < b->n) {
        size_t n = (size_t)(b->n - at > 65536 ? 65536 : b->n - at);
        if (binary_stop(b->pd) || !blob_ascii(b->p + (size_t)at, n, false)) return false;
        at += n;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool structure_vectors(scientific_text_lines *c, unsigned n, unsigned width)
{
    scientific_text_token line, t[8];
    unsigned i, nt;
    if (width > 8) return false;
    for (i = 0; i < n; ++i)
        if (!structure_words(c, &line, t, 8, &nt, "#") || nt != width || !structure_floats(c->b, t, width)) return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool structure_cell(memory_blob *b, scientific_text_token t[3][3])
{
    double a[3][3], d;
    unsigned i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            if (!scientific_text_float(b, t[i][j])) return false;
            a[i][j] = molecular_value(b, t[i][j]);
        }
    d = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    return d == d && d > 1e-18 && d < 1e240;
}
/* OpenFOAM tokens: no directives, expansion, code streams or external reads. */
typedef struct structure_lexer {
    memory_blob *b;
    uint64_t at, work;
} structure_lexer;
static bool structure_charge(structure_lexer *c)
{
    if (!c->work) {
        c->at = c->b->n + 1;
        return false;
    }
    --c->work;
    return true;
}
static bool structure_token(structure_lexer *c, scientific_text_token *t)
{
    uint64_t start;
    memory_blob *b = c->b;
    for (;;) {
        if (!c->work || binary_stop(b->pd)) return false;
        while (c->at < b->n && b->p[(size_t)c->at] <= 32) {
            uint8_t ch = b->p[(size_t)c->at++];
            if (ch != 32 && ch != 9 && ch != 10 && ch != 13) return false;
            if (!structure_charge(c)) return false;
        }
        if (c->at == b->n) return false;
        if (c->at + 1 < b->n && b->p[(size_t)c->at] == '/' && b->p[(size_t)c->at + 1] == '/') {
            c->at += 2;
            while (c->at < b->n && b->p[(size_t)c->at] != '\n') {
                if (!structure_charge(c)) return false;
                ++c->at;
            }
            continue;
        }
        if (c->at + 1 < b->n && b->p[(size_t)c->at] == '/' && b->p[(size_t)c->at + 1] == '*') {
            bool ended = false;
            c->at += 2;
            while (c->at + 1 < b->n) {
                if (!structure_charge(c)) return false;
                if (b->p[(size_t)c->at] == '*' && b->p[(size_t)c->at + 1] == '/') {
                    c->at += 2;
                    ended = true;
                    break;
                }
                ++c->at;
            }
            if (!ended) {
                c->at = b->n + 1;
                return false;
            }
            continue;
        }
        break;
    }
    start = c->at;
    if (b->p[(size_t)c->at] == '"') {
        ++c->at;
        while (c->at < b->n && b->p[(size_t)c->at] != '"') {
            uint8_t ch = b->p[(size_t)c->at++];
            if (ch < 32 || ch > 126 || ch == '\\' || c->at - start > 255 || !structure_charge(c)) {
                c->at = b->n + 1;
                return false;
            }
        }
        if (c->at == b->n) {
            c->at = b->n + 1;
            return false;
        }
        ++c->at;
    } else if (b->p[(size_t)c->at] == '{' || b->p[(size_t)c->at] == '}' || b->p[(size_t)c->at] == '(' || b->p[(size_t)c->at] == ')' || b->p[(size_t)c->at] == ';')
        ++c->at;
    else {
        while (c->at < b->n) {
            uint8_t ch = b->p[(size_t)c->at];
            if (ch <= 32 || ch == '{' || ch == '}' || ch == '(' || ch == ')' || ch == ';' || ch == '/') break;
            if (ch > 126 || ch == '#' || ch == '$' || ch == '"' || ch == '\\' || c->at - start > 255 || !structure_charge(c)) return false;
            ++c->at;
        }
        if (c->at == start) return false;
    }
    t->at = start;
    t->n = c->at - start;
    return true;
}
static XXFC_MAYBE_UNUSED bool structure_expect(structure_lexer *c, const char *s)
{
    scientific_text_token t;
    return structure_token(c, &t) && scientific_text_eq(c->b, t, s);
}
static XXFC_MAYBE_UNUSED bool structure_finish(structure_lexer *c)
{
    scientific_text_token t;
    if (structure_token(c, &t)) return false;
    return c->at == c->b->n && c->work && !binary_stop(c->b->pd);
}
#endif
