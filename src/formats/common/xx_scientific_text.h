/* SPDX-License-Identifier: MIT. Checked scientific text primitives. */
#ifndef XX_SCIENTIFIC_TEXT_H
#define XX_SCIENTIFIC_TEXT_H
#include "xx_memory_blob.h"
typedef struct scientific_text_token {
    uint64_t at, n;
} scientific_text_token;
typedef struct scientific_text_lines {
    memory_blob *b;
    uint64_t at;
    unsigned lines;
} scientific_text_lines;
static XXFC_MAYBE_UNUSED bool scientific_text_line(scientific_text_lines *c, scientific_text_token *v)
{
    uint64_t begin = c->at, end;
    if (begin >= c->b->n || ++c->lines > 1000000 || binary_stop(c->b->pd)) return false;
    while (c->at < c->b->n && c->b->p[(size_t)c->at] != '\n') {
        uint8_t ch = c->b->p[(size_t)c->at];
        if (!ch || ch > 126 || (ch < 32 && ch != '\t' && ch != '\r') || c->at - begin >= 1048576) return false;
        ++c->at;
    }
    if (c->at == c->b->n) {
        return false;
    }
    end = c->at++;
    if (end > begin && c->b->p[(size_t)end - 1] == '\r') --end;
    v->at = begin;
    v->n = end - begin;
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_text_eq(memory_blob *b, scientific_text_token v, const char *s)
{
    size_t n = xx_rt_strlen(s);
    return v.n == n && !xx_rt_memcmp(b->p + (size_t)v.at, s, n);
}
static XXFC_MAYBE_UNUSED bool scientific_text_prefix(memory_blob *b, scientific_text_token v, const char *s)
{
    size_t n = xx_rt_strlen(s);
    return v.n >= n && !xx_rt_memcmp(b->p + (size_t)v.at, s, n);
}
static scientific_text_token scientific_text_trim(memory_blob *b, scientific_text_token v)
{
    while (v.n && (b->p[(size_t)v.at] == ' ' || b->p[(size_t)v.at] == '\t')) {
        ++v.at;
        --v.n;
    }
    while (v.n && (b->p[(size_t)(v.at + v.n - 1)] == ' ' || b->p[(size_t)(v.at + v.n - 1)] == '\t')) --v.n;
    return v;
}
static bool scientific_text_uint(memory_blob *b, scientific_text_token v, uint64_t *out)
{
    uint64_t i, n = 0;
    v = scientific_text_trim(b, v);
    if (!v.n || v.n > 20) return false;
    for (i = 0; i < v.n; ++i) {
        uint8_t ch = b->p[(size_t)(v.at + i)];
        if (ch < '0' || ch > '9' || n > (UINT64_MAX - (ch - '0')) / 10) return false;
        n = n * 10 + ch - '0';
    }
    *out = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_text_integer(memory_blob *b, scientific_text_token v)
{
    uint64_t n;
    v = scientific_text_trim(b, v);
    if (v.n && (b->p[(size_t)v.at] == '-' || b->p[(size_t)v.at] == '+')) {
        ++v.at;
        --v.n;
    }
    return scientific_text_uint(b, v, &n) && n <= INT64_MAX;
}
static bool scientific_text_float(memory_blob *b, scientific_text_token v)
{
    uint64_t i = 0;
    unsigned digits = 0, ed = 0, e = 0;
    v = scientific_text_trim(b, v);
    if (!v.n || v.n > 96) return false;
    if (b->p[(size_t)v.at] == '+' || b->p[(size_t)v.at] == '-') ++i;
    while (i < v.n && b->p[(size_t)(v.at + i)] >= '0' && b->p[(size_t)(v.at + i)] <= '9') {
        ++i;
        ++digits;
    }
    if (i < v.n && b->p[(size_t)(v.at + i)] == '.') {
        ++i;
        while (i < v.n && b->p[(size_t)(v.at + i)] >= '0' && b->p[(size_t)(v.at + i)] <= '9') {
            ++i;
            ++digits;
        }
    }
    if (!digits || digits > 64) return false;
    if (i < v.n && (b->p[(size_t)(v.at + i)] == 'e' || b->p[(size_t)(v.at + i)] == 'E')) {
        ++i;
        if (i < v.n && (b->p[(size_t)(v.at + i)] == '+' || b->p[(size_t)(v.at + i)] == '-')) ++i;
        while (i < v.n && b->p[(size_t)(v.at + i)] >= '0' && b->p[(size_t)(v.at + i)] <= '9') {
            e = e * 10 + b->p[(size_t)(v.at + i++)] - '0';
            if (++ed > 3) return false;
        }
        if (!ed || e > 240) return false;
    }
    if (i != v.n) return false;
    {
        char text[100];
        const char *end;
        double value;
        uint64_t bits;
        xx_rt_memcpy(text, b->p + (size_t)v.at, (size_t)v.n);
        text[v.n] = 0;
        value = xx_rt_strtod(text, &end);
        xx_rt_memcpy(&bits, &value, sizeof(bits));
        return end == text + v.n && numeric_finite64(bits);
    }
}
static XXFC_MAYBE_UNUSED bool scientific_text_split(memory_blob *b, scientific_text_token v, scientific_text_token *t, unsigned cap, unsigned *count, bool tabs)
{
    uint64_t at = v.at, end = v.at + v.n;
    unsigned n = 0;
    while (at < end) {
        uint64_t start;
        if (!tabs)
            while (at < end && (b->p[(size_t)at] == ' ' || b->p[(size_t)at] == '\t')) ++at;
        if (at == end) break;
        start = at;
        while (at < end && b->p[(size_t)at] != '\t' && (tabs || b->p[(size_t)at] != ' ')) ++at;
        if (n == cap || at == start) {
            return false;
        }
        t[n].at = start;
        t[n++].n = at - start;
        if (at < end) {
            ++at;
            if (tabs && at == end) return false;
        }
    }
    *count = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_text_chars(memory_blob *b, scientific_text_token v, const char *allowed, bool nonempty)
{
    uint64_t i;
    size_t j, n = xx_rt_strlen(allowed);
    if (nonempty && !v.n) return false;
    for (i = 0; i < v.n; ++i) {
        uint8_t ch = b->p[(size_t)(v.at + i)];
        for (j = 0; j < n; ++j)
            if (ch == (uint8_t)allowed[j]) break;
        if (j == n) return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_text_ident(memory_blob *b, scientific_text_token v)
{
    uint64_t i;
    if (!v.n || v.n > 255) return false;
    for (i = 0; i < v.n; ++i) {
        uint8_t ch = b->p[(size_t)(v.at + i)];
        if (ch <= 32 || ch > 126 || ch == '>' || ch == '@' || ch == '\\') return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_text_unique(memory_blob *b, scientific_text_token v, scientific_text_token *names, unsigned count, uint64_t *budget)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        uint64_t j;
        if (names[i].n != v.n) continue;
        for (j = 0; j < v.n; ++j) {
            if (!*budget) return false;
            --*budget;
            if (b->p[(size_t)(v.at + j)] != b->p[(size_t)(names[i].at + j)]) break;
        }
        if (j == v.n) return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED scientific_text_token scientific_text_slice(scientific_text_token v, uint64_t at, uint64_t n)
{
    scientific_text_token t;
    t.at = v.at + at;
    t.n = n;
    return t;
}
static XXFC_MAYBE_UNUSED bool scientific_text_sep(memory_blob *b, scientific_text_token v, uint8_t delimiter, scientific_text_token *t, unsigned cap, unsigned *count)
{
    uint64_t at = v.at, end = v.at + v.n;
    unsigned n = 0;
    if (!v.n) return false;
    while (at < end) {
        uint64_t start = at;
        while (at < end && b->p[(size_t)at] != delimiter) ++at;
        if (at == start || n == cap) return false;
        t[n].at = start;
        t[n++].n = at - start;
        if (at < end && ++at == end) return false;
    }
    *count = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_text_range(memory_blob *b, scientific_text_token v, uint64_t negative, uint64_t positive)
{
    uint64_t n;
    bool neg = false;
    v = scientific_text_trim(b, v);
    if (v.n && (b->p[(size_t)v.at] == '-' || b->p[(size_t)v.at] == '+')) {
        neg = b->p[(size_t)v.at] == '-';
        ++v.at;
        --v.n;
    }
    return scientific_text_uint(b, v, &n) && n <= (neg ? negative : positive);
}
static XXFC_MAYBE_UNUSED bool scientific_text_f32(memory_blob *b, scientific_text_token v)
{
    char text[100];
    const char *end;
    double x;
    if (!scientific_text_float(b, v)) return false;
    v = scientific_text_trim(b, v);
    xx_rt_memcpy(text, b->p + (size_t)v.at, (size_t)v.n);
    text[v.n] = 0;
    x = xx_rt_strtod(text, &end);
    return x <= 3.4028234663852886e38 && x >= -3.4028234663852886e38;
}
static XXFC_MAYBE_UNUSED bool scientific_text_zero(memory_blob *b, scientific_text_token v)
{
    uint64_t i;
    v = scientific_text_trim(b, v);
    for (i = 0; i < v.n && b->p[(size_t)(v.at + i)] != 'e' && b->p[(size_t)(v.at + i)] != 'E'; ++i)
        if (b->p[(size_t)(v.at + i)] >= '1' && b->p[(size_t)(v.at + i)] <= '9') return false;
    return scientific_text_float(b, v);
}
#endif
