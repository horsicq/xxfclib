/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/algo/ec/xx_ec.h"
#include "xxfclib/algo/bignum/xx_bignum.h"
#include <string.h>

typedef struct ec_context {
    size_t size;
    xx_bignum p, n, b, gx, gy;
    xx_bignum_mont_context field;
} ec_context;

typedef struct ec_jacobian {
    xx_bignum x, y, z;
} ec_jacobian;

static unsigned ec_hex_digit(char c)
{
    return c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'A' + 10);
}

static bool ec_hex(xx_bignum *out, const char *hex, size_t size)
{
    uint8_t bytes[48];
    size_t i;
    for (i = 0; i < size; ++i) bytes[i] = (uint8_t)((ec_hex_digit(hex[i * 2]) << 4) | ec_hex_digit(hex[i * 2 + 1]));
    return xx_bignum_from_be(out, bytes, size);
}

size_t xx_ec_field_size(xx_ec_curve curve)
{
    return curve == XX_EC_P256 ? 32U : curve == XX_EC_P384 ? 48U : 0U;
}

/* Domain parameters: SEC 2 v2, sections 2.4.2 and 2.5.1. */
static bool ec_init(ec_context *c, xx_ec_curve curve)
{
    const char *p, *n, *b, *gx, *gy;
    if (c == NULL || (c->size = xx_ec_field_size(curve)) == 0U) return false;
    if (curve == XX_EC_P256) {
        p = "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF";
        n = "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";
        b = "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B";
        gx = "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296";
        gy = "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5";
    } else {
        p = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF";
        n = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973";
        b = "B3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875AC656398D8A2ED19D2A85C8EDD3EC2AEF";
        gx = "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7";
        gy = "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F";
    }
    return ec_hex(&c->p, p, c->size) && ec_hex(&c->n, n, c->size) && ec_hex(&c->b, b, c->size) &&
           ec_hex(&c->gx, gx, c->size) && ec_hex(&c->gy, gy, c->size) && xx_bignum_mont_init(&c->field, &c->p);
}

static bool ec_zero(const xx_bignum *a) { return xx_bignum_bit_length(a) == 0U; }
static bool ec_fadd(xx_bignum *r, const xx_bignum *a, const xx_bignum *b, const ec_context *c)
{ return xx_bignum_modadd(r, a, b, &c->p); }
static bool ec_fsub(xx_bignum *r, const xx_bignum *a, const xx_bignum *b, const ec_context *c)
{ return xx_bignum_modsub(r, a, b, &c->p); }
static bool ec_fmul(xx_bignum *r, const xx_bignum *a, const xx_bignum *b, const ec_context *c)
{ return xx_bignum_mont_mul(r, a, b, &c->field); }

static bool ec_field_op(xx_ec_curve curve, uint8_t *out, const uint8_t *a, const uint8_t *b, unsigned op)
{
    ec_context c;
    xx_bignum aa, bb, r;
    if (out == NULL || a == NULL || (op != 3U && b == NULL) || !ec_init(&c, curve) ||
        !xx_bignum_from_be(&aa, a, c.size) || xx_bignum_cmp(&aa, &c.p) >= 0) return false;
    if (op != 3U && (!xx_bignum_from_be(&bb, b, c.size) || xx_bignum_cmp(&bb, &c.p) >= 0)) return false;
    if (op == 0U) { if (!xx_bignum_modadd(&r, &aa, &bb, &c.p)) return false; }
    else if (op == 1U) { if (!xx_bignum_modsub(&r, &aa, &bb, &c.p)) return false; }
    else if (op == 2U) { if (!xx_bignum_modmul(&r, &aa, &bb, &c.p)) return false; }
    else { if (ec_zero(&aa) || !xx_bignum_modinv(&r, &aa, &c.p)) return false; }
    return xx_bignum_to_be(&r, out, c.size);
}

bool xx_ec_field_add(xx_ec_curve curve, uint8_t *out, const uint8_t *a, const uint8_t *b)
{ return ec_field_op(curve, out, a, b, 0U); }
bool xx_ec_field_sub(xx_ec_curve curve, uint8_t *out, const uint8_t *a, const uint8_t *b)
{ return ec_field_op(curve, out, a, b, 1U); }
bool xx_ec_field_mul(xx_ec_curve curve, uint8_t *out, const uint8_t *a, const uint8_t *b)
{ return ec_field_op(curve, out, a, b, 2U); }
bool xx_ec_field_inv(xx_ec_curve curve, uint8_t *out, const uint8_t *a)
{ return ec_field_op(curve, out, a, NULL, 3U); }

