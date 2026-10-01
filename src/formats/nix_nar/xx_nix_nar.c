/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://releases.nixos.org/nix/nix-2.34.8/manual/protocols/nix-archive/index.html */
#include "xxfclib/formats/nix_nar/xx_nix_nar.h"
#include "../nix_nar/xx_eleventh_containers.h"

static bool nr_str(nh_blob *b,uint64_t *at,uint64_t *start,uint64_t *n) {if(!nh_span(b,*at,8)) return false;*n=ec_le64(b->p+(size_t)*at);*at+=8;*start=*at;if(!nh_span(b,*at,*n)) return false;*at+=*n;return ec_pad(b,at,8);}
static bool nr_token(nh_blob *b,uint64_t *at,const char *text) {uint64_t start,n;return nr_str(b,at,&start,&n) && n==xx_rt_strlen(text) && !xx_rt_memcmp(b->p+(size_t)start,text,(size_t)n);}
static bool nr_node(Abstractformat *f,pm_stream *s,nh_blob *b,uint64_t *at,unsigned depth,unsigned *nodes) {
    uint64_t start,n,previous=0,previous_n=0;unsigned count=0;if(depth>32 || ++*nodes>4096 || !nr_token(b,at,"(") || !nr_token(b,at,"type") || !nr_str(b,at,&start,&n)) return false;
    if(n==7 && !xx_rt_memcmp(b->p+(size_t)start,"regular",7)) {uint64_t saved=*at;if(!nr_token(b,at,"executable")) *at=saved;else if(!nr_token(b,at,"")) return false;if(!nr_token(b,at,"contents") || !nr_str(b,at,&start,&n) || !nh_add(f,s,b,"file",start,n)) return false;}
    else if(n==7 && !xx_rt_memcmp(b->p+(size_t)start,"symlink",7)) {if(!nr_token(b,at,"target") || !nr_str(b,at,&start,&n) || !n || !ec_utf(b,start,n) || !nh_add(f,s,b,"symlink-target",start,n)) return false;}
    else if(n==9 && !xx_rt_memcmp(b->p+(size_t)start,"directory",9)) {while(*at<b->n) {uint64_t saved=*at;if(!nr_token(b,at,"entry")) {*at=saved;break;}if(++count>2048 || !nr_token(b,at,"(") || !nr_token(b,at,"name") || !nr_str(b,at,&start,&n) || !ec_leaf(b->p+(size_t)start,n) || !ec_utf(b,start,n) || (previous_n && ec_cmp(b->p+(size_t)previous,previous_n,b->p+(size_t)start,n)>=0)) return false;previous=start;previous_n=n;if(!nr_token(b,at,"node") || !nr_node(f,s,b,at,depth+1,nodes) || !nr_token(b,at,")")) return false;}}
    else return false;return nr_token(b,at,")");
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=0;unsigned nodes=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(nr_token(&b,&at,"nix-archive-1") && nh_add(f,s,&b,"nar-header",0,at) && nr_node(f,s,&b,&at,0,&nodes) && at==b.n);s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_nix_nar_init(xx_nix_nar *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NIX_NAR,"nar"); } }
xx_nix_nar *xx_nix_nar_create(xx_io_device *d,int64_t b) { xx_nix_nar *r=(xx_nix_nar *)xx_mem_alloc(sizeof(*r)); if(r) xx_nix_nar_init(r,d,b); return r; }
void xx_nix_nar_destroy(xx_nix_nar *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nix_nar_free(xx_nix_nar *r) { if(r) { xx_nix_nar_destroy(r); xx_mem_free(r); } }
bool xx_nix_nar_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nix_nar_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
