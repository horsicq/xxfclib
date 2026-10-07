/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/openssh/openssh-portable/blob/master/PROTOCOL.key */
#include "xxfclib/formats/openssh_private_key/xx_openssh_private_key.h"
#include "../xx_twelfth_a.h"

static bool sh_string(nh_blob *b,uint64_t *at,uint64_t end,uint64_t *start,uint64_t *n) {if(!eh_span(*at,4,end)) return false;*n=xx_data_get_u32(b->p+(size_t)*at, 4, 0, true);*at+=4;*start=*at;if(*n>1048576 || !eh_span(*at,*n,end) || !nh_span(b,*at,*n)) return false;*at+=*n;return true;}
static bool sh_equal(nh_blob *b,uint64_t at,uint64_t n,const char *text) {return n==xx_rt_strlen(text) && !xx_rt_memcmp(b->p+(size_t)at,text,(size_t)n);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t at=15,p,n,publicAt,publicN,end,priv,pubkey,seed,comment,padding;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(b.n>=64 && !xx_rt_memcmp(b.p,"openssh-key-v1\0",15));NH_NEED(sh_string(&b,&at,b.n,&p,&n) && sh_equal(&b,p,n,"none") && sh_string(&b,&at,b.n,&p,&n) && sh_equal(&b,p,n,"none") && sh_string(&b,&at,b.n,&p,&n) && !n && nh_span(&b,at,4) && xx_data_get_u32(b.p+(size_t)at, 4, 0, true)==1);at+=4;NH_NEED(nh_add(f,s,&b,"openssh-header",0,at) && sh_string(&b,&at,b.n,&publicAt,&publicN));p=publicAt;NH_NEED(sh_string(&b,&p,publicAt+publicN,&priv,&n) && sh_equal(&b,priv,n,"ssh-ed25519") && sh_string(&b,&p,publicAt+publicN,&pubkey,&n) && n==32 && p==publicAt+publicN && nh_add(f,s,&b,"public-key",publicAt,publicN));NH_NEED(sh_string(&b,&at,b.n,&priv,&n) && at==b.n && n>=8 && !(n&7));end=priv+n;p=priv;NH_NEED(xx_data_get_u32(b.p+(size_t)p, 4, 0, true)==xx_data_get_u32(b.p+(size_t)p+4, 4, 0, true) && nh_add(f,s,&b,"checkints",p,8));p+=8;NH_NEED(sh_string(&b,&p,end,&comment,&n) && sh_equal(&b,comment,n,"ssh-ed25519") && sh_string(&b,&p,end,&comment,&n) && n==32 && !xx_rt_memcmp(b.p+(size_t)comment,b.p+(size_t)pubkey,32) && sh_string(&b,&p,end,&seed,&n) && n==64 && !xx_rt_memcmp(b.p+(size_t)seed+32,b.p+(size_t)pubkey,32) && nh_add(f,s,&b,"private-key-fields",comment,seed+64-comment));NH_NEED(sh_string(&b,&p,end,&comment,&n) && ec_utf(&b,comment,n) && nh_add(f,s,&b,"comment",comment,n));padding=end-p;NH_NEED(padding>=1 && padding<=8);for(uint64_t i=0;i<padding;++i) NH_NEED(b.p[(size_t)(p+i)]==i+1);NH_NEED(nh_add(f,s,&b,"padding",p,padding));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_openssh_private_key_init(xx_openssh_private_key *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OPENSSH_PRIVATE_KEY,"openssh"); } }
xx_openssh_private_key *xx_openssh_private_key_create(xx_io_device *d,int64_t b) { xx_openssh_private_key *r=(xx_openssh_private_key *)xx_mem_alloc(sizeof(*r)); if(r) xx_openssh_private_key_init(r,d,b); return r; }
void xx_openssh_private_key_destroy(xx_openssh_private_key *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_openssh_private_key_free(xx_openssh_private_key *r) { if(r) { xx_openssh_private_key_destroy(r); xx_mem_free(r); } }
bool xx_openssh_private_key_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_openssh_private_key_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
