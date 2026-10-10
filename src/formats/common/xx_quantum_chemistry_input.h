/* SPDX-License-Identifier: MIT. Bounded inert scientific input grammars. */
#ifndef XX_QUANTUM_CHEMISTRY_INPUT_H
#define XX_QUANTUM_CHEMISTRY_INPUT_H
#include "xx_scientific_structure.h"
static XXFC_MAYBE_UNUSED __inline bool chemistry_line(scientific_text_lines *c, scientific_text_token *v) {
    uint64_t a = c->at, z;
    memory_blob *b = c->b;
    if (a >= b->n || ++c->lines > 65536 || binary_stop(b->pd))
        return false;
    while (c->at < b->n && b->p[(size_t)c->at] != 10) {
        uint8_t x = b->p[(size_t)c->at];
        if (!x || x > 126 || (x < 32 && x != 9 && x != 13) || c->at - a >= 4096) {
            c->at = b->n + 1;
            return false;
        }
        ++c->at;
    }
    z = c->at;
    if (c->at < b->n)
        ++c->at;
    if (z > a && b->p[(size_t)z - 1] == 13)
        --z;
    v->at = a;
    v->n = z - a;
    {
        uint64_t i;
        for (i = a; i < z; ++i)
            if (b->p[(size_t)i] == 13) {
                c->at = b->n + 1;
                return false;
            }
    }
    return true;
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_words(scientific_text_lines *c, scientific_text_token *line,
                                                       scientific_text_token *t, unsigned cap, unsigned *n,
                                                       const char *comments) {
    while (c->at < c->b->n) {
        if (!chemistry_line(c, line)) {
            return false;
        }
        *line = structure_comment(c->b, *line, comments);
        if (!line->n)
            continue;
        if (!scientific_text_split(c->b, *line, t, cap, n, false)) {
            c->at = c->b->n + 1;
            return false;
        }
        return true;
    }
    return false;
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_finish(scientific_text_lines *c, const char *comments) {
    scientific_text_token line;
    while (c->at < c->b->n)
        if (!chemistry_line(c, &line) || structure_comment(c->b, line, comments).n)
            return false;
    return c->at == c->b->n && !binary_stop(c->b->pd);
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_in(memory_blob *b, scientific_text_token t, const char *const *names,
                                                    unsigned n) {
    unsigned i;
    for (i = 0; i < n; ++i)
        if (structure_eq(b, t, names[i]))
            return true;
    return false;
}
static XXFC_MAYBE_UNUSED __inline unsigned chemistry_atomic_number(memory_blob *b, scientific_text_token t) {
    unsigned i;
    for (i = 1; i <= 118; ++i)
        if (structure_eq(b, t, molecular_elements[i]))
            return i;
    return 0;
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_identifier(memory_blob *b, scientific_text_token t) {
    return t.n && t.n <= 128 &&
           scientific_text_chars(b, t, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.+", true);
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_same_key(memory_blob *b, scientific_text_token a,
                                                          scientific_text_token z) {
    uint64_t i;
    if (a.n != z.n)
        return false;
    for (i = 0; i < a.n; ++i) {
        unsigned x = b->p[(size_t)(a.at + i)], y = b->p[(size_t)(z.at + i)];
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_cell_valid(memory_blob *b, scientific_text_token t[3][3]) {
    double a[3][3], d;
    unsigned i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            if (!scientific_text_float(b, t[i][j]))
                return false;
            a[i][j] = molecular_value(b, t[i][j]);
        }
    d = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
        a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    return d == d && ((d > 1e-18 && d < 1e240) || (d < -1e-18 && d > -1e240));
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_cell_comments(scientific_text_lines *c, const char *comments) {
    scientific_text_token row, t[3][3];
    unsigned i, n;
    for (i = 0; i < 3; ++i)
        if (!chemistry_words(c, &row, t[i], 3, &n, comments) || n != 3)
            return false;
    return chemistry_cell_valid(c->b, t);
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_cell(scientific_text_lines *c) {
    return chemistry_cell_comments(c, "#");
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_atom(memory_blob *b, scientific_text_token *t, unsigned n,
                                                      unsigned symbol, unsigned xyz) {
    return n >= xyz + 3 && symbol < n && chemistry_atomic_number(b, t[symbol]) && structure_floats(b, t + xyz, 3);
}
static XXFC_MAYBE_UNUSED __inline uint64_t chemistry_row_start(memory_blob *b, scientific_text_token row) {
    uint64_t at = row.at;
    while (at && b->p[(size_t)at - 1] != 10)
        --at;
    return at;
}
static XXFC_MAYBE_UNUSED __inline bool chemistry_add_row(Abstractformat *f, pm_stream *s, memory_blob *b,
                                                         const char *label, scientific_text_token row, uint64_t end) {
    uint64_t at = chemistry_row_start(b, row);
    return at < end && blob_add(f, s, b, label, at, end - at);
}

static XXFC_MAYBE_UNUSED __inline bool chemistry_assignment(memory_blob *b, scientific_text_token line,
                                                            scientific_text_token *key, scientific_text_token *value) {
    uint64_t at = 0;
    line = scientific_text_trim(b, line);
    while (at < line.n && b->p[(size_t)(line.at + at)] != '=')
        ++at;
    if (!at || at == line.n)
        return false;
    *key = scientific_text_trim(b, scientific_text_slice(line, 0, at));
    *value = scientific_text_trim(b, scientific_text_slice(line, at + 1, line.n - at - 1));
    if (!chemistry_identifier(b, *key) || !value->n)
        return false;
    if (b->p[(size_t)(value->at + value->n - 1)] == ',') {
        --value->n;
        *value = scientific_text_trim(b, *value);
    }
    if (!value->n || value->n > 255)
        return false;
    if (b->p[(size_t)value->at] == '\'' || b->p[(size_t)value->at] == '"') {
        uint8_t quote = b->p[(size_t)value->at];
        uint64_t i;
        if (value->n < 3 || b->p[(size_t)(value->at + value->n - 1)] != quote)
            return false;
        for (i = 1; i + 1 < value->n; ++i)
            if (b->p[(size_t)(value->at + i)] == quote || b->p[(size_t)(value->at + i)] < 32)
                return false;
        *value = scientific_text_slice(*value, 1, value->n - 2);
        return true;
    }
    return scientific_text_float(b, *value) || chemistry_identifier(b, *value);
}

#endif
