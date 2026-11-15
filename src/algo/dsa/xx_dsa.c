/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/algo/dsa/xx_dsa.h"
#include "../pki/xx_pki_der_internal.h"
static bool dsa_valid(const xx_dsa_public_key *k)
{
    xx_bignum one,t,r; size_t pb,qb;
    if(!k)return false;
    pb=xx_bignum_bit_length(&k->p); qb=xx_bignum_bit_length(&k->q);
    if(pb<1024 || pb>3072 || (qb!=160 && qb!=224 && qb!=256) ||
       !(k->p.limbs[0]&1U) || !(k->q.limbs[0]&1U))return false;
    xx_bignum_from_u32(&one,1);
    if(xx_bignum_cmp(&k->q,&k->p)>=0 || xx_bignum_cmp(&k->g,&one)<=0 || xx_bignum_cmp(&k->g,&k->p)>=0 ||
       xx_bignum_cmp(&k->y,&one)<=0 || xx_bignum_cmp(&k->y,&k->p)>=0)return false;
    return xx_bignum_sub(&t,&k->p,&one) && xx_bignum_mod(&r,&t,&k->q) && !r.used &&
       xx_bignum_modexp(&t,&k->g,&k->q,&k->p) && !xx_bignum_cmp(&t,&one) &&
       xx_bignum_modexp(&t,&k->y,&k->q,&k->p) && !xx_bignum_cmp(&t,&one);
}
bool xx_dsa_public_key_init(xx_dsa_public_key *key,const uint8_t *p,size_t ps,const uint8_t *q,size_t qs,
    const uint8_t *g,size_t gs,const uint8_t *y,size_t ys)
{
    xx_dsa_public_key t;
    if(!key || !p || !q || !g || !y || ps>384 || qs>32 || gs>384 || ys>384 ||
       !xx_bignum_from_be(&t.p,p,ps) || !xx_bignum_from_be(&t.q,q,qs) ||
       !xx_bignum_from_be(&t.g,g,gs) || !xx_bignum_from_be(&t.y,y,ys) || !dsa_valid(&t))return false;
    *key=t; return true;
}
bool xx_dsa_public_key_from_der(xx_dsa_public_key *key,const uint8_t *params,size_t ps,const uint8_t *pub,size_t ys)
{
    xx_pki_der in={params,ps},seq,y={pub,ys}; xx_dsa_public_key t;
    if(!key || !pki_tlv(&in,0x30,&seq) || in.n || !pki_integer(&seq,&t.p) || !pki_integer(&seq,&t.q) ||
       !pki_integer(&seq,&t.g) || seq.n || !pki_integer(&y,&t.y) || y.n || !dsa_valid(&t))return false;
    *key=t;return true;
}
bool xx_dsa_verify_digest(const xx_dsa_public_key *key,const uint8_t *digest,size_t ds,const uint8_t *sig,size_t ss,bool der)
{
    xx_bignum r,s,z,w,u1,u2,a,b,v; size_t qb,n;
    if(!digest || !ds || !dsa_valid(key))return false;
    qb=xx_bignum_bit_length(&key->q); n=(qb+7)/8;
    if(!pki_rs(sig,ss,der,n,&r,&s) || !r.used || !s.used ||
       xx_bignum_cmp(&r,&key->q)>=0 || xx_bignum_cmp(&s,&key->q)>=0)return false;
    if(ds>n)ds=n; /* Supported q sizes are exact octet widths. */
    return xx_bignum_from_be(&z,digest,ds) && xx_bignum_modinv(&w,&s,&key->q) &&
       xx_bignum_modmul(&u1,&z,&w,&key->q) && xx_bignum_modmul(&u2,&r,&w,&key->q) &&
       xx_bignum_modexp(&a,&key->g,&u1,&key->p) && xx_bignum_modexp(&b,&key->y,&u2,&key->p) &&
       xx_bignum_modmul(&v,&a,&b,&key->p) && xx_bignum_mod(&v,&v,&key->q) && !xx_bignum_cmp(&v,&r);
}
