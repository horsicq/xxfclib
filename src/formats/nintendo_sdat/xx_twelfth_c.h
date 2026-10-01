/* SPDX-License-Identifier: MIT. Private bounded twelfth retro components. */
#ifndef XX_TWELFTH_C_H
#define XX_TWELFTH_C_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define TC_LIMIT (32U*1024U*1024U)
typedef struct tc_blob { uint8_t *p; uint32_t n,work,strings,crc_bytes; xx_pd_struct *pd; } tc_blob;
typedef struct tc_extent { uint32_t a,z; } tc_extent;
static bool tc_poll(const tc_blob *b) { return !b->pd || !xx_pd_is_stopped(b->pd); }
static bool tc_work(tc_blob *b,uint32_t n) { if(n>b->work || !tc_poll(b)) return false; b->work-=n;return true; }
static bool tc_span(const tc_blob *b,uint32_t a,uint32_t z) { return a<=b->n && z<=b->n-a; }
static bool tc_load(Abstractformat *f,tc_blob *b,xx_pd_struct *pd) {
 int64_t n=pm_available(f);uint32_t a=0;xx_mem_zero(b,sizeof(*b));b->pd=pd;b->work=1000000;b->strings=1024U*1024U;b->crc_bytes=TC_LIMIT;
 if(n<=0 || n>TC_LIMIT || !tc_poll(b) || !(b->p=(uint8_t *)xx_mem_alloc((size_t)n))) return false;b->n=(uint32_t)n;
 while(a<b->n) {uint32_t z=b->n-a;if(z>65536) z=65536;if(!tc_poll(b) || !pm_read(f,a,b->p+a,z)) {xx_mem_free(b->p);b->p=NULL;return false;}a+=z;}
 return true;
}
static bool tc_emit(Abstractformat *f,pm_stream *s,tc_blob *b,const char *name,uint32_t a,uint32_t z) {
 return z && s->count<4096 && tc_work(b,1) && tc_span(b,a,z) && pm_add(f,s,name,a,z);
}
static bool tc_zero(const uint8_t *p,uint32_t n) { uint32_t i;for(i=0;i<n;++i) if(p[i]) return false;return true; }
static bool tc_crc(tc_blob *b,uint32_t a,uint32_t n,bool record,uint32_t *out) {
 uint32_t at,c=0;if(!tc_span(b,a,n) || n>b->crc_bytes) return false;b->crc_bytes-=n;
 for(at=0;at<n;) {uint32_t part=n-at;if(part>4096) part=4096;
  if(!tc_poll(b)) return false;
  if(record && at<12 && at+part>8) {uint8_t chunk[4096];uint32_t i;
   xx_rt_memcpy(chunk,b->p+a+at,part);
   for(i=0;i<part;++i) if(at+i>=8 && at+i<12) chunk[i]=0;
   c=xx_crc32_calc(c,chunk,part);
  } else c=xx_crc32_calc(c,b->p+a+at,part);
  at+=part;
 }*out=c;return true;
}
static bool tc_utf8(tc_blob *b,uint32_t a,uint32_t n) {
 uint32_t i=0;if(!tc_span(b,a,n) || n>b->strings || !tc_poll(b)) return false;b->strings-=n;
 while(i<n) {unsigned c=b->p[a+i++],need=0,cp=c,min=0;if(c<128) {if(!c || c<32) return false;continue;}
  if(c>=0xc2 && c<=0xdf) {need=1;cp=c&31;min=128;}else if(c>=0xe0 && c<=0xef) {need=2;cp=c&15;min=2048;}else if(c>=0xf0 && c<=0xf4) {need=3;cp=c&7;min=65536;}else return false;
  if(need>n-i) return false;while(need--) {c=b->p[a+i++];if((c&0xc0)!=0x80) return false;cp=(cp<<6)|(c&63);}if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
 }return true;
}
static bool tc_string(tc_blob *b,uint32_t a,uint32_t end,uint32_t *z,bool unicode) {
 uint32_t n=0;if(a>=end || end>b->n) return false;
 while(a+n<end && b->p[a+n]) {if(n>=4096 || !tc_work(b,1)) return false;++n;}
 if(a+n>=end) return false;if(unicode && !tc_utf8(b,a,n)) return false;*z=n+1;return true;
}
static bool tc_claim(tc_blob *b,tc_extent *v,uint32_t *count,uint32_t a,uint32_t z,bool same) {
 uint32_t i;if(!z || !tc_span(b,a,z) || *count>=4096 || !tc_work(b,*count+1)) return false;
 for(i=0;i<*count;++i) if(a<v[i].a+v[i].z && v[i].a<a+z) {if(same && a==v[i].a && z==v[i].z) return true;return false;}
 v[*count].a=a;v[*count].z=z;++*count;return true;
}
#endif
