/* Independently implemented interoperability primitives. SPDX-License-Identifier: MIT.
 * SHA-512: https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf
 * MT19937: standard 624-word recurrence, directly supplied initial state.
 * Container-specific state export and byte feedback are observed format facts.
 */
#include "dgca_cipher.h"
#include <string.h>

static const uint64_t dc_sha_iv[8]={UINT64_C(0x6a09e667f3bcc908),UINT64_C(0xbb67ae8584caa73b),UINT64_C(0x3c6ef372fe94f82b),UINT64_C(0xa54ff53a5f1d36f1),UINT64_C(0x510e527fade682d1),UINT64_C(0x9b05688c2b3e6c1f),UINT64_C(0x1f83d9abfb41bd6b),UINT64_C(0x5be0cd19137e2179)};
static const uint64_t dc_sha_k[80]={UINT64_C(0x428a2f98d728ae22),UINT64_C(0x7137449123ef65cd),UINT64_C(0xb5c0fbcfec4d3b2f),UINT64_C(0xe9b5dba58189dbbc),UINT64_C(0x3956c25bf348b538),UINT64_C(0x59f111f1b605d019),UINT64_C(0x923f82a4af194f9b),UINT64_C(0xab1c5ed5da6d8118),UINT64_C(0xd807aa98a3030242),UINT64_C(0x12835b0145706fbe),UINT64_C(0x243185be4ee4b28c),UINT64_C(0x550c7dc3d5ffb4e2),UINT64_C(0x72be5d74f27b896f),UINT64_C(0x80deb1fe3b1696b1),UINT64_C(0x9bdc06a725c71235),UINT64_C(0xc19bf174cf692694),UINT64_C(0xe49b69c19ef14ad2),UINT64_C(0xefbe4786384f25e3),UINT64_C(0x0fc19dc68b8cd5b5),UINT64_C(0x240ca1cc77ac9c65),UINT64_C(0x2de92c6f592b0275),UINT64_C(0x4a7484aa6ea6e483),UINT64_C(0x5cb0a9dcbd41fbd4),UINT64_C(0x76f988da831153b5),UINT64_C(0x983e5152ee66dfab),UINT64_C(0xa831c66d2db43210),UINT64_C(0xb00327c898fb213f),UINT64_C(0xbf597fc7beef0ee4),UINT64_C(0xc6e00bf33da88fc2),UINT64_C(0xd5a79147930aa725),UINT64_C(0x06ca6351e003826f),UINT64_C(0x142929670a0e6e70),UINT64_C(0x27b70a8546d22ffc),UINT64_C(0x2e1b21385c26c926),UINT64_C(0x4d2c6dfc5ac42aed),UINT64_C(0x53380d139d95b3df),UINT64_C(0x650a73548baf63de),UINT64_C(0x766a0abb3c77b2a8),UINT64_C(0x81c2c92e47edaee6),UINT64_C(0x92722c851482353b),UINT64_C(0xa2bfe8a14cf10364),UINT64_C(0xa81a664bbc423001),UINT64_C(0xc24b8b70d0f89791),UINT64_C(0xc76c51a30654be30),UINT64_C(0xd192e819d6ef5218),UINT64_C(0xd69906245565a910),UINT64_C(0xf40e35855771202a),UINT64_C(0x106aa07032bbd1b8),UINT64_C(0x19a4c116b8d2d0c8),UINT64_C(0x1e376c085141ab53),UINT64_C(0x2748774cdf8eeb99),UINT64_C(0x34b0bcb5e19b48a8),UINT64_C(0x391c0cb3c5c95a63),UINT64_C(0x4ed8aa4ae3418acb),UINT64_C(0x5b9cca4f7763e373),UINT64_C(0x682e6ff3d6b2b8a3),UINT64_C(0x748f82ee5defb2fc),UINT64_C(0x78a5636f43172f60),UINT64_C(0x84c87814a1f0ab72),UINT64_C(0x8cc702081a6439ec),UINT64_C(0x90befffa23631e28),UINT64_C(0xa4506cebde82bde9),UINT64_C(0xbef9a3f7b2c67915),UINT64_C(0xc67178f2e372532b),UINT64_C(0xca273eceea26619c),UINT64_C(0xd186b8c721c0c207),UINT64_C(0xeada7dd6cde0eb1e),UINT64_C(0xf57d4f7fee6ed178),UINT64_C(0x06f067aa72176fba),UINT64_C(0x0a637dc5a2c898a6),UINT64_C(0x113f9804bef90dae),UINT64_C(0x1b710b35131c471b),UINT64_C(0x28db77f523047d84),UINT64_C(0x32caab7b40c72493),UINT64_C(0x3c9ebe0a15c9bebc),UINT64_C(0x431d67c49c100d4c),UINT64_C(0x4cc5d4becb3e42b6),UINT64_C(0x597f299cfc657e2a),UINT64_C(0x5fcb6fab3ad6faec),UINT64_C(0x6c44198c4a475817)};

