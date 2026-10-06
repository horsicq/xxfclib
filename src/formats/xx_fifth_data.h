/* SPDX-License-Identifier: MIT. Private checked binary-input primitives. */
#ifndef XX_FIFTH_DATA_H
#define XX_FIFTH_DATA_H
#include "xx_payload_members.h"
#include "xx_fourth_utf8.h"
static XXFC_MAYBE_UNUSED uint64_t fd_le64(const uint8_t *p) { return pm_le32(p)|((uint64_t)pm_le32(p+4)<<32); }
static XXFC_MAYBE_UNUSED uint64_t fd_be64(const uint8_t *p) { return ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4); }
static XXFC_MAYBE_UNUSED uint32_t fd_u32(const uint8_t *p,bool be) { return be ? pm_be32(p):pm_le32(p); }
static XXFC_MAYBE_UNUSED uint16_t fd_u16(const uint8_t *p,bool be) { return be ? pm_be16(p):pm_le16(p); }
static bool fd_range(uint64_t at,uint64_t n,uint64_t end) { return at<=end && n<=end-at && end<=INT64_MAX; }
static XXFC_MAYBE_UNUSED bool fd_mul(uint64_t a,uint64_t b,uint64_t *out) { if(b && a>(uint64_t)INT64_MAX/b) return false; *out=a*b; return true; }
static bool fd_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static XXFC_MAYBE_UNUSED bool fd_equal(Abstractformat *f,int64_t at,const char *p,size_t n) { uint8_t b[128]; return n<=sizeof(b) && pm_read(f,at,b,n) && !xx_rt_memcmp(b,p,n); }
typedef struct fd_cursor { Abstractformat *f; uint64_t at,end; xx_pd_struct *pd; unsigned steps; } fd_cursor;
static bool fd_get(fd_cursor *c,void *p,size_t n) { if(fd_stop(c->pd) || !fd_range(c->at,n,c->end) || ++c->steps>2000000 || !pm_read(c->f,(int64_t)c->at,p,n)) return false; c->at+=n; return true; }
static XXFC_MAYBE_UNUSED bool fd_skip(fd_cursor *c,uint64_t n) { if(fd_stop(c->pd) || !fd_range(c->at,n,c->end)) return false; c->at+=n; return true; }
static XXFC_MAYBE_UNUSED bool fd_be(fd_cursor *c,uint32_t *v) { uint8_t b[4]; if(!fd_get(c,b,4)) return false; *v=pm_be32(b); return true; }
static XXFC_MAYBE_UNUSED bool fd_disjoint(pm_stream *s,uint64_t begin) {
    /* A fixed upper bound keeps comparison work small and predictable. */
    size_t i,j; if(s->count>4096) return false;
    for(i=0;i<s->count;++i) { pm_member *a=&s->items[i]; if((uint64_t)a->offset<begin) return false;
        for(j=0;j<i;++j) { pm_member *b=&s->items[j]; if(a->size && b->size && a->offset<b->offset+b->size && b->offset<a->offset+a->size) return false; }
    } return true;
}
#endif
