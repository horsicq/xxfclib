/* SPDX-License-Identifier: MIT. Bounded original OPL music components; no playback. */
#ifndef XX_FIFTEENTH_MEDIA_H
#define XX_FIFTEENTH_MEDIA_H
#include "xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
typedef struct m15_blob {uint8_t *p;uint64_t n,work;xx_pd_struct *pd;} m15_blob;
static bool m15_stop(m15_blob *b) {return b->pd&&xx_pd_is_stopped(b->pd);}
static bool m15_work(m15_blob *b,uint64_t n) {if(m15_stop(b)||n>2000000-b->work)return false;b->work+=n;return true;}
static bool m15_span(m15_blob *b,uint64_t at,uint64_t n) {return !m15_stop(b)&&at<=b->n&&n<=b->n-at;}
static bool m15_load(Abstractformat *f,m15_blob *b,xx_pd_struct *pd) {int64_t n=pm_available(f);uint64_t at=0;b->pd=pd;if(n<1||n>33554432||m15_stop(b))return false;b->n=(uint64_t)n;b->p=(uint8_t *)xx_mem_alloc((size_t)n);if(!b->p)return false;while(at<b->n){size_t z=b->n-at>65536?65536:(size_t)(b->n-at);if(!m15_span(b,at,z)||!pm_read(f,(int64_t)at,b->p+(size_t)at,z))return false;at+=z;}return !m15_stop(b);}
static XXFC_MAYBE_UNUSED bool m15_tag(m15_blob *b,uint64_t at,const char *tag,size_t n) {return m15_span(b,at,n)&&!xx_rt_memcmp(b->p+(size_t)at,tag,n);}
static bool m15_emit(Abstractformat *f,pm_stream *s,m15_blob *b,const char *name,uint64_t at,uint64_t n) {return n&&s->count<4096&&m15_span(b,at,n)&&pm_add(f,s,name,(int64_t)at,(int64_t)n);}
static XXFC_MAYBE_UNUSED bool m15_z(m15_blob *b,uint64_t *at,uint64_t end) {uint64_t start=*at;while(*at<end&&*at-start<4096){if(!m15_span(b,*at,1)||!m15_work(b,1))return false;if(!b->p[(size_t)(*at)++])return true;}return false;}
static XXFC_MAYBE_UNUSED bool m15_float(m15_blob *b,uint64_t at,bool positive) {uint32_t u;if(!m15_span(b,at,4))return false;u=xx_data_get_u32(b->p+(size_t)at, 4, 0, false);return !(u&0x80000000U)&&(u&0x7f800000U)!=0x7f800000U&&(!positive||(u&0x7fffffffU));}
static XXFC_MAYBE_UNUSED bool m15_vlq(m15_blob *b,uint64_t *at,uint32_t *value) {unsigned i;uint32_t v=0;for(i=0;i<4;++i){uint8_t c;if(!m15_span(b,*at,1)||!m15_work(b,1))return false;c=b->p[(size_t)(*at)++];v=(v<<7)|(c&127);if(!(c&128)){*value=v;return true;}}return false;}
#define M15_NEED(x) do {if(!(x))goto done;} while(0)
#endif
