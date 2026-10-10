/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private checked component primitives. Concrete readers own their grammars.
 * Explicit policy variants preserve cursor, syntax, bounds and ownership rules.
 */

#ifndef XX_COMPONENT_TEXT_H

#define XX_COMPONENT_TEXT_H

#include "xx_component_binary.h"

typedef struct component_text_cursor {
    const uint8_t *b;
    uint64_t p, end, start, stop, t;
} component_text_cursor;

static __inline bool component_text_line(component_text_cursor *q)
{
    uint64_t p = q->p;
    if (p >= q->end) return false;
    q->start = p;
    while (p < q->end && q->b[p] != 10 && q->b[p] != 13) {
        if (p - q->start >= 8192) return false;
        ++p;
    }
    q->stop = p;
    if (p < q->end && q->b[p] == 13) {
        ++p;
    }
    if (p < q->end && q->b[p] == 10) ++p;
    q->p = p;
    q->t = q->start;
    return true;
}

static __inline bool component_text_line_poison_overflow(component_text_cursor *q)
{
    uint64_t p = q->p;
    if (p >= q->end) return false;
    q->start = p;
    while (p < q->end && q->b[p] != 10 && q->b[p] != 13) {
        if (p - q->start >= 8192) {
            q->p = q->end + 1;
            return false;
        }
        ++p;
    }
    q->stop = p;
    if (p < q->end && q->b[p] == 13) {
        ++p;
    }
    if (p < q->end && q->b[p] == 10) ++p;
    q->p = p;
    q->t = q->start;
    return true;
}

static __inline void component_text_space(component_text_cursor *q)
{
    while (q->t < q->stop && (q->b[q->t] == 32 || q->b[q->t] == 9)) ++q->t;
}

static __inline bool component_text_done(component_text_cursor *q)
{
    component_text_space(q);
    return q->t == q->stop || q->b[q->t] == '#';
}

static __inline bool component_text_integer(component_text_cursor *q, int32_t *v)
{
    uint64_t p;
    uint32_t u = 0;
    bool neg = false;
    component_text_space(q);
    p = q->t;
    if (p < q->stop && (q->b[p] == '-' || q->b[p] == '+')) neg = q->b[p++] == '-';
    if (p >= q->stop || q->b[p] < '0' || q->b[p] > '9') return false;
    while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
        uint32_t d = q->b[p++] - '0';
        if (u > (2147483647U - d) / 10) return false;
        u = u * 10 + d;
    }
    q->t = p;
    *v = neg ? -(int32_t)u : (int32_t)u;
    return true;
}

static __inline bool component_text_integer_delimited(component_text_cursor *q, int32_t *v)
{
    uint64_t p;
    uint32_t u = 0;
    bool neg = false;
    component_text_space(q);
    p = q->t;
    if (p < q->stop && (q->b[p] == '-' || q->b[p] == '+')) neg = q->b[p++] == '-';
    if (p >= q->stop || q->b[p] < '0' || q->b[p] > '9') return false;
    while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
        uint32_t d = q->b[p++] - '0';
        if (u > (2147483647U - d) / 10) return false;
        u = u * 10 + d;
    }
    if (p < q->stop && q->b[p] != 32 && q->b[p] != 9 && q->b[p] != '#') {
        return false;
    }
    q->t = p;
    *v = neg ? -(int32_t)u : (int32_t)u;
    return true;
}

