/* SPDX-License-Identifier: MIT. Private checked original music components. */
#ifndef XX_FOURTEENTH_MEDIA_H
#define XX_FOURTEENTH_MEDIA_H
#include "xx_payload_members.h"
typedef struct fm_blob {uint8_t *p;uint64_t n,work;xx_pd_struct *pd;} fm_blob;
typedef struct fm_range {uint64_t at,n;} fm_range;
static bool fm_stop(fm_blob *b) {return b->pd && xx_pd_is_stopped(b->pd);}
static bool fm_work(fm_blob *b,uint64_t n) {if(fm_stop(b)||n>2000000-b->work)return false;b->work+=n;return true;}
static bool fm_span(fm_blob *b,uint64_t at,uint64_t n) {return !fm_stop(b)&&at<=b->n&&n<=b->n-at;}
static bool fm_load(Abstractformat *f,fm_blob *b,xx_pd_struct *pd) {int64_t n=pm_available(f);uint64_t at=0;b->pd=pd;if(n<1||n>33554432||fm_stop(b))return false;b->n=(uint64_t)n;b->p=(uint8_t *)xx_mem_alloc((size_t)n);if(!b->p)return false;while(at<b->n){size_t z=b->n-at>65536?65536:(size_t)(b->n-at);if(!fm_span(b,at,z)||!pm_read(f,(int64_t)at,b->p+(size_t)at,z))return false;at+=z;}return !fm_stop(b);}
static XXFC_MAYBE_UNUSED bool fm_tag(fm_blob *b,uint64_t at,const char *tag,size_t n) {return fm_span(b,at,n)&&!xx_rt_memcmp(b->p+(size_t)at,tag,n);}
static XXFC_MAYBE_UNUSED bool fm_zero(fm_blob *b,uint64_t at,uint64_t n) {uint64_t i;if(!fm_span(b,at,n))return false;for(i=0;i<n;++i){if(!fm_work(b,1)||b->p[(size_t)(at+i)])return false;}return true;}
static bool fm_emit(Abstractformat *f,pm_stream *s,fm_blob *b,const char *name,uint64_t at,uint64_t n) {return n&&s->count<4096&&fm_span(b,at,n)&&pm_add(f,s,name,(int64_t)at,(int64_t)n);}
static XXFC_MAYBE_UNUSED bool fm_claim(fm_blob *b,fm_range *ranges,unsigned *count,uint64_t at,uint64_t n,bool shared) {unsigned i;if(!n||!fm_span(b,at,n)||*count>=1024)return false;for(i=0;i<*count;++i){if(!fm_work(b,1))return false;if(at==ranges[i].at&&n==ranges[i].n&&shared)return true;if(at<ranges[i].at+ranges[i].n&&ranges[i].at<at+n)return false;}ranges[*count].at=at;ranges[(*count)++].n=n;return true;}
static XXFC_MAYBE_UNUSED bool fm_zstring(fm_blob *b,uint64_t *at,uint64_t end,uint64_t cap) {uint64_t start=*at;while(*at<end&&*at-start<cap){if(!fm_span(b,*at,1)||!fm_work(b,1))return false;if(!b->p[(size_t)(*at)++])return true;}return false;}
#define FM_NEED(x) do{if(!(x))goto done;}while(0)
#endif