static bool ec_on_curve(const xx_bignum *x, const xx_bignum *y, const ec_context *c)
{
    xx_bignum xm, ym, lhs, rhs, t, bm;
    if (xx_bignum_cmp(x, &c->p) >= 0 || xx_bignum_cmp(y, &c->p) >= 0 ||
        !xx_bignum_mont_import(&xm, x, &c->field) || !xx_bignum_mont_import(&ym, y, &c->field) ||
        !xx_bignum_mont_import(&bm, &c->b, &c->field) || !ec_fmul(&lhs, &ym, &ym, c) ||
        !ec_fmul(&rhs, &xm, &xm, c) || !ec_fmul(&rhs, &rhs, &xm, c) ||
        !ec_fadd(&t, &xm, &xm, c) || !ec_fadd(&t, &t, &xm, c) ||
        !ec_fsub(&rhs, &rhs, &t, c) || !ec_fadd(&rhs, &rhs, &bm, c)) return false;
    return xx_bignum_cmp(&lhs, &rhs) == 0;
}

static bool ec_read_point(const xx_ec_point *p, xx_bignum *x, xx_bignum *y, const ec_context *c)
{
    size_t i;
    if (p == NULL) return false;
    for (i = c->size; i < sizeof(p->x); ++i) if (p->x[i] != 0U || p->y[i] != 0U) return false;
    if (!xx_bignum_from_be(x, p->x, c->size) || !xx_bignum_from_be(y, p->y, c->size)) return false;
    if (p->infinity) return ec_zero(x) && ec_zero(y);
    return ec_on_curve(x, y, c);
}

static bool ec_write_point(xx_ec_point *out, const xx_bignum *x, const xx_bignum *y, bool infinity, const ec_context *c)
{
    xx_ec_point r;
    memset(&r, 0, sizeof(r));
    r.infinity = infinity;
    if (!infinity && (!xx_bignum_to_be(x, r.x, c->size) || !xx_bignum_to_be(y, r.y, c->size))) return false;
    *out = r;
    return true;
}

bool xx_ec_generator(xx_ec_curve curve, xx_ec_point *out)
{
    ec_context c;
    return out != NULL && ec_init(&c, curve) && ec_write_point(out, &c.gx, &c.gy, false, &c);
}

static bool ec_decode(xx_ec_point *out, const uint8_t *encoded, size_t size, const ec_context *c)
{
    xx_bignum x, y, rhs, t, exponent, one, four;
    if (out == NULL || encoded == NULL || size == 0U) return false;
    if (encoded[0] == 4U) {
        if (size != 1U + c->size * 2U || !xx_bignum_from_be(&x, encoded + 1, c->size) ||
            !xx_bignum_from_be(&y, encoded + 1U + c->size, c->size)) return false;
    } else if (encoded[0] == 2U || encoded[0] == 3U) {
        if (size != 1U + c->size || !xx_bignum_from_be(&x, encoded + 1, c->size) || xx_bignum_cmp(&x, &c->p) >= 0 ||
            !xx_bignum_modmul(&rhs, &x, &x, &c->p) || !xx_bignum_modmul(&rhs, &rhs, &x, &c->p) ||
            !xx_bignum_modadd(&t, &x, &x, &c->p) || !xx_bignum_modadd(&t, &t, &x, &c->p) ||
            !xx_bignum_modsub(&rhs, &rhs, &t, &c->p) || !xx_bignum_modadd(&rhs, &rhs, &c->b, &c->p) ||
            !xx_bignum_from_u32(&one, 1U) || !xx_bignum_from_u32(&four, 4U) || !xx_bignum_add(&exponent, &c->p, &one) ||
            !xx_bignum_divmod(&exponent, NULL, &exponent, &four) || !xx_bignum_modexp(&y, &rhs, &exponent, &c->p) ||
            !xx_bignum_modmul(&t, &y, &y, &c->p) || xx_bignum_cmp(&t, &rhs) != 0) return false;
        if ((y.limbs[0] & 1U) != (encoded[0] & 1U) && !xx_bignum_sub(&y, &c->p, &y)) return false;
    } else return false;
    return ec_on_curve(&x, &y, c) && ec_write_point(out, &x, &y, false, c);
}

