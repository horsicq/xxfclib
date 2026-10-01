/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://capnproto.org/encoding.html */
#include "xxfclib/formats/capnproto_message/xx_capnproto_message.h"
#include "../nix_nar/xx_eleventh_containers.h"

static bool cp_pointer(nh_blob *b,uint64_t word,unsigned depth,unsigned *work) {uint64_t p,target,n,i,words,tag,elements,data,ptrs;int64_t offset,pos;unsigned type,size;if(depth>32 || ++*work>65536 || !nh_span(b,8+word*8,8)) return false;p=ec_le64(b->p+(size_t)(8+word*8));if(!p) return true;type=(unsigned)(p&3);if(type>1) return false;offset=(int64_t)((int32_t)(uint32_t)p)>>2;pos=(int64_t)word+1+offset;if(pos<0) return false;target=(uint64_t)pos;words=(b->n-8)/8;
    if(!type) {data=(p>>32)&65535;ptrs=p>>48;if(!eh_span(target,data+ptrs,words)) return false;for(i=0;i<ptrs;++i) if(!cp_pointer(b,target+data+i,depth+1,work)) return false;return true;}
    size=(unsigned)((p>>32)&7);n=p>>35;if(n>1048576) return false;if(size==7) {if(!eh_span(target,n+1,words)) return false;tag=ec_le64(b->p+(size_t)(8+target*8));if(tag&3) return false;elements=(tag>>2)&0x3fffffffU;data=(tag>>32)&65535;ptrs=tag>>48;if(elements>65536 || elements*(data+ptrs)!=n) return false;for(i=0;i<elements;++i) for(uint64_t j=0;j<ptrs;++j) if(!cp_pointer(b,target+1+i*(data+ptrs)+data+j,depth+1,work)) return false;return true;}
    if(size==6) {if(!eh_span(target,n,words)) return false;for(i=0;i<n;++i) if(!cp_pointer(b,target+i,depth+1,work)) return false;return true;}data=size==0 ? 0:size==1 ? 1:UINT64_C(1)<<(size+1);return eh_span(target,(n*data+63)/64,words);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {nh_blob b;uint64_t words;unsigned work=0;bool ok=false;if(!nh_load(f,&b,pd)) return false;NH_NEED(nh_span(&b,0,16) && !pm_le32(b.p));words=pm_le32(b.p+4);NH_NEED(words && words<=1048576 && b.n==8+words*8 && ec_le64(b.p+8) && !(ec_le64(b.p+8)&3) && cp_pointer(&b,0,0,&work) && nh_add(f,s,&b,"segment-table",0,8) && nh_add(f,s,&b,"encoded-segment",8,words*8));s->size=(int64_t)b.n;ok=true;done:xx_mem_free(b.p);return ok;}

void xx_capnproto_message_init(xx_capnproto_message *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CAPNPROTO_MESSAGE,"capnp"); } }
xx_capnproto_message *xx_capnproto_message_create(xx_io_device *d,int64_t b) { xx_capnproto_message *r=(xx_capnproto_message *)xx_mem_alloc(sizeof(*r)); if(r) xx_capnproto_message_init(r,d,b); return r; }
void xx_capnproto_message_destroy(xx_capnproto_message *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_capnproto_message_free(xx_capnproto_message *r) { if(r) { xx_capnproto_message_destroy(r); xx_mem_free(r); } }
bool xx_capnproto_message_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_capnproto_message_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
