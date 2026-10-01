/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://ubjson.org/type-reference/ */
#include "xxfclib/formats/ubjson/xx_ubjson.h"
#include "../nix_nar/xx_eleventh_containers.h"

static bool ub_number(nh_blob *b,uint64_t *at,int64_t *v) {uint8_t c;unsigned w;uint64_t u=0;if(!nh_span(b,*at,1)) return false;c=b->p[(size_t)(*at)++];w=c=='i' || c=='U' ? 1:c=='I' ? 2:c=='l' ? 4:c=='L' ? 8:0;if(!w || !nh_span(b,*at,w)) return false;for(unsigned j=0;j<w;++j) u=(u<<8)|b->p[(size_t)(*at)++];if(c!='U' && w<8 && (u&(UINT64_C(1)<<(w*8-1)))) u|=UINT64_MAX<<(w*8);*v=(int64_t)u;return true;}
static bool ub_text(nh_blob *b,uint64_t *at,uint64_t *start,uint64_t *n) {int64_t len;if(!ub_number(b,at,&len) || len<0 || len>65536) return false;*n=(uint64_t)len;*start=*at;if(!ec_utf(b,*at,*n)) return false;*at+=*n;return true;}
static bool ub_value(Abstractformat *,pm_stream *,nh_blob *,uint64_t *,uint8_t,unsigned,unsigned *,bool);
static bool ub_container(Abstractformat *f,pm_stream *s,nh_blob *b,uint64_t *at,uint8_t c,unsigned depth,unsigned *work,bool root) {uint8_t fixed=0;uint64_t count=0,i=0,start,keyat[2048],keyn[2048],key,j;bool counted=false;if(!nh_span(b,*at,1)) return false;if(b->p[(size_t)*at]=='$') {++*at;if(!nh_span(b,*at,1)) return false;fixed=b->p[(size_t)(*at)++];if(!fixed || fixed=='N' || fixed=='$' || fixed=='#' || !nh_span(b,*at,1) || b->p[(size_t)*at]!='#') return false;}if(b->p[(size_t)*at]=='#') {int64_t n;++*at;if(!ub_number(b,at,&n) || n<0 || n>2048) return false;count=(uint64_t)n;counted=true;}
    if(root && !nh_add(f,s,b,"ubjson-root",0,*at)) return false;
    while(counted ? i<count:(!nh_span(b,*at,1) || b->p[(size_t)*at]!=(c=='{' ? '}':']'))) {if(i>=2048 || (!fixed && !nh_span(b,*at,1))) return false;if(!fixed && b->p[(size_t)*at]=='N') {++*at;continue;}start=*at;if(c=='{') {if(!ub_text(b,at,&key,&keyn[i]) || !keyn[i]) return false;keyat[i]=key;for(j=0;j<i;++j) if(!ec_cmp(b->p+(size_t)keyat[j],keyn[j],b->p+(size_t)key,keyn[i])) return false;}if(!ub_value(f,s,b,at,fixed,depth+1,work,false) || (root && !nh_add(f,s,b,"encoded-entry",start,*at-start))) return false;++i;}
    if(!counted) ++*at;return !root || i>0;
}
static bool ub_value(Abstractformat *f,pm_stream *s,nh_blob *b,uint64_t *at,uint8_t fixed,unsigned depth,unsigned *work,bool root) {uint8_t c=fixed;uint64_t start,n;int64_t v;if(depth>32 || ++*work>1048576) return false;if(!c) {if(!nh_span(b,*at,1)) return false;c=b->p[(size_t)(*at)++];}if(c=='{' || c=='[') return ub_container(f,s,b,at,c,depth,work,root);if(root) return false;if(c=='Z' || c=='T' || c=='F') return true;if(c=='i' || c=='U' || c=='I' || c=='l' || c=='L') {--*at;if(fixed) {unsigned w=c=='i' || c=='U' ? 1:c=='I' ? 2:c=='l' ? 4:8;++*at;if(!nh_span(b,*at,w)) return false;*at+=w;return true;}return ub_number(b,at,&v);}if(c=='d' || c=='D') {n=c=='d' ? 4:8;if(!nh_span(b,*at,n) || !nh_floats(b,*at,n,(unsigned)n,true)) return false;*at+=n;return true;}if(c=='C') {if(!nh_span(b,*at,1) || b->p[(size_t)*at]>127) return false;++*at;return true;}if(c=='S' || c=='H') {if(!ub_text(b,at,&start,&n)) return false;if(c=='H' && !ec_number(b->p+(size_t)start,n,false)) return false;return true;}return false;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=0;unsigned work=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(ub_value(f,s,&b,&at,0,0,&work,true) && at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_ubjson_init(xx_ubjson *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_UBJSON,"ubj"); } }
xx_ubjson *xx_ubjson_create(xx_io_device *d,int64_t b) { xx_ubjson *r=(xx_ubjson *)xx_mem_alloc(sizeof(*r)); if(r) xx_ubjson_init(r,d,b); return r; }
void xx_ubjson_destroy(xx_ubjson *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ubjson_free(xx_ubjson *r) { if(r) { xx_ubjson_destroy(r); xx_mem_free(r); } }
bool xx_ubjson_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ubjson_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
