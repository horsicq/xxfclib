/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/algo/bignum/xx_bignum.h"
#include <string.h>

static bool bn_valid(const xx_bignum *a)
{
    return a != NULL && a->used <= XX_BIGNUM_MAX_LIMBS && (a->used == 0U || a->limbs[a->used - 1U] != 0U);
}

static void bn_normalize(xx_bignum *a)
{
    while (a->used != 0U && a->limbs[a->used - 1U] == 0U) --a->used;
}

static int bn_compare(const xx_bignum *a, const xx_bignum *b)
{
    size_t i;
    if (a->used != b->used) return a->used > b->used ? 1 : -1;
    for (i = a->used; i != 0U; --i) {
        if (a->limbs[i - 1U] != b->limbs[i - 1U]) return a->limbs[i - 1U] > b->limbs[i - 1U] ? 1 : -1;
    }
    return 0;
}

bool xx_bignum_init(xx_bignum *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    return true;
}

bool xx_bignum_from_u32(xx_bignum *out, uint32_t value)
{
    xx_bignum t = {0};
    if (out == NULL) return false;
    t.limbs[0] = value;
    t.used = value != 0U ? 1U : 0U;
    *out = t;
    return true;
}

bool xx_bignum_from_be(xx_bignum *out, const uint8_t *data, size_t size)
{
    xx_bignum t = {0};
    size_t i;
    if (out == NULL || size > XX_BIGNUM_MAX_BYTES || (size != 0U && data == NULL)) return false;
    for (i = 0U; i < size; ++i) t.limbs[i / 4U] |= (uint32_t)data[size - i - 1U] << ((i % 4U) * 8U);
    t.used = (size + 3U) / 4U;
    bn_normalize(&t);
    *out = t;
    return true;
}

size_t xx_bignum_bit_length(const xx_bignum *value)
{
    uint32_t v;
    size_t n;
    if (!bn_valid(value) || value->used == 0U) return 0U;
    n = (value->used - 1U) * 32U;
    v = value->limbs[value->used - 1U];
    while (v != 0U) { ++n; v >>= 1U; }
    return n;
}

bool xx_bignum_to_be(const xx_bignum *value, uint8_t *data, size_t size)
{
    size_t needed, i;
    if (!bn_valid(value) || (size != 0U && data == NULL)) return false;
    needed = (xx_bignum_bit_length(value) + 7U) / 8U;
    if (needed > size) return false;
    /* Read before writing so a caller may export into the value's storage. */
    if (size != 0U) {
        xx_bignum t = *value;
        memset(data, 0, size);
        for (i = 0U; i < needed; ++i) data[size - i - 1U] = (uint8_t)(t.limbs[i / 4U] >> ((i % 4U) * 8U));
    }
    return true;
}

int xx_bignum_cmp(const xx_bignum *a, const xx_bignum *b)
{
    return bn_valid(a) && bn_valid(b) ? bn_compare(a, b) : 0;
}

bool xx_bignum_add(xx_bignum *out, const xx_bignum *a, const xx_bignum *b)
{
    xx_bignum t = {0};
    uint64_t carry = 0U;
    size_t i, n;
    if (out == NULL || !bn_valid(a) || !bn_valid(b)) return false;
    n = a->used > b->used ? a->used : b->used;
    for (i = 0U; i < n; ++i) {
        uint64_t s = carry + (i < a->used ? a->limbs[i] : 0U) + (uint64_t)(i < b->used ? b->limbs[i] : 0U);
        t.limbs[i] = (uint32_t)s;
        carry = s >> 32U;
    }
    t.used = n;
    if (carry != 0U) {
        if (n == XX_BIGNUM_MAX_LIMBS) return false;
        t.limbs[t.used++] = (uint32_t)carry;
    }
    *out = t;
    return true;
}

bool xx_bignum_sub(xx_bignum *out, const xx_bignum *a, const xx_bignum *b)
{
    xx_bignum t = {0};
    uint64_t borrow = 0U;
    size_t i;
    if (out == NULL || !bn_valid(a) || !bn_valid(b) || bn_compare(a, b) < 0) return false;
    for (i = 0U; i < a->used; ++i) {
        uint64_t v = (uint64_t)(i < b->used ? b->limbs[i] : 0U) + borrow;
        t.limbs[i] = (uint32_t)((uint64_t)a->limbs[i] - v);
        borrow = (uint64_t)a->limbs[i] < v ? 1U : 0U;
    }
    t.used = a->used;
    bn_normalize(&t);
    *out = t;
    return true;
}

