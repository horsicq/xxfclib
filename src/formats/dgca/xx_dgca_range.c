/* Independent DGCA range mathematics. SPDX-License-Identifier: MIT. */
#include "dgca_range.h"
int dg_range_init(dg_range *r,const unsigned char *data,size_t size) {
    if(!r||!data||size<2)return 0;
    r->data=data;r->size=size;r->position=2;r->previous=data[1];
    r->code=data[1]>>1;r->width=128;return 1;
}
static int normalize(dg_range *r) {
    if(!r||!r->width)return 0;
    while(r->width<=UINT32_C(0x800000)) {
        unsigned char next;
        if(r->position>=r->size)return 0;
        next=r->data[r->position++];
        r->code=(r->code<<8)|((uint32_t)(r->previous&1)<<7)|(next>>1);
        r->width<<=8;r->previous=next;
    }
    return r->code<r->width;
}
int dg_range_bit(dg_range *r,uint32_t p,unsigned *bit) {
    uint32_t cut;
    if(!bit||p>4096||!normalize(r))return 0;
    cut=(r->width>>12)*p;
    if(r->code<cut){r->width=cut;*bit=0;}
    else {r->code-=cut;r->width-=cut;*bit=1;}
    return r->width!=0;
}
int dg_range_uniform(dg_range *r,unsigned width,uint32_t *value) {
    uint32_t v=0;unsigned i;
    if(!value||width>31)return 0;
    for(i=0;i<width;i++) {
        uint32_t cut;
        if(!normalize(r))return 0;
        cut=r->width>>1;v<<=1;
        if(r->code<cut)r->width=cut;
        else {r->code-=cut;r->width-=cut;v|=1;}
    }
    *value=v;return 1;
}
int dg_range_integer(dg_range *r,uint32_t probs[32],unsigned shift,uint32_t *value) {
    unsigned exponent=0,bit;uint32_t suffix;
    if(!probs||!value||shift>15)return 0;
    while(exponent<32) {
        uint32_t p=probs[exponent];
        if(!dg_range_bit(r,p,&bit))return 0;
        probs[exponent]=bit?p-(p>>shift):p+((4096-p)>>shift);
        if(bit)break;
        exponent++;
    }
    if(exponent<2){*value=exponent;return 1;}
    if(!dg_range_uniform(r,exponent-1,&suffix))return 0;
    *value=(UINT32_C(1)<<(exponent-1))+suffix;return 1;
}
