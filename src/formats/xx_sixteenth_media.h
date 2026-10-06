/* SPDX-License-Identifier: MIT. Original music components; no playback or external resolution. */
#ifndef XX_SIXTEENTH_MEDIA_H
#define XX_SIXTEENTH_MEDIA_H
#include "xx_payload_members.h"
typedef struct m16_blob {uint8_t *p;uint64_t n,work;xx_pd_struct *pd;} m16_blob;
static bool m16_stop(m16_blob *b) {return b->pd&&xx_pd_is_stopped(b->pd);}
static bool m16_work(m16_blob *b,uint64_t n) {if(m16_stop(b)||n>2000000-b->work)return false;b->work+=n;return true;}
static bool m16_span(m16_blob *b,uint64_t at,uint64_t n) {return !m16_stop(b)&&at<=b->n&&n<=b->n-at;}
static bool m16_load(Abstractformat *f,m16_blob *b,xx_pd_struct *pd) {int64_t n=pm_available(f);uint64_t at=0;b->pd=pd;if(n<1||n>33554432||m16_stop(b))return false;b->n=(uint64_t)n;b->p=(uint8_t *)xx_mem_alloc((size_t)n);if(!b->p)return false;while(at<b->n){size_t z=b->n-at>65536?65536:(size_t)(b->n-at);if(!m16_span(b,at,z)||!pm_read(f,(int64_t)at,b->p+(size_t)at,z))return false;at+=z;}return !m16_stop(b);}
static XXFC_MAYBE_UNUSED bool m16_tag(m16_blob *b,uint64_t at,const char *tag,size_t n) {return m16_span(b,at,n)&&!xx_rt_memcmp(b->p+(size_t)at,tag,n);}
static bool m16_emit(Abstractformat *f,pm_stream *s,m16_blob *b,const char *name,uint64_t at,uint64_t n) {return n&&s->count<4096&&m16_span(b,at,n)&&pm_add(f,s,name,(int64_t)at,(int64_t)n);}
static XXFC_MAYBE_UNUSED bool m16_zero(m16_blob *b,uint64_t at,uint64_t n) {uint64_t i;if(!m16_span(b,at,n)||!m16_work(b,n))return false;for(i=0;i<n;++i)if(b->p[(size_t)(at+i)])return false;return true;}
static XXFC_MAYBE_UNUSED bool m16_hex(m16_blob *b,uint64_t at,unsigned width,uint32_t *out) {unsigned i;uint32_t v=0;if(!width||width>8||!m16_span(b,at,width))return false;for(i=0;i<width;++i){uint8_t c=b->p[(size_t)at+i];unsigned d;if(c>='0'&&c<='9')d=c-'0';else if(c>='A'&&c<='F')d=c-'A'+10;else if(c>='a'&&c<='f')d=c-'a'+10;else return false;v=(v<<4)|d;}*out=v;return true;}
static XXFC_MAYBE_UNUSED bool m16_z(m16_blob *b,uint64_t *at,uint64_t end,uint64_t cap) {uint64_t start=*at;while(*at<end&&*at-start<=cap){uint8_t c;if(!m16_span(b,*at,1)||!m16_work(b,1))return false;c=b->p[(size_t)(*at)++];if(!c)return true;if(c<32||c>126)return false;}return false;}
#define M16_NEED(x) do {if(!(x))goto done;} while(0)
#endif