bool xx_bignum_mul(xx_bignum *out, const xx_bignum *a, const xx_bignum *b)
{
    uint32_t words[XX_BIGNUM_MAX_LIMBS * 2U] = {0};
    xx_bignum t = {0};
    size_t i, j, n;
    if (out == NULL || !bn_valid(a) || !bn_valid(b)) return false;
    for (i = 0U; i < a->used; ++i) {
        uint64_t carry = 0U;
        for (j = 0U; j < b->used; ++j) {
            uint64_t v = (uint64_t)a->limbs[i] * b->limbs[j] + words[i + j] + carry;
            words[i + j] = (uint32_t)v;
            carry = v >> 32U;
        }
        words[i + b->used] = (uint32_t)carry;
    }
    n = a->used + b->used;
    while (n != 0U && words[n - 1U] == 0U) --n;
    if (n > XX_BIGNUM_MAX_LIMBS) return false;
    memcpy(t.limbs, words, n * sizeof(uint32_t));
    t.used = n;
    *out = t;
    return true;
}

bool xx_bignum_divmod(xx_bignum *quotient, xx_bignum *remainder, const xx_bignum *a, const xx_bignum *b)
{
    xx_bignum q = {0}, r = {0};
    uint32_t words[XX_BIGNUM_MAX_LIMBS + 1U] = {0};
    size_t bits, n = 0U, i;
    if ((quotient == NULL && remainder == NULL) || (quotient != NULL && quotient == remainder) ||
        !bn_valid(a) || !bn_valid(b) || b->used == 0U) return false;
    bits = xx_bignum_bit_length(a);
    while (bits != 0U) {
        uint32_t carry;
        bool ge;
        --bits;
        carry = (a->limbs[bits / 32U] >> (bits % 32U)) & 1U;
        for (i = 0U; i < n; ++i) {
            uint32_t next = words[i] >> 31U;
            words[i] = (words[i] << 1U) | carry;
            carry = next;
        }
        if (carry != 0U) words[n++] = carry;
        ge = n > b->used;
        if (n == b->used) {
            i = n;
            while (i != 0U && words[i - 1U] == b->limbs[i - 1U]) --i;
            ge = i == 0U || words[i - 1U] > b->limbs[i - 1U];
        }
        if (ge) {
            uint64_t borrow = 0U;
            for (i = 0U; i < n; ++i) {
                uint64_t v = (uint64_t)(i < b->used ? b->limbs[i] : 0U) + borrow;
                uint32_t old = words[i];
                words[i] = (uint32_t)((uint64_t)old - v);
                borrow = (uint64_t)old < v ? 1U : 0U;
            }
            while (n != 0U && words[n - 1U] == 0U) --n;
            q.limbs[bits / 32U] |= (uint32_t)1U << (bits % 32U);
        }
    }
    q.used = a->used;
    bn_normalize(&q);
    r.used = n;
    memcpy(r.limbs, words, n * sizeof(uint32_t));
    if (quotient != NULL) *quotient = q;
    if (remainder != NULL) *remainder = r;
    return true;
}

bool xx_bignum_mod(xx_bignum *out, const xx_bignum *a, const xx_bignum *modulus)
{
    if (out == NULL || !bn_valid(a) || !bn_valid(modulus) || modulus->used == 0U) return false;
    if (bn_compare(a, modulus) < 0) { *out = *a; return true; }
    return xx_bignum_divmod(NULL, out, a, modulus);
}

/* a,b < m. Avoid a+b overflow by comparing a with m-b. */
static bool bn_modadd_reduced(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum *m)
{
    xx_bignum gap;
    if (!xx_bignum_sub(&gap, m, b)) return false;
    return bn_compare(a, &gap) >= 0 ? xx_bignum_sub(out, a, &gap) : xx_bignum_add(out, a, b);
}

bool xx_bignum_modadd(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum *modulus)
{
    xx_bignum ar, br;
    if (out == NULL || !xx_bignum_mod(&ar, a, modulus) || !xx_bignum_mod(&br, b, modulus)) return false;
    return bn_modadd_reduced(out, &ar, &br, modulus);
}