static __inline bool component_text_number_18_digits(component_text_cursor *q, double *value)
{
    uint64_t p;
    double v = 0, scale = 1;
    unsigned digits = 0, frac = 0;
    int exp = 0;
    bool neg = false, eneg = false;
    component_text_space(q);
    p = q->t;
    if (p < q->stop && (q->b[p] == '-' || q->b[p] == '+')) neg = q->b[p++] == '-';
    while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
        if (++digits > 18) return false;
        v = v * 10 + (q->b[p++] - '0');
    }
    if (p < q->stop && q->b[p] == '.') {
        ++p;
        while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
            if (++digits > 18) return false;
            v = v * 10 + (q->b[p++] - '0');
            ++frac;
        }
    }
    if (!digits) return false;
    if (p < q->stop && (q->b[p] == 'e' || q->b[p] == 'E')) {
        ++p;
        if (p < q->stop && (q->b[p] == '+' || q->b[p] == '-')) eneg = q->b[p++] == '-';
        if (p >= q->stop || q->b[p] < '0' || q->b[p] > '9') return false;
        while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
            exp = exp * 10 + q->b[p++] - '0';
            if (exp > 38) return false;
        }
    }
    if (p < q->stop && q->b[p] != 32 && q->b[p] != 9 && q->b[p] != '#') return false;
    while (frac--) {
        scale *= 10;
    }
    v /= scale;
    while (exp--) v = eneg ? v / 10 : v * 10;
    if (v > 3.402823466e38) return false;
    q->t = p;
    *value = neg ? -v : v;
    return true;
}

static __inline bool component_text_number_36_digits(component_text_cursor *q, double *value)
{
    uint64_t p;
    double v = 0, scale = 1;
    unsigned digits = 0, frac = 0;
    int exp = 0;
    bool neg = false, eneg = false;
    component_text_space(q);
    p = q->t;
    if (p < q->stop && (q->b[p] == '-' || q->b[p] == '+')) neg = q->b[p++] == '-';
    while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
        if (++digits > 36) return false;
        v = v * 10 + (q->b[p++] - '0');
    }
    if (p < q->stop && q->b[p] == '.') {
        ++p;
        while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
            if (++digits > 36) return false;
            v = v * 10 + (q->b[p++] - '0');
            ++frac;
        }
    }
    if (!digits) return false;
    if (p < q->stop && (q->b[p] == 'e' || q->b[p] == 'E')) {
        ++p;
        if (p < q->stop && (q->b[p] == '+' || q->b[p] == '-')) eneg = q->b[p++] == '-';
        if (p >= q->stop || q->b[p] < '0' || q->b[p] > '9') return false;
        while (p < q->stop && q->b[p] >= '0' && q->b[p] <= '9') {
            exp = exp * 10 + q->b[p++] - '0';
            if (exp > 38) return false;
        }
    }
    if (p < q->stop && q->b[p] != 32 && q->b[p] != 9 && q->b[p] != '#') return false;
    while (frac--) {
        scale *= 10;
    }
    v /= scale;
    while (exp--) v = eneg ? v / 10 : v * 10;
    if (v > 3.402823466e38) return false;
    q->t = p;
    *value = neg ? -v : v;
    return true;
}

static __inline bool component_text_string(component_text_cursor *q)
{
    uint64_t p;
    component_text_space(q);
    p = q->t;
    if (p >= q->stop || q->b[p++] != '"') return false;
    while (p < q->stop) {
        uint8_t c = q->b[p++];
        if (c == '"') {
            q->t = p;
            return true;
        }
        if (c == '\\') {
            if (p >= q->stop || (q->b[p] != '"' && q->b[p] != '\\')) return false;
            ++p;
        }
    }
    return false;
}

static __inline bool component_text_word(component_text_cursor *q, const char *s)
{
    uint64_t p;
    size_t n = xx_rt_strlen(s);
    component_text_space(q);
    p = q->t;
    if (!component_span(p, n, q->stop) || !component_tag(q->b + p, s, n) || (p + n < q->stop && q->b[p + n] != 32 && q->b[p + n] != 9)) {
        return false;
    }
    q->t = p + n;
    return true;
}

static __inline bool component_text_word_ci(component_text_cursor *q, const char *s)
{
    uint64_t p;
    size_t i, z = xx_rt_strlen(s);
    component_text_space(q);
    p = q->t;
    if (!component_span(p, z, q->stop)) return false;
    for (i = 0; i < z; ++i) {
        uint8_t c = q->b[p + i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != (uint8_t)s[i]) return false;
    }
    if (p + z < q->stop && q->b[p + z] != 32 && q->b[p + z] != 9) return false;
    q->t = p + z;
    return true;
}

