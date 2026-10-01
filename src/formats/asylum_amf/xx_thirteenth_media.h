/* SPDX-License-Identifier: MIT. Private checked disk/music primitives. */
#ifndef XX_THIRTEENTH_MEDIA_H
#define XX_THIRTEENTH_MEDIA_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
typedef struct tm_blob {uint8_t *p;uint64_t n;xx_pd_struct *pd;uint64_t work,crc_bytes;} tm_blob;
typedef struct tm_range {uint64_t at,n;} tm_range;
static bool tm_stop(tm_blob *b) {return b->pd && xx_pd_is_stopped(b->pd);}
static bool tm_work(tm_blob *b,uint64_t n) {if(tm_stop(b) || n>2000000-b->work) return false;b->work+=n;return true;}
static bool tm_span(tm_blob *b,uint64_t at,uint64_t n) {return !tm_stop(b) && at<=b->n && n<=b->n-at;}
static bool tm_load(Abstractformat *f,tm_blob *b,xx_pd_struct *pd) {int64_t n=pm_available(f);uint64_t at=0;b->pd=pd;if(n<1 || n>33554432 || tm_stop(b)) return false;b->n=(uint64_t)n;b->p=(uint8_t *)xx_mem_alloc((size_t)n);if(!b->p)return false;while(at<b->n) {size_t z=b->n-at>65536?65536:(size_t)(b->n-at);if(!tm_span(b,at,z)||!pm_read(f,(int64_t)at,b->p+(size_t)at,z))return false;at+=z;}return true;}
static bool tm_tag(tm_blob *b,uint64_t at,const char *tag,size_t n) {return tm_span(b,at,n) && !xx_rt_memcmp(b->p+(size_t)at,tag,n);}
static bool tm_zero(tm_blob *b,uint64_t at,uint64_t n) {uint64_t i;if(!tm_span(b,at,n))return false;for(i=0;i<n;++i){if(!(i&4095U)&&tm_stop(b))return false;if(b->p[(size_t)(at+i)])return false;}return true;}
static bool tm_emit(Abstractformat *f,pm_stream *s,tm_blob *b,const char *name,uint64_t at,uint64_t n) {return n && s->count<4096 && tm_span(b,at,n) && pm_add(f,s,name,(int64_t)at,(int64_t)n);}
static bool tm_bytes(Abstractformat *f,pm_stream *s,tm_blob *b,const char *name,const void *p,size_t n) {uint8_t *copy;if(!n||s->count>=4096||tm_stop(b))return false;copy=(uint8_t *)xx_mem_alloc(n);if(!copy)return false;xx_rt_memcpy(copy,p,n);if(!pm_add(f,s,name,0,0)){xx_mem_free(copy);return false;}s->items[s->count-1].memory=copy;s->items[s->count-1].size=(int64_t)n;return true;}
static bool tm_claim(tm_blob *b,tm_range *r,unsigned *count,uint64_t at,uint64_t n) {unsigned i;if(!n||*count>=1024||!tm_span(b,at,n))return false;for(i=0;i<*count;++i){if(!tm_work(b,1)|| (at<r[i].at+r[i].n && r[i].at<at+n))return false;}r[*count].at=at;r[(*count)++].n=n;return true;}
static bool tm_crc(tm_blob *b,uint64_t at,uint64_t n,uint32_t *out) {
 static const xx_crc_model model={32U,0x1edc6f41U,0U,false,false,0U,"CRC-32/Castagnoli (unreflected)"};
 xx_crc_context ctx;uint64_t k=0;
 if(!tm_span(b,at,n)||n>33554432-b->crc_bytes)return false;
 b->crc_bytes+=n;
 if(!xx_crc_context_init(&ctx,&model))return false;
 while(k<n) {size_t part=n-k>4096U ? 4096U:(size_t)(n-k);if(tm_stop(b))return false;xx_crc_context_update(&ctx,b->p+(size_t)(at+k),part);k+=part;}
 *out=(uint32_t)xx_crc_context_final(&ctx);return true;
}
#define TM_NEED(x) do{if(!(x))goto done;}while(0)
#endif