bool xx_ec_point_decode(xx_ec_curve curve, xx_ec_point *out, const uint8_t *encoded, size_t size)
{
    ec_context c;
    return ec_init(&c, curve) && ec_decode(out, encoded, size, &c);
}

bool xx_ec_point_encode(xx_ec_curve curve, const xx_ec_point *point, bool compressed, uint8_t *out, size_t out_size)
{
    ec_context c;
    xx_bignum x, y;
    uint8_t encoded[97];
    size_t size;
    if (out == NULL || !ec_init(&c, curve) || !ec_read_point(point, &x, &y, &c)) return false;
    size = point->infinity ? 1U : 1U + c.size * (compressed ? 1U : 2U);
    if (out_size != size) return false;
    if (point->infinity) encoded[0] = 0U;
    else {
        encoded[0] = compressed ? (uint8_t)(2U | (y.limbs[0] & 1U)) : 4U;
        memcpy(encoded + 1, point->x, c.size);
        if (!compressed) memcpy(encoded + 1U + c.size, point->y, c.size);
    }
    memcpy(out, encoded, size);
    return true;
}

static bool ec_jac_init(ec_jacobian *r, const xx_bignum *x, const xx_bignum *y, bool infinity, const ec_context *c)
{
    if (infinity) {
        return xx_bignum_init(&r->x) && xx_bignum_init(&r->y) && xx_bignum_init(&r->z);
    }
    r->z = c->field.one;
    return xx_bignum_mont_import(&r->x, x, &c->field) && xx_bignum_mont_import(&r->y, y, &c->field);
}

/* Jacobian doubling for a = -3: dbl-2001-b, explicit infinity handling. */
static bool ec_double(ec_jacobian *out, const ec_jacobian *p, const ec_context *c)
{
    ec_jacobian r;
    xx_bignum delta, gamma, beta, alpha, t, u;
    if (ec_zero(&p->z) || ec_zero(&p->y)) return ec_jac_init(out, NULL, NULL, true, c);
    if (!ec_fmul(&delta, &p->z, &p->z, c) || !ec_fmul(&gamma, &p->y, &p->y, c) || !ec_fmul(&beta, &p->x, &gamma, c) ||
        !ec_fsub(&t, &p->x, &delta, c) || !ec_fadd(&u, &p->x, &delta, c) || !ec_fmul(&alpha, &t, &u, c) ||
        !ec_fadd(&t, &alpha, &alpha, c) || !ec_fadd(&alpha, &t, &alpha, c) || !ec_fmul(&r.x, &alpha, &alpha, c) ||
        !ec_fadd(&t, &beta, &beta, c) || !ec_fadd(&t, &t, &t, c) || !ec_fadd(&u, &t, &t, c) ||
        !ec_fsub(&r.x, &r.x, &u, c) || !ec_fsub(&t, &t, &r.x, c) || !ec_fmul(&r.y, &alpha, &t, c) ||
        !ec_fmul(&u, &gamma, &gamma, c) || !ec_fadd(&u, &u, &u, c) || !ec_fadd(&u, &u, &u, c) ||
        !ec_fadd(&u, &u, &u, c) || !ec_fsub(&r.y, &r.y, &u, c) ||
        !ec_fadd(&t, &p->y, &p->z, c) || !ec_fmul(&r.z, &t, &t, c) || !ec_fsub(&r.z, &r.z, &gamma, c) ||
        !ec_fsub(&r.z, &r.z, &delta, c)) return false;
    *out = r;
    return true;
}

