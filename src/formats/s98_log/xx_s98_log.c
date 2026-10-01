/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/s98_log/xx_s98_log.h"
#include "../snes_spc/xx_tenth_retro.h"
static bool read_components(Abstractformat *f,pm_stream *s,th_blob *b) {
 const uint8_t *p=b->p;uint32_t count,data,tag,loop,a,end,commands=0,i,types[64],max=0;bool loop_seen=false,ended=false;
 if(b->n<33 || xx_rt_memcmp(p,"S983",4) || pm_le32(p+12)) return false;count=pm_le32(p+28);if(count>64) return false;data=pm_le32(p+20);tag=pm_le32(p+16);loop=pm_le32(p+24);
 if(data<32+count*16 || data>=b->n || !th_zero(p+32+count*16,data-32-count*16) || (tag && (tag<=data || tag>=b->n))) return false;
 for(i=0;i<count;++i) {uint32_t at=32+i*16,t=pm_le32(p+at);if((t>9 && t!=15 && t!=16) || (t && !pm_le32(p+at+4)) || !th_zero(p+at+12,4)) return false;types[i]=t;}
 if(!count) {types[0]=4;count=1;}end=tag ? tag:b->n;a=data;
 while(a<end) {unsigned op=p[a];if(!th_poll(b) || ++commands>2000000) return false;if(a==loop) loop_seen=true;++a;
  if(op==0xfd) {ended=true;break;}
  if(op==0xff) continue;
  if(op==0xfe) {unsigned shift=0;uint32_t v=0;for(;;) {unsigned x;if(a>=end || shift>28) return false;x=p[a++];if(shift==28 && (x&0x7f)>15) return false;v|=(uint32_t)(x&127)<<shift;if(!(x&128)) break;shift+=7;}if(v>UINT32_MAX-2) return false;continue;}
  if(op/2>=count || end-a<2 || ((op&1) && types[op/2] && types[op/2]!=3 && types[op/2]!=4 && types[op/2]!=9)) return false;a+=2;
 }
 if(!ended || a!=end || (loop && !loop_seen)) return false;max=a;
 if(tag) {
  uint32_t t=tag+5,line=0;bool equals=false;if(!th_range(b,tag,5) || xx_rt_memcmp(p+tag,"[S98]",5) || b->n-tag>1048576) return false;if(th_range(b,t,3) && !xx_rt_memcmp(p+t,"\xef\xbb\xbf",3)) t+=3;
  for(;t<b->n;++t) {unsigned c=p[t];if(!th_poll(b)) return false;if(!c) {if(t!=b->n-1) return false;break;}if(c==10) {if(line && !equals) return false;line=0;equals=false;}else if(c!=13) {if(c<32 || ++line>4096) return false;if(c=='=' && line>1) equals=true;}}
  if(line && !equals) return false;max=b->n;
 }
 if(!th_emit(f,s,b,"log-descriptor.bin",0,data) || !th_emit(f,s,b,"register-commands.s98data",data,end-data) || (tag && !th_emit(f,s,b,"tags.s98",tag,max-tag))) return false;s->size=max;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 th_blob b;bool ok;if(!th_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok;
}
void xx_s98_log_init(xx_s98_log *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_S98_LOG,"s98_log"); } }
xx_s98_log *xx_s98_log_create(xx_io_device *d,int64_t b) { xx_s98_log *r=(xx_s98_log *)xx_mem_alloc(sizeof(*r));if(r) xx_s98_log_init(r,d,b);return r; }
void xx_s98_log_destroy(xx_s98_log *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_s98_log_free(xx_s98_log *r) { if(r) {xx_s98_log_destroy(r);xx_mem_free(r);} }
bool xx_s98_log_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_s98_log_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
