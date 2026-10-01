/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/msgpack/msgpack/blob/master/spec.md */
#include "xxfclib/formats/msgpack/xx_msgpack.h"
#include "../nix_nar/xx_eleventh_containers.h"

static bool mp_head(nh_blob *b,uint64_t *at,uint8_t *kind,uint64_t *n) {uint8_t c;unsigned width=0;if(!nh_span(b,*at,1)) return false;c=b->p[(size_t)(*at)++];*n=0;*kind=0;if(c<=127 || c>=224 || c==192 || c==194 || c==195) return true;if((c&224)==160) {*kind=1;*n=c&31;return true;}if((c&240)==144 || (c&240)==128) {*kind=(c&240)==144 ? 3:4;*n=c&15;return true;}
    if(c==193) return false;if(c>=196 && c<=198) {*kind=2;width=1U<<(c-196);}else if(c>=199 && c<=201) {*kind=5;width=1U<<(c-199);}else if(c==202 || c==203) {*kind=0;*n=c==202 ? 4:8;return true;}else if(c>=204 && c<=211) {*kind=0;*n=(uint64_t)1<<((c-204)&3);return true;}else if(c>=212 && c<=216) {*kind=5;*n=(uint64_t)1<<(c-212);return true;}else if(c>=217 && c<=219) {*kind=1;width=1U<<(c-217);}else if(c>=220 && c<=223) {*kind=c<222 ? 3:4;width=(c&1) ? 4:2;}else return false;
    if(!nh_span(b,*at,width)) return false;for(unsigned i=0;i<width;++i) *n=(*n<<8)|b->p[(size_t)(*at)++];return true;
}
static bool mp_value(nh_blob *b,uint64_t *at,unsigned depth,unsigned *work) {uint8_t kind;uint64_t n,i;if(depth>32 || ++*work>1048576 || !mp_head(b,at,&kind,&n)) return false;if(kind==3 || kind==4) {if(n>65536) return false;for(i=0;i<n*(kind==4 ? 2U:1U);++i) if(!mp_value(b,at,depth+1,work)) return false;return true;}if(kind==5) {if(!nh_span(b,*at,1)) return false;uint8_t ext=b->p[(size_t)(*at)++];if(ext==255) {if(n!=4 && n!=8 && n!=12) return false;if(!nh_span(b,*at,n)) return false;if(n==8 && (ec_be64(b->p+(size_t)*at)>>34)>=1000000000U) return false;if(n==12 && pm_be32(b->p+(size_t)*at)>=1000000000U) return false;}}
    if(!nh_span(b,*at,n) || (kind==1 && !ec_utf(b,*at,n))) return false;*at+=n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=0,n,i,start,keyat[2048],keyn[2048],j;uint8_t kind,k;unsigned work=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(mp_head(&b,&at,&kind,&n) && (kind==3 || kind==4) && n && n<=2048);NH_NEED(nh_add(f,s,&b,"msgpack-header",0,at));for(i=0;i<n;++i) {start=at;if(kind==4) {NH_NEED(mp_head(&b,&at,&k,&keyn[i]) && k==1 && keyn[i] && ec_utf(&b,at,keyn[i]));keyat[i]=at;for(j=0;j<i;++j) NH_NEED(ec_cmp(b.p+(size_t)keyat[j],keyn[j],b.p+(size_t)at,keyn[i]));at+=keyn[i];}NH_NEED(mp_value(&b,&at,0,&work) && nh_add(f,s,&b,"encoded-entry",start,at-start));}NH_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_msgpack_init(xx_msgpack *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MSGPACK,"msgpack"); } }
xx_msgpack *xx_msgpack_create(xx_io_device *d,int64_t b) { xx_msgpack *r=(xx_msgpack *)xx_mem_alloc(sizeof(*r)); if(r) xx_msgpack_init(r,d,b); return r; }
void xx_msgpack_destroy(xx_msgpack *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_msgpack_free(xx_msgpack *r) { if(r) { xx_msgpack_destroy(r); xx_mem_free(r); } }
bool xx_msgpack_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_msgpack_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
