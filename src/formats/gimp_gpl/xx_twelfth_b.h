/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded primitives. Each reader validates its own complete grammar.
 */
#ifndef XX_TWELFTH_B_H
#define XX_TWELFTH_B_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
static bool tb_parse(Abstractformat *,pm_stream *,const uint8_t *,uint64_t,xx_pd_struct *);
static bool tb_quick(Abstractformat *,uint64_t);
static __inline bool tb_span(uint64_t p,uint64_t z,uint64_t n) {return p<=n && z<=n-p;}
static __inline bool tb_stop(xx_pd_struct *pd) {return pd && xx_pd_is_stopped(pd);}
static __inline bool tb_tag(const uint8_t *p,const char *t,size_t n) {return !xx_rt_memcmp(p,t,n);}
static __inline bool tb_zero(const uint8_t *p,uint64_t n) {uint64_t i;for(i=0;i<n;++i)if(p[i])return false;return true;}
static __inline bool tb_emit(Abstractformat *f,pm_stream *s,const char *t,uint64_t p,uint64_t z,uint64_t n) {return z && s->count<4096 && tb_span(p,z,n) && pm_add(f,s,t,(int64_t)p,(int64_t)z);}
static __inline bool tb_cover(Abstractformat *f,pm_stream *s,const char *label,uint64_t n) {size_t i,initial=s->count;uint64_t covered=0;for(i=0;i<initial;++i){uint64_t start=(uint64_t)(s->items[i].offset-f->base_address),size=(uint64_t)s->items[i].packed_size;if(s->items[i].memory)continue;if(start<covered)return false;if(start>covered&&!tb_emit(f,s,label,covered,start-covered,n))return false;covered=start+size;}return covered==n||tb_emit(f,s,label,covered,n-covered,n);}
static __inline bool tb_f32(uint32_t u) {return (u&0x7f800000U)!=0x7f800000U;}
typedef struct tb_bin {const uint8_t *b;uint64_t p,n;xx_pd_struct *pd;} tb_bin;
static __inline bool tb_take(tb_bin *q,uint64_t n,const uint8_t **value) {if(tb_stop(q->pd)||!tb_span(q->p,n,q->n))return false;if(value)*value=q->b+q->p;q->p+=n;return true;}
static __inline bool tb_count(tb_bin *q,uint32_t maximum,uint32_t *value) {const uint8_t *p;if(!tb_take(q,4,&p))return false;*value=pm_le32(p);return *value<=maximum;}
static __inline bool tb_floats(tb_bin *q,unsigned count) {const uint8_t *p;unsigned i;if(!tb_take(q,(uint64_t)count*4,&p))return false;for(i=0;i<count;++i)if(!tb_f32(pm_le32(p+i*4)))return false;return true;}
static __inline bool tb_float(tb_bin *q,double *value) {const uint8_t *p;union {uint32_t u;float f;} v;if(!tb_take(q,4,&p)||!tb_f32(v.u=pm_le32(p)))return false;*value=v.f;return true;}
static __inline bool tb_index(tb_bin *q,unsigned size,bool unsign,int32_t *value) {const uint8_t *p;uint32_t u;if(!tb_take(q,size,&p))return false;u=size==1?p[0]:size==2?pm_le16(p):pm_le32(p);if(!unsign&&size<4&&(u&(1U<<(size*8-1))))u|=~0U<<(size*8);if(unsign&&u>0x7fffffffU)return false;*value=(int32_t)u;return unsign||*value>=-1;}
typedef struct tb_ids {uint32_t *values,size,work;} tb_ids;
static __inline bool tb_ids_init(tb_ids *set,uint32_t count) {uint32_t size=8;while(size<count*2)size*=2;set->size=size;set->work=0;set->values=(uint32_t *)xx_mem_alloc((size_t)size*4);if(!set->values)return false;xx_mem_zero(set->values,(size_t)size*4);return true;}
static __inline bool tb_id(tb_ids *set,uint32_t value,bool insert,xx_pd_struct *pd) {uint32_t i,start=value*2654435761U;if(!value)return false;for(i=0;i<set->size;++i){uint32_t at=(start+i)&(set->size-1);if(++set->work>16000000||tb_stop(pd))return false;if(!set->values[at]){if(!insert)return false;set->values[at]=value;return true;}if(set->values[at]==value)return !insert;}return false;}
static __inline uint32_t tb_uint(const uint8_t *p,unsigned z) {uint32_t u=0;unsigned i;for(i=0;i<z;++i)u=(u<<8)|p[i];return u;}
static __inline int32_t tb_sint(const uint8_t *p,unsigned z) {uint32_t u=tb_uint(p,z);if(z<4 && (u&(1U<<(z*8-1))))u|=~0U<<(z*8);return (int32_t)u;}
static __inline bool tb_utf(const uint8_t *b,uint64_t n,bool ascii,xx_pd_struct *pd) {
 uint64_t p=0;while(p<n){uint32_t c,min;unsigned k,i;uint8_t v;if((p&4095)==0&&tb_stop(pd))return false;v=b[p++];
 if(v<128){if(!v || (v<32 && v!=9 && v!=10 && v!=13))return false;continue;}
 if(ascii)return false;if(v>=0xc2 && v<=0xdf){k=1;c=v&31;min=128;}else if(v>=0xe0&&v<=0xef){k=2;c=v&15;min=2048;}else if(v>=0xf0&&v<=0xf4){k=3;c=v&7;min=65536;}else return false;
 if(!tb_span(p,k,n))return false;for(i=0;i<k;++i){if((b[p]&0xc0)!=0x80)return false;c=(c<<6)|(b[p++]&63);}
 if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;}return true;
}
typedef struct tb_text {const uint8_t *b;uint64_t p,end,start,stop,t;} tb_text;
static __inline bool tb_line(tb_text *q) {
 uint64_t p=q->p;if(p>=q->end)return false;q->start=p;
 while(p<q->end&&q->b[p]!=10&&q->b[p]!=13){if(p-q->start>=8192)return false;++p;}q->stop=p;
 if(p<q->end&&q->b[p]==13)++p;if(p<q->end&&q->b[p]==10)++p;q->p=p;q->t=q->start;return true;
}
static __inline void tb_space(tb_text *q) {while(q->t<q->stop&&(q->b[q->t]==32||q->b[q->t]==9))++q->t;}
static __inline bool tb_done(tb_text *q) {tb_space(q);return q->t==q->stop || q->b[q->t]=='#';}
static __inline bool tb_next(tb_text *q) {while(q->p<q->end){if(!tb_line(q))return false;if(!tb_done(q))return true;}return false;}
static __inline bool tb_word(tb_text *q,const char *s) {
 uint64_t p;size_t n=xx_rt_strlen(s);tb_space(q);p=q->t;
 if(!tb_span(p,n,q->stop)||!tb_tag(q->b+p,s,n)||(p+n<q->stop&&q->b[p+n]!=32&&q->b[p+n]!=9))return false;q->t=p+n;return true;
}
static __inline bool tb_i(tb_text *q,int32_t *v) {
 uint64_t p;uint32_t u=0;bool neg=false;tb_space(q);p=q->t;
 if(p<q->stop&&(q->b[p]=='-'||q->b[p]=='+'))neg=q->b[p++]=='-';
 if(p>=q->stop||q->b[p]<'0'||q->b[p]>'9')return false;
 while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){uint32_t d=q->b[p++]-'0';if(u>(2147483647U-d)/10)return false;u=u*10+d;}
 q->t=p;*v=neg?-(int32_t)u:(int32_t)u;return true;
}
static __inline bool tb_num(tb_text *q,double *value) {
 uint64_t p;double v=0,scale=1;unsigned digits=0,frac=0;int exp=0;bool neg=false,eneg=false;tb_space(q);p=q->t;
 if(p<q->stop&&(q->b[p]=='-'||q->b[p]=='+'))neg=q->b[p++]=='-';
 while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){if(++digits>36)return false;v=v*10+(q->b[p++]-'0');}
 if(p<q->stop&&q->b[p]=='.'){++p;while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){if(++digits>36)return false;v=v*10+(q->b[p++]-'0');++frac;}}
 if(!digits)return false;
 if(p<q->stop&&(q->b[p]=='e'||q->b[p]=='E')){++p;if(p<q->stop&&(q->b[p]=='+'||q->b[p]=='-'))eneg=q->b[p++]=='-';if(p>=q->stop||q->b[p]<'0'||q->b[p]>'9')return false;while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){exp=exp*10+q->b[p++]-'0';if(exp>38)return false;}}
 if(p<q->stop&&q->b[p]!=32&&q->b[p]!=9&&q->b[p]!='#')return false;
 while(frac--)scale*=10;v/=scale;while(exp--)v=eneg?v/10:v*10;if(v>3.402823466e38)return false;
 q->t=p;*value=neg?-v:v;return true;
}
static __inline bool tb_nums(tb_text *q,unsigned n) {unsigned i;double v;for(i=0;i<n;++i)if(!tb_num(q,&v))return false;return tb_done(q);}
static __inline bool tb_string(tb_text *q) {
 uint64_t p;tb_space(q);p=q->t;if(p>=q->stop||q->b[p++]!='"')return false;
 while(p<q->stop){uint8_t c=q->b[p++];if(c=='"'){q->t=p;return true;}if(c=='\\'){if(p>=q->stop||(q->b[p]!='"'&&q->b[p]!='\\'))return false;++p;}}
 return false;
}
static __inline uint32_t tb_crc(const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t crc=0U;uint64_t i=0;
 while(i<n) {size_t part=n-i>4096U ? 4096U:(size_t)(n-i);if(tb_stop(pd))return 0U;crc=xx_crc32_calc(crc,b+(size_t)i,part);i+=part;}
 return crc;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b;uint64_t p=0;bool ok=false;
 if(available<1||available>33554432||tb_stop(pd)||!tb_quick(f,(uint64_t)available))return false;
 b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 while(p<(uint64_t)available){size_t z=(uint64_t)available-p>65536?65536:(size_t)((uint64_t)available-p);
  if(tb_stop(pd)||!pm_read(f,(int64_t)p,b+p,z))goto done;p+=z;}
 ok=!tb_stop(pd)&&tb_parse(f,s,b,(uint64_t)available,pd)&&!tb_stop(pd);
done:xx_mem_free(b);return ok;
}
#endif