static uint64_t dc_ror(uint64_t x, unsigned n) {return (x>>n)|(x<<(64-n));}
static uint64_t dc_be64(const unsigned char *p) {
    uint64_t v=0;unsigned i;for(i=0;i<8;++i)v=(v<<8)|p[i];return v;
}
static void dc_put64(unsigned char *p,uint64_t value,int little) {
    unsigned i;for(i=0;i<8;++i){p[little?i:7-i]=(unsigned char)value;value>>=8;}
}
static void dc_sha_block(dg_sha512 *s,const unsigned char *block) {
    uint64_t w[80],a,b,c,d,e,f,g,h;unsigned i;
    for(i=0;i<16;++i)w[i]=dc_be64(block+i*8);
    for(i=16;i<80;++i){
        uint64_t x=w[i-15],y=w[i-2];
        w[i]=w[i-16]+(dc_ror(x,1)^dc_ror(x,8)^(x>>7))+w[i-7]+(dc_ror(y,19)^dc_ror(y,61)^(y>>6));
    }
    a=s->words[0];b=s->words[1];c=s->words[2];d=s->words[3];
    e=s->words[4];f=s->words[5];g=s->words[6];h=s->words[7];
    for(i=0;i<80;++i){
        uint64_t t1=h+(dc_ror(e,14)^dc_ror(e,18)^dc_ror(e,41))+((e&f)^(~e&g))+dc_sha_k[i]+w[i];
        uint64_t t2=(dc_ror(a,28)^dc_ror(a,34)^dc_ror(a,39))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->words[0]+=a;s->words[1]+=b;s->words[2]+=c;s->words[3]+=d;
    s->words[4]+=e;s->words[5]+=f;s->words[6]+=g;s->words[7]+=h;
}
static void dc_sha_init(dg_sha512 *s) {memset(s,0,sizeof(*s));memcpy(s->words,dc_sha_iv,sizeof(dc_sha_iv));}
static void dc_sha_update(dg_sha512 *s,const void *data,size_t size) {
    const unsigned char *p=(const unsigned char *)data;s->length+=size;
    while(size){size_t n=128-s->used;if(n>size)n=size;memcpy(s->buffer+s->used,p,n);
        s->used+=n;p+=n;size-=n;if(s->used==128){dc_sha_block(s,s->buffer);s->used=0;}}
}
static void dc_sha_finish(dg_sha512 *s) {
    unsigned char pad[256]={0};uint64_t length=s->length;size_t size=s->used<112?128-s->used:256-s->used;
    pad[0]=0x80;dc_put64(pad+size-16,length>>61,0);dc_put64(pad+size-8,length<<3,0);dc_sha_update(s,pad,size);
}
static void dc_sha_export(const dg_sha512 *s,unsigned char *out,int little) {
    unsigned i;for(i=0;i<8;++i)dc_put64(out+8*i,s->words[i],little);
}
void dg_sha512_digest(const void *data,size_t size,unsigned char out[64]) {
    dg_sha512 s;dc_sha_init(&s);dc_sha_update(&s,data,size);dc_sha_finish(&s);dc_sha_export(&s,out,0);memset(&s,0,sizeof(s));
}
static void dc_mt_twist(dg_cipher *c) {
    size_t i;for(i=0;i<624;++i){uint32_t x=(c->mt[i]&UINT32_C(0x80000000))|(c->mt[(i+1)%624]&UINT32_C(0x7fffffff));
        c->mt[i]=c->mt[(i+397)%624]^(x>>1)^((0U-(x&1U))&UINT32_C(0x9908b0df));}c->mt_pos=0;
}
static uint32_t dc_mt_next(dg_cipher *c) {
    uint32_t x;if(c->mt_pos==624)dc_mt_twist(c);x=c->mt[c->mt_pos++];x^=x>>11;x^=(x<<7)&UINT32_C(0x9d2c5680);
    x^=(x<<15)&UINT32_C(0xefc60000);return x^(x>>18);
}
void dg_cipher_init(dg_cipher *c,const void *seed,size_t seed_size,const void *key,size_t key_size) {
    unsigned i,j;dg_sha512 h;unsigned char digest[64];memset(c,0,sizeof(*c));dc_sha_init(&h);
    dc_sha_update(&h,seed,seed_size);dc_sha_update(&h,key,key_size);dc_sha_finish(&h);
    c->mt[0]=UINT32_MAX;
    for(i=0;i<39;++i){dc_sha_export(&h,digest,1);
        for(j=0;j<16&&16*i+j+1<624;++j){const unsigned char *p=digest+4*j;c->mt[16*i+j+1]=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
        dc_sha_update(&h,digest,64);dc_sha_update(&h,digest,64);
    }
    c->mt_pos=624;dc_sha_init(&c->hash);memset(&h,0,sizeof(h));memset(digest,0,sizeof(digest));
}
static unsigned char dc_draw(dg_cipher *c) {
    unsigned char value;
    if(!c->position||c->count<64){unsigned i;unsigned char random[128];
        for(i=0;i<32;++i){uint32_t x=dc_mt_next(c);random[4*i]=(unsigned char)x;random[4*i+1]=(unsigned char)(x>>8);
            random[4*i+2]=(unsigned char)(x>>16);random[4*i+3]=(unsigned char)(x>>24);}
        dc_sha_update(&c->hash,random,128);dc_sha_update(&c->hash,c->feedback,128);dc_sha_export(&c->hash,c->digest,1);
        if(c->count<64)++c->count;
    }
    value=c->digest[c->position]^c->previous;c->feedback[c->position]=c->feedback[64+c->position]=c->previous;
    c->previous=0;c->position=(unsigned char)((c->position+1)&63);return value;
}
void dg_cipher_decrypt(dg_cipher *c,unsigned char *data,size_t size) {
    size_t i;for(i=0;i<size;++i){unsigned char first=dc_draw(c),second=dc_draw(c),encrypted=data[i];
        data[i]=encrypted^first^second;c->previous=encrypted^second;}
}
size_t dg_cipher_state_size(void) {return sizeof(dg_cipher);}
