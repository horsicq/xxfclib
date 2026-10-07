/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/imf_load.c
 * Imago Orpheus IMF1.0 with832-byte descriptor, up to256 patterns/instruments and32 channels; instrument II10/sample IS10 records, PCM8/16. Validates packed row events, multisample maps, bounded envelopes, sample loops/rates and complete sequential extents. Exports descriptor/patterns/instrument/sample headers and original PCM. Alternate IW10, compression and unknown extensions rejected; no playback.256MiB cap/4096 output components.
 */
#include "xxfclib/formats/tracker_imf/xx_tracker_imf.h"
#include "../tracker_mod/xx_eighth_media.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[832],b[384],q[8];uint32_t no,np,ni,i,j,k;fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};char label[40];
 if(!fd_get(&c,h,832) || xx_rt_memcmp(h+60,"IM10",4) || !(no=xx_data_get_u16(h+32, 2, 0, false)) || no>256 || !(np=xx_data_get_u16(h+34, 2, 0, false)) || np>256 || (ni=xx_data_get_u16(h+36, 2, 0, false))>256 || (xx_data_get_u16(h+38, 2, 0, false)&~1U) || !h[48] || h[49]<32 || h[50]>64 || h[51]<4 || h[51]>127) return false;
 for(i=0;i<32;++i) { if(h[64+i*16+15]>2) return false; } for(i=0;i<no;++i) if(h[576+i]>=np && h[576+i]!=255) return false;if(!em_emit(f,s,"descriptor.bin",0,832,c.end)) return false;
 for(i=0;i<np;++i) {uint64_t at=c.at,end;uint32_t n,rows,row=0;if(!fd_get(&c,q,4) || (n=xx_data_get_u16(q, 2, 0, false))<4 || !(rows=xx_data_get_u16(q+2, 2, 0, false)) || rows>256 || !fd_range(at,n,c.end)) return false;end=at+n;{fd_cursor d=c;d.end=end;while(d.at<end) {uint32_t tag;if(!fd_get(&d,q,1)) return false;tag=q[0];if(!tag) {if(++row>rows) return false;continue;}if(row>=rows) return false;if(tag&0x20) {if(!fd_get(&d,q,2) || q[1]>ni) return false;}if(tag&0x80 && !fd_get(&d,q,2)) return false;if(tag&0x40 && !fd_get(&d,q,2)) return false;}if(row!=rows) return false;}c.at=end;xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i);if(!em_emit(f,s,label,at,n,c.end)) return false; }
 for(i=0;i<ni;++i) {uint64_t at=c.at;uint32_t ns;if(!fd_get(&c,b,384) || xx_rt_memcmp(b+380,"II10",4) || (ns=xx_data_get_u16(b+378, 2, 0, false))>64 || xx_data_get_u16(b+376, 2, 0, false)>4095) return false;for(j=0;j<120;++j) if(ns && b[32+j]>=ns) return false;
  for(j=0;j<3;++j) {uint8_t *e=b+352+j*8;uint32_t prev=0;if(e[0]>16 || e[4]&~7U || (e[4]&2 && e[1]>=e[0]) || (e[4]&4 && (e[2]>=e[3] || e[3]>=e[0]))) return false;for(k=0;k<e[0];++k) {uint32_t x=xx_data_get_u16(b+160+j*64+k*4, 2, 0, false);if(k && x<=prev) return false;prev=x;} }
  xx_rt_snprintf(label,sizeof(label),"instrument-%u.bin",i+1);if(!em_emit(f,s,label,at,384,c.end)) return false;
  for(j=0;j<ns;++j) {uint32_t len,a,z,flags;at=c.at;if(!fd_get(&c,b,64) || xx_rt_memcmp(b+60,"IS10",4) || (len=xx_data_get_u32(b+16, 4, 0, false))>16777216 || !(k=xx_data_get_u32(b+28, 4, 0, false)) || k>384000 || b[32]>64 || ((flags=b[48])&~15U) || (flags&1 && ((a=xx_data_get_u32(b+20, 4, 0, false))>=(z=xx_data_get_u32(b+24, 4, 0, false)) || z>len)) || (flags&4 && (len&1))) return false;xx_rt_snprintf(label,sizeof(label),"sample-%u-%u-header.bin",i+1,j);if(!em_emit(f,s,label,at,64,c.end)) return false;if(len) {xx_rt_snprintf(label,sizeof(label),"sample-%u-%u.pcm",i+1,j);if(!em_take_emit(&c,s,label,len)) return false;} }
 }
 s->size=(int64_t)c.at;return true;
}

void xx_tracker_imf_init(xx_tracker_imf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_IMF,"imf"); } }
xx_tracker_imf *xx_tracker_imf_create(xx_io_device *d,int64_t b) { xx_tracker_imf *r=(xx_tracker_imf *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_imf_init(r,d,b); return r; }
void xx_tracker_imf_destroy(xx_tracker_imf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_imf_free(xx_tracker_imf *r) { if(r) { xx_tracker_imf_destroy(r); xx_mem_free(r); } }
bool xx_tracker_imf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_imf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
