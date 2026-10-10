/* SPDX-License-Identifier: MIT. Private scientific framing helpers. */
#ifndef XX_SCIENTIFIC_NUMBERS_H
#define XX_SCIENTIFIC_NUMBERS_H
#include "xx_binary_cursor.h"
static bool scientific_number_uint(const char *p, uint64_t *v) {
    uint64_t n = 0;
    bool digit = false;
    if (!p)
        return false;
    while (*p == ' ' || *p == '\t')
        ++p;
    while (*p >= '0' && *p <= '9') {
        unsigned d = (unsigned)(*p++ - '0');
        if (n > ((uint64_t)INT64_MAX - d) / 10)
            return false;
        n = n * 10 + d;
        digit = true;
    }
    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p || !digit) {
        return false;
    }
    *v = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_number_fixed_uint(const uint8_t *p, size_t n, uint64_t *v) {
    char b[64];
    size_t i;
    if (n >= sizeof(b))
        return false;
    for (i = 0; i < n; ++i)
        if (!p[i])
            return false;
    xx_rt_memcpy(b, p, n);
    b[n] = 0;
    return scientific_number_uint(b, v);
}
static XXFC_MAYBE_UNUSED bool scientific_number_dims(const char *p, unsigned count, uint64_t *total) {
    unsigned i;
    uint64_t n = 1;
    for (i = 0; i < count; ++i) {
        uint64_t v = 0;
        bool digit = false;
        while (*p == ' ' || *p == '\t')
            ++p;
        while (*p >= '0' && *p <= '9') {
            unsigned d = (unsigned)(*p++ - '0');
            if (v > ((uint64_t)INT64_MAX - d) / 10)
                return false;
            v = v * 10 + d;
            digit = true;
        }
        if (!digit || !v || !binary_mul(n, v, &n) || (*p && *p != ' ' && *p != '\t'))
            return false;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p)
        return false;
    *total = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool scientific_number_line(binary_cursor *c, char *b, size_t cap) {
    size_t n = 0;
    uint8_t ch;
    while (binary_get(c, &ch, 1)) {
        if (ch == '\n') {
            if (n && b[n - 1] == '\r')
                --n;
            b[n] = 0;
            return true;
        }
        if (!ch || ch > 126 || (ch < 32 && ch != '\r' && ch != '\t') || n + 1 >= cap)
            return false;
        b[n++] = (char)ch;
    }
    return false;
}
static XXFC_MAYBE_UNUSED char *scientific_number_trim(char *p) {
    size_t n;
    while (*p == ' ' || *p == '\t')
        ++p;
    n = xx_rt_strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t'))
        p[--n] = 0;
    return p;
}
static XXFC_MAYBE_UNUSED bool scientific_number_prefix(const char *p, const char *q, size_t n) {
    return xx_rt_strlen(p) >= n && !xx_rt_memcmp(p, q, n);
}
static XXFC_MAYBE_UNUSED bool scientific_number_float32_uint(uint32_t bits, uint64_t *v) {
    unsigned e = (bits >> 23) & 255;
    uint64_t m = (bits & 0x7fffffU) | 0x800000U;
    int shift = (int)e - 150;
    if (bits >> 31 || !e || e == 255 || shift > 39 || shift < -23)
        return false;
    if (shift < 0) {
        unsigned k = (unsigned)-shift;
        if (m & ((UINT64_C(1) << k) - 1))
            return false;
        m >>= k;
    } else
        m <<= shift;
    if (m > INT64_MAX) {
        return false;
    }
    *v = m;
    return true;
}
static bool scientific_number_float_token(const char *p) {
    bool digits = false;
    unsigned n = 0;
    if (*p == '+' || *p == '-')
        ++p;
    while (*p >= '0' && *p <= '9') {
        digits = true;
        ++p;
        if (++n > 128)
            return false;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            digits = true;
            ++p;
            if (++n > 128)
                return false;
        }
    }
    if (!digits)
        return false;
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-')
            ++p;
        digits = false;
        while (*p >= '0' && *p <= '9') {
            digits = true;
            ++p;
            if (++n > 128)
                return false;
        }
        if (!digits)
            return false;
    }
    return !*p;
}
static XXFC_MAYBE_UNUSED bool scientific_number_positive_float(const char *p) {
    const char *q = p;
    bool nonzero = false;
    if (!scientific_number_float_token(p) || *q == '-') {
        return false;
    }
    if (*q == '+')
        ++q;
    while (*q && *q != 'e' && *q != 'E') {
        if (*q >= '1' && *q <= '9')
            nonzero = true;
        ++q;
    }
    return nonzero;
}
#endif
