/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/algo/pki/xx_pki_verify.h"
#include "xxfclib/algo/rsa/xx_rsa.h"
#include "xxfclib/algo/dsa/xx_dsa.h"
#include "xxfclib/algo/ec/xx_ec.h"
#include "xxfclib/algo/md2/xx_md2.h"
#include "xxfclib/algo/md5/xx_md5.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "xxfclib/algo/sha512/xx_sha512.h"
#include "xx_pki_der_internal.h"

typedef struct pki_oid { const char *text; const uint8_t *der; size_t size; xx_pki_hash hash; int family; } pki_oid;
#define O(t,b,h,f) {t,(const uint8_t *)b,sizeof(b)-1,h,f}
/* family: 0=digest, 1=RSA v1.5, 2=ECDSA, 3=DSA, 4=PSS, 5=key/parameter. */
static const pki_oid pki_oids[]={
 O("1.2.840.113549.2.2","\x2a\x86\x48\x86\xf7\x0d\x02\x02",XX_PKI_HASH_MD2,0),
 O("1.2.840.113549.2.5","\x2a\x86\x48\x86\xf7\x0d\x02\x05",XX_PKI_HASH_MD5,0),
 O("1.3.14.3.2.26","\x2b\x0e\x03\x02\x1a",XX_PKI_HASH_SHA1,0),
 O("2.16.840.1.101.3.4.2.4","\x60\x86\x48\x01\x65\x03\x04\x02\x04",XX_PKI_HASH_SHA224,0),
 O("2.16.840.1.101.3.4.2.1","\x60\x86\x48\x01\x65\x03\x04\x02\x01",XX_PKI_HASH_SHA256,0),
 O("2.16.840.1.101.3.4.2.2","\x60\x86\x48\x01\x65\x03\x04\x02\x02",XX_PKI_HASH_SHA384,0),
 O("2.16.840.1.101.3.4.2.3","\x60\x86\x48\x01\x65\x03\x04\x02\x03",XX_PKI_HASH_SHA512,0),
 O("1.2.840.113549.1.1.2","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x02",XX_PKI_HASH_MD2,1),
 O("1.2.840.113549.1.1.4","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x04",XX_PKI_HASH_MD5,1),
 O("1.2.840.113549.1.1.5","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x05",XX_PKI_HASH_SHA1,1),
 O("1.3.14.3.2.29","\x2b\x0e\x03\x02\x1d",XX_PKI_HASH_SHA1,1),
 O("1.2.840.113549.1.1.14","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0e",XX_PKI_HASH_SHA224,1),
 O("1.2.840.113549.1.1.11","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0b",XX_PKI_HASH_SHA256,1),
 O("1.2.840.113549.1.1.12","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0c",XX_PKI_HASH_SHA384,1),
 O("1.2.840.113549.1.1.13","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0d",XX_PKI_HASH_SHA512,1),
 O("1.2.840.10045.4.1","\x2a\x86\x48\xce\x3d\x04\x01",XX_PKI_HASH_SHA1,2),
 O("1.2.840.10045.4.3.1","\x2a\x86\x48\xce\x3d\x04\x03\x01",XX_PKI_HASH_SHA224,2),
 O("1.2.840.10045.4.3.2","\x2a\x86\x48\xce\x3d\x04\x03\x02",XX_PKI_HASH_SHA256,2),
 O("1.2.840.10045.4.3.3","\x2a\x86\x48\xce\x3d\x04\x03\x03",XX_PKI_HASH_SHA384,2),
 O("1.2.840.10045.4.3.4","\x2a\x86\x48\xce\x3d\x04\x03\x04",XX_PKI_HASH_SHA512,2),
 O("1.2.840.10040.4.3","\x2a\x86\x48\xce\x38\x04\x03",XX_PKI_HASH_SHA1,3),
 O("2.16.840.1.101.3.4.3.1","\x60\x86\x48\x01\x65\x03\x04\x03\x01",XX_PKI_HASH_SHA224,3),
 O("2.16.840.1.101.3.4.3.2","\x60\x86\x48\x01\x65\x03\x04\x03\x02",XX_PKI_HASH_SHA256,3),
 O("2.16.840.1.101.3.4.3.3","\x60\x86\x48\x01\x65\x03\x04\x03\x03",XX_PKI_HASH_SHA384,3),
 O("2.16.840.1.101.3.4.3.4","\x60\x86\x48\x01\x65\x03\x04\x03\x04",XX_PKI_HASH_SHA512,3),
 O("1.2.840.113549.1.1.10","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0a",XX_PKI_HASH_NONE,4),
 O("1.2.840.113549.1.1.1","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01",XX_PKI_HASH_NONE,5),
 O("1.2.840.10045.2.1","\x2a\x86\x48\xce\x3d\x02\x01",XX_PKI_HASH_NONE,5),
 O("1.2.840.10040.4.1","\x2a\x86\x48\xce\x38\x04\x01",XX_PKI_HASH_NONE,5),
 O("1.2.840.10045.3.1.7","\x2a\x86\x48\xce\x3d\x03\x01\x07",XX_PKI_HASH_NONE,5),
 O("1.3.132.0.34","\x2b\x81\x04\x00\x22",XX_PKI_HASH_NONE,5),
 O("1.2.840.113549.1.1.8","\x2a\x86\x48\x86\xf7\x0d\x01\x01\x08",XX_PKI_HASH_NONE,5)
};
#undef O
static const pki_oid *oid_text(const char *s)
{ size_t i;if(!s)return NULL;for(i=0;i<sizeof(pki_oids)/sizeof(pki_oids[0]);++i)if(!strcmp(s,pki_oids[i].text))return &pki_oids[i];return NULL; }
static const pki_oid *oid_der(xx_pki_der v)
{ size_t i;for(i=0;i<sizeof(pki_oids)/sizeof(pki_oids[0]);++i)if(v.n==pki_oids[i].size && !memcmp(v.p,pki_oids[i].der,v.n))return &pki_oids[i];return NULL; }
size_t xx_pki_hash_size(xx_pki_hash alg)
{ static const uint8_t sizes[]={0,16,16,20,28,32,48,64};return (unsigned)alg<sizeof(sizes)?sizes[alg]:0; }
bool xx_pki_hash_init(xx_pki_hash_context *context,xx_pki_hash alg)
{
    xx_pki_hash_context t;bool ok;
    if(!context)return false;
    memset(&t,0,sizeof(t));t.algorithm=alg;
    switch(alg){
    case XX_PKI_HASH_MD2:ok=xx_md2_init(&t.state.md2);break;
    case XX_PKI_HASH_MD5:ok=xx_md5_init(&t.state.md5);break;
    case XX_PKI_HASH_SHA1:ok=xx_sha1_init(&t.state.sha1);break;
    case XX_PKI_HASH_SHA224:ok=xx_sha224_init(&t.state.sha224);break;
    case XX_PKI_HASH_SHA256:ok=xx_sha256_init(&t.state.sha256);break;
    case XX_PKI_HASH_SHA384:ok=xx_sha384_init(&t.state.sha512);break;
    case XX_PKI_HASH_SHA512:ok=xx_sha512_init(&t.state.sha512);break;
    default:return false;
    }
    if(ok)*context=t;return ok;
}
bool xx_pki_hash_update(xx_pki_hash_context *context,const void *data,size_t size)
{
    if(!context || (!data&&size))return false;
    switch(context->algorithm){
    case XX_PKI_HASH_MD2:return xx_md2_update(&context->state.md2,data,size);
    case XX_PKI_HASH_MD5:
        if(!context->state.md5.initialized || context->state.md5.buffered>=64 ||
           context->state.md5.length>UINT64_MAX/8 || size>UINT64_MAX/8-context->state.md5.length)return false;
        xx_md5_update(&context->state.md5,data,size);return true;
    case XX_PKI_HASH_SHA1:
        if(!context->state.sha1.initialized || context->state.sha1.buffer_size>=64 ||
           context->state.sha1.total_size>UINT64_MAX/8 || size>UINT64_MAX/8-context->state.sha1.total_size)return false;
        xx_sha1_update(&context->state.sha1,data,size);return true;
    case XX_PKI_HASH_SHA224:return xx_sha224_update(&context->state.sha224,data,size);
    case XX_PKI_HASH_SHA256:
        if(!context->state.sha256.initialized || context->state.sha256.buffer_size>=64 ||
           context->state.sha256.total_size>UINT64_MAX/8 || size>UINT64_MAX/8-context->state.sha256.total_size)return false;
        xx_sha256_update(&context->state.sha256,data,size);return true;
    case XX_PKI_HASH_SHA384:return xx_sha384_update(&context->state.sha512,data,size);
    case XX_PKI_HASH_SHA512:return xx_sha512_update(&context->state.sha512,data,size);
    default:return false;
    }
}
bool xx_pki_hash_final(xx_pki_hash_context *context,uint8_t *out,size_t size)
{
    bool ok;
    if(!context || !out || !xx_pki_hash_size(context->algorithm) || size<xx_pki_hash_size(context->algorithm))return false;
    switch(context->algorithm){
    case XX_PKI_HASH_MD2:ok=xx_md2_final(&context->state.md2,out,size);break;
    case XX_PKI_HASH_MD5:ok=xx_md5_final(&context->state.md5,out,size);break;
    case XX_PKI_HASH_SHA1:ok=xx_sha1_final(&context->state.sha1,out,size);break;
    case XX_PKI_HASH_SHA224:ok=xx_sha224_final(&context->state.sha224,out,size);break;
    case XX_PKI_HASH_SHA256:ok=xx_sha256_final(&context->state.sha256,out,size);break;
    case XX_PKI_HASH_SHA384:ok=xx_sha384_final(&context->state.sha512,out,size);break;
    case XX_PKI_HASH_SHA512:ok=xx_sha512_final(&context->state.sha512,out,size);break;
    default:return false;
    }
    if(ok)memset(context,0,sizeof(*context));return ok;
}
bool xx_pki_hash_memoryv(xx_pki_hash alg,const xx_pki_hash_part *parts,size_t count,uint8_t *out)
{
    xx_pki_hash_context context;size_t i;
    if((!parts&&count)||!out||!xx_pki_hash_init(&context,alg))return false;
    for(i=0;i<count;++i)if(!xx_pki_hash_update(&context,parts[i].data,parts[i].size))return false;
    return xx_pki_hash_final(&context,out,xx_pki_hash_size(alg));
}
bool xx_pki_hash_memory(xx_pki_hash alg,const void *data,size_t size,uint8_t *out)
{
    if(!out || (!data && size))return false;
    switch(alg){
    case XX_PKI_HASH_MD2:return xx_md2_memory(data,size,out);
    case XX_PKI_HASH_MD5:return xx_md5_memory(data,size,out);
    case XX_PKI_HASH_SHA1:return xx_sha1_memory(data,size,out);
    case XX_PKI_HASH_SHA224:return xx_sha224_memory(data,size,out);
    case XX_PKI_HASH_SHA256:return xx_sha256_memory(data,size,out);
    case XX_PKI_HASH_SHA384:return xx_sha384_memory(data,size,out);
    case XX_PKI_HASH_SHA512:return xx_sha512_memory(data,size,out);
    default:return false;
    }
}
xx_pki_hash xx_pki_hash_from_oid(const char *s)
{ const pki_oid *o=oid_text(s);return o?o->hash:XX_PKI_HASH_NONE; }
static bool null_or_absent(xx_pki_der p)
{ return !p.n || (p.n==2 && p.p[0]==5 && !p.p[1]); }
static bool algorithm(xx_pki_der *in,const pki_oid **oid,xx_pki_der *params)
{ xx_pki_der seq,v;if(!pki_tlv(in,0x30,&seq)||!pki_tlv(&seq,6,&v)||!(*oid=oid_der(v)))return false;*params=seq;return true; }
static bool hash_algorithm(xx_pki_der in,xx_pki_hash *alg)
{const pki_oid *o;xx_pki_der p;if(!algorithm(&in,&o,&p)||in.n||o->family!=0||o->hash<XX_PKI_HASH_SHA1||!null_or_absent(p))return false;*alg=o->hash;return true;}
static bool pss_params(xx_pki_der in,xx_pki_hash *hash,xx_pki_hash *mgf,size_t *salt)
{
    xx_pki_der seq,v,params;const pki_oid *o;unsigned tag;size_t trailer;
    *hash=XX_PKI_HASH_SHA1;*mgf=XX_PKI_HASH_SHA1;*salt=20;
    if(!pki_tlv(&in,0x30,&seq)||in.n)return false;
    for(tag=0;tag<4;++tag){
        if(!seq.n || seq.p[0]!=(uint8_t)(0xa0+tag))continue;
        if(!pki_tlv(&seq,(uint8_t)(0xa0+tag),&v))return false;
        if(tag==0){if(!hash_algorithm(v,hash))return false;}
        else if(tag==1){if(!algorithm(&v,&o,&params)||v.n||strcmp(o->text,"1.2.840.113549.1.1.8")||!hash_algorithm(params,mgf))return false;}
        else if(tag==2){if(!pki_small_integer(&v,salt)||v.n)return false;}
        else if(!pki_small_integer(&v,&trailer)||v.n||trailer!=1)return false;
    }
    return !seq.n;
}
bool xx_pki_verify_digest(const uint8_t *spki,size_t size,const char *sig_oid,const uint8_t *params,size_t ps,
    xx_pki_hash hash,const uint8_t *digest,size_t ds,const uint8_t *sig,size_t ss)
{
    xx_pki_der in={spki,size},seq,key_params,bits,v,sig_params={params,ps}; const pki_oid *key_oid,*so,*curve;
    bool automatic=!sig_oid || !*sig_oid;
    so=automatic?NULL:oid_text(sig_oid);
    if(!spki || !size || !sig || !ss || !digest || !ds || ds!=xx_pki_hash_size(hash) || (!automatic&&!params&&ps) ||
       (!automatic&&!so) || !pki_tlv(&in,0x30,&seq)||in.n||!algorithm(&seq,&key_oid,&key_params)||
       !pki_tlv(&seq,3,&bits)||seq.n||bits.n<2||bits.p[0])return false;
    ++bits.p;--bits.n;
    if(!automatic && so->hash!=XX_PKI_HASH_NONE && so->hash!=hash)return false;
    if(!strcmp(key_oid->text,"1.2.840.113549.1.1.1")){
        xx_rsa_public_key key;
        if((!automatic&&!null_or_absent(key_params))||!xx_rsa_public_key_from_der(&key,bits.p,bits.n))return false;
        if(!automatic && so->family==4){xx_pki_hash h,m;size_t salt;if(!pss_params(sig_params,&h,&m,&salt)||h!=hash)return false;
            return xx_rsa_pss_verify_digest(&key,h,m,salt,digest,ds,sig,ss);}
        if(!automatic && ((so->family!=1 && strcmp(so->text,"1.2.840.113549.1.1.1")) || !null_or_absent(sig_params)))return false;
        return xx_rsa_pkcs1_verify_digest(&key,hash,digest,ds,sig,ss,true);
    }
    if(!strcmp(key_oid->text,"1.2.840.10045.2.1")){
        xx_ec_curve c;
        if((!automatic && (so->family!=2 || ps)) || !pki_tlv(&key_params,6,&v)||key_params.n||!(curve=oid_der(v)))return false;
        if(!strcmp(curve->text,"1.2.840.10045.3.1.7"))c=XX_EC_P256;
        else if(!strcmp(curve->text,"1.3.132.0.34"))c=XX_EC_P384;else return false;
        return xx_ecdsa_verify_digest(c,bits.p,bits.n,digest,ds,sig,ss,true);
    }
    if(!strcmp(key_oid->text,"1.2.840.10040.4.1")){
        xx_dsa_public_key key;
        if((!automatic && (so->family!=3 || ps)) || !xx_dsa_public_key_from_der(&key,key_params.p,key_params.n,bits.p,bits.n))return false;
        return xx_dsa_verify_digest(&key,digest,ds,sig,ss,true);
    }
    return false;
}