/* Mixed Jacobian/affine addition: madd-2007-bl. qx/qy are Montgomery. */
static bool ec_mixed_add(ec_jacobian *out, const ec_jacobian *p, const xx_bignum *qx, const xx_bignum *qy, const ec_context *c)
{
    ec_jacobian r;
    xx_bignum zz, u, s, h, hh, i, j, rr, v, t;
    if (ec_zero(&p->z)) { r.x = *qx; r.y = *qy; r.z = c->field.one; *out = r; return true; }
    if (!ec_fmul(&zz, &p->z, &p->z, c) || !ec_fmul(&u, qx, &zz, c) ||
        !ec_fmul(&s, qy, &p->z, c) || !ec_fmul(&s, &s, &zz, c) ||
        !ec_fsub(&h, &u, &p->x, c) || !ec_fsub(&rr, &s, &p->y, c)) return false;
    if (ec_zero(&h)) {
        if (ec_zero(&rr)) return ec_double(out, p, c);
        return ec_jac_init(out, NULL, NULL, true, c);
    }
    if (!ec_fmul(&hh, &h, &h, c) || !ec_fadd(&i, &hh, &hh, c) || !ec_fadd(&i, &i, &i, c) ||
        !ec_fmul(&j, &h, &i, c) || !ec_fadd(&rr, &rr, &rr, c) || !ec_fmul(&v, &p->x, &i, c) ||
        !ec_fmul(&r.x, &rr, &rr, c) || !ec_fsub(&r.x, &r.x, &j, c) || !ec_fadd(&t, &v, &v, c) ||
        !ec_fsub(&r.x, &r.x, &t, c) || !ec_fsub(&t, &v, &r.x, c) || !ec_fmul(&r.y, &rr, &t, c) ||
        !ec_fmul(&t, &p->y, &j, c) || !ec_fadd(&t, &t, &t, c) || !ec_fsub(&r.y, &r.y, &t, c) ||
        !ec_fadd(&t, &p->z, &h, c) || !ec_fmul(&r.z, &t, &t, c) || !ec_fsub(&r.z, &r.z, &zz, c) ||
        !ec_fsub(&r.z, &r.z, &hh, c)) return false;
    *out = r;
    return true;
}

static bool ec_affine(xx_ec_point *out, const ec_jacobian *p, const ec_context *c)
{
    xx_bignum z, zi, zz, x, y;
    if (ec_zero(&p->z)) return ec_write_point(out, NULL, NULL, true, c);
    if (!xx_bignum_mont_export(&z, &p->z, &c->field) || !xx_bignum_modinv(&zi, &z, &c->p) ||
        !xx_bignum_mont_import(&zi, &zi, &c->field) || !ec_fmul(&zz, &zi, &zi, c) ||
        !ec_fmul(&x, &p->x, &zz, c) || !ec_fmul(&zz, &zz, &zi, c) || !ec_fmul(&y, &p->y, &zz, c) ||
        !xx_bignum_mont_export(&x, &x, &c->field) || !xx_bignum_mont_export(&y, &y, &c->field)) return false;
    return ec_write_point(out, &x, &y, false, c);
}

static bool ec_scalar_mul(ec_jacobian *out, const xx_bignum *x, const xx_bignum *y, const xx_bignum *k, const ec_context *c)
{
    ec_jacobian r;
    xx_bignum xm, ym;
    size_t bit = xx_bignum_bit_length(k);
    if (!ec_jac_init(&r, NULL, NULL, true, c) || !xx_bignum_mont_import(&xm, x, &c->field) ||
        !xx_bignum_mont_import(&ym, y, &c->field)) return false;
    while (bit > 0U) {
        --bit;
        if (!ec_double(&r, &r, c)) return false;
        if (((k->limbs[bit / 32U] >> (bit % 32U)) & 1U) != 0U && !ec_mixed_add(&r, &r, &xm, &ym, c)) return false;
    }
    *out = r;
    return true;
}

bool xx_ec_point_add(xx_ec_curve curve, xx_ec_point *out, const xx_ec_point *a, const xx_ec_point *b)
{
    ec_context c;
    xx_bignum ax, ay, bx, by;
    ec_jacobian r;
    if (out == NULL || !ec_init(&c, curve) || !ec_read_point(a, &ax, &ay, &c) || !ec_read_point(b, &bx, &by, &c)) return false;
    if (a->infinity) { *out = *b; return true; }
    if (b->infinity) { *out = *a; return true; }
    return ec_jac_init(&r, &ax, &ay, false, &c) && xx_bignum_mont_import(&bx, &bx, &c.field) &&
           xx_bignum_mont_import(&by, &by, &c.field) && ec_mixed_add(&r, &r, &bx, &by, &c) && ec_affine(out, &r, &c);
}

