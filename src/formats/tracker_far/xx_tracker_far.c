/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/far_load.c
 * Farandole Composer1.0 complete header/order tables, up to256 stored16-channel patterns and64 stored8/16-bit samples. Validates event references, row counts, loop extents and sample bitmap; exports descriptor/comment, patterns, instrument records and samples. Header extensions preserved; no playback.256MiB cap.
 */
#include "xxfclib/formats/tracker_far/xx_tracker_far.h"
#include "../common/xx_tracker_components.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 uint8_t h[98],orders[771],p[1024],map[8],inst[48],refs[8]={0}; uint16_t sizes[256]; uint32_t i,j,n,rows,len,count; uint64_t at; binary_cursor c={f,0,(uint64_t)pm_available(f),pd,0}; char label[40];
 if(!binary_get(&c,h,98) || xx_rt_memcmp(h,"FAR\xfe",4) || xx_rt_memcmp(h+44,"\r\n\x1a",3) || h[49]!=0x10 || h[75]>=16 || !binary_skip(&c,xx_data_get_u16(h+96, 2, 0, false)) || !binary_get(&c,orders,sizeof(orders))) return false;
 count=orders[257]; if(!count || orders[258]>=count || xx_data_get_u16(h+47, 2, 0, false)<c.at || !binary_skip(&c,xx_data_get_u16(h+47, 2, 0, false)-c.at)) return false;
 for(i=0;i<16;++i) if(h[50+i]>1 || h[76+i]>15) return false;
 for(i=0;i<256;++i) { n=sizes[i]=xx_data_get_u16(orders+259+i*2, 2, 0, false); if(n && (n<66 || (n-2)%64 || (n-2)/64>256)) return false; }
 if(!tracker_component_emit(f,s,"descriptor.bin",0,c.at,c.end)) return false;
 for(i=0;i<256;++i) if((n=sizes[i])!=0) { at=c.at; rows=(n-2)/64; if(!binary_get(&c,p,2) || p[0]>=rows || !binary_skip(&c,n-2)) return false;
  { binary_cursor d={f,at+2,at+n,pd,0}; while(d.at<d.end) { size_t part=d.end-d.at>sizeof(p) ? sizeof(p):(size_t)(d.end-d.at); if(!binary_get(&d,p,part)) return false; for(j=0;j<part;j+=4) {if(p[j]>72 || (p[j] && p[j+1]>=64) || p[j+2]>16) return false;if(p[j]) refs[p[j+1]/8]|=(uint8_t)(1U<<(p[j+1]%8));} } }
  xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i); if(!tracker_component_emit(f,s,label,at,n,c.end)) return false;
 }
 at=c.at; if(!binary_get(&c,map,8) || !tracker_component_emit(f,s,"sample-bitmap.bin",at,8,c.end)) return false;for(i=0;i<8;++i) if(refs[i]&~map[i]) return false;
 for(i=0;i<64;++i) if(map[i/8]&(1U<<(i%8))) { at=c.at; if(!binary_get(&c,inst,48) || (len=xx_data_get_u32(inst+32, 4, 0, false))>65536 || inst[46]>1 || (inst[47]&~15U) || ((inst[47]&8) && (xx_data_get_u32(inst+38, 4, 0, false)>=xx_data_get_u32(inst+42, 4, 0, false) || xx_data_get_u32(inst+42, 4, 0, false)>len)) || (inst[46] && ((len|xx_data_get_u32(inst+38, 4, 0, false)|xx_data_get_u32(inst+42, 4, 0, false))&1))) return false;
  xx_rt_snprintf(label,sizeof(label),"instrument-%u.bin",i); if(!tracker_component_emit(f,s,label,at,48,c.end)) return false; if(len) { xx_rt_snprintf(label,sizeof(label),"sample-%u.pcm",i); if(!tracker_component_take_emit(&c,s,label,len)) return false; }
 }
 s->size=(int64_t)c.at; return true;
}

void xx_tracker_far_init(xx_tracker_far *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_FAR,"far"); } }
xx_tracker_far *xx_tracker_far_create(xx_io_device *d,int64_t b) { xx_tracker_far *r=(xx_tracker_far *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_far_init(r,d,b); return r; }
void xx_tracker_far_destroy(xx_tracker_far *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_far_free(xx_tracker_far *r) { if(r) { xx_tracker_far_destroy(r); xx_mem_free(r); } }
bool xx_tracker_far_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_far_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