bool xx_bignum_modsub(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum *modulus)
{
    xx_bignum ar, br, gap;
    if (out == NULL || !xx_bignum_mod(&ar, a, modulus) || !xx_bignum_mod(&br, b, modulus)) return false;
    if (bn_compare(&ar, &br) >= 0) return xx_bignum_sub(out, &ar, &br);
    return xx_bignum_sub(&gap, &br, &ar) && xx_bignum_sub(out, modulus, &gap);
}

bool xx_bignum_modmul(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum *modulus)
{
    xx_bignum ar, br, product, r = {0};
    size_t i, bits;
    if (out == NULL || !xx_bignum_mod(&ar, a, modulus) || !xx_bignum_mod(&br, b, modulus)) return false;
    if (xx_bignum_mul(&product, &ar, &br)) return xx_bignum_mod(out, &product, modulus);
    /* Full-width multiplication still works when its unreduced product cannot
     * fit in the public capacity. No temporary allocation is needed. */
    if (xx_bignum_bit_length(&ar) < xx_bignum_bit_length(&br)) {
        product = ar;
        ar = br;
        br = product;
    }
    bits = xx_bignum_bit_length(&br);
    for (i = 0U; i < bits; ++i) {
        if (((br.limbs[i / 32U] >> (i % 32U)) & 1U) != 0U && !bn_modadd_reduced(&r, &r, &ar, modulus)) return false;
        if (i + 1U < bits && !bn_modadd_reduced(&ar, &ar, &ar, modulus)) return false;
    }
    *out = r;
    return true;
}

static bool bn_mont_valid(const xx_bignum_mont_context *c)
{
    return c != NULL && c->initialized && bn_valid(&c->modulus) && c->modulus.used != 0U &&
           c->limbs == c->modulus.used && (c->modulus.limbs[0] & 1U) != 0U &&
           !(c->modulus.used == 1U && c->modulus.limbs[0] == 1U) &&
           (uint32_t)((uint64_t)c->modulus.limbs[0] * c->n0) == UINT32_MAX &&
           bn_valid(&c->rr) && bn_compare(&c->rr, &c->modulus) < 0 &&
           bn_valid(&c->one) && bn_compare(&c->one, &c->modulus) < 0;
}

bool xx_bignum_mont_mul(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum_mont_context *c)
{
    uint32_t t[XX_BIGNUM_MAX_LIMBS * 2U + 2U] = {0};
    xx_bignum r = {0};
    size_t i, j, n;
    if (out == NULL || !bn_mont_valid(c) || !bn_valid(a) || !bn_valid(b) ||
        bn_compare(a, &c->modulus) >= 0 || bn_compare(b, &c->modulus) >= 0) return false;
    n = c->limbs;
    for (i = 0U; i < a->used; ++i) {
        uint64_t carry = 0U;
        for (j = 0U; j < b->used; ++j) {
            uint64_t v = (uint64_t)a->limbs[i] * b->limbs[j] + t[i + j] + carry;
            t[i + j] = (uint32_t)v;
            carry = v >> 32U;
        }
        t[i + b->used] = (uint32_t)carry;
    }
    for (i = 0U; i < n; ++i) {
        uint32_t factor = (uint32_t)((uint64_t)t[i] * c->n0);
        uint64_t carry = 0U;
        for (j = 0U; j < n; ++j) {
            uint64_t v = (uint64_t)factor * c->modulus.limbs[j] + t[i + j] + carry;
            t[i + j] = (uint32_t)v;
            carry = v >> 32U;
        }
        j = i + n;
        while (carry != 0U) {
            uint64_t v = (uint64_t)t[j] + carry;
            t[j++] = (uint32_t)v;
            carry = v >> 32U;
        }
    }
    memcpy(r.limbs, t + n, n * sizeof(uint32_t));
    r.used = n;
    bn_normalize(&r);
    if (t[n * 2U] != 0U || bn_compare(&r, &c->modulus) >= 0) {
        uint64_t borrow = 0U;
        for (i = 0U; i < n; ++i) {
            uint64_t v = (uint64_t)c->modulus.limbs[i] + borrow;
            uint32_t old = r.limbs[i];
            r.limbs[i] = (uint32_t)((uint64_t)old - v);
            borrow = (uint64_t)old < v ? 1U : 0U;
        }
        r.used = n;
        bn_normalize(&r);
    }
    *out = r;
    return true;
}

