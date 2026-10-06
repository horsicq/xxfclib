/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/lsp-plugins/lsp-3rd-party/master/include/steinberg/vst2.h
 * Big-endian VST2 CcnK FxCk/FPCh presets and FxBk/FBCh banks, bank versions1/2 and presetversion1. Up to1024 programs/65536 normalized finite parameters and256MiB opaque plugin data. Validates signed chunk sizes, identities, program counts/current program and exact nesting. Exports descriptors, parameter arrays or declared opaque plugin state without interpreting/loading plugins. A standalone FPCh zero outer byteSize is accepted only with a complete signed inner chunk length, as emitted by Surge XT. Unknown versions/extensions rejected.
 */
#include "xxfclib/formats/steinberg_fxb/xx_steinberg_fxb.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool em_program(Abstractformat *f,pm_stream *s,fd_cursor *c,uint32_t bankid,bool nested) {
 uint8_t h[56],p[4];uint32_t n,params,id,i;uint64_t at=c->at,end;if(!fd_get(c,h,56) || xx_rt_memcmp(h,"CcnK",4) || ((n=pm_be32(h+4))<48 && (n || nested || xx_rt_memcmp(h+8,"FPCh",4))) || n>INT32_MAX || !fd_range(at,8U+(uint64_t)n,c->end) || pm_be32(h+12)!=1 || !(id=pm_be32(h+16)) || (nested && id!=bankid) || !(params=pm_be32(h+24)) || params>65536 || !xx_rt_memchr(h+28,0,28) || !em_emit(f,s,"program-descriptor.bin",at,56,c->end)) return false;if(!n) {if(!pm_read(f,(int64_t)at+56,p,4) || !pm_be32(p) || pm_be32(p)>268435396 || !fd_range(at,60U+(uint64_t)pm_be32(p),c->end)) return false;n=52U+pm_be32(p);}end=at+8+n;
 if(!xx_rt_memcmp(h+8,"FxCk",4)) {if(n!=48U+(uint64_t)params*4) return false;for(i=0;i<params;++i) {if(!fd_get(c,p,4) || !em_finite(p) || pm_be32(p)>0x3f800000U) return false;}if(!em_emit(f,s,"parameters.bin",at+56,(uint64_t)params*4,c->end)) return false;}
 else if(!xx_rt_memcmp(h+8,"FPCh",4)) {uint32_t bytes;if(!fd_get(c,p,4) || !(bytes=pm_be32(p)) || bytes>INT32_MAX || n!=52U+(uint64_t)bytes || !em_emit(f,s,"chunk-size.bin",at+56,4,c->end) || !em_take_emit(c,s,"plugin-state.bin",bytes)) return false;}
 else { return false; } return c->at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[156],p[4];uint32_t n,version,id,count,i;fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};
 if(!pm_read(f,0,h,28) || xx_rt_memcmp(h,"CcnK",4) || (n=pm_be32(h+4))>INT32_MAX || 8U+(uint64_t)n>c.end || 8U+(uint64_t)n>268435456) { return false; } if(!n) {if(xx_rt_memcmp(h+8,"FPCh",4) || !pm_read(f,56,p,4) || !pm_be32(p) || pm_be32(p)>268435396 || 60U+(uint64_t)pm_be32(p)>c.end) return false;n=52U+pm_be32(p);}c.end=8U+(uint64_t)n;
 if(!xx_rt_memcmp(h+8,"FxCk",4) || !xx_rt_memcmp(h+8,"FPCh",4)) {if(!em_program(f,s,&c,0,false)) return false;}
 else {if(!fd_get(&c,h,156) || (xx_rt_memcmp(h+8,"FxBk",4) && xx_rt_memcmp(h+8,"FBCh",4)) || ((version=pm_be32(h+12))!=1 && version!=2) || !(id=pm_be32(h+16)) || !(count=pm_be32(h+24)) || count>1024 || (version==2 && pm_be32(h+28)>=count) || !em_zero(h+32,124) || !em_emit(f,s,"bank-descriptor.bin",0,156,c.end)) return false;
  if(!xx_rt_memcmp(h+8,"FxBk",4)) {for(i=0;i<count;++i) if(!em_program(f,s,&c,id,true)) return false;}
  else {uint32_t bytes;if(!fd_get(&c,p,4) || !(bytes=pm_be32(p)) || bytes>INT32_MAX || c.at+(uint64_t)bytes!=c.end || !em_emit(f,s,"chunk-size.bin",156,4,c.end) || !em_take_emit(&c,s,"plugin-state.bin",bytes)) return false;}
 }if(c.at!=c.end) return false;s->size=(int64_t)c.end;return true;
}

void xx_steinberg_fxb_init(xx_steinberg_fxb *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_STEINBERG_FXB,"fxb"); } }
xx_steinberg_fxb *xx_steinberg_fxb_create(xx_io_device *d,int64_t b) { xx_steinberg_fxb *r=(xx_steinberg_fxb *)xx_mem_alloc(sizeof(*r)); if(r) xx_steinberg_fxb_init(r,d,b); return r; }
void xx_steinberg_fxb_destroy(xx_steinberg_fxb *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_steinberg_fxb_free(xx_steinberg_fxb *r) { if(r) { xx_steinberg_fxb_destroy(r); xx_mem_free(r); } }
bool xx_steinberg_fxb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_steinberg_fxb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
