/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded primitives. Each reader validates its own complete grammar.
 */
#ifndef XX_ELEVENTH_MEDIA_H
#define XX_ELEVENTH_MEDIA_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
static bool eg_parse(Abstractformat *,pm_stream *,const uint8_t *,uint64_t,xx_pd_struct *);
static bool eg_quick(Abstractformat *,uint64_t);
static __inline bool eg_span(uint64_t p,uint64_t z,uint64_t n) {return p<=n && z<=n-p;}
static __inline bool eg_stop(xx_pd_struct *pd) {return pd && xx_pd_is_stopped(pd);}
static __inline bool eg_tag(const uint8_t *p,const char *t,size_t n) {return !xx_rt_memcmp(p,t,n);}
static __inline bool eg_zero(const uint8_t *p,uint64_t n) {uint64_t i;for(i=0;i<n;++i)if(p[i])return false;return true;}
static __inline bool eg_emit(Abstractformat *f,pm_stream *s,const char *t,uint64_t p,uint64_t z,uint64_t n) {return z && s->count<4096 && eg_span(p,z,n) && pm_add(f,s,t,(int64_t)p,(int64_t)z);}
static __inline bool eg_f32(uint32_t u) {return (u&0x7f800000U)!=0x7f800000U;}
static __inline uint32_t eg_uint(const uint8_t *p,unsigned z) {uint32_t u=0;unsigned i;for(i=0;i<z;++i)u=(u<<8)|p[i];return u;}
static __inline int32_t eg_sint(const uint8_t *p,unsigned z) {uint32_t u=eg_uint(p,z);if(z<4 && (u&(1U<<(z*8-1))))u|=~0U<<(z*8);return (int32_t)u;}
static __inline bool eg_utf(const uint8_t *b,uint64_t n,bool ascii,xx_pd_struct *pd) {
 uint64_t p=0;while(p<n){uint32_t c,min;unsigned k,i;uint8_t v;if((p&4095)==0&&eg_stop(pd))return false;v=b[p++];
 if(v<128){if(!v || (v<32 && v!=9 && v!=10 && v!=13))return false;continue;}
 if(ascii)return false;if(v>=0xc2 && v<=0xdf){k=1;c=v&31;min=128;}else if(v>=0xe0&&v<=0xef){k=2;c=v&15;min=2048;}else if(v>=0xf0&&v<=0xf4){k=3;c=v&7;min=65536;}else return false;
 if(!eg_span(p,k,n))return false;for(i=0;i<k;++i){if((b[p]&0xc0)!=0x80)return false;c=(c<<6)|(b[p++]&63);}
 if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;}return true;
}
typedef struct eg_text {const uint8_t *b;uint64_t p,end,start,stop,t;} eg_text;
static __inline bool eg_line(eg_text *q) {
 uint64_t p=q->p;if(p>=q->end)return false;q->start=p;
 while(p<q->end&&q->b[p]!=10&&q->b[p]!=13){if(p-q->start>=8192)return false;++p;}q->stop=p;
 if(p<q->end&&q->b[p]==13)++p;if(p<q->end&&q->b[p]==10)++p;q->p=p;q->t=q->start;return true;
}
static __inline void eg_space(eg_text *q) {while(q->t<q->stop&&(q->b[q->t]==32||q->b[q->t]==9))++q->t;}
static __inline bool eg_done(eg_text *q) {eg_space(q);return q->t==q->stop || q->b[q->t]=='#';}
static __inline bool eg_word(eg_text *q,const char *s) {
 uint64_t p;size_t n=xx_rt_strlen(s);eg_space(q);p=q->t;
 if(!eg_span(p,n,q->stop)||!eg_tag(q->b+p,s,n)||(p+n<q->stop&&q->b[p+n]!=32&&q->b[p+n]!=9))return false;q->t=p+n;return true;
}
static __inline bool eg_i(eg_text *q,int32_t *v) {
 uint64_t p;uint32_t u=0;bool neg=false;eg_space(q);p=q->t;
 if(p<q->stop&&(q->b[p]=='-'||q->b[p]=='+'))neg=q->b[p++]=='-';
 if(p>=q->stop||q->b[p]<'0'||q->b[p]>'9')return false;
 while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){uint32_t d=q->b[p++]-'0';if(u>(2147483647U-d)/10)return false;u=u*10+d;}
 q->t=p;*v=neg?-(int32_t)u:(int32_t)u;return true;
}
static __inline bool eg_num(eg_text *q,double *value) {
 uint64_t p;double v=0,scale=1;unsigned digits=0,frac=0;int exp=0;bool neg=false,eneg=false;eg_space(q);p=q->t;
 if(p<q->stop&&(q->b[p]=='-'||q->b[p]=='+'))neg=q->b[p++]=='-';
 while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){if(++digits>18)return false;v=v*10+(q->b[p++]-'0');}
 if(p<q->stop&&q->b[p]=='.'){++p;while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){if(++digits>18)return false;v=v*10+(q->b[p++]-'0');++frac;}}
 if(!digits)return false;
 if(p<q->stop&&(q->b[p]=='e'||q->b[p]=='E')){++p;if(p<q->stop&&(q->b[p]=='+'||q->b[p]=='-'))eneg=q->b[p++]=='-';if(p>=q->stop||q->b[p]<'0'||q->b[p]>'9')return false;while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){exp=exp*10+q->b[p++]-'0';if(exp>38)return false;}}
 if(p<q->stop&&q->b[p]!=32&&q->b[p]!=9&&q->b[p]!='#')return false;
 while(frac--)scale*=10;v/=scale;while(exp--)v=eneg?v/10:v*10;if(v>3.402823466e38)return false;
 q->t=p;*value=neg?-v:v;return true;
}
static __inline bool eg_nums(eg_text *q,unsigned n) {unsigned i;double v;for(i=0;i<n;++i)if(!eg_num(q,&v))return false;return eg_done(q);}
static __inline bool eg_string(eg_text *q) {
 uint64_t p;eg_space(q);p=q->t;if(p>=q->stop||q->b[p++]!='"')return false;
 while(p<q->stop){uint8_t c=q->b[p++];if(c=='"'){q->t=p;return true;}if(c=='\\'){if(p>=q->stop||(q->b[p]!='"'&&q->b[p]!='\\'))return false;++p;}}
 return false;
}
static __inline uint32_t eg_crc(const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t c=0;uint64_t i;for(i=0;i<n;){uint64_t remain=n-i;size_t part=(size_t)(remain>4096?4096:remain);if(eg_stop(pd))return 0;c=xx_crc32_calc(c,b+i,part);i+=part;}return c;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b;uint64_t p=0;bool ok=false;
 if(available<1||available>67108864||eg_stop(pd)||!eg_quick(f,(uint64_t)available))return false;
 b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 while(p<(uint64_t)available){size_t z=(uint64_t)available-p>65536?65536:(size_t)((uint64_t)available-p);
  if(eg_stop(pd)||!pm_read(f,(int64_t)p,b+p,z))goto done;p+=z;}
 ok=!eg_stop(pd)&&eg_parse(f,s,b,(uint64_t)available,pd)&&!eg_stop(pd);
done:xx_mem_free(b);return ok;
}
#endif