/* r = 2r mod m in place, for r < m; both are n words (r zero-padded). */
static void bn_words_double_mod(uint32_t *r, const uint32_t *m, size_t n)
{
    uint32_t carry = 0U;
    size_t i;
    bool ge;
    for (i = 0U; i < n; ++i) {
        uint32_t next = r[i] >> 31U;
        r[i] = (r[i] << 1U) | carry;
        carry = next;
    }
    ge = carry != 0U;
    if (!ge) {
        i = n;
        while (i != 0U && r[i - 1U] == m[i - 1U]) --i;
        ge = i == 0U || r[i - 1U] > m[i - 1U];
    }
    if (ge) {
        /* 2r < 2m, so one subtraction lands below m; a carried-out bit is
         * absorbed by the wrap-around of the n-word subtraction. */
        uint64_t borrow = 0U;
        for (i = 0U; i < n; ++i) {
            uint64_t v = (uint64_t)m[i] + borrow;
            uint32_t old = r[i];
            r[i] = (uint32_t)((uint64_t)old - v);
            borrow = (uint64_t)old < v ? 1U : 0U;
        }
    }
}

/* r = r * r * R^-1 mod m in place (Montgomery squaring), for r < m. The
 * arithmetic is that of xx_bignum_mont_mul on n-word arrays. */
static void bn_words_mont_square(uint32_t *r, const uint32_t *m, size_t n, uint32_t n0)
{
    uint32_t t[XX_BIGNUM_MAX_LIMBS * 2U + 2U];
    size_t i, j;
    bool ge;
    memset(t, 0, (n * 2U + 2U) * sizeof(uint32_t));
    for (i = 0U; i < n; ++i) {
        uint64_t carry = 0U;
        for (j = 0U; j < n; ++j) {
            uint64_t v = (uint64_t)r[i] * r[j] + t[i + j] + carry;
            t[i + j] = (uint32_t)v;
            carry = v >> 32U;
        }
        t[i + n] = (uint32_t)carry;
    }
    for (i = 0U; i < n; ++i) {
        uint32_t factor = (uint32_t)((uint64_t)t[i] * n0);
        uint64_t carry = 0U;
        for (j = 0U; j < n; ++j) {
            uint64_t v = (uint64_t)factor * m[j] + t[i + j] + carry;
            t[i + j] = (uint32_t)v;
            carry = v >> 32U;
        }
        j = i + n;
        while (carry != 0U) {
            uint64_t v = (uint64_t)t[j] + carry;
            t[j++] = (uint32_t)v;
            carry = v >> 32U;
        }
    }
    ge = t[n * 2U] != 0U;
    if (!ge) {
        i = n;
        while (i != 0U && t[n + i - 1U] == m[i - 1U]) --i;
        ge = i == 0U || t[n + i - 1U] > m[i - 1U];
    }
    if (ge) {
        uint64_t borrow = 0U;
        for (i = 0U; i < n; ++i) {
            uint64_t v = (uint64_t)m[i] + borrow;
            uint32_t old = t[n + i];
            t[n + i] = (uint32_t)((uint64_t)old - v);
            borrow = (uint64_t)old < v ? 1U : 0U;
        }
    }
    memcpy(r, t + n, n * sizeof(uint32_t));
}

bool xx_bignum_mont_init(xx_bignum_mont_context *context, const xx_bignum *modulus)
{
    xx_bignum_mont_context c;
    uint32_t r[XX_BIGNUM_MAX_LIMBS];
    uint32_t inverse = 1U;
    size_t i, n, odd, squarings = 0U;
    if (context == NULL || !bn_valid(modulus) || modulus->used == 0U || (modulus->limbs[0] & 1U) == 0U ||
        (modulus->used == 1U && modulus->limbs[0] == 1U)) return false;
    memset(&c, 0, sizeof(c));
    c.modulus = *modulus;
    c.limbs = n = modulus->used;
    for (i = 0U; i < 5U; ++i) inverse *= 2U - modulus->limbs[0] * inverse;
    c.n0 = 0U - inverse;
    /* one = R mod m, R = 2^(32n). With the top bit of m set, R / 2 < m < R
     * and R mod m = R - m, the n-word negation of m; otherwise double 1. */
    if ((modulus->limbs[n - 1U] & 0x80000000U) != 0U) {
        uint64_t borrow = 0U;
        for (i = 0U; i < n; ++i) {
            uint64_t v = (uint64_t)modulus->limbs[i] + borrow;
            r[i] = (uint32_t)(0U - v);
            borrow = v != 0U ? 1U : 0U;
        }
    } else {
        memset(r, 0, n * sizeof(uint32_t));
        r[0] = 1U;
        for (i = 0U; i < n * 32U; ++i) bn_words_double_mod(r, modulus->limbs, n);
    }
    memcpy(c.one.limbs, r, n * sizeof(uint32_t));
    c.one.used = n;
    bn_normalize(&c.one);
    /* rr = R^2 mod m is the Montgomery form of 2^(32n). Doubling the form of
     * 2^k gives that of 2^(k+1); squaring it gives that of 2^(2k). Split 32n
     * into its odd part (doublings of one) and power of two (squarings). */
    odd = n * 32U;
    while ((odd & 1U) == 0U) {
        odd >>= 1U;
        ++squarings;
    }
    for (i = 0U; i < odd; ++i) bn_words_double_mod(r, modulus->limbs, n);
    for (i = 0U; i < squarings; ++i) bn_words_mont_square(r, modulus->limbs, n, c.n0);
    memcpy(c.rr.limbs, r, n * sizeof(uint32_t));
    c.rr.used = n;
    bn_normalize(&c.rr);
    c.initialized = true;
    *context = c;
    return true;
}

