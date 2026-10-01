/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://www.rfc-editor.org/rfc/rfc8949.html */
#include "xxfclib/formats/cbor/xx_cbor.h"
#include "../microsoft_msf/xx_tenth_containers.h"

static bool cb_arg(nh_blob *b,uint64_t *at,unsigned ai,uint64_t *v) {
    unsigned width,i;if(ai<24) {*v=ai;return true;}if(ai>27) return false;width=1U<<(ai-24);if(!nh_span(b,*at,width)) return false;*v=0;for(i=0;i<width;++i) *v=(*v<<8)|b->p[(size_t)(*at)++];return true;
}
static bool cb_item(nh_blob *b,uint64_t *at,unsigned depth,unsigned *nodes) {
    uint8_t tag;unsigned major,ai;uint64_t n,i;if(depth>32 || ++*nodes>1000000 || !nh_span(b,*at,1)) return false;tag=b->p[(size_t)(*at)++];major=tag>>5;ai=tag&31;if(!cb_arg(b,at,ai,&n)) return false;
    if(major<=1) return true;
    if(major==2 || major==3) {if(!nh_span(b,*at,n) || (major==3 && !fourth_utf8(b->p+(size_t)*at,(size_t)n,b->pd))) return false;*at+=n;return true;}
    if(major==4 || major==5) {if(n>4094 || (major==5 && n>2047)) return false;n*=major==5 ? 2:1;for(i=0;i<n;++i) if(!cb_item(b,at,depth+1,nodes)) return false;return true;}
    if(major==6) {if(n==28 || n==29) return false;return cb_item(b,at,depth+1,nodes);}
    return ai<24 || (ai==24 && n>=32) || ai==25 || ai==26 || ai==27;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;uint64_t at=3,n,i,start,check;unsigned nodes=0,major,ai;bool ok=false;
    if(!nh_load(f,&b,pd)) return false;NH_NEED(nh_span(&b,0,4) && !xx_rt_memcmp(b.p,"\xd9\xd9\xf7",3));check=at;NH_NEED(cb_item(&b,&check,0,&nodes) && check==b.n);major=b.p[3]>>5;ai=b.p[3]&31;++at;NH_NEED((major==4 || major==5) && cb_arg(&b,&at,ai,&n));NH_NEED(nh_add(f,s,&b,"cbor-container-header",0,at));
    for(i=0;i<n;++i) {start=at;NH_NEED(cb_item(&b,&at,0,&nodes));if(major==5) NH_NEED(cb_item(&b,&at,0,&nodes));NH_NEED(nh_add(f,s,&b,"entry",start,at-start));}NH_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_cbor_init(xx_cbor *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CBOR,"cbor"); } }
xx_cbor *xx_cbor_create(xx_io_device *d,int64_t b) { xx_cbor *r=(xx_cbor *)xx_mem_alloc(sizeof(*r)); if(r) xx_cbor_init(r,d,b); return r; }
void xx_cbor_destroy(xx_cbor *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_cbor_free(xx_cbor *r) { if(r) { xx_cbor_destroy(r); xx_mem_free(r); } }
bool xx_cbor_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_cbor_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
