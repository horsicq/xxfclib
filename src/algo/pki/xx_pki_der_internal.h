/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Small strict DER reader for the math lane. No dependency on lane b internals.
 */
#ifndef XX_PKI_DER_INTERNAL_H
#define XX_PKI_DER_INTERNAL_H
#include "xxfclib/algo/bignum/xx_bignum.h"
#include <string.h>
typedef struct xx_pki_der { const uint8_t *p; size_t n; } xx_pki_der;
static bool pki_tlv(xx_pki_der *in, uint8_t tag, xx_pki_der *out)
{
    size_t n, count, i, head = 2;
    if (!in || !out || !in->p || in->n < 2 || in->p[0] != tag) return false;
    n = in->p[1];
    if (n & 128U) {
        count = n & 127U;
        if (!count || count > sizeof(size_t) || count > in->n - 2 || in->p[2] == 0) return false;
        n = 0;
        for (i = 0; i < count; ++i) {
            if (n > (SIZE_MAX >> 8)) return false;
            n = (n << 8) | in->p[2+i];
        }
        if (n < 128) return false;
        head += count;
    }
    if (n > in->n - head) return false;
    out->p = in->p + head; out->n = n;
    in->p += head + n; in->n -= head + n;
    return true;
}
static bool pki_integer(xx_pki_der *in, xx_bignum *out)
{
    xx_pki_der v;
    if (!pki_tlv(in, 2, &v) || !v.n || (v.p[0] & 128U)) return false;
    if (v.n > 1 && v.p[0] == 0) {
        if (!(v.p[1] & 128U)) return false;
        ++v.p; --v.n;
    }
    return xx_bignum_from_be(out, v.p, v.n);
}
static bool pki_small_integer(xx_pki_der *in, size_t *out)
{
    xx_pki_der v; size_t n = 0, i;
    if (!pki_tlv(in, 2, &v) || !v.n || (v.p[0] & 128U) || v.n > sizeof(size_t)+1) return false;
    if (v.n > 1 && !v.p[0] && !(v.p[1] & 128U)) return false;
    for (i = 0; i < v.n; ++i) { if (n > (SIZE_MAX >> 8)) return false; n = (n << 8) | v.p[i]; }
    *out = n; return true;
}
static bool pki_rs(const uint8_t *sig, size_t size, bool der, size_t raw_size, xx_bignum *r, xx_bignum *s)
{
    xx_pki_der in = {sig,size}, seq;
    if (!sig) return false;
    if (der) return pki_tlv(&in,0x30,&seq) && !in.n && pki_integer(&seq,r) && pki_integer(&seq,s) && !seq.n;
    return size == 2*raw_size && xx_bignum_from_be(r,sig,raw_size) && xx_bignum_from_be(s,sig+raw_size,raw_size);
}
#endif
