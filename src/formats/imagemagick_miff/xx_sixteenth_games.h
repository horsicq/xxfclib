/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded primitives. Each reader validates its own complete grammar.
 */
#ifndef XX_SIXTEENTH_GAMES_H
#define XX_SIXTEENTH_GAMES_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
static bool hg_parse(Abstractformat *,pm_stream *,const uint8_t *,uint64_t,xx_pd_struct *);
static bool hg_quick(Abstractformat *,uint64_t);
static __inline bool hg_span(uint64_t p,uint64_t z,uint64_t n) {return p<=n && z<=n-p;}
static __inline bool hg_stop(xx_pd_struct *pd) {return pd && xx_pd_is_stopped(pd);}
static __inline bool hg_tag(const uint8_t *p,const char *t,size_t n) {return !xx_rt_memcmp(p,t,n);}
static __inline bool hg_zero(const uint8_t *p,uint64_t n) {uint64_t i;for(i=0;i<n;++i)if(p[i])return false;return true;}
static __inline bool hg_emit(Abstractformat *f,pm_stream *s,const char *t,uint64_t p,uint64_t z,uint64_t n) {return z && s->count<4096 && hg_span(p,z,n) && pm_add(f,s,t,(int64_t)p,(int64_t)z);}
static __inline bool hg_memory(Abstractformat *f,pm_stream *s,const char *t,uint8_t *bytes,uint64_t z) {pm_member *m;if(!bytes||!z||z>33554432||s->count>=4096||!pm_add(f,s,t,0,0))return false;m=&s->items[s->count-1];m->memory=bytes;m->size=(int64_t)z;m->packed_size=0;return true;}
static __inline bool hg_cover(Abstractformat *f,pm_stream *s,const char *label,uint64_t n) {size_t i,initial=s->count;uint64_t covered=0;for(i=0;i<initial;++i){uint64_t start=(uint64_t)(s->items[i].offset-f->base_address),size=(uint64_t)s->items[i].packed_size;if(s->items[i].memory)continue;if(start<covered)return false;if(start>covered&&!hg_emit(f,s,label,covered,start-covered,n))return false;covered=start+size;}return covered==n||hg_emit(f,s,label,covered,n-covered,n);}
static __inline bool hg_f32(uint32_t u) {return (u&0x7f800000U)!=0x7f800000U;}
typedef struct hg_bin {const uint8_t *b;uint64_t p,n;xx_pd_struct *pd;} hg_bin;
static __inline bool hg_take(hg_bin *q,uint64_t n,const uint8_t **value) {if(hg_stop(q->pd)||!hg_span(q->p,n,q->n))return false;if(value)*value=q->b+q->p;q->p+=n;return true;}
static __inline bool hg_count(hg_bin *q,uint32_t maximum,uint32_t *value) {const uint8_t *p;if(!hg_take(q,4,&p))return false;*value=xx_data_get_u32(p, 4, 0, false);return *value<=maximum;}
static __inline bool hg_floats(hg_bin *q,unsigned count) {const uint8_t *p;unsigned i;if(!hg_take(q,(uint64_t)count*4,&p))return false;for(i=0;i<count;++i)if(!hg_f32(xx_data_get_u32(p+i*4, 4, 0, false)))return false;return true;}
static __inline bool hg_float(hg_bin *q,double *value) {const uint8_t *p;union {uint32_t u;float f;} v;if(!hg_take(q,4,&p)||!hg_f32(v.u=xx_data_get_u32(p, 4, 0, false)))return false;*value=v.f;return true;}
static __inline bool hg_index(hg_bin *q,unsigned size,bool unsign,int32_t *value) {const uint8_t *p;uint32_t u;if(!hg_take(q,size,&p))return false;u=size==1?p[0]:size==2?xx_data_get_u16(p, 2, 0, false):xx_data_get_u32(p, 4, 0, false);if(!unsign&&size<4&&(u&(1U<<(size*8-1))))u|=~0U<<(size*8);if(unsign&&u>0x7fffffffU)return false;*value=(int32_t)u;return unsign||*value>=-1;}
typedef struct hg_ids {uint32_t *values,size,work;} hg_ids;
static __inline bool hg_ids_init(hg_ids *set,uint32_t count) {uint32_t size=8;while(size<count*2)size*=2;set->size=size;set->work=0;set->values=(uint32_t *)xx_mem_alloc((size_t)size*4);if(!set->values)return false;xx_mem_zero(set->values,(size_t)size*4);return true;}
static __inline bool hg_id(hg_ids *set,uint32_t value,bool insert,xx_pd_struct *pd) {uint32_t i,start=value*2654435761U;if(!value)return false;for(i=0;i<set->size;++i){uint32_t at=(start+i)&(set->size-1);if(++set->work>16000000||hg_stop(pd))return false;if(!set->values[at]){if(!insert)return false;set->values[at]=value;return true;}if(set->values[at]==value)return !insert;}return false;}
static __inline uint32_t hg_uint(const uint8_t *p,unsigned z) {uint32_t u=0;unsigned i;for(i=0;i<z;++i)u=(u<<8)|p[i];return u;}
static __inline int32_t hg_sint(const uint8_t *p,unsigned z) {uint32_t u=hg_uint(p,z);if(z<4 && (u&(1U<<(z*8-1))))u|=~0U<<(z*8);return (int32_t)u;}
static __inline bool hg_utf(const uint8_t *b,uint64_t n,bool ascii,xx_pd_struct *pd) {
 uint64_t p=0;while(p<n){uint32_t c,min;unsigned k,i;uint8_t v;if((p&4095)==0&&hg_stop(pd))return false;v=b[p++];
 if(v<128){if(!v || (v<32 && v!=9 && v!=10 && v!=13))return false;continue;}
 if(ascii) {return false; } if(v>=0xc2 && v<=0xdf){k=1;c=v&31;min=128;}else if(v>=0xe0&&v<=0xef){k=2;c=v&15;min=2048;}else if(v>=0xf0&&v<=0xf4){k=3;c=v&7;min=65536;}else return false;
 if(!hg_span(p,k,n)) {return false; } for(i=0;i<k;++i){if((b[p]&0xc0)!=0x80)return false;c=(c<<6)|(b[p++]&63);}
 if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;}return true;
}
typedef struct hg_text {const uint8_t *b;uint64_t p,end,start,stop,t;} hg_text;
static __inline bool hg_line(hg_text *q) {
 uint64_t p=q->p;if(p>=q->end)return false;q->start=p;
 while(p<q->end&&q->b[p]!=10&&q->b[p]!=13){if(p-q->start>=8192){q->p=q->end+1;return false;}++p;}q->stop=p;
 if(p<q->end&&q->b[p]==13) {++p; } if(p<q->end&&q->b[p]==10)++p;q->p=p;q->t=q->start;return true;
}
static __inline void hg_space(hg_text *q) {while(q->t<q->stop&&(q->b[q->t]==32||q->b[q->t]==9))++q->t;}
static __inline bool hg_done(hg_text *q) {hg_space(q);return q->t==q->stop || q->b[q->t]=='#';}
static __inline bool hg_next(hg_text *q) {while(q->p<q->end){if(!hg_line(q))return false;if(!hg_done(q))return true;}return false;}
static __inline bool hg_word(hg_text *q,const char *s) {
 uint64_t p;size_t n=xx_rt_strlen(s);hg_space(q);p=q->t;
 if(!hg_span(p,n,q->stop)||!hg_tag(q->b+p,s,n)||(p+n<q->stop&&q->b[p+n]!=32&&q->b[p+n]!=9)) {return false; } q->t=p+n;return true;
}
static __inline bool hg_i(hg_text *q,int32_t *v) {
 uint64_t p;uint32_t u=0;bool neg=false;hg_space(q);p=q->t;
 if(p<q->stop&&(q->b[p]=='-'||q->b[p]=='+'))neg=q->b[p++]=='-';
 if(p>=q->stop||q->b[p]<'0'||q->b[p]>'9')return false;
 while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){uint32_t d=q->b[p++]-'0';if(u>(2147483647U-d)/10)return false;u=u*10+d;}
 if(p<q->stop&&q->b[p]!=32&&q->b[p]!=9&&q->b[p]!='#') {return false; } q->t=p;*v=neg?-(int32_t)u:(int32_t)u;return true;
}
static __inline bool hg_num(hg_text *q,double *value) {
 uint64_t p;double v=0,scale=1;unsigned digits=0,frac=0;int exp=0;bool neg=false,eneg=false;hg_space(q);p=q->t;
 if(p<q->stop&&(q->b[p]=='-'||q->b[p]=='+'))neg=q->b[p++]=='-';
 while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){if(++digits>36)return false;v=v*10+(q->b[p++]-'0');}
 if(p<q->stop&&q->b[p]=='.'){++p;while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){if(++digits>36)return false;v=v*10+(q->b[p++]-'0');++frac;}}
 if(!digits)return false;
 if(p<q->stop&&(q->b[p]=='e'||q->b[p]=='E')){++p;if(p<q->stop&&(q->b[p]=='+'||q->b[p]=='-'))eneg=q->b[p++]=='-';if(p>=q->stop||q->b[p]<'0'||q->b[p]>'9')return false;while(p<q->stop&&q->b[p]>='0'&&q->b[p]<='9'){exp=exp*10+q->b[p++]-'0';if(exp>38)return false;}}
 if(p<q->stop&&q->b[p]!=32&&q->b[p]!=9&&q->b[p]!='#')return false;
 while(frac--) {scale*=10; } v/=scale;while(exp--)v=eneg?v/10:v*10;if(v>3.402823466e38)return false;
 q->t=p;*value=neg?-v:v;return true;
}
static __inline bool hg_nums(hg_text *q,unsigned n) {unsigned i;double v;for(i=0;i<n;++i)if(!hg_num(q,&v))return false;return hg_done(q);}
static __inline bool hg_string(hg_text *q) {
 uint64_t p;hg_space(q);p=q->t;if(p>=q->stop||q->b[p++]!='"')return false;
 while(p<q->stop){uint8_t c=q->b[p++];if(c=='"'){q->t=p;return true;}if(c=='\\'){if(p>=q->stop||(q->b[p]!='"'&&q->b[p]!='\\'))return false;++p;}}
 return false;
}
static __inline uint32_t hg_crc(const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t crc=0U;uint64_t i=0;
 while(i<n) {size_t part=n-i>4096U ? 4096U:(size_t)(n-i);if(hg_stop(pd))return 0U;crc=xx_crc32_calc(crc,b+(size_t)i,part);i+=part;}
 return crc;
}
typedef struct hg_lex {const uint8_t *b;uint64_t p,n;xx_pd_struct *pd;uint32_t work;bool hash,commas,comments;} hg_lex;
static __inline bool hg_word_ci(hg_text *q,const char *s) {uint64_t p;size_t i,z=xx_rt_strlen(s);hg_space(q);p=q->t;if(!hg_span(p,z,q->stop))return false;for(i=0;i<z;++i){uint8_t c=q->b[p+i];if(c>='A'&&c<='Z')c+=32;if(c!=(uint8_t)s[i])return false;}if(p+z<q->stop&&q->b[p+z]!=32&&q->b[p+z]!=9)return false;q->t=p+z;return true;}
static __inline bool hg_near(double a,double b) {double diff=a-b,scale=a<0?-a:a,other=b<0?-b:b;if(diff<0)diff=-diff;if(other>scale)scale=other;return diff<=scale*1e-12;}
static __inline bool hg_grid_rows(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,uint64_t at,uint64_t count,double nodata,double *lo,double *hi,xx_pd_struct *pd,bool eof) {hg_text q={b,at,n,0,0,0};uint64_t seen=0;bool values=false;unsigned rows=0;char label[48];while(q.p<n){double v;if(hg_stop(pd)||!hg_line(&q))return false;hg_space(&q);if(q.t==q.stop)continue;if(eof&&hg_word(&q,"#EOF")){if(!hg_done(&q))return false;while(q.p<n)if(!hg_line(&q)||!hg_done(&q))return false;break;}while(q.t<q.stop){hg_space(&q);if(q.t==q.stop)break;if(!hg_num(&q,&v)||++seen>count)return false;if(!hg_near(v,nodata)){if(!values){*lo=*hi=v;values=true;}else {if(v<*lo)*lo=v;if(v>*hi)*hi=v;}}}if(++rows>4093)return false;xx_rt_snprintf(label,sizeof(label),"samples-%u.txt",rows-1);if(!hg_emit(f,s,label,q.start,q.p-q.start,n))return false;}return seen==count&&hg_cover(f,s,"whitespace.txt",n);}
static __inline bool hg_skip(hg_lex *q) {
 while(q->p<q->n){uint8_t c=q->b[q->p];if(++q->work>16000000||hg_stop(q->pd))return false;
  if(c==32||c==9||c==10||c==13||(q->commas&&c==',')){++q->p;continue;}
  if(q->hash&&c=='#'){while(q->p<q->n&&q->b[q->p]!=10&&q->b[q->p]!=13){if(++q->work>16000000||((q->p&4095)==0&&hg_stop(q->pd)))return false;++q->p;}continue;}
  if(q->comments&&c=='/'&&hg_span(q->p,2,q->n)&&q->b[q->p+1]=='/'){q->p+=2;while(q->p<q->n&&q->b[q->p]!=10&&q->b[q->p]!=13){if(++q->work>16000000||hg_stop(q->pd))return false;++q->p;}continue;}
  if(q->comments&&c=='/'&&hg_span(q->p,2,q->n)&&q->b[q->p+1]=='*'){q->p+=2;while(hg_span(q->p,2,q->n)&&!(q->b[q->p]=='*'&&q->b[q->p+1]=='/')){if(++q->work>16000000||hg_stop(q->pd))return false;++q->p;}if(!hg_span(q->p,2,q->n))return false;q->p+=2;continue;}
  break;
 }return true;
}
static __inline bool hg_char(hg_lex *q,uint8_t c) {if(!hg_skip(q)||q->p==q->n||q->b[q->p]!=c)return false;++q->p;return true;}
static __inline bool hg_ident_char(uint8_t c) {return(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';}
static __inline bool hg_kw(hg_lex *q,const char *s) {uint64_t p;size_t z=xx_rt_strlen(s);if(!hg_skip(q))return false;p=q->p;if(!hg_span(p,z,q->n)||!hg_tag(q->b+p,s,z)||(p+z<q->n&&hg_ident_char(q->b[p+z])))return false;q->p=p+z;return true;}
static __inline bool hg_ident(hg_lex *q,uint64_t *at,uint64_t *size) {uint64_t p;if(!hg_skip(q))return false;p=q->p;if(p==q->n||!((q->b[p]>='A'&&q->b[p]<='Z')||(q->b[p]>='a'&&q->b[p]<='z')||q->b[p]=='_'))return false;while(q->p<q->n&&hg_ident_char(q->b[q->p])){if(q->p-p>=255)return false;++q->p;}if(at)*at=p;if(size)*size=q->p-p;return true;}
static __inline bool hg_number(hg_lex *q,double *v) {
 uint64_t start,p;hg_text t;bool ok;if(!hg_skip(q))return false;start=p=q->p;
 while(p<q->n&&((q->b[p]>='0'&&q->b[p]<='9')||q->b[p]=='+'||q->b[p]=='-'||q->b[p]=='.'||q->b[p]=='E'||q->b[p]=='e')){if(p-start>=96)return false;++p;}
 t.b=q->b;t.p=start;t.end=p;t.start=start;t.stop=p;t.t=start;ok=hg_num(&t,v)&&t.t==p;if(ok)q->p=p;return ok;
}
static __inline bool hg_integer(hg_lex *q,int32_t *v) {uint64_t start,p;hg_text t;bool ok;if(!hg_skip(q))return false;start=p=q->p;if(p<q->n&&(q->b[p]=='+'||q->b[p]=='-'))++p;while(p<q->n&&q->b[p]>='0'&&q->b[p]<='9'){if(p-start>=12)return false;++p;}t.b=q->b;t.p=start;t.end=p;t.start=start;t.stop=p;t.t=start;ok=hg_i(&t,v)&&t.t==p;if(ok&&p<q->n&&!((q->b[p]==32)||(q->b[p]==9)||(q->b[p]==10)||(q->b[p]==13)||(q->b[p]==']')||(q->b[p]=='}')||(q->b[p]==',')||(q->b[p]=='#')))ok=false;if(ok)q->p=p;return ok;}
static __inline bool hg_quoted(hg_lex *q,uint8_t delim,uint64_t *at,uint64_t *size) {uint64_t p;if(!hg_skip(q)||q->p==q->n||q->b[q->p++]!=delim)return false;p=q->p;while(q->p<q->n){uint8_t c=q->b[q->p++];if(q->p-p>8192||hg_stop(q->pd))return false;if(c==delim){if(delim=='\''&&q->p<q->n&&q->b[q->p]==delim){++q->p;continue;}if(at)*at=p;if(size)*size=q->p-p-1;return true;}if(delim=='"'&&c=='\\'){if(q->p==q->n||(q->b[q->p]!='"'&&q->b[q->p]!='\\'))return false;++q->p;}else if(delim=='\''&&c=='\\')return false;}return false;}
static __inline bool hg_end(hg_lex *q) {return hg_skip(q)&&q->p==q->n;}
static __inline bool hg_u16(const uint8_t *p,uint32_t units) {uint32_t i;for(i=0;i<units;++i){uint16_t a=xx_data_get_u16(p+i*2, 2, 0, true);if(!a)return false;if(a>=0xd800&&a<=0xdbff){uint16_t b;if(++i==units)return false;b=xx_data_get_u16(p+i*2, 2, 0, true);if(b<0xdc00||b>0xdfff)return false;}else if(a>=0xdc00&&a<=0xdfff)return false;}return true;}
static __inline bool hg_pstring(hg_bin *q) {const uint8_t *p;uint32_t z;if(!hg_take(q,4,&p)||(z=xx_data_get_u32(p, 4, 0, true))>4096||!hg_take(q,(uint64_t)z*2,&p))return false;return hg_u16(p,z);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b;uint64_t p=0;bool ok=false;
 if(available<1||available>33554432||hg_stop(pd)||!hg_quick(f,(uint64_t)available))return false;
 b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 while(p<(uint64_t)available){size_t z=(uint64_t)available-p>65536?65536:(size_t)((uint64_t)available-p);
  if(hg_stop(pd)||!pm_read(f,(int64_t)p,b+p,z)) {goto done; } p+=z;}
 ok=!hg_stop(pd)&&hg_parse(f,s,b,(uint64_t)available,pd)&&!hg_stop(pd);
 if(ok)s->size=available;
done:xx_mem_free(b);return ok;
}
#endif
