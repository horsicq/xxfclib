/* SPDX-License-Identifier: MIT. Private bounded shared format wire primitives. */
#ifndef XX_PHYLOGENETIC_TEXT_H
#define XX_PHYLOGENETIC_TEXT_H
#include "xx_memory_blob.h"
typedef struct phylo_text {
    memory_blob *b;
    uint64_t at, label_left;
    unsigned nodes, leaves, taxa;
    uint64_t *tax_at, *tax_len;
    uint8_t *tax_used;
} phylo_text;
static bool phylo_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static bool phylo_skip(phylo_text *t)
{
    memory_blob *b = t->b;
    while (t->at < b->n) {
        uint8_t c = b->p[(size_t)t->at];
        if (phylo_space(c)) {
            ++t->at;
            continue;
        }
        if (c == '[') {
            unsigned depth = 1;
            ++t->at;
            while (t->at < b->n && depth) {
                c = b->p[(size_t)t->at++];
                if (c == '[' && ++depth > 64) return false;
                if (c == ']') --depth;
                if (!c || binary_stop(b->pd)) return false;
            }
            if (depth) return false;
            continue;
        }
        break;
    }
    return !binary_stop(b->pd);
}
static XXFC_MAYBE_UNUSED bool phylo_word(phylo_text *t, const char *word)
{
    uint64_t at;
    size_t i, n = xx_rt_strlen(word);
    if (!phylo_skip(t)) return false;
    at = t->at;
    if (!blob_span(t->b, at, n)) return false;
    for (i = 0; i < n; ++i) {
        uint8_t c = t->b->p[(size_t)at + i];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
        if (c != (uint8_t)word[i]) return false;
    }
    if (at + n < t->b->n) {
        uint8_t c = t->b->p[(size_t)(at + n)];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') return false;
    }
    t->at += n;
    return true;
}
static bool phylo_char(phylo_text *t, uint8_t c)
{
    if (!phylo_skip(t) || t->at >= t->b->n || t->b->p[(size_t)t->at] != c) return false;
    ++t->at;
    return true;
}
static bool phylo_label(phylo_text *t, bool required)
{
    uint64_t start;
    memory_blob *b = t->b;
    if (!phylo_skip(t)) return false;
    start = t->at;
    if (t->at < b->n && b->p[(size_t)t->at] == '\'') {
        ++t->at;
        while (t->at < b->n) {
            uint8_t c = b->p[(size_t)t->at++];
            if (c == '\'') {
                if (t->at < b->n && b->p[(size_t)t->at] == '\'') {
                    ++t->at;
                    continue;
                }
                return t->at - start <= 4096 && (!required || t->at - start > 2);
            }
            if (!c || c < 32 || t->at - start > 4096) return false;
        }
        return false;
    }
    while (t->at < b->n) {
        uint8_t c = b->p[(size_t)t->at];
        if (phylo_space(c) || c == '(' || c == ')' || c == ',' || c == ':' || c == ';' || c == '[' || c == ']' || c == '=') break;
        if (c < 32 || c == '\'' || t->at - start >= 4096) return false;
        ++t->at;
    }
    return !required || t->at > start;
}
static bool phylo_number(phylo_text *t)
{
    uint64_t start = t->at;
    unsigned digits = 0, exdigits = 0;
    int exponent = 0;
    memory_blob *b = t->b;
    if (t->at < b->n && (b->p[(size_t)t->at] == '+' || b->p[(size_t)t->at] == '-')) ++t->at;
    while (t->at < b->n && b->p[(size_t)t->at] >= '0' && b->p[(size_t)t->at] <= '9') {
        ++t->at;
        ++digits;
    }
    if (t->at < b->n && b->p[(size_t)t->at] == '.') {
        ++t->at;
        while (t->at < b->n && b->p[(size_t)t->at] >= '0' && b->p[(size_t)t->at] <= '9') {
            ++t->at;
            ++digits;
        }
    }
    if (!digits || digits > 64) return false;
    if (t->at < b->n && (b->p[(size_t)t->at] == 'e' || b->p[(size_t)t->at] == 'E')) {
        ++t->at;
        if (t->at < b->n && (b->p[(size_t)t->at] == '+' || b->p[(size_t)t->at] == '-')) ++t->at;
        while (t->at < b->n && b->p[(size_t)t->at] >= '0' && b->p[(size_t)t->at] <= '9') {
            exponent = exponent * 10 + b->p[(size_t)t->at++] - '0';
            if (++exdigits > 3) return false;
        }
        if (!exdigits || exponent > 240) return false;
    }
    return t->at - start <= 80;
}
/* Compare names after removing quote delimiters and doubled quote escapes. */
static bool phylo_label_eq(memory_blob *b, uint64_t a, uint64_t na, uint64_t c, uint64_t nc, uint64_t *budget)
{
    uint64_t ae = a + na, ce = c + nc;
    bool aq, cq;
    if (!na || !nc || !blob_span(b, a, na) || !blob_span(b, c, nc)) return false;
    aq = b->p[(size_t)a] == '\'';
    cq = b->p[(size_t)c] == '\'';
    if (aq) {
        if (na < 2 || b->p[(size_t)ae - 1] != '\'') return false;
        ++a;
        --ae;
    }
    if (cq) {
        if (nc < 2 || b->p[(size_t)ce - 1] != '\'') return false;
        ++c;
        --ce;
    }
    while (a < ae && c < ce) {
        uint8_t x, y;
        if (!*budget) return false;
        --*budget;
        x = b->p[(size_t)a++];
        y = b->p[(size_t)c++];
        if (aq && x == '\'' && a < ae && b->p[(size_t)a] == '\'') ++a;
        if (cq && y == '\'' && c < ce && b->p[(size_t)c] == '\'') ++c;
        if (x != y) return false;
    }
    return a == ae && c == ce;
}
static bool phylo_clade(phylo_text *t, unsigned depth)
{
    memory_blob *b = t->b;
    unsigned children = 0;
    if (depth > 64 || ++t->nodes > 4096 || !phylo_skip(t) || t->at >= b->n) return false;
    if (b->p[(size_t)t->at] == '(') {
        ++t->at;
        do {
            if (!phylo_clade(t, depth + 1)) return false;
            ++children;
            if (!phylo_skip(t) || t->at >= b->n) return false;
            if (b->p[(size_t)t->at] != ',') break;
            ++t->at;
        } while (true);
        if (children < 2 || !phylo_char(t, ')') || !phylo_label(t, false)) return false;
    } else {
        uint64_t start = t->at;
        unsigned i;
        if (!phylo_label(t, true)) return false;
        if (t->taxa) {
            for (i = 0; i < t->taxa; ++i) {
                bool equal = phylo_label_eq(b, start, t->at - start, t->tax_at[i], t->tax_len[i], &t->label_left);
                if (!t->label_left) return false;
                if (equal) break;
            }
            if (i == t->taxa || t->tax_used[i]) return false;
            t->tax_used[i] = 1;
        }
        ++t->leaves;
    }
    if (!phylo_skip(t)) {
        return false;
    }
    if (t->at < b->n && b->p[(size_t)t->at] == ':') {
        ++t->at;
        if (!phylo_skip(t) || !phylo_number(t)) return false;
    }
    return phylo_skip(t);
}
static XXFC_MAYBE_UNUSED bool phylo_tree(phylo_text *t)
{
    t->nodes = 0;
    t->leaves = 0;
    if (t->taxa) xx_mem_zero(t->tax_used, t->taxa);
    return phylo_clade(t, 0) && t->leaves >= 2 && (!t->taxa || t->leaves == t->taxa) && phylo_char(t, ';');
}
static bool phylo_decimal(const uint8_t *p, size_t n, uint64_t *value)
{
    size_t i;
    uint64_t v = 0;
    if (!n || n > 18) return false;
    for (i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9' || v > (UINT64_MAX - (p[i] - '0')) / 10) return false;
        v = v * 10 + p[i] - '0';
    }
    *value = v;
    return true;
}
static XXFC_MAYBE_UNUSED bool phylo_line_uint(memory_blob *b, uint64_t end, const char *key, uint64_t *value)
{
    uint64_t at = 0;
    size_t n = xx_rt_strlen(key);
    unsigned seen = 0;
    while (at < end) {
        uint64_t line = at, last;
        while (at < end && b->p[(size_t)at] != '\n') ++at;
        last = at;
        if (last > line && b->p[(size_t)(last - 1)] == '\r') --last;
        if (last - line > n && !xx_rt_memcmp(b->p + (size_t)line, key, n) && b->p[(size_t)line + n] == ' ') {
            uint64_t start = line + n + 1, stop = start;
            while (stop < last && b->p[(size_t)stop] != ' ') ++stop;
            if (++seen > 1 || !phylo_decimal(b->p + (size_t)start, (size_t)(stop - start), value)) return false;
        }
        if (at < end) ++at;
    }
    return seen == 1;
}
#endif
