/* SPDX-License-Identifier: MIT. Private tenth container primitives. */
#ifndef XX_TENTH_CONTAINERS_H
#define XX_TENTH_CONTAINERS_H
#include "../xx_ninth_data.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/data/xx_data.h"
static XXFC_MAYBE_UNUSED bool th_mem(Abstractformat *f,pm_stream *s,const char *name,uint8_t **p,uint64_t n) {
    if(n>67108864 || s->count>=4096 || !pm_add(f,s,name,0,0)) return false;
    s->items[s->count-1].memory=*p;s->items[s->count-1].size=(int64_t)n;s->items[s->count-1].packed_size=(int64_t)n;*p=NULL;return true;
}
static XXFC_MAYBE_UNUSED bool th_var(nh_blob *b,uint64_t *at,uint64_t end,uint64_t *value) {
    unsigned shift=0;uint64_t v=0;uint8_t c;
    do {if(*at>=end || !nh_span(b,*at,1) || shift>=70) return false;c=b->p[(size_t)(*at)++];if(shift==63 && (c&0xfe)) return false;v|=(uint64_t)(c&127)<<shift;shift+=7;} while(c&128);
    *value=v;return true;
}
static XXFC_MAYBE_UNUSED bool th_z(nh_blob *b,uint64_t *at,uint64_t end,bool empty) {
    uint64_t start=*at;while(*at<end && nh_span(b,*at,1) && b->p[(size_t)*at]) {if(*at-start>=65536) return false;++*at;}
    if(*at>=end || (!empty && *at==start) || !fourth_utf8(b->p+(size_t)start,(size_t)(*at-start),b->pd)) { return false; } ++*at;return true;
}
static uint32_t th_mask(uint32_t c) {return ((c>>15)|(c<<17))+0xa282ead8U;}
static XXFC_MAYBE_UNUSED bool th_crc(nh_blob *b,uint64_t at,uint64_t n,uint32_t want,bool castagnoli) {
    uint32_t c=0;uint64_t i=0;if(!nh_span(b,at,n)) return false;
    while(i<n) {size_t part=n-i>65536 ? 65536:(size_t)(n-i);if(fd_stop(b->pd)) return false;c=castagnoli ? xx_crc32c_calc(c,b->p+(size_t)(at+i),part):xx_crc32_calc(c,b->p+(size_t)(at+i),part);i+=part;}
    return (castagnoli ? th_mask(c):c)==want;
}
static bool th_copy(uint8_t *out,uint64_t *made,uint64_t cap,uint64_t distance,uint64_t n,xx_pd_struct *pd) {
    uint64_t i;if(!distance || distance>*made || !eh_span(*made,n,cap)) return false;
    for(i=0;i<n;++i) {if(!(i&65535U) && fd_stop(pd)) return false;out[(size_t)*made]=out[(size_t)(*made-distance)];++*made;}return true;
}
/* Independently bounded Snappy raw tags; checks exact source and output sizes. */
static XXFC_MAYBE_UNUSED bool th_snappy(const uint8_t *p,uint64_t n,uint8_t *out,uint64_t cap,xx_pd_struct *pd) {
    uint64_t at=0,made=0,want=0,v,len,dist;unsigned shift=0,k;uint8_t c,tag;
    do {if(at>=n || shift>=35) return false;c=p[(size_t)at++];want|=(uint64_t)(c&127)<<shift;shift+=7;} while(c&128);
    if(want!=cap) return false;
    while(at<n) {if(fd_stop(pd)) return false;tag=p[(size_t)at++];k=tag&3;
        if(!k) {v=tag>>2;if(v<60) len=v+1;else {k=(unsigned)(v-59);if(!eh_span(at,k,n)) return false;v=0;for(unsigned j=0;j<k;++j) v|=(uint64_t)p[(size_t)at++]<<(8*j);len=v+1;}
            if(!eh_span(at,len,n) || !eh_span(made,len,cap)) { return false; } xx_rt_memcpy(out+(size_t)made,p+(size_t)at,(size_t)len);made+=len;at+=len;
        } else {if(k==1) {if(at>=n) return false;len=4+((tag>>2)&7);dist=((uint64_t)(tag&224)<<3)|p[(size_t)at++];}else {unsigned width=k==2 ? 2:4;if(!eh_span(at,width,n)) return false;len=1+(tag>>2);dist=0;for(unsigned j=0;j<width;++j) dist|=(uint64_t)p[(size_t)at++]<<(8*j);}
            if(!th_copy(out,&made,cap,dist,len,pd)) return false;
        }
    }return made==cap;
}
/* LZF and FastLZ share literals/backreferences but differ in length extension. */
static XXFC_MAYBE_UNUSED bool th_lz(const uint8_t *p,uint64_t n,uint8_t *out,uint64_t cap,xx_pd_struct *pd,unsigned mode) {
    uint64_t at=0,made=0,len,dist;unsigned level=1;uint8_t ctrl,code;
    if(!n) { return false; } if(mode) {level=(p[0]>>5)+1;if(level>2) return false;}
    while(at<n) {if(fd_stop(pd)) return false;ctrl=p[(size_t)at++];if(mode && at==1) ctrl&=31;
        if(ctrl<32) {len=(uint64_t)ctrl+1;if(!eh_span(at,len,n) || !eh_span(made,len,cap)) return false;xx_rt_memcpy(out+(size_t)made,p+(size_t)at,(size_t)len);made+=len;at+=len;}
        else {len=ctrl>>5;dist=(uint64_t)(ctrl&31)<<8;
            if(!mode) {if(len==7) {if(at>=n) return false;len+=p[(size_t)at++];}len+=2;}
            else {--len;if(len==6) {do {if(at>=n) return false;code=p[(size_t)at++];len+=code;if(len>cap) return false;} while(level==2 && code==255);}len+=3;}
            if(at>=n) { return false; } code=p[(size_t)at++];dist+=code+1;
            if(mode && level==2 && code==255 && (ctrl&31)==31) {if(!eh_span(at,2,n)) return false;dist=8192+xx_data_get_u16(p+(size_t)at, 2, 0, true);at+=2;}
            if(!th_copy(out,&made,cap,dist,len,pd)) return false;
        }
    }return made==cap;
}
#endif
