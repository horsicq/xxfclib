/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded retro-container helpers; grammars remain in each reader.
 */
#ifndef XX_NINTH_RETRO_H
#define XX_NINTH_RETRO_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define NH_LIMIT (64U*1024U*1024U)
typedef struct nh_blob { uint8_t *p; uint32_t n,crc_budget; xx_pd_struct *pd; } nh_blob;
typedef struct nh_span { uint32_t a,z; } nh_span;
static bool nh_poll(const nh_blob *b) { return !b->pd || !xx_pd_is_stopped(b->pd); }
static bool nh_range(const nh_blob *b,uint32_t at,uint32_t n) { return at<=b->n && n<=b->n-at; }
static XXFC_MAYBE_UNUSED bool nh_load(Abstractformat *f,nh_blob *b,xx_pd_struct *pd) {
 int64_t n=pm_available(f); uint32_t at=0; xx_mem_zero(b,sizeof(*b)); b->pd=pd;
 if(n<=0 || n>NH_LIMIT || !nh_poll(b) || !(b->p=(uint8_t *)xx_mem_alloc((size_t)n))) return false;
 b->n=(uint32_t)n; b->crc_budget=NH_LIMIT;
 while(at<b->n) { uint32_t z=b->n-at; if(z>65536U) z=65536U; if(!nh_poll(b) || !pm_read(f,at,b->p+at,z)) { xx_mem_free(b->p); b->p=NULL; return false; } at+=z; }
 return true;
}
static bool nh_emit(Abstractformat *f,pm_stream *s,const nh_blob *b,const char *name,uint32_t a,uint32_t z) {
 return s->count<4096U && nh_poll(b) && nh_range(b,a,z) && pm_add(f,s,name,a,z);
}
static XXFC_MAYBE_UNUSED bool nh_memory(Abstractformat *f,pm_stream *s,const nh_blob *b,const char *name,uint32_t a,uint8_t *p,uint32_t z) {
 if(z>NH_LIMIT || !nh_emit(f,s,b,name,a,0)) { xx_mem_free(p); return false; }
 s->items[s->count-1].memory=p; s->items[s->count-1].size=z; s->items[s->count-1].packed_size=0; return true;
}
static XXFC_MAYBE_UNUSED bool nh_disjoint(nh_span *sp,uint32_t *count,uint32_t cap,uint32_t a,uint32_t z) {
 uint32_t i; if(!z) return true; if(*count>=cap || a>UINT32_MAX-z) return false;
 for(i=0;i<*count;++i) if(a<sp[i].a+sp[i].z && sp[i].a<a+z) return false;
 sp[*count].a=a; sp[*count].z=z; ++*count; return true;
}
static XXFC_MAYBE_UNUSED bool nh_zero(const uint8_t *p,uint32_t z) { uint32_t i; for(i=0;i<z;++i) if(p[i]) return false; return true; }
static XXFC_MAYBE_UNUSED bool nh_ascii(const uint8_t *p,uint32_t z,bool allowzero) { uint32_t i; for(i=0;i<z;++i) if((p[i]<32 || p[i]>126) && !(allowzero && !p[i])) return false; return true; }
static XXFC_MAYBE_UNUSED uint32_t nh_crc(nh_blob *b,uint32_t a,uint32_t z,bool *ok) {
 uint32_t c=0,i; *ok=false; if(!nh_range(b,a,z) || z>b->crc_budget) return 0; b->crc_budget-=z;
 for(i=0;i<z;) {uint32_t part=z-i;if(part>4096) part=4096;if(!nh_poll(b)) return 0;c=xx_crc32_calc(c,b->p+a+i,part);i+=part;}
 *ok=true; return c;
}
#endif
