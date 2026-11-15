/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/algo/rsa/xx_rsa.h"
#include "../pki/xx_pki_der_internal.h"

static bool rsa_valid(const xx_rsa_public_key *key)
{
    xx_bignum three;
    if (!key || !xx_bignum_from_u32(&three,3)) return false;
    return xx_bignum_bit_length(&key->n) >= 16 && xx_bignum_bit_length(&key->n) <= XX_RSA_MAX_BYTES*8U &&
        (key->n.limbs[0]&1U) && (key->e.limbs[0]&1U) && xx_bignum_cmp(&key->e,&three)>=0 &&
        xx_bignum_cmp(&key->e,&key->n)<0;
}
bool xx_rsa_public_key_init(xx_rsa_public_key *key,const uint8_t *n,size_t ns,const uint8_t *e,size_t es)
{
    xx_rsa_public_key t;
    if (!key || !n || !e || !ns || ns>XX_RSA_MAX_BYTES || !es || es>XX_RSA_MAX_BYTES ||
        !xx_bignum_from_be(&t.n,n,ns) || !xx_bignum_from_be(&t.e,e,es) || !rsa_valid(&t)) return false;
    *key=t; return true;
}
bool xx_rsa_public_key_from_der(xx_rsa_public_key *key,const uint8_t *der,size_t size)
{
    xx_rsa_public_key t; xx_pki_der in={der,size}, seq;
    if (!key || !pki_tlv(&in,0x30,&seq) || in.n || !pki_integer(&seq,&t.n) ||
        !pki_integer(&seq,&t.e) || seq.n || !rsa_valid(&t)) return false;
    *key=t; return true;
}
static bool rsa_recover(const xx_rsa_public_key *key,const uint8_t *sig,size_t size,uint8_t *em,size_t *em_size)
{
    xx_bignum s,m; size_t k;
    if (!rsa_valid(key) || !sig) return false;
    k=(xx_bignum_bit_length(&key->n)+7)/8;
    if (size!=k || !xx_bignum_from_be(&s,sig,size) || xx_bignum_cmp(&s,&key->n)>=0 ||
        !xx_bignum_modexp(&m,&s,&key->e,&key->n) || !xx_bignum_to_be(&m,em,k)) return false;
    *em_size=k; return true;
}
bool xx_rsa_pkcs1_verify_digest(const xx_rsa_public_key *key,xx_pki_hash alg,const uint8_t *digest,size_t ds,
    const uint8_t *sig,size_t ss,bool raw)
{
    static const uint8_t old_oid[]={0x2a,0x86,0x48,0x86,0xf7,0x0d,2};
    static const uint8_t sha1_oid[]={0x2b,0x0e,3,2,0x1a};
    static const uint8_t sha2_oid[]={0x60,0x86,0x48,1,0x65,3,4,2};
    uint8_t em[XX_RSA_MAX_BYTES], prefix[20]; size_t k,i,ps=0,os,pn; const uint8_t *oid;
    if (!digest || !ds || ds!=xx_pki_hash_size(alg) || !rsa_recover(key,sig,ss,em,&k) || k<11 || em[0] || em[1]!=1) return false;
    i=2; while (i<k && em[i]==255) { ++i; ++ps; }
    if (ps<8 || i>=k || em[i++]!=0) return false;
    if (raw && k-i==ds && !memcmp(em+i,digest,ds)) return true;
    if (alg==XX_PKI_HASH_MD2 || alg==XX_PKI_HASH_MD5) {oid=old_oid; os=sizeof(old_oid)+1;}
    else if (alg==XX_PKI_HASH_SHA1) {oid=sha1_oid; os=sizeof(sha1_oid);}
    else {oid=sha2_oid; os=sizeof(sha2_oid)+1;}
    pn=os+10; prefix[0]=0x30; prefix[1]=(uint8_t)(pn+ds-2); prefix[2]=0x30;
    prefix[3]=(uint8_t)(os+4); prefix[4]=6; prefix[5]=(uint8_t)os;
    memcpy(prefix+6,oid,(alg==XX_PKI_HASH_SHA1)?os:os-1);
    if (alg==XX_PKI_HASH_MD2) prefix[6+os-1]=2;
    else if (alg==XX_PKI_HASH_MD5) prefix[6+os-1]=5;
    else if (alg!=XX_PKI_HASH_SHA1) prefix[6+os-1]=(uint8_t)(alg==XX_PKI_HASH_SHA224?4:alg==XX_PKI_HASH_SHA256?1:alg==XX_PKI_HASH_SHA384?2:3);
    prefix[6+os]=5; prefix[7+os]=0; prefix[8+os]=4; prefix[9+os]=(uint8_t)ds;
    return k-i==pn+ds && !memcmp(em+i,prefix,pn) && !memcmp(em+i+pn,digest,ds);
}
bool xx_rsa_mgf1(xx_pki_hash alg,const uint8_t *seed,size_t size,uint8_t *out,size_t out_size)
{
    uint8_t input[XX_RSA_MAX_BYTES+4],h[64]; size_t hs=xx_pki_hash_size(alg),offset=0,n; uint32_t counter=0;
    if (!hs || size>XX_RSA_MAX_BYTES || (!seed && size) || (!out && out_size) ||
        (out_size && (out_size-1)/hs>UINT32_MAX)) return false;
    if (size) memcpy(input,seed,size);
    while (offset<out_size) {
        input[size]=(uint8_t)(counter>>24); input[size+1]=(uint8_t)(counter>>16);
        input[size+2]=(uint8_t)(counter>>8); input[size+3]=(uint8_t)counter;
        if (!xx_pki_hash_memory(alg,input,size+4,h)) return false;
        n=out_size-offset; if(n>hs)n=hs; memcpy(out+offset,h,n); offset+=n; ++counter;
    }
    return true;
}
bool xx_rsa_pss_verify_digest(const xx_rsa_public_key *key,xx_pki_hash alg,xx_pki_hash mgf_alg,size_t salt,
    const uint8_t *digest,size_t ds,const uint8_t *sig,size_t ss)
{
    uint8_t em[XX_RSA_MAX_BYTES],mask[XX_RSA_MAX_BYTES],m[8+64+XX_RSA_MAX_BYTES],h[64]; size_t k,bits,len,db,i,zeros,unused;
    if (!digest || !ds || ds!=xx_pki_hash_size(alg) || !xx_pki_hash_size(mgf_alg) || !rsa_recover(key,sig,ss,em,&k)) return false;
    bits=xx_bignum_bit_length(&key->n)-1; len=(bits+7)/8;
    if (len<ds+2 || len>k || (k>len && em[0])) return false;
    if(k>len)memmove(em,em+k-len,len);
    db=len-ds-1; unused=len*8-bits;
    if (em[len-1]!=0xbc || (unused && (em[0]>>(8-unused)))) return false;
    if (!xx_rsa_mgf1(mgf_alg,em+db,ds,mask,db)) return false;
    for(i=0;i<db;++i)em[i]^=mask[i];
    em[0]&=(uint8_t)(255U>>unused);
    zeros=0; while(zeros<db && !em[zeros])++zeros;
    if(zeros==db || em[zeros]!=1) return false;
    if(salt!=XX_RSA_PSS_SALT_AUTO && salt!=db-zeros-1)return false;
    salt=db-zeros-1;
    memset(m,0,8); memcpy(m+8,digest,ds); memcpy(m+8+ds,em+zeros+1,salt);
    return xx_pki_hash_memory(alg,m,8+ds+salt,h) && !memcmp(h,em+db,ds);
}