bool xx_ec_point_mul(xx_ec_curve curve, xx_ec_point *out, const xx_ec_point *point, const uint8_t *scalar, size_t scalar_size)
{
    ec_context c;
    xx_bignum x, y, k;
    ec_jacobian r;
    if (out == NULL || (scalar == NULL && scalar_size != 0U) || !ec_init(&c, curve) || scalar_size > c.size ||
        !ec_read_point(point, &x, &y, &c) || !xx_bignum_from_be(&k, scalar, scalar_size)) return false;
    if (point->infinity) return ec_write_point(out, NULL, NULL, true, &c);
    return ec_scalar_mul(&r, &x, &y, &k, &c) && ec_affine(out, &r, &c);
}

static bool ec_der_integer(xx_bignum *out, const uint8_t **cursor, size_t *remaining, size_t field_size)
{
    const uint8_t *p = *cursor;
    size_t len;
    if (*remaining < 3U || p[0] != 2U) return false;
    len = p[1];
    if (len == 0U || len > field_size + 1U || len > *remaining - 2U || (p[2] & 0x80U) != 0U ||
        (len > 1U && p[2] == 0U && (p[3] & 0x80U) == 0U)) return false;
    if (!xx_bignum_from_be(out, p + 2, len)) return false;
    *cursor += len + 2U;
    *remaining -= len + 2U;
    return true;
}

bool xx_ecdsa_verify_digest(xx_ec_curve curve,
                            const uint8_t *public_key, size_t public_key_size,
                            const uint8_t *digest, size_t digest_size,
                            const uint8_t *signature, size_t signature_size,
                            bool signature_is_der)
{
    ec_context c;
    xx_ec_point q, sum;
    xx_bignum qx, qy, r, s, e, inverse, u1, u2, x, y;
    ec_jacobian a, b;
    const uint8_t *cursor;
    size_t remaining;
    if (public_key == NULL || digest == NULL || digest_size == 0U || signature == NULL || !ec_init(&c, curve) ||
        !ec_decode(&q, public_key, public_key_size, &c) || !ec_read_point(&q, &qx, &qy, &c)) return false;
    if (signature_is_der) {
        if (signature_size < 8U || signature_size > c.size * 2U + 8U || signature[0] != 0x30U ||
            signature[1] != signature_size - 2U) return false;
        cursor = signature + 2;
        remaining = signature_size - 2U;
        if (!ec_der_integer(&r, &cursor, &remaining, c.size) || !ec_der_integer(&s, &cursor, &remaining, c.size) || remaining != 0U) return false;
    } else {
        if (signature_size != c.size * 2U || !xx_bignum_from_be(&r, signature, c.size) ||
            !xx_bignum_from_be(&s, signature + c.size, c.size)) return false;
    }
    if (ec_zero(&r) || ec_zero(&s) || xx_bignum_cmp(&r, &c.n) >= 0 || xx_bignum_cmp(&s, &c.n) >= 0 ||
        !xx_bignum_from_be(&e, digest, digest_size < c.size ? digest_size : c.size) ||
        !ec_scalar_mul(&a, &qx, &qy, &c.n, &c) || !ec_zero(&a.z) ||
        !xx_bignum_modinv(&inverse, &s, &c.n) || !xx_bignum_modmul(&u1, &e, &inverse, &c.n) ||
        !xx_bignum_modmul(&u2, &r, &inverse, &c.n) ||
        !ec_scalar_mul(&a, &c.gx, &c.gy, &u1, &c) || !ec_scalar_mul(&b, &qx, &qy, &u2, &c) ||
        !ec_affine(&sum, &b, &c)) return false;
    if (!sum.infinity) {
        if (!xx_bignum_from_be(&x, sum.x, c.size) || !xx_bignum_from_be(&y, sum.y, c.size) ||
            !xx_bignum_mont_import(&x, &x, &c.field) || !xx_bignum_mont_import(&y, &y, &c.field) ||
            !ec_mixed_add(&a, &a, &x, &y, &c)) return false;
    }
    if (ec_zero(&a.z) || !ec_affine(&sum, &a, &c) || !xx_bignum_from_be(&x, sum.x, c.size) || !xx_bignum_mod(&x, &x, &c.n)) return false;
    return xx_bignum_cmp(&x, &r) == 0;
}