bool xx_bignum_mont_import(xx_bignum *out, const xx_bignum *value, const xx_bignum_mont_context *context)
{
    xx_bignum reduced;
    if (out == NULL || !bn_mont_valid(context) || !xx_bignum_mod(&reduced, value, &context->modulus)) return false;
    return xx_bignum_mont_mul(out, &reduced, &context->rr, context);
}

bool xx_bignum_mont_export(xx_bignum *out, const xx_bignum *value, const xx_bignum_mont_context *context)
{
    xx_bignum one;
    xx_bignum_from_u32(&one, 1U);
    return xx_bignum_mont_mul(out, value, &one, context);
}

bool xx_bignum_modexp(xx_bignum *out, const xx_bignum *base, const xx_bignum *exponent, const xx_bignum *modulus)
{
    xx_bignum r, b;
    size_t bits;
    if (out == NULL || !bn_valid(base) || !bn_valid(exponent) || !bn_valid(modulus) || modulus->used == 0U) return false;
    if (modulus->used == 1U && modulus->limbs[0] == 1U) return xx_bignum_init(out);
    bits = xx_bignum_bit_length(exponent);
    if ((modulus->limbs[0] & 1U) != 0U) {
        xx_bignum_mont_context c;
        if (!xx_bignum_mont_init(&c, modulus) || !xx_bignum_mont_import(&b, base, &c)) return false;
        r = c.one;
        while (bits != 0U) {
            --bits;
            if (!xx_bignum_mont_mul(&r, &r, &r, &c)) return false;
            if (((exponent->limbs[bits / 32U] >> (bits % 32U)) & 1U) != 0U && !xx_bignum_mont_mul(&r, &r, &b, &c)) return false;
        }
        return xx_bignum_mont_export(out, &r, &c);
    }
    xx_bignum_from_u32(&r, 1U);
    if (!xx_bignum_mod(&b, base, modulus)) return false;
    while (bits != 0U) {
        --bits;
        if (!xx_bignum_modmul(&r, &r, &r, modulus)) return false;
        if (((exponent->limbs[bits / 32U] >> (bits % 32U)) & 1U) != 0U && !xx_bignum_modmul(&r, &r, &b, modulus)) return false;
    }
    *out = r;
    return true;
}

bool xx_bignum_modinv(xx_bignum *out, const xx_bignum *a, const xx_bignum *modulus)
{
    xx_bignum r, next_r, t = {0}, next_t, q, remainder, product, coefficient;
    if (out == NULL || !bn_valid(modulus) || modulus->used == 0U ||
        (modulus->used == 1U && modulus->limbs[0] == 1U) || !xx_bignum_mod(&next_r, a, modulus)) return false;
    r = *modulus;
    xx_bignum_from_u32(&next_t, 1U);
    while (next_r.used != 0U) {
        if (!xx_bignum_divmod(&q, &remainder, &r, &next_r) ||
            !xx_bignum_modmul(&product, &q, &next_t, modulus) ||
            !xx_bignum_modsub(&coefficient, &t, &product, modulus)) return false;
        r = next_r;
        next_r = remainder;
        t = next_t;
        next_t = coefficient;
    }
    if (r.used != 1U || r.limbs[0] != 1U) return false;
    *out = t;
    return true;
}
