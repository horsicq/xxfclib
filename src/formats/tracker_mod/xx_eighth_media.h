/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
#ifndef XX_EIGHTH_MEDIA_H
#define XX_EIGHTH_MEDIA_H
#include "../xx_fifth_data.h"
typedef struct em_rg { uint64_t at,n; } em_rg;
static bool em_overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool em_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t end) {
    size_t i; if(end>268435456 || end>(uint64_t)pm_available(f) || !fd_range(at,n,end) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(em_overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,label,(int64_t)at,(int64_t)n);
}
static bool em_take_emit(fd_cursor *c,pm_stream *s,const char *name,uint64_t n) { uint64_t at=c->at; return fd_skip(c,n) && em_emit(c->f,s,name,at,n,c->end); }
static bool em_zero(const uint8_t *b,size_t n) { size_t i; for(i=0;i<n;++i) if(b[i]) return false; return true; }
static bool em_loop(uint32_t a,uint32_t n,uint32_t size) { return a<=size && n<=size-a; }
static bool em_finite(const uint8_t *b) { return (pm_be32(b)&0x7f800000U)!=0x7f800000U; }
static bool em_reserve(em_rg *r,unsigned *nr,unsigned cap,uint64_t at,uint64_t n,uint64_t lower,uint64_t end) { unsigned i; if(*nr>=cap || at<lower || !fd_range(at,n,end)) return false; for(i=0;i<*nr;++i) if(em_overlap(at,n,r[i].at,r[i].n)) return false; r[*nr].at=at;r[*nr].n=n;++*nr; return true; }
#endif