static __inline bool component_text_next(component_text_cursor *q)
{
    while (q->p < q->end) {
        if (!component_text_line(q)) return false;
        if (!component_text_done(q)) return true;
    }
    return false;
}

static __inline bool component_text_next_poison_overflow(component_text_cursor *q)
{
    while (q->p < q->end) {
        if (!component_text_line_poison_overflow(q)) return false;
        if (!component_text_done(q)) return true;
    }
    return false;
}

static __inline bool component_text_numbers_18_digits(component_text_cursor *q, unsigned n)
{
    unsigned i;
    double v;
    for (i = 0; i < n; ++i)
        if (!component_text_number_18_digits(q, &v)) return false;
    return component_text_done(q);
}

static __inline bool component_text_numbers_36_digits(component_text_cursor *q, unsigned n)
{
    unsigned i;
    double v;
    for (i = 0; i < n; ++i)
        if (!component_text_number_36_digits(q, &v)) return false;
    return component_text_done(q);
}

static __inline bool component_grid_rows(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, uint64_t at, uint64_t count, double nodata, double *lo,
                                         double *hi, xx_pd_struct *pd, bool eof)
{
    component_text_cursor q = {b, at, n, 0, 0, 0};
    uint64_t seen = 0;
    bool values = false;
    unsigned rows = 0;
    char label[48];
    while (q.p < n) {
        double v;
        if (xx_component_parser_stopped(pd) || !component_text_line(&q)) return false;
        component_text_space(&q);
        if (q.t == q.stop) continue;
        if (eof && component_text_word(&q, "#EOF")) {
            if (!component_text_done(&q)) return false;
            while (q.p < n)
                if (!component_text_line(&q) || !component_text_done(&q)) return false;
            break;
        }
        while (q.t < q.stop) {
            component_text_space(&q);
            if (q.t == q.stop) break;
            if (!component_text_number_36_digits(&q, &v) || ++seen > count) return false;
            if (!component_near(v, nodata)) {
                if (!values) {
                    *lo = *hi = v;
                    values = true;
                } else {
                    if (v < *lo) *lo = v;
                    if (v > *hi) *hi = v;
                }
            }
        }
        if (++rows > 4093) return false;
        xx_rt_snprintf(label, sizeof(label), "samples-%u.txt", rows - 1);
        if (!component_emit(f, s, label, q.start, q.p - q.start, n)) return false;
    }
    return seen == count && component_cover(f, s, "whitespace.txt", n);
}

static __inline bool component_grid_rows_poison_overflow(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, uint64_t at, uint64_t count, double nodata,
                                                         double *lo, double *hi, xx_pd_struct *pd, bool eof)
{
    component_text_cursor q = {b, at, n, 0, 0, 0};
    uint64_t seen = 0;
    bool values = false;
    unsigned rows = 0;
    char label[48];
    while (q.p < n) {
        double v;
        if (xx_component_parser_stopped(pd) || !component_text_line_poison_overflow(&q)) return false;
        component_text_space(&q);
        if (q.t == q.stop) continue;
        if (eof && component_text_word(&q, "#EOF")) {
            if (!component_text_done(&q)) return false;
            while (q.p < n)
                if (!component_text_line_poison_overflow(&q) || !component_text_done(&q)) return false;
            break;
        }
        while (q.t < q.stop) {
            component_text_space(&q);
            if (q.t == q.stop) break;
            if (!component_text_number_36_digits(&q, &v) || ++seen > count) return false;
            if (!component_near(v, nodata)) {
                if (!values) {
                    *lo = *hi = v;
                    values = true;
                } else {
                    if (v < *lo) *lo = v;
                    if (v > *hi) *hi = v;
                }
            }
        }
        if (++rows > 4093) return false;
        xx_rt_snprintf(label, sizeof(label), "samples-%u.txt", rows - 1);
        if (!component_emit(f, s, label, q.start, q.p - q.start, n)) return false;
    }
    return seen == count && component_cover(f, s, "whitespace.txt", n);
}

#endif
